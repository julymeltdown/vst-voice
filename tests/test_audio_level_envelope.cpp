#include "test_framework.hpp"
#include "seam/rendering/audio_level_envelope.hpp"
#include <cmath>
#include <limits>

TEST_CASE("measured PCM levels preserve antiphase channels silence and float headroom") {
  using namespace seam::rendering;
  const std::vector<float> pcm{1.0F, -1.0F, 0.5F, -0.5F};
  const auto result = measureAudioLevels(pcm, 2U, 0U, 2U, 1U); CHECK(result);
  CHECK(result.value().channelCount == 2U); CHECK(result.value().bins.size() == 1U);
  for (const auto& channel : result.value().bins[0].channels) {
    CHECK_NEAR(channel.rms, std::sqrt(0.625), 1e-12); CHECK(channel.peak == 1.0);
    CHECK(channel.atOrAboveFullScale == 1U); CHECK(channel.rmsDbfs());
    CHECK_NEAR(*channel.rmsDbfs(), 20.0 * std::log10(std::sqrt(0.625)), 1e-12);
  }
  const std::vector<float> extremes{0.0F, 2.0F, -2.0F, 0.0F};
  const auto perFrame = measureAudioLevels(extremes, 1U, 0U, 4U, 4096U); CHECK(perFrame);
  CHECK(perFrame.value().bins.size() == 4U); CHECK(!perFrame.value().bins[0].channels[0].rmsDbfs());
  CHECK(perFrame.value().bins[1].channels[0].rms == 2.0); CHECK(perFrame.value().bins[2].channels[0].peak == 2.0);
  const std::vector<float> large{std::numeric_limits<float>::max()};
  const auto huge = measureAudioLevels(large, 1U, 0U, 1U); CHECK(huge);
  CHECK(std::isfinite(huge.value().bins[0].channels[0].rms));
}

TEST_CASE("measured audio windows partition frames exactly with correct partial bin energy") {
  using namespace seam::rendering;
  const std::vector<float> pcm{1000, 1, 2, 3, 4, 5, 6, 1000};
  const auto result = measureAudioLevels(pcm, 1U, 1U, 6U, 4U); CHECK(result);
  CHECK(result.value().firstFrame == 1U); CHECK(result.value().frameCount == 6U);
  std::size_t next = 1U; double energy = 0.0;
  for (const auto& bin : result.value().bins) {
    CHECK(bin.firstFrame == next); CHECK(bin.frameCount > 0U); CHECK(bin.channels.size() == 1U);
    next += bin.frameCount; energy += bin.channels[0].rms * bin.channels[0].rms * static_cast<double>(bin.frameCount);
    CHECK(bin.channels[0].peak <= 6.0);
  }
  CHECK(next == 7U); CHECK_NEAR(energy, 91.0, 1e-10);
  CHECK(result.value().bins[1].frameCount == 2U);
}

TEST_CASE("audio measurement rejects invalid shapes nonfinite windows cancellation and excess work") {
  using namespace seam::rendering;
  std::vector<float> pcm{0.5F, std::numeric_limits<float>::quiet_NaN(), 0.25F};
  CHECK(!measureAudioLevels(pcm, 0U, 0U, 1U)); CHECK(!measureAudioLevels(pcm, 65U, 0U, 1U));
  CHECK(!measureAudioLevels(pcm, 2U, 0U, 1U)); CHECK(!measureAudioLevels(pcm, 1U, 0U, 0U));
  CHECK(!measureAudioLevels(pcm, 1U, 0U, 1U, 0U)); CHECK(!measureAudioLevels(pcm, 1U, 0U, 1U, 4097U));
  CHECK(!measureAudioLevels(pcm, 1U, std::numeric_limits<std::size_t>::max(), 1U));
  CHECK(!measureAudioLevels(pcm, 1U, 2U, 2U)); CHECK(!measureAudioLevels(pcm, 1U, 0U, 3U));
  CHECK(measureAudioLevels(pcm, 1U, 2U, 1U)); // Unmeasured source is deliberately outside this window's evidence.
  pcm[1] = std::numeric_limits<float>::infinity(); CHECK(!measureAudioLevels(pcm, 1U, 0U, 3U));
  std::stop_source stop; stop.request_stop(); CHECK(!measureAudioLevels(pcm, 1U, 0U, 1U, 1U, stop.get_token()));
  const std::vector<float> overBudget(kMaximumMeasuredWindowSamples + 1U, 0.0F);
  CHECK(!measureAudioLevels(overBudget, 1U, 0U, overBudget.size()));
}
