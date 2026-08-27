#include "seam/standalone/native_editor_app.hpp"

#include "seam/core/sha256.hpp"
#include "seam/platform/accessibility_preferences.hpp"
#include "seam/platform/application_paths.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/standalone/eula_acceptance.hpp"
#include "seam/standalone/native_project_dialog.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>

namespace seam::standalone {

namespace {

native_ui::RenderStatusState renderStatusState(
    authoring::RenderState state) noexcept {
  switch (state) {
    case authoring::RenderState::Idle:
      return native_ui::RenderStatusState::Idle;
    case authoring::RenderState::Queued:
      return native_ui::RenderStatusState::Queued;
    case authoring::RenderState::Rendering:
      return native_ui::RenderStatusState::Rendering;
    case authoring::RenderState::Ready:
      return native_ui::RenderStatusState::Ready;
    case authoring::RenderState::Stale:
      return native_ui::RenderStatusState::Stale;
    case authoring::RenderState::Cancelled:
      return native_ui::RenderStatusState::Cancelled;
    case authoring::RenderState::Failed:
      return native_ui::RenderStatusState::Failed;
  }
  return native_ui::RenderStatusState::Failed;
}

std::string timestampNow() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

}

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
  recoveryRoot_ = paths.value().recoveryRoot;
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
  const auto manualsRoot = config_.manualsRoot.empty()
                               ? paths.value().manualsRoot
                               : config_.manualsRoot;

  if (effective.mode == ProductionRuntimeMode::Release) {
    const auto eulaPath = manualsRoot / "EULA.md";
    std::error_code eulaError;
    if (manualsRoot.empty() ||
        !std::filesystem::is_regular_file(
            std::filesystem::symlink_status(eulaPath, eulaError)) ||
        eulaError) {
      return core::failure(core::ErrorCode::NotFound,
                           "Bundled EULA is required for release launch",
                           eulaPath.string());
    }
    auto eulaDigest = core::sha256File(eulaPath);
    if (!eulaDigest) return core::Result<void>{eulaDigest.error()};
    const auto acceptancePath =
        paths.value().settingsRoot / "eula-acceptance.json";
    auto acceptance = EulaAcceptanceStore::load(acceptancePath);
    if (!acceptance) return core::Result<void>{acceptance.error()};
    const auto accepted = acceptance.value().has_value() &&
                          EulaAcceptanceStore::matches(
                              *acceptance.value(),
                              kExternalBetaEulaDocumentVersion,
                              eulaDigest.value());
    if (!accepted) {
      auto prompted = platform::requestEulaAcceptance(eulaPath);
      if (!prompted) return core::Result<void>{prompted.error()};
      if (!prompted.value()) {
        return core::failure(core::ErrorCode::Conflict,
                             "External Beta EULA was not accepted");
      }
      auto saved = EulaAcceptanceStore::save(
          acceptancePath,
          EulaAcceptanceRecord{
              .documentVersion = std::string{kExternalBetaEulaDocumentVersion},
              .documentSha256 = eulaDigest.value(),
              .acceptedAtUtc = timestampNow(),
          });
      if (!saved) return saved;
    }
  }

  auto crashCapture = platform::CrashCapture::install(
      platform::CrashCaptureConfig{.root = paths.value().crashReportsRoot});
  if (crashCapture) {
    crashCapture_ = std::move(crashCapture).value();
    auto marker = crashCapture_->readMarker();
    if (marker && marker.value().has_value()) {
      startupCrashMarker_ = std::move(marker).value();
    }
  } else {
    lastError_ = crashCapture.error().message;
  }
  supportBundle_ = std::make_unique<authoring::SupportBundleService>(
      paths.value().recoveryRoot / "SupportReports");

  audioSettingsStore_ = std::make_unique<authoring::AudioSettingsStore>(
      config_.applicationSupportRoot / "Settings" / "audio-settings.json");
  const auto persistedSettings = audioSettingsStore_->load();
  if (persistedSettings) {
    config_.authoring.sampleRate = persistedSettings.value().sampleRate;
    config_.authoring.outputChannels = persistedSettings.value().outputChannels;
    config_.audioBlockFrames = persistedSettings.value().blockFrames;
    startupDeviceId_ = persistedSettings.value().deviceId;
  } else if (persistedSettings.error().code != core::ErrorCode::NotFound) {
    lastError_ = persistedSettings.error().message;
  }

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
      .setPlaying = [this](bool playing) {
        if (playing) {
          if (authoring_->runtime().transport().state().available) {
            const auto started = startAudioForPlayback();
            if (!started) {
              static_cast<void>(authoring_->runtime().transport().pause());
              return started;
            }
          }
        } else {
          stopAudioForPlayback();
        }
        if (window_ != nullptr) window_->requestRepaint();
        return core::success();
      },
      .documentChanged = [this] {
        if (applicationController_ != nullptr) {
          record(applicationController_->onDocumentChanged());
        }
        if (window_ != nullptr) window_->requestRepaint();
      },
      .cancelExport = [this] {
        if (applicationController_ != nullptr) {
          applicationController_->cancelExport();
        }
        if (window_ != nullptr) window_->requestRepaint();
      },
      .previewSeam = [this](domain::PhonemeKey key, bool alternate) {
        if (authoring_ == nullptr) {
          return core::failure(core::ErrorCode::InvalidState,
                               "Standalone authoring runtime is unavailable");
        }
        const auto result = authoring_->runtime().previewSeam(key, alternate);
        if (window_ != nullptr) window_->requestRepaint();
        return result;
      },
      .diagnosticAction = [this](const authoring::Diagnostic& diagnostic,
                                 authoring::DiagnosticAction action) {
        return handleDiagnosticAction(diagnostic, action);
      },
      .selectVoicebank = [this](std::string_view id, std::string_view version,
                                std::string_view contentHash) {
        if (applicationController_ == nullptr) {
          return core::failure(core::ErrorCode::InvalidState,
                               "Voicebank selection is unavailable");
        }
        const auto selected = applicationController_->selectVoicebank(
            id, version, contentHash);
        if (window_ != nullptr) window_->requestRepaint();
        return selected;
      },
      .viewChanged = [this] {
        if (window_ != nullptr) window_->requestRepaint();
      },
      .applyAudioSettings = [this](authoring::AudioSettings settings)
          -> core::Result<void> {
        auto applied = applyAudioSettings(std::move(settings));
        if (!applied) return core::Result<void>{applied.error()};
        if (window_ != nullptr) window_->requestRepaint();
        return core::success();
      },
      .reduceMotionEnabled = [] {
        return platform::currentAccessibilityPreferences().reduceMotion;
      },
  };
  auto created = AuthoringSession::create(config_.authoring,
                                          std::move(callbacks));
  if (!created) {
    return core::Result<void>{created.error()};
  }
  authoring_ = std::move(created).value();
  if (startupCrashMarker_.has_value()) {
    authoring_->controller().setDiagnostics({authoring::Diagnostic{
        .code = "CRASH_RECOVERY_AVAILABLE",
        .severity = authoring::DiagnosticSeverity::Warning,
        .messageKey = "crash.recovery_available",
        .affectedIds = {},
        .actions = authoring::DiagnosticRegistry::actions(
            "CRASH_RECOVERY_AVAILABLE"),
        .occurrenceCount = 1U}});
  }

  const auto supportRoot = config_.applicationSupportRoot;
  auto application = StandaloneApplicationController::create(
      *authoring_, platform::createNativeFileDialog(),
      platform::createNativeUnsavedChangesPrompt(),
      StandaloneApplicationControllerConfig{
          .autosaveRoot = supportRoot / "Autosaves",
          .recentProjectsPath = supportRoot / "recent-projects.json",
          .voicebankInstallRoot = supportRoot / "Voicebanks",
          .manualsRoot = manualsRoot,
          .trustedVoicebankKeys = config_.trustedVoicebankKeys,
          .developmentTrustRoot = config_.developmentTrustRoot,
          .allowDevelopmentVoicebanks = config_.allowDevelopmentVoicebanks,
          .requestNewProject = config_.requestNewProject
                                   ? config_.requestNewProject
                                   : [this]() -> core::Result<std::optional<authoring::NewProjectRequest>> {
                                       auto dialog = createNativeNewProjectDialog();
                                       if (dialog == nullptr) {
                                         return core::failure<std::optional<authoring::NewProjectRequest>>(
                                             core::ErrorCode::Unsupported,
                                             "Native New Project form is unavailable");
                                       }
                                       const auto currentPath =
                                           authoring_->runtime().document().identity().projectPath;
                                       return dialog->choose(NativeNewProjectDialogConfig{
                                           .candidates = authoring_->runtime().voicebanks().candidates(),
                                           .initialDirectory = currentPath.has_value()
                                                                   ? currentPath->parent_path()
                                                                   : std::filesystem::path{},
                                           .suggestedName = "Untitled.seam",
                                           .sampleRate = config_.authoring.sampleRate,
                                           .outputChannels = config_.authoring.outputChannels,
                                       });
                                     },
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
          .progressChanged = [this] {
            if (window_ != nullptr) window_->requestRepaint();
          },
          .openAudioSettings = [this] {
            authoring_->controller().showAudioSettings();
            if (window_ != nullptr) window_->requestRepaint();
            return core::success();
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

  std::error_code characterError;
  const auto hasCharacterPackage =
      !config_.characterPackage.empty() &&
      std::filesystem::is_directory(config_.characterPackage, characterError);
  if (hasCharacterPackage) {
    const auto character = character_.load(config_.characterPackage);
    if (character) {
      authoring_->controller().setCharacterMetadata(
          character_.displayName(), character_.styleName());
      const auto* package = character_.package();
      if (package != nullptr) {
        authoring_->controller().setCharacterBinding({
            .id = package->manifest.characterId,
            .version = package->manifest.version,
            .voicebankId = package->manifest.voicebankId,
            .accentPrimary = package->manifest.accent.primary,
            .accentSecondary = package->manifest.accent.secondary,
        });
      }
      authoring_->controller().setCharacterPortrait(
          character_.portrait(character::State::Neutral));
    } else {
      lastError_ = character.error().message;
    }
  }
  return initializeAudio();
}

core::Result<void> NativeEditorApp::initializeAudio() {
  auto& runtime = authoring_->runtime();
  processor_ = std::make_unique<platform::MultichannelRingBufferAudioProcessor>(
      runtime.transport().ringBuffer(),
      std::max<std::size_t>(config_.audioBlockFrames, 4096U));
  platform::AudioDeviceConfig deviceConfig{
      .deviceId = startupDeviceId_,
      .sampleRate = runtime.transport().sampleRate(),
      .blockFrames = config_.audioBlockFrames,
      .outputChannels = runtime.transport().outputChannels(),
      .applicationName = "Project SEAM",
      .streamName = "Production standalone preview",
  };

  std::optional<core::Error> physicalError;
  if (config_.forceThreadedAudio) {
    if (config_.runtimeMode != ProductionRuntimeMode::DeterministicTest) {
      return core::failure(
          core::ErrorCode::InvalidState,
          "Callback-clock audio is restricted to deterministic test mode");
    }
    auto threaded = config_.threadedAudioDeviceFactory
                        ? config_.threadedAudioDeviceFactory()
                        : platform::createThreadedAudioDevice();
    auto opened = threaded->open(deviceConfig, *processor_);
    if (!opened) return opened;
    audioDevice_ = std::move(threaded);
  } else {
    auto physical = config_.systemAudioDeviceFactory
                        ? config_.systemAudioDeviceFactory()
                        : platform::createSystemAudioDevice();
    auto opened = physical->open(deviceConfig, *processor_);
    if (opened) audioDevice_ = std::move(physical);
    else physicalError = opened.error();
  }

  if (!config_.startPaused) {
    const auto played = runtime.transport().play();
    if (!played) return played;
  }
  const auto info = audioDevice_ != nullptr
                        ? audioDevice_->info()
                        : platform::AudioDeviceInfo{
                              .backend = "unavailable",
                              .deviceId = startupDeviceId_.empty()
                                              ? "system-default"
                                              : startupDeviceId_,
                              .deviceName = "Unavailable system output",
                              .sampleRate = deviceConfig.sampleRate,
                              .blockFrames = deviceConfig.blockFrames,
                              .outputChannels = deviceConfig.outputChannels,
                              .physical = false,
                          };
  authoring_->controller().setAudioState(
      audioDevice_ != nullptr && info.physical, info.backend);
  audioDeviceCatalog_ = platform::createSystemAudioDeviceCatalog();
  audioSettings_ = std::make_unique<authoring::AudioSettingsController>(
      authoring::AudioSettings{
          .deviceId = info.deviceId,
          .sampleRate = info.sampleRate,
          .blockFrames = info.blockFrames,
          .outputChannels = info.outputChannels,
          .revision = 1U,
      },
      [this](const authoring::AudioSettings& settings) {
        return restartAudio(settings);
      });
  if (physicalError.has_value()) setAudioUnavailable(*physicalError);
  else clearAudioUnavailable();
  return core::success();
}

core::Result<void> NativeEditorApp::startAudioForPlayback() {
  if (authoring_ == nullptr || audioDevice_ == nullptr || processor_ == nullptr) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Audio playback is unavailable before initialization");
  }
  if (audioDevice_->running()) return core::success();

  auto& transport = authoring_->runtime().transport();
  auto state = transport.state();
  if (!state.available) {
    return core::failure(core::ErrorCode::Conflict,
                         "Playable audio is not ready for the audio device");
  }
  if (!state.playing) {
    const auto played = transport.play();
    if (!played) return played;
  }

  const auto transportConfig = transport.config();
  auto targetFrames = std::min(transportConfig.watermarkFrames,
                               transport.ringBuffer().capacityFrames());
  if (state.timelineEnd > 0) {
    targetFrames = std::min(
        targetFrames, static_cast<std::size_t>(state.timelineEnd));
  }
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds{2};
  while (transport.ringBuffer().availableReadFrames() < targetFrames) {
    if (!transport.state().available) {
      return core::failure(core::ErrorCode::Conflict,
                           "Playable audio became unavailable while buffering");
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      return core::failure(
          core::ErrorCode::Conflict,
          "Audio playback did not reach its startup buffer before timeout");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }

  auto started = audioDevice_->start();
  if (!started) {
    audioDevice_->stop();
    setAudioUnavailable(started.error());
    return started;
  }
  clearAudioUnavailable();
  const auto info = audioDevice_->info();
  authoring_->controller().setAudioState(info.physical, info.backend);
  return core::success();
}

void NativeEditorApp::stopAudioForPlayback() noexcept {
  if (audioDevice_ != nullptr) audioDevice_->stop();
}

core::Result<void> NativeEditorApp::restartAudio(
    const authoring::AudioSettings& settings) {
  if (authoring_ == nullptr || processor_ == nullptr || audioSettings_ == nullptr) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Audio settings are unavailable before audio initialization");
  }

  const auto previous = audioSettings_->current();
  if (audioDevice_ != nullptr && settings.deviceId == previous.deviceId &&
      settings.sampleRate == previous.sampleRate &&
      settings.blockFrames == previous.blockFrames &&
      settings.outputChannels == previous.outputChannels) {
    return core::success();
  }
  const auto previousDeviceInfo =
      audioDevice_ != nullptr
          ? audioDevice_->info()
          : platform::AudioDeviceInfo{
                .backend = "unavailable",
                .deviceId = previous.deviceId,
                .deviceName = "Unavailable system output",
                .sampleRate = previous.sampleRate,
                .blockFrames = previous.blockFrames,
                .outputChannels = previous.outputChannels,
                .physical = false,
            };
  const auto wasPlaying = authoring_->runtime().transport().state().playing;
  const auto wasRunning = audioDevice_ != nullptr && audioDevice_->running();
  auto previousDevice = std::move(audioDevice_);
  if (previousDevice != nullptr) previousDevice->stop();
  static_cast<void>(authoring_->runtime().transport().pause());

  auto open = [this](const authoring::AudioSettings& requested,
                     std::unique_ptr<platform::IAudioDevice>& device,
                     std::unique_ptr<platform::MultichannelRingBufferAudioProcessor>&
                         processor) -> core::Result<void> {
    auto& runtime = authoring_->runtime();
    auto nextProcessor =
        std::make_unique<platform::MultichannelRingBufferAudioProcessor>(
            runtime.transport().ringBuffer(),
            std::max<std::size_t>(requested.blockFrames, 4096U));
    platform::AudioDeviceConfig config{
        .deviceId = requested.deviceId,
        .sampleRate = requested.sampleRate,
        .blockFrames = requested.blockFrames,
        .outputChannels = requested.outputChannels,
        .applicationName = "Project SEAM",
        .streamName = "Production standalone preview",
    };
    std::unique_ptr<platform::IAudioDevice> nextDevice;
    if (config_.forceThreadedAudio) {
      if (config_.runtimeMode != ProductionRuntimeMode::DeterministicTest) {
        return core::failure(
            core::ErrorCode::InvalidState,
            "Callback-clock audio is restricted to deterministic test mode");
      }
      nextDevice = config_.threadedAudioDeviceFactory
                       ? config_.threadedAudioDeviceFactory()
                       : platform::createThreadedAudioDevice();
    } else {
      if (requested.deviceId == "threaded-callback-clock") {
        return core::failure(
            core::ErrorCode::InvalidArgument,
            "Callback-clock audio cannot be selected outside deterministic tests");
      }
      nextDevice = config_.systemAudioDeviceFactory
                       ? config_.systemAudioDeviceFactory()
                       : platform::createSystemAudioDevice();
    }
    const auto opened = nextDevice->open(config, *nextProcessor);
    if (!opened) return opened;
    processor = std::move(nextProcessor);
    device = std::move(nextDevice);
    return core::success();
  };

  std::optional<core::Error> requestedOpenError;
  const auto runtimeChanged = authoring_->runtime().reconfigureAudio(
      settings.sampleRate, settings.outputChannels, settings.blockFrames);
  if (runtimeChanged) {
    std::unique_ptr<platform::IAudioDevice> nextDevice;
    std::unique_ptr<platform::MultichannelRingBufferAudioProcessor> nextProcessor;
    const auto opened = open(settings, nextDevice, nextProcessor);
    if (opened) {
      audioDevice_ = std::move(nextDevice);
      processor_ = std::move(nextProcessor);
      const auto info = audioDevice_->info();
      authoring_->controller().setAudioState(info.physical, info.backend);
      clearAudioUnavailable();
      if (wasPlaying) {
        const auto resumed = authoring_->runtime().transport().play();
        if (!resumed) return resumed;
        if (wasRunning &&
            authoring_->runtime().transport().state().available) {
          const auto started = startAudioForPlayback();
          if (!started) return started;
        }
      }
      return core::success();
    }
    requestedOpenError = opened.error();
  }

  if (runtimeChanged) {
    static_cast<void>(authoring_->runtime().reconfigureAudio(
        previous.sampleRate, previous.outputChannels, previous.blockFrames));
  }
  std::optional<core::Error> restorationError;
  if (previousDevice != nullptr) {
    auto restoredProcessor =
        std::make_unique<platform::MultichannelRingBufferAudioProcessor>(
            authoring_->runtime().transport().ringBuffer(),
            std::max<std::size_t>(previous.blockFrames, 4096U));
    const auto restored = previousDevice->open(
        platform::AudioDeviceConfig{
            .deviceId = previousDeviceInfo.deviceId,
            .sampleRate = previous.sampleRate,
            .blockFrames = previous.blockFrames,
            .outputChannels = previous.outputChannels,
            .applicationName = "Project SEAM",
            .streamName = "Production standalone preview",
        },
        *restoredProcessor);
    auto started = restored ? core::success()
                            : core::Result<void>{restored.error()};
    if (restored && wasRunning) started = previousDevice->start();
    if (started) {
      processor_ = std::move(restoredProcessor);
      audioDevice_ = std::move(previousDevice);
      const auto info = audioDevice_->info();
      authoring_->controller().setAudioState(info.physical, info.backend);
      clearAudioUnavailable();
      if (wasPlaying) {
        const auto resumed = authoring_->runtime().transport().play();
        if (!resumed) return resumed;
        if (wasRunning &&
            authoring_->runtime().transport().state().available) {
          const auto restarted = startAudioForPlayback();
          if (!restarted) return restarted;
        }
      }
    } else {
      restorationError = started.error();
    }
  }
  if (restorationError.has_value()) setAudioUnavailable(*restorationError);
  if (!runtimeChanged) return runtimeChanged;
  if (requestedOpenError.has_value()) {
    if (audioDevice_ == nullptr) setAudioUnavailable(*requestedOpenError);
    return core::Result<void>{*requestedOpenError};
  }
  return core::failure(core::ErrorCode::Internal,
                       "Audio settings restart failed without a device error");
}

void NativeEditorApp::setAudioUnavailable(const core::Error& error) noexcept {
  lastError_ = error.message;
  if (!error.context.empty()) lastError_ += ": " + error.context;
  audioDiagnostic_ = authoring::Diagnostic{
      .code = "AUDIO_UNAVAILABLE",
      .severity = authoring::DiagnosticRegistry::severity("AUDIO_UNAVAILABLE"),
      .messageKey = "audio.unavailable",
      .affectedIds = {},
      .actions = authoring::DiagnosticRegistry::actions("AUDIO_UNAVAILABLE"),
      .occurrenceCount = 1U,
  };
  if (authoring_ != nullptr) {
    authoring_->controller().setAudioState(false, "unavailable");
    auto diagnostics = authoring_->runtime().diagnostics();
    diagnostics.push_back(*audioDiagnostic_);
    authoring_->controller().setDiagnostics(std::move(diagnostics));
  }
}

void NativeEditorApp::clearAudioUnavailable() noexcept {
  audioDiagnostic_.reset();
}

core::Result<void> NativeEditorApp::handleDiagnosticAction(
    const authoring::Diagnostic& diagnostic,
    authoring::DiagnosticAction action) {
  switch (action) {
    case authoring::DiagnosticAction::Dismiss:
      if (diagnostic.code == "CRASH_RECOVERY_AVAILABLE" && crashCapture_ != nullptr) {
        static_cast<void>(crashCapture_->clearMarker());
        startupCrashMarker_.reset();
      }
      authoring_->runtime().clearDiagnostics();
      authoring_->controller().setDiagnostics({});
      if (window_ != nullptr) window_->requestRepaint();
      return core::success();
    case authoring::DiagnosticAction::Retry:
      authoring_->runtime().requestPreview(true);
      if (window_ != nullptr) window_->requestRepaint();
      return core::success();
    case authoring::DiagnosticAction::RecoverAutosave:
      if (applicationController_ == nullptr) {
        return core::failure(core::ErrorCode::InvalidState,
                             "Recovery is unavailable before application setup");
      }
      return applicationController_->dispatch(
          platform::ApplicationCommand::RecoverLatestAutosave);
    case authoring::DiagnosticAction::OpenSupport: {
      if (config_.applicationSupportRoot.empty() || supportBundle_ == nullptr) {
        return core::failure(core::ErrorCode::InvalidState,
                             "Support export root is unavailable");
      }
      const auto destination = config_.applicationSupportRoot / "Support" /
                               "latest-diagnostic.zip";
      const auto level = diagnostic.severity == authoring::DiagnosticSeverity::Critical
                             ? core::LogLevel::Error
                             : diagnostic.severity == authoring::DiagnosticSeverity::Warning
                                   ? core::LogLevel::Warning
                                   : core::LogLevel::Error;
      const authoring::SupportBundleRequest request{
          .events = {core::LogEvent{
              .code = diagnostic.code,
              .level = level,
              .category = "diagnostic",
              .message = {},
              .fields = {{"messageKey", diagnostic.messageKey,
                          core::LogPrivacyClass::ExportSafe}},
              .occurrenceCount = diagnostic.occurrenceCount}},
          .attachments = {},
          .attachmentConsent = false,
          .createdAt = {}};
      auto exported = supportBundle_->exportBundle(request, destination);
      if (!exported) return core::Result<void>{exported.error()};
      lastError_ = exported.value().destination.string();
      return core::success();
    }
    case authoring::DiagnosticAction::ChooseVoicebank:
      authoring_->controller().showVoicebankBrowser();
      return core::success();
    case authoring::DiagnosticAction::RelinkVoicebank:
      if (applicationController_ == nullptr) {
        return core::failure(core::ErrorCode::InvalidState,
                             "Voicebank relink is unavailable");
      }
      return applicationController_->relinkVoicebankFromDialog();
    case authoring::DiagnosticAction::InstallVoicebank:
      if (applicationController_ == nullptr) {
        return core::failure(core::ErrorCode::InvalidState,
                             "Voicebank installation is unavailable");
      }
      return applicationController_->dispatch(
          platform::ApplicationCommand::InstallVoicebank);
    case authoring::DiagnosticAction::SaveAs:
      if (applicationController_ == nullptr) {
        return core::failure(core::ErrorCode::InvalidState,
                             "Save As is unavailable before application setup");
      }
      return applicationController_->dispatch(
          platform::ApplicationCommand::SaveProjectAs);
    case authoring::DiagnosticAction::OpenRecoveryFolder:
      if (recoveryRoot_.empty()) {
        return core::failure(core::ErrorCode::InvalidState,
                             "Recovery folder is unavailable");
      }
      {
        std::error_code error;
        std::filesystem::create_directories(recoveryRoot_, error);
        if (error) {
          return core::failure(core::ErrorCode::IoError,
                               "Unable to create recovery folder",
                               error.message());
        }
      }
      return platform::openExternalPath(recoveryRoot_);
    case authoring::DiagnosticAction::CopyDiagnostic: {
      std::string text = "Project SEAM diagnostic\n";
      text += "Code: " + diagnostic.code + "\n";
      text += "Message: " + diagnostic.messageKey + "\n";
      text += "Occurrences: " +
              std::to_string(diagnostic.occurrenceCount) + "\n";
      if (!diagnostic.affectedIds.empty()) {
        text += "Affected IDs:\n";
        for (const auto& id : diagnostic.affectedIds) {
          text += "- " + id + "\n";
        }
      }
      return platform::copyTextToClipboard(text);
    }
    case authoring::DiagnosticAction::OpenSettings:
      authoring_->controller().showAudioSettings();
      return core::success();
    case authoring::DiagnosticAction::RelinkMedia:
      if (applicationController_ == nullptr) {
        return core::failure(core::ErrorCode::InvalidState,
                             "Backing media relink is unavailable");
      }
      return applicationController_->relinkBackingMediaFromDialog();
  }
  return core::failure(core::ErrorCode::Unsupported,
                       "Unknown diagnostic action");
}

void NativeEditorApp::setWindow(native_ui::INativeWindow& window) noexcept {
  window_ = &window;
}

void NativeEditorApp::shutdownAudio() noexcept {
  stopAudioForPlayback();
  if (authoring_ != nullptr) {
    static_cast<void>(authoring_->runtime().transport().pause());
  }
}

core::Result<void> NativeEditorApp::openProject(
    const std::filesystem::path& path) {
  if (applicationController_ == nullptr) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Project lifecycle is not initialized");
  }
  const auto opened = applicationController_->openRecent(path);
  record(opened);
  return opened;
}

void NativeEditorApp::openProjectPath(
    const std::filesystem::path& path) noexcept {
  static_cast<void>(openProject(path));
}

std::optional<std::filesystem::path> NativeEditorApp::documentPath()
    const noexcept {
  if (authoring_ == nullptr) return std::nullopt;
  return authoring_->runtime().document().identity().projectPath;
}

void NativeEditorApp::paint(native_ui::RasterCanvas& canvas) noexcept {
  if (authoring_ == nullptr) return;
  if (applicationController_ != nullptr) {
    record(applicationController_->tickAutosave());
  }
  const auto transport = authoring_->runtime().transport().state();
  if (!transport.playing) stopAudioForPlayback();
  else if (transport.available && audioDevice_ != nullptr &&
           !audioDevice_->running()) {
    const auto started = startAudioForPlayback();
    if (!started) record(started);
  }
  const auto progress = authoring_->runtime().renderer().progress();
  authoring_->controller().setPlaying(transport.playing);
  authoring_->controller().setLoopEnabled(transport.loop.enabled);
  authoring_->controller().setRenderStatus(native_ui::RenderStatusView{
      .state = renderStatusState(progress.state),
      .requestedRevision = progress.requestedRevision,
      .audibleRevision = transport.publishedRevision,
      .requestedQuality = progress.requestedQuality,
      .audibleQuality = progress.publishedQuality,
      .completedPhrases = progress.completedPhrases,
      .totalPhrases = progress.totalPhrases,
      .fraction = progress.fraction,
      .hasAudibleAudio = transport.available,
      .audibleAudioStale = progress.audibleAudioStale,
      .diagnostic = progress.diagnostic,
      .activeVoicebankId = progress.activeVoicebankId,
      .activeVoicebankVersion = progress.activeVoicebankVersion,
      .activeRenderer = progress.activeRenderer,
  });
  const auto activeAudio = audioInfo();
  const auto audioOnline = audioDevice_ != nullptr && activeAudio.physical;
  const auto audioBackend = audioDevice_ != nullptr
                                ? activeAudio.backend
                                : std::string{"unavailable"};
  const auto currentScene = authoring_->controller().sceneState();
  if (currentScene.audioDeviceOnline != audioOnline ||
      currentScene.audioBackend != audioBackend) {
    authoring_->controller().setAudioState(audioOnline, audioBackend);
  }
  auto diagnostics = authoring_->runtime().diagnostics();
  if (audioDiagnostic_.has_value()) diagnostics.push_back(*audioDiagnostic_);
  authoring_->controller().setDiagnostics(std::move(diagnostics));
  const auto& project = authoring_->runtime().document().session().project();
  const auto tick = project.tempoMap().tickAtSampleFrame(
      transport.playhead, authoring_->runtime().transport().sampleRate());
  if (applicationController_ != nullptr) {
    authoring_->controller().setExportProgress(
        applicationController_->exportProgress().progress());
    authoring_->controller().setVoicebankCards(
        applicationController_->voicebankCards());
    authoring_->controller().setLastExport(
        applicationController_->lastExport());
  }
  if (const auto settings = audioSettings(); settings) {
    std::vector<native_ui::EditorSceneState::AudioDeviceOption> devices;
    if (const auto catalog = enumerateAudioDevices(); catalog) {
      devices.reserve(catalog.value().devices.size() + 1U);
      for (const auto& device : catalog.value().devices) {
        devices.push_back(native_ui::EditorSceneState::AudioDeviceOption{
            .id = device.id,
            .name = device.name,
            .physical = device.physical,
            .selected = device.id == settings.value().deviceId,
        });
      }
    }
    const auto activeDevice = std::find_if(
        devices.begin(), devices.end(), [&settings](const auto& device) {
          return device.id == settings.value().deviceId;
        });
    if (activeDevice == devices.end() && !settings.value().deviceId.empty()) {
      const auto info = audioInfo();
      devices.push_back(native_ui::EditorSceneState::AudioDeviceOption{
          .id = settings.value().deviceId,
          .name = settings.value().deviceId,
          .physical = info.physical,
          .selected = true,
      });
    }
    authoring_->controller().setAudioSettings(
        settings.value(), std::move(devices), processorStats().underflowFrames,
        audioStats().xruns);
  }
  auto state = authoring_->controller().sceneState();
  state.playheadPixel =
      authoring_->controller().pianoRoll().timeline().tickToPixel(tick);
  if (window_ != nullptr &&
      (progress.state == authoring::RenderState::Queued ||
       progress.state == authoring::RenderState::Rendering ||
       progress.state == authoring::RenderState::Stale)) {
    window_->requestRepaint();
  }
  character_.setDisplayMode(state.characterMode);
  character_.setState(state.characterState);
  state.characterPortrait = character_.portrait();
  authoring_->controller().setCharacterPortrait(state.characterPortrait);
  if (state.characterName.empty()) state.characterName = character_.displayName();
  if (state.characterStyle.empty()) state.characterStyle = character_.styleName();
  authoring_->controller().rebuildAccessibilityTree();
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
      command = platform::ApplicationCommand::ExportSet;
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
const native_ui::AccessibilityTree* NativeEditorApp::accessibilityTree()
    const noexcept {
  return authoring_ == nullptr ? nullptr
                               : &authoring_->controller().accessibilityTree();
}

core::Result<void> NativeEditorApp::dispatchAccessibility(
    std::string_view id, native_ui::SemanticAction action) noexcept {
  if (authoring_ == nullptr) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Accessibility dispatch requires an authoring session");
  }
  return authoring_->controller().dispatchAccessibility(id, action);
}

core::Result<void> NativeEditorApp::setAccessibilityValue(
    std::string_view id, std::string_view value) {
  if (authoring_ == nullptr) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Accessibility value setting requires an authoring session");
  }
  return authoring_->controller().setAccessibilityValue(id, value);
}

bool NativeEditorApp::requestClose() noexcept {
  if (applicationController_ == nullptr) return true;
  auto requested = applicationController_->requestClose();
  if (!requested) {
    record(core::Result<void>{requested.error()});
    return false;
  }
  const bool accepted = requested.value();
  if (accepted && window_ != nullptr) window_->saveRestorationState();
  return accepted;
}

bool NativeEditorApp::wantsClose() const noexcept {
  return closeRequested_.load(std::memory_order_acquire);
}

platform::AudioDeviceInfo NativeEditorApp::audioInfo() const {
  return audioDevice_ == nullptr ? platform::AudioDeviceInfo{}
                                 : audioDevice_->info();
}

core::Result<platform::AudioDeviceCatalogSnapshot>
NativeEditorApp::enumerateAudioDevices() {
  if (audioDeviceCatalog_ == nullptr) {
    return core::failure<platform::AudioDeviceCatalogSnapshot>(
        core::ErrorCode::InvalidState,
        "Audio device catalog is unavailable before audio initialization");
  }
  return audioDeviceCatalog_->enumerate();
}

core::Result<authoring::AudioSettings> NativeEditorApp::audioSettings() const {
  if (audioSettings_ == nullptr) {
    return core::failure<authoring::AudioSettings>(
        core::ErrorCode::InvalidState,
        "Audio settings are unavailable before audio initialization");
  }
  return audioSettings_->current();
}

core::Result<authoring::AudioSettings> NativeEditorApp::applyAudioSettings(
    authoring::AudioSettings requested) {
  if (audioSettings_ == nullptr) {
    return core::failure<authoring::AudioSettings>(
        core::ErrorCode::InvalidState,
        "Audio settings are unavailable before audio initialization");
  }
  if (requested.deviceId.empty()) requested.deviceId = audioSettings_->current().deviceId;
  if (requested.deviceId != audioSettings_->current().deviceId &&
      audioDeviceCatalog_ != nullptr) {
    const auto catalog = audioDeviceCatalog_->enumerate();
    if (!catalog) {
      return core::Result<authoring::AudioSettings>{catalog.error()};
    }
    const auto found = std::find_if(
        catalog.value().devices.begin(), catalog.value().devices.end(),
        [&requested](const auto& device) { return device.id == requested.deviceId; });
    if (found == catalog.value().devices.end()) {
      return core::failure<authoring::AudioSettings>(
          core::ErrorCode::NotFound,
          "Requested audio device is not present in the current catalog");
    }
    if (requested.blockFrames < found->minimumBlockFrames ||
        requested.blockFrames > found->maximumBlockFrames ||
        requested.outputChannels < found->minimumOutputChannels ||
        requested.outputChannels > found->maximumOutputChannels ||
        std::find(found->supportedSampleRates.begin(),
                  found->supportedSampleRates.end(), requested.sampleRate) ==
            found->supportedSampleRates.end()) {
      return core::failure<authoring::AudioSettings>(
          core::ErrorCode::InvalidArgument,
          "Requested audio settings exceed the selected device capabilities");
    }
  }
  const auto previous = audioSettings_->current();
  auto applied = audioSettings_->apply(std::move(requested));
  if (!applied) return applied;
  if (audioSettingsStore_ != nullptr) {
    const auto saved = audioSettingsStore_->save(applied.value());
    if (!saved) {
      static_cast<void>(audioSettings_->apply(previous));
      return core::Result<authoring::AudioSettings>{saved.error()};
    }
  }
  return applied;
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
