#pragma once

#include <atomic>
#include <cstddef>
#include <span>
#include <vector>

namespace seam::rendering {

class SpscAudioRingBuffer final {
public:
  explicit SpscAudioRingBuffer(std::size_t capacityFrames);

  [[nodiscard]] std::size_t write(std::span<const float> input) noexcept;
  [[nodiscard]] std::size_t read(std::span<float> output) noexcept;
  [[nodiscard]] std::size_t availableRead() const noexcept;
  [[nodiscard]] std::size_t availableWrite() const noexcept;
  [[nodiscard]] std::size_t capacity() const noexcept { return buffer_.size() - 1U; }
  void clear() noexcept;

private:
  std::vector<float> buffer_;
  alignas(64) std::atomic<std::size_t> readIndex_{0};
  alignas(64) std::atomic<std::size_t> writeIndex_{0};
};

}  // namespace seam::rendering
