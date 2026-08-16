#include "seam/synthesis/spectral_classic.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

namespace seam::synthesis {
namespace {

bool powerOfTwo(std::size_t value) noexcept {
  return value != 0U && (value & (value - 1U)) == 0U;
}

void fft(std::vector<std::complex<double>>& values, bool inverse) {
  const auto size = values.size();
  for (std::size_t index = 1U, reversed = 0U; index < size; ++index) {
    std::size_t bit = size >> 1U;
    for (; (reversed & bit) != 0U; bit >>= 1U) reversed ^= bit;
    reversed ^= bit;
    if (index < reversed) std::swap(values[index], values[reversed]);
  }
  for (std::size_t length = 2U; length <= size; length <<= 1U) {
    const auto direction = inverse ? 1.0 : -1.0;
    const auto angle = direction * 2.0 * std::numbers::pi /
                       static_cast<double>(length);
    const std::complex<double> step{std::cos(angle), std::sin(angle)};
    for (std::size_t start = 0U; start < size; start += length) {
      std::complex<double> phase{1.0, 0.0};
      const auto half = length / 2U;
      for (std::size_t offset = 0U; offset < half; ++offset) {
        const auto even = values[start + offset];
        const auto odd = values[start + offset + half] * phase;
        values[start + offset] = even + odd;
        values[start + offset + half] = even - odd;
        phase *= step;
      }
    }
  }
  if (inverse) {
    const auto scale = 1.0 / static_cast<double>(size);
    for (auto& value : values) value *= scale;
  }
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

double midiRatio(std::int32_t targetMidi,
                 std::int32_t rootMidi,
                 float cents) noexcept {
  return std::pow(2.0,
      (static_cast<double>(targetMidi - rootMidi) +
       static_cast<double>(cents) / 100.0) / 12.0);
}

double wrappedPhaseDifference(double target, double source) noexcept {
  auto difference = target - source;
  while (difference > std::numbers::pi) difference -= 2.0 * std::numbers::pi;
  while (difference < -std::numbers::pi) difference += 2.0 * std::numbers::pi;
  return difference;
}

double blendPhase(double continuous, double source, double reset) noexcept {
  return continuous + wrappedPhaseDifference(source, continuous) * reset;
}

void finishUnit(RenderedUnit& result,
                float gainDb,
                std::uint32_t sampleRate) {
  const auto gain = static_cast<float>(std::pow(10.0,
      static_cast<double>(gainDb) / 20.0));
  long double mean = 0.0L;
  for (auto& sample : result.samples) {
    sample = std::isfinite(sample) ? sample * gain : 0.0F;
    mean += sample;
  }
  if (!result.samples.empty()) {
    mean /= static_cast<long double>(result.samples.size());
    for (auto& sample : result.samples) {
      sample -= static_cast<float>(mean);
    }
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

core::Result<RenderedUnit> SpectralClassicRenderer::render(
    const voicebank::Unit& unit,
    const voicebank::AudioBuffer& source,
    std::uint32_t outputSampleRate,
    time::SampleFrame outputFrames,
    std::int32_t targetMidi,
    const SpectralRenderParameters& parameters,
    std::stop_token stopToken) const {
  if (source.sampleRate == 0U || source.channels == 0U ||
      source.interleaved.empty() || outputSampleRate < 8000U ||
      outputSampleRate > 384000U || outputFrames <= 0 || targetMidi < 0 ||
      targetMidi > 127 || !powerOfTwo(parameters.fftSize) ||
      parameters.fftSize < 128U || parameters.fftSize > 8192U ||
      parameters.hopSize == 0U || parameters.hopSize > parameters.fftSize / 2U ||
      !std::isfinite(parameters.formantFollow) ||
      parameters.formantFollow < 0.0F || parameters.formantFollow > 1.0F ||
      !std::isfinite(parameters.phaseReset) || parameters.phaseReset < 0.0F ||
      parameters.phaseReset > 1.0F ||
      !std::isfinite(parameters.additionalGainDb)) {
    return core::failure<RenderedUnit>(core::ErrorCode::InvalidArgument,
                                       "SpectralClassic input is invalid",
                                       unit.id);
  }
  const auto curveValidation = parameters.pitchCurve.validate();
  if (!curveValidation) return core::Result<RenderedUnit>{curveValidation.error()};
  const auto markerValidation = unit.markers.validate(
      static_cast<time::SampleFrame>(source.frameCount()));
  if (!markerValidation) return core::Result<RenderedUnit>{markerValidation.error()};
  if (stopToken.stop_requested()) {
    return core::failure<RenderedUnit>(core::ErrorCode::Conflict,
                                       "SpectralClassic render was cancelled",
                                       unit.id);
  }

  const auto mono = source.monoMix();
  const auto sourcePerOutput = static_cast<double>(source.sampleRate) /
                               static_cast<double>(outputSampleRate);
  const auto& markers = unit.markers;
  const auto offset = markers.audioOffset;
  const auto audioEnd = markers.audioEnd;
  const auto stableStart = markers.stableStart;
  const auto loopStart = markers.loopStart.value_or(stableStart);
  const auto loopEnd = markers.loopEnd.value_or(
      markers.releaseStart.value_or(audioEnd));
  const auto releaseStart = markers.releaseStart.value_or(audioEnd);
  if (loopEnd - loopStart < static_cast<time::SampleFrame>(parameters.fftSize / 4U)) {
    return core::failure<RenderedUnit>(core::ErrorCode::Unsupported,
                                       "SpectralClassic sustain loop is too short",
                                       unit.id);
  }

  const auto preFrames = std::clamp<time::SampleFrame>(
      static_cast<time::SampleFrame>(std::llround(
          static_cast<double>(markers.vowelOnset - offset) / sourcePerOutput)),
      0, outputFrames - 1);
  const auto sourceReleaseFrames = std::max<time::SampleFrame>(0, audioEnd - releaseStart);
  const auto releaseFrames = std::clamp<time::SampleFrame>(
      static_cast<time::SampleFrame>(std::llround(
          static_cast<double>(sourceReleaseFrames) / sourcePerOutput)),
      0, std::max<time::SampleFrame>(0, outputFrames - preFrames));
  const auto releaseOutputStart = std::max(preFrames, outputFrames - releaseFrames);

  RenderedUnit result{
      .unitId = unit.id,
      .samples = std::vector<float>(static_cast<std::size_t>(outputFrames), 0.0F),
      .vowelOnsetOffset = preFrames,
  };

  // Preserve consonant/transition and release directly from the source. Only the
  // stable vowel is spectrally transformed, retaining the unit's physical edges.
  for (time::SampleFrame frame = 0; frame < preFrames; ++frame) {
    const auto sourcePosition = static_cast<double>(offset) +
                                static_cast<double>(frame) * sourcePerOutput;
    result.samples[static_cast<std::size_t>(frame)] = interpolate(mono, sourcePosition);
  }
  for (time::SampleFrame frame = releaseOutputStart; frame < outputFrames; ++frame) {
    const auto sourcePosition = static_cast<double>(releaseStart) +
        static_cast<double>(frame - releaseOutputStart) * sourcePerOutput;
    result.samples[static_cast<std::size_t>(frame)] =
        interpolate(mono, std::min(sourcePosition, static_cast<double>(audioEnd - 1)));
  }

  const auto fftSize = parameters.fftSize;
  const auto half = fftSize / 2U;
  const auto hop = static_cast<time::SampleFrame>(parameters.hopSize);
  const auto stableFrames = std::max<time::SampleFrame>(1, releaseOutputStart - preFrames);
  const auto loopLength = static_cast<double>(loopEnd - loopStart);
  std::vector<std::complex<double>> spectrum(fftSize);
  std::vector<std::complex<double>> shifted(fftSize);
  std::vector<double> outputPhase(half + 1U, 0.0);
  std::vector<float> overlap(result.samples.size(), 0.0F);
  std::vector<float> weights(result.samples.size(), 0.0F);
  bool firstFrame = true;
  std::size_t frameIndex = 0U;

  const auto firstCenter = preFrames;
  for (auto center = firstCenter; center < releaseOutputStart; center += hop, ++frameIndex) {
    if ((frameIndex & 0x0fU) == 0U && stopToken.stop_requested()) {
      return core::failure<RenderedUnit>(core::ErrorCode::Conflict,
                                         "SpectralClassic render was cancelled",
                                         unit.id);
    }
    const auto progress = static_cast<double>(center - preFrames) /
                          static_cast<double>(stableFrames);
    const auto sourceCenter = static_cast<double>(loopStart) +
        std::fmod(progress * loopLength, loopLength);
    for (std::size_t index = 0U; index < fftSize; ++index) {
      const auto relative = static_cast<double>(index) - static_cast<double>(half);
      const auto sourcePosition = sourceCenter + relative * sourcePerOutput;
      const auto phase = 2.0 * std::numbers::pi * static_cast<double>(index) /
                         static_cast<double>(fftSize - 1U);
      const auto window = 0.5 - 0.5 * std::cos(phase);
      spectrum[index] = std::complex<double>{
          static_cast<double>(loopInterpolate(mono, sourcePosition,
                                              static_cast<double>(loopStart),
                                              static_cast<double>(loopEnd))) * window,
          0.0};
      shifted[index] = {0.0, 0.0};
    }
    fft(spectrum, false);

    // Estimate a deliberately coarse spectral envelope.  Pitch shifting must
    // move harmonic energy first; formant preservation is applied as a
    // multiplicative envelope correction rather than mixing the unshifted
    // spectrum back in.  Mixing unshifted magnitudes would leave a second
    // fundamental at the source pitch, which is especially obvious on the
    // single-harmonic fixtures used by the regression suite.
    std::vector<double> magnitudes(half + 1U, 0.0);
    std::vector<double> envelope(half + 1U, 0.0);
    for (std::size_t bin = 0U; bin <= half; ++bin) {
      magnitudes[bin] = std::abs(spectrum[bin]);
    }
    constexpr std::size_t kEnvelopeRadius = 12U;
    for (std::size_t bin = 0U; bin <= half; ++bin) {
      const auto first = bin > kEnvelopeRadius ? bin - kEnvelopeRadius : 0U;
      const auto last = std::min(half, bin + kEnvelopeRadius);
      double sum = 0.0;
      for (auto neighbor = first; neighbor <= last; ++neighbor) {
        sum += magnitudes[neighbor];
      }
      envelope[bin] = sum / static_cast<double>(last - first + 1U);
    }

    const auto cents = parameters.pitchCurve.centsAt(center);
    const auto ratio = midiRatio(targetMidi, unit.rootMidi, cents);
    if (!std::isfinite(ratio) || ratio < 0.125 || ratio > 8.0) {
      return core::failure<RenderedUnit>(core::ErrorCode::Unsupported,
                                         "SpectralClassic pitch ratio is unsupported",
                                         unit.id);
    }
    for (std::size_t bin = 0U; bin <= half; ++bin) {
      const auto sourceBin = static_cast<double>(bin) / ratio;
      if (sourceBin > static_cast<double>(half)) continue;
      const auto left = static_cast<std::size_t>(std::floor(sourceBin));
      const auto right = std::min(left + 1U, half);
      const auto fraction = sourceBin - static_cast<double>(left);
      const auto shiftedValue = spectrum[left] * (1.0 - fraction) +
                                spectrum[right] * fraction;
      const auto shiftedMagnitude = std::abs(shiftedValue);
      const auto interpolateEnvelope = [&](double position) {
        const auto bounded = std::clamp(position, 0.0, static_cast<double>(half));
        const auto envelopeLeft = static_cast<std::size_t>(std::floor(bounded));
        const auto envelopeRight = std::min(envelopeLeft + 1U, half);
        const auto envelopeFraction = bounded - static_cast<double>(envelopeLeft);
        return envelope[envelopeLeft] * (1.0 - envelopeFraction) +
               envelope[envelopeRight] * envelopeFraction;
      };
      const auto sourceEnvelope = interpolateEnvelope(sourceBin);
      const auto targetEnvelope = envelope[bin];
      const auto formantPreserved = shiftedMagnitude *
          std::clamp(targetEnvelope / std::max(sourceEnvelope, 1.0e-9),
                     0.5, 2.0);
      const auto magnitude = shiftedMagnitude *
          static_cast<double>(1.0F - parameters.formantFollow) +
          formantPreserved * static_cast<double>(parameters.formantFollow);
      const auto sourcePhase = std::arg(shiftedValue);
      if (firstFrame) {
        outputPhase[bin] = sourcePhase;
      } else {
        const auto expectedAdvance = 2.0 * std::numbers::pi *
            static_cast<double>(bin) * static_cast<double>(parameters.hopSize) /
            static_cast<double>(fftSize);
        const auto continuous = outputPhase[bin] + expectedAdvance;
        outputPhase[bin] = blendPhase(
            continuous, sourcePhase, static_cast<double>(parameters.phaseReset));
      }
      shifted[bin] = std::polar(magnitude, outputPhase[bin]);
      if (bin != 0U && bin != half) shifted[fftSize - bin] = std::conj(shifted[bin]);
    }
    firstFrame = false;
    fft(shifted, true);

    for (std::size_t index = 0U; index < fftSize; ++index) {
      const auto destination = center + static_cast<time::SampleFrame>(index) -
                               static_cast<time::SampleFrame>(half);
      if (destination < preFrames || destination >= releaseOutputStart ||
          destination < 0 || destination >= outputFrames) {
        continue;
      }
      const auto phase = 2.0 * std::numbers::pi * static_cast<double>(index) /
                         static_cast<double>(fftSize - 1U);
      const auto window = static_cast<float>(0.5 - 0.5 * std::cos(phase));
      const auto destinationIndex = static_cast<std::size_t>(destination);
      overlap[destinationIndex] += static_cast<float>(shifted[index].real()) * window;
      weights[destinationIndex] += window * window;
    }
  }

  for (time::SampleFrame frame = preFrames; frame < releaseOutputStart; ++frame) {
    const auto index = static_cast<std::size_t>(frame);
    if (weights[index] > 1.0e-7F) {
      result.samples[index] = overlap[index] / weights[index];
    } else {
      const auto sourcePosition = static_cast<double>(loopStart) +
          static_cast<double>(frame - preFrames) * sourcePerOutput;
      result.samples[index] = loopInterpolate(
          mono, sourcePosition, static_cast<double>(loopStart),
          static_cast<double>(loopEnd));
    }
  }

  finishUnit(result, unit.gainDb + parameters.additionalGainDb, outputSampleRate);
  return result;
}

}  // namespace seam::synthesis
