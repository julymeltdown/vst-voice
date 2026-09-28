#pragma once

// Internal to seam-synthesis.
//
// Every classical renderer that retargets voiced material outside its sustain
// has to answer the same two questions: is this source position voiced, and
// what is its local period there. They answer them from one contract -- the
// unit's stored pitch marks, plus measured voicing when the source map carries
// it. The marks are derived from the versioned acoustic analysis of the exact
// take (the generator places them only inside measured voiced spans), the bank's
// content identity binds them to those bytes, and bank QC re-measures them
// against the audio present. Keeping the rule in one place means PSOLA, Spectral
// Classic and Stretch cannot disagree about where the voice is.
//
// Unknown is never read as voiced here. Without a stored mark within one local
// period nothing measured the position, so a renderer keeps its source samples
// there instead of retargeting something that may be a fricative.

#include "seam/synthesis/source_target_map.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace seam::synthesis::detail {

// Median gap between consecutive marks, ignoring gaps of one frame or less.
// Zero when there is nothing to measure.
[[nodiscard]] inline double medianMarkPeriod(std::span<const voicebank::PitchMark> marks) {
  std::vector<double> periods;
  periods.reserve(marks.size() > 1U ? marks.size() - 1U : 0U);
  for (std::size_t index = 1; index < marks.size(); ++index) {
    const auto difference = marks[index].frame - marks[index - 1U].frame;
    if (difference > 1) periods.push_back(static_cast<double>(difference));
  }
  if (periods.empty()) return 0.0;
  const auto middle = periods.begin() + static_cast<std::ptrdiff_t>(periods.size() / 2U);
  std::nth_element(periods.begin(), middle, periods.end());
  if (periods.size() % 2U != 0U) return *middle;
  const auto lower = *std::max_element(periods.begin(), middle);
  return (lower + *middle) * 0.5;
}

// The marks a sustain is cut on: those inside the loop, or, when the loop holds
// fewer than three, those between the stable start and the release.
[[nodiscard]] inline std::vector<voicebank::PitchMark> sustainMarks(
    std::span<const voicebank::PitchMark> marks, time::SampleFrame loopStart,
    time::SampleFrame loopEnd, time::SampleFrame stableStart,
    time::SampleFrame releaseStart) {
  std::vector<voicebank::PitchMark> result;
  for (const auto& mark : marks) {
    if (mark.frame >= loopStart && mark.frame < loopEnd) result.push_back(mark);
  }
  if (result.size() < 3U) {
    for (const auto& mark : marks) {
      if (mark.frame >= stableStart && mark.frame < releaseStart) result.push_back(mark);
    }
    std::sort(result.begin(), result.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.frame < rhs.frame; });
    result.erase(std::unique(result.begin(), result.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.frame == rhs.frame; }),
        result.end());
  }
  return result;
}

class VoicedSourceMarks final {
public:
  // medianPeriod is the sustain's median mark gap in source frames; it bounds
  // every local period so one missing mark cannot read as a lower octave.
  VoicedSourceMarks(std::span<const voicebank::PitchMark> marks,
                    const SourceTargetMap* map, double medianPeriod) noexcept
      : marks_(marks), map_(map), medianPeriod_(medianPeriod) {}

  // False when the marks cannot answer at all; every position then reads as
  // unmeasured.
  [[nodiscard]] bool usable() const noexcept {
    return marks_.size() >= 2U && std::isfinite(medianPeriod_) && medianPeriod_ > 1.0;
  }

  [[nodiscard]] double frame(std::size_t index) const noexcept {
    return static_cast<double>(marks_[index].frame);
  }

  // Requires at least one mark.
  [[nodiscard]] std::size_t nearest(double position) const noexcept {
    const auto found = std::lower_bound(marks_.begin(), marks_.end(), position,
        [](const voicebank::PitchMark& mark, double value) {
          return static_cast<double>(mark.frame) < value;
        });
    if (found == marks_.end()) return marks_.size() - 1U;
    const auto after = static_cast<std::size_t>(found - marks_.begin());
    if (after == 0U) return after;
    return position - static_cast<double>(marks_[after - 1U].frame) <=
                   static_cast<double>(found->frame) - position
               ? after - 1U
               : after;
  }

  // A mark's period is the gap to its nearer neighbour, so the gap across an
  // unvoiced span between two voiced runs is never read as one period.
  [[nodiscard]] double period(std::size_t index) const noexcept {
    auto result = std::numeric_limits<double>::infinity();
    if (index > 0U) {
      result = std::min(result, static_cast<double>(marks_[index].frame - marks_[index - 1U].frame));
    }
    if (index + 1U < marks_.size()) {
      result = std::min(result, static_cast<double>(marks_[index + 1U].frame - marks_[index].frame));
    }
    if (!std::isfinite(result)) result = medianPeriod_;
    return std::clamp(result, medianPeriod_ * 0.55, medianPeriod_ * 1.8);
  }

  // The mark that makes position voiced, or nullopt when the map measured it
  // unvoiced or no stored mark lies within one local period of it.
  [[nodiscard]] std::optional<std::size_t> voicedNear(double position) const noexcept {
    if (!usable()) return std::nullopt;
    if (map_ != nullptr && map_->voicedAtSource(position) == false) return std::nullopt;
    const auto index = nearest(position);
    if (std::abs(frame(index) - position) > period(index)) return std::nullopt;
    return index;
  }

private:
  std::span<const voicebank::PitchMark> marks_;
  const SourceTargetMap* map_;
  double medianPeriod_;
};

struct EdgePitchTarget final {
  double frequencyHz{0.0};
  // A compiled note without a frequency may explicitly accept an unvoiced
  // source span. Keep its samples, but retain the score-derived period for
  // advancing the edge grain train.
  bool retarget{true};
};

// Overlay pitch-retargeted grains only where this exact source take has a
// nearby stored pitch mark and the optional acoustic map does not call it
// unvoiced. The renderer first creates its ordinary edge rendering; uncovered
// or unvoiced positions therefore remain source-faithful.
template <typename AttackSourcePositionAt, typename ReleaseSourcePositionAt,
          typename PitchTargetAt>
[[nodiscard]] core::Result<void> retargetVoicedEdges(
    std::span<const float> mono,
    std::span<const voicebank::PitchMark> marks,
    const SourceTargetMap* sourceMap,
    std::vector<float>& output,
    double medianPeriod,
    std::uint32_t outputSampleRate,
    time::SampleFrame outputFrames,
    time::SampleFrame preFrames,
    time::SampleFrame releaseOutputStart,
    double releaseCenter,
    float sourcePitchResidual,
    std::string_view rendererName,
    std::string_view unitId,
    AttackSourcePositionAt&& attackSourcePositionAt,
    ReleaseSourcePositionAt&& releaseSourcePositionAt,
    PitchTargetAt&& pitchTargetAt,
    std::stop_token stopToken = {}) {
  if (mono.empty() || output.size() != static_cast<std::size_t>(outputFrames) ||
      outputFrames <= 0 || outputSampleRate < 8000U ||
      preFrames < 0 || preFrames > outputFrames || releaseOutputStart < preFrames ||
      releaseOutputStart > outputFrames || !std::isfinite(medianPeriod) ||
      medianPeriod <= 1.0 || !std::isfinite(sourcePitchResidual) ||
      sourcePitchResidual < 0.0F || sourcePitchResidual > 1.0F) {
    return core::failure<void>(core::ErrorCode::InvalidArgument,
        std::string{rendererName} + " voiced-edge retarget input is invalid",
        std::string{unitId});
  }

  const VoicedSourceMarks voicedMarks(marks, sourceMap, medianPeriod);
  if (!voicedMarks.usable()) return {};

  const auto periodAt = [&](double center, const auto& sourcePositionAt)
      -> core::Result<double> {
    const auto centerFrame = std::clamp<time::SampleFrame>(
        static_cast<time::SampleFrame>(std::llround(center)), 0, outputFrames - 1);
    const auto target = pitchTargetAt(centerFrame);
    if (!std::isfinite(target.frequencyHz)) {
      return core::failure<double>(core::ErrorCode::Unsupported,
          std::string{rendererName} + " edge pitch is nonfinite", std::string{unitId});
    }
    const auto fallbackPeriod = std::clamp(
        static_cast<double>(outputSampleRate) / std::max(1.0, target.frequencyHz),
        2.0, static_cast<double>(outputSampleRate) / 20.0);
    if (!target.retarget) return fallbackPeriod;
    if (target.frequencyHz <= 1.0 ||
        target.frequencyHz >= static_cast<double>(outputSampleRate) * 0.45) {
      return core::failure<double>(core::ErrorCode::Unsupported,
          std::string{rendererName} + " edge target pitch is unsupported",
          std::string{unitId});
    }
    const auto markIndex = voicedMarks.voicedNear(sourcePositionAt(center));
    if (!markIndex) return fallbackPeriod;
    const auto sourcePeriod = voicedMarks.period(*markIndex);
    const auto residual = 1.0 + static_cast<double>(sourcePitchResidual) *
        (sourcePeriod / medianPeriod - 1.0);
    return std::clamp(static_cast<double>(outputSampleRate) /
                          target.frequencyHz * residual,
                      2.0, static_cast<double>(outputSampleRate) / 20.0);
  };

  std::vector<float> overlap;
  std::vector<float> weights;
  const auto processRegion = [&](double initialCenter, time::SampleFrame regionStart,
                                 time::SampleFrame regionEnd,
                                 const auto& sourcePositionAt,
                                 bool backwards) -> core::Result<void> {
    if (regionStart >= regionEnd) return {};
    const auto regionFrames = static_cast<std::size_t>(regionEnd - regionStart);
    overlap.assign(regionFrames, 0.0F);
    weights.assign(regionFrames, 0.0F);
    const auto layGrain = [&](double center) -> core::Result<double> {
      const auto centerFrame = static_cast<time::SampleFrame>(std::llround(center));
      const auto period = periodAt(center, sourcePositionAt);
      if (!period) return period;
      const auto target = pitchTargetAt(std::clamp<time::SampleFrame>(
          centerFrame, 0, outputFrames - 1));
      if (!target.retarget) return period.value();
      const auto markIndex = voicedMarks.voicedNear(sourcePositionAt(center));
      if (!markIndex) return period.value();

      const auto sourcePeriod = voicedMarks.period(*markIndex);
      const auto half = std::max<time::SampleFrame>(
          2, static_cast<time::SampleFrame>(std::llround(period.value())));
      // One measured source period occupies one target period. This ratio is in
      // source frames per destination frame, independent of the device rate.
      const auto grainStep = sourcePeriod / std::max(1.0, period.value());
      const auto markFrame = voicedMarks.frame(*markIndex);
      for (time::SampleFrame relative = -half; relative <= half; ++relative) {
        const auto destination = centerFrame + relative;
        if (destination < regionStart || destination >= regionEnd ||
            destination < 0 || destination >= outputFrames ||
            !voicedMarks.voicedNear(sourcePositionAt(static_cast<double>(destination)))) {
          continue;
        }
        const auto window = static_cast<float>(0.5 * (1.0 + std::cos(
            std::numbers::pi * static_cast<double>(relative) / static_cast<double>(half))));
        const auto sourceFrame = markFrame + static_cast<double>(relative) * grainStep;
        const auto boundedSource = std::clamp(sourceFrame, 0.0,
            static_cast<double>(mono.size() - 1U));
        const auto left = static_cast<std::size_t>(std::floor(boundedSource));
        const auto right = std::min(left + 1U, mono.size() - 1U);
        const auto fraction = static_cast<float>(boundedSource - static_cast<double>(left));
        const auto sample = mono[left] * (1.0F - fraction) + mono[right] * fraction;
        const auto index = static_cast<std::size_t>(destination - regionStart);
        overlap[index] += sample * window;
        weights[index] += window;
      }
      return period.value();
    };

    auto center = initialCenter;
    std::size_t grains = 0U;
    const auto beyondEdge = [&]() {
      return backwards
          ? center <= -static_cast<double>(outputSampleRate) / 20.0
          : center >= static_cast<double>(outputFrames) +
                static_cast<double>(outputSampleRate) / 20.0;
    };
    while (!beyondEdge()) {
      if ((grains++ & 0x3fU) == 0U && stopToken.stop_requested()) {
        return core::failure<void>(core::ErrorCode::Conflict,
            std::string{rendererName} + " voiced-edge render was cancelled",
            std::string{unitId});
      }
      const auto period = layGrain(center);
      if (!period) return core::Result<void>{period.error()};
      center += backwards ? -period.value() : period.value();
    }
    for (time::SampleFrame frame = regionStart; frame < regionEnd; ++frame) {
      const auto index = static_cast<std::size_t>(frame - regionStart);
      if (weights[index] <= 1.0e-5F) continue;
      const auto blend = std::min(weights[index], 1.0F);
      const auto outputIndex = static_cast<std::size_t>(frame);
      output[outputIndex] = output[outputIndex] * (1.0F - blend) +
                            (overlap[index] / weights[index]) * blend;
    }
    return {};
  };

  const auto attack = processRegion(static_cast<double>(preFrames), 0, preFrames,
                                    attackSourcePositionAt, true);
  if (!attack) return attack;
  auto center = releaseCenter;
  if (!std::isfinite(center)) center = static_cast<double>(releaseOutputStart);
  const auto release = processRegion(center, releaseOutputStart, outputFrames,
                                     releaseSourcePositionAt, false);
  if (!release) return release;
  return {};
}

}  // namespace seam::synthesis::detail
