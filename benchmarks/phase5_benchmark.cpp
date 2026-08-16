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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace std::chrono_literals;

seam::domain::Project makeProject(seam::application::ProjectFactory& factory,
                                  seam::domain::RegionId& regionId) {
  auto project = factory.createProject("Phase 5 benchmark");
  const auto track = factory.addVocalTrack(project, "Voice");
  regionId = factory.addRegion(project, track, "Dense",
                               seam::time::Tick{0}, seam::time::Tick{1200000});
  auto* region = project.findRegion(regionId);
  region->lyrics.reserve(5000U);
  region->notes.reserve(5000U);
  for (std::size_t index = 0U; index < 5000U; ++index) {
    auto [lyric, note] = factory.makeNote(
        seam::time::Tick{static_cast<std::int64_t>(index) * 120},
        seam::time::Tick{96},
        static_cast<std::uint8_t>(48U + index % 24U), U"a",
        seam::domain::Language::English);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  }
  region->sortNotes();
  return project;
}

std::shared_ptr<const seam::rendering::PlaybackTimeline> makeTimeline() {
  auto pcm = std::make_shared<const seam::rendering::CachedPcm>(
      seam::rendering::CachedPcm{
          .sampleRate = 48000U,
          .startFrame = 0,
          .samples = std::vector<float>(48000U * 4U, 0.1F),
      });
  auto timeline = std::make_shared<seam::rendering::PlaybackTimeline>(48000U);
  const auto added = timeline->addClip(seam::rendering::PlaybackClip{
      .id = "benchmark",
      .pcm = std::move(pcm),
  });
  return added ? timeline : nullptr;
}
}  // namespace

int main() {
  seam::application::ProjectFactory factory{9000U};
  seam::domain::RegionId regionId;
  seam::application::EditorSession session{makeProject(factory, regionId)};
  seam::native_ui::NativeEditorController controller{session, factory, regionId};
  controller.resize(1280.0, 720.0);
  seam::native_ui::EditorScenePainter painter;
  seam::native_ui::PixelSurface surface{1280U, 720U};
  seam::native_ui::RasterCanvas canvas{surface, 1.0};

  constexpr std::size_t paintIterations = 60U;
  const auto paintStart = std::chrono::steady_clock::now();
  for (std::size_t iteration = 0U; iteration < paintIterations; ++iteration) {
    painter.paint(canvas, controller.pianoRoll(), controller.sceneState());
  }
  const auto paintElapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - paintStart);

  seam::rendering::SpscAudioRingBuffer ring{32768U};
  seam::rendering::PlaybackFeeder feeder{ring, 48000U, 512U, 64U};
  seam::rendering::PlaybackFeederService service{
      feeder, seam::rendering::PlaybackFeederServiceConfig{
                  .targetBufferedFrames = 16384U,
                  .activePollInterval = 100us,
                  .idlePollInterval = 1ms,
              }};
  seam::platform::RingBufferAudioProcessor processor{ring};
  auto device = seam::platform::createThreadedAudioDevice();
  const auto timeline = makeTimeline();
  if (timeline == nullptr || !service.setTimeline(timeline) ||
      !service.setLoop(seam::rendering::PlaybackLoop{
          .enabled = true, .startFrame = 0, .endFrame = timeline->endFrame()}) ||
      !service.start() || !service.setPlaying(true)) {
    return 1;
  }
  std::this_thread::sleep_for(20ms);
  if (!device->open(seam::platform::AudioDeviceConfig{
                        .sampleRate = 48000U,
                        .blockFrames = 128U,
                        .outputChannels = 2U,
                        .applicationName = "SEAM benchmark",
                        .streamName = "benchmark",
                    },
                    processor) ||
      !device->start()) {
    service.stop();
    return 1;
  }
  std::this_thread::sleep_for(250ms);
  device->stop();
  service.stop();

  const auto averagePaintMs = paintElapsed.count() /
                              static_cast<double>(paintIterations);
  const auto audio = device->stats();
  const auto feederStats = service.stats();
  const auto callback = processor.stats();
  std::cout << "{\n"
            << "  \"phase\": \"5.0\",\n"
            << "  \"visibleNotes\": "
            << controller.pianoRoll().visibleNotes().size() << ",\n"
            << "  \"paintIterations\": " << paintIterations << ",\n"
            << "  \"averagePaintMs\": " << averagePaintMs << ",\n"
            << "  \"surfaceChecksum\": " << surface.checksum() << ",\n"
            << "  \"audioCallbacks\": " << audio.callbacks << ",\n"
            << "  \"feederFrames\": " << feederStats.framesFed << ",\n"
            << "  \"deliveredFrames\": " << callback.deliveredFrames << ",\n"
            << "  \"underflowFrames\": " << callback.underflowFrames << "\n"
            << "}\n";
  return callback.deliveredFrames > 0U ? 0 : 1;
}
