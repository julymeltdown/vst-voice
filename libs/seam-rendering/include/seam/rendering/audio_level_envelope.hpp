#pragma once
#include "seam/core/result.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace seam::rendering {
struct MeasuredChannelLevel final {
  double rms{0.0}, peak{0.0};
  std::size_t atOrAboveFullScale{0U};
  // Absence denotes digital silence, not a finite display-floor measurement.
  [[nodiscard]] std::optional<double> rmsDbfs() const noexcept;
  friend bool operator==(const MeasuredChannelLevel&, const MeasuredChannelLevel&) = default;
};
struct MeasuredAudioBin final {
  std::size_t firstFrame{0U}, frameCount{0U};
  std::vector<MeasuredChannelLevel> channels;
  friend bool operator==(const MeasuredAudioBin&, const MeasuredAudioBin&) = default;
};
struct MeasuredAudioEnvelope final {
  std::size_t firstFrame{0U}, frameCount{0U};
  std::uint8_t channelCount{0U};
  std::vector<MeasuredAudioBin> bins;
  friend bool operator==(const MeasuredAudioEnvelope&, const MeasuredAudioEnvelope&) = default;
};
inline constexpr std::size_t kMaximumMeasuredWindowSamples = 16U * 1024U * 1024U;
inline constexpr std::size_t kMaximumMeasuredBins = 4096U;
// Read-only analysis of a requested interleaved PCM window. Frame indices are
// absolute within this buffer, not inferred score ticks. Never call on audio RT.
// No downmix, clipping, nonfinite substitution, resampling or normalization.
[[nodiscard]] core::Result<MeasuredAudioEnvelope> measureAudioLevels(
    std::span<const float> interleaved, std::uint8_t channels,
    std::size_t firstFrame, std::size_t frameCount, std::size_t maximumBins = 512U,
    std::stop_token stop = {});
}
