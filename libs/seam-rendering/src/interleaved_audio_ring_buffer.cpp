#include "seam/rendering/interleaved_audio_ring_buffer.hpp"

#include "seam/domain/routing.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace seam::rendering {

SpscInterleavedAudioRingBuffer::SpscInterleavedAudioRingBuffer(
    std::size_t capacityFrames, std::uint8_t channelCount)
    : buffer_((capacityFrames + 1U) * channelCount, 0.0F),
      positions_(capacityFrames + 1U),
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
  return writeFramesAt(input, nullptr);
}

std::size_t SpscInterleavedAudioRingBuffer::writeFrames(
    std::span<const float> input, std::span<const std::int64_t> positions) noexcept {
  if (input.size() % channelCount_ != 0U || positions.size() != input.size() / channelCount_) {
    return 0U;
  }
  return writeFramesAt(input, positions.data());
}

std::size_t SpscInterleavedAudioRingBuffer::writeFramesAt(
    std::span<const float> input, const std::int64_t* positions) noexcept {
  if (input.empty() || input.size() % channelCount_ != 0U) return 0U;
  const auto requestedFrames = input.size() / channelCount_;
  const auto frameCount = std::min(requestedFrames, availableWriteFrames());
  auto write = writeFrame_.load(std::memory_order_relaxed);
  const auto logicalCapacity = frameCapacity_ + 1U;
  for (std::size_t frame = 0U; frame < frameCount; ++frame) {
    positions_[write].store(positions != nullptr ? positions[frame] : kNoPosition,
                            std::memory_order_release);
    const auto destinationOffset = write * channelCount_;
    const auto sourceOffset = frame * channelCount_;
    for (std::uint8_t channel = 0U; channel < channelCount_; ++channel) {
      buffer_[destinationOffset + channel] = input[sourceOffset + channel];
    }
    write = (write + 1U) % logicalCapacity;
  }
  // The count before the index: whoever reads the count sees every position stored above.
  writeTotal_.store(writeTotal_.load(std::memory_order_relaxed) + frameCount,
                    std::memory_order_release);
  writeFrame_.store(write, std::memory_order_release);
  return frameCount;
}

bool SpscInterleavedAudioRingBuffer::serviceResetRequest() noexcept {
  const auto requested = requestedResetEpoch_.load(std::memory_order_acquire);
  const auto acknowledged =
      acknowledgedResetEpoch_.load(std::memory_order_relaxed);
  if (requested == acknowledged) return false;
  const auto write = writeFrame_.load(std::memory_order_acquire);
  const auto written = writeTotal_.load(std::memory_order_acquire);
  // Nothing is written while a reset waits: the producer stops until it is answered. The counter
  // before the index, as when frames are read.
  readTotal_.store(written, std::memory_order_release);
  readFrame_.store(write, std::memory_order_release);
  acknowledgedResetEpoch_.store(requested, std::memory_order_release);
  return true;
}

bool SpscInterleavedAudioRingBuffer::consumeResetRequest(
    std::span<float> output) noexcept {
  if (!serviceResetRequest()) return false;
  std::fill(output.begin(), output.end(), 0.0F);
  return true;
}

std::size_t SpscInterleavedAudioRingBuffer::readFrames(
    std::span<float> output) noexcept {
  lastReadWasReset_.store(false, std::memory_order_relaxed);
  if (output.empty() || output.size() % channelCount_ != 0U) {
    std::fill(output.begin(), output.end(), 0.0F);
    return 0U;
  }
  if (consumeResetRequest(output)) {
    lastReadWasReset_.store(true, std::memory_order_release);
    return 0U;
  }
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
  // The count before the index: the producer gets a slot back only from the index, and whoever
  // reads a position out of that slot afterwards finds the count already moved.
  readTotal_.store(readTotal_.load(std::memory_order_relaxed) + frameCount,
                   std::memory_order_release);
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

bool SpscInterleavedAudioRingBuffer::resetPending() const noexcept {
  return requestedResetEpoch_.load(std::memory_order_acquire) !=
         acknowledgedResetEpoch_.load(std::memory_order_acquire);
}

SpscInterleavedAudioRingBuffer::NextFrame SpscInterleavedAudioRingBuffer::nextFrame() const noexcept {
  const auto logicalCapacity = frameCapacity_ + 1U;
  // The consumer would have to move between every pair of loads below for a reader to start again
  // more than a few times; the bound only keeps a reader that cannot be answered from waiting for
  // ever, and what it is then told is Busy: it did not see the ring empty.
  constexpr int kAttempts = 1000;
  for (int attempt = 0; attempt < kAttempts; ++attempt) {
    const auto read = readTotal_.load(std::memory_order_acquire);
    if (snapshotProbe_) snapshotProbe_(1);
    const auto written = writeTotal_.load(std::memory_order_acquire);
    if (snapshotProbe_) snapshotProbe_(2);
    if (written <= read) return NextFrame{NextFrame::Kind::Empty, 0};
    const auto place = positions_[read % logicalCapacity].load(std::memory_order_acquire);
    if (snapshotProbe_) snapshotProbe_(3);
    if (readTotal_.load(std::memory_order_acquire) != read) continue;
    if (place == kNoPosition) return NextFrame{NextFrame::Kind::Unplaced, 0};
    return NextFrame{NextFrame::Kind::Placed, place};
  }
  return NextFrame{NextFrame::Kind::Busy, 0};
}

void SpscInterleavedAudioRingBuffer::setSnapshotProbe(SnapshotProbe probe) {
  snapshotProbe_ = std::move(probe);
}

}  // namespace seam::rendering
