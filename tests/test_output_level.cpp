// The output level publisher: what the audio thread measures, what the UI reads, and when a
// meter must show nothing at all.
#include "test_framework.hpp"

#include "seam/platform/multichannel_ring_buffer_processor.hpp"
#include "seam/platform/output_level_meter.hpp"
#include "seam/rendering/interleaved_audio_ring_buffer.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <span>
#include <thread>
#include <vector>

namespace {

using seam::platform::AudioProcessContext;
using seam::platform::OutputLevelMeter;
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

bool near(float a, float b, float tolerance = 1.0e-4F) { return std::fabs(a - b) <= tolerance; }

// One stereo block whose left channel peaks at left and right channel at right.
struct StereoBlock final {
  std::array<std::vector<float>, 2U> buffers;
  std::array<std::span<float>, 2U> views;
  StereoBlock(float left, float right, std::size_t frames = 64U)
      : buffers{std::vector<float>(frames, 0.0F), std::vector<float>(frames, 0.0F)} {
    buffers[0][frames / 2U] = -left;  // negative: peaks are magnitudes
    buffers[1][frames / 3U] = right;
    views = {buffers[0], buffers[1]};
  }
  [[nodiscard]] AudioProcessContext context() {
    return AudioProcessContext{.sampleRate = 48000.0,
                               .frameCount = buffers[0].size(),
                               .left = views[0],
                               .right = views[1],
                               .outputs = views};
  }
};

}  // namespace

TEST_CASE("output level reports the per-channel block peak and nothing before the first block") {
  OutputLevelMeter meter;
  const auto t0 = Clock::now();
  // Running but no callback has arrived: nothing has been measured.
  CHECK(!meter.read(true, t0).has_value());
  StereoBlock block{0.5F, 0.25F};
  meter.measure(block.context());
  const auto reading = meter.read(true, t0 + milliseconds{16});
  CHECK(reading.has_value());
  if (!reading) return;
  CHECK(reading->peak.size() == 2U && reading->hold.size() == 2U);
  CHECK(near(reading->peak[0], 0.5F) && near(reading->peak[1], 0.25F));
  CHECK(near(reading->hold[0], 0.5F) && near(reading->hold[1], 0.25F));
  CHECK(!reading->clipped);
}

TEST_CASE("output level keeps the loudest block between two reads") {
  OutputLevelMeter meter;
  const auto t0 = Clock::now();
  StereoBlock loud{0.9F, 0.1F};
  StereoBlock quiet{0.2F, 0.1F};
  meter.measure(loud.context());
  meter.measure(quiet.context());
  meter.measure(quiet.context());
  const auto reading = meter.read(true, t0);
  CHECK(reading.has_value() && near(reading->peak[0], 0.9F));
}

TEST_CASE("output level holds the peak for about 1.5 s and decays the level meanwhile") {
  OutputLevelMeter meter;  // defaults: 1500 ms hold, 24 dB/s decay
  const auto t0 = Clock::now();
  StereoBlock loud{0.5F, 0.5F};
  StereoBlock silent{0.0F, 0.0F};
  meter.measure(loud.context());
  CHECK(meter.read(true, t0).has_value());
  // One second of silence: the level falls 24 dB (0.5 -> ~0.0315), the hold stays at 0.5.
  auto at = t0;
  std::optional<seam::platform::OutputLevelReading> reading;
  for (int step = 0; step < 50; ++step) {
    meter.measure(silent.context());
    at += milliseconds{20};
    reading = meter.read(true, at);
  }
  CHECK(reading.has_value());
  if (!reading) return;
  const auto expected = 0.5F * static_cast<float>(std::pow(10.0, -24.0 / 20.0));
  CHECK(near(reading->peak[0], expected, 1.0e-3F));
  CHECK(near(reading->hold[0], 0.5F));
  // Past the hold time the marker falls too, never below the level.
  for (int step = 0; step < 50; ++step) {
    meter.measure(silent.context());
    at += milliseconds{20};
    reading = meter.read(true, at);
  }
  CHECK(reading.has_value());
  if (!reading) return;
  CHECK(reading->hold[0] < 0.5F);
  CHECK(reading->hold[0] >= reading->peak[0]);
  // A new louder block lifts both at once.
  StereoBlock louder{0.8F, 0.1F};
  meter.measure(louder.context());
  at += milliseconds{20};
  reading = meter.read(true, at);
  CHECK(reading.has_value() && near(reading->peak[0], 0.8F) && near(reading->hold[0], 0.8F));
}

TEST_CASE("output level latches clip at full scale until it is reset") {
  OutputLevelMeter meter;
  const auto t0 = Clock::now();
  StereoBlock under{0.999F, 0.5F};
  meter.measure(under.context());
  auto reading = meter.read(true, t0);
  CHECK(reading.has_value() && !reading->clipped);
  StereoBlock full{0.1F, 1.0F};  // exactly 0 dBFS clips
  meter.measure(full.context());
  reading = meter.read(true, t0 + milliseconds{10});
  CHECK(reading.has_value() && reading->clipped);
  // Quiet audio does not clear it.
  for (int step = 0; step < 10; ++step) meter.measure(under.context());
  reading = meter.read(true, t0 + milliseconds{3000});
  CHECK(reading.has_value() && reading->clipped);
  meter.resetClip();
  meter.measure(under.context());
  reading = meter.read(true, t0 + milliseconds{3010});
  CHECK(reading.has_value() && !reading->clipped);
  // An infinite sample clips; NaN is not a level.
  StereoBlock broken{0.0F, 0.0F};
  broken.buffers[0][1] = std::numeric_limits<float>::quiet_NaN();
  meter.measure(broken.context());
  reading = meter.read(true, t0 + milliseconds{3020});
  CHECK(reading.has_value() && !reading->clipped && std::isfinite(reading->peak[0]));
  broken.buffers[1][2] = std::numeric_limits<float>::infinity();
  meter.measure(broken.context());
  reading = meter.read(true, t0 + milliseconds{3030});
  CHECK(reading.has_value() && reading->clipped && std::isfinite(reading->peak[1]));
}

TEST_CASE("output level is absent when the device stops or callbacks stop arriving") {
  OutputLevelMeter meter;
  const auto t0 = Clock::now();
  StereoBlock block{0.7F, 0.7F};
  meter.measure(block.context());
  CHECK(meter.read(true, t0).has_value());
  // Stopped: nothing, even though a block was measured just before.
  meter.measure(block.context());
  CHECK(!meter.read(false, t0 + milliseconds{5}).has_value());
  // Restarted with no new block: still nothing (the old block is not replayed).
  CHECK(!meter.read(true, t0 + milliseconds{10}).has_value());
  StereoBlock quiet{0.1F, 0.1F};
  meter.measure(quiet.context());
  const auto restarted = meter.read(true, t0 + milliseconds{20});
  CHECK(restarted.has_value() && near(restarted->peak[0], 0.1F) && near(restarted->hold[0], 0.1F));
  // Running but silent callbacks stopped (a device that died without stopping): stale -> nothing.
  CHECK(meter.read(true, t0 + milliseconds{300}).has_value());
  CHECK(!meter.read(true, t0 + milliseconds{800}).has_value());
}

TEST_CASE("a mono bus has one entry") {
  OutputLevelMeter meter;
  std::vector<float> mono(32U, 0.0F);
  mono[4] = 0.3F;
  std::array<std::span<float>, 1U> views{mono};
  meter.measure(AudioProcessContext{.sampleRate = 48000.0, .frameCount = 32U, .left = views[0],
                                    .outputs = views});
  const auto reading = meter.read(true, Clock::now());
  CHECK(reading.has_value() && reading->peak.size() == 1U && near(reading->peak[0], 0.3F));
  // Planar host buffers (a plug-in's float and double outputs) measure the same way.
  OutputLevelMeter planar;
  std::vector<double> left(16U, 0.0), right(16U, 0.0);
  left[3] = -0.6;
  right[7] = 0.2;
  const std::array<const double*, 2U> channels{left.data(), right.data()};
  planar.measure(std::span<const double* const>{channels}, 16U);
  const auto planarReading = planar.read(true, Clock::now());
  CHECK(planarReading.has_value() && planarReading->peak.size() == 2U &&
        near(planarReading->peak[0], 0.6F) && near(planarReading->peak[1], 0.2F));
}

TEST_CASE("the ring-buffer processor meters exactly what it hands the device") {
  seam::rendering::SpscInterleavedAudioRingBuffer ring{256U, 2U};
  OutputLevelMeter meter;
  seam::platform::MultichannelRingBufferAudioProcessor processor{ring, 64U, &meter};
  std::vector<float> input(64U * 2U, 0.0F);
  input[10 * 2U] = 0.8F;       // left
  input[20 * 2U + 1U] = -0.4F;  // right
  CHECK(ring.writeFrames(input) == 64U);
  processor.setGain(0.5F);
  StereoBlock device{0.0F, 0.0F};
  processor.process(device.context());
  const auto reading = meter.read(true, Clock::now());
  CHECK(reading.has_value());
  if (!reading) return;
  CHECK(near(reading->peak[0], 0.4F) && near(reading->peak[1], 0.2F));
  float deviceLeft = 0.0F;
  for (const auto sample : device.buffers[0]) deviceLeft = std::max(deviceLeft, std::fabs(sample));
  CHECK(near(deviceLeft, reading->peak[0]));
}

TEST_CASE("output level loses no transient while the audio thread and UI run concurrently") {
  // The UI clock here is synthetic, so staleness is disabled; this checks only the hand-off.
  OutputLevelMeter meter{OutputLevelMeter::Ballistics{.staleAfter = std::chrono::hours{1}}};
  std::atomic<bool> done{false};
  std::thread audio([&meter, &done] {
    StereoBlock quiet{0.1F, 0.1F};
    StereoBlock spike{0.95F, 0.1F};
    for (int block = 0; block < 20000; ++block)
      meter.measure(block == 12345 ? spike.context() : quiet.context());
    done.store(true, std::memory_order_release);
  });
  float loudest = 0.0F;
  auto at = Clock::now();
  while (!done.load(std::memory_order_acquire)) {
    at += milliseconds{1};
    if (const auto reading = meter.read(true, at)) loudest = std::max(loudest, reading->hold[0]);
  }
  audio.join();
  at += milliseconds{1};
  if (const auto reading = meter.read(true, at)) loudest = std::max(loudest, reading->hold[0]);
  CHECK(near(loudest, 0.95F));
}
