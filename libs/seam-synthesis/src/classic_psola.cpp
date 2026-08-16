#include "seam/synthesis/classic_psola.hpp"

#include "seam/voicebank/pitch_marks.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>
#include <vector>

namespace seam::synthesis {
namespace {

float interpolate(std::span<const float> samples, double position) noexcept {
  if (samples.empty()) return 0.0F;
  const auto clamped = std::clamp(position, 0.0,
                                  static_cast<double>(samples.size() - 1U));
  const auto left = static_cast<std::size_t>(std::floor(clamped));
  const auto right = std::min(left + 1U, samples.size() - 1U);
  const auto fraction = static_cast<float>(clamped - static_cast<double>(left));
  return samples[left] * (1.0F - fraction) + samples[right] * fraction;
}

double midiToHz(double midi) noexcept {
  return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

double medianPeriod(std::span<const voicebank::PitchMark> marks) {
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

double localPeriod(std::span<const voicebank::PitchMark> marks,
                   std::size_t index,
                   double fallback) noexcept {
  if (marks.size() < 2U) return fallback;
  if (index == 0U) {
    return static_cast<double>(marks[1U].frame - marks[0U].frame);
  }
  if (index + 1U >= marks.size()) {
    return static_cast<double>(marks[index].frame - marks[index - 1U].frame);
  }
  return static_cast<double>(marks[index + 1U].frame - marks[index - 1U].frame) * 0.5;
}

float loopFallback(std::span<const float> samples,
                   double sourcePosition,
                   double loopStart,
                   double loopEnd) noexcept {
  const auto length = loopEnd - loopStart;
  if (length <= 1.0) return interpolate(samples, sourcePosition);
  auto relative = std::fmod(sourcePosition - loopStart, length);
  if (relative < 0.0) relative += length;
  return interpolate(samples, loopStart + relative);
}

}  // namespace

core::Result<RenderedUnit> ClassicPsolaRenderer::render(
    const voicebank::Unit& unit,
    const voicebank::AudioBuffer& source,
    std::uint32_t outputSampleRate,
    time::SampleFrame outputFrames,
    std::int32_t targetMidi,
    const PsolaRenderParameters& parameters,
    std::stop_token stopToken) const {
  if (source.sampleRate == 0 || source.channels == 0 || source.interleaved.empty() ||
      outputSampleRate < 8000 || outputSampleRate > 384000 || outputFrames <= 0 ||
      targetMidi < 0 || targetMidi > 127 ||
      !std::isfinite(parameters.sourcePitchResidual) ||
      parameters.sourcePitchResidual < 0.0F || parameters.sourcePitchResidual > 1.0F ||
      !std::isfinite(parameters.additionalGainDb)) {
    return core::failure<RenderedUnit>(core::ErrorCode::InvalidArgument,
                                       "Classic PSOLA input is invalid",
                                       unit.id);
  }
  const auto curveValidation = parameters.pitchCurve.validate();
  if (!curveValidation) return core::Result<RenderedUnit>{curveValidation.error()};
  const auto markerValidation = unit.markers.validate(
      static_cast<time::SampleFrame>(source.frameCount()));
  if (!markerValidation) return core::Result<RenderedUnit>{markerValidation.error()};
  const auto pitchMarkValidation = voicebank::validatePitchMarks(
      unit.pitchMarks, unit.markers.audioOffset, unit.markers.audioEnd);
  if (!pitchMarkValidation || unit.pitchMarks.size() < 3U) {
    return core::failure<RenderedUnit>(core::ErrorCode::Unsupported,
                                       "Classic PSOLA requires valid editable pitch marks",
                                       unit.id);
  }

  const auto mono = source.monoMix();
  const auto sampleRateRatio = static_cast<double>(outputSampleRate) /
                               static_cast<double>(source.sampleRate);
  const auto sourcePerOutput = static_cast<double>(source.sampleRate) /
                               static_cast<double>(outputSampleRate);
  const auto offset = unit.markers.audioOffset;
  const auto stableStart = unit.markers.stableStart;
  const auto loopStart = unit.markers.loopStart.value_or(stableStart);
  const auto releaseStart = unit.markers.releaseStart.value_or(unit.markers.audioEnd);
  const auto loopEnd = unit.markers.loopEnd.value_or(releaseStart);
  const auto audioEnd = unit.markers.audioEnd;

  const auto preFrames = std::clamp<time::SampleFrame>(
      static_cast<time::SampleFrame>(std::llround(
          static_cast<double>(stableStart - offset) * sampleRateRatio)),
      0, outputFrames);
  const auto releaseFrames = std::clamp<time::SampleFrame>(
      static_cast<time::SampleFrame>(std::llround(
          static_cast<double>(audioEnd - releaseStart) * sampleRateRatio)),
      0, outputFrames);
  const auto releaseOutputStart = std::max(preFrames, outputFrames - releaseFrames);
  const auto vowelOnsetOffset = std::clamp<time::SampleFrame>(
      static_cast<time::SampleFrame>(std::llround(
          static_cast<double>(unit.markers.vowelOnset - offset) * sampleRateRatio)),
      0, outputFrames - 1);

  RenderedUnit result;
  result.unitId = unit.id;
  result.vowelOnsetOffset = vowelOnsetOffset;
  result.samples.resize(static_cast<std::size_t>(outputFrames), 0.0F);

  // Establish a deterministic fallback waveform first. PSOLA replaces only the
  // voiced sustain region; consonants and release transients retain their source identity.
  for (time::SampleFrame output = 0; output < outputFrames; ++output) {
    if ((output & 0x3fff) == 0 && stopToken.stop_requested()) {
      return core::failure<RenderedUnit>(core::ErrorCode::Conflict,
                                         "Classic PSOLA render was cancelled",
                                         unit.id);
    }
    double sourcePosition = static_cast<double>(offset);
    if (output < preFrames) {
      sourcePosition = static_cast<double>(offset) +
          static_cast<double>(output) * sourcePerOutput;
      result.samples[static_cast<std::size_t>(output)] = interpolate(mono, sourcePosition);
    } else if (releaseFrames > 0 && output >= releaseOutputStart) {
      sourcePosition = static_cast<double>(releaseStart) +
          static_cast<double>(output - releaseOutputStart) * sourcePerOutput;
      result.samples[static_cast<std::size_t>(output)] =
          interpolate(mono, std::min(sourcePosition, static_cast<double>(audioEnd - 1)));
    } else {
      sourcePosition = static_cast<double>(loopStart) +
          static_cast<double>(output - preFrames) * sourcePerOutput;
      result.samples[static_cast<std::size_t>(output)] = loopFallback(
          mono, sourcePosition, static_cast<double>(loopStart),
          static_cast<double>(loopEnd));
    }
  }

  std::vector<voicebank::PitchMark> stableMarks;
  for (const auto& mark : unit.pitchMarks) {
    if (mark.frame >= loopStart && mark.frame < loopEnd) stableMarks.push_back(mark);
  }
  if (stableMarks.size() < 3U) {
    for (const auto& mark : unit.pitchMarks) {
      if (mark.frame >= stableStart && mark.frame < releaseStart) {
        stableMarks.push_back(mark);
      }
    }
    std::sort(stableMarks.begin(), stableMarks.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.frame < rhs.frame; });
    stableMarks.erase(std::unique(stableMarks.begin(), stableMarks.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.frame == rhs.frame; }),
        stableMarks.end());
  }
  if (stableMarks.size() < 3U) {
    return core::failure<RenderedUnit>(core::ErrorCode::Unsupported,
                                       "Classic PSOLA has too few marks in the sustain region",
                                       unit.id);
  }

  const auto sourceMedianPeriod = medianPeriod(stableMarks);
  if (!std::isfinite(sourceMedianPeriod) || sourceMedianPeriod <= 1.0) {
    return core::failure<RenderedUnit>(core::ErrorCode::Unsupported,
                                       "Classic PSOLA source period is invalid",
                                       unit.id);
  }

  std::vector<float> overlap(result.samples.size(), 0.0F);
  std::vector<float> weights(result.samples.size(), 0.0F);
  auto outputMark = static_cast<double>(preFrames);
  std::size_t sourceIndex = 0;
  std::size_t generatedMarks = 0;
  while (outputMark < static_cast<double>(releaseOutputStart)) {
    if ((generatedMarks & 0x3fU) == 0U && stopToken.stop_requested()) {
      return core::failure<RenderedUnit>(core::ErrorCode::Conflict,
                                         "Classic PSOLA render was cancelled",
                                         unit.id);
    }
    const auto stableIndex = sourceIndex % stableMarks.size();
    const auto& sourceMark = stableMarks[stableIndex];
    const auto periodSource = std::clamp(
        localPeriod(stableMarks, stableIndex, sourceMedianPeriod),
        sourceMedianPeriod * 0.55, sourceMedianPeriod * 1.8);
    const auto halfOutput = std::max<time::SampleFrame>(
        2, static_cast<time::SampleFrame>(std::llround(periodSource * sampleRateRatio)));
    const auto centerOutput = static_cast<time::SampleFrame>(std::llround(outputMark));
    for (time::SampleFrame relative = -halfOutput;
         relative <= halfOutput; ++relative) {
      const auto destination = centerOutput + relative;
      if (destination < preFrames || destination >= releaseOutputStart ||
          destination < 0 || destination >= outputFrames) {
        continue;
      }
      const auto normalized = static_cast<double>(relative) /
                              static_cast<double>(halfOutput);
      const auto window = static_cast<float>(
          0.5 * (1.0 + std::cos(std::numbers::pi * normalized)));
      const auto sourcePosition = static_cast<double>(sourceMark.frame) +
                                  static_cast<double>(relative) * sourcePerOutput;
      const auto sample = interpolate(mono, sourcePosition);
      const auto index = static_cast<std::size_t>(destination);
      overlap[index] += sample * window;
      weights[index] += window;
    }

    const auto cents = parameters.pitchCurve.centsAt(centerOutput);
    const auto targetHz = midiToHz(static_cast<double>(targetMidi) +
                                   static_cast<double>(cents) / 100.0);
    if (!std::isfinite(targetHz) || targetHz <= 1.0 ||
        targetHz >= static_cast<double>(outputSampleRate) * 0.45) {
      return core::failure<RenderedUnit>(core::ErrorCode::Unsupported,
                                         "Classic PSOLA target pitch is unsupported",
                                         unit.id);
    }
    const auto residualRatio = periodSource / sourceMedianPeriod;
    const auto residual = 1.0 +
        static_cast<double>(parameters.sourcePitchResidual) * (residualRatio - 1.0);
    const auto targetPeriod = std::clamp(
        static_cast<double>(outputSampleRate) / targetHz * residual,
        2.0, static_cast<double>(outputSampleRate) / 20.0);
    outputMark += targetPeriod;
    ++sourceIndex;
    ++generatedMarks;
  }

  for (time::SampleFrame frame = preFrames; frame < releaseOutputStart; ++frame) {
    const auto index = static_cast<std::size_t>(frame);
    if (weights[index] > 1.0e-5F) {
      result.samples[index] = overlap[index] / weights[index];
    }
  }

  const auto gain = static_cast<float>(std::pow(10.0,
      static_cast<double>(unit.gainDb + parameters.additionalGainDb) / 20.0));
  double mean = 0.0;
  for (auto& sample : result.samples) {
    sample *= gain;
    if (!std::isfinite(sample)) sample = 0.0F;
    mean += sample;
  }
  mean /= static_cast<double>(result.samples.size());
  for (auto& sample : result.samples) sample -= static_cast<float>(mean);

  const auto fadeFrames = std::min<std::size_t>(
      result.samples.size() / 2U,
      std::max<std::size_t>(1, static_cast<std::size_t>(outputSampleRate / 1000U)));
  for (std::size_t index = 0; index < fadeFrames; ++index) {
    const auto factor = static_cast<float>(index + 1U) /
                        static_cast<float>(fadeFrames + 1U);
    result.samples[index] *= factor;
    result.samples[result.samples.size() - 1U - index] *= factor;
  }
  return result;
}

}  // namespace seam::synthesis
