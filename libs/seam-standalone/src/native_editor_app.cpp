#include "seam/standalone/native_editor_app.hpp"

#include "seam/platform/application_paths.hpp"
#include "seam/platform/file_dialog.hpp"

#include <algorithm>
#include <iostream>
#include <utility>

namespace seam::standalone {

core::Result<std::unique_ptr<NativeEditorApp>> NativeEditorApp::create(
    NativeEditorAppConfig config) {
  auto app = std::unique_ptr<NativeEditorApp>{
      new NativeEditorApp(std::move(config))};
  auto initialized = app->initialize();
  if (!initialized) {
    return core::Result<std::unique_ptr<NativeEditorApp>>{initialized.error()};
  }
  return app;
}

NativeEditorApp::~NativeEditorApp() {
  if (applicationMenu_ != nullptr) applicationMenu_->uninstall();
  applicationController_.reset();
  shutdownAudio();
  authoring_.reset();
}

core::Result<void> NativeEditorApp::initialize() {
  const auto paths = config_.applicationSupportRoot.empty()
                         ? platform::applicationPaths()
                         : core::success(platform::ApplicationPaths::forTestRoot(
                               config_.applicationSupportRoot));
  if (!paths) return core::Result<void>{paths.error()};
  auto production = makeProductionConfiguration(
      ProductionConfigurationInput{
          .mode = config_.runtimeMode,
          .paths = paths.value(),
          .voicebankRoots = config_.authoring.voicebankRoots,
          .trustedVoicebankKeys = config_.trustedVoicebankKeys,
          .developmentTrustRoot = config_.developmentTrustRoot,
          .allowDevelopmentVoicebanks = config_.allowDevelopmentVoicebanks,
          .forceThreadedAudio = config_.forceThreadedAudio,
          .bindFirstAvailableVoicebank =
              config_.authoring.bindFirstAvailableVoicebank,
          .startPaused = config_.startPaused,
          .sampleRate = config_.authoring.sampleRate,
          .outputChannels = config_.authoring.outputChannels,
          .audioBlockFrames = config_.audioBlockFrames,
          .characterPackage = config_.characterPackage,
      });
  if (!production) return core::Result<void>{production.error()};
  const auto& effective = production.value();
  config_.authoring.cacheRoot = effective.cacheRoot;
  config_.authoring.voicebankRoots = effective.voicebankRoots;
  config_.authoring.bindFirstAvailableVoicebank =
      effective.bindFirstAvailableVoicebank;
  config_.authoring.allowDevelopmentVoicebanks =
      effective.allowDevelopmentVoicebanks;
  config_.characterPackage = effective.characterPackage;
  config_.applicationSupportRoot = effective.applicationSupportRoot;
  config_.trustedVoicebankKeys = effective.trustedVoicebankKeys;
  config_.developmentTrustRoot = effective.developmentTrustRoot;
  config_.allowDevelopmentVoicebanks = effective.allowDevelopmentVoicebanks;
  config_.forceThreadedAudio = effective.forceThreadedAudio;
  config_.startPaused = effective.startPaused;

  native_ui::EditorHostCallbacks callbacks{
      .requestRepaint = [this] {
        if (window_ != nullptr) window_->requestRepaint();
      },
      .beginTextInput = [this](const native_ui::TextInputRequest& request) {
        if (window_ != nullptr) window_->beginTextInput(request);
      },
      .endTextInput = [this] {
        if (window_ != nullptr) window_->endTextInput();
      },
      .setPlaying = [this](bool) {
        if (window_ != nullptr) window_->requestRepaint();
      },
      .documentChanged = [this] {
        if (applicationController_ != nullptr) {
          record(applicationController_->onDocumentChanged());
        }
        if (window_ != nullptr) window_->requestRepaint();
      },
  };
  auto created = AuthoringSession::create(config_.authoring,
                                          std::move(callbacks));
  if (!created) {
    return core::Result<void>{created.error()};
  }
  authoring_ = std::move(created).value();

  const auto supportRoot = config_.applicationSupportRoot;
  auto application = StandaloneApplicationController::create(
      *authoring_, platform::createNativeFileDialog(),
      platform::createNativeUnsavedChangesPrompt(),
      StandaloneApplicationControllerConfig{
          .autosaveRoot = supportRoot / "Autosaves",
          .recentProjectsPath = supportRoot / "recent-projects.json",
          .voicebankInstallRoot = supportRoot / "Voicebanks",
          .trustedVoicebankKeys = config_.trustedVoicebankKeys,
          .developmentTrustRoot = config_.developmentTrustRoot,
          .allowDevelopmentVoicebanks = config_.allowDevelopmentVoicebanks,
          .defaultNewProject = authoring::NewProjectRequest{
              .name = "Untitled",
              .tempoBpm = 120.0,
              .sampleRate = config_.authoring.sampleRate,
              .outputChannels = config_.authoring.outputChannels,
              .initialVoicebank = std::nullopt,
          },
          .stateChanged = [this] {
            if (applicationMenu_ != nullptr) applicationMenu_->refresh();
            if (window_ != nullptr) window_->requestRepaint();
          },
      },
      [this] { closeRequested_.store(true, std::memory_order_release); });
  if (!application) return core::Result<void>{application.error()};
  applicationController_ = std::move(application).value();
  applicationMenu_ = platform::createNativeApplicationMenu();
  if (applicationMenu_ != nullptr) {
    auto installed = applicationMenu_->install(*applicationController_);
    if (!installed && installed.error().code != core::ErrorCode::Unsupported) {
      return installed;
    }
  }

  const auto character = character_.load(config_.characterPackage);
  if (character) {
    authoring_->controller().setCharacterMetadata(
        character_.displayName(), character_.styleName());
  } else {
    lastError_ = character.error().message;
  }
  return initializeAudio();
}

core::Result<void> NativeEditorApp::initializeAudio() {
  auto& runtime = authoring_->runtime();
  processor_ = std::make_unique<platform::MultichannelRingBufferAudioProcessor>(
      runtime.transport().ringBuffer(),
      std::max<std::size_t>(config_.audioBlockFrames, 4096U));
  platform::AudioDeviceConfig deviceConfig{
      .sampleRate = runtime.transport().sampleRate(),
      .blockFrames = config_.audioBlockFrames,
      .outputChannels = runtime.transport().outputChannels(),
      .applicationName = "Project SEAM",
      .streamName = "Production standalone preview",
  };

  std::string physicalError;
  if (!config_.forceThreadedAudio) {
    auto physical = platform::createSystemAudioDevice();
    auto opened = physical->open(deviceConfig, *processor_);
    if (opened) {
      audioDevice_ = std::move(physical);
    } else {
      physicalError = opened.error().message;
      if (!opened.error().context.empty()) {
        physicalError += ": " + opened.error().context;
      }
    }
  }
  if (audioDevice_ == nullptr) {
    auto fallback = platform::createThreadedAudioDevice();
    auto opened = fallback->open(deviceConfig, *processor_);
    if (!opened) return opened;
    audioDevice_ = std::move(fallback);
  }

  auto started = audioDevice_->start();
  if (!started && audioDevice_->info().physical) {
    physicalError = started.error().message;
    audioDevice_->stop();
    auto fallback = platform::createThreadedAudioDevice();
    auto opened = fallback->open(deviceConfig, *processor_);
    if (!opened) return opened;
    started = fallback->start();
    if (!started) return started;
    audioDevice_ = std::move(fallback);
  } else if (!started) {
    return started;
  }

  if (!config_.startPaused) {
    const auto played = runtime.transport().play();
    if (!played) return played;
  }
  const auto info = audioDevice_->info();
  authoring_->controller().setAudioState(info.physical, info.backend);
  if (!physicalError.empty()) {
    std::cerr << "Physical audio unavailable, using callback-clock fallback: "
              << physicalError << '\n';
  }
  return core::success();
}

void NativeEditorApp::setWindow(native_ui::INativeWindow& window) noexcept {
  window_ = &window;
}

void NativeEditorApp::shutdownAudio() noexcept {
  if (audioDevice_ != nullptr) audioDevice_->stop();
  if (authoring_ != nullptr) {
    static_cast<void>(authoring_->runtime().transport().pause());
  }
}

void NativeEditorApp::paint(native_ui::RasterCanvas& canvas) noexcept {
  if (authoring_ == nullptr) return;
  if (applicationController_ != nullptr) {
    record(applicationController_->tickAutosave());
  }
  const auto transport = authoring_->runtime().transport().state();
  const auto& project = authoring_->runtime().document().session().project();
  const auto tick = project.tempoMap().tickAtSampleFrame(
      transport.playhead, authoring_->runtime().transport().sampleRate());
  auto state = authoring_->controller().sceneState();
  state.playheadPixel =
      authoring_->controller().pianoRoll().timeline().tickToPixel(tick);
  character_.setDisplayMode(state.characterMode);
  character_.setState(state.characterState);
  state.characterPortrait = character_.portrait();
  if (state.characterName.empty()) state.characterName = character_.displayName();
  if (state.characterStyle.empty()) state.characterStyle = character_.styleName();
  painter_.paint(canvas, authoring_->controller().pianoRoll(), state);
}

void NativeEditorApp::resized(double logicalWidth, double logicalHeight,
                              double) noexcept {
  authoring_->controller().resize(logicalWidth, logicalHeight);
}

void NativeEditorApp::pointerDown(
    const native_ui::PointerEvent& event) noexcept {
  record(authoring_->controller().pointerDown(event));
}
void NativeEditorApp::pointerMove(
    const native_ui::PointerEvent& event) noexcept {
  record(authoring_->controller().pointerMove(event));
}
void NativeEditorApp::pointerUp(
    const native_ui::PointerEvent& event) noexcept {
  record(authoring_->controller().pointerUp(event));
}
void NativeEditorApp::scroll(double deltaX, double deltaY, ui::Point anchor,
                             native_ui::InputModifiers modifiers) noexcept {
  authoring_->controller().scroll(deltaX, deltaY, anchor, modifiers);
}
void NativeEditorApp::keyDown(const native_ui::KeyEvent& event) noexcept {
  if (applicationController_ != nullptr && event.modifiers.primaryShortcut()) {
    std::optional<platform::ApplicationCommand> command;
    if (event.key == native_ui::NativeKey::N) {
      command = platform::ApplicationCommand::NewProject;
    } else if (event.key == native_ui::NativeKey::O) {
      command = platform::ApplicationCommand::OpenProject;
    } else if (event.key == native_ui::NativeKey::S) {
      command = event.modifiers.shift
                    ? platform::ApplicationCommand::SaveProjectAs
                    : platform::ApplicationCommand::SaveProject;
    } else if (event.key == native_ui::NativeKey::E) {
      command = platform::ApplicationCommand::ExportAudio;
    } else if (event.key == native_ui::NativeKey::Q) {
      command = platform::ApplicationCommand::Quit;
    }
    if (command.has_value()) {
      record(applicationController_->dispatch(*command));
      return;
    }
  }
  record(authoring_->controller().keyDown(event));
}
void NativeEditorApp::textComposition(
    std::u32string text,
    ui::CompositionSelection selection) noexcept {
  record(authoring_->controller().updateTextComposition(std::move(text),
                                                        selection));
}
void NativeEditorApp::textCommit(std::u32string text) noexcept {
  record(authoring_->controller().commitTextComposition(std::move(text)));
}
void NativeEditorApp::textCancel() noexcept {
  authoring_->controller().cancelTextComposition();
}
bool NativeEditorApp::requestClose() noexcept {
  if (applicationController_ == nullptr) return true;
  auto requested = applicationController_->requestClose();
  if (!requested) {
    record(core::Result<void>{requested.error()});
    return false;
  }
  return requested.value();
}

bool NativeEditorApp::wantsClose() const noexcept {
  return closeRequested_.load(std::memory_order_acquire);
}

platform::AudioDeviceInfo NativeEditorApp::audioInfo() const {
  return audioDevice_ == nullptr ? platform::AudioDeviceInfo{}
                                 : audioDevice_->info();
}
platform::AudioDeviceStats NativeEditorApp::audioStats() const noexcept {
  return audioDevice_ == nullptr ? platform::AudioDeviceStats{}
                                 : audioDevice_->stats();
}
platform::MultichannelRingProcessorStats
NativeEditorApp::processorStats() const noexcept {
  return processor_ == nullptr ? platform::MultichannelRingProcessorStats{}
                               : processor_->stats();
}

void NativeEditorApp::record(const core::Result<void>& result) noexcept {
  if (!result) {
    lastError_ = result.error().message;
    if (!result.error().context.empty()) {
      lastError_ += ": " + result.error().context;
    }
  }
}

}  // namespace seam::standalone
