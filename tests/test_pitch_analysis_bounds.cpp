#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/voicebank/pitch.hpp"
#include <limits>
#include <numbers>
#include <cmath>

TEST_CASE("FFT pitch correlation matches the direct estimator across rates and signals") {
  using namespace seam::voicebank;
  for (const auto rate : {8000U, 48000U, 192000U}) {
    PitchConfig direct;
    direct.frameSize = rate == 8000U ? 300U : (rate == 48000U ? 2048U : 8192U);
    direct.hopSize = direct.frameSize;
    auto fast = direct; fast.correlationMethod = PitchCorrelationMethod::Fft;
    for (const auto frequency : {0.0, 110.0, 440.0}) {
      std::vector<float> samples(direct.frameSize * 2U, 0.0F);
      if (frequency > 0.0) for (std::size_t index = 0U; index < samples.size(); ++index) {
        const auto phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(index) / rate;
        samples[index] = static_cast<float>(0.2 * std::sin(phase) + 0.03 * std::sin(2.0 * phase) + 0.01);
      }
      const auto reference = analyzePitch(samples, rate, direct); CHECK(reference);
      const auto transformed = analyzePitch(samples, rate, fast); CHECK(transformed);
      CHECK(transformed.value().size() == reference.value().size());
      for (std::size_t index = 0U; index < reference.value().size(); ++index) {
        CHECK(transformed.value()[index].sourceFrame == reference.value()[index].sourceFrame);
        CHECK(transformed.value()[index].voiced == reference.value()[index].voiced);
        CHECK_NEAR(transformed.value()[index].f0Hz, reference.value()[index].f0Hz, 1e-6);
        CHECK_NEAR(transformed.value()[index].confidence, reference.value()[index].confidence, 1e-9);
      }
    }
  }
}

TEST_CASE("FFT pitch work admission counts both transforms before allocation") {
  using namespace seam::voicebank;
  std::vector<float> samples(4096U, 0.0F);
  PitchConfig config; config.hopSize = 2048U; config.correlationMethod = PitchCorrelationMethod::Fft;
  const auto butterflies = std::uint64_t{4096} * 12U * 2U;
  CHECK(analyzePitch(samples, 48000U, config, {}, {2U, 0U, butterflies}));
  CHECK(!analyzePitch(samples, 48000U, config, {}, {2U, 0U, butterflies - 1U}));
  CHECK(!analyzePitch(samples, 48000U, config, {}, {1U, 0U, butterflies}));
  config.correlationMethod = static_cast<PitchCorrelationMethod>(99);
  CHECK(!analyzePitch(samples, 48000U, config));
}

TEST_CASE("FFT pitch analysis admits longer gestures under a finite transform budget") {
  using namespace seam::voicebank;
  const auto samples = seam::test::support::sineWave(48000U, 220.0, 8.0, 0.25F);
  PitchConfig config; config.hopSize = 480U;
  CHECK(!analyzePitch(samples, 48000U, config, {}, {256U, 500000000U}));
  config.correlationMethod = PitchCorrelationMethod::Fft;
  const auto result = analyzePitch(samples, 48000U, config, {}, {4096U, 0U, 512000000U});
  CHECK(result); CHECK(result.value().size() > 256U);
  CHECK(result.value().back().sourceFrame + config.frameSize <= samples.size());
  CHECK_NEAR(medianVoicedPitch(result.value()), 220.0, 2.0);
  std::stop_source stop; stop.request_stop();
  CHECK(!analyzePitch(samples, 48000U, config, stop.get_token(), {4096U, 0U, 512000000U}));
}

TEST_CASE("pitch analysis bounds reject nonfinite configuration and preserve normal measurements") {
  using namespace seam::voicebank;
  const auto samples = seam::test::support::sineWave(48000U, 220.0, 0.2, 0.25F);
  const auto normal = analyzePitch(samples, 48000U); CHECK(normal);
  CHECK_NEAR(medianVoicedPitch(normal.value()), 220.0, 2.0);
  for (const auto value : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
    PitchConfig config;
    config.minimumHz = value; CHECK(!analyzePitch(samples, 48000U, config));
    config = {}; config.maximumHz = value; CHECK(!analyzePitch(samples, 48000U, config));
    config = {}; config.voicingThreshold = value; CHECK(!analyzePitch(samples, 48000U, config));
  }
  PitchConfig tiny;
  tiny.minimumHz = std::numeric_limits<double>::denorm_min();
  tiny.maximumHz = 2.0 * tiny.minimumHz;
  CHECK(!analyzePitch(samples, 48000U, tiny));
  PitchConfig huge;
  huge.frameSize = std::numeric_limits<std::size_t>::max();
  CHECK(!analyzePitch(samples, 48000U, huge));
  std::stop_source stop; stop.request_stop();
  const auto cancelled = analyzePitch(samples, 48000U, {}, stop.get_token());
  CHECK(!cancelled); CHECK(cancelled.error().code == seam::core::ErrorCode::Conflict);
}

TEST_CASE("pitch analysis admits exact correlation budgets before allocating results") {
  using namespace seam::voicebank;
  const auto samples = seam::test::support::sineWave(48000U, 220.0, 0.2, 0.25F);
  const auto frames = 1U + (samples.size() - 2048U) / 256U;
  // Default lag interval is [40, 800], inclusive.
  const auto terms = std::uint64_t{761} * (2048U - 420U) * frames;
  const auto exact = analyzePitch(samples, 48000U, {}, {}, {frames, terms}); CHECK(exact);
  CHECK(exact.value().size() == frames);
  CHECK(!analyzePitch(samples, 48000U, {}, {}, {frames - 1U, terms}));
  CHECK(!analyzePitch(samples, 48000U, {}, {}, {frames, terms - 1U}));
  CHECK(!analyzePitch(samples, 48000U, {}, {}, {0U, 0U}));
  const auto unrestricted = analyzePitch(samples, 48000U); CHECK(unrestricted);
  for (std::size_t index = 0U; index < frames; ++index) {
    CHECK(exact.value()[index].sourceFrame == unrestricted.value()[index].sourceFrame);
    CHECK(exact.value()[index].f0Hz == unrestricted.value()[index].f0Hz);
    CHECK(exact.value()[index].confidence == unrestricted.value()[index].confidence);
    CHECK(exact.value()[index].voiced == unrestricted.value()[index].voiced);
  }
  std::vector<float> silence(samples.size(), 0.0F);
  CHECK(!analyzePitch(silence, 48000U, {}, {}, {frames, terms - 1U}));
}
