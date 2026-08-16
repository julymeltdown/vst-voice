#include "seam/rendering/audio_ring_buffer.hpp"

#include <algorithm>
#include <stdexcept>

namespace seam::rendering {

SpscAudioRingBuffer::SpscAudioRingBuffer(std::size_t capacityFrames)
    : buffer_(capacityFrames + 1U, 0.0F) {
  if (capacityFrames < 2U) {
    throw std::invalid_argument("Audio ring buffer capacity must be at least two frames");
  }
}

std::size_t SpscAudioRingBuffer::availableRead() const noexcept {
  const auto read = readIndex_.load(std::memory_order_acquire);
  const auto write = writeIndex_.load(std::memory_order_acquire);
  return write >= read ? write - read : buffer_.size() - read + write;
}

std::size_t SpscAudioRingBuffer::availableWrite() const noexcept {
  return capacity() - availableRead();
}

std::size_t SpscAudioRingBuffer::write(std::span<const float> input) noexcept {
  const auto count = std::min(input.size(), availableWrite());
  auto write = writeIndex_.load(std::memory_order_relaxed);
  for (std::size_t index = 0; index < count; ++index) {
    buffer_[write] = input[index];
    write = (write + 1U) % buffer_.size();
  }
  writeIndex_.store(write, std::memory_order_release);
  return count;
}

bool SpscAudioRingBuffer::consumeResetRequest(std::span<float> output) noexcept {
  const auto requested = requestedResetEpoch_.load(std::memory_order_acquire);
  const auto acknowledged = acknowledgedResetEpoch_.load(std::memory_order_relaxed);
  if (requested == acknowledged) return false;

  // The consumer owns readIndex_. Discard everything published before this
  // reset and acknowledge only after the new read position is visible.
  const auto write = writeIndex_.load(std::memory_order_acquire);
  readIndex_.store(write, std::memory_order_release);
  acknowledgedResetEpoch_.store(requested, std::memory_order_release);
  std::fill(output.begin(), output.end(), 0.0F);
  return true;
}

std::size_t SpscAudioRingBuffer::read(std::span<float> output) noexcept {
  if (consumeResetRequest(output)) return 0U;

  const auto count = std::min(output.size(), availableRead());
  auto read = readIndex_.load(std::memory_order_relaxed);
  for (std::size_t index = 0; index < count; ++index) {
    output[index] = buffer_[read];
    read = (read + 1U) % buffer_.size();
  }
  for (std::size_t index = count; index < output.size(); ++index) output[index] = 0.0F;
  readIndex_.store(read, std::memory_order_release);
  return count;
}

std::uint64_t SpscAudioRingBuffer::requestConsumerReset() noexcept {
  return requestedResetEpoch_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
}

bool SpscAudioRingBuffer::resetAcknowledged(std::uint64_t epoch) const noexcept {
  return acknowledgedResetEpoch_.load(std::memory_order_acquire) >= epoch;
}

std::uint64_t SpscAudioRingBuffer::acknowledgedResetEpoch() const noexcept {
  return acknowledgedResetEpoch_.load(std::memory_order_acquire);
}

}  // namespace seam::rendering
