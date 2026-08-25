#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/platform/audio_callback.hpp"
#include "seam/platform/audio_input_device.hpp"
#include "seam/platform/recording_session.hpp"
#include "seam/platform/ring_buffer_processor.hpp"
#include "seam/rendering/audio_ring_buffer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <thread>

#include "seam/voicebank/wav.hpp"

TEST_CASE("phase one audio callback produces deterministic silence") {
  seam::platform::SilenceProcessor processor;
  seam::platform::AudioCallbackSimulator simulator{48000.0, 64};
  simulator.run(processor, 100);
  CHECK(processor.callbackCount() == 100);
  CHECK(processor.processedFrames() == 6400);
  CHECK(std::all_of(simulator.left().begin(), simulator.left().end(),
                    [](float value) { return value == 0.0F; }));
  CHECK(std::all_of(simulator.right().begin(), simulator.right().end(),
                    [](float value) { return value == 0.0F; }));
}


TEST_CASE("ring buffer audio processor delivers callback audio and accounts underflow") {
  seam::rendering::SpscAudioRingBuffer ring{16};
  const std::array<float, 4> source{0.2F, -0.4F, 0.6F, -0.8F};
  CHECK(ring.write(source) == source.size());
  seam::platform::RingBufferAudioProcessor processor{ring};
  processor.setGain(0.5F);
  seam::platform::AudioCallbackSimulator simulator{48000.0, 4};
  simulator.run(processor, 1);
  CHECK_NEAR(simulator.left()[0], 0.1F, 1.0e-6);
  CHECK_NEAR(simulator.left()[3], -0.4F, 1.0e-6);
  CHECK(simulator.left()[2] == simulator.right()[2]);

  simulator.run(processor, 1);
  CHECK(std::all_of(simulator.left().begin(), simulator.left().end(),
                    [](float value) { return value == 0.0F; }));
  const auto stats = processor.stats();
  CHECK(stats.callbacks == 2);
  CHECK(stats.requestedFrames == 8);
  CHECK(stats.deliveredFrames == 4);
  CHECK(stats.underflowFrames == 4);
}

TEST_CASE("ring buffer audio processor does not count reset zero-fill as underflow") {
  seam::rendering::SpscAudioRingBuffer ring{16};
  const std::array<float, 4> source{0.2F, -0.4F, 0.6F, -0.8F};
  CHECK(ring.write(source) == source.size());
  const auto resetEpoch = ring.requestConsumerReset();
  seam::platform::RingBufferAudioProcessor processor{ring};
  seam::platform::AudioCallbackSimulator simulator{48000.0, 4};

  simulator.run(processor, 1);
  CHECK(ring.resetAcknowledged(resetEpoch));
  auto stats = processor.stats();
  CHECK(stats.deliveredFrames == 0U);
  CHECK(stats.underflowFrames == 0U);
  CHECK(stats.intentionalResetFrames == 4U);

  simulator.run(processor, 1);
  stats = processor.stats();
  CHECK(stats.underflowFrames == 4U);
  CHECK(stats.intentionalResetFrames == 4U);
}

TEST_CASE("threaded audio input feeds recording session without allocation-sensitive overflow") {
  seam::platform::RecordingSession recording{48000U, 1U};
  auto input = seam::platform::createThreadedSilenceInputDevice();
  CHECK(input != nullptr);
  CHECK(input->open(seam::platform::AudioInputDeviceConfig{
      .sampleRate = 48000U,
      .blockFrames = 128U,
      .applicationName = "Project SEAM Test",
      .streamName = "Synthetic Recording",
  }, recording));
  CHECK(recording.arm());
  CHECK(input->start());
  std::this_thread::sleep_for(std::chrono::milliseconds{40});
  input->stop();
  recording.stop();
  CHECK(!input->running());
  CHECK(recording.recordedFrames() > 0U);
  CHECK(!recording.overflowed());
  CHECK(input->stats().callbacks > 0U);
  CHECK(input->stats().frames == recording.recordedFrames());
  CHECK(std::all_of(recording.samples().begin(), recording.samples().end(),
                    [](float value) { return value == 0.0F; }));

  const auto directory = seam::test::support::temporaryDirectory("recording-session");
  const auto path = directory / "take.wav";
  CHECK(recording.exportWav(path));
  CHECK(std::filesystem::exists(path));
  const auto loaded = seam::voicebank::readWav(path);
  CHECK(loaded);
  CHECK(loaded.value().sampleRate == 48000U);
  CHECK(loaded.value().channels == 1U);
  CHECK(loaded.value().bitsPerSample == 16U);
  CHECK(loaded.value().frameCount() == recording.recordedFrames());

  const auto productionPath = directory / "take-24bit.wav";
  CHECK(recording.exportWav(
      productionPath, seam::voicebank::WavSampleFormat::Pcm24));
  const auto production = seam::voicebank::readWav(productionPath);
  CHECK(production);
  CHECK(production.value().bitsPerSample == 24U);
  CHECK(!recording.exportWav(
      productionPath, seam::voicebank::WavSampleFormat::Pcm24, false));
  const auto preserved = seam::voicebank::readWav(productionPath);
  CHECK(preserved);
  CHECK(preserved.value().bitsPerSample == 24U);
}
