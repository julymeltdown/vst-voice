#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace seam::voicebank {

struct PitchConfig final {
  std::size_t frameSize{2048};
  std::size_t hopSize{256};
  double minimumHz{60.0};
  double maximumHz{1200.0};
  double voicingThreshold{0.32};
};

struct PitchFrame final {
  std::size_t sourceFrame{0};
  double f0Hz{0.0};
  double confidence{0.0};
  bool voiced{false};
};

[[nodiscard]] core::Result<std::vector<PitchFrame>> analyzePitch(
    std::span<const float> samples,
    std::uint32_t sampleRate,
    PitchConfig config = {});
[[nodiscard]] double medianVoicedPitch(std::span<const PitchFrame> frames) noexcept;

}  // namespace seam::voicebank
