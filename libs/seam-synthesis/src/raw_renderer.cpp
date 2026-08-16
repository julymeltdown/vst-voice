#include "seam/synthesis/raw_renderer.hpp"

#include <algorithm>
#include <cmath>

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
    RawRenderParameters parameters) const {
  if (source.sampleRate == 0 || source.channels == 0 || source.interleaved.empty() ||
      outputSampleRate < 8000 || outputSampleRate > 384000 || outputFrames <= 0 ||
      targetMidi < 0 || targetMidi > 127) {
    return core::failure<RenderedUnit>(core::ErrorCode::InvalidArgument,
                                       "Raw renderer input is invalid",
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

  const auto preFrames = static_cast<time::SampleFrame>(std::max(0.0,
      std::ceil((loopStart - offset) / sourceStep)));
  const auto releaseFrames = releaseStart < audioEnd
      ? static_cast<time::SampleFrame>(std::max(0.0,
            std::ceil((audioEnd - releaseStart) / sourceStep)))
      : 0;
  const auto vowelOnsetOffset = static_cast<time::SampleFrame>(std::llround(
      (static_cast<double>(unit.markers.vowelOnset) - offset) / sourceStep));

  RenderedUnit result;
  result.unitId = unit.id;
  result.vowelOnsetOffset = std::clamp<time::SampleFrame>(
      vowelOnsetOffset, 0, outputFrames - 1);
  result.samples.resize(static_cast<std::size_t>(outputFrames), 0.0F);
  const auto releaseOutputStart = std::max<time::SampleFrame>(
      preFrames, outputFrames - releaseFrames);
  const auto gain = static_cast<float>(std::pow(10.0,
      static_cast<double>(unit.gainDb + parameters.additionalGainDb) / 20.0));

  for (time::SampleFrame output = 0; output < outputFrames; ++output) {
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
  return result;
}

}  // namespace seam::synthesis
