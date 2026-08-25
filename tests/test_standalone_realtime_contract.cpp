#include "test_framework.hpp"

#include "seam/platform/audio_callback.hpp"
#include "seam/platform/multichannel_ring_buffer_processor.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/logger.hpp"
#include "seam/core/realtime_audit.hpp"
#include "seam/rendering/interleaved_audio_ring_buffer.hpp"

#include <array>
#include <cmath>
#include <span>
#include <sstream>
#include <vector>

TEST_CASE("realtime audit counts core file and logger operations") {
  seam::core::RealtimeAuditCounters counters;
  {
    seam::core::RealtimeAuditScope scope{counters};
    std::ostringstream stream;
    seam::core::StreamLogger logger{stream};
    logger.write(seam::core::LogLevel::Info, "test", "message");
    static_cast<void>(seam::core::readTextFileLimited(
        std::filesystem::path{"/path/that/does/not/exist"}, 1024U));
  }
  CHECK(counters.fileIoCalls.load(std::memory_order_relaxed) >= 1U);
  CHECK(counters.loggerCalls.load(std::memory_order_relaxed) == 1U);
  CHECK(counters.lockAttempts.load(std::memory_order_relaxed) == 1U);
}

TEST_CASE("multichannel callback processor zero fills underflow without nonfinite output") {
  seam::rendering::SpscInterleavedAudioRingBuffer ring{128U, 2U};
  seam::platform::MultichannelRingBufferAudioProcessor processor{ring, 64U};
  std::array<std::vector<float>, 2U> buffers{
      std::vector<float>(64U, 1.0F), std::vector<float>(64U, 1.0F)};
  std::array<std::span<float>, 2U> outputs{buffers[0], buffers[1]};
  processor.process(seam::platform::AudioProcessContext{
      .sampleRate = 48000.0,
      .frameCount = 64U,
      .left = outputs[0],
      .right = outputs[1],
      .outputs = outputs,
  });
  for (const auto& channel : buffers) {
    for (const auto sample : channel) {
      CHECK(std::isfinite(sample));
      CHECK(sample == 0.0F);
    }
  }
  const auto stats = processor.stats();
  CHECK(stats.callbacks == 1U);
  CHECK(stats.underflowFrames == 64U);
}

TEST_CASE("multichannel callback does not count reset zero-fill as an underflow") {
  seam::rendering::SpscInterleavedAudioRingBuffer ring{128U, 2U};
  seam::platform::MultichannelRingBufferAudioProcessor processor{ring, 64U};
  std::vector<float> input(64U * 2U, 0.25F);
  CHECK(ring.writeFrames(input) == 64U);
  const auto resetEpoch = ring.requestConsumerReset();
  CHECK(!ring.resetAcknowledged(resetEpoch));

  std::array<std::vector<float>, 2U> buffers{
      std::vector<float>(64U, 1.0F), std::vector<float>(64U, 1.0F)};
  std::array<std::span<float>, 2U> outputs{buffers[0], buffers[1]};
  processor.process(seam::platform::AudioProcessContext{
      .sampleRate = 48000.0,
      .frameCount = 64U,
      .left = outputs[0],
      .right = outputs[1],
      .outputs = outputs,
  });

  CHECK(ring.resetAcknowledged(resetEpoch));
  const auto stats = processor.stats();
  CHECK(stats.deliveredFrames == 0U);
  CHECK(stats.underflowFrames == 0U);
  CHECK(stats.intentionalResetFrames == 64U);

  processor.process(seam::platform::AudioProcessContext{
      .sampleRate = 48000.0,
      .frameCount = 64U,
      .left = outputs[0],
      .right = outputs[1],
      .outputs = outputs,
  });
  const auto afterReset = processor.stats();
  CHECK(afterReset.underflowFrames == 64U);
  CHECK(afterReset.intentionalResetFrames == 64U);
}
