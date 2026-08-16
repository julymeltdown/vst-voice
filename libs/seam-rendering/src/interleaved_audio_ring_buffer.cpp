#include "seam/rendering/interleaved_audio_ring_buffer.hpp"

#include "seam/domain/routing.hpp"

#include <algorithm>
#include <stdexcept>

namespace seam::rendering {

SpscInterleavedAudioRingBuffer::SpscInterleavedAudioRingBuffer(
    std::size_t capacityFrames, std::uint8_t channelCount)
    : buffer_((capacityFrames + 1U) * channelCount, 0.0F),
      frameCapacity_(capacityFrames),
      channelCount_(channelCount) {
  if (capacityFrames < 2U || channelCount == 0U ||
      channelCount > domain::kMaximumAudioChannels) {
    throw std::invalid_argument(
        "Interleaved audio ring dimensions are invalid");
  }
}

std::size_t SpscInterleavedAudioRingBuffer::availableReadFrames() const noexcept {
  const auto read = readFrame_.load(std::memory_order_acquire);
  const auto write = writeFrame_.load(std::memory_order_acquire);
  const auto logicalCapacity = frameCapacity_ + 1U;
  return write >= read ? write - read : logicalCapacity - read + write;
}

std::size_t SpscInterleavedAudioRingBuffer::availableWriteFrames() const noexcept {
  return frameCapacity_ - availableReadFrames();
}

std::size_t SpscInterleavedAudioRingBuffer::writeFrames(
    std::span<const float> input) noexcept {
  if (input.empty() || input.size() % channelCount_ != 0U) return 0U;
  const auto requestedFrames = input.size() / channelCount_;
  const auto frameCount = std::min(requestedFrames, availableWriteFrames());
  auto write = writeFrame_.load(std::memory_order_relaxed);
  const auto logicalCapacity = frameCapacity_ + 1U;
  for (std::size_t frame = 0U; frame < frameCount; ++frame) {
    const auto destinationOffset = write * channelCount_;
    const auto sourceOffset = frame * channelCount_;
    for (std::uint8_t channel = 0U; channel < channelCount_; ++channel) {
      buffer_[destinationOffset + channel] = input[sourceOffset + channel];
    }
    write = (write + 1U) % logicalCapacity;
  }
  writeFrame_.store(write, std::memory_order_release);
  return frameCount;
}

bool SpscInterleavedAudioRingBuffer::consumeResetRequest(
    std::span<float> output) noexcept {
  const auto requested = requestedResetEpoch_.load(std::memory_order_acquire);
  const auto acknowledged =
      acknowledgedResetEpoch_.load(std::memory_order_relaxed);
  if (requested == acknowledged) return false;
  const auto write = writeFrame_.load(std::memory_order_acquire);
  readFrame_.store(write, std::memory_order_release);
  acknowledgedResetEpoch_.store(requested, std::memory_order_release);
  std::fill(output.begin(), output.end(), 0.0F);
  return true;
}

std::size_t SpscInterleavedAudioRingBuffer::readFrames(
    std::span<float> output) noexcept {
  if (output.empty() || output.size() % channelCount_ != 0U) {
    std::fill(output.begin(), output.end(), 0.0F);
    return 0U;
  }
  if (consumeResetRequest(output)) return 0U;
  const auto requestedFrames = output.size() / channelCount_;
  const auto frameCount = std::min(requestedFrames, availableReadFrames());
  auto read = readFrame_.load(std::memory_order_relaxed);
  const auto logicalCapacity = frameCapacity_ + 1U;
  for (std::size_t frame = 0U; frame < frameCount; ++frame) {
    const auto sourceOffset = read * channelCount_;
    const auto destinationOffset = frame * channelCount_;
    for (std::uint8_t channel = 0U; channel < channelCount_; ++channel) {
      output[destinationOffset + channel] = buffer_[sourceOffset + channel];
    }
    read = (read + 1U) % logicalCapacity;
  }
  std::fill(output.begin() +
                static_cast<std::ptrdiff_t>(frameCount * channelCount_),
            output.end(), 0.0F);
  readFrame_.store(read, std::memory_order_release);
  return frameCount;
}

std::uint64_t SpscInterleavedAudioRingBuffer::requestConsumerReset() noexcept {
  return requestedResetEpoch_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
}

bool SpscInterleavedAudioRingBuffer::resetAcknowledged(
    std::uint64_t epoch) const noexcept {
  return acknowledgedResetEpoch_.load(std::memory_order_acquire) >= epoch;
}

}  // namespace seam::rendering
