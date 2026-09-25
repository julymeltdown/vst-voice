// Region waveform envelopes for the SING notes: the min/max pyramid answers every range exactly at
// block resolution, the cache hands out a view only for the exact publication it was built from,
// and the binding shows audio only for a current, matching render of the region's own voice.
#include "test_framework.hpp"

#include "seam/native_ui/region_envelope.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <random>
#include <thread>

namespace {

using namespace seam;
using native_ui::bindRegionWaveform;
using native_ui::EnvelopePair;
using native_ui::RegionEnvelope;
using native_ui::RegionEnvelopeCache;
using native_ui::RegionEnvelopeKey;
using native_ui::RegionWaveformRequest;

// The same answer computed sample by sample over the covering whole blocks.
std::optional<EnvelopePair> bruteForce(const std::vector<float>& mono, std::int64_t first,
                                       std::int64_t last) {
  const auto total = static_cast<std::int64_t>(mono.size());
  first = std::max<std::int64_t>(first, 0);
  last = std::min(last, total);
  if (last <= first) return std::nullopt;
  const auto block = static_cast<std::int64_t>(RegionEnvelope::kBlock);
  const auto from = (first / block) * block;
  const auto to = std::min(total, ((last + block - 1) / block) * block);
  EnvelopePair pair{std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
  for (auto i = from; i < to; ++i) {
    const auto sample = std::isfinite(mono[static_cast<std::size_t>(i)]) ? mono[static_cast<std::size_t>(i)] : 0.0F;
    pair.minimum = std::min(pair.minimum, sample);
    pair.maximum = std::max(pair.maximum, sample);
  }
  return pair;
}

std::shared_ptr<const native_ui::RegionEnvelopeView> waitFor(RegionEnvelopeCache& cache,
                                                             const RegionEnvelopeKey& key,
                                                             const rendering::SharedPcmBuffer& pcm) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto view = cache.request(key, pcm)) return view;
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  return nullptr;
}

std::shared_ptr<authoring::PublishedProjectAudio> readyAudio(domain::TrackId track,
                                                             domain::RegionId region,
                                                             std::uint64_t revision) {
  auto audio = std::make_shared<authoring::PublishedProjectAudio>();
  audio->projectRevision = revision;
  audio->requestId = 7U;
  audio->state = authoring::RenderState::Ready;
  audio->result.sampleRate = 48000U;
  audio->result.performanceTrackId = track;
  audio->result.performanceRegionId = region;
  audio->result.performanceAudioStartFrame = 96000;
  std::vector<float> mono(48000U);
  for (std::size_t i = 0U; i < mono.size(); ++i)
    mono[i] = 0.5F * static_cast<float>(std::sin(static_cast<double>(i) * 0.05));
  audio->result.performanceAudioMono = std::move(mono);
  return audio;
}

}  // namespace

TEST_CASE("region envelope ranges match the samples at block resolution, asymmetric and clipped") {
  std::mt19937 random{42U};
  std::uniform_real_distribution<float> value{-0.3F, 0.9F};
  std::vector<float> mono(100003U);
  for (auto& sample : mono) sample = value(random);
  mono[5000U] = std::numeric_limits<float>::quiet_NaN();
  mono[70000U] = -0.95F;
  const auto envelope = RegionEnvelope::build(mono);
  CHECK(envelope != nullptr);
  CHECK(envelope->sampleCount() == mono.size());
  CHECK(envelope->levelCount() > 3U);
  CHECK_NEAR(envelope->peak(), 0.95F, 1e-6);
  std::uniform_int_distribution<std::int64_t> position{-2000, 102000};
  for (int i = 0; i < 2000; ++i) {
    auto a = position(random);
    auto b = position(random);
    if (a > b) std::swap(a, b);
    const auto expected = bruteForce(mono, a, b);
    const auto actual = envelope->range(a, b);
    CHECK(expected.has_value() == actual.has_value());
    if (expected && actual) {
      CHECK(expected->minimum == actual->minimum);
      CHECK(expected->maximum == actual->maximum);
    }
  }
  CHECK(!envelope->range(200000, 300000).has_value());
  CHECK(!envelope->range(-50, 0).has_value());
  const auto empty = RegionEnvelope::build({});
  CHECK(empty != nullptr && !empty->range(0, 10).has_value());
}

TEST_CASE("the envelope cache serves only the exact key and never blocks the caller") {
  std::atomic<int> ready{0};
  RegionEnvelopeCache cache{[&ready] { ++ready; }};
  rendering::SharedPcmBuffer pcm{std::vector<float>(20000U, 0.25F)};
  RegionEnvelopeKey key{.projectRevision = 3U, .requestId = 1U,
                        .pcmIdentity = pcm.storageIdentity(), .pcmSamples = pcm.size(), .sampleRate = 48000U};
  CHECK(cache.request(key, pcm) == nullptr);  // building
  const auto view = waitFor(cache, key, pcm);
  CHECK(view != nullptr);
  CHECK(view->key == key);
  CHECK(ready.load() >= 1);
  // Any change of identity is a different envelope: nothing until it is built.
  auto other = key;
  other.requestId = 2U;
  CHECK(cache.request(other, pcm) == nullptr);
  CHECK(cache.request(key, pcm) == nullptr);  // the new request replaced the old view
  CHECK(waitFor(cache, key, pcm) != nullptr);
  cache.clear();
  CHECK(cache.request(key, pcm) == nullptr);
  // Many superseding requests in a row return at once; destruction joins every worker.
  rendering::SharedPcmBuffer large{std::vector<float>(4'000'000U, 0.1F)};
  const auto started = std::chrono::steady_clock::now();
  for (std::uint64_t i = 10U; i < 40U; ++i) {
    auto next = key;
    next.requestId = i;
    next.pcmIdentity = large.storageIdentity();
    next.pcmSamples = large.size();
    static_cast<void>(cache.request(next, large));
  }
  CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{2});
}

TEST_CASE("the note waveform shows only a current render of the region's own audio") {
  const domain::TrackId track{11U};
  const domain::RegionId region{12U};
  RegionEnvelopeCache cache;
  const auto bind = [&](std::shared_ptr<const authoring::PublishedProjectAudio> audio, bool stale,
                        std::uint64_t revision, domain::RegionId shown) {
    return bindRegionWaveform(RegionWaveformRequest{.audio = std::move(audio), .stale = stale,
                                                    .documentRevision = revision, .track = track,
                                                    .region = shown},
                              cache);
  };
  const auto none = bind(nullptr, false, 5U, region);
  CHECK(!none.shown() && !none.reason.empty());

  auto rendering = readyAudio(track, region, 5U);
  rendering->state = authoring::RenderState::Rendering;
  CHECK(bind(rendering, false, 5U, region).caption == "Rendering");

  auto failed = readyAudio(track, region, 5U);
  failed->state = authoring::RenderState::Failed;
  failed->diagnostic = "phoneme r a is not covered";
  const auto failure = bind(failed, false, 5U, region);
  CHECK(!failure.shown());
  CHECK(failure.reason.find("phoneme r a is not covered") != std::string::npos);

  const auto ready = readyAudio(track, region, 5U);
  CHECK(bind(ready, true, 5U, region).caption == "Out of date");
  CHECK(bind(ready, false, 6U, region).caption == "Out of date");
  CHECK(bind(ready, false, 5U, domain::RegionId{99U}).caption == "Other region");
  auto silent = readyAudio(track, region, 5U);
  silent->result.performanceAudioMono = std::vector<float>{};
  CHECK(bind(silent, false, 5U, region).caption == "No audio");

  // A current, matching render: drawing, then shown with its identity and origin.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  native_ui::RegionWaveform shown;
  while (std::chrono::steady_clock::now() < deadline) {
    shown = bind(ready, false, 5U, region);
    if (shown.shown()) break;
    CHECK(shown.caption == "Drawing");
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  CHECK(shown.shown());
  if (shown.shown()) {
    CHECK(shown.reason.empty());
    CHECK(shown.view->key.region == region);
    CHECK(shown.view->key.originFrame == 96000);
    CHECK(shown.view->key.pcmIdentity == ready->result.performanceAudioMono.storageIdentity());
    CHECK_NEAR(shown.view->envelope->peak(), 0.5F, 1e-3);
  }
  // A newer publication with other PCM never reuses the old envelope.
  const auto newer = readyAudio(track, region, 5U);
  CHECK(!bind(newer, false, 5U, region).shown());
}

