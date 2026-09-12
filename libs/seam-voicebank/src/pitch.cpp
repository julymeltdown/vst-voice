#include "seam/voicebank/pitch.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace seam::voicebank {
namespace {
// Same radix-2 decomposition used by the spectrogram, with inverse scaling and cancellation.
bool pitchFft(std::vector<std::complex<double>>& values, bool inverse, std::stop_token stop) {
  const auto size = values.size();
  for (std::size_t index = 1U, reversed = 0U; index < size; ++index) {
    if (index % 1024U == 0U && stop.stop_requested()) return false;
    std::size_t bit = size >> 1U;
    for (; (reversed & bit) != 0U; bit >>= 1U) reversed ^= bit;
    reversed ^= bit;
    if (index < reversed) std::swap(values[index], values[reversed]);
  }
  for (std::size_t length = 2U; length <= size; length <<= 1U) {
    if (stop.stop_requested()) return false;
    const auto angle = (inverse ? 2.0 : -2.0) * std::numbers::pi / static_cast<double>(length);
    const std::complex<double> step{std::cos(angle), std::sin(angle)};
    for (std::size_t start = 0U; start < size; start += length) {
      std::complex<double> phase{1.0, 0.0};
      for (std::size_t offset = 0U; offset < length / 2U; ++offset) {
        const auto even = values[start + offset];
        const auto odd = values[start + offset + length / 2U] * phase;
        values[start + offset] = even + odd;
        values[start + offset + length / 2U] = even - odd;
        phase *= step;
      }
    }
  }
  if (inverse) for (auto& value : values) value /= static_cast<double>(size);
  return !stop.stop_requested();
}
}

core::Result<std::vector<PitchFrame>> analyzePitch(std::span<const float> samples,
                                                   std::uint32_t sampleRate,
                                                   PitchConfig config, std::stop_token stopToken,
                                                   PitchAnalysisLimits limits) {
  const auto cancelled = [] { return core::failure<std::vector<PitchFrame>>(core::ErrorCode::Conflict, "Pitch analysis cancelled"); };
  if (stopToken.stop_requested()) return cancelled();
  if (samples.empty() || sampleRate < 8000 || sampleRate > 384000 ||
      config.frameSize < 128 || config.frameSize > 65536U || config.hopSize == 0 ||
      !std::isfinite(config.minimumHz) || !std::isfinite(config.maximumHz) || !std::isfinite(config.voicingThreshold) ||
      (config.correlationMethod != PitchCorrelationMethod::Direct && config.correlationMethod != PitchCorrelationMethod::Fft) ||
      config.minimumHz <= 0.0 || config.maximumHz <= config.minimumHz ||
      config.maximumHz >= static_cast<double>(sampleRate) / 2.0 ||
      config.voicingThreshold <= 0.0 || config.voicingThreshold >= 1.0) {
    return core::failure<std::vector<PitchFrame>>(
        core::ErrorCode::InvalidArgument, "Pitch analysis configuration is invalid");
  }
  const auto minimumLagValue = std::floor(static_cast<double>(sampleRate) / config.maximumHz);
  if (!std::isfinite(minimumLagValue) || minimumLagValue >= static_cast<double>(config.frameSize / 2U)) {
    return core::failure<std::vector<PitchFrame>>(core::ErrorCode::InvalidArgument, "Pitch lag range is outside the analysis window");
  }
  const auto minimumLag = std::max<std::size_t>(1, static_cast<std::size_t>(minimumLagValue));
  const auto maximumLag = std::min<std::size_t>(config.frameSize / 2U,
      static_cast<std::size_t>(std::min(static_cast<double>(config.frameSize / 2U),
          std::ceil(static_cast<double>(sampleRate) / config.minimumHz))));
  if (minimumLag >= maximumLag) {
    return core::failure<std::vector<PitchFrame>>(
        core::ErrorCode::InvalidArgument, "Pitch lag range is empty");
  }

  const auto frameCount = samples.size() <= config.frameSize
                              ? 1U
                              : 1U + (samples.size() - config.frameSize) / config.hopSize;
  // Exact number of inner correlation terms in a non-silent frame; charge all
  // frames before allocation so admission does not depend on signal contents.
  const auto lags = static_cast<std::uint64_t>(maximumLag - minimumLag + 1U);
  const auto termsPerFrame = lags * static_cast<std::uint64_t>(config.frameSize) -
      lags * static_cast<std::uint64_t>(minimumLag + maximumLag) / 2U;
  std::size_t transformSize = 1U, transformStages = 0U;
  while (transformSize < 2U * config.frameSize) { transformSize *= 2U; ++transformStages; }
  const auto butterfliesPerFrame = static_cast<std::uint64_t>(transformSize) * transformStages; // Forward plus inverse.
  const bool exceedsWork = config.correlationMethod == PitchCorrelationMethod::Direct
      ? frameCount > limits.maximumCorrelationTerms / termsPerFrame
      : frameCount > limits.maximumTransformButterflies / butterfliesPerFrame;
  if (frameCount > limits.maximumFrames || exceedsWork) {
    return core::failure<std::vector<PitchFrame>>(core::ErrorCode::InvalidArgument, "Pitch analysis exceeds its requested work budget");
  }
  std::vector<PitchFrame> result;
  result.reserve(frameCount);
  std::vector<double> frame(config.frameSize, 0.0);
  std::vector<double> correlations(maximumLag + 1U, 0.0);
  std::vector<std::complex<double>> spectrum(config.correlationMethod == PitchCorrelationMethod::Fft ? transformSize : 0U);
  std::vector<double> energyPrefix(config.correlationMethod == PitchCorrelationMethod::Fft ? config.frameSize + 1U : 0U);

  for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    if (stopToken.stop_requested()) return cancelled();
    const auto start = frameIndex * config.hopSize;
    double mean = 0.0;
    for (std::size_t index = 0; index < config.frameSize; ++index) {
      const auto sourceIndex = start + index;
      const auto value = sourceIndex < samples.size() && std::isfinite(samples[sourceIndex])
                             ? static_cast<double>(samples[sourceIndex])
                             : 0.0;
      frame[index] = value;
      mean += value;
    }
    mean /= static_cast<double>(config.frameSize);
    double energy = 0.0;
    for (auto& value : frame) {
      value -= mean;
      energy += value * value;
    }

    std::size_t bestLag = minimumLag;
    double best = 0.0;
    std::fill(correlations.begin(), correlations.end(), 0.0);
    if (energy > 1.0e-9) {
      if (config.correlationMethod == PitchCorrelationMethod::Fft) {
        std::fill(spectrum.begin(), spectrum.end(), std::complex<double>{});
        energyPrefix[0] = 0.0;
        for (std::size_t index = 0U; index < frame.size(); ++index) {
          spectrum[index] = frame[index];
          energyPrefix[index + 1U] = energyPrefix[index] + frame[index] * frame[index];
        }
        if (!pitchFft(spectrum, false, stopToken)) return cancelled();
        for (auto& value : spectrum) value = std::norm(value);
        if (!pitchFft(spectrum, true, stopToken)) return cancelled();
      }
      for (std::size_t lag = minimumLag; lag <= maximumLag; ++lag) {
        if (stopToken.stop_requested()) return cancelled();
        double numerator = 0.0;
        double leftEnergy = 0.0;
        double rightEnergy = 0.0;
        if (config.correlationMethod == PitchCorrelationMethod::Fft) {
          numerator = spectrum[lag].real();
          leftEnergy = energyPrefix[config.frameSize - lag];
          rightEnergy = std::max(0.0, energyPrefix.back() - energyPrefix[lag]);
        } else {
          const auto count = config.frameSize - lag;
          for (std::size_t index = 0; index < count; ++index) {
            const auto left = frame[index];
            const auto right = frame[index + lag];
            numerator += left * right;
            leftEnergy += left * left;
            rightEnergy += right * right;
          }
        }
        const auto denominator = std::sqrt(leftEnergy * rightEnergy);
        const auto correlation = denominator > 1.0e-12 ? numerator / denominator : 0.0;
        correlations[lag] = correlation;
        if (correlation > best) {
          best = correlation;
          bestLag = lag;
        }
      }
    }

    if (best > 0.0 && maximumLag > minimumLag + 1U) {
      const auto peakThreshold = std::max(config.voicingThreshold, best * 0.92);
      for (std::size_t lag = minimumLag + 1U; lag < maximumLag; ++lag) {
        const auto value = correlations[lag];
        if (value >= peakThreshold && value >= correlations[lag - 1U] &&
            value >= correlations[lag + 1U]) {
          bestLag = lag;
          best = value;
          break;
        }
      }
    }

    double refinedLag = static_cast<double>(bestLag);
    if (bestLag > minimumLag && bestLag < maximumLag) {
      const auto left = correlations[bestLag - 1U];
      const auto center = correlations[bestLag];
      const auto right = correlations[bestLag + 1U];
      const auto denominator = left - 2.0 * center + right;
      if (std::abs(denominator) > 1.0e-12) {
        refinedLag += 0.5 * (left - right) / denominator;
      }
    }
    const bool voiced = best >= config.voicingThreshold && refinedLag > 0.0;
    result.push_back(PitchFrame{
        .sourceFrame = start,
        .f0Hz = voiced ? static_cast<double>(sampleRate) / refinedLag : 0.0,
        .confidence = best,
        .voiced = voiced,
    });
  }
  if (stopToken.stop_requested()) return cancelled();
  return result;
}

double medianVoicedPitch(std::span<const PitchFrame> frames) noexcept {
  std::vector<double> pitches;
  for (const auto& frame : frames) {
    if (frame.voiced && std::isfinite(frame.f0Hz) && frame.f0Hz > 0.0) {
      pitches.push_back(frame.f0Hz);
    }
  }
  if (pitches.empty()) return 0.0;
  const auto middle = pitches.begin() + static_cast<std::ptrdiff_t>(pitches.size() / 2U);
  std::nth_element(pitches.begin(), middle, pitches.end());
  if (pitches.size() % 2U != 0U) return *middle;
  const auto lower = *std::max_element(pitches.begin(), middle);
  return (lower + *middle) / 2.0;
}

}  // namespace seam::voicebank
