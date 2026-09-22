#include "seam/voicebank_production/operations.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace seam::voicebank_production {
namespace {

core::Result<voicebank::AudioBuffer> selectChannel(
    const voicebank::AudioBuffer& input, std::uint16_t channel) {
  if (input.channels == 0U || channel >= input.channels) {
    return core::failure<voicebank::AudioBuffer>(
        core::ErrorCode::InvalidArgument, "Channel selection is out of range");
  }
  voicebank::AudioBuffer output{
      .sampleRate = input.sampleRate,
      .channels = 1U,
      .bitsPerSample = input.bitsPerSample,
  };
  output.interleaved.reserve(input.frameCount());
  for (std::size_t frame = 0U; frame < input.frameCount(); ++frame) {
    output.interleaved.push_back(
        input.interleaved[frame * input.channels + channel]);
  }
  return output;
}

core::Result<voicebank::AudioBuffer> downmix(
    const voicebank::AudioBuffer& input) {
  if (input.channels == 0U) {
    return core::failure<voicebank::AudioBuffer>(
        core::ErrorCode::InvalidArgument, "Downmix input has no channels");
  }
  return voicebank::AudioBuffer{
      .sampleRate = input.sampleRate,
      .channels = 1U,
      .bitsPerSample = input.bitsPerSample,
      .interleaved = input.monoMix(),
  };
}

// Band-limited resampling.
//
// The previous implementation interpolated linearly between neighbouring samples.
// On a downsample that is not merely low-quality, it is wrong: nothing removes
// energy above the new Nyquist frequency, so that energy folds back into the
// audible band. A 10 kHz tone resampled from 48 kHz to 16 kHz survives at full
// amplitude as a 6 kHz tone. A singer recorded at 48 kHz and prepared as a 16 kHz
// bank would therefore acquire inharmonic partials that were never sung, and the
// artefact is not detectable afterwards as noise, only as wrong pitch content.
//
// Each output sample is now the windowed-sinc interpolation of the input, with the
// cutoff placed at the *lower* of the two Nyquist frequencies. Downsampling
// therefore attenuates content that cannot be represented before it can alias, and
// upsampling still reconstructs the original band. The kernel is evaluated
// directly rather than read from a table so the code stays auditable.
constexpr double kResampleKernelHalfWidth = 16.0;

double sinc(double value) {
  if (std::abs(value) < 1e-12) return 1.0;
  const auto angle = std::numbers::pi * value;
  return std::sin(angle) / angle;
}

// Blackman window: continuous, so it does not introduce the derivative
// discontinuity a rectangular truncation would, and its sidelobes fall off fast
// enough that a 16-sample half-width suppresses stopband energy below the
// float32 noise floor.
double windowWeight(double normalized) {
  if (std::abs(normalized) >= 1.0) return 0.0;
  const auto position = std::numbers::pi * (normalized + 1.0);
  return 0.42 - 0.5 * std::cos(position) + 0.08 * std::cos(2.0 * position);
}

core::Result<voicebank::AudioBuffer> resample(
    const voicebank::AudioBuffer& input, std::uint32_t targetRate) {
  if (input.sampleRate == 0U || input.channels == 0U || targetRate == 0U) {
    return core::failure<voicebank::AudioBuffer>(
        core::ErrorCode::InvalidArgument, "Resample rates and channels must be non-zero");
  }
  if (input.sampleRate == targetRate || input.frameCount() == 0U) {
    auto output = input;
    output.sampleRate = targetRate;
    return output;
  }
  const auto ratio = static_cast<double>(targetRate) /
                     static_cast<double>(input.sampleRate);
  const auto scaledFrames = static_cast<double>(input.frameCount()) * ratio;
  if (!std::isfinite(scaledFrames) ||
      scaledFrames > static_cast<double>(
          std::numeric_limits<std::size_t>::max() / input.channels)) {
    return core::failure<voicebank::AudioBuffer>(
        core::ErrorCode::InvalidArgument, "Resample output is too large");
  }
  const auto outputFrames = std::max<std::size_t>(
      1U, static_cast<std::size_t>(std::llround(scaledFrames)));
  voicebank::AudioBuffer output{
      .sampleRate = targetRate,
      .channels = input.channels,
      .bitsPerSample = input.bitsPerSample,
      .interleaved = std::vector<float>(outputFrames * input.channels, 0.0F),
  };
  // Cutoff in input-domain cycles per sample. When downsampling this is below 0.5
  // so the kernel itself does the anti-aliasing; when upsampling it stays at 0.5.
  const double cutoff = std::min(0.5, 0.5 * ratio);
  const auto halfWidth = static_cast<std::int64_t>(
      std::ceil(kResampleKernelHalfWidth / (2.0 * cutoff)));
  const auto sourceFrames = input.frameCount();
  for (std::size_t frame = 0U; frame < outputFrames; ++frame) {
    const double sourcePosition = static_cast<double>(frame) / ratio;
    const auto centre = static_cast<std::int64_t>(std::floor(sourcePosition));
    // A silent input must not acquire a DC step at the edges, so the left and
    // right neighbours are edge-clamped exactly as the linear version did.
    for (std::size_t channel = 0U; channel < input.channels; ++channel) {
      double accumulated = 0.0;
      double weightTotal = 0.0;
      for (std::int64_t offset = -halfWidth; offset <= halfWidth; ++offset) {
        const auto sourceIndex = centre + offset;
        const auto clamped = static_cast<std::size_t>(std::clamp<std::int64_t>(
            sourceIndex, 0, static_cast<std::int64_t>(sourceFrames) - 1));
        const auto distance = sourcePosition - static_cast<double>(sourceIndex);
        const auto weight = 2.0 * cutoff * sinc(2.0 * cutoff * distance) *
                            windowWeight(distance / static_cast<double>(halfWidth));
        accumulated += static_cast<double>(
                           input.interleaved[clamped * input.channels + channel]) * weight;
        weightTotal += weight;
      }
      // Normalising by the realised weight sum keeps the DC gain at exactly 1 even
      // where the kernel is truncated at the signal edges.
      output.interleaved[frame * input.channels + channel] =
          weightTotal == 0.0 ? 0.0F
                             : static_cast<float>(accumulated / weightTotal);
    }
  }
  return output;
}

core::Result<voicebank::AudioBuffer> removeDc(
    const voicebank::AudioBuffer& input) {
  if (input.channels == 0U) {
    return core::failure<voicebank::AudioBuffer>(
        core::ErrorCode::InvalidArgument, "DC-removal input has no channels");
  }
  auto output = input;
  if (output.frameCount() == 0U) return output;
  for (std::size_t channel = 0U; channel < input.channels; ++channel) {
    double total = 0.0;
    for (std::size_t frame = 0U; frame < input.frameCount(); ++frame) {
      total += input.interleaved[frame * input.channels + channel];
    }
    const auto offset = static_cast<float>(
        total / static_cast<double>(input.frameCount()));
    for (std::size_t frame = 0U; frame < input.frameCount(); ++frame) {
      output.interleaved[frame * input.channels + channel] -= offset;
    }
  }
  return output;
}

core::Result<voicebank::AudioBuffer> normalize(
    const voicebank::AudioBuffer& input, float targetPeak) {
  if (!(targetPeak > 0.0F && targetPeak <= 1.0F)) {
    return core::failure<voicebank::AudioBuffer>(
        core::ErrorCode::InvalidArgument, "Normalization peak must be in (0, 1]");
  }
  auto output = input;
  float peak = 0.0F;
  for (const auto sample : input.interleaved) {
    peak = std::max(peak, std::abs(sample));
  }
  if (peak <= std::numeric_limits<float>::epsilon()) return output;
  const auto gain = targetPeak / peak;
  for (auto& sample : output.interleaved) sample *= gain;
  return output;
}

core::Result<voicebank::AudioBuffer> slice(
    const voicebank::AudioBuffer& input, std::size_t start,
    std::size_t end) {
  if (input.channels == 0U || start >= end || end > input.frameCount()) {
    return core::failure<voicebank::AudioBuffer>(
        core::ErrorCode::InvalidArgument, "Frame slice is invalid");
  }
  const auto first = input.interleaved.begin() +
                     static_cast<std::ptrdiff_t>(start * input.channels);
  const auto last = input.interleaved.begin() +
                    static_cast<std::ptrdiff_t>(end * input.channels);
  return voicebank::AudioBuffer{
      .sampleRate = input.sampleRate,
      .channels = input.channels,
      .bitsPerSample = input.bitsPerSample,
      .interleaved = std::vector<float>(first, last),
  };
}

}

core::Result<voicebank::AudioBuffer> applyOperation(
    const voicebank::AudioBuffer& input, const OperationRequest& request) {
  switch (request.kind) {
    case OperationKind::ChannelSelect:
      return selectChannel(input, request.channelIndex);
    case OperationKind::Downmix:
      return downmix(input);
    case OperationKind::Resample:
      return resample(input, request.targetSampleRate);
    case OperationKind::RemoveDc:
      return removeDc(input);
    case OperationKind::NormalizeGain:
      return normalize(input, request.targetPeak);
    case OperationKind::Trim:
    case OperationKind::Segment:
      return slice(input, request.startFrame, request.endFrame);
  }
  return core::failure<voicebank::AudioBuffer>(
      core::ErrorCode::Unsupported, "Unsupported production audio operation");
}

std::map<std::string, std::string, std::less<>> operationParameters(
    const OperationRequest& request) {
  switch (request.kind) {
    case OperationKind::ChannelSelect:
      return {{"channelIndex", std::to_string(request.channelIndex)}};
    case OperationKind::Downmix:
      return {{"method", "equal-weight-mono"}};
    case OperationKind::Resample:
      return {{"targetSampleRate", std::to_string(request.targetSampleRate)},
              {"method", "bandlimited-sinc-blackman-v2"}};
    case OperationKind::RemoveDc:
      return {{"method", "per-channel-mean-v1"}};
    case OperationKind::NormalizeGain:
      return {{"targetPeak", std::to_string(request.targetPeak)}};
    case OperationKind::Trim:
    case OperationKind::Segment:
      return {{"startFrame", std::to_string(request.startFrame)},
              {"endFrame", std::to_string(request.endFrame)}};
  }
  return {};
}

}
