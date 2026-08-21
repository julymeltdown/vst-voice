#include "seam/authoring/render_coordinator.hpp"

#include "seam/rendering/phrase_segmenter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <span>
#include <utility>

namespace seam::authoring {
namespace {

bool isAudible(const domain::VocalTrack& track, bool anySolo) noexcept {
  return !track.muted && (!anySolo || track.solo);
}

const rendering::TrackVoicebankSource* sourceFor(
    std::span<const rendering::TrackVoicebankSource> sources,
    domain::TrackId trackId) noexcept {
  const auto iterator = std::find_if(
      sources.begin(), sources.end(), [trackId](const auto& source) {
        return source.trackId == trackId;
      });
  return iterator == sources.end() ? nullptr : &*iterator;
}


}  // namespace

std::string_view renderStateName(RenderState state) noexcept {
  switch (state) {
    case RenderState::Idle: return "idle";
    case RenderState::Queued: return "queued";
    case RenderState::Rendering: return "rendering";
    case RenderState::Ready: return "ready";
    case RenderState::Stale: return "stale";
    case RenderState::Cancelled: return "cancelled";
    case RenderState::Failed: return "failed";
  }
  return "unknown";
}

std::string_view renderFailureName(RenderFailureKind failure) noexcept {
  switch (failure) {
    case RenderFailureKind::None: return "none";
    case RenderFailureKind::VoicebankMissing: return "voicebank-missing";
    case RenderFailureKind::VoicebankVersionMismatch:
      return "voicebank-version-mismatch";
    case RenderFailureKind::VoicebankContentHashMissing:
      return "voicebank-content-hash-missing";
    case RenderFailureKind::VoicebankContentMismatch:
      return "voicebank-content-mismatch";
    case RenderFailureKind::VoicebankUntrusted: return "voicebank-untrusted";
    case RenderFailureKind::InvalidProject: return "invalid-project";
    case RenderFailureKind::RenderFailed: return "render-failed";
    case RenderFailureKind::PublicationBusy: return "publication-busy";
  }
  return "unknown";
}

RealtimeProjectAudioPublication::ReadHandle::ReadHandle(
    const RealtimeProjectAudioPublication* owner,
    std::size_t slot,
    const PublishedProjectAudio* value) noexcept
    : owner_(owner), slot_(slot), value_(value) {}

RealtimeProjectAudioPublication::ReadHandle::ReadHandle(
    ReadHandle&& other) noexcept
    : owner_(other.owner_), slot_(other.slot_), value_(other.value_) {
  other.owner_ = nullptr;
  other.value_ = nullptr;
}

RealtimeProjectAudioPublication::ReadHandle&
RealtimeProjectAudioPublication::ReadHandle::operator=(
    ReadHandle&& other) noexcept {
  if (this == &other) return *this;
  release();
  owner_ = other.owner_;
  slot_ = other.slot_;
  value_ = other.value_;
  other.owner_ = nullptr;
  other.value_ = nullptr;
  return *this;
}

RealtimeProjectAudioPublication::ReadHandle::~ReadHandle() { release(); }

void RealtimeProjectAudioPublication::ReadHandle::release() noexcept {
  if (owner_ != nullptr) {
    owner_->slots_[slot_].readers.fetch_sub(1U, std::memory_order_release);
  }
  owner_ = nullptr;
  value_ = nullptr;
}

RealtimeProjectAudioPublication::RealtimeProjectAudioPublication() {
  slots_[0].audio = PublishedProjectAudio{};
}

RealtimeProjectAudioPublication::ReadHandle
RealtimeProjectAudioPublication::acquire() const noexcept {
  for (;;) {
    const auto slot = published_.load(std::memory_order_acquire);
    slots_[slot].readers.fetch_add(1U, std::memory_order_acquire);
    if (slot == published_.load(std::memory_order_acquire)) {
      return ReadHandle{this, slot, &slots_[slot].audio};
    }
    slots_[slot].readers.fetch_sub(1U, std::memory_order_release);
  }
}

bool RealtimeProjectAudioPublication::publish(PublishedProjectAudio audio) {
  std::scoped_lock lock(writerMutex_);
  const auto current = published_.load(std::memory_order_acquire);
  for (std::size_t offset = 1U; offset < kSlotCount; ++offset) {
    const auto candidate = (current + offset) % kSlotCount;
    if (slots_[candidate].readers.load(std::memory_order_acquire) != 0U) {
      continue;
    }
    slots_[candidate].audio = std::move(audio);
    published_.store(candidate, std::memory_order_release);
    return true;
  }
  return false;
}

AuthoringRenderCoordinator::AuthoringRenderCoordinator(
    std::filesystem::path cacheRoot,
    RenderCoordinatorHooks hooks)
    : cache_(std::make_unique<rendering::PcmCache>(std::move(cacheRoot))),
      hooks_(std::move(hooks)),
      worker_([this](std::stop_token token) { workerLoop(token); }) {}

AuthoringRenderCoordinator::~AuthoringRenderCoordinator() { shutdown(); }

void AuthoringRenderCoordinator::shutdown() noexcept {
  if (shutdown_.exchange(true, std::memory_order_acq_rel)) return;

  setCompletionCallback({});
  const auto revision =
      latestSubmittedRevision_.load(std::memory_order_acquire);
  bool hadPending = false;
  bool wasActive = false;
  {
    std::lock_guard lock(mutex_);
    activeStopSource_.request_stop();
    hadPending = pending_.has_value();
    wasActive = active_;
    pending_.reset();
  }
  worker_.request_stop();
  condition_.notify_all();
  if (worker_.joinable()) worker_.join();

  if (hadPending) {
    cancelled_.fetch_add(1U, std::memory_order_relaxed);
  }
  const auto current = progress();
  if (wasActive || hadPending || current.state == RenderState::Queued ||
      current.state == RenderState::Rendering) {
    updateProgress(RenderProgress{
        .state = RenderState::Cancelled,
        .requestedRevision = revision,
        .publishedRevision = current.publishedRevision,
        .completedPhrases = current.completedPhrases,
        .totalPhrases = current.totalPhrases,
        .fraction = current.fraction,
        .diagnostic = "Production render coordinator shut down",
    });
  }
}

void AuthoringRenderCoordinator::submit(
    domain::Project project,
    std::vector<rendering::TrackVoicebankSource> voicebanks,
    domain::TrackId activeTrack,
    domain::RegionId activeRegion,
    std::uint64_t revision,
    std::uint32_t sampleRate,
    rendering::RenderQuality quality,
    bool immediate) {
  if (shutdown_.load(std::memory_order_acquire)) return;
  sampleRate = std::clamp(sampleRate, 8000U, 192000U);
  {
    std::lock_guard lock(mutex_);
    if (shutdown_.load(std::memory_order_acquire)) return;
    latestSubmittedRevision_.store(revision, std::memory_order_release);
    activeStopSource_.request_stop();
    if (pending_.has_value()) {
      cancelled_.fetch_add(1U, std::memory_order_relaxed);
    }
    pending_ = Request{
        .project = std::move(project),
        .voicebanks = std::move(voicebanks),
        .activeTrack = activeTrack,
        .activeRegion = activeRegion,
        .revision = revision,
        .sampleRate = sampleRate,
        .quality = quality,
        .immediate = immediate,
    };
  }
  submitted_.fetch_add(1U, std::memory_order_relaxed);
  const auto previous = progress();
  if (previous.state == RenderState::Ready &&
      previous.publishedRevision != revision) {
    updateProgress(RenderProgress{
        .state = RenderState::Stale,
        .requestedRevision = revision,
        .publishedRevision = previous.publishedRevision,
        .completedPhrases = previous.completedPhrases,
        .totalPhrases = previous.totalPhrases,
        .fraction = previous.fraction,
        .diagnostic = "Previous audio is stale while the new revision renders",
    });
  }
  updateProgress(RenderProgress{
      .state = RenderState::Queued,
      .requestedRevision = revision,
      .publishedRevision = progress().publishedRevision,
      .completedPhrases = 0U,
      .totalPhrases = 0U,
      .fraction = 0.0,
      .diagnostic = "Production render request queued",
  });
  condition_.notify_all();
}

void AuthoringRenderCoordinator::cancel() noexcept {
  std::uint64_t revision = latestSubmittedRevision_.load(std::memory_order_acquire);
  bool cancelledPending = false;
  {
    std::lock_guard lock(mutex_);
    activeStopSource_.request_stop();
    cancelledPending = pending_.has_value();
    pending_.reset();
  }
  if (cancelledPending) {
    cancelled_.fetch_add(1U, std::memory_order_relaxed);
  }
  const auto current = progress();
  updateProgress(RenderProgress{
      .state = RenderState::Cancelled,
      .requestedRevision = revision,
      .publishedRevision = current.publishedRevision,
      .completedPhrases = current.completedPhrases,
      .totalPhrases = current.totalPhrases,
      .fraction = current.fraction,
      .diagnostic = "Production render request cancelled",
  });
  notifyCompletion();
}

std::shared_ptr<const PublishedProjectAudio>
AuthoringRenderCoordinator::latest() const {
  auto handle = publication_.acquire();
  return handle ? std::make_shared<PublishedProjectAudio>(*handle)
                : std::make_shared<PublishedProjectAudio>();
}

RenderProgress AuthoringRenderCoordinator::progress() const noexcept {
  std::lock_guard lock(progressMutex_);
  return progress_;
}

RenderCoordinatorStats AuthoringRenderCoordinator::stats() const noexcept {
  return RenderCoordinatorStats{
      .submitted = submitted_.load(std::memory_order_relaxed),
      .completed = completed_.load(std::memory_order_relaxed),
      .cancelled = cancelled_.load(std::memory_order_relaxed),
      .stale = stale_.load(std::memory_order_relaxed),
      .failed = failed_.load(std::memory_order_relaxed),
  };
}

void AuthoringRenderCoordinator::setCompletionCallback(
    std::function<void()> callback) {
  std::lock_guard lock(callbackMutex_);
  completionCallback_ = std::move(callback);
}

void AuthoringRenderCoordinator::workerLoop(std::stop_token stopToken) {
  while (!stopToken.stop_requested()) {
    Request request;
    std::stop_token requestToken;
    {
      std::unique_lock lock(mutex_);
      condition_.wait(lock, stopToken, [this] { return pending_.has_value(); });
      if (stopToken.stop_requested()) break;
      if (!pending_->immediate) {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds{20};
        static_cast<void>(condition_.wait_until(
            lock, deadline, [this, &stopToken] {
              return stopToken.stop_requested() ||
                     (pending_.has_value() && pending_->immediate);
            }));
        if (stopToken.stop_requested()) break;
      }
      request = std::move(*pending_);
      pending_.reset();
      activeStopSource_ = std::stop_source{};
      requestToken = activeStopSource_.get_token();
      active_ = true;
    }

    const auto totalPhrases = countPhrases(request.project);
    updateProgress(RenderProgress{
        .state = RenderState::Rendering,
        .requestedRevision = request.revision,
        .publishedRevision = progress().publishedRevision,
        .completedPhrases = 0U,
        .totalPhrases = totalPhrases,
        .fraction = 0.0,
        .diagnostic = "Production render in progress",
    });

    if (hooks_.beforeRender) {
      hooks_.beforeRender(request.revision, requestToken);
    }
    auto audio = requestToken.stop_requested()
                     ? std::optional<PublishedProjectAudio>{}
                     : render(request, requestToken);

    {
      std::lock_guard lock(mutex_);
      active_ = false;
    }
    if (stopToken.stop_requested()) break;
    if (!audio.has_value()) {
      cancelled_.fetch_add(1U, std::memory_order_relaxed);
      if (request.revision ==
          latestSubmittedRevision_.load(std::memory_order_acquire)) {
        const auto current = progress();
        updateProgress(RenderProgress{
            .state = RenderState::Cancelled,
            .requestedRevision = request.revision,
            .publishedRevision = current.publishedRevision,
            .completedPhrases = current.completedPhrases,
            .totalPhrases = current.totalPhrases,
            .fraction = current.fraction,
            .diagnostic = "Production render cancelled",
        });
        notifyCompletion();
      }
      continue;
    }

    if (request.revision !=
        latestSubmittedRevision_.load(std::memory_order_acquire)) {
      stale_.fetch_add(1U, std::memory_order_relaxed);
      continue;
    }
    const auto state = audio->state;
    const auto completedPhrases = audio->result.phraseCount;
    const auto diagnostic = audio->diagnostic;
    if (!publication_.publish(std::move(*audio))) {
      failed_.fetch_add(1U, std::memory_order_relaxed);
      updateProgress(RenderProgress{
          .state = RenderState::Failed,
          .requestedRevision = request.revision,
          .publishedRevision = progress().publishedRevision,
          .completedPhrases = 0U,
          .totalPhrases = totalPhrases,
          .fraction = 0.0,
          .diagnostic = "Realtime render publication has no free slot",
      });
      notifyCompletion();
      continue;
    }

    if (state == RenderState::Ready) {
      completed_.fetch_add(1U, std::memory_order_relaxed);
    } else {
      failed_.fetch_add(1U, std::memory_order_relaxed);
    }
    updateProgress(RenderProgress{
        .state = state,
        .requestedRevision = request.revision,
        .publishedRevision = request.revision,
        .completedPhrases = completedPhrases,
        .totalPhrases = totalPhrases,
        .fraction = state == RenderState::Ready ? 1.0 : 0.0,
        .diagnostic = diagnostic,
    });
    notifyCompletion();
  }
}

std::optional<PublishedProjectAudio> AuthoringRenderCoordinator::render(
    const Request& request,
    std::stop_token stopToken) {
  const auto checked = preflight(request);
  if (!checked.ok()) return makeFailureAudio(request, checked);

  rendering::ProductionProjectRenderer renderer;
  auto rendered = renderer.render(
      request.project, request.voicebanks, request.activeTrack,
      request.activeRegion, request.revision, request.sampleRate,
      request.quality, synthesis::PhraseRenderOptions{}, cache_.get(),
      stopToken);
  if (!rendered) {
    if (rendered.error().code == core::ErrorCode::Conflict &&
        stopToken.stop_requested()) {
      return std::nullopt;
    }
    PublishedProjectAudio failed = makeFailureAudio(
        request,
        PreflightResult{
            .failure = RenderFailureKind::RenderFailed,
            .diagnostic = rendered.error().context.empty()
                              ? rendered.error().message
                              : rendered.error().message + ": " +
                                    rendered.error().context,
            .activeVoicebankId = checked.activeVoicebankId,
            .activeVoicebankVersion = checked.activeVoicebankVersion,
            .activeVoicebankContentHash = checked.activeVoicebankContentHash,
        });
    return failed;
  }

  PublishedProjectAudio audio;
  audio.projectRevision = request.revision;
  audio.quality = request.quality;
  audio.state = RenderState::Ready;
  audio.failure = RenderFailureKind::None;
  audio.result = std::move(rendered).value();
  audio.diagnostic = "Production multi-track routing render completed";
  audio.activeVoicebankId = checked.activeVoicebankId;
  audio.activeVoicebankVersion = checked.activeVoicebankVersion;
  audio.activeVoicebankContentHash = checked.activeVoicebankContentHash;
  return audio;
}

AuthoringRenderCoordinator::PreflightResult
AuthoringRenderCoordinator::preflight(const Request& request) {
  PreflightResult result;
  const auto validation = request.project.validate();
  if (!validation) {
    result.failure = RenderFailureKind::InvalidProject;
    result.diagnostic = validation.error().context.empty()
                            ? validation.error().message
                            : validation.error().message + ": " +
                                  validation.error().context;
    return result;
  }

  const auto anySolo = std::any_of(
      request.project.vocalTracks().begin(),
      request.project.vocalTracks().end(),
      [](const domain::VocalTrack& track) {
        return track.solo && !track.muted;
      });
  bool foundAudible = false;
  for (const auto& track : request.project.vocalTracks()) {
    if (!isAudible(track, anySolo)) continue;
    foundAudible = true;
    if (track.voicebank.id.empty() || track.voicebank.version.empty()) {
      result.failure = RenderFailureKind::VoicebankMissing;
      result.diagnostic =
          "Voicebank ID and version are missing for audible track " +
          track.id.toString();
      return result;
    }
    if (track.voicebank.contentHash.empty()) {
      result.failure = RenderFailureKind::VoicebankContentHashMissing;
      result.diagnostic =
          "Voicebank content hash is missing for audible track " +
          track.id.toString();
      return result;
    }
    const auto* source = sourceFor(request.voicebanks, track.id);
    if (source == nullptr) {
      result.failure = RenderFailureKind::VoicebankMissing;
      result.diagnostic = "Voicebank source is missing for audible track " +
                          track.id.toString();
      return result;
    }
    if (source->manifest.id != track.voicebank.id) {
      result.failure = RenderFailureKind::VoicebankMissing;
      result.diagnostic = "Voicebank source ID does not match the project track";
      return result;
    }
    if (source->manifest.version != track.voicebank.version) {
      result.failure = RenderFailureKind::VoicebankVersionMismatch;
      result.diagnostic =
          "Voicebank source version does not match the project track";
      return result;
    }
    if (source->contentHash != track.voicebank.contentHash) {
      result.failure = RenderFailureKind::VoicebankContentMismatch;
      result.diagnostic =
          "Voicebank source content hash does not match the project track";
      return result;
    }
    if (source->trust == voicebank::VoicebankTrust::UntrustedInstalled) {
      result.failure = RenderFailureKind::VoicebankUntrusted;
      result.diagnostic = "Voicebank source is not trusted";
      return result;
    }
    if (track.id == request.activeTrack) {
      result.activeVoicebankId = source->manifest.id;
      result.activeVoicebankVersion = source->manifest.version;
      result.activeVoicebankContentHash = source->contentHash;
    }
  }
  if (!foundAudible) {
    result.failure = RenderFailureKind::InvalidProject;
    result.diagnostic = "Project contains no audible vocal track";
  }
  return result;
}


PublishedProjectAudio AuthoringRenderCoordinator::makeFailureAudio(
    const Request& request, const PreflightResult& preflight) {
  PublishedProjectAudio audio;
  audio.projectRevision = request.revision;
  audio.quality = request.quality;
  audio.state = RenderState::Failed;
  audio.failure = preflight.failure;
  audio.result.sampleRate = request.sampleRate;
  audio.result.channelCount = request.project.routing().deviceOutputChannels;
  audio.diagnostic = preflight.diagnostic;
  audio.activeVoicebankId = preflight.activeVoicebankId;
  audio.activeVoicebankVersion = preflight.activeVoicebankVersion;
  audio.activeVoicebankContentHash = preflight.activeVoicebankContentHash;
  return audio;
}

std::size_t AuthoringRenderCoordinator::countPhrases(
    const domain::Project& project) {
  const auto anySolo = std::any_of(
      project.vocalTracks().begin(), project.vocalTracks().end(),
      [](const domain::VocalTrack& track) {
        return track.solo && !track.muted;
      });
  rendering::PhraseSegmenter segmenter;
  std::size_t result = 0U;
  for (const auto& track : project.vocalTracks()) {
    if (!isAudible(track, anySolo)) continue;
    for (const auto& region : track.regions) {
      if (region.notes.empty()) continue;
      const auto phrases = segmenter.segment(region);
      if (phrases) result += phrases.value().size();
    }
  }
  return result;
}

void AuthoringRenderCoordinator::updateProgress(RenderProgress value) noexcept {
  std::lock_guard lock(progressMutex_);
  progress_ = std::move(value);
}

void AuthoringRenderCoordinator::notifyCompletion() {
  std::function<void()> callback;
  {
    std::lock_guard lock(callbackMutex_);
    callback = completionCallback_;
  }
  if (callback) callback();
}

}  // namespace seam::authoring
