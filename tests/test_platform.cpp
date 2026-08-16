#include "test_framework.hpp"

#include "seam/platform/audio_callback.hpp"

#include <algorithm>

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
