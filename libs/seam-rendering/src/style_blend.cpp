#include "seam/rendering/style_blend.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace seam::rendering {
core::Result<void> validateStyleBlendTiming(const synthesis::TimingPlan& primary,
                                          const synthesis::TimingPlan& secondary) {
  if (primary.placements.empty() || primary.placements.size() != secondary.placements.size() ||
      primary.placements.size() > 4096U) {
    return core::failure(core::ErrorCode::Conflict,
        "StyleBlend requires matching bounded phoneme partitions");
  }
  const auto anchorKey = [](const synthesis::PhonemeTimingAnchor& anchor) {
    return std::tie(anchor.key, anchor.nucleusFrame, anchor.endFrame, anchor.explicitStartFrame,
        anchor.syllableIndex, anchor.nucleusKey, anchor.endExplicit, anchor.voiced, anchor.inferredStartFrame);
  };
  for (std::size_t i = 0; i < primary.placements.size(); ++i) {
    const auto& a = primary.placements[i];
    const auto& b = secondary.placements[i];
    if (a.tokenStart != b.tokenStart || a.tokenCount != b.tokenCount || a.startKey != b.startKey ||
        a.targetMidi != b.targetMidi || a.noteOn != b.noteOn || a.sourceStartTick != b.sourceStartTick ||
        a.sourceEndTick != b.sourceEndTick || a.destinationStart != b.destinationStart ||
        a.destinationEnd != b.destinationEnd || a.desiredVowelOnset != b.desiredVowelOnset ||
        a.explicitOnsetStart != b.explicitOnsetStart || a.compressShortTransition != b.compressShortTransition ||
        a.phonemeTargets.size() != b.phonemeTargets.size() ||
        !std::equal(a.phonemeTargets.begin(), a.phonemeTargets.end(), b.phonemeTargets.begin(),
            [&](const auto& first, const auto& second) { return anchorKey(first) == anchorKey(second); })) {
      return core::failure(core::ErrorCode::Conflict,
          "StyleBlend requires matching destination phoneme timing and partitions", a.unitId + " / " + b.unitId);
    }
  }
  return core::success();
}

core::Result<StyleBlendAudio> crossfadeStylePair(const synthesis::PhraseAudio& primary,
    const synthesis::PhraseAudio& secondary, const synthesis::CompiledScorePerformance& performance,
    std::stop_token stop) {
  using Frame = time::SampleFrame;
  constexpr Frame maximumPosition = Frame{1} << 52;
  const auto bounded = [](const auto& audio) {
    return !audio.samples.empty() && audio.samples.size() <= kMaximumStyleBlendFrames &&
        audio.startFrame >= -maximumPosition && audio.startFrame <= maximumPosition &&
        audio.startFrame <= maximumPosition - static_cast<Frame>(audio.samples.size());
  };
  if (!bounded(primary) || !bounded(secondary) ||
      primary.samples.size() > kMaximumStyleBlendFrames - secondary.samples.size() ||
      performance.sampleRate() < 8000U || performance.sampleRate() > 384000U) {
    return core::failure<StyleBlendAudio>(core::ErrorCode::Unsupported,
        "StyleBlend audio exceeds its aggregate frame or timeline budget");
  }
  const auto first = std::min(primary.startFrame, secondary.startFrame);
  const auto last = std::max(primary.startFrame + static_cast<Frame>(primary.samples.size()),
      secondary.startFrame + static_cast<Frame>(secondary.samples.size()));
  if (last - first > static_cast<Frame>(kMaximumStyleBlendFrames)) {
    return core::failure<StyleBlendAudio>(core::ErrorCode::Unsupported,
        "StyleBlend absolute output span exceeds its frame budget");
  }
  const auto sample = [](const auto& audio, Frame frame) -> double {
    const auto offset = frame - audio.startFrame;
    return offset < 0 || offset >= static_cast<Frame>(audio.samples.size()) ? 0.0 :
        static_cast<double>(audio.samples[static_cast<std::size_t>(offset)]);
  };
  for (const auto* arm : {&primary, &secondary}) {
    for (std::size_t i = 0; i < arm->samples.size(); ++i) {
      if ((i & 4095U) == 0U && stop.stop_requested()) return core::failure<StyleBlendAudio>(
          core::ErrorCode::Conflict, "StyleBlend was cancelled");
      if (!std::isfinite(arm->samples[i])) return core::failure<StyleBlendAudio>(
          core::ErrorCode::InvalidArgument, "StyleBlend requires finite source audio");
    }
  }
  StyleBlendReport report;
  const auto window = static_cast<Frame>(performance.sampleRate() / 20U);
  const auto hop = std::max(Frame{1}, window / 2);
  const auto remainder = (first % hop + hop) % hop;
  for (auto start = first - remainder; start < last; start += hop) {
    if (stop.stop_requested()) return core::failure<StyleBlendAudio>(
        core::ErrorCode::Conflict, "StyleBlend compatibility check was cancelled");
    double aa = 0.0, bb = 0.0, ab = 0.0;
    const auto begin = std::max(start, first);
    const auto end = std::min(start + window, last);
    for (auto frame = begin; frame < end; ++frame) {
      const auto a = sample(primary, frame), b = sample(secondary, frame);
      aa += a * a; bb += b * b; ab += a * b;
    }
    // Energy-aware: silence is valid and is not a phase measurement. Compare
    // both active arms regardless of their relative level or requested weight.
    const auto floor = static_cast<double>(end - begin) * 1.0e-12;
    if (aa <= floor || bb <= floor) continue;
    const auto correlation = std::clamp(ab / (std::sqrt(aa) * std::sqrt(bb)), -1.0, 1.0);
    ++report.comparedWindows;
    report.minimumCorrelation = std::min(report.minimumCorrelation, correlation);
    if (correlation < -0.5) return core::failure<StyleBlendAudio>(core::ErrorCode::Conflict,
        "StyleBlend pair has unsafe local phase cancellation; choose a compatible source pair",
        "absolute frame " + std::to_string(begin));
  }
  StyleBlendAudio result;
  result.compatibility = report;
  result.audio.startFrame = first;
  result.audio.samples.resize(static_cast<std::size_t>(last - first));
  bool onlyPrimary = true, onlySecondary = true;
  for (auto frame = first; frame < last; ++frame) {
    const auto offset = static_cast<std::size_t>(frame - first);
    if ((offset & 4095U) == 0U && stop.stop_requested()) return core::failure<StyleBlendAudio>(
        core::ErrorCode::Conflict, "StyleBlend mixing was cancelled");
    const auto amount = performance.at(frame).styleBlend;
    if (!std::isfinite(amount) || amount < 0.0F || amount > 1.0F) return core::failure<StyleBlendAudio>(
        core::ErrorCode::InvalidArgument, "Compiled StyleBlend amount is outside zero to one");
    onlyPrimary = onlyPrimary && amount == 0.0F;
    onlySecondary = onlySecondary && amount == 1.0F;
    const auto a = sample(primary, frame), b = sample(secondary, frame);
    // Branch at endpoints to preserve their exact sample values. The convex
    // double-precision expression neither boosts identical arms nor overflows
    // for finite float inputs, unlike a float (b-a) interpolation.
    result.audio.samples[offset] = amount == 0.0F ? static_cast<float>(a) : amount == 1.0F ?
        static_cast<float>(b) : static_cast<float>((1.0 - static_cast<double>(amount)) * a +
                                                  static_cast<double>(amount) * b);
  }
  // Exact endpoint extent too: no added leading/trailing zero frames.
  if (onlyPrimary) result.audio = primary;
  else if (onlySecondary) result.audio = secondary;
  return result;
}
}
