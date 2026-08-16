#include "seam/voicebank/pitch.hpp"

#include <algorithm>
#include <cmath>

namespace seam::voicebank {

core::Result<std::vector<PitchFrame>> analyzePitch(std::span<const float> samples,
                                                   std::uint32_t sampleRate,
                                                   PitchConfig config) {
  if (samples.empty() || sampleRate < 8000 || sampleRate > 384000 ||
      config.frameSize < 128 || config.hopSize == 0 ||
      config.minimumHz <= 0.0 || config.maximumHz <= config.minimumHz ||
      config.maximumHz >= static_cast<double>(sampleRate) / 2.0 ||
      config.voicingThreshold <= 0.0 || config.voicingThreshold >= 1.0) {
    return core::failure<std::vector<PitchFrame>>(
        core::ErrorCode::InvalidArgument, "Pitch analysis configuration is invalid");
  }
  const auto minimumLag = std::max<std::size_t>(1, static_cast<std::size_t>(
      std::floor(static_cast<double>(sampleRate) / config.maximumHz)));
  const auto maximumLag = std::min<std::size_t>(config.frameSize / 2U,
      static_cast<std::size_t>(std::ceil(static_cast<double>(sampleRate) /
                                         config.minimumHz)));
  if (minimumLag >= maximumLag) {
    return core::failure<std::vector<PitchFrame>>(
        core::ErrorCode::InvalidArgument, "Pitch lag range is empty");
  }

  const auto frameCount = samples.size() <= config.frameSize
                              ? 1U
                              : 1U + (samples.size() - config.frameSize) / config.hopSize;
  std::vector<PitchFrame> result;
  result.reserve(frameCount);
  std::vector<double> frame(config.frameSize, 0.0);
  std::vector<double> correlations(maximumLag + 1U, 0.0);

  for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
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
      for (std::size_t lag = minimumLag; lag <= maximumLag; ++lag) {
        double numerator = 0.0;
        double leftEnergy = 0.0;
        double rightEnergy = 0.0;
        const auto count = config.frameSize - lag;
        for (std::size_t index = 0; index < count; ++index) {
          const auto left = frame[index];
          const auto right = frame[index + lag];
          numerator += left * right;
          leftEnergy += left * left;
          rightEnergy += right * right;
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
