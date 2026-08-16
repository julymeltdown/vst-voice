#include "seam/platform/audio_device.hpp"

#include "seam/domain/routing.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <span>
#include <stop_token>
#include <thread>
#include <vector>

namespace seam::platform {
namespace {

core::Result<void> validateConfig(const AudioDeviceConfig& config) {
  if (config.sampleRate < 8000U || config.sampleRate > 384000U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Audio sample rate is outside the supported range");
  }
  if (config.blockFrames == 0U || config.blockFrames > 16384U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Audio block size is outside the supported range");
  }
  if (config.outputChannels == 0U ||
      config.outputChannels > domain::kMaximumAudioChannels) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Audio output channel count is outside the supported range");
  }
  return core::success();
}

class ThreadedAudioDevice final : public IAudioDevice {
public:
  ~ThreadedAudioDevice() override { stop(); }

  core::Result<void> open(const AudioDeviceConfig& config,
                          IAudioProcessor& processor) override {
    if (running()) {
      return core::failure(core::ErrorCode::Conflict,
                           "Cannot reopen a running audio device");
    }
    const auto valid = validateConfig(config);
    if (!valid) return valid;
    config_ = config;
    processor_ = &processor;
    channels_.assign(config.outputChannels,
                     std::vector<float>(config.blockFrames, 0.0F));
    outputViews_.clear();
    outputViews_.reserve(channels_.size());
    for (auto& channel : channels_) outputViews_.emplace_back(channel);
    opened_ = true;
    return core::success();
  }

  core::Result<void> start() override {
    if (!opened_ || processor_ == nullptr) {
      return core::failure(core::ErrorCode::Conflict,
                           "Audio device must be opened before start");
    }
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true,
                                          std::memory_order_acq_rel)) {
      return core::failure(core::ErrorCode::Conflict,
                           "Audio device is already running");
    }
    try {
      worker_ = std::jthread([this](std::stop_token stopToken) {
        const auto period = std::chrono::duration<double>(
            static_cast<double>(config_.blockFrames) /
            static_cast<double>(config_.sampleRate));
        auto next = std::chrono::steady_clock::now();
        while (!stopToken.stop_requested()) {
          next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              period);
          processor_->process(AudioProcessContext{
              .sampleRate = static_cast<double>(config_.sampleRate),
              .frameCount = config_.blockFrames,
              .left = outputViews_.empty() ? std::span<float>{} : outputViews_[0],
              .right = outputViews_.size() < 2U ? std::span<float>{}
                                                : outputViews_[1],
              .outputs = outputViews_,
          });
          callbacks_.fetch_add(1U, std::memory_order_relaxed);
          frames_.fetch_add(config_.blockFrames, std::memory_order_relaxed);
          std::this_thread::sleep_until(next);
          const auto now = std::chrono::steady_clock::now();
          if (now > next + std::chrono::milliseconds{20}) {
            xruns_.fetch_add(1U, std::memory_order_relaxed);
            next = now;
          }
        }
        running_.store(false, std::memory_order_release);
      });
    } catch (...) {
      running_.store(false, std::memory_order_release);
      return core::failure(core::ErrorCode::Internal,
                           "Unable to create audio callback thread");
    }
    return core::success();
  }

  void stop() noexcept override {
    if (worker_.joinable()) {
      worker_.request_stop();
      worker_.join();
    }
    running_.store(false, std::memory_order_release);
  }

  bool running() const noexcept override {
    return running_.load(std::memory_order_acquire);
  }

  AudioDeviceInfo info() const override {
    return AudioDeviceInfo{
        .backend = "threaded-callback-clock",
        .deviceName = "no physical output",
        .sampleRate = config_.sampleRate,
        .blockFrames = config_.blockFrames,
        .outputChannels = config_.outputChannels,
        .physical = false,
    };
  }

  AudioDeviceStats stats() const noexcept override {
    return AudioDeviceStats{
        .callbacks = callbacks_.load(std::memory_order_relaxed),
        .frames = frames_.load(std::memory_order_relaxed),
        .writeFailures = 0U,
        .xruns = xruns_.load(std::memory_order_relaxed),
    };
  }

private:
  AudioDeviceConfig config_;
  IAudioProcessor* processor_{nullptr};
  std::vector<std::vector<float>> channels_;
  std::vector<std::span<float>> outputViews_;
  std::jthread worker_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> callbacks_{0U};
  std::atomic<std::uint64_t> frames_{0U};
  std::atomic<std::uint64_t> xruns_{0U};
  bool opened_{false};
};

}  // namespace

std::unique_ptr<IAudioDevice> createThreadedAudioDevice() {
  return std::make_unique<ThreadedAudioDevice>();
}

}  // namespace seam::platform
