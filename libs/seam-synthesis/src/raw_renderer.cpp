#include "seam/synthesis/raw_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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

float loopSample(std::span<const float> samples,
                 double position,
                 double loopStart,
                 double loopEnd,
                 float loopPrint) noexcept {
  const auto length = loopEnd - loopStart;
  if (length <= 1.0) return interpolate(samples, position);
  auto relative = std::fmod(position - loopStart, length);
  if (relative < 0.0) relative += length;
  const auto wrapped = loopStart + relative;
  const auto hard = interpolate(samples, wrapped);
  const auto smoothAmount = std::clamp(1.0F - loopPrint, 0.0F, 1.0F);
  if (smoothAmount <= 0.0F) return hard;

  const auto crossfadeSourceFrames = std::min(32.0, length * 0.15);
  if (relative >= crossfadeSourceFrames) return hard;
  const auto tailPosition = loopEnd - crossfadeSourceFrames + relative;
  const auto tail = interpolate(samples, tailPosition);
  const auto mix = static_cast<float>(relative / crossfadeSourceFrames);
  const auto smooth = tail * (1.0F - mix) + hard * mix;
  return hard * (1.0F - smoothAmount) + smooth * smoothAmount;
}

}  // namespace

core::Result<RenderedUnit> RawLoopRenderer::render(
    const voicebank::Unit& unit,
    const voicebank::AudioBuffer& source,
    std::uint32_t outputSampleRate,
    time::SampleFrame outputFrames,
    std::int32_t targetMidi,
    RawRenderParameters parameters, std::stop_token stopToken) const {
  if (stopToken.stop_requested()) return core::failure<RenderedUnit>(core::ErrorCode::Conflict, "Raw render was cancelled", unit.id);
  if (source.sampleRate == 0 || source.channels == 0 || source.interleaved.empty() ||
      outputSampleRate < 8000 || outputSampleRate > 384000 || outputFrames <= 0 ||
      targetMidi < 0 || targetMidi > 127) {
    return core::failure<RenderedUnit>(core::ErrorCode::InvalidArgument,
                                       "Raw renderer input is invalid",
                                       unit.id);
  }
  const auto pitchCurveValidation = parameters.pitchCurve.validate();
  if (!pitchCurveValidation) {
    return core::Result<RenderedUnit>{pitchCurveValidation.error()};
  }
  if (parameters.performance && !parameters.pitchCurve.points().empty()) {
    return core::failure<RenderedUnit>(
        core::ErrorCode::InvalidArgument,
        "Compiled raw performance cannot be combined with a duplicate pitch curve",
        unit.id);
  }
  const auto markerValidation = unit.markers.validate(
      static_cast<time::SampleFrame>(source.frameCount()));
  if (!markerValidation) return core::Result<RenderedUnit>{markerValidation.error()};

  const auto mono = source.monoMix();
  const auto pitchRatio = std::pow(2.0,
      static_cast<double>(targetMidi - unit.rootMidi) / 12.0);
  const auto sourceStep = pitchRatio * static_cast<double>(source.sampleRate) /
                          static_cast<double>(outputSampleRate);
  if (!std::isfinite(sourceStep) || sourceStep <= 0.0) {
    return core::failure<RenderedUnit>(core::ErrorCode::Internal,
                                       "Raw renderer produced an invalid source step",
                                       unit.id);
  }

  const auto offset = static_cast<double>(unit.markers.audioOffset);
  const auto loopStart = static_cast<double>(unit.markers.loopStart.value_or(
      unit.markers.stableStart));
  const auto loopEnd = static_cast<double>(unit.markers.loopEnd.value_or(
      unit.markers.releaseStart.value_or(unit.markers.audioEnd)));
  const auto releaseStart = static_cast<double>(unit.markers.releaseStart.value_or(
      unit.markers.audioEnd));
  const auto audioEnd = static_cast<double>(unit.markers.audioEnd);

  auto preFrames = static_cast<time::SampleFrame>(std::max(0.0,
      std::ceil((loopStart - offset) / sourceStep)));
  auto releaseFrames = releaseStart < audioEnd
      ? static_cast<time::SampleFrame>(std::max(0.0,
            std::ceil((audioEnd - releaseStart) / sourceStep)))
      : 0;
  auto vowelOnsetOffset = static_cast<time::SampleFrame>(std::llround(
      (static_cast<double>(unit.markers.vowelOnset) - offset) / sourceStep));

  if (parameters.sourceMap) {
    const auto valid = parameters.sourceMap->validate(static_cast<time::SampleFrame>(source.frameCount()));
    if (!valid) return core::Result<RenderedUnit>{valid.error()};
    const auto& knots = parameters.sourceMap->knots;
    if (knots.front().sourceFrame != unit.markers.audioOffset || knots.back().sourceFrame != unit.markers.audioEnd ||
        knots.front().targetFrame != parameters.performanceStartFrame ||
        knots.back().targetFrame - knots.front().targetFrame != outputFrames)
      return core::failure<RenderedUnit>(core::ErrorCode::Conflict, "Raw source map does not match output extent", unit.id);
    const auto mapped = [&](double frame) {
      return static_cast<time::SampleFrame>(std::llround(parameters.sourceMap->targetAt(frame))) - parameters.performanceStartFrame;
    };
    preFrames = mapped(loopStart);
    releaseFrames = outputFrames - mapped(releaseStart);
    vowelOnsetOffset = mapped(static_cast<double>(unit.markers.vowelOnset));
    if (vowelOnsetOffset < 0 || preFrames < vowelOnsetOffset || releaseFrames < 0 ||
        preFrames >= outputFrames - releaseFrames || vowelOnsetOffset >= outputFrames)
      return core::failure<RenderedUnit>(core::ErrorCode::Conflict, "Raw source map leaves no sustain between transition and release", unit.id);
    if (parameters.performanceVowelFrame && *parameters.performanceVowelFrame != parameters.performanceStartFrame + vowelOnsetOffset)
      return core::failure<RenderedUnit>(core::ErrorCode::Conflict, "Raw mapped vowel conflicts with performance origin", unit.id);
  }

  const auto renderedVowelOffset = std::clamp<time::SampleFrame>(vowelOnsetOffset, 0, outputFrames - 1);
  auto performanceOrigin = parameters.performanceStartFrame;
  if (parameters.performanceVowelFrame) {
    if (*parameters.performanceVowelFrame < std::numeric_limits<time::SampleFrame>::min() + renderedVowelOffset) {
      return core::failure<RenderedUnit>(core::ErrorCode::InvalidArgument, "Raw performance origin underflows", unit.id);
    }
    performanceOrigin = *parameters.performanceVowelFrame - renderedVowelOffset;
  }
  if (parameters.performance && (parameters.performance->sampleRate() != outputSampleRate ||
      outputFrames > 32LL * 1024LL * 1024LL ||
      performanceOrigin > std::numeric_limits<time::SampleFrame>::max() - outputFrames)) {
    return core::failure<RenderedUnit>(core::ErrorCode::InvalidArgument, "Compiled raw performance exceeds supported bounds", unit.id);
  }
  if (parameters.performance && unit.phones.size() == 1U &&
      !parameters.performance->at(performanceOrigin + renderedVowelOffset).reattack) preFrames = 0;
  RenderedUnit result;
  result.unitId = unit.id;
  result.vowelOnsetOffset = std::clamp<time::SampleFrame>(
      vowelOnsetOffset, 0, outputFrames - 1);
  result.samples.resize(static_cast<std::size_t>(outputFrames), 0.0F);
  const auto releaseOutputStart = std::max<time::SampleFrame>(
      preFrames, outputFrames - releaseFrames);
  const auto gain = static_cast<float>(std::pow(10.0,
      static_cast<double>(unit.gainDb + parameters.additionalGainDb) / 20.0));

  double performancePosition = offset;
  const bool hasTrajectory = parameters.performance != nullptr ||
                             !parameters.pitchCurve.points().empty();
  const auto rootHz = 440.0 * std::exp2((static_cast<double>(unit.rootMidi) - 69.0) / 12.0);
  for (time::SampleFrame output = 0; output < outputFrames; ++output) {
    if ((output & 4095) == 0 && stopToken.stop_requested()) return core::failure<RenderedUnit>(core::ErrorCode::Conflict, "Raw render was cancelled", unit.id);
    double sourcePosition = offset;
    if (output < preFrames) {
      sourcePosition = offset + static_cast<double>(output) * sourceStep;
    } else if (releaseFrames > 0 && output >= releaseOutputStart) {
      sourcePosition = releaseStart +
          static_cast<double>(output - releaseOutputStart) * sourceStep;
    } else {
      sourcePosition = loopStart +
          static_cast<double>(output - preFrames) * sourceStep;
    }
    if (hasTrajectory && output >= preFrames &&
        !(releaseFrames > 0 && output >= releaseOutputStart)) {
      if (output == preFrames) performancePosition = loopStart;
      sourcePosition = performancePosition;
      auto step = sourceStep;
      if (parameters.performance) {
        const auto value = parameters.performance->at(performanceOrigin + output);
        if (value.noteId && !value.scoreFrequencyHz) return core::failure<RenderedUnit>(core::ErrorCode::Unsupported,
            "Raw rendering needs source voicing for accepted unvoiced pitch", unit.id);
        if (value.scoreFrequencyHz) {
          if (*value.scoreFrequencyHz >= outputSampleRate * 0.45 || *value.scoreFrequencyHz <= 1.0) return core::failure<RenderedUnit>(
              core::ErrorCode::Unsupported, "Compiled raw target pitch is unsupported", unit.id);
          step = static_cast<double>(source.sampleRate) / outputSampleRate * *value.scoreFrequencyHz / rootHz;
        }
      } else {
        const auto cents = static_cast<double>(parameters.pitchCurve.centsAt(output));
        step *= std::exp2(cents / 1200.0);
        if (!std::isfinite(step) || step <= 0.0 ||
            step > static_cast<double>(source.frameCount())) {
          return core::failure<RenderedUnit>(core::ErrorCode::Unsupported,
              "Raw pitch trajectory is outside supported bounds", unit.id);
        }
      }
      performancePosition += step;
    }
    // Time-map only the transient sections. Sustain retains the normal Raw
    // pitch-ratio loop (or compiled pitch stepping), never map-rate pitch.
    if (parameters.sourceMap && (output < preFrames || output >= releaseOutputStart))
      sourcePosition = parameters.sourceMap->sourceAt(static_cast<double>(parameters.performanceStartFrame + output));
    float sample = 0.0F;
    if (output >= preFrames &&
        !(releaseFrames > 0 && output >= releaseOutputStart)) {
      sample = loopSample(mono, sourcePosition, loopStart, loopEnd,
                          parameters.loopPrint);
    } else {
      sample = interpolate(mono, std::min(sourcePosition, audioEnd - 1.0));
    }
    result.samples[static_cast<std::size_t>(output)] = sample * gain;
  }

  double mean = 0.0;
  for (const auto sample : result.samples) mean += sample;
  mean /= static_cast<double>(result.samples.size());
  for (auto& sample : result.samples) {
    sample -= static_cast<float>(mean);
    if (!std::isfinite(sample)) sample = 0.0F;
  }

  const auto fadeFrames = std::min<std::size_t>(
      result.samples.size() / 2U,
      std::max<std::size_t>(1, static_cast<std::size_t>(outputSampleRate / 1000U)));
  for (std::size_t index = 0; index < fadeFrames; ++index) {
    const auto factor = static_cast<float>(index + 1U) /
                        static_cast<float>(fadeFrames + 1U);
    result.samples[index] *= factor;
    result.samples[result.samples.size() - 1U - index] *= factor;
  }
  if (parameters.performance) {
    const auto applied = applyCompiledPerformanceGain(result.samples, *parameters.performance, performanceOrigin, stopToken);
    if (!applied) return core::Result<RenderedUnit>{applied.error()};
  }
  return result;
}

core::Result<void> retimeRenderedOnset(RenderedUnit& unit, time::SampleFrame targetVowelOffset) {
  if (unit.samples.size() < 3U || unit.samples.size() > 32U * 1024U * 1024U ||
      unit.vowelOnsetOffset <= 0 || targetVowelOffset <= 0 ||
      unit.vowelOnsetOffset >= static_cast<time::SampleFrame>(unit.samples.size() - 1U) ||
      targetVowelOffset >= static_cast<time::SampleFrame>(unit.samples.size() - 1U)) {
    return core::failure(core::ErrorCode::Conflict, "Onset retiming requires interior source and target vowel landmarks", unit.unitId);
  }
  if (unit.vowelOnsetOffset == targetVowelOffset) return core::success();
  std::vector<float> samples(unit.samples.size());
  const auto sourceAnchor = static_cast<double>(unit.vowelOnsetOffset);
  const auto targetAnchor = static_cast<double>(targetVowelOffset);
  const auto last = static_cast<double>(samples.size() - 1U);
  for (std::size_t i = 0U; i < samples.size(); ++i) {
    const auto frame = static_cast<double>(i);
    const auto position = frame < targetAnchor ? frame * sourceAnchor / targetAnchor
        : sourceAnchor + (frame - targetAnchor) * (last - sourceAnchor) / (last - targetAnchor);
    samples[i] = interpolate(unit.samples, position);
  }
  unit.samples.swap(samples);
  unit.vowelOnsetOffset = targetVowelOffset;
  return core::success();
}

}  // namespace seam::synthesis
