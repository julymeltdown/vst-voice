#include "seam/platform/ring_buffer_processor.hpp"

#include <algorithm>
#include <cmath>

namespace seam::platform {

void RingBufferAudioProcessor::process(AudioProcessContext context) noexcept {
  const auto count = std::min({context.frameCount, context.left.size(),
                               context.right.size()});
  auto left = context.left.first(count);
  const auto delivered = ring_.read(left);
  const auto intentionalReset = ring_.lastReadWasReset();
  const auto gain = gain_.load(std::memory_order_relaxed);
  for (std::size_t index = 0U; index < count; ++index) {
    const auto sample = left[index] * gain;
    left[index] = sample;
    context.right[index] = sample;
  }
  if (context.left.size() > count) {
    std::fill(context.left.begin() + static_cast<std::ptrdiff_t>(count),
              context.left.end(), 0.0F);
  }
  if (context.right.size() > count) {
    std::fill(context.right.begin() + static_cast<std::ptrdiff_t>(count),
              context.right.end(), 0.0F);
  }
  callbacks_.fetch_add(1U, std::memory_order_relaxed);
  requestedFrames_.fetch_add(count, std::memory_order_relaxed);
  deliveredFrames_.fetch_add(delivered, std::memory_order_relaxed);
  const auto missing = count - delivered;
  if (intentionalReset) {
    intentionalResetFrames_.fetch_add(missing, std::memory_order_relaxed);
  } else {
    underflowFrames_.fetch_add(missing, std::memory_order_relaxed);
  }
}

void RingBufferAudioProcessor::setGain(float gain) noexcept {
  gain_.store(std::isfinite(gain) ? std::clamp(gain, 0.0F, 4.0F) : 1.0F,
              std::memory_order_relaxed);
}

RingBufferProcessorStats RingBufferAudioProcessor::stats() const noexcept {
  return RingBufferProcessorStats{
      .callbacks = callbacks_.load(std::memory_order_relaxed),
      .requestedFrames = requestedFrames_.load(std::memory_order_relaxed),
      .deliveredFrames = deliveredFrames_.load(std::memory_order_relaxed),
      .underflowFrames = underflowFrames_.load(std::memory_order_relaxed),
      .intentionalResetFrames =
          intentionalResetFrames_.load(std::memory_order_relaxed),
  };
}

}  // namespace seam::platform
