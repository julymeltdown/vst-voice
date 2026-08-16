#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <span>
#include <vector>

namespace seam::platform {

struct AudioProcessContext final {
  double sampleRate{48000.0};
  std::size_t frameCount{0};
  std::span<float> left;
  std::span<float> right;
};

class IAudioProcessor {
public:
  virtual ~IAudioProcessor() = default;
  virtual void process(AudioProcessContext context) noexcept = 0;
};

class SilenceProcessor final : public IAudioProcessor {
public:
  void process(AudioProcessContext context) noexcept override {
    std::fill(context.left.begin(), context.left.end(), 0.0F);
    std::fill(context.right.begin(), context.right.end(), 0.0F);
    callbacks_.fetch_add(1, std::memory_order_relaxed);
    frames_.fetch_add(context.frameCount, std::memory_order_relaxed);
  }

  [[nodiscard]] std::size_t callbackCount() const noexcept {
    return callbacks_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::size_t processedFrames() const noexcept {
    return frames_.load(std::memory_order_relaxed);
  }

private:
  std::atomic<std::size_t> callbacks_{0};
  std::atomic<std::size_t> frames_{0};
};

class AudioCallbackSimulator final {
public:
  AudioCallbackSimulator(double sampleRate, std::size_t blockSize)
      : sampleRate_(sampleRate),
        blockSize_(blockSize),
        left_(blockSize),
        right_(blockSize) {}

  void run(IAudioProcessor& processor, std::size_t callbackCount) noexcept {
    for (std::size_t index = 0; index < callbackCount; ++index) {
      processor.process(AudioProcessContext{
          .sampleRate = sampleRate_,
          .frameCount = blockSize_,
          .left = left_,
          .right = right_,
      });
    }
  }

  [[nodiscard]] std::span<const float> left() const noexcept { return left_; }
  [[nodiscard]] std::span<const float> right() const noexcept { return right_; }

private:
  double sampleRate_;
  std::size_t blockSize_;
  std::vector<float> left_;
  std::vector<float> right_;
};

}  // namespace seam::platform
