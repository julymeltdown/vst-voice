#include "test_framework.hpp"

#include "seam/time/meter_map.hpp"
#include "seam/time/quantizer.hpp"
#include "seam/time/tempo_map.hpp"

TEST_CASE("tempo map converts ticks, seconds, and sample frames") {
  seam::time::TempoMap tempo{960};
  CHECK_NEAR(tempo.secondsAt(seam::time::Tick{960}), 0.5, 1e-9);
  CHECK(tempo.addOrReplace(seam::time::Tick{1920}, 60.0));
  CHECK_NEAR(tempo.secondsAt(seam::time::Tick{2880}), 2.0, 1e-9);
  CHECK(tempo.tickAtSeconds(2.0) == seam::time::Tick{2880});
  CHECK(tempo.sampleFrameAt(seam::time::Tick{2880}, 48000.0) == 96000);
  CHECK(tempo.tickAtSampleFrame(96000, 48000.0) == seam::time::Tick{2880});
  CHECK_NEAR(tempo.bpmAt(seam::time::Tick{2500}), 60.0, 1e-9);
}

TEST_CASE("tempo map preserves an initial event") {
  seam::time::TempoMap tempo;
  CHECK(!tempo.remove(seam::time::Tick{0}));
  CHECK(!tempo.addOrReplace(seam::time::Tick{-1}, 120.0));
  CHECK(!tempo.addOrReplace(seam::time::Tick{0}, 0.0));
}

TEST_CASE("meter map reports one-based bar and beat") {
  seam::time::MeterMap meter{960};
  CHECK(meter.barBeatAt(seam::time::Tick{0}) ==
        (seam::time::BarBeat{1, 1, seam::time::Tick{0}}));
  CHECK(meter.barBeatAt(seam::time::Tick{960}) ==
        (seam::time::BarBeat{1, 2, seam::time::Tick{0}}));
  CHECK(meter.addOrReplace(seam::time::Tick{3840}, 3, 4));
  CHECK(meter.barBeatAt(seam::time::Tick{3840}) ==
        (seam::time::BarBeat{2, 1, seam::time::Tick{0}}));
  CHECK(meter.barBeatAt(seam::time::Tick{5760}) ==
        (seam::time::BarBeat{2, 3, seam::time::Tick{0}}));
  CHECK(meter.barBeatAt(seam::time::Tick{6720}) ==
        (seam::time::BarBeat{3, 1, seam::time::Tick{0}}));
}

TEST_CASE("quantizer handles positive and negative ticks") {
  seam::time::Quantizer quantizer{seam::time::Tick{240}};
  CHECK(quantizer.snap(seam::time::Tick{130}) == seam::time::Tick{240});
  CHECK(quantizer.snap(seam::time::Tick{110}) == seam::time::Tick{0});
  CHECK(quantizer.snap(seam::time::Tick{-130}) == seam::time::Tick{-240});
  CHECK(quantizer.snap(seam::time::Tick{-110}) == seam::time::Tick{0});
  CHECK(quantizer.snap(seam::time::Tick{241}, seam::time::SnapDirection::Floor) ==
        seam::time::Tick{240});
  CHECK(quantizer.snap(seam::time::Tick{241}, seam::time::SnapDirection::Ceil) ==
        seam::time::Tick{480});
}
