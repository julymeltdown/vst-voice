#include "seam/authoring/authoring_runtime.hpp"

#include <algorithm>
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

}  // namespace

AuthoringRuntime::AuthoringRuntime(std::unique_ptr<ProjectDocument> document,
                                   AuthoringRuntimeConfig config)
    : document_(std::move(document)),
      config_(std::move(config)),
      voicebanks_(config_.voicebankRoots,
                  config_.allowDevelopmentVoicebanks),
      renderer_(config_.cacheRoot),
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
}

AuthoringRuntime::~AuthoringRuntime() { shutdown(); }

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
  selectedTrack_ = trackId;
  selectedRegion_ = regionId;
  technicalEdits_.setRegion(regionId);
  initialized_ = true;
  requestPreview();
  return core::success();
}

void AuthoringRuntime::shutdown() noexcept {
  if (!document_) return;
  setCompletionCallback({});
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
  auto result = document_->execute(std::move(command));
  if (result) handleDocumentChanged();
  return result;
}

core::Result<void> AuthoringRuntime::undo() {
  auto result = document_->undo();
  if (result) handleDocumentChanged();
  return result;
}

core::Result<void> AuthoringRuntime::redo() {
  auto result = document_->redo();
  if (result) handleDocumentChanged();
  return result;
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

void AuthoringRuntime::setRenderQuality(
    rendering::RenderQuality quality) noexcept {
  renderQuality_ = quality;
}

void AuthoringRuntime::setCompletionCallback(
    std::function<void()> callback) {
  std::lock_guard lock(callbackMutex_);
  completionCallback_ = std::move(callback);
}

void AuthoringRuntime::handleDocumentChanged() {
  document_->synchronizeDirtyState();
  requestPreview();
}

void AuthoringRuntime::requestPreview() {
  if (!initialized_ || document_ == nullptr) return;

  auto project = document_->session().project();
  const auto states = voicebanks_.resolveAll(project);
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

  renderer_.submit(std::move(project), std::move(sources), activeTrack,
                   activeRegion, document_->session().revision(),
                   previewSampleRate_, renderQuality_);
}

TechnicalRenderView AuthoringRuntime::currentTechnicalRenderView() const {
  TechnicalRenderView view;
  const auto audio = renderer_.latest();
  if (!audio || audio->state != RenderState::Ready) return view;
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
  if (progress.state != RenderState::Ready) return;
  auto handle = renderer_.acquire();
  if (!handle || handle->state != RenderState::Ready) return;
  if (config_.enableTransport) {
    static_cast<void>(transport_.publishAudio(std::move(handle)));
  }
  std::function<void()> callback;
  {
    std::lock_guard lock(callbackMutex_);
    callback = completionCallback_;
  }
  if (callback) callback();
}

}  // namespace seam::authoring
