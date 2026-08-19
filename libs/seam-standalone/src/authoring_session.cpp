#include "seam/standalone/authoring_session.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/application/render_commands.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace seam::standalone {
namespace {

const voicebank::VoicebankCandidate* preferredCandidate(
    const std::vector<voicebank::VoicebankCandidate>& candidates) {
  const auto preferred = std::find_if(
      candidates.begin(), candidates.end(), [](const auto& candidate) {
        return candidate.manifest.id == "demo.public-domain.human.production" &&
               candidate.manifest.version == "0.12.0";
      });
  return preferred == candidates.end()
             ? (candidates.empty() ? nullptr : &candidates.front())
             : &*preferred;
}

}  // namespace

core::Result<std::unique_ptr<AuthoringSession>> AuthoringSession::create(
    AuthoringSessionConfig config,
    native_ui::EditorHostCallbacks callbacks) {
  if (config.cacheRoot.empty()) {
    return core::failure<std::unique_ptr<AuthoringSession>>(
        core::ErrorCode::InvalidArgument,
        "Standalone authoring cache root cannot be empty");
  }
  if (config.sampleRate < 8000U || config.sampleRate > 192000U) {
    return core::failure<std::unique_ptr<AuthoringSession>>(
        core::ErrorCode::InvalidArgument,
        "Standalone sample rate must be between 8000 and 192000 Hz");
  }
  if (config.outputChannels < 1U || config.outputChannels > 8U) {
    return core::failure<std::unique_ptr<AuthoringSession>>(
        core::ErrorCode::InvalidArgument,
        "Standalone output channel count must be between one and eight");
  }

  application::ProjectFactory factory{1000U};
  domain::TrackId trackId;
  domain::RegionId regionId;
  auto project = makeUntitledProject(factory, trackId, regionId,
                                     config.sampleRate,
                                     config.outputChannels);
  auto document = std::unique_ptr<authoring::ProjectDocument>{
      new authoring::ProjectDocument(
          std::move(project),
          application::ProjectFactory{factory.nextIdValue()})};
  auto runtime = std::make_unique<authoring::AuthoringRuntime>(
      std::move(document), authoring::AuthoringRuntimeConfig{
                               .cacheRoot = std::move(config.cacheRoot),
                               .voicebankRoots = std::move(config.voicebankRoots),
                               .previewSampleRate = config.sampleRate,
                               .outputChannels = config.outputChannels,
                               .allowDevelopmentVoicebanks = true,
                               .enableTransport = true,
                           });
  auto result = std::unique_ptr<AuthoringSession>{new AuthoringSession(
      std::move(runtime), trackId, regionId, std::move(callbacks))};
  auto initialized = result->initialize(config.bindFirstAvailableVoicebank);
  if (!initialized) {
    return core::Result<std::unique_ptr<AuthoringSession>>{initialized.error()};
  }
  return result;
}

AuthoringSession::AuthoringSession(
    std::unique_ptr<authoring::AuthoringRuntime> runtime,
    domain::TrackId trackId,
    domain::RegionId regionId,
    native_ui::EditorHostCallbacks callbacks)
    : runtime_(std::move(runtime)),
      externalCallbacks_(std::move(callbacks)),
      trackId_(trackId),
      regionId_(regionId) {}

AuthoringSession::~AuthoringSession() {
  if (runtime_) {
    runtime_->setCompletionCallback({});
    runtime_->shutdown();
  }
  controller_.reset();
}

domain::Project AuthoringSession::makeUntitledProject(
    application::ProjectFactory& factory,
    domain::TrackId& trackId,
    domain::RegionId& regionId,
    std::uint32_t sampleRate,
    std::uint8_t outputChannels) {
  auto project = factory.createProject("Untitled");
  project.settings().sampleRate = sampleRate;
  project.settings().characterDisplay = domain::CharacterDisplayMode::Minimal;
  static_cast<void>(project.tempoMap().addOrReplace(time::Tick{0}, 120.0));
  trackId = factory.addVocalTrack(project, "Voice 1");
  regionId = factory.addRegion(project, trackId, "Region 1",
                               time::Tick{0}, time::Tick{15360});
  application::EditorSession routeSession{project};
  auto route = routeSession.execute(
      std::make_unique<application::ConfigureProjectOutputCommand>(
          outputChannels));
  if (route) project = routeSession.project();
  return project;
}

core::Result<void> AuthoringSession::initialize(
    bool bindFirstAvailableVoicebank) {
  auto initialized = runtime_->initialize();
  if (!initialized) return initialized;

  if (bindFirstAvailableVoicebank) {
    auto bound = bindInitialVoicebank();
    if (!bound && bound.error().code != core::ErrorCode::NotFound) return bound;
  }
  static_cast<void>(runtime_->selectTrack(trackId_));
  static_cast<void>(runtime_->selectRegion(regionId_));
  configureController();
  runtime_->setCompletionCallback([this] { onRenderCompleted(); });
  runtime_->handleDocumentChanged();
  return core::success();
}

core::Result<void> AuthoringSession::bindInitialVoicebank() {
  const auto candidates = runtime_->voicebanks().candidates();
  const auto* candidate = preferredCandidate(candidates);
  if (candidate == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "No installed or development voicebank is available");
  }
  return runtime_->voicebanks().bindTrack(runtime_->document(), trackId_,
                                           *candidate);
}

void AuthoringSession::configureController() {
  native_ui::EditorHostCallbacks callbacks{
      .requestRepaint = externalCallbacks_.requestRepaint,
      .beginTextInput = externalCallbacks_.beginTextInput,
      .endTextInput = externalCallbacks_.endTextInput,
      .setPlaying = [this](bool playing) {
        const auto result = playing ? runtime_->transport().play()
                                    : runtime_->transport().pause();
        if (externalCallbacks_.setPlaying) {
          externalCallbacks_.setPlaying(result && playing);
        }
      },
      .documentChanged = [this] { onDocumentChanged(); },
  };
  controller_ = std::make_unique<native_ui::NativeEditorController>(
      runtime_->document().session(), runtime_->document().factory(),
      regionId_, std::move(callbacks));
  controller_->setDirty(runtime_->document().dirty());
}

void AuthoringSession::onDocumentChanged() {
  runtime_->handleDocumentChanged();
  if (controller_) controller_->setDirty(runtime_->document().dirty());
  if (externalCallbacks_.documentChanged) externalCallbacks_.documentChanged();
}

void AuthoringSession::onRenderCompleted() {
  if (controller_) {
    const auto latest = runtime_->renderer().latest();
    const auto ready = latest != nullptr &&
                       latest->state == authoring::RenderState::Ready &&
                       !latest->result.interleaved.empty();
    controller_->setAudioState(ready, ready ? "PRODUCTION" : "OFFLINE");
  }
  if (externalCallbacks_.requestRepaint) externalCallbacks_.requestRepaint();
}

voicebank::VoicebankResolution AuthoringSession::voicebankResolution() const {
  return runtime_->voicebanks().resolveTrack(
      runtime_->document().session().project(), trackId_);
}

}  // namespace seam::standalone
