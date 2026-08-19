#include "seam/standalone/native_editor_app.hpp"
#include "seam/voicebank/catalog.hpp"

#include <chrono>
#include <clocale>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct CommandLine final {
  std::chrono::milliseconds autoClose{0};
  std::optional<std::filesystem::path> screenshot;
  double scale{1.0};
  bool forceThreadedAudio{false};
  bool startPaused{false};
  std::filesystem::path characterPackage{"assets/character-01"};
  std::vector<std::filesystem::path> voicebankRoots;
};

void printUsage() {
  std::cout
      << "Usage: seam_editor_native [options]\n"
      << "  --auto-close-ms N       close automatically after N milliseconds\n"
      << "  --screenshot PATH       write the final software-raster frame as PPM\n"
      << "  --scale N               logical UI scale from 0.5 to 4.0\n"
      << "  --force-threaded-audio  skip physical system audio\n"
      << "  --paused                do not start transport automatically\n"
      << "  --character-package P   character package root\n"
      << "  --voicebank-root P      add an exact voicebank search root\n";
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
      continue;
    }
    if (argument == "--paused") {
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
    std::cerr << "Unknown argument: " << argument << '\n';
    return std::nullopt;
  }
  return result;
}

std::vector<seam::voicebank::VoicebankSearchRoot> voicebankRoots(
    const CommandLine& commandLine) {
  auto roots = seam::voicebank::defaultVoicebankSearchRoots();
  for (const auto& path : commandLine.voicebankRoots) {
    roots.push_back(seam::voicebank::VoicebankSearchRoot{
        .path = path,
        .kind = seam::voicebank::VoicebankRootKind::Installed,
    });
  }
#ifdef SEAM_SOURCE_PRODUCTION_VOICEBANK
  roots.push_back(seam::voicebank::VoicebankSearchRoot{
      .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
      .kind = seam::voicebank::VoicebankRootKind::Development,
  });
#endif
  return roots;
}

std::filesystem::path previewCacheRoot() {
  std::error_code error;
  auto root = std::filesystem::temp_directory_path(error);
  if (error) root = std::filesystem::current_path(error);
  return root / "project-seam" / "standalone-preview-cache";
}

}  // namespace

int main(int argc, char** argv) {
  static_cast<void>(std::setlocale(LC_ALL, ""));
  const auto commandLine = parseArguments(argc, argv);
  if (!commandLine.has_value()) {
    if (argc > 1 && std::string_view{argv[1]} == "--help") return 0;
    printUsage();
    return 2;
  }

  auto created = seam::standalone::NativeEditorApp::create(
      seam::standalone::NativeEditorAppConfig{
          .authoring = seam::standalone::AuthoringSessionConfig{
              .cacheRoot = previewCacheRoot(),
              .voicebankRoots = voicebankRoots(*commandLine),
              .sampleRate = 48000U,
              .outputChannels = 2U,
              .bindFirstAvailableVoicebank = true,
          },
          .characterPackage = commandLine->characterPackage,
          .applicationSupportRoot = {},
          .audioBlockFrames = 256U,
          .forceThreadedAudio = commandLine->forceThreadedAudio,
          .startPaused = commandLine->startPaused,
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
  auto window = seam::native_ui::createNativeWindow();
  app->setWindow(*window);
  const auto opened = window->open(
      seam::native_ui::NativeWindowConfig{
          .title = "Project SEAM / Production Standalone",
          .width = 1440U,
          .height = 900U,
          .scale = commandLine->scale,
          .autoCloseAfter = commandLine->autoClose,
          .screenshotPath = commandLine->screenshot,
      },
      *app);
  if (!opened) {
    std::cerr << "Native window failed: " << opened.error().message << '\n';
    return 4;
  }

  const auto result = window->run();
  app->shutdownAudio();
  const auto info = app->audioInfo();
  const auto audio = app->audioStats();
  const auto processor = app->processorStats();
  const auto resolution = app->authoring().voicebankResolution();
  std::cout << "window_backend=" << window->backendName() << '\n'
            << "audio_backend=" << info.backend << '\n'
            << "audio_physical=" << (info.physical ? "true" : "false") << '\n'
            << "audio_callbacks=" << audio.callbacks << '\n'
            << "callback_delivered=" << processor.deliveredFrames << '\n'
            << "callback_underflow=" << processor.underflowFrames << '\n'
            << "voicebank_resolved="
            << (resolution.resolved() ? "true" : "false") << '\n';
  if (!app->lastError().empty()) {
    std::cerr << "last_editor_error=" << app->lastError() << '\n';
  }
  return result;
}
