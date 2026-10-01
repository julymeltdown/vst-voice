#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace seam::rendering {

class SpscInterleavedAudioRingBuffer final {
public:
  SpscInterleavedAudioRingBuffer(std::size_t capacityFrames,
                                 std::uint8_t channelCount);

  [[nodiscard]] std::size_t writeFrames(
      std::span<const float> interleavedInput) noexcept;
  [[nodiscard]] std::size_t readFrames(
      std::span<float> interleavedOutput) noexcept;
  [[nodiscard]] std::size_t availableReadFrames() const noexcept;
  [[nodiscard]] std::size_t availableWriteFrames() const noexcept;
  [[nodiscard]] std::size_t capacityFrames() const noexcept {
    return frameCapacity_;
  }
  [[nodiscard]] std::uint8_t channelCount() const noexcept {
    return channelCount_;
  }

  [[nodiscard]] std::uint64_t requestConsumerReset() noexcept;
  [[nodiscard]] bool resetAcknowledged(std::uint64_t epoch) const noexcept;
  // The consumer's half of a reset, without reading anything, for the owner of a consumer that is
  // not running (an audio device that is stopped). The producer asks for a reset and writes nothing
  // until it is acknowledged, and only the consumer acknowledges: with the device stopped nobody
  // would, so the owner answers in its place. Drops what the ring holds and acknowledges the request
  // that is waiting. Returns whether one was: with none waiting it changes nothing, so audio written
  // after the last reset is kept.
  // Call it only while no thread is inside readFrames(): the consumer's work is not shared.
  [[nodiscard]] bool serviceResetRequest() noexcept;
  // Whether the producer has asked for a reset that no consumer has answered yet. What the ring
  // holds is then audio that is going to be dropped unplayed, and not audio that is about to be
  // heard. Safe to call from any thread.
  [[nodiscard]] bool resetPending() const noexcept;
  [[nodiscard]] bool lastReadWasReset() const noexcept {
    return lastReadWasReset_.load(std::memory_order_acquire);
  }

private:
  [[nodiscard]] bool consumeResetRequest(
      std::span<float> interleavedOutput) noexcept;

  std::vector<float> buffer_;
  std::size_t frameCapacity_{0U};
  std::uint8_t channelCount_{0U};
  // Indices address logical frames in a ring with one hidden sentinel frame.
  alignas(64) std::atomic<std::size_t> readFrame_{0U};
  alignas(64) std::atomic<std::size_t> writeFrame_{0U};
  alignas(64) std::atomic<std::uint64_t> requestedResetEpoch_{0U};
  alignas(64) std::atomic<std::uint64_t> acknowledgedResetEpoch_{0U};
  alignas(64) std::atomic<bool> lastReadWasReset_{false};
};

}  // namespace seam::rendering
