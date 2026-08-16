#include "test_framework.hpp"

#include "seam/platform/audio_callback.hpp"
#include "seam/platform/ring_buffer_processor.hpp"
#include "seam/rendering/audio_ring_buffer.hpp"

#include <algorithm>
#include <array>

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
