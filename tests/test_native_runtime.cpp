#include "test_framework.hpp"

#include "seam/platform/audio_device.hpp"
#include "seam/platform/ring_buffer_processor.hpp"
#include "seam/rendering/audio_ring_buffer.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/playback_engine.hpp"
#include "seam/rendering/playback_feeder_service.hpp"

#include <chrono>
#include <cstdlib>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

std::shared_ptr<const seam::rendering::PlaybackTimeline> testTimeline(
    std::size_t frames = 48000U) {
  std::vector<float> samples(frames, 0.15F);
  auto pcm = std::make_shared<const seam::rendering::CachedPcm>(
      seam::rendering::CachedPcm{
          .sampleRate = 48000U,
          .startFrame = 0,
          .samples = std::move(samples),
      });
  auto timeline = std::make_shared<seam::rendering::PlaybackTimeline>(48000U);
  const auto added = timeline->addClip(seam::rendering::PlaybackClip{
      .id = "native-runtime",
      .pcm = std::move(pcm),
      .gain = 1.0F,
      .fadeInFrames = 0,
      .fadeOutFrames = 0,
      .enabled = true,
  });
  if (!added) throw std::runtime_error(added.error().message);
  return timeline;
}

}  // namespace

TEST_CASE("dedicated playback feeder service owns production and stops cleanly") {
  seam::rendering::SpscAudioRingBuffer ring{8192U};
  seam::rendering::PlaybackFeeder feeder{ring, 48000U, 512U, 64U};
  seam::rendering::PlaybackFeederService service{
      feeder, seam::rendering::PlaybackFeederServiceConfig{
                  .targetBufferedFrames = 4096U,
                  .activePollInterval = 100us,
                  .idlePollInterval = 1ms,
              }};
  CHECK(service.setTimeline(testTimeline()));
  CHECK(service.start());
  CHECK(!service.start());
  CHECK(service.setPlaying(true));
  for (int attempt = 0; attempt < 100 && ring.availableRead() == 0U; ++attempt) {
    std::this_thread::sleep_for(1ms);
  }
  CHECK(ring.availableRead() > 0U);
  CHECK(service.running());
  service.stop();
  CHECK(!service.running());
  CHECK(service.stats().framesFed > 0U);
}

TEST_CASE("threaded audio device drives the real callback contract") {
  seam::rendering::SpscAudioRingBuffer ring{16384U};
  seam::rendering::PlaybackFeeder feeder{ring, 48000U, 512U, 64U};
  seam::rendering::PlaybackFeederService service{
      feeder, seam::rendering::PlaybackFeederServiceConfig{
                  .targetBufferedFrames = 8192U,
                  .activePollInterval = 100us,
                  .idlePollInterval = 1ms,
              }};
  seam::platform::RingBufferAudioProcessor processor{ring};
  auto device = seam::platform::createThreadedAudioDevice();
  CHECK(service.setTimeline(testTimeline(96000U)));
  CHECK(service.setLoop(seam::rendering::PlaybackLoop{
      .enabled = true,
      .startFrame = 0,
      .endFrame = 96000,
  }));
  CHECK(service.start());
  CHECK(service.setPlaying(true));
  for (int attempt = 0; attempt < 100 && ring.availableRead() < 2048U; ++attempt) {
    std::this_thread::sleep_for(1ms);
  }
  CHECK(device->open(seam::platform::AudioDeviceConfig{
                         .sampleRate = 48000U,
                         .blockFrames = 128U,
                         .outputChannels = 2U,
                         .applicationName = "SEAM test",
                         .streamName = "callback test",
                     },
                     processor));
  CHECK(device->start());
  CHECK(!device->start());
  for (int attempt = 0; attempt < 100 && device->stats().callbacks < 8U; ++attempt) {
    std::this_thread::sleep_for(2ms);
  }
  device->stop();
  service.stop();
  CHECK(device->stats().callbacks >= 8U);
  CHECK(device->stats().frames >= 1024U);
  CHECK(processor.stats().deliveredFrames > 0U);
  CHECK(device->info().physical == false);
}

TEST_CASE("playback controls remain race-free while feeder and callback threads run") {
  seam::rendering::SpscAudioRingBuffer ring{32768U};
  seam::rendering::PlaybackFeeder feeder{ring, 48000U, 256U, 512U};
  seam::rendering::PlaybackFeederService service{
      feeder, seam::rendering::PlaybackFeederServiceConfig{
                  .targetBufferedFrames = 16384U,
                  .activePollInterval = 100us,
                  .idlePollInterval = 1ms,
              }};
  seam::platform::RingBufferAudioProcessor processor{ring};
  auto device = seam::platform::createThreadedAudioDevice();
  CHECK(service.setTimeline(testTimeline(192000U)));
  CHECK(service.setLoop(seam::rendering::PlaybackLoop{
      .enabled = true,
      .startFrame = 0,
      .endFrame = 192000,
  }));
  CHECK(service.start());
  CHECK(service.setPlaying(true));
  CHECK(device->open(seam::platform::AudioDeviceConfig{
                         .sampleRate = 48000U,
                         .blockFrames = 64U,
                         .outputChannels = 2U,
                         .applicationName = "SEAM race test",
                         .streamName = "race test",
                     },
                     processor));
  CHECK(device->start());
  for (int index = 0; index < 80; ++index) {
    CHECK(service.seek(static_cast<seam::time::SampleFrame>(index * 97)));
    CHECK(service.setPlaying(index % 7 != 0));
    if (index % 7 == 0) CHECK(service.setPlaying(true));
    std::this_thread::sleep_for(200us);
  }
  std::this_thread::sleep_for(30ms);
  device->stop();
  service.stop();
  CHECK(feeder.stats().controlCommands > 100U);
  CHECK(device->stats().callbacks > 0U);
}

TEST_CASE("system audio adapter reports an explicit bounded open result") {
#if defined(__linux__)
  const auto* previous = std::getenv("PULSE_SERVER");
  const std::string previousValue = previous == nullptr ? std::string{} : previous;
  const bool hadPrevious = previous != nullptr;
  CHECK(::setenv("PULSE_SERVER",
                 "unix:/__project_seam_nonexistent__/pulse.sock", 1) == 0);
#endif

  seam::platform::SilenceProcessor processor;
  auto device = seam::platform::createSystemAudioDevice();
  CHECK(device != nullptr);
  const auto opened = device->open(seam::platform::AudioDeviceConfig{
                                       .sampleRate = 48000U,
                                       .blockFrames = 256U,
                                       .outputChannels = 2U,
                                       .applicationName = "SEAM probe",
                                       .streamName = "probe",
                                   },
                                   processor);

#if defined(__linux__)
  if (hadPrevious) {
    CHECK(::setenv("PULSE_SERVER", previousValue.c_str(), 1) == 0);
  } else {
    CHECK(::unsetenv("PULSE_SERVER") == 0);
  }
#endif

  if (opened) {
    CHECK(device->info().physical);
    device->stop();
  } else {
    CHECK(!opened.error().message.empty());
  }
}
