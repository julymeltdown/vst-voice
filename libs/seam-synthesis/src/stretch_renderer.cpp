#include "seam/synthesis/stretch_renderer.hpp"

#include <algorithm>
#include <cmath>
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
  const auto markerValidation = unit.markers.validate(
      static_cast<time::SampleFrame>(source.frameCount()));
  if (!markerValidation) return core::Result<RenderedUnit>{markerValidation.error()};

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

  const auto preFrames = std::clamp<time::SampleFrame>(
      static_cast<time::SampleFrame>(std::llround(
          static_cast<double>(markers.vowelOnset - markers.audioOffset) /
          sourcePerOutput)), 0, outputFrames - 1);
  const auto releaseFrames = std::clamp<time::SampleFrame>(
      static_cast<time::SampleFrame>(std::llround(
          static_cast<double>(markers.audioEnd - releaseStart) / sourcePerOutput)),
      0, std::max<time::SampleFrame>(0, outputFrames - preFrames));
  const auto releaseOutputStart = std::max(preFrames, outputFrames - releaseFrames);

  RenderedUnit result{
      .unitId = unit.id,
      .samples = std::vector<float>(static_cast<std::size_t>(outputFrames), 0.0F),
      .vowelOnsetOffset = preFrames,
  };

  for (time::SampleFrame frame = 0; frame < preFrames; ++frame) {
    const auto sourcePosition = static_cast<double>(markers.audioOffset) +
                                static_cast<double>(frame) * sourcePerOutput;
    result.samples[static_cast<std::size_t>(frame)] = interpolate(mono, sourcePosition);
  }
  for (time::SampleFrame frame = releaseOutputStart; frame < outputFrames; ++frame) {
    const auto sourcePosition = static_cast<double>(releaseStart) +
        static_cast<double>(frame - releaseOutputStart) * sourcePerOutput;
    result.samples[static_cast<std::size_t>(frame)] = interpolate(
        mono, std::min(sourcePosition, static_cast<double>(markers.audioEnd - 1)));
  }

  const auto grainSize = parameters.grainSize;
  const auto half = grainSize / 2U;
  const auto hop = static_cast<time::SampleFrame>(parameters.hopSize);
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
    const auto sourceCenter = static_cast<double>(loopStart) +
                              std::fmod(drift, loopLength);
    const auto ratio = pitchRatio(targetMidi, unit.rootMidi,
                                   parameters.pitchCurve.centsAt(center));
    if (!std::isfinite(ratio) || ratio < 0.125 || ratio > 8.0) {
      return core::failure<RenderedUnit>(core::ErrorCode::Unsupported,
                                         "Stretch pitch ratio is unsupported",
                                         unit.id);
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
      overlap[destinationIndex] += loopInterpolate(
          mono, sourcePosition, static_cast<double>(loopStart),
          static_cast<double>(loopEnd)) * window;
      weights[destinationIndex] += window;
    }
  }

  for (time::SampleFrame frame = preFrames; frame < releaseOutputStart; ++frame) {
    const auto index = static_cast<std::size_t>(frame);
    if (weights[index] > 1.0e-6F) {
      result.samples[index] = overlap[index] / weights[index];
    } else {
      const auto sourcePosition = static_cast<double>(loopStart) +
          static_cast<double>(frame - preFrames) * sourcePerOutput;
      result.samples[index] = loopInterpolate(
          mono, sourcePosition, static_cast<double>(loopStart),
          static_cast<double>(loopEnd));
    }
  }
  finish(result, unit.gainDb + parameters.additionalGainDb, outputSampleRate);
  return result;
}

}  // namespace seam::synthesis
