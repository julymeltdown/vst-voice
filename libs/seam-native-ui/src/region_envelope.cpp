#include "seam/native_ui/region_envelope.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace seam::native_ui {

namespace {

EnvelopePair merged(EnvelopePair a, EnvelopePair b) noexcept {
  return {std::min(a.minimum, b.minimum), std::max(a.maximum, b.maximum)};
}

constexpr EnvelopePair kEmpty{std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity()};

}  // namespace

std::shared_ptr<const RegionEnvelope> RegionEnvelope::build(std::span<const float> mono,
                                                            std::stop_token stop) {
  auto envelope = std::make_shared<RegionEnvelope>();
  envelope->samples_ = mono.size();
  if (mono.empty()) return envelope;
  std::vector<EnvelopePair> level;
  level.reserve((mono.size() + kBlock - 1U) / kBlock);
  float peak = 0.0F;
  for (std::size_t start = 0U; start < mono.size(); start += kBlock) {
    if ((start & 0xFFFFU) == 0U && stop.stop_requested()) return nullptr;
    const auto end = std::min(mono.size(), start + kBlock);
    auto pair = kEmpty;
    for (auto i = start; i < end; ++i) {
      const auto sample = std::isfinite(mono[i]) ? mono[i] : 0.0F;
      pair.minimum = std::min(pair.minimum, sample);
      pair.maximum = std::max(pair.maximum, sample);
    }
    peak = std::max({peak, std::abs(pair.minimum), std::abs(pair.maximum)});
    level.push_back(pair);
  }
  envelope->peak_ = peak;
  envelope->levels_.push_back(std::move(level));
  while (envelope->levels_.back().size() > 1U) {
    const auto& below = envelope->levels_.back();
    std::vector<EnvelopePair> above;
    above.reserve((below.size() + 3U) / 4U);
    for (std::size_t i = 0U; i < below.size(); i += 4U) {
      auto pair = below[i];
      for (auto j = i + 1U; j < std::min(below.size(), i + 4U); ++j) pair = merged(pair, below[j]);
      above.push_back(pair);
    }
    envelope->levels_.push_back(std::move(above));
  }
  return envelope;
}

std::optional<EnvelopePair> RegionEnvelope::range(std::int64_t first, std::int64_t last) const {
  queries_.fetch_add(1U, std::memory_order_relaxed);
  const auto total = static_cast<std::int64_t>(samples_);
  first = std::max<std::int64_t>(first, 0);
  last = std::min(last, total);
  if (levels_.empty() || last <= first) return std::nullopt;
  const auto block = static_cast<std::int64_t>(kBlock);
  auto lo = static_cast<std::size_t>(first / block);
  auto hi = static_cast<std::size_t>((last + block - 1) / block);
  auto pair = kEmpty;
  // Take whole coarse pairs where the range is aligned, finer pairs at its ragged edges.
  for (std::size_t level = 0U; lo < hi; ++level) {
    const auto& pairs = levels_[level];
    hi = std::min(hi, pairs.size());
    if (level + 1U == levels_.size()) {
      for (auto i = lo; i < hi; ++i) pair = merged(pair, pairs[i]);
      break;
    }
    while (lo < hi && lo % 4U != 0U) pair = merged(pair, pairs[lo++]);
    while (lo < hi && hi % 4U != 0U) pair = merged(pair, pairs[--hi]);
    lo /= 4U;
    hi /= 4U;
  }
  if (pair.minimum > pair.maximum) return std::nullopt;
  return pair;
}

RegionEnvelopeCache::RegionEnvelopeCache(std::function<void()> ready) : ready_(std::move(ready)) {}

RegionEnvelopeCache::~RegionEnvelopeCache() {
  stop();
}

void RegionEnvelopeCache::stop() {
  std::vector<Worker> workers;
  {
    std::lock_guard lock(mutex_);
    requested_.reset();
    built_.reset();
    if (worker_) workers.push_back(std::move(*worker_));
    worker_.reset();
    for (auto& worker : retired_) workers.push_back(std::move(worker));
    retired_.clear();
  }
  for (auto& worker : workers) {
    worker.thread.request_stop();
    if (worker.thread.joinable()) worker.thread.join();
  }
}

void RegionEnvelopeCache::retireLocked() {
  // Finished workers are joined at once (their join does not wait); a running one is stopped and
  // kept until it finishes, so no caller ever blocks on a build.
  std::erase_if(retired_, [](Worker& worker) {
    if (!worker.done->load(std::memory_order_acquire)) return false;
    if (worker.thread.joinable()) worker.thread.join();
    return true;
  });
  if (!worker_) return;
  worker_->thread.request_stop();
  retired_.push_back(std::move(*worker_));
  worker_.reset();
}

void RegionEnvelopeCache::clear() {
  std::lock_guard lock(mutex_);
  requested_.reset();
  built_.reset();
  retireLocked();
}

std::shared_ptr<const RegionEnvelopeView> RegionEnvelopeCache::request(
    const RegionEnvelopeKey& key, rendering::SharedPcmBuffer pcm) {
  std::lock_guard lock(mutex_);
  if (built_ != nullptr && built_->key == key) return built_;
  if (requested_ == key) return nullptr;  // still building
  requested_ = key;
  built_.reset();
  retireLocked();
  auto done = std::make_shared<std::atomic<bool>>(false);
  worker_ = Worker{
      done, std::jthread{[this, key, done, pcm = std::move(pcm)](std::stop_token stop) {
        auto envelope =
            RegionEnvelope::build(std::span<const float>{pcm.data(), pcm.size()}, stop);
        bool publish = false;
        if (envelope != nullptr) {
          std::lock_guard guard(mutex_);
          if (!stop.stop_requested() && requested_ == key) {
            built_ = std::make_shared<const RegionEnvelopeView>(RegionEnvelopeView{key, envelope});
            publish = true;
          }
        }
        // The callback runs without this cache's lock; the host may take its own.
        if (publish && ready_) ready_();
        done->store(true, std::memory_order_release);
      }}};
  return nullptr;
}

RegionWaveform bindRegionWaveform(const RegionWaveformRequest& request,
                                  RegionEnvelopeCache& cache) {
  const auto omit = [](std::string caption, std::string reason) {
    return RegionWaveform{nullptr, std::move(caption), std::move(reason)};
  };
  const auto& audio = request.audio;
  if (audio == nullptr) {
    cache.clear();
    return omit("No render", "Nothing has been rendered yet.");
  }
  switch (audio->state) {
    case authoring::RenderState::Ready: break;
    case authoring::RenderState::Failed:
      return omit("Render failed", audio->diagnostic.empty()
                                       ? std::string{"The last render failed."}
                                       : "The last render failed: " + audio->diagnostic);
    case authoring::RenderState::Cancelled:
      return omit("No render", "The last render was cancelled.");
    default:
      return omit("Rendering", "The waveform appears when the current render is ready.");
  }
  if (request.stale || audio->projectRevision != request.documentRevision)
    return omit("Out of date",
                "The score changed after the last render; the waveform returns with the next one.");
  const auto& result = audio->result;
  if (result.performanceTrackId != request.track || result.performanceRegionId != request.region)
    return omit("Other region", "The last render's own audio belongs to a different singer or region.");
  if (result.performanceAudioMono.empty())
    return omit("No audio", "This region's render has no audio of its own.");
  const RegionEnvelopeKey key{
      .projectRevision = audio->projectRevision,
      .requestId = audio->requestId,
      .publicationIdentity = audio->sourceIdentity.get(),
      .quality = static_cast<std::uint8_t>(audio->quality),
      .track = result.performanceTrackId,
      .region = result.performanceRegionId,
      .pcmIdentity = result.performanceAudioMono.storageIdentity(),
      .pcmSamples = result.performanceAudioMono.size(),
      .sampleRate = result.sampleRate,
      .originFrame = result.performanceAudioStartFrame,
  };
  auto view = cache.request(key, result.performanceAudioMono);
  if (view == nullptr) return omit("Drawing", "The waveform of the current render is being prepared.");
  return RegionWaveform{std::move(view), "Waveform", {}};
}

}  // namespace seam::native_ui
