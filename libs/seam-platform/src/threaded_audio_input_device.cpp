#include "seam/platform/audio_input_device.hpp"

#include <atomic>
#include <chrono>
#include <stop_token>
#include <thread>
#include <vector>

namespace seam::platform {
namespace {

class ThreadedSilenceInputDevice final : public IAudioInputDevice {
public:
  ~ThreadedSilenceInputDevice() override { stop(); }

  core::Result<void> open(const AudioInputDeviceConfig& config,
                          IAudioInputProcessor& processor) override {
    if (config.sampleRate < 8000U || config.sampleRate > 384000U ||
        config.blockFrames == 0U || config.blockFrames > 16384U) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Audio input configuration is outside supported bounds");
    }
    if (running()) {
      return core::failure(core::ErrorCode::Conflict,
                           "Cannot reopen a running input device");
    }
    config_ = config;
    processor_ = &processor;
    mono_.assign(config.blockFrames, 0.0F);
    opened_ = true;
    return core::success();
  }

  core::Result<void> start() override {
    if (!opened_ || processor_ == nullptr) {
      return core::failure(core::ErrorCode::Conflict,
                           "Input device must be opened before start");
    }
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true,
                                          std::memory_order_acq_rel)) {
      return core::failure(core::ErrorCode::Conflict,
                           "Input device is already running");
    }
    worker_ = std::jthread([this](std::stop_token stopToken) {
      const auto duration = std::chrono::duration<double>(
          static_cast<double>(config_.blockFrames) /
          static_cast<double>(config_.sampleRate));
      while (!stopToken.stop_requested()) {
        const auto started = std::chrono::steady_clock::now();
        processor_->process(AudioInputProcessContext{
            .sampleRate = static_cast<double>(config_.sampleRate),
            .frameCount = config_.blockFrames,
            .mono = mono_,
        });
        callbacks_.fetch_add(1U, std::memory_order_relaxed);
        frames_.fetch_add(config_.blockFrames, std::memory_order_relaxed);
        std::this_thread::sleep_until(started +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration));
      }
      running_.store(false, std::memory_order_release);
    });
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
  AudioInputDeviceInfo info() const override {
    return AudioInputDeviceInfo{
        .backend = "Threaded Silence Input",
        .deviceName = "synthetic-silence",
        .sampleRate = config_.sampleRate,
        .blockFrames = config_.blockFrames,
        .physical = false,
    };
  }
  AudioInputDeviceStats stats() const noexcept override {
    return AudioInputDeviceStats{
        .callbacks = callbacks_.load(std::memory_order_relaxed),
        .frames = frames_.load(std::memory_order_relaxed),
        .readFailures = 0U,
    };
  }

private:
  AudioInputDeviceConfig config_;
  IAudioInputProcessor* processor_{nullptr};
  std::vector<float> mono_;
  std::jthread worker_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> callbacks_{0U};
  std::atomic<std::uint64_t> frames_{0U};
  bool opened_{false};
};

}  // namespace

std::unique_ptr<IAudioInputDevice> createThreadedSilenceInputDevice() {
  return std::make_unique<ThreadedSilenceInputDevice>();
}

}  // namespace seam::platform
