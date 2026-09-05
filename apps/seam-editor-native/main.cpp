#include "seam/standalone/native_editor_app.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/platform/application_paths.hpp"
#include "seam/standalone/production_configuration.hpp"
#if defined(__APPLE__)
#include "macos_application_delegate.hpp"
#endif

#include <chrono>
#include <clocale>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct CommandLine final {
  std::chrono::milliseconds autoClose{0};
  std::optional<std::filesystem::path> screenshot;
  std::uint32_t windowWidth{1440U};
  std::uint32_t windowHeight{900U};
  double scale{1.0};
  seam::standalone::ProductionRuntimeMode mode{
      seam::standalone::ProductionRuntimeMode::Release};
  bool forceThreadedAudio{false};
  bool startPaused{true};
  std::filesystem::path characterPackage;
  std::vector<std::filesystem::path> voicebankRoots;
  std::vector<std::filesystem::path> trustedVoicebankKeyFiles;
  std::optional<std::filesystem::path> developmentTrustKeyFile;
  bool allowDevelopmentVoicebanks{false};
  std::optional<std::filesystem::path> applicationSupportRoot;
  std::vector<std::filesystem::path> openProjects;
};

void printUsage() {
  std::cout
      << "Usage: seam_editor_native [options]\n"
      << "  --auto-close-ms N       close automatically after N milliseconds\n"
      << "  --screenshot PATH       write the final software-raster frame as PPM\n"
      << "  --window-width N        physical window width from 320 to 8192\n"
      << "  --window-height N       physical window height from 240 to 8192\n"
      << "  --scale N               logical UI scale from 0.5 to 4.0\n"
      << "  --force-threaded-audio  skip physical system audio\n"
      << "  --play                  start transport in development mode\n"
      << "  --paused                do not start transport automatically\n"
      << "  --development           opt into development runtime behavior\n"
      << "  --deterministic-test    opt into nonphysical deterministic audio\n"
      << "  --character-package P   character package root\n"
      << "  --voicebank-root P      add an exact search root (development in non-release modes)\n"
      << "  --trusted-voicebank-key P  trust an Ed25519 public key for installs\n"
      << "  --development-trust-key P  built-in development signing key\n"
      << "  --deny-development-voicebanks  hide development fixtures\n"
      << "  --application-support-root P  override application support root\n";
}

std::optional<CommandLine> parseArguments(int argc, char** argv) {
  CommandLine result;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--help") {
      printUsage();
      return std::nullopt;
    }
    if (argument == "--force-threaded-audio") {
      result.forceThreadedAudio = true;
      if (result.mode == seam::standalone::ProductionRuntimeMode::Release) {
        result.mode = seam::standalone::ProductionRuntimeMode::DeterministicTest;
        result.startPaused = true;
      }
      continue;
    }
    if (argument == "--paused") {
      result.startPaused = true;
      continue;
    }
    if (argument == "--play") {
      result.startPaused = false;
      continue;
    }
    if (argument == "--development") {
      result.mode = seam::standalone::ProductionRuntimeMode::Development;
      result.allowDevelopmentVoicebanks = true;
      continue;
    }
    if (argument == "--deterministic-test") {
      result.mode = seam::standalone::ProductionRuntimeMode::DeterministicTest;
      result.allowDevelopmentVoicebanks = true;
      result.forceThreadedAudio = true;
      result.startPaused = true;
      continue;
    }
    if (argument == "--character-package" && index + 1 < argc) {
      result.characterPackage = std::filesystem::path{argv[++index]};
      continue;
    }
    if (argument == "--voicebank-root" && index + 1 < argc) {
      result.voicebankRoots.emplace_back(argv[++index]);
      continue;
    }
    if (argument == "--trusted-voicebank-key" && index + 1 < argc) {
      result.trustedVoicebankKeyFiles.emplace_back(argv[++index]);
      continue;
    }
    if (argument == "--development-trust-key" && index + 1 < argc) {
      result.developmentTrustKeyFile = std::filesystem::path{argv[++index]};
      continue;
    }
    if (argument == "--deny-development-voicebanks") {
      result.allowDevelopmentVoicebanks = false;
      continue;
    }
    if (argument == "--application-support-root" && index + 1 < argc) {
      result.applicationSupportRoot = std::filesystem::path{argv[++index]};
      continue;
    }
    if (argument == "--auto-close-ms" && index + 1 < argc) {
      try {
        const auto value = std::stoll(argv[++index]);
        if (value < 0 || value > 600000) return std::nullopt;
        result.autoClose = std::chrono::milliseconds{value};
      } catch (...) {
        return std::nullopt;
      }
      continue;
    }
    if (argument == "--screenshot" && index + 1 < argc) {
      result.screenshot = std::filesystem::path{argv[++index]};
      continue;
    }
    if ((argument == "--window-width" || argument == "--window-height") &&
        index + 1 < argc) {
      try {
        const auto value = std::stoul(argv[++index]);
        if (value < (argument == "--window-width" ? 320U : 240U) ||
            value > 8192U) {
          return std::nullopt;
        }
        if (argument == "--window-width") {
          result.windowWidth = static_cast<std::uint32_t>(value);
        } else {
          result.windowHeight = static_cast<std::uint32_t>(value);
        }
      } catch (...) {
        return std::nullopt;
      }
      continue;
    }
    if (argument == "--scale" && index + 1 < argc) {
      try {
        result.scale = std::stod(argv[++index]);
      } catch (...) {
        return std::nullopt;
      }
      if (!std::isfinite(result.scale) || result.scale < 0.5 ||
          result.scale > 4.0) {
        return std::nullopt;
      }
      continue;
    }
    if (!argument.empty() && argument.front() != '-') {
      result.openProjects.emplace_back(argument);
      continue;
    }
    std::cerr << "Unknown argument: " << argument << '\n';
    return std::nullopt;
  }
  return result;
}

std::optional<std::vector<seam::distribution::Ed25519PublicKey>>
trustedVoicebankKeys(const CommandLine& commandLine) {
  std::vector<seam::distribution::Ed25519PublicKey> result;
  for (const auto& path : commandLine.trustedVoicebankKeyFiles) {
    auto loaded = seam::distribution::loadPublicKey(path);
    if (!loaded) {
      std::cerr << "Unable to load trusted voicebank key: "
                << loaded.error().message << " (" << path << ")\n";
      return std::nullopt;
    }
    result.push_back(loaded.value());
  }
  return result;
}

std::optional<seam::distribution::Ed25519PublicKey>
developmentTrustRoot(const CommandLine& commandLine, bool& valid) {
  valid = true;
  if (!commandLine.developmentTrustKeyFile.has_value()) return std::nullopt;
  auto loaded = seam::distribution::loadPublicKey(
      *commandLine.developmentTrustKeyFile);
  if (!loaded) {
    std::cerr << "Unable to load development voicebank key: "
              << loaded.error().message << "\n";
    valid = false;
    return std::nullopt;
  }
  return loaded.value();
}

seam::core::Result<void> startTransportWhenReady(
    seam::standalone::NativeEditorApp& app,
    std::chrono::milliseconds timeout) {
  auto& transport = app.authoring().runtime().transport();
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!transport.state().available &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  const auto state = transport.state();
  if (!state.available) {
    return seam::core::failure(
        seam::core::ErrorCode::Conflict,
        "Requested playback did not receive a rendered timeline before timeout");
  }
  if (!state.playing) {
    const auto played = transport.play();
    if (!played) return played;
  }
  return app.startAudioForPlayback();
}

}  // namespace

int seam_editor_native_main(int argc, char** argv) {
  static_cast<void>(std::setlocale(LC_ALL, ""));
  const auto commandLine = parseArguments(argc, argv);
  if (!commandLine.has_value()) {
    if (argc > 1 && std::string_view{argv[1]} == "--help") return 0;
    printUsage();
    return 2;
  }

  const auto trustedKeys = trustedVoicebankKeys(*commandLine);
  if (!trustedKeys.has_value()) return 3;
  bool developmentKeyValid = true;
  const auto developmentKey =
      developmentTrustRoot(*commandLine, developmentKeyValid);
  if (!developmentKeyValid) return 3;

  const auto paths = seam::platform::applicationPaths(
      std::filesystem::path{argv[0]});
  if (!paths) {
    std::cerr << "Unable to resolve application paths: "
              << paths.error().message << '\n';
    return 3;
  }
  std::vector<seam::voicebank::VoicebankSearchRoot> requestedRoots;
  requestedRoots.reserve(commandLine->voicebankRoots.size());
  for (const auto& path : commandLine->voicebankRoots) {
    requestedRoots.push_back(seam::voicebank::VoicebankSearchRoot{
        .path = path,
        .kind = commandLine->mode ==
                        seam::standalone::ProductionRuntimeMode::Release
                    ? seam::voicebank::VoicebankRootKind::Installed
                    : seam::voicebank::VoicebankRootKind::Development,
    });
  }
  const auto production = seam::standalone::makeProductionConfiguration(
      seam::standalone::ProductionConfigurationInput{
          .mode = commandLine->mode,
          .paths = paths.value(),
          .voicebankRoots = std::move(requestedRoots),
          .trustedVoicebankKeys = *trustedKeys,
          .developmentTrustRoot = developmentKey,
          .allowDevelopmentVoicebanks =
              commandLine->allowDevelopmentVoicebanks,
          .forceThreadedAudio = commandLine->forceThreadedAudio,
          .bindFirstAvailableVoicebank = false,
          .startPaused = commandLine->startPaused,
          .sampleRate = 48000U,
          .outputChannels = 2U,
          .audioBlockFrames = 256U,
          .characterPackage = commandLine->characterPackage,
      });
  if (!production) {
    std::cerr << "Invalid production runtime configuration: "
              << production.error().message << '\n';
    return 3;
  }
  const auto& runtime = production.value();

  auto created = seam::standalone::NativeEditorApp::create(
      seam::standalone::NativeEditorAppConfig{
          .authoring = seam::standalone::AuthoringSessionConfig{
              .cacheRoot = runtime.cacheRoot,
              .voicebankRoots = runtime.voicebankRoots,
              .sampleRate = runtime.sampleRate,
              .outputChannels = runtime.outputChannels,
              .bindFirstAvailableVoicebank =
                  runtime.bindFirstAvailableVoicebank,
              .allowDevelopmentVoicebanks =
                  runtime.allowDevelopmentVoicebanks,
          },
          .runtimeMode = runtime.mode,
          .characterPackage = runtime.characterPackage,
          .applicationSupportRoot = commandLine->applicationSupportRoot.value_or(
              runtime.applicationSupportRoot),
          .trustedVoicebankKeys = runtime.trustedVoicebankKeys,
          .developmentTrustRoot = runtime.developmentTrustRoot,
          .allowDevelopmentVoicebanks = runtime.allowDevelopmentVoicebanks,
          .audioBlockFrames = runtime.audioBlockFrames,
          .forceThreadedAudio = runtime.forceThreadedAudio,
          .startPaused = runtime.startPaused,
          .manualsRoot = paths.value().manualsRoot,
      });
  if (!created) {
    std::cerr << "Standalone initialization failed: "
              << created.error().message;
    if (!created.error().context.empty()) {
      std::cerr << " (" << created.error().context << ')';
    }
    std::cerr << '\n';
    return 3;
  }
  auto app = std::move(created).value();
#if defined(__APPLE__)
  auto macosDelegate =
      seam::standalone::macos::ApplicationDelegateHandle::install(*app);
  if (!macosDelegate) {
    std::cerr << "Unable to install the macOS application delegate: "
              << macosDelegate.error().message << '\n';
    return 4;
  }
#endif
  auto window = seam::native_ui::createNativeWindow();
  app->setWindow(*window);
  const auto opened = window->open(
      seam::native_ui::NativeWindowConfig{
          .title = "Project SEAM / Production Standalone",
          .width = commandLine->windowWidth,
          .height = commandLine->windowHeight,
          .scale = commandLine->scale,
          .restoreLastDocument = commandLine->openProjects.empty(),
          .autoCloseAfter = commandLine->autoClose,
          .screenshotPath = commandLine->screenshot,
      },
      *app);
  if (!opened) {
    std::cerr << "Native window failed: " << opened.error().message << '\n';
    return 4;
  }

  for (const auto& path : commandLine->openProjects) {
    const auto startupProject = app->openProject(path);
    if (!startupProject) {
      std::cerr << "Unable to open startup project: "
                << startupProject.error().message;
      if (!startupProject.error().context.empty()) {
        std::cerr << " (" << startupProject.error().context << ')';
      }
      std::cerr << '\n';
      return 5;
    }
  }
  if (!runtime.startPaused) {
    const auto started =
        startTransportWhenReady(*app, std::chrono::seconds{10});
    if (!started) {
      std::cerr << "Unable to start requested playback: "
                << started.error().message << '\n';
      return 5;
    }
  }

  const auto result = window->run();
  app->shutdownAudio();
  const auto info = app->audioInfo();
  const auto audio = app->audioStats();
  const auto processor = app->processorStats();
  const auto resolution = app->authoring().voicebankResolution();
  const auto render = app->authoring().runtime().renderer().progress();
  const auto transport = app->authoring().runtime().transport().state();
  std::cout << "window_backend=" << window->backendName() << '\n'
            << "audio_backend=" << info.backend << '\n'
            << "audio_physical=" << (info.physical ? "true" : "false") << '\n'
            << "audio_callbacks=" << audio.callbacks << '\n'
            << "audio_frames=" << audio.frames << '\n'
            << "audio_write_failures=" << audio.writeFailures << '\n'
            << "audio_xruns=" << audio.xruns << '\n'
            << "callback_delivered=" << processor.deliveredFrames << '\n'
            << "callback_underflow=" << processor.underflowFrames << '\n'
            << "callback_intentional_reset="
            << processor.intentionalResetFrames << '\n'
            << "voicebank_resolved="
            << (resolution.resolved() ? "true" : "false") << '\n'
            << "render_state=" << seam::authoring::renderStateName(render.state)
            << '\n'
            << "render_requested_revision=" << render.requestedRevision << '\n'
            << "render_published_revision=" << render.publishedRevision << '\n'
            << "render_diagnostic=" << render.diagnostic << '\n'
            << "transport_playing=" << (transport.playing ? "true" : "false")
            << '\n'
            << "transport_available="
            << (transport.available ? "true" : "false") << '\n'
            << "transport_published_revision=" << transport.publishedRevision
            << '\n';
  if (!app->lastError().empty()) {
    std::cerr << "last_editor_error=" << app->lastError() << '\n';
  }
  return result;
}

#if !defined(_WIN32)
int main(int argc, char** argv) { return seam_editor_native_main(argc, argv); }
#endif
