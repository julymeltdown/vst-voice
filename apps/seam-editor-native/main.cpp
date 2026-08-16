#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/core/result.hpp"
#include "seam/native_ui/character_presentation.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/native_window.hpp"
#include "seam/platform/audio_device.hpp"
#include "seam/platform/ring_buffer_processor.hpp"
#include "seam/rendering/audio_ring_buffer.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/playback_engine.hpp"
#include "seam/rendering/playback_feeder_service.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

struct CommandLine final {
  std::chrono::milliseconds autoClose{0};
  std::optional<std::filesystem::path> screenshot;
  double scale{1.0};
  bool forceThreadedAudio{false};
  bool startPaused{false};
  std::filesystem::path characterPackage{"assets/character-01"};
};

void printUsage() {
  std::cout
      << "Usage: seam_editor_native [options]\n"
      << "  --auto-close-ms N       close automatically after N milliseconds\n"
      << "  --screenshot PATH       write the final software-raster frame as PPM\n"
      << "  --scale N               logical UI scale from 0.5 to 4.0\n"
      << "  --force-threaded-audio  skip physical system audio\n"
      << "  --paused                do not start transport automatically\n"
      << "  --character-package P   character package root (default assets/character-01)\n";
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

seam::domain::Project makeDemoProject(
    seam::application::ProjectFactory& factory,
    seam::domain::RegionId& regionId) {
  auto project = factory.createProject("SEAM / NATIVE SESSION");
  static_cast<void>(project.tempoMap().addOrReplace(seam::time::Tick{0}, 154.0));
  const auto trackId = factory.addVocalTrack(project, "VOICE 01");
  regionId = factory.addRegion(project, trackId, "VERSE A",
                               seam::time::Tick{0}, seam::time::Tick{15360});
  auto* region = project.findRegion(regionId);
  project.settings().characterDisplay = seam::domain::CharacterDisplayMode::Full;
  if (auto* track = project.findVocalTrack(trackId); track != nullptr) {
    track->voicebank = seam::domain::VoicebankReference{
        .id = "official.voice.01", .version = "0.1.0", .contentHash = "demo"};
    track->character = seam::domain::CharacterReference{
        .id = "official.character.01", .version = "0.1.0"};
  }
  const std::vector<std::tuple<std::int64_t, std::int64_t, std::uint8_t,
                               std::u32string>> notes{
      {0, 720, 64U, U"こ"},       {720, 480, 67U, U"え"},
      {1200, 960, 69U, U"を"},    {2400, 480, 67U, U"つ"},
      {2880, 720, 64U, U"な"},    {3600, 960, 62U, U"ぐ"},
      {4800, 480, 64U, U"ま"},    {5280, 480, 67U, U"ま"},
      {5760, 1440, 71U, U"で"},   {7440, 480, 69U, U"い"},
      {7920, 960, 67U, U"い"},    {9360, 1920, 64U, U"よ"},
  };
  for (const auto& [startTick, duration, key, lyricText] : notes) {
    auto [lyric, note] = factory.makeNote(
        seam::time::Tick{startTick}, seam::time::Tick{duration}, key, lyricText,
        seam::domain::Language::Japanese);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  }
  region->sortNotes();
  if (region->notes.size() >= 4U) {
    region->unitSelectionOverrides.push_back(seam::domain::UnitSelectionOverride{
        .startKey = seam::domain::PhonemeKey{.noteId = region->notes[0].id, .ordinal = 0U},
        .tokenCount = 2U,
        .unitId = "ja.original.mid.k-o.01",
        .renderer = seam::domain::UnitRendererKind::ClassicPsola,
        .locked = true,
    });
    region->unitSelectionOverrides.push_back(seam::domain::UnitSelectionOverride{
        .startKey = seam::domain::PhonemeKey{.noteId = region->notes[3].id, .ordinal = 0U},
        .tokenCount = 2U,
        .unitId = "ja.original.mid.ts-u.01",
        .renderer = seam::domain::UnitRendererKind::SpectralClassic,
        .locked = true,
    });
  }
  static_cast<void>(region->pitchAutomation.upsert(seam::domain::PitchAutomationPoint{
      .tick = seam::time::Tick{0}, .cents = -18.0F,
      .interpolation = seam::domain::CurveInterpolation::Smooth}));
  static_cast<void>(region->pitchAutomation.upsert(seam::domain::PitchAutomationPoint{
      .tick = seam::time::Tick{2400}, .cents = 42.0F,
      .interpolation = seam::domain::CurveInterpolation::Linear}));
  static_cast<void>(region->pitchAutomation.upsert(seam::domain::PitchAutomationPoint{
      .tick = seam::time::Tick{5760}, .cents = -34.0F,
      .interpolation = seam::domain::CurveInterpolation::Smooth}));
  region->sortNotes();
  return project;
}

std::shared_ptr<const seam::rendering::PlaybackTimeline> makeDemoTimeline() {
  constexpr std::uint32_t sampleRate = 48000U;
  constexpr std::size_t durationFrames = sampleRate * 16U;
  std::vector<float> samples(durationFrames, 0.0F);
  for (std::size_t frame = 0U; frame < samples.size(); ++frame) {
    const auto seconds = static_cast<double>(frame) /
                         static_cast<double>(sampleRate);
    const auto phrasePosition = std::fmod(seconds, 2.0) / 2.0;
    const auto attack = std::min(1.0, phrasePosition * 18.0);
    const auto release = std::min(1.0, (1.0 - phrasePosition) * 7.0);
    const auto envelope = attack * release;
    const auto root = frame / (sampleRate * 2U) % 4U;
    const auto frequency = root == 0U ? 164.81
                          : root == 1U ? 196.00
                          : root == 2U ? 220.00
                                       : 174.61;
    const auto base = std::sin(2.0 * std::numbers::pi * frequency * seconds);
    const auto upper = std::sin(2.0 * std::numbers::pi * frequency * 2.01 *
                                seconds + 0.2);
    samples[frame] = static_cast<float>((base * 0.12 + upper * 0.035) *
                                        envelope);
  }
  auto pcm = std::make_shared<const seam::rendering::CachedPcm>(
      seam::rendering::CachedPcm{
          .sampleRate = sampleRate,
          .startFrame = 0,
          .samples = std::move(samples),
      });
  auto timeline = std::make_shared<seam::rendering::PlaybackTimeline>(
      sampleRate);
  const auto added = timeline->addClip(seam::rendering::PlaybackClip{
      .id = "native-preview",
      .pcm = std::move(pcm),
      .gain = 1.0F,
      .fadeInFrames = 240,
      .fadeOutFrames = 480,
      .enabled = true,
  });
  if (!added) return {};
  return timeline;
}

class NativeEditorApp final : public seam::native_ui::INativeWindowClient {
public:
  NativeEditorApp()
      : factory_(1000U),
        session_(makeDemoProject(factory_, regionId_)),
        ring_(32768U),
        feeder_(ring_, 48000U, 1024U, 128U),
        feederService_(feeder_, seam::rendering::PlaybackFeederServiceConfig{
                                    .targetBufferedFrames = 16384U,
                                    .activePollInterval = 250us,
                                    .idlePollInterval = 3ms,
                                }),
        processor_(ring_),
        controller_(
            session_, factory_, regionId_,
            seam::native_ui::EditorHostCallbacks{
                .requestRepaint = [this] {
                  if (window_ != nullptr) window_->requestRepaint();
                },
                .beginTextInput = [this](
                                      const seam::native_ui::TextInputRequest& request) {
                  if (window_ != nullptr) window_->beginTextInput(request);
                },
                .endTextInput = [this] {
                  if (window_ != nullptr) window_->endTextInput();
                },
                .setPlaying = [this](bool playing) {
                  const auto result = feederService_.setPlaying(playing);
                  if (!result) lastError_ = result.error().message;
                },
            }) {}

  ~NativeEditorApp() override { shutdownAudio(); }

  seam::core::Result<void> initializeCharacter(
      const std::filesystem::path& packageRoot) {
    auto result = character_.load(packageRoot);
    if (!result) return result;
    controller_.setCharacterMetadata(character_.displayName(), character_.styleName());
    return seam::core::success();
  }

  seam::core::Result<void> initializeAudio(bool forceThreaded,
                                            bool startPaused) {
    const auto timeline = makeDemoTimeline();
    if (timeline == nullptr) {
      return seam::core::failure(seam::core::ErrorCode::Internal,
                                 "Unable to construct the preview timeline");
    }
    auto result = feederService_.setTimeline(timeline);
    if (!result) return result;
    result = feederService_.setLoop(seam::rendering::PlaybackLoop{
        .enabled = true,
        .startFrame = 0,
        .endFrame = timeline->endFrame(),
    });
    if (!result) return result;
    result = feederService_.start();
    if (!result) return result;

    seam::platform::AudioDeviceConfig config{
        .sampleRate = 48000U,
        .blockFrames = 256U,
        .outputChannels = 2U,
        .applicationName = "Project SEAM",
        .streamName = "Native editor preview",
    };

    std::string physicalError;
    if (!forceThreaded) {
      auto physical = seam::platform::createSystemAudioDevice();
      auto opened = physical->open(config, processor_);
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
      auto fallback = seam::platform::createThreadedAudioDevice();
      result = fallback->open(config, processor_);
      if (!result) return result;
      audioDevice_ = std::move(fallback);
    }

    if (!startPaused) {
      result = controller_.keyDown(seam::native_ui::KeyEvent{
          .key = seam::native_ui::NativeKey::Space,
          .modifiers = {},
          .repeat = false,
      });
      if (!result) return result;
      for (int attempt = 0; attempt < 100 &&
                            feederService_.stats().framesFed < 4096U;
           ++attempt) {
        std::this_thread::sleep_for(1ms);
      }
    }

    result = audioDevice_->start();
    if (!result && audioDevice_->info().physical) {
      physicalError = result.error().message;
      audioDevice_->stop();
      auto fallback = seam::platform::createThreadedAudioDevice();
      result = fallback->open(config, processor_);
      if (!result) return result;
      result = fallback->start();
      if (!result) return result;
      audioDevice_ = std::move(fallback);
    } else if (!result) {
      return result;
    }

    const auto info = audioDevice_->info();
    controller_.setAudioState(info.physical, info.backend);
    if (!physicalError.empty()) {
      std::cerr << "Physical audio unavailable, using callback-clock fallback: "
                << physicalError << '\n';
    }
    return seam::core::success();
  }

  void setWindow(seam::native_ui::INativeWindow& window) noexcept {
    window_ = &window;
  }

  void paint(seam::native_ui::RasterCanvas& canvas) noexcept override {
    const auto frame = feeder_.playhead();
    const auto tick = session_.project().tempoMap().tickAtSampleFrame(
        frame, session_.project().settings().sampleRate);
    auto state = controller_.sceneState();
    state.playheadPixel = controller_.pianoRoll().timeline().tickToPixel(tick);
    character_.setDisplayMode(state.characterMode);
    character_.setState(state.characterState);
    state.characterPortrait = character_.portrait();
    if (state.characterName.empty()) state.characterName = character_.displayName();
    if (state.characterStyle.empty()) state.characterStyle = character_.styleName();
    painter_.paint(canvas, controller_.pianoRoll(), state);
  }

  void resized(double logicalWidth, double logicalHeight,
               double) noexcept override {
    controller_.resize(logicalWidth, logicalHeight);
  }

  void pointerDown(const seam::native_ui::PointerEvent& event) noexcept override {
    record(controller_.pointerDown(event));
  }
  void pointerMove(const seam::native_ui::PointerEvent& event) noexcept override {
    record(controller_.pointerMove(event));
  }
  void pointerUp(const seam::native_ui::PointerEvent& event) noexcept override {
    record(controller_.pointerUp(event));
  }
  void scroll(double deltaX, double deltaY, seam::ui::Point anchor,
              seam::native_ui::InputModifiers modifiers) noexcept override {
    controller_.scroll(deltaX, deltaY, anchor, modifiers);
  }
  void keyDown(const seam::native_ui::KeyEvent& event) noexcept override {
    record(controller_.keyDown(event));
  }
  void textComposition(
      std::u32string text,
      seam::ui::CompositionSelection selection) noexcept override {
    record(controller_.updateTextComposition(std::move(text), selection));
  }
  void textCommit(std::u32string text) noexcept override {
    record(controller_.commitTextComposition(std::move(text)));
  }
  void textCancel() noexcept override { controller_.cancelTextComposition(); }
  bool wantsClose() const noexcept override {
    return closeRequested_.load(std::memory_order_acquire);
  }

  void shutdownAudio() noexcept {
    if (audioDevice_ != nullptr) audioDevice_->stop();
    feederService_.stop();
  }

  [[nodiscard]] seam::platform::AudioDeviceInfo audioInfo() const {
    return audioDevice_ == nullptr ? seam::platform::AudioDeviceInfo{}
                                   : audioDevice_->info();
  }
  [[nodiscard]] seam::platform::AudioDeviceStats audioStats() const noexcept {
    return audioDevice_ == nullptr ? seam::platform::AudioDeviceStats{}
                                   : audioDevice_->stats();
  }
  [[nodiscard]] seam::rendering::PlaybackFeederServiceStats feederStats() const noexcept {
    return feederService_.stats();
  }
  [[nodiscard]] seam::platform::RingBufferProcessorStats processorStats() const noexcept {
    return processor_.stats();
  }
  [[nodiscard]] const std::string& lastError() const noexcept { return lastError_; }

private:
  void record(const seam::core::Result<void>& result) noexcept {
    if (!result) lastError_ = result.error().message;
  }

  seam::application::ProjectFactory factory_;
  seam::domain::RegionId regionId_{};
  seam::application::EditorSession session_;
  seam::rendering::SpscAudioRingBuffer ring_;
  seam::rendering::PlaybackFeeder feeder_;
  seam::rendering::PlaybackFeederService feederService_;
  seam::platform::RingBufferAudioProcessor processor_;
  seam::native_ui::CharacterPresentation character_;
  seam::native_ui::EditorScenePainter painter_;
  seam::native_ui::NativeEditorController controller_;
  std::unique_ptr<seam::platform::IAudioDevice> audioDevice_;
  seam::native_ui::INativeWindow* window_{nullptr};
  std::atomic<bool> closeRequested_{false};
  std::string lastError_;
};

}  // namespace

int main(int argc, char** argv) {
  static_cast<void>(std::setlocale(LC_ALL, ""));

  const auto commandLine = parseArguments(argc, argv);
  if (!commandLine.has_value()) {
    if (argc > 1 && std::string_view{argv[1]} == "--help") return 0;
    printUsage();
    return 2;
  }

  NativeEditorApp app;
  const auto character = app.initializeCharacter(commandLine->characterPackage);
  if (!character) {
    std::cerr << "Character package unavailable: " << character.error().message;
    if (!character.error().context.empty()) {
      std::cerr << " (" << character.error().context << ')';
    }
    std::cerr << '\n';
  }
  const auto audio = app.initializeAudio(commandLine->forceThreadedAudio,
                                         commandLine->startPaused);
  if (!audio) {
    std::cerr << "Audio initialization failed: " << audio.error().message;
    if (!audio.error().context.empty()) {
      std::cerr << " (" << audio.error().context << ')';
    }
    std::cerr << '\n';
    return 3;
  }

  auto window = seam::native_ui::createNativeWindow();
  app.setWindow(*window);
  const auto opened = window->open(
      seam::native_ui::NativeWindowConfig{
          .title = "Project SEAM 0.5 / Native Editor",
          .width = 1440U,
          .height = 900U,
          .scale = commandLine->scale,
          .autoCloseAfter = commandLine->autoClose,
          .screenshotPath = commandLine->screenshot,
      },
      app);
  if (!opened) {
    std::cerr << "Native window failed: " << opened.error().message;
    if (!opened.error().context.empty()) {
      std::cerr << " (" << opened.error().context << ')';
    }
    std::cerr << '\n';
    return 4;
  }

  const auto result = window->run();
  app.shutdownAudio();
  const auto audioInfo = app.audioInfo();
  const auto audioStats = app.audioStats();
  const auto feederStats = app.feederStats();
  const auto processorStats = app.processorStats();
  std::cout << "window_backend=" << window->backendName() << '\n'
            << "audio_backend=" << audioInfo.backend << '\n'
            << "audio_physical=" << (audioInfo.physical ? "true" : "false") << '\n'
            << "audio_callbacks=" << audioStats.callbacks << '\n'
            << "feeder_frames=" << feederStats.framesFed << '\n'
            << "callback_delivered=" << processorStats.deliveredFrames << '\n'
            << "callback_underflow=" << processorStats.underflowFrames << '\n';
  if (!app.lastError().empty()) {
    std::cerr << "last_editor_error=" << app.lastError() << '\n';
  }
  return result;
}
