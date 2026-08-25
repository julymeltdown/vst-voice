#include "seam/authoring/authoring_runtime.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <utility>

namespace seam::authoring {
namespace {

rendering::TrackVoicebankSource sourceFor(
    domain::TrackId trackId,
    const voicebank::VoicebankCandidate& candidate) {
  return rendering::TrackVoicebankSource{
      .trackId = trackId,
      .manifest = candidate.manifest,
      .bankRoot = candidate.bankRoot,
      .contentHash = candidate.contentHash,
      .trust = candidate.trust,
  };
}

bool isResolved(const TrackVoicebankState& state) noexcept {
  return state.resolution.resolved();
}

std::string_view renderDiagnosticCode(RenderFailureKind failure) noexcept {
  switch (failure) {
    case RenderFailureKind::VoicebankMissing:
    case RenderFailureKind::VoicebankVersionMismatch:
    case RenderFailureKind::VoicebankContentHashMissing:
    case RenderFailureKind::VoicebankContentMismatch:
      return "BANK_MISSING";
    case RenderFailureKind::VoicebankUntrusted:
      return "BANK_UNTRUSTED";
    case RenderFailureKind::InvalidProject:
    case RenderFailureKind::RenderFailed:
    case RenderFailureKind::PublicationBusy:
      return "RENDER_FAILED";
    case RenderFailureKind::None:
      return {};
  }
  return {};
}

}  // namespace

AuthoringRuntime::AuthoringRuntime(std::unique_ptr<ProjectDocument> document,
                                   AuthoringRuntimeConfig config)
    : document_(std::move(document)),
      config_(std::move(config)),
      voicebanks_(config_.voicebankRoots,
                  config_.allowDevelopmentVoicebanks),
      renderer_(config_.cacheRoot),
      seamPreviewRenderer_(config_.cacheRoot / "seam-previews"),
      transport_(TransportConfig{
          .sampleRate = config_.previewSampleRate,
          .outputChannels = config_.outputChannels,
      }),
      technicalEdits_(
          *document_, domain::RegionId{},
          [this] { return currentTechnicalRenderView(); },
          [this] { handleDocumentChanged(); }),
      previewSampleRate_(config_.previewSampleRate) {
  renderer_.setCompletionCallback([this] { publishCompletedAudio(); });
  seamPreviewRenderer_.setCompletionCallback(
      [this] { publishCompletedSeamPreview(); });
  previewWorker_ = std::jthread(
      [this](std::stop_token stopToken) { previewWorkerLoop(stopToken); });
}

AuthoringRuntime::~AuthoringRuntime() { shutdown(); }

void AuthoringRuntime::recordDiagnostic(const core::Error& error) {
  auto diagnostic = DiagnosticRegistry::fromError(error);
  std::lock_guard lock(diagnosticsMutex_);
  const auto existing = std::find_if(
      diagnostics_.begin(), diagnostics_.end(),
      [&diagnostic](const auto& value) {
        return value.code == diagnostic.code &&
               value.messageKey == diagnostic.messageKey;
      });
  if (existing != diagnostics_.end()) {
    existing->occurrenceCount += diagnostic.occurrenceCount;
  } else {
    diagnostics_.push_back(std::move(diagnostic));
  }
}

void AuthoringRuntime::recordRenderFailure(RenderFailureKind failure,
                                            std::string message) {
  const auto code = renderDiagnosticCode(failure);
  if (code.empty()) return;
  Diagnostic diagnostic{
      .code = std::string{code},
      .severity = DiagnosticRegistry::severity(code),
      .messageKey = "render." + std::string{renderFailureName(failure)},
      .affectedIds = {},
      .actions = DiagnosticRegistry::actions(code),
      .occurrenceCount = 1U,
  };
  if (!message.empty()) diagnostic.messageKey += "." + std::move(message);
  std::lock_guard lock(diagnosticsMutex_);
  const auto existing = std::find_if(
      diagnostics_.begin(), diagnostics_.end(),
      [&diagnostic](const auto& value) {
        return value.code == diagnostic.code &&
               value.messageKey == diagnostic.messageKey;
      });
  if (existing != diagnostics_.end()) {
    existing->occurrenceCount += diagnostic.occurrenceCount;
  } else {
    diagnostics_.push_back(std::move(diagnostic));
  }
}

void AuthoringRuntime::clearRenderDiagnostics() noexcept {
  static constexpr std::array<std::string_view, 3> codes{
      "BANK_MISSING", "BANK_UNTRUSTED", "RENDER_FAILED"};
  std::lock_guard lock(diagnosticsMutex_);
  std::erase_if(diagnostics_, [](const auto& diagnostic) {
    return std::find(codes.begin(), codes.end(), diagnostic.code) !=
           codes.end();
  });
}

core::Result<void> AuthoringRuntime::initialize() {
  if (document_ == nullptr) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Authoring runtime requires a project document");
  }
  if (config_.cacheRoot.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Authoring runtime cache root cannot be empty");
  }
  auto refreshed = voicebanks_.refresh();
  if (!refreshed) return refreshed;
  if (config_.enableTransport) {
    auto started = transport_.start();
    if (!started) return started;
  }

  const auto states = voicebanks_.resolveAll(document_->session().project());
  const auto [trackId, regionId] = firstRenderableSelection(
      document_->session().project(), states);
  const auto hadRenderableSelection = trackId.valid();
  selectedTrack_ = trackId;
  selectedRegion_ = regionId;
  if (!selectedTrack_.valid() &&
      !document_->session().project().vocalTracks().empty()) {
    const auto& fallbackTrack =
        document_->session().project().vocalTracks().front();
    selectedTrack_ = fallbackTrack.id;
    selectedRegion_ = fallbackTrack.regions.empty()
                          ? domain::RegionId{}
                          : fallbackTrack.regions.front().id;
  }
  technicalEdits_.setRegion(selectedRegion_);
  if (!hadRenderableSelection &&
      !document_->session().project().vocalTracks().empty()) {
    recordRenderFailure(RenderFailureKind::VoicebankMissing,
                        "voicebank-missing");
  }
  initialized_ = true;
  requestPreview(true);
  return core::success();
}

void AuthoringRuntime::shutdown() noexcept {
  if (!document_) return;
  setCompletionCallback({});
  {
    std::lock_guard lock(previewMutex_);
    pendingPreview_.reset();
    ++previewGeneration_;
  }
  previewWorker_.request_stop();
  previewCondition_.notify_all();
  if (previewWorker_.joinable()) previewWorker_.join();
  seamPreviewActive_.store(false, std::memory_order_release);
  seamPreviewReady_.store(false, std::memory_order_release);
  seamPreviewRenderer_.setCompletionCallback({});
  seamPreviewRenderer_.shutdown();
  renderer_.setCompletionCallback({});
  renderer_.shutdown();
  if (config_.enableTransport) transport_.shutdown();
  initialized_ = false;
}

core::Result<void> AuthoringRuntime::selectTrack(domain::TrackId trackId) {
  const auto* track = document_->session().project().findVocalTrack(trackId);
  if (track == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Selected vocal track is missing");
  }
  selectedTrack_ = trackId;
  if (track->findRegion(selectedRegion_) == nullptr) {
    selectedRegion_ = track->regions.empty() ? domain::RegionId{}
                                             : track->regions.front().id;
    technicalEdits_.setRegion(selectedRegion_);
  }
  return core::success();
}

core::Result<void> AuthoringRuntime::selectRegion(domain::RegionId regionId) {
  const auto* region = document_->session().project().findRegion(regionId);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Selected vocal region is missing");
  }
  const auto track = std::find_if(
      document_->session().project().vocalTracks().begin(),
      document_->session().project().vocalTracks().end(),
      [regionId](const domain::VocalTrack& value) {
        return value.findRegion(regionId) != nullptr;
      });
  if (track == document_->session().project().vocalTracks().end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Selected vocal region has no owning track");
  }
  selectedTrack_ = track->id;
  selectedRegion_ = regionId;
  technicalEdits_.setRegion(regionId);
  return core::success();
}

core::Result<void> AuthoringRuntime::execute(
    std::unique_ptr<application::ICommand> command) {
  const auto impact = command == nullptr
                          ? application::CommandImpact{}
                          : command->impact();
  auto result = document_->execute(std::move(command));
  if (!result) recordDiagnostic(result.error());
  document_->synchronizeDirtyState();
  if (result && impact.scope != application::CommandAudioImpact::ViewOnly &&
      impact.scope != application::CommandAudioImpact::MetadataOnly) {
    seamPreviewRenderer_.cancel();
    seamPreviewActive_.store(false, std::memory_order_release);
    seamPreviewReady_.store(false, std::memory_order_release);
    requestPreview(false, impact);
  }
  return result;
}

core::Result<void> AuthoringRuntime::undo() {
  auto result = document_->undo();
  if (!result) recordDiagnostic(result.error());
  if (result) {
    document_->synchronizeDirtyState();
    const auto& impact = document_->lastImpact();
    if (impact.scope != application::CommandAudioImpact::ViewOnly &&
        impact.scope != application::CommandAudioImpact::MetadataOnly) {
      seamPreviewRenderer_.cancel();
      seamPreviewActive_.store(false, std::memory_order_release);
      seamPreviewReady_.store(false, std::memory_order_release);
      requestPreview(false, document_->lastImpact());
    }
  }
  return result;
}

core::Result<void> AuthoringRuntime::redo() {
  auto result = document_->redo();
  if (!result) recordDiagnostic(result.error());
  if (result) {
    document_->synchronizeDirtyState();
    const auto& impact = document_->lastImpact();
    if (impact.scope != application::CommandAudioImpact::ViewOnly &&
        impact.scope != application::CommandAudioImpact::MetadataOnly) {
      seamPreviewRenderer_.cancel();
      seamPreviewActive_.store(false, std::memory_order_release);
      seamPreviewReady_.store(false, std::memory_order_release);
      requestPreview(false, document_->lastImpact());
    }
  }
  return result;
}

core::Result<void> AuthoringRuntime::previewSeam(domain::PhonemeKey key,
                                                 bool alternate) {
  if (!initialized_ || !config_.enableTransport) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Transient seam preview requires an active transport");
  }
  if (!key.noteId.valid()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Transient seam preview requires a valid phoneme key");
  }
  auto request = makePreviewRequest(application::CommandImpact{
      .scope = application::CommandAudioImpact::PhraseAudio,
      .regionIds = {selectedRegion_},
      .noteIds = {key.noteId},
  });
  if (!request.has_value()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Transient seam preview has no renderable selection");
  }
  auto* region = request->project.findRegion(selectedRegion_);
  if (region == nullptr || region->findNote(key.noteId) == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Transient seam preview target is not in the active region");
  }
  if (alternate) {
    std::erase_if(region->seamOverrides,
                  [key](const auto& value) {
                    return value.incomingStartKey == key;
                  });
    seamPreviewActive_.store(true, std::memory_order_release);
    seamPreviewReady_.store(false, std::memory_order_release);
    seamPreviewRenderer_.submit(
        std::move(request->project), std::move(request->voicebanks),
        request->activeTrack, request->activeRegion, request->revision,
        request->sampleRate, request->quality, true,
        std::move(request->impact));
    return core::success();
  }

  seamPreviewRenderer_.cancel();
  seamPreviewActive_.store(false, std::memory_order_release);
  seamPreviewReady_.store(false, std::memory_order_release);
  auto canonical = renderer_.acquire();
  if (!canonical || canonical->state != RenderState::Ready) {
    return core::failure(core::ErrorCode::Conflict,
                         "Canonical audio is not ready for seam A/B restore");
  }
  return transport_.publishAudio(std::move(canonical));
}

core::Result<void> AuthoringRuntime::setPreviewSampleRate(
    std::uint32_t sampleRate) {
  if (sampleRate < 8000U || sampleRate > 192000U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Preview sample rate must be between 8000 and 192000 Hz");
  }
  previewSampleRate_ = sampleRate;
  return core::success();
}

core::Result<void> AuthoringRuntime::reconfigureAudio(
    std::uint32_t sampleRate, std::uint8_t outputChannels,
    std::size_t blockFrames) {
  if (sampleRate < 8000U || sampleRate > 192000U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Preview sample rate must be between 8000 and 192000 Hz");
  }
  if (outputChannels == 0U || outputChannels > 8U || blockFrames == 0U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Audio output format is outside supported bounds");
  }
  auto config = transport_.config();
  config.sampleRate = sampleRate;
  config.outputChannels = outputChannels;
  config.blockFrames = blockFrames;
  const auto reconfigured = transport_.reconfigure(config);
  if (!reconfigured) return reconfigured;
  previewSampleRate_ = sampleRate;
  config_.previewSampleRate = sampleRate;
  config_.outputChannels = outputChannels;
  requestPreview(true);
  return core::success();
}

void AuthoringRuntime::setRenderQuality(
    rendering::RenderQuality quality) {
  if (renderQuality_ == quality) return;
  renderQuality_ = quality;
  requestPreview(true);
}

void AuthoringRuntime::setCompletionCallback(
    std::function<void()> callback) {
  std::lock_guard lock(callbackMutex_);
  completionCallback_ = std::move(callback);
}

void AuthoringRuntime::handleDocumentChanged() {
  document_->synchronizeDirtyState();
  seamPreviewRenderer_.cancel();
  seamPreviewActive_.store(false, std::memory_order_release);
  seamPreviewReady_.store(false, std::memory_order_release);
  requestPreview(false, document_->session().lastImpact());
}

void AuthoringRuntime::requestPreview(bool immediate,
                                      application::CommandImpact impact) {
  if (!initialized_ || document_ == nullptr) return;

  auto request = makePreviewRequest(std::move(impact));
  if (!request.has_value()) return;
  if (immediate) {
    {
      std::lock_guard lock(previewMutex_);
      pendingPreview_.reset();
      ++previewGeneration_;
    }
    submitPreview(std::move(*request), true);
    return;
  }

  {
    std::lock_guard lock(previewMutex_);
    pendingPreview_ = std::move(*request);
    previewDeadline_ = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds{20};
    ++previewGeneration_;
  }
  previewCondition_.notify_all();
}

std::optional<AuthoringRuntime::PreviewRequest>
AuthoringRuntime::makePreviewRequest(application::CommandImpact impact) const {
  auto project = document_->session().project();
  if (document_->identity().projectPath.has_value()) {
    const auto projectRoot = document_->identity().projectPath->parent_path();
    for (auto& track : project.audioTracks()) {
      if (track.mediaOwnership == domain::MediaOwnership::ProjectCopy &&
          !track.mediaPath.empty() &&
          std::filesystem::path{track.mediaPath}.is_relative()) {
        track.mediaPath =
            (projectRoot / std::filesystem::path{track.mediaPath})
                .lexically_normal()
                .string();
      }
    }
  }
  const auto states = voicebanks_.resolveAll(project);
  const auto hasBackingAudio = std::any_of(
      project.audioTracks().begin(), project.audioTracks().end(),
      [](const auto& track) { return !track.mediaPath.empty(); });
  std::vector<rendering::TrackVoicebankSource> sources;
  sources.reserve(states.size());
  for (const auto& state : states) {
    if (state.resolution.resolved()) {
      sources.push_back(sourceFor(state.trackId,
                                  *state.resolution.candidate));
      continue;
    }
    if (auto* track = project.findVocalTrack(state.trackId); track != nullptr) {
      track->muted = true;
    }
  }

  auto activeTrack = selectedTrack_;
  auto activeRegion = selectedRegion_;
  const auto selectedResolved = std::any_of(
      states.begin(), states.end(), [activeTrack](const auto& state) {
        return state.trackId == activeTrack && state.resolution.resolved();
      });
  if (!selectedResolved || project.findRegion(activeRegion) == nullptr) {
    std::tie(activeTrack, activeRegion) =
        firstRenderableSelection(project, states);
  }

  if ((!activeTrack.valid() || !activeRegion.valid()) && !hasBackingAudio) {
    return std::nullopt;
  }

  const auto* region = project.findRegion(activeRegion);
  const auto hasNotes = region != nullptr && !region->notes.empty();
  if (!hasNotes && !hasBackingAudio) return std::nullopt;

  return PreviewRequest{
      .project = std::move(project),
      .voicebanks = std::move(sources),
      .activeTrack = activeTrack,
      .activeRegion = activeRegion,
      .revision = document_->session().revision(),
      .sampleRate = previewSampleRate_,
      .quality = renderQuality_,
      .impact = std::move(impact),
  };
}

void AuthoringRuntime::submitPreview(PreviewRequest request, bool immediate) {
  renderer_.submit(std::move(request.project), std::move(request.voicebanks),
                   request.activeTrack, request.activeRegion, request.revision,
                   request.sampleRate, request.quality, immediate,
                   std::move(request.impact));
}

void AuthoringRuntime::previewWorkerLoop(std::stop_token stopToken) {
  while (!stopToken.stop_requested()) {
    std::optional<PreviewRequest> request;
    {
      std::unique_lock lock(previewMutex_);
      previewCondition_.wait(lock, stopToken,
                             [this] { return pendingPreview_.has_value(); });
      if (stopToken.stop_requested()) break;

      while (pendingPreview_.has_value() && !stopToken.stop_requested()) {
        const auto generation = previewGeneration_;
        const auto deadline = previewDeadline_;
        const auto changed = previewCondition_.wait_until(
            lock, deadline, [&] {
              return stopToken.stop_requested() || !pendingPreview_.has_value() ||
                     previewGeneration_ != generation;
            });
        if (stopToken.stop_requested()) break;
        if (changed) continue;
        request = std::move(pendingPreview_);
        pendingPreview_.reset();
        break;
      }
    }
    if (stopToken.stop_requested()) break;
    if (request.has_value()) submitPreview(std::move(*request), false);
  }
}

TechnicalRenderView AuthoringRuntime::currentTechnicalRenderView() const {
  TechnicalRenderView view;
  const auto audio = renderer_.latest();
  if (audio && audio->state == RenderState::Ready) {
    view.units.reserve(audio->result.activeUnitPlan.size());
    for (const auto& entry : audio->result.activeUnitPlan) {
      view.units.push_back(TechnicalUnitView{
          .entry = entry,
          .usedFallback = false,
          .diagnostic = {},
      });
    }
    return view;
  }

  std::lock_guard lock(technicalViewMutex_);
  view.units = lastTechnicalUnits_;
  return view;
}

std::pair<domain::TrackId, domain::RegionId>
AuthoringRuntime::firstRenderableSelection(
    const domain::Project& project,
    const std::vector<TrackVoicebankState>& states) const {
  for (const auto& state : states) {
    if (!isResolved(state)) continue;
    const auto* track = project.findVocalTrack(state.trackId);
    if (track == nullptr || track->regions.empty()) continue;
    const auto region = std::find_if(
        track->regions.begin(), track->regions.end(),
        [](const domain::VocalRegion& value) { return !value.notes.empty(); });
    return {track->id,
            region == track->regions.end() ? track->regions.front().id
                                           : region->id};
  }
  return {domain::TrackId{}, domain::RegionId{}};
}

void AuthoringRuntime::publishCompletedAudio() {
  const auto progress = renderer_.progress();
  if (progress.state != RenderState::Ready) {
    if (progress.state == RenderState::Failed) {
      recordRenderFailure(progress.failure,
                          std::string{renderFailureName(progress.failure)});
    }
    std::function<void()> callback;
    {
      std::lock_guard lock(callbackMutex_);
      callback = completionCallback_;
    }
    if (callback) callback();
    return;
  }
  clearRenderDiagnostics();
  auto handle = renderer_.acquire();
  if (!handle || handle->state != RenderState::Ready) return;
  {
    std::lock_guard lock(technicalViewMutex_);
    lastTechnicalUnits_.clear();
    lastTechnicalUnits_.reserve(handle->result.activeUnitPlan.size());
    for (const auto& entry : handle->result.activeUnitPlan) {
      lastTechnicalUnits_.push_back(TechnicalUnitView{
          .entry = entry,
          .usedFallback = false,
          .diagnostic = {},
      });
    }
  }
  if (config_.enableTransport &&
      !seamPreviewActive_.load(std::memory_order_acquire)) {
    static_cast<void>(transport_.publishAudio(std::move(handle)));
  }
  std::function<void()> callback;
  {
    std::lock_guard lock(callbackMutex_);
    callback = completionCallback_;
  }
  if (callback) callback();
}

void AuthoringRuntime::publishCompletedSeamPreview() {
  const auto progress = seamPreviewRenderer_.progress();
  if (progress.state != RenderState::Ready) {
    seamPreviewActive_.store(false, std::memory_order_release);
    seamPreviewReady_.store(false, std::memory_order_release);
    return;
  }
  auto handle = seamPreviewRenderer_.acquire();
  if (!handle || handle->state != RenderState::Ready) {
    seamPreviewActive_.store(false, std::memory_order_release);
    seamPreviewReady_.store(false, std::memory_order_release);
    return;
  }
  const auto published = transport_.publishAudio(std::move(handle));
  if (!published) {
    seamPreviewActive_.store(false, std::memory_order_release);
    seamPreviewReady_.store(false, std::memory_order_release);
    return;
  }
  seamPreviewReady_.store(true, std::memory_order_release);
  std::function<void()> callback;
  {
    std::lock_guard lock(callbackMutex_);
    callback = completionCallback_;
  }
  if (callback) callback();
}

}  // namespace seam::authoring
