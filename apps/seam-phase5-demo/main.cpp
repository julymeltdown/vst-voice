#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/platform/audio_device.hpp"
#include "seam/platform/ring_buffer_processor.hpp"
#include "seam/rendering/audio_ring_buffer.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/playback_engine.hpp"
#include "seam/rendering/playback_feeder_service.hpp"
#include "seam/voicebank/wav.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

std::filesystem::path outputDirectory(int argc, char** argv) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (std::string_view{argv[index]} == "--output") {
      return std::filesystem::path{argv[index + 1]};
    }
  }
  return "out/phase5";
}

seam::domain::Project makeProject(seam::application::ProjectFactory& factory,
                                  seam::domain::RegionId& regionId) {
  auto project = factory.createProject("SEAM PHASE 5 / NATIVE RUNTIME");
  static_cast<void>(project.tempoMap().addOrReplace(seam::time::Tick{0}, 154.0));
  const auto trackId = factory.addVocalTrack(project, "VOICE 01");
  regionId = factory.addRegion(project, trackId, "NATIVE SLICE",
                               seam::time::Tick{0}, seam::time::Tick{15360});
  auto* region = project.findRegion(regionId);
  const std::vector<std::tuple<std::int64_t, std::int64_t, std::uint8_t,
                               std::u32string>> source{
      {0, 720, 64U, U"cut"},       {720, 480, 67U, U"the"},
      {1200, 960, 69U, U"voice"},  {2400, 480, 67U, U"at"},
      {2880, 720, 64U, U"the"},    {3600, 960, 62U, U"edge"},
      {4800, 480, 64U, U"keep"},   {5280, 480, 67U, U"the"},
      {5760, 1440, 71U, U"seam"},  {7440, 480, 69U, U"in"},
      {7920, 960, 67U, U"side"},   {9360, 1920, 64U, U"..."},
  };
  for (const auto& [start, duration, key, text] : source) {
    auto [lyric, note] = factory.makeNote(
        seam::time::Tick{start}, seam::time::Tick{duration}, key, text,
        seam::domain::Language::English);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  }
  region->sortNotes();
  return project;
}

std::shared_ptr<const seam::rendering::PlaybackTimeline> makeTimeline() {
  constexpr std::uint32_t sampleRate = 48000U;
  std::vector<float> samples(sampleRate * 6U, 0.0F);
  for (std::size_t frame = 0U; frame < samples.size(); ++frame) {
    const auto seconds = static_cast<double>(frame) /
                         static_cast<double>(sampleRate);
    const auto gate = std::fmod(seconds, 1.5) < 1.12 ? 1.0 : 0.0;
    const auto slow = 0.72 + 0.28 * std::sin(2.0 * std::numbers::pi * 0.7 * seconds);
    const auto primary = std::sin(2.0 * std::numbers::pi * 196.0 * seconds);
    const auto shifted = std::sin(2.0 * std::numbers::pi * 393.2 * seconds + 0.35);
    samples[frame] = static_cast<float>((primary * 0.13 + shifted * 0.027) *
                                        gate * slow);
  }
  auto pcm = std::make_shared<const seam::rendering::CachedPcm>(
      seam::rendering::CachedPcm{
          .sampleRate = sampleRate,
          .startFrame = 0,
          .samples = std::move(samples),
      });
  auto timeline = std::make_shared<seam::rendering::PlaybackTimeline>(sampleRate);
  const auto result = timeline->addClip(seam::rendering::PlaybackClip{
      .id = "phase5-preview",
      .pcm = std::move(pcm),
      .gain = 1.0F,
      .fadeInFrames = 128,
      .fadeOutFrames = 256,
      .enabled = true,
  });
  return result ? timeline : nullptr;
}

bool writeSummary(const std::filesystem::path& path,
                  std::uint64_t oneXChecksum,
                  std::uint64_t twoXChecksum,
                  std::uint64_t revision,
                  std::size_t visibleNotes,
                  const seam::platform::AudioDeviceInfo& audioInfo,
                  const seam::platform::AudioDeviceStats& audioStats,
                  const seam::rendering::PlaybackFeederServiceStats& feederStats,
                  const seam::platform::RingBufferProcessorStats& processorStats,
                  std::size_t repaintRequests,
                  std::size_t textRequests) {
  std::ofstream stream(path, std::ios::trunc);
  if (!stream) return false;
  stream << "{\n"
         << "  \"phase\": \"5.0\",\n"
         << "  \"nativeUi\": {\n"
         << "    \"renderer\": \"first-party software raster\",\n"
         << "    \"oneXChecksum\": " << oneXChecksum << ",\n"
         << "    \"twoXChecksum\": " << twoXChecksum << ",\n"
         << "    \"visibleNotes\": " << visibleNotes << ",\n"
         << "    \"editorRevision\": " << revision << ",\n"
         << "    \"repaintRequests\": " << repaintRequests << ",\n"
         << "    \"textInputRequests\": " << textRequests << "\n"
         << "  },\n"
         << "  \"audio\": {\n"
         << "    \"backend\": \"" << audioInfo.backend << "\",\n"
         << "    \"physical\": " << (audioInfo.physical ? "true" : "false") << ",\n"
         << "    \"callbacks\": " << audioStats.callbacks << ",\n"
         << "    \"deviceFrames\": " << audioStats.frames << ",\n"
         << "    \"feederFrames\": " << feederStats.framesFed << ",\n"
         << "    \"deliveredFrames\": " << processorStats.deliveredFrames << ",\n"
         << "    \"underflowFrames\": " << processorStats.underflowFrames << "\n"
         << "  }\n"
         << "}\n";
  stream.flush();
  return static_cast<bool>(stream);
}

}  // namespace

int main(int argc, char** argv) {
  const auto output = outputDirectory(argc, argv);
  std::error_code error;
  std::filesystem::create_directories(output, error);
  if (error) {
    std::cerr << "Unable to create output directory: " << error.message() << '\n';
    return 2;
  }

  seam::application::ProjectFactory factory{5000U};
  seam::domain::RegionId regionId;
  seam::application::EditorSession session{makeProject(factory, regionId)};
  std::size_t repaintRequests = 0U;
  std::size_t textRequests = 0U;
  seam::native_ui::NativeEditorController controller{
      session, factory, regionId,
      seam::native_ui::EditorHostCallbacks{
          .requestRepaint = [&repaintRequests] { ++repaintRequests; },
          .beginTextInput = [&textRequests](const auto&) { ++textRequests; },
          .endTextInput = [] {},
          .setPlaying = [](bool) {},
          .documentChanged = {},
      }};
  controller.resize(1440.0, 900.0);

  const auto visuals = controller.pianoRoll().visibleNotes();
  if (visuals.empty()) {
    std::cerr << "Native editor model did not produce visible notes\n";
    return 3;
  }
  const auto first = visuals.front();
  const seam::ui::Point noteCenter{
      first.bounds.x + first.bounds.width * 0.5,
      first.bounds.y + first.bounds.height * 0.5 + 98.0,
  };
  auto result = controller.pointerDown(seam::native_ui::PointerEvent{
      .position = noteCenter,
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  });
  if (!result) return 4;
  result = controller.pointerMove(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{noteCenter.x + 112.0, noteCenter.y - 18.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  });
  if (!result) return 4;
  result = controller.pointerUp(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{noteCenter.x + 112.0, noteCenter.y - 18.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  });
  if (!result) return 4;

  result = controller.beginLyricEdit(first.noteId);
  if (!result) return 5;
  result = controller.updateTextComposition(U"継ぎ目",
      seam::ui::CompositionSelection{3U, 0U});
  if (!result) return 5;
  result = controller.commitTextComposition(U"継ぎ目");
  if (!result) return 5;

  seam::native_ui::EditorScenePainter painter;
  seam::native_ui::PixelSurface oneX{1440U, 900U};
  seam::native_ui::RasterCanvas oneXCanvas{oneX, 1.0};
  painter.paint(oneXCanvas, controller.pianoRoll(), controller.sceneState());
  if (!oneX.writePpm(output / "phase5-editor-1x.ppm")) return 6;

  seam::native_ui::PixelSurface twoX{2880U, 1800U};
  seam::native_ui::RasterCanvas twoXCanvas{twoX, 2.0};
  painter.paint(twoXCanvas, controller.pianoRoll(), controller.sceneState());
  if (!twoX.writePpm(output / "phase5-editor-2x.ppm")) return 6;

  auto timeline = makeTimeline();
  if (timeline == nullptr) return 7;
  seam::rendering::SpscAudioRingBuffer ring{32768U};
  seam::rendering::PlaybackFeeder feeder{ring, 48000U, 1024U, 64U};
  seam::rendering::PlaybackFeederService service{
      feeder, seam::rendering::PlaybackFeederServiceConfig{
                  .targetBufferedFrames = 16384U,
                  .activePollInterval = 250us,
                  .idlePollInterval = 2ms,
              }};
  seam::platform::RingBufferAudioProcessor processor{ring};
  auto device = seam::platform::createThreadedAudioDevice();
  result = service.setTimeline(timeline);
  if (!result) return 8;
  result = service.setLoop(seam::rendering::PlaybackLoop{
      .enabled = true,
      .startFrame = 0,
      .endFrame = timeline->endFrame(),
  });
  if (!result) return 8;
  result = service.start();
  if (!result) return 8;
  result = service.setPlaying(true);
  if (!result) return 8;
  std::this_thread::sleep_for(30ms);

  result = device->open(seam::platform::AudioDeviceConfig{
                            .sampleRate = 48000U,
                            .blockFrames = 256U,
                            .outputChannels = 2U,
                            .applicationName = "Project SEAM Tests",
                            .streamName = "Phase 5 callback",
                        },
                        processor);
  if (!result) return 9;
  result = device->start();
  if (!result) return 9;
  std::this_thread::sleep_for(350ms);
  device->stop();
  service.stop();

  std::vector<float> preview(48000U * 2U, 0.0F);
  timeline->mix(0, preview);
  if (!seam::voicebank::writeMonoPcm16Wav(
          output / "phase5-playback-reference.wav", 48000U, preview)) {
    return 10;
  }

  const auto audioInfo = device->info();
  const auto audioStats = device->stats();
  const auto feederStats = service.stats();
  const auto processorStats = processor.stats();
  if (!writeSummary(output / "phase5-summary.json", oneX.checksum(),
                    twoX.checksum(), session.revision(),
                    controller.pianoRoll().visibleNotes().size(), audioInfo,
                    audioStats, feederStats, processorStats, repaintRequests,
                    textRequests)) {
    return 11;
  }

  std::cout << "phase=5.0\n"
            << "editor_revision=" << session.revision() << '\n'
            << "visible_notes=" << controller.pianoRoll().visibleNotes().size() << '\n'
            << "surface_1x_checksum=" << oneX.checksum() << '\n'
            << "surface_2x_checksum=" << twoX.checksum() << '\n'
            << "audio_backend=" << audioInfo.backend << '\n'
            << "audio_callbacks=" << audioStats.callbacks << '\n'
            << "feeder_frames=" << feederStats.framesFed << '\n'
            << "callback_delivered=" << processorStats.deliveredFrames << '\n'
            << "callback_underflow=" << processorStats.underflowFrames << '\n';
  return processorStats.deliveredFrames > 0U ? 0 : 12;
}
