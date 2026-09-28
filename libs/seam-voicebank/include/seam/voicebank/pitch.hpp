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

// The samples one analysis frame speaks for.
struct PitchFrameRegion final {
  std::size_t start{0};
  std::size_t end{0};  // exclusive
  std::size_t frameIndex{0};

  friend bool operator==(const PitchFrameRegion&, const PitchFrameRegion&) = default;
};

// Assigns every sample of [0, extent) to exactly one analysis frame: the frame
// whose window centre is nearest. This is the one definition of "the frame that
// describes this sample" shared by the stored acoustic analysis, pitch mark
// generation and anything else that turns frames into sample positions.
//
// Why the centre: a frame at origin s measures [s, s + frameSize), so its
// conclusion is best supported at s + frameSize / 2, where both halves of the
// window agree. Anchoring a frame to its origin instead attributes that conclusion
// to the first hop of the window, which is at the window's edge. Measured on a
// 48 kHz engineering fixture (200 Hz sine, noise from 19200 to 28800, 240 Hz
// sine) with the 2048/256 producer analysis, origin anchoring put the
// noise-to-voice boundary 1408 samples before the real one: 29 ms of fricative
// described as voiced.
//
// Regions are contiguous, ascending, non-empty and cover [0, extent) exactly;
// the first reaches back to 0 and the last reaches forward to extent. Frames are
// expected in ascending origin order, as analyzePitch emits them. An empty result
// means there is nothing to partition (no frames, zero extent, or frames out of
// order), never a partial description.
[[nodiscard]] std::vector<PitchFrameRegion> partitionPitchFrames(
    std::span<const PitchFrame> frames, std::size_t frameSize, std::size_t extent);

}  // namespace seam::voicebank
