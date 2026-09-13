#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <stop_token>
#include <limits>

namespace seam::voicebank {

enum class PitchCorrelationMethod { Direct, Fft };
enum class PitchFrameCoverage { CompleteWindows, FullHopGrid };

struct PitchConfig final {
  std::size_t frameSize{2048};
  std::size_t hopSize{256};
  double minimumHz{60.0};
  double maximumHz{1200.0};
  double voicingThreshold{0.32};
  PitchCorrelationMethod correlationMethod{PitchCorrelationMethod::Direct};
  // FullHopGrid emits ceil(sampleCount / hopSize) frames, zero-padding tails.
  PitchFrameCoverage coverage{PitchFrameCoverage::CompleteWindows};
};

struct PitchFrame final {
  std::size_t sourceFrame{0};
  double f0Hz{0.0};
  double confidence{0.0};
  bool voiced{false};
};
struct PitchAnalysisLimits final {
  std::size_t maximumFrames{1048576U};
  std::uint64_t maximumCorrelationTerms{std::numeric_limits<std::uint64_t>::max()};
  std::uint64_t maximumTransformButterflies{std::numeric_limits<std::uint64_t>::max()};
};

[[nodiscard]] core::Result<std::vector<PitchFrame>> analyzePitch(
    std::span<const float> samples,
    std::uint32_t sampleRate,
    PitchConfig config = {}, std::stop_token stopToken = {}, PitchAnalysisLimits limits = {});
[[nodiscard]] double medianVoicedPitch(std::span<const PitchFrame> frames) noexcept;

}  // namespace seam::voicebank
