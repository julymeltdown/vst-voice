#include "seam/authoring/authoring_runtime.hpp"

#include <algorithm>
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
    case RenderFailureKind::NeuralSourceMissing:
    case RenderFailureKind::PublicationBusy:
      return "RENDER_FAILED";
    case RenderFailureKind::None:
      return {};
  }
  return {};
}

Diagnostic renderDiagnostic(RenderFailureKind failure, std::string_view message) {
  const auto code = renderDiagnosticCode(failure);
  Diagnostic diagnostic{
      .code = std::string{code},
      .severity = DiagnosticRegistry::severity(code),
      .messageKey = "render." + std::string{renderFailureName(failure)},
      .affectedIds = {},
      .actions = DiagnosticRegistry::actions(code),
      .occurrenceCount = 1U,
  };
  diagnostic.setDetail(message);
  return diagnostic;
}

}  // namespace

AuthoringRuntime::AuthoringRuntime(std::unique_ptr<ProjectDocument> document,
                                   AuthoringRuntimeConfig config)
    : document_(std::move(document)),
      config_(std::move(config)),
      voicebanks_(config_.voicebankRoots,
                  config_.allowDevelopmentVoicebanks),
      renderer_(config_.cacheRoot, config_.renderHooks),
      seamPreviewRenderer_(config_.cacheRoot / "seam-previews"),
      performanceAuditionRenderer_(config_.cacheRoot / "performance-auditions"),
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
  performanceAuditionRenderer_.setCompletionCallback(
      [this] { publishCompletedPerformanceAudition(); });
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
        return value.value.sameIssueAs(diagnostic);
      });
  if (existing != diagnostics_.end()) {
    existing->value.addOccurrences(diagnostic.occurrenceCount);
  } else {
    diagnostics_.push_back(RecordedDiagnostic{std::move(diagnostic), false});
  }
}

void AuthoringRuntime::recordRenderFailure(RenderFailureKind failure,
                                            std::string message) {
  if (renderDiagnosticCode(failure).empty()) return;
  auto diagnostic = renderDiagnostic(failure, message);
  std::lock_guard lock(diagnosticsMutex_);
  const auto existing = std::find_if(
      diagnostics_.begin(), diagnostics_.end(),
      [&diagnostic](const auto& value) {
        return value.value.sameIssueAs(diagnostic);
      });
  if (existing != diagnostics_.end()) {
    existing->value.addOccurrences(diagnostic.occurrenceCount);
  } else {
    diagnostics_.push_back(RecordedDiagnostic{std::move(diagnostic), true});
  }
}

void AuthoringRuntime::clearRenderDiagnostics() noexcept {
  std::lock_guard lock(diagnosticsMutex_);
  std::erase_if(diagnostics_, [](const auto& recorded) { return recorded.attemptOutcome; });
}

std::vector<Diagnostic> AuthoringRuntime::diagnostics() const {
  // The outcome of a render attempt describes that attempt. While a newer attempt is queued or
  // rendering, saying the render did not complete would be wrong: it is under way. The outcome
  // stays on record, so it comes back if that attempt is cancelled and goes for good when one
  // succeeds. A missing vocal bank is not an outcome: no attempt makes it any less missing, so it
  // is reported whatever a render is doing.
  const auto state = renderer_.progress().state;
  const auto attemptInFlight =
      state == RenderState::Queued || state == RenderState::Rendering;
  std::lock_guard lock(diagnosticsMutex_);
  std::vector<Diagnostic> result;
  result.reserve(diagnostics_.size() + 1U);
  const auto add = [&result](const Diagnostic& diagnostic) {
    const auto known = std::any_of(
        result.begin(), result.end(),
        [&diagnostic](const auto& value) { return value.sameIssueAs(diagnostic); });
    if (!known) result.push_back(diagnostic);
  };
  if (bankUnavailable_)
    add(renderDiagnostic(RenderFailureKind::VoicebankMissing, "voicebank-missing"));
  for (const auto& recorded : diagnostics_) {
    if (recorded.attemptOutcome && attemptInFlight) continue;
    add(recorded.value);
  }
  return result;
}

void AuthoringRuntime::noteBankAvailability(bool unavailable) {
  std::lock_guard lock(diagnosticsMutex_);
  bankUnavailable_ = unavailable;
}

void AuthoringRuntime::settleWithNothingToRender() {
  // The attempt outcomes go first and the coordinator last: resetting the coordinator tells the
  // completion callback, which reads them.
  clearRenderDiagnostics();
  if (config_.enableTransport) {
    // The completion callback publishes a finished render to the transport under this mutex, after
    // checking that the render is still current; taking it here means a publication that was
    // already under way finishes before the audio is dropped, and a later one fails that check.
    std::lock_guard lock(performanceAuditionMutex_);
    static_cast<void>(transport_.clearAudio());
  }
  renderer_.resetToIdle();
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
  const auto migrated = voicebanks_.migrateLegacyStyles(document_->session().project());
  if (!migrated) return core::Result<void>{migrated.error()};
  if (config_.enableTransport) {
    auto started = transport_.start();
    if (!started) return started;
  }

  const auto states = voicebanks_.resolveAll(document_->session().project());
  const auto [trackId, regionId] = firstRenderableSelection(
      document_->session().project(), states);
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
  // Whether a missing bank is reported is decided where a render request is composed, so that it
  // follows the score and the banks from then on and not only from this first look.
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
  performanceAuditionActive_.store(false, std::memory_order_release);
  performanceAuditionReady_.store(false, std::memory_order_release);
  performanceAuditionRenderer_.setCompletionCallback({});
  performanceAuditionRenderer_.shutdown();
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
  if (trackId != selectedTrack_)
    static_cast<void>(stopPerformanceAudition());
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
  if (regionId != selectedRegion_)
    static_cast<void>(stopPerformanceAudition());
  selectedTrack_ = track->id;
  selectedRegion_ = regionId;
  technicalEdits_.setRegion(regionId);
  return core::success();
}

void AuthoringRuntime::clearSelection() {
  if (selectedTrack_.valid() || selectedRegion_.valid()) {
    static_cast<void>(stopPerformanceAudition());
  }
  selectedTrack_ = {};
  selectedRegion_ = {};
  technicalEdits_.setRegion({});
}

core::Result<void> AuthoringRuntime::execute(
    std::unique_ptr<application::ICommand> command) {
  const auto impact = command == nullptr
                          ? application::CommandImpact{}
                          : command->impact();
  return afterCommandExecution(document_->execute(std::move(command)), impact);
}

core::Result<void> AuthoringRuntime::executePerformanceResult(
    const application::PerformanceJobContext& context,
    std::unique_ptr<application::ICommand> command) {
  const auto impact = command == nullptr
                          ? application::CommandImpact{}
                          : command->impact();
  return afterCommandExecution(document_->executePerformanceResult(context, std::move(command)), impact);
}

core::Result<void> AuthoringRuntime::afterCommandExecution(
    core::Result<void> result, application::CommandImpact impact,
    std::shared_ptr<const PublishedProjectAudio> retainedAudition) {
  const bool preserveAcceptedAudio = retainedAudition != nullptr;
  if (!result) recordDiagnostic(result.error());
  document_->synchronizeDirtyState();
  if (result && retainedAudition) {
    {
      std::lock_guard lock(performanceAuditionMutex_);
      retainedAcceptedAudition_ = std::move(retainedAudition);
      performanceAuditionActive_.store(false, std::memory_order_release);
      performanceAuditionReady_.store(false, std::memory_order_release);
    }
    performanceAuditionRenderer_.cancel();
  } else if (result) {
    static_cast<void>(stopPerformanceAudition());
  }
  if (result && impact.scope != application::CommandAudioImpact::ViewOnly &&
      impact.scope != application::CommandAudioImpact::MetadataOnly) {
    seamPreviewRenderer_.cancel();
    seamPreviewActive_.store(false, std::memory_order_release);
    seamPreviewReady_.store(false, std::memory_order_release);
    requestPreviewImpl(false, impact, preserveAcceptedAudio);
  }
  return result;
}

core::Result<void> AuthoringRuntime::undo() {
  auto result = document_->undo();
  if (!result) recordDiagnostic(result.error());
  if (result) {
    document_->synchronizeDirtyState();
    static_cast<void>(stopPerformanceAudition());
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
    static_cast<void>(stopPerformanceAudition());
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
  if (performanceAuditionActive_.load(std::memory_order_acquire)) {
    const auto stopped = stopPerformanceAudition();
    if (!stopped) return stopped;
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
    seamPreviewRenderer_.submitWithSources(
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

core::Result<void> AuthoringRuntime::auditionPerformance(
    domain::RegionId regionId,
    std::vector<domain::AcceptedPerformanceSelection> accepted) {
  if (!initialized_ || !config_.enableTransport)
    return core::failure(core::ErrorCode::Unsupported,
                         "Performance comparison requires an active transport");
  if (seamPreviewActive_.load(std::memory_order_acquire) ||
      performanceAuditionActive_.load(std::memory_order_acquire))
    return core::failure(core::ErrorCode::Conflict,
                         "Another transient audio preview is active");
  if (regionId != selectedRegion_ || !renderer_.acquireCurrent())
    return core::failure(core::ErrorCode::Conflict,
                         "Comparison requires the current selected region and ready canonical audio");
  auto request = makePreviewRequest(application::CommandImpact{
      .scope = application::CommandAudioImpact::PhraseAudio,
      .regionIds = {regionId},
  });
  if (!request || request->activeRegion != regionId)
    return core::failure(core::ErrorCode::Conflict,
                         "The selected performance region is not renderable");
  auto* region = request->project.findRegion(regionId);
  if (region == nullptr)
    return core::failure(core::ErrorCode::NotFound,
                         "The performance region disappeared before audition");
  region->performance.accepted = std::move(accepted);
  const auto valid = region->validate();
  if (!valid) return valid;
  {
    std::lock_guard lock(performanceAuditionMutex_);
    performanceAuditionActive_.store(true, std::memory_order_release);
    performanceAuditionReady_.store(false, std::memory_order_release);
  }
  performanceAuditionRenderer_.submitWithSources(
      std::move(request->project), std::move(request->voicebanks),
      request->activeTrack, request->activeRegion, request->revision,
      request->sampleRate, request->quality, true, std::move(request->impact));
  return core::success();
}

core::Result<void> AuthoringRuntime::acceptPerformanceAudition(
    std::unique_ptr<application::ICommand> command) {
  if (command == nullptr)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Performance audition acceptance requires a command");
  if (!performanceAuditionActive() || !performanceAuditionReady())
    return core::failure(core::ErrorCode::Conflict,
                         "The compared take is not currently audible");
  const auto current = performanceAuditionRenderer_.acquireCurrent();
  if (!current || current->state != RenderState::Ready)
    return core::failure(core::ErrorCode::Conflict,
                         "The compared take render is no longer current");
  auto retained = performanceAuditionRenderer_.latest();
  if (!retained || retained->state != RenderState::Ready ||
      retained->requestId != current->requestId)
    return core::failure(core::ErrorCode::Conflict,
                         "The compared take audio changed before acceptance");
  const auto impact = command->impact();
  auto result = document_->execute(std::move(command));
  return afterCommandExecution(std::move(result), impact, std::move(retained));
}

core::Result<void> AuthoringRuntime::stopPerformanceAudition() {
  core::Result<void> restored = core::success();
  {
    std::lock_guard lock(performanceAuditionMutex_);
    if (!performanceAuditionActive_.load(std::memory_order_acquire))
      return core::success();
    performanceAuditionActive_.store(false, std::memory_order_release);
    performanceAuditionReady_.store(false, std::memory_order_release);
    auto canonical = renderer_.acquire();
    if (canonical && canonical->state == RenderState::Ready)
      restored = transport_.publishAudio(std::move(canonical));
    else {
      restored = core::failure(core::ErrorCode::Conflict,
          "Canonical audio is unavailable after performance comparison");
      static_cast<void>(transport_.pause());
    }
  }
  // Cancellation may invoke the coordinator callback, which takes the mutex above.
  performanceAuditionRenderer_.cancel();
  return restored;
}

AuthoringRuntime::AudiblePublication AuthoringRuntime::audiblePublication() const {
  std::lock_guard lock(performanceAuditionMutex_);
  if (performanceAuditionActive_.load(std::memory_order_acquire) &&
      performanceAuditionReady_.load(std::memory_order_acquire)) {
    auto audition = performanceAuditionRenderer_.latest();
    if (audition && audition->state == RenderState::Ready)
      return {std::move(audition), true,
              performanceAuditionRenderer_.progress().audibleAudioStale};
  }
  if (retainedAcceptedAudition_)
    return {retainedAcceptedAudition_, true, false};
  return {renderer_.latest(), false, renderer_.progress().audibleAudioStale};
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

void AuthoringRuntime::setTempoMapOverride(std::optional<time::TempoMap> map) {
  // The caller decides when to render: a host-timed final render sets the map and asks
  // for the render in the same step, and clearing the map afterwards must not enqueue a
  // second render that would replace the audio the host just asked for.
  if (tempoMapOverride_ == map) return;
  tempoMapOverride_ = std::move(map);
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

void AuthoringRuntime::invalidatePreview() {
  {
    std::lock_guard lock(previewMutex_);
    pendingPreview_.reset();
    ++previewGeneration_;
    // The debounce worker submits under this same lock. It cannot restore an
    // obsolete request token after this owner-thread invalidation returns.
    renderer_.invalidateCurrent();
  }
  previewCondition_.notify_all();
}

void AuthoringRuntime::requestPreview(bool immediate,
                                      application::CommandImpact impact) {
  requestPreviewImpl(immediate, std::move(impact), false);
}

void AuthoringRuntime::requestPreviewImpl(
    bool immediate, application::CommandImpact impact,
    bool retainAcceptedAudition) {
  if (!initialized_ || document_ == nullptr) return;

  // A document/render-setting change ends any alternate-take audition before its new canonical
  // request can publish. The audition never becomes the source of a save or export.
  static_cast<void>(stopPerformanceAudition());
  if (!retainAcceptedAudition) {
    std::lock_guard lock(performanceAuditionMutex_);
    retainedAcceptedAudition_.reset();
  }

  // A previously prepared Final must not remain current during debounce.
  // The publication itself stays alive for readers and technical diagnostics.
  invalidatePreview();

  PreviewAssessment assessment;
  auto request = makePreviewRequest(std::move(impact), &assessment);
  noteBankAvailability(assessment.bankUnavailable);
  if (!request.has_value()) {
    if (assessment.nothingAudible) settleWithNothingToRender();
    return;
  }
  if (immediate) {
    std::lock_guard lock(previewMutex_);
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
AuthoringRuntime::makePreviewRequest(application::CommandImpact impact,
                                     PreviewAssessment* assessment) const {
  auto project = document_->session().project();
  // A host-owned timing authority replaces the document's map for this render only.
  // The substituted project is what the renderer compiles and what the render identity
  // hashes, so the published audio always carries the map it was rendered with.
  if (tempoMapOverride_.has_value()) project.tempoMap() = *tempoMapOverride_;
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
  const auto trackHasNotes = [](const domain::VocalTrack& track) {
    return std::any_of(track.regions.begin(), track.regions.end(),
                       [](const domain::VocalRegion& region) { return !region.notes.empty(); });
  };
  std::vector<rendering::TrackSingerSource> sources;
  sources.reserve(states.size());
  bool anyUsableSinger = false;
  bool anyNoteToSing = false;
  for (const auto& state : states) {
    if (const auto* track = project.findVocalTrack(state.trackId); track && track->proceduralRecipe) {
      anyUsableSinger = true;
      anyNoteToSing = anyNoteToSing || trackHasNotes(*track);
      const auto& savedPath = document_->identity().projectPath;
      sources.emplace_back(rendering::TrackRecipeFileSource{state.trackId, *track->proceduralRecipe,
          savedPath ? std::optional<std::filesystem::path>{savedPath->parent_path()} : std::nullopt});
      continue;
    }
    if (state.resolution.resolved()) {
      anyUsableSinger = true;
      if (const auto* track = project.findVocalTrack(state.trackId); track != nullptr)
        anyNoteToSing = anyNoteToSing || trackHasNotes(*track);
      sources.push_back(sourceFor(state.trackId,
                                  *state.resolution.candidate));
      continue;
    }
    if (auto* track = project.findVocalTrack(state.trackId); track != nullptr) {
      track->muted = true;
    }
  }
  // Whether there is anything to render is a question about the whole score. The renderer renders
  // every region that has notes; the track and region chosen below only decide whose performance
  // (cues, waveform, unit plan) it reports back. So a region on screen with no notes is not an
  // empty score: the score can sound in another region, and an edit made while that region is
  // selected still has to be rendered. A request is composed exactly when something is audible,
  // and the caller relies on it: no request means nothing to render, and it settles the status
  // and the transport instead of leaving a superseded render on screen.
  const auto nothingAudible = !hasBackingAudio && !anyNoteToSing;
  if (assessment != nullptr) {
    assessment->bankUnavailable = !project.vocalTracks().empty() && !anyUsableSinger;
    assessment->nothingAudible = nothingAudible;
  }
  if (nothingAudible) return std::nullopt;

  auto activeTrack = selectedTrack_;
  auto activeRegion = selectedRegion_;
  const auto selectedResolved = std::any_of(
      sources.begin(), sources.end(), [activeTrack](const auto& source) {
        return std::visit([activeTrack](const auto& value) { return value.trackId == activeTrack; }, source);
      });
  if (!selectedResolved || project.findRegion(activeRegion) == nullptr) {
    std::tie(activeTrack, activeRegion) =
        firstRenderableSelection(project, states);
  }

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
  renderer_.submitWithSources(std::move(request.project), std::move(request.voicebanks),
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
      if (!stopToken.stop_requested() && request.has_value()) {
        submitPreview(std::move(*request), false);
      }
    }
    if (stopToken.stop_requested()) break;
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
    const auto* track = project.findVocalTrack(state.trackId);
    if (!isResolved(state) && !(track && track->proceduralRecipe)) continue;
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
  if (config_.enableTransport) {
    std::lock_guard lock(performanceAuditionMutex_);
    if (!seamPreviewActive_.load(std::memory_order_acquire) &&
        !performanceAuditionActive_.load(std::memory_order_acquire) &&
        renderer_.matchesCurrent(*handle)) {
      const auto published = transport_.publishAudio(std::move(handle));
      if (published) retainedAcceptedAudition_.reset();
    }
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

void AuthoringRuntime::publishCompletedPerformanceAudition() {
  const auto progress = performanceAuditionRenderer_.progress();
  // A cancelled older request can finish after a new audition was submitted.
  // Only a terminal current publication (or a terminal render failure) may
  // change the active comparison's transport state.
  if (progress.state != RenderState::Ready &&
      progress.state != RenderState::Failed) return;
  bool published = false;
  {
    std::lock_guard lock(performanceAuditionMutex_);
    if (!performanceAuditionActive_.load(std::memory_order_acquire)) return;
    if (progress.state == RenderState::Ready) {
      auto handle = performanceAuditionRenderer_.acquireCurrent();
      if (!handle) return;
      published = static_cast<bool>(transport_.publishAudio(std::move(handle)));
    }
    if (published) {
      performanceAuditionReady_.store(true, std::memory_order_release);
    } else {
      performanceAuditionActive_.store(false, std::memory_order_release);
      performanceAuditionReady_.store(false, std::memory_order_release);
      auto canonical = renderer_.acquire();
      if (canonical && canonical->state == RenderState::Ready)
        static_cast<void>(transport_.publishAudio(std::move(canonical)));
      else
        static_cast<void>(transport_.pause());
    }
  }
  if (!published && progress.state == RenderState::Failed)
    recordRenderFailure(progress.failure, progress.diagnostic);
  std::function<void()> callback;
  {
    std::lock_guard lock(callbackMutex_);
    callback = completionCallback_;
  }
  if (callback) callback();
}

}  // namespace seam::authoring
