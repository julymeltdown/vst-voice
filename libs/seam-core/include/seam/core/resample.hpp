#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

// Band-limited sample-rate conversion, shared by every path that changes rate.
//
// Why this is shared rather than written per call site: three separate
// interpolators in this repository each downsampled by linear interpolation
// between neighbouring samples. None of them removed energy above the target
// Nyquist frequency, so that energy folded back into the band instead of being
// rejected. Measured through the production paths: a 10 kHz tone resampled
// 48k -> 16k survived at full amplitude as a 6 kHz tone; a 30 kHz tone
// resampled 96k -> 48k survived at ratio 1.0000; a 27.5 kHz tone resampled
// 48k -> 44.1k survived at 0.6074. Aliasing is not attenuation -- the energy
// reappears at a frequency the source never contained, which for voice reads as
// wrong pitch content rather than as noise.
//
// Each output sample is the windowed-sinc interpolation of the input with the
// cutoff at the lower of the two Nyquist frequencies, so downsampling
// attenuates what cannot be represented before it can alias, and upsampling
// still reconstructs the original band.
namespace seam::core {

// Half-width of the interpolation kernel in zero crossings. Larger suppresses
// more stopband energy; 16 with a Blackman window puts the stopband below the
// float32 noise floor at a cost linear in the input length.
inline constexpr double kResampleKernelHalfWidth = 16.0;

// One output sample of the band-limited interpolation at fractional input
// position `position`. `ratio` is targetRate / sourceRate. The returned value is
// normalised by the realised kernel weight so DC gain is exactly 1 even where
// the kernel is truncated at the signal edges.
[[nodiscard]] double bandLimitedSampleAt(
    std::span<const float> source, double position, double ratio,
    std::size_t channels, std::size_t channel);

// The same kernel for callers whose samples are not contiguous. `read` returns
// the sample at an absolute frame index and must be defined for every index in
// [0, totalFrames); the caller supplies whatever edge handling its storage needs.
// This exists because a streaming converter buffers in a deque, which has no
// contiguous span, and duplicating the kernel for that case is how the two
// converters in this repository drifted apart in the first place.
[[nodiscard]] double bandLimitedSampleAt(
    const std::function<double(std::int64_t)>& read, double position,
    double ratio, std::int64_t totalFrames);
// Number of input samples on each side of `position` the kernel reaches.
[[nodiscard]] std::int64_t resampleKernelHalfWidthSamples(double ratio) noexcept;

}  // namespace seam::core
