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
};

}  // namespace seam::rendering
