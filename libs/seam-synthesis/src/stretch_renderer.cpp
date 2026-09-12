#include "seam/synthesis/stretch_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

namespace seam::synthesis {
namespace {

bool powerOfTwo(std::size_t value) noexcept {
  return value != 0U && (value & (value - 1U)) == 0U;
}

float interpolate(std::span<const float> samples, double position) noexcept {
  if (samples.empty()) return 0.0F;
  const auto clamped = std::clamp(position, 0.0,
                                  static_cast<double>(samples.size() - 1U));
  const auto left = static_cast<std::size_t>(std::floor(clamped));
  const auto right = std::min(left + 1U, samples.size() - 1U);
  const auto fraction = static_cast<float>(clamped - static_cast<double>(left));
  return samples[left] * (1.0F - fraction) + samples[right] * fraction;
}

float loopInterpolate(std::span<const float> samples,
                      double position,
                      double loopStart,
                      double loopEnd) noexcept {
  const auto length = loopEnd - loopStart;
  if (length <= 1.0) return interpolate(samples, position);
  auto relative = std::fmod(position - loopStart, length);
  if (relative < 0.0) relative += length;
  return interpolate(samples, loopStart + relative);
}

double pitchRatio(std::int32_t targetMidi,
                  std::int32_t rootMidi,
                  float cents) noexcept {
  return std::pow(2.0,
      (static_cast<double>(targetMidi - rootMidi) +
       static_cast<double>(cents) / 100.0) / 12.0);
}

void finish(RenderedUnit& result, float gainDb, std::uint32_t sampleRate) {
  const auto gain = static_cast<float>(std::pow(10.0,
      static_cast<double>(gainDb) / 20.0));
  long double mean = 0.0L;
  for (auto& sample : result.samples) {
    sample = std::isfinite(sample) ? sample * gain : 0.0F;
    mean += sample;
  }
  if (!result.samples.empty()) {
    mean /= static_cast<long double>(result.samples.size());
    for (auto& sample : result.samples) sample -= static_cast<float>(mean);
  }
  const auto fadeFrames = std::min<std::size_t>(
      result.samples.size() / 2U,
      std::max<std::size_t>(1U, static_cast<std::size_t>(sampleRate / 1000U)));
  for (std::size_t index = 0U; index < fadeFrames; ++index) {
    const auto factor = static_cast<float>(index + 1U) /
                        static_cast<float>(fadeFrames + 1U);
    result.samples[index] *= factor;
    result.samples[result.samples.size() - 1U - index] *= factor;
  }
}

}  // namespace

core::Result<RenderedUnit> StretchUnitRenderer::render(
    const voicebank::Unit& unit,
    const voicebank::AudioBuffer& source,
    std::uint32_t outputSampleRate,
    time::SampleFrame outputFrames,
    std::int32_t targetMidi,
    const StretchRenderParameters& parameters,
    std::stop_token stopToken) const {
  if (source.sampleRate == 0U || source.channels == 0U ||
      source.interleaved.empty() || outputSampleRate < 8000U ||
      outputSampleRate > 384000U || outputFrames <= 0 || targetMidi < 0 ||
      targetMidi > 127 || !powerOfTwo(parameters.grainSize) ||
      parameters.grainSize < 128U || parameters.grainSize > 8192U ||
      parameters.hopSize == 0U || parameters.hopSize > parameters.grainSize / 2U ||
      !std::isfinite(parameters.transientPreservation) ||
      parameters.transientPreservation < 0.0F ||
      parameters.transientPreservation > 1.0F ||
      !std::isfinite(parameters.sourceDrift) || parameters.sourceDrift < 0.0F ||
      parameters.sourceDrift > 1.0F ||
      !std::isfinite(parameters.additionalGainDb)) {
    return core::failure<RenderedUnit>(core::ErrorCode::InvalidArgument,
                                       "Stretch renderer input is invalid",
                                       unit.id);
  }
  const auto curveValidation = parameters.pitchCurve.validate();
  if (!curveValidation) return core::Result<RenderedUnit>{curveValidation.error()};
  if (parameters.performance && (parameters.performance->sampleRate() != outputSampleRate ||
      !parameters.pitchCurve.points().empty() ||
      parameters.performanceStartFrame > std::numeric_limits<time::SampleFrame>::max() - outputFrames)) {
    return core::failure<RenderedUnit>(core::ErrorCode::InvalidArgument,
        "Compiled stretch performance requires matching rate, bounded origin and no duplicate pitch curve", unit.id);
  }
  const auto markerValidation = unit.markers.validate(
      static_cast<time::SampleFrame>(source.frameCount()));
  if (!markerValidation) return core::Result<RenderedUnit>{markerValidation.error()};

  if (parameters.sourceMap) {
    if (parameters.sourceDrift != 0.25F) return core::failure<RenderedUnit>(core::ErrorCode::Unsupported,
        "Authored stretch timing cannot combine with a source-drift override", unit.id);
    const auto valid = parameters.sourceMap->validate(static_cast<time::SampleFrame>(source.frameCount()));
    if (!valid) return core::Result<RenderedUnit>{valid.error()};
    const auto& knots = parameters.sourceMap->knots;
    if (!parameters.performance || knots.front().sourceFrame != unit.markers.audioOffset ||
        knots.back().sourceFrame != unit.markers.audioEnd || knots.front().targetFrame != parameters.performanceStartFrame ||
        knots.back().targetFrame - knots.front().targetFrame != outputFrames) {
      return core::failure<RenderedUnit>(core::ErrorCode::Conflict, "Stretch source map does not match output extent", unit.id);
    }
  }
  const auto mono = source.monoMix();
  const auto sourcePerOutput = static_cast<double>(source.sampleRate) /
                               static_cast<double>(outputSampleRate);
  const auto& markers = unit.markers;
  const auto loopStart = markers.loopStart.value_or(markers.stableStart);
  const auto loopEnd = markers.loopEnd.value_or(
      markers.releaseStart.value_or(markers.audioEnd));
  const auto releaseStart = markers.releaseStart.value_or(markers.audioEnd);
  if (loopEnd - loopStart < static_cast<time::SampleFrame>(parameters.grainSize / 4U)) {
    return core::failure<RenderedUnit>(core::ErrorCode::Unsupported,
                                       "Stretch renderer sustain loop is too short",
                                       unit.id);
  }

  auto vowelOnsetFrames = std::clamp<time::SampleFrame>(
      static_cast<time::SampleFrame>(std::llround(
          static_cast<double>(markers.vowelOnset - markers.audioOffset) /
          sourcePerOutput)), 0, outputFrames - 1);
  // Granular processing is restricted to the stable vowel. The recorded onset
  // and vowel transition are copied verbatim to preserve the unit's character.
  auto preFrames = std::clamp<time::SampleFrame>(
      static_cast<time::SampleFrame>(std::llround(
          static_cast<double>(markers.stableStart - markers.audioOffset) /
          sourcePerOutput)), vowelOnsetFrames, outputFrames - 1);
  const auto releaseFrames = std::clamp<time::SampleFrame>(
      static_cast<time::SampleFrame>(std::llround(
          static_cast<double>(markers.audioEnd - releaseStart) / sourcePerOutput)),
      0, std::max<time::SampleFrame>(0, outputFrames - preFrames));
  auto releaseOutputStart = std::max(preFrames, outputFrames - releaseFrames);
  if (parameters.sourceMap) {
    const auto mapped = [&](time::SampleFrame frame) {
      return static_cast<time::SampleFrame>(std::llround(parameters.sourceMap->targetAt(static_cast<double>(frame)))) - parameters.performanceStartFrame;
    };
    vowelOnsetFrames = std::clamp<time::SampleFrame>(mapped(markers.vowelOnset), 0, outputFrames - 1);
    preFrames = std::clamp<time::SampleFrame>(mapped(markers.stableStart), vowelOnsetFrames, outputFrames);
    releaseOutputStart = std::clamp<time::SampleFrame>(mapped(releaseStart), preFrames, outputFrames);
  }

  const bool continuation = parameters.performance && unit.phones.size() == 1U &&
      !parameters.performance->at(parameters.performanceStartFrame + vowelOnsetFrames).reattack;
  if (continuation) preFrames = 0;
  RenderedUnit result{
      .unitId = unit.id,
      .samples = std::vector<float>(static_cast<std::size_t>(outputFrames), 0.0F),
      .vowelOnsetOffset = vowelOnsetFrames,
  };

  for (time::SampleFrame frame = 0; frame < preFrames; ++frame) {
    auto sourcePosition = static_cast<double>(markers.audioOffset) +
                                static_cast<double>(frame) * sourcePerOutput;
    if (parameters.sourceMap) sourcePosition = parameters.sourceMap->sourceAt(static_cast<double>(parameters.performanceStartFrame + frame));
    result.samples[static_cast<std::size_t>(frame)] = interpolate(mono, sourcePosition);
  }
  for (time::SampleFrame frame = releaseOutputStart; frame < outputFrames; ++frame) {
    auto sourcePosition = static_cast<double>(releaseStart) +
        static_cast<double>(frame - releaseOutputStart) * sourcePerOutput;
    if (parameters.sourceMap) sourcePosition = parameters.sourceMap->sourceAt(static_cast<double>(parameters.performanceStartFrame + frame));
    result.samples[static_cast<std::size_t>(frame)] = interpolate(
        mono, std::min(sourcePosition, static_cast<double>(markers.audioEnd - 1)));
  }

  const auto grainSize = parameters.grainSize;
  const auto half = grainSize / 2U;
  const auto hop = static_cast<time::SampleFrame>(parameters.hopSize);
  const auto sourceHz = 440.0 * std::exp2((static_cast<double>(unit.rootMidi) - 69.0) / 12.0);
  const auto sourcePeriod = static_cast<double>(source.sampleRate) / sourceHz;
  double targetPhaseSource = 0.0;
  const auto loopLength = static_cast<double>(loopEnd - loopStart);
  std::vector<float> overlap(result.samples.size(), 0.0F);
  std::vector<float> weights(result.samples.size(), 0.0F);
  std::size_t grainIndex = 0U;
  for (auto center = preFrames; center < releaseOutputStart; center += hop, ++grainIndex) {
    if ((grainIndex & 0x1fU) == 0U && stopToken.stop_requested()) {
      return core::failure<RenderedUnit>(core::ErrorCode::Conflict,
                                         "Stretch render was cancelled",
                                         unit.id);
    }
    const auto drift = static_cast<double>(center - preFrames) * sourcePerOutput *
                       static_cast<double>(parameters.sourceDrift);
    auto sourceCenter = static_cast<double>(loopStart) +
                              std::fmod(drift, loopLength);
    if (parameters.sourceMap) sourceCenter = parameters.sourceMap->sourceAt(static_cast<double>(parameters.performanceStartFrame + center));
    if (continuation && parameters.sourceMap) sourceCenter = std::max(sourceCenter, static_cast<double>(loopStart));
    auto ratio = pitchRatio(targetMidi, unit.rootMidi,
                                   parameters.pitchCurve.centsAt(center));
    if (parameters.performance) {
      const auto value = parameters.performance->at(parameters.performanceStartFrame + center);
      const bool unvoiced = parameters.sourceMap && parameters.sourceMap->voicedAtSource(sourceCenter) == false;
      if (value.noteId && !value.scoreFrequencyHz && !unvoiced) return core::failure<RenderedUnit>(core::ErrorCode::Unsupported,
          "Stretch rendering needs a voicing-aware path for accepted unvoiced pitch", unit.id);
      if (value.scoreFrequencyHz) ratio = *value.scoreFrequencyHz /
          (440.0 * std::exp2((static_cast<double>(unit.rootMidi) - 69.0) / 12.0));
    }
    if (!std::isfinite(ratio) || ratio < 0.125 || ratio > 8.0) {
      return core::failure<RenderedUnit>(core::ErrorCode::Unsupported,
                                         "Stretch pitch ratio is unsupported",
                                         unit.id);
    }
    if (parameters.performance) {
      const auto targetHz = ratio * sourceHz;
      if (targetHz <= 1.0 || targetHz >= static_cast<double>(outputSampleRate) * 0.45) {
        return core::failure<RenderedUnit>(core::ErrorCode::Unsupported, "Compiled stretch target pitch is unsupported", unit.id);
      }
      sourceCenter += std::remainder(targetPhaseSource - (sourceCenter - static_cast<double>(loopStart)), sourcePeriod);
      targetPhaseSource = std::fmod(targetPhaseSource + ratio * static_cast<double>(hop) * sourcePerOutput, sourcePeriod);
    }
    for (std::size_t index = 0U; index < grainSize; ++index) {
      const auto destination = center + static_cast<time::SampleFrame>(index) -
                               static_cast<time::SampleFrame>(half);
      if (destination < preFrames || destination >= releaseOutputStart ||
          destination < 0 || destination >= outputFrames) {
        continue;
      }
      const auto relative = static_cast<double>(index) - static_cast<double>(half);
      const auto sourcePosition = sourceCenter + relative * ratio * sourcePerOutput;
      if (parameters.sourceMap && (parameters.sourceMap->voicedAtSource(sourcePosition) == false ||
          parameters.sourceMap->voicedAtSource(parameters.sourceMap->sourceAt(
              static_cast<double>(parameters.performanceStartFrame + destination))) == false)) continue;
      const auto phase = 2.0 * std::numbers::pi * static_cast<double>(index) /
                         static_cast<double>(grainSize - 1U);
      auto window = static_cast<float>(0.5 - 0.5 * std::cos(phase));
      const auto edgeDistance = std::min(
          static_cast<double>(destination - preFrames),
          static_cast<double>(releaseOutputStart - destination - 1));
      const auto transitionFrames = std::max(1.0,
          static_cast<double>(grainSize) *
          static_cast<double>(1.0F - parameters.transientPreservation));
      if (edgeDistance < transitionFrames) {
        window *= static_cast<float>(std::clamp(edgeDistance / transitionFrames,
                                                0.0, 1.0));
      }
      const auto destinationIndex = static_cast<std::size_t>(destination);
      const auto sample = parameters.sourceMap ? interpolate(mono, std::clamp(sourcePosition,
          static_cast<double>(markers.audioOffset), static_cast<double>(markers.audioEnd - 1))) :
          loopInterpolate(mono, sourcePosition, static_cast<double>(loopStart), static_cast<double>(loopEnd));
      overlap[destinationIndex] += sample * window;
      weights[destinationIndex] += window;
    }
  }

  for (time::SampleFrame frame = preFrames; frame < releaseOutputStart; ++frame) {
    const auto index = static_cast<std::size_t>(frame);
    if (weights[index] > 1.0e-6F) {
      result.samples[index] = overlap[index] / weights[index];
    } else {
      if (parameters.sourceMap) {
        result.samples[index] = interpolate(mono, std::min(static_cast<double>(markers.audioEnd - 1),
            parameters.sourceMap->sourceAt(static_cast<double>(parameters.performanceStartFrame + frame))));
        continue;
      }
      const auto sourcePosition = static_cast<double>(loopStart) +
          static_cast<double>(frame - preFrames) * sourcePerOutput;
      result.samples[index] = loopInterpolate(
          mono, sourcePosition, static_cast<double>(loopStart),
          static_cast<double>(loopEnd));
    }
  }
  finish(result, unit.gainDb + parameters.additionalGainDb, outputSampleRate);
  if (parameters.performance) {
    const auto applied = applyCompiledPerformanceGain(result.samples, *parameters.performance, parameters.performanceStartFrame, stopToken);
    if (!applied) return core::Result<RenderedUnit>{applied.error()};
  }
  return result;
}

}  // namespace seam::synthesis
