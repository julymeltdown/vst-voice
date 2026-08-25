#include "seam/platform/multichannel_ring_buffer_processor.hpp"

#include <algorithm>
#include <cmath>

namespace seam::platform {

MultichannelRingBufferAudioProcessor::MultichannelRingBufferAudioProcessor(
    rendering::SpscInterleavedAudioRingBuffer& ring,
    std::size_t maximumBlockFrames)
    : ring_(ring),
      scratch_(maximumBlockFrames * ring.channelCount(), 0.0F) {}

void MultichannelRingBufferAudioProcessor::process(
    AudioProcessContext context) noexcept {
  const auto channelCount = static_cast<std::size_t>(ring_.channelCount());
  const auto providedChannels = context.outputChannelCount();
  const auto frames = std::min(
      context.frameCount,
      channelCount == 0U ? 0U : scratch_.size() / channelCount);
  if (providedChannels < channelCount) {
    channelMismatchCallbacks_.fetch_add(1U, std::memory_order_relaxed);
  }

  auto interleaved = std::span<float>{scratch_}.first(frames * channelCount);
  const auto delivered = ring_.readFrames(interleaved);
  const auto intentionalReset = ring_.lastReadWasReset();
  const auto gain = gain_.load(std::memory_order_relaxed);
  for (std::size_t channel = 0U; channel < providedChannels; ++channel) {
    auto output = context.output(channel);
    const auto count = std::min(frames, output.size());
    if (channel < channelCount) {
      for (std::size_t frame = 0U; frame < count; ++frame) {
        output[frame] = interleaved[frame * channelCount + channel] * gain;
      }
    } else {
      std::fill_n(output.begin(), static_cast<std::ptrdiff_t>(count), 0.0F);
    }
    if (output.size() > count) {
      std::fill(output.begin() + static_cast<std::ptrdiff_t>(count),
                output.end(), 0.0F);
    }
  }

  callbacks_.fetch_add(1U, std::memory_order_relaxed);
  requestedFrames_.fetch_add(frames, std::memory_order_relaxed);
  deliveredFrames_.fetch_add(delivered, std::memory_order_relaxed);
  const auto missing = frames - delivered;
  if (intentionalReset) {
    intentionalResetFrames_.fetch_add(missing, std::memory_order_relaxed);
  } else {
    underflowFrames_.fetch_add(missing, std::memory_order_relaxed);
  }
}

void MultichannelRingBufferAudioProcessor::setGain(float gain) noexcept {
  gain_.store(std::isfinite(gain) ? std::clamp(gain, 0.0F, 4.0F) : 1.0F,
              std::memory_order_relaxed);
}

MultichannelRingProcessorStats
MultichannelRingBufferAudioProcessor::stats() const noexcept {
  return MultichannelRingProcessorStats{
      .callbacks = callbacks_.load(std::memory_order_relaxed),
      .requestedFrames = requestedFrames_.load(std::memory_order_relaxed),
      .deliveredFrames = deliveredFrames_.load(std::memory_order_relaxed),
      .underflowFrames = underflowFrames_.load(std::memory_order_relaxed),
      .intentionalResetFrames =
          intentionalResetFrames_.load(std::memory_order_relaxed),
      .channelMismatchCallbacks =
          channelMismatchCallbacks_.load(std::memory_order_relaxed),
  };
}

}  // namespace seam::platform
