#include "test_framework.hpp"

#include "seam/rendering/sample_rate_converter.hpp"

#include <array>
#include <cmath>
#include <span>
#include <vector>

TEST_CASE("sample rate converter preserves bounded impulse placement") {
  const std::array<float, 4> impulse{0.0F, 1.0F, 0.0F, 0.0F};
  const auto converted = seam::rendering::SampleRateConverter::convert(
      impulse, 44100U, 48000U, seam::rendering::SampleRateQuality::Final);
  CHECK(converted);
  CHECK(converted.value().size() == 4U);
  for (const auto sample : converted.value()) CHECK(std::isfinite(sample));
  CHECK(converted.value()[1] > 0.8F);
}

TEST_CASE("sample rate converter rejects invalid rates") {
  const auto converted = seam::rendering::SampleRateConverter::convert(
      std::array<float, 1>{0.0F}, 1000U, 48000U);
  CHECK(!converted);
  CHECK(converted.error().code == seam::core::ErrorCode::InvalidArgument);
}

TEST_CASE("streaming sample rate converter matches bounded whole-buffer conversion") {
  const std::vector<float> source{0.0F, 0.25F, 0.5F, 0.75F, 1.0F,
                                  0.5F, 0.0F, -0.5F, -1.0F};
  const auto expected = seam::rendering::SampleRateConverter::convert(
      source, 44100U, 48000U, seam::rendering::SampleRateQuality::Final);
  CHECK(expected);
  seam::rendering::StreamingSampleRateConverter streaming{
      44100U, 48000U, seam::rendering::SampleRateQuality::Final};
  std::vector<float> actual;
  const auto first = streaming.append(
      std::span<const float>{source.data(), 3U});
  CHECK(first);
  actual.insert(actual.end(), first.value().begin(), first.value().end());
  const auto second = streaming.append(
      std::span<const float>{source.data() + 3U, source.size() - 3U});
  CHECK(second);
  actual.insert(actual.end(), second.value().begin(), second.value().end());
  const auto tail = streaming.finish();
  CHECK(tail);
  actual.insert(actual.end(), tail.value().begin(), tail.value().end());
  CHECK(actual.size() == expected.value().size());
  for (std::size_t index = 0U; index < actual.size(); ++index) {
    CHECK_NEAR(actual[index], expected.value()[index], 1.0e-5);
  }
  CHECK(!streaming.finish().value().size());
}
