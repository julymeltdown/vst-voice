#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace seam::rendering {

// Single-producer/single-consumer PCM ring. Reset requests are producer-safe:
// the producer publishes an epoch, while only the consumer advances readIndex_.
class SpscAudioRingBuffer final {
public:
  explicit SpscAudioRingBuffer(std::size_t capacityFrames);

  [[nodiscard]] std::size_t write(std::span<const float> input) noexcept;
  [[nodiscard]] std::size_t read(std::span<float> output) noexcept;
  [[nodiscard]] std::size_t availableRead() const noexcept;
  [[nodiscard]] std::size_t availableWrite() const noexcept;
  [[nodiscard]] std::size_t capacity() const noexcept { return buffer_.size() - 1U; }

  [[nodiscard]] std::uint64_t requestConsumerReset() noexcept;
  [[nodiscard]] bool resetAcknowledged(std::uint64_t epoch) const noexcept;
  [[nodiscard]] bool lastReadWasReset() const noexcept {
    return lastReadWasReset_.load(std::memory_order_acquire);
  }
  [[nodiscard]] std::uint64_t acknowledgedResetEpoch() const noexcept;

  // Compatibility alias. This is asynchronous and never writes readIndex_ from
  // the producer side; callers that need ordering must wait for acknowledgement.
  void clear() noexcept { static_cast<void>(requestConsumerReset()); }

private:
  [[nodiscard]] bool consumeResetRequest(std::span<float> output) noexcept;

  std::vector<float> buffer_;
  alignas(64) std::atomic<std::size_t> readIndex_{0};
  alignas(64) std::atomic<std::size_t> writeIndex_{0};
  alignas(64) std::atomic<std::uint64_t> requestedResetEpoch_{0};
  alignas(64) std::atomic<std::uint64_t> acknowledgedResetEpoch_{0};
  alignas(64) std::atomic<bool> lastReadWasReset_{false};
};

}  // namespace seam::rendering
