#include "seam/rendering/audio_level_envelope.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace seam::rendering {
std::optional<double> MeasuredChannelLevel::rmsDbfs() const noexcept {
  return rms > 0.0 ? std::optional<double>{20.0 * std::log10(rms)} : std::nullopt;
}
core::Result<MeasuredAudioEnvelope> measureAudioLevels(std::span<const float> pcm,
    std::uint8_t channels, std::size_t first, std::size_t count, std::size_t maximumBins, std::stop_token stop) {
  const auto cancelled = [] { return core::failure<MeasuredAudioEnvelope>(core::ErrorCode::Conflict, "Audio measurement cancelled"); };
  if (stop.stop_requested()) return cancelled();
  if (channels == 0U || channels > 64U || pcm.size() % channels != 0U ||
      count == 0U || maximumBins == 0U || maximumBins > kMaximumMeasuredBins)
    return core::failure<MeasuredAudioEnvelope>(core::ErrorCode::InvalidArgument, "Audio measurement requires complete frames, 1 to 64 channels and 1 to 4096 bins");
  const auto frames = pcm.size() / channels;
  if (first > frames || count > frames - first)
    return core::failure<MeasuredAudioEnvelope>(core::ErrorCode::InvalidArgument, "Audio measurement window exceeds its PCM buffer");
  if (count > kMaximumMeasuredWindowSamples / channels)
    return core::failure<MeasuredAudioEnvelope>(core::ErrorCode::Unsupported, "Audio measurement window exceeds the sample budget; choose a smaller window");
  const auto bins = std::min(maximumBins, count);
  MeasuredAudioEnvelope result{first, count, channels, {}}; result.bins.reserve(bins);
  const auto boundary = [&](std::size_t i) { return first + (count / bins) * i + ((count % bins) * i) / bins; };
  std::size_t visited = 0U;
  for (std::size_t i = 0U; i < bins; ++i) {
    if (stop.stop_requested()) return cancelled();
    const auto begin = boundary(i), end = boundary(i + 1U);
    MeasuredAudioBin bin{begin, end - begin, std::vector<MeasuredChannelLevel>(channels)};
    std::array<double, 64U> squares{};
    for (auto frame = begin; frame < end; ++frame) for (std::size_t channel = 0U; channel < channels; ++channel) {
      if ((visited++ & 4095U) == 0U && stop.stop_requested()) return cancelled();
      const double sample = pcm[frame * channels + channel];
      if (!std::isfinite(sample))
        return core::failure<MeasuredAudioEnvelope>(core::ErrorCode::InvalidArgument, "Audio measurement encountered a nonfinite PCM sample");
      const auto magnitude = std::abs(sample);
      squares[channel] += sample * sample;
      auto& level = bin.channels[channel]; level.peak = std::max(level.peak, magnitude);
      if (magnitude >= 1.0) ++level.atOrAboveFullScale;
    }
    for (std::size_t channel = 0U; channel < channels; ++channel)
      bin.channels[channel].rms = std::sqrt(squares[channel] / static_cast<double>(bin.frameCount));
    result.bins.push_back(std::move(bin));
  }
  if (stop.stop_requested()) return cancelled();
  return result;
}
}
