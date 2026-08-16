#include "seam/native_ui/native_window.hpp"
#include "seam/native_ui/voicebank_studio.hpp"
#include "seam/platform/audio_input_device.hpp"
#include "seam/platform/recording_session.hpp"

#include <atomic>
#include <memory>
#include <algorithm>
#include <chrono>
#include <clocale>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct Options final {
  std::filesystem::path manifest;
  std::optional<std::filesystem::path> screenshot;
  std::chrono::milliseconds autoClose{0};
  bool forceSyntheticInput{false};
};

void usage() {
  std::cout << "Usage: seam_voicebank_studio_native --manifest PATH [options]\n"
            << "  --screenshot PATH        write final PPM screenshot\n"
            << "  --auto-close-ms N        close after N milliseconds\n"
            << "  --force-synthetic-input  skip physical microphone capture\n";
}

std::optional<Options> parse(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    if (arg == "--help") {
      usage();
      return std::nullopt;
    }
    if (arg == "--manifest" && index + 1 < argc) {
      options.manifest = std::filesystem::path{argv[++index]};
      continue;
    }
    if (arg == "--screenshot" && index + 1 < argc) {
      options.screenshot = std::filesystem::path{argv[++index]};
      continue;
    }
    if (arg == "--force-synthetic-input") {
      options.forceSyntheticInput = true;
      continue;
    }
    if (arg == "--auto-close-ms" && index + 1 < argc) {
      try {
        const auto value = std::stoll(argv[++index]);
        if (value < 0 || value > 600000) return std::nullopt;
        options.autoClose = std::chrono::milliseconds{value};
      } catch (...) {
        return std::nullopt;
      }
      continue;
    }
    std::cerr << "Unknown argument: " << arg << '\n';
    return std::nullopt;
  }
  if (options.manifest.empty()) return std::nullopt;
  return options;
}

class VoicebankStudioApp final : public seam::native_ui::INativeWindowClient {
public:
  explicit VoicebankStudioApp(bool forceSyntheticInput)
      : forceSyntheticInput_(forceSyntheticInput), recording_(48000U, 300U) {}

  ~VoicebankStudioApp() override { stopRecording(); }

  seam::core::Result<void> open(const std::filesystem::path& manifest) {
    auto loaded = controller_.openManifest(manifest);
    if (!loaded) return loaded;
    return initializeInput();
  }

  void setWindow(seam::native_ui::INativeWindow& window) noexcept { window_ = &window; }

  void paint(seam::native_ui::RasterCanvas& canvas) noexcept override {
    painter_.paint(canvas, controller_, recording_.armed(), inputBackend_);
  }
  void resized(double width, double height, double) noexcept override {
    controller_.resize(width, height);
  }

  void pointerDown(const seam::native_ui::PointerEvent& event) noexcept override {
    if (event.button != seam::native_ui::PointerButton::Left) return;
    if (const auto marker = controller_.microscope().hitTestMarker(event.position, 6.0);
        marker.has_value()) {
      markerDrag_ = *marker;
      pitchDrag_.reset();
      return;
    }
    if (const auto pitch = controller_.microscope().hitTestPitchMark(event.position, 5.0);
        pitch.has_value()) {
      pitchDrag_ = *pitch;
      markerDrag_.reset();
      return;
    }
    // Unit-list rows are stable 32px entries in the current scene.
    if (event.position.x >= 8.0 && event.position.x < 244.0 &&
        event.position.y >= 108.0) {
      const auto offset = static_cast<std::size_t>((event.position.y - 108.0) / 32.0);
      const auto first = controller_.selectedIndex() > 8U
                             ? controller_.selectedIndex() - 8U
                             : 0U;
      const auto index = first + offset;
      if (index < controller_.manifest().units.size()) record(controller_.selectUnit(index));
    }
  }

  void pointerMove(const seam::native_ui::PointerEvent& event) noexcept override {
    if (markerDrag_.has_value()) {
      record(controller_.moveSelectedMarker(*markerDrag_, event.position.x));
      repaint();
    } else if (pitchDrag_.has_value()) {
      record(controller_.moveSelectedPitchMark(*pitchDrag_, event.position.x));
      repaint();
    }
  }
  void pointerUp(const seam::native_ui::PointerEvent&) noexcept override {
    markerDrag_.reset();
    pitchDrag_.reset();
  }
  void scroll(double, double, seam::ui::Point,
              seam::native_ui::InputModifiers) noexcept override {}

  void keyDown(const seam::native_ui::KeyEvent& event) noexcept override {
    const auto size = controller_.manifest().units.size();
    if (event.key == seam::native_ui::NativeKey::Up && controller_.selectedIndex() > 0U) {
      record(controller_.selectUnit(controller_.selectedIndex() - 1U));
    } else if (event.key == seam::native_ui::NativeKey::Down &&
               controller_.selectedIndex() + 1U < size) {
      record(controller_.selectUnit(controller_.selectedIndex() + 1U));
    } else if (event.key == seam::native_ui::NativeKey::S &&
               event.modifiers.primaryShortcut()) {
      record(controller_.save());
    } else if (event.key == seam::native_ui::NativeKey::R) {
      if (recording_.armed()) stopRecording(); else startRecording();
    }
    repaint();
  }

  void textComposition(std::u32string, seam::ui::CompositionSelection) noexcept override {}
  void textCommit(std::u32string) noexcept override {}
  void textCancel() noexcept override {}
  bool wantsClose() const noexcept override { return false; }

  [[nodiscard]] const std::string& lastError() const noexcept { return lastError_; }
  [[nodiscard]] const std::filesystem::path& lastRecording() const noexcept {
    return lastRecording_;
  }

private:
  seam::core::Result<void> initializeInput() {
    seam::platform::AudioInputDeviceConfig config{
        .sampleRate = 48000U,
        .blockFrames = 256U,
        .applicationName = "Project SEAM",
        .streamName = "Voicebank Studio recording",
    };
    if (!forceSyntheticInput_) {
      auto physical = seam::platform::createSystemAudioInputDevice();
      auto opened = physical->open(config, recording_);
      if (opened) input_ = std::move(physical);
      else lastError_ = opened.error().message;
    }
    if (input_ == nullptr) {
      auto fallback = seam::platform::createThreadedSilenceInputDevice();
      auto opened = fallback->open(config, recording_);
      if (!opened) return opened;
      input_ = std::move(fallback);
    }
    inputBackend_ = input_->info().backend;
    return seam::core::success();
  }

  void startRecording() noexcept {
    if (input_ == nullptr) return;
    const auto armed = recording_.arm();
    if (!armed) {
      lastError_ = armed.error().message;
      return;
    }
    const auto started = input_->start();
    if (!started) {
      recording_.stop();
      lastError_ = started.error().message;
    }
  }

  void stopRecording() noexcept {
    if (input_ != nullptr) input_->stop();
    if (!recording_.armed() && recording_.recordedFrames() == 0U) return;
    recording_.stop();
    if (recording_.recordedFrames() == 0U) return;
    std::error_code error;
    auto directory = controller_.manifestPath().parent_path() / "recordings";
    std::filesystem::create_directories(directory, error);
    if (error) {
      lastError_ = error.message();
      return;
    }
    auto name = controller_.selectedUnit() == nullptr
                    ? std::string{"take"}
                    : controller_.selectedUnit()->id;
    std::replace(name.begin(), name.end(), '/', '_');
    std::replace(name.begin(), name.end(), ':', '_');
    lastRecording_ = directory / (name + "-recorded.wav");
    const auto saved = recording_.exportWav(lastRecording_);
    if (!saved) lastError_ = saved.error().message;
    recording_.clear();
  }

  void repaint() noexcept {
    if (window_ != nullptr) window_->requestRepaint();
  }
  void record(const seam::core::Result<void>& result) noexcept {
    if (!result) lastError_ = result.error().message;
  }

  bool forceSyntheticInput_{false};
  seam::native_ui::VoicebankStudioController controller_;
  seam::native_ui::VoicebankStudioScenePainter painter_;
  seam::platform::RecordingSession recording_;
  std::unique_ptr<seam::platform::IAudioInputDevice> input_;
  std::string inputBackend_{"OFF"};
  std::optional<seam::ui::AcousticMarkerKind> markerDrag_;
  std::optional<std::size_t> pitchDrag_;
  seam::native_ui::INativeWindow* window_{nullptr};
  std::string lastError_;
  std::filesystem::path lastRecording_;
};

}  // namespace

int main(int argc, char** argv) {
  static_cast<void>(std::setlocale(LC_ALL, ""));
  const auto options = parse(argc, argv);
  if (!options.has_value()) {
    if (argc > 1 && std::string_view{argv[1]} == "--help") return 0;
    usage();
    return 2;
  }

  VoicebankStudioApp app{options->forceSyntheticInput};
  const auto openedBank = app.open(options->manifest);
  if (!openedBank) {
    std::cerr << "Voicebank Studio load failed: " << openedBank.error().message;
    if (!openedBank.error().context.empty()) std::cerr << " (" << openedBank.error().context << ')';
    std::cerr << '\n';
    return 3;
  }
  auto window = seam::native_ui::createNativeWindow();
  app.setWindow(*window);
  const auto opened = window->open(seam::native_ui::NativeWindowConfig{
      .title = "Project SEAM / Voicebank Studio",
      .width = 1440U,
      .height = 900U,
      .scale = 1.0,
      .autoCloseAfter = options->autoClose,
      .screenshotPath = options->screenshot,
  }, app);
  if (!opened) {
    std::cerr << "Native Voicebank Studio unavailable: " << opened.error().message << '\n';
    return 4;
  }
  return window->run();
}
