#include "seam/voicebank_production/operations.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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
  for (std::size_t frame = 0U; frame < outputFrames; ++frame) {
    const auto sourcePosition = static_cast<double>(frame) / ratio;
    const auto left = std::min(
        static_cast<std::size_t>(sourcePosition), input.frameCount() - 1U);
    const auto right = std::min(left + 1U, input.frameCount() - 1U);
    const auto fraction = static_cast<float>(
        sourcePosition - static_cast<double>(left));
    for (std::size_t channel = 0U; channel < input.channels; ++channel) {
      const auto leftSample = input.interleaved[left * input.channels + channel];
      const auto rightSample = input.interleaved[right * input.channels + channel];
      output.interleaved[frame * input.channels + channel] =
          leftSample + (rightSample - leftSample) * fraction;
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
              {"method", "linear-v1"}};
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
