#include "seam/core/resample.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace seam::core {
namespace {

// Normalised sinc. The limit at zero is 1.
double sinc(double value) {
  if (std::abs(value) < 1e-12) return 1.0;
  const auto angle = std::numbers::pi * value;
  return std::sin(angle) / angle;
}

// Blackman window over [-1, 1]. Continuous, so it introduces no derivative
// discontinuity the way a rectangular truncation would, and its sidelobes fall
// fast enough that a 16-crossing half width puts stopband energy below the
// float32 noise floor.
double windowWeight(double normalized) {
  if (std::abs(normalized) >= 1.0) return 0.0;
  const auto position = std::numbers::pi * (normalized + 1.0);
  return 0.42 - 0.5 * std::cos(position) + 0.08 * std::cos(2.0 * position);
}

// One kernel evaluation, expressed over a read callback so contiguous and
// deque-backed callers share exactly one implementation.
double bandLimitedAt(const std::function<double(std::int64_t)>& read,
                     double position, double ratio, std::int64_t totalFrames) {
  if (totalFrames <= 0) return 0.0;
  // Cutoff in source-domain cycles per sample. Downsampling puts it below 0.5 so
  // the kernel itself does the anti-aliasing; upsampling leaves it at 0.5.
  const auto cutoff = std::min(0.5, 0.5 * ratio);
  const auto halfWidth = resampleKernelHalfWidthSamples(ratio);
  const auto centre = static_cast<std::int64_t>(std::floor(position));
  double accumulated = 0.0;
  double weightTotal = 0.0;
  for (std::int64_t offset = -halfWidth; offset <= halfWidth; ++offset) {
    const auto index = centre + offset;
    const auto clamped = std::clamp<std::int64_t>(index, 0, totalFrames - 1);
    const auto distance = position - static_cast<double>(index);
    const auto weight = 2.0 * cutoff * sinc(2.0 * cutoff * distance) *
                        windowWeight(distance / static_cast<double>(halfWidth));
    accumulated += read(clamped) * weight;
    weightTotal += weight;
  }
  // Normalising by the realised weight sum keeps DC gain at exactly 1 even where
  // the kernel is truncated at the signal edges.
  return weightTotal == 0.0 ? 0.0 : accumulated / weightTotal;
}

}  // namespace

std::int64_t resampleKernelHalfWidthSamples(double ratio) noexcept {
  const auto cutoff = std::min(0.5, 0.5 * ratio);
  if (!(cutoff > 0.0)) return static_cast<std::int64_t>(kResampleKernelHalfWidth);
  return static_cast<std::int64_t>(
      std::ceil(kResampleKernelHalfWidth / (2.0 * cutoff)));
}

double bandLimitedSampleAt(std::span<const float> source, double position,
                           double ratio, std::size_t channels,
                           std::size_t channel) {
  if (source.empty() || channels == 0U || channel >= channels) return 0.0;
  const auto frames = source.size() / channels;
  if (frames == 0U) return 0.0;
  const auto totalFrames = static_cast<std::int64_t>(frames);
  const auto read = [&](std::int64_t index) {
    return static_cast<double>(
        source[static_cast<std::size_t>(index) * channels + channel]);
  };
  return bandLimitedAt(read, position, ratio, totalFrames);
}

double bandLimitedSampleAt(const std::function<double(std::int64_t)>& read,
                           double position, double ratio,
                           std::int64_t totalFrames) {
  return bandLimitedAt(read, position, ratio, totalFrames);
}

}  // namespace seam::core

