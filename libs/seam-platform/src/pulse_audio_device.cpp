#include "seam/platform/audio_device.hpp"

#if defined(SEAM_AUDIO_PULSE)

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace seam::platform {
namespace {

struct PulseSimple;
struct PulseSampleSpec final {
  int format;
  std::uint32_t rate;
  std::uint8_t channels;
};

using PulseSimpleNew = PulseSimple* (*)(const char*, const char*, int,
                                        const char*, const char*,
                                        const PulseSampleSpec*, const void*,
                                        const void*, int*);
using PulseSimpleFree = void (*)(PulseSimple*);
using PulseSimpleWrite = int (*)(PulseSimple*, const void*, std::size_t, int*);
using PulseSimpleDrain = int (*)(PulseSimple*, int*);
using PulseStrError = const char* (*)(int);

constexpr int kPulsePlayback = 1;
constexpr int kPulseS16Le = 3;

class SharedLibrary final {
public:
  SharedLibrary() = default;
  explicit SharedLibrary(const char* name) : handle_(dlopen(name, RTLD_NOW | RTLD_LOCAL)) {}
  ~SharedLibrary() {
    if (handle_ != nullptr) dlclose(handle_);
  }
  SharedLibrary(const SharedLibrary&) = delete;
  SharedLibrary& operator=(const SharedLibrary&) = delete;
  SharedLibrary(SharedLibrary&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
  SharedLibrary& operator=(SharedLibrary&& other) noexcept {
    if (this == &other) return *this;
    if (handle_ != nullptr) dlclose(handle_);
    handle_ = std::exchange(other.handle_, nullptr);
    return *this;
  }
  [[nodiscard]] bool valid() const noexcept { return handle_ != nullptr; }
  [[nodiscard]] void* symbol(const char* name) const noexcept {
    return handle_ == nullptr ? nullptr : dlsym(handle_, name);
  }
private:
  void* handle_{nullptr};
};

template <typename Function>
Function functionFromSymbol(void* symbol) noexcept {
  Function function{};
  static_assert(sizeof(function) == sizeof(symbol));
  std::memcpy(&function, &symbol, sizeof(function));
  return function;
}

class PulseApi final {
public:
  core::Result<void> load() {
    simpleLibrary_ = SharedLibrary{"libpulse-simple.so.0"};
    pulseLibrary_ = SharedLibrary{"libpulse.so.0"};
    if (!simpleLibrary_.valid()) {
      return core::failure(core::ErrorCode::Unsupported,
                           "PulseAudio Simple runtime library is unavailable");
    }
    simpleNew = functionFromSymbol<PulseSimpleNew>(
        simpleLibrary_.symbol("pa_simple_new"));
    simpleFree = functionFromSymbol<PulseSimpleFree>(
        simpleLibrary_.symbol("pa_simple_free"));
    simpleWrite = functionFromSymbol<PulseSimpleWrite>(
        simpleLibrary_.symbol("pa_simple_write"));
    simpleDrain = functionFromSymbol<PulseSimpleDrain>(
        simpleLibrary_.symbol("pa_simple_drain"));
    if (pulseLibrary_.valid()) {
      strError = functionFromSymbol<PulseStrError>(
          pulseLibrary_.symbol("pa_strerror"));
    }
    if (simpleNew == nullptr || simpleFree == nullptr || simpleWrite == nullptr ||
        simpleDrain == nullptr) {
      return core::failure(core::ErrorCode::Unsupported,
                           "PulseAudio Simple runtime is missing required symbols");
    }
    return core::success();
  }

  [[nodiscard]] std::string errorText(int error) const {
    if (strError == nullptr) return "PulseAudio error " + std::to_string(error);
    const auto* text = strError(error);
    return text == nullptr ? "PulseAudio error " + std::to_string(error)
                           : std::string{text};
  }

  PulseSimpleNew simpleNew{nullptr};
  PulseSimpleFree simpleFree{nullptr};
  PulseSimpleWrite simpleWrite{nullptr};
  PulseSimpleDrain simpleDrain{nullptr};
  PulseStrError strError{nullptr};

private:
  SharedLibrary simpleLibrary_;
  SharedLibrary pulseLibrary_;
};

core::Result<void> validateConfig(const AudioDeviceConfig& config) {
  if (config.sampleRate < 8000U || config.sampleRate > 384000U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Audio sample rate is outside the supported range");
  }
  if (config.blockFrames == 0U || config.blockFrames > 16384U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Audio block size is outside the supported range");
  }
  if (config.outputChannels == 0U || config.outputChannels > 2U) {
    return core::failure(core::ErrorCode::Unsupported,
                         "PulseAudio adapter supports mono or stereo output");
  }
  return core::success();
}

std::int16_t floatToS16(float value) noexcept {
  const auto clamped = std::clamp(std::isfinite(value) ? value : 0.0F,
                                  -1.0F, 1.0F);
  return static_cast<std::int16_t>(std::lround(
      static_cast<double>(clamped) *
      static_cast<double>(std::numeric_limits<std::int16_t>::max())));
}

class PulseAudioDevice final : public IAudioDevice {
public:
  ~PulseAudioDevice() override {
    stop();
    closeHandle();
  }

  core::Result<void> open(const AudioDeviceConfig& config,
                          IAudioProcessor& processor) override {
    if (running()) {
      return core::failure(core::ErrorCode::Conflict,
                           "Cannot reopen a running PulseAudio device");
    }
    closeHandle();
    const auto valid = validateConfig(config);
    if (!valid) return valid;
    const auto loaded = api_.load();
    if (!loaded) return loaded;

    const PulseSampleSpec spec{
        .format = kPulseS16Le,
        .rate = config.sampleRate,
        .channels = config.outputChannels,
    };
    int error = 0;
    handle_ = api_.simpleNew(nullptr, config.applicationName.c_str(),
                             kPulsePlayback, nullptr, config.streamName.c_str(),
                             &spec, nullptr, nullptr, &error);
    if (handle_ == nullptr) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to open the default PulseAudio output",
                           api_.errorText(error));
    }
    config_ = config;
    processor_ = &processor;
    left_.assign(config.blockFrames, 0.0F);
    right_.assign(config.blockFrames, 0.0F);
    interleaved_.assign(config.blockFrames * config.outputChannels, 0);
    opened_ = true;
    return core::success();
  }

  core::Result<void> start() override {
    if (!opened_ || handle_ == nullptr || processor_ == nullptr) {
      return core::failure(core::ErrorCode::Conflict,
                           "PulseAudio device must be opened before start");
    }
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true,
                                          std::memory_order_acq_rel)) {
      return core::failure(core::ErrorCode::Conflict,
                           "PulseAudio device is already running");
    }
    try {
      worker_ = std::jthread([this](std::stop_token stopToken) {
        while (!stopToken.stop_requested()) {
          processor_->process(AudioProcessContext{
              .sampleRate = static_cast<double>(config_.sampleRate),
              .frameCount = config_.blockFrames,
              .left = left_,
              .right = right_,
          });
          for (std::size_t frame = 0U; frame < config_.blockFrames; ++frame) {
            interleaved_[frame * config_.outputChannels] = floatToS16(left_[frame]);
            if (config_.outputChannels == 2U) {
              interleaved_[frame * 2U + 1U] = floatToS16(right_[frame]);
            }
          }
          int error = 0;
          const auto bytes = interleaved_.size() * sizeof(std::int16_t);
          if (api_.simpleWrite(handle_, interleaved_.data(), bytes, &error) < 0) {
            writeFailures_.fetch_add(1U, std::memory_order_relaxed);
            lastError_.store(error, std::memory_order_relaxed);
            break;
          }
          callbacks_.fetch_add(1U, std::memory_order_relaxed);
          frames_.fetch_add(config_.blockFrames, std::memory_order_relaxed);
        }
        running_.store(false, std::memory_order_release);
      });
    } catch (...) {
      running_.store(false, std::memory_order_release);
      return core::failure(core::ErrorCode::Internal,
                           "Unable to create PulseAudio callback thread");
    }
    return core::success();
  }

  void stop() noexcept override {
    if (worker_.joinable()) {
      worker_.request_stop();
      worker_.join();
    }
    running_.store(false, std::memory_order_release);
    if (handle_ != nullptr && api_.simpleDrain != nullptr) {
      int error = 0;
      if (api_.simpleDrain(handle_, &error) < 0) {
        writeFailures_.fetch_add(1U, std::memory_order_relaxed);
        lastError_.store(error, std::memory_order_relaxed);
      }
    }
  }

  bool running() const noexcept override {
    return running_.load(std::memory_order_acquire);
  }

  AudioDeviceInfo info() const override {
    return AudioDeviceInfo{
        .backend = "PulseAudio Simple",
        .deviceName = "default",
        .sampleRate = config_.sampleRate,
        .blockFrames = config_.blockFrames,
        .outputChannels = config_.outputChannels,
        .physical = true,
    };
  }

  AudioDeviceStats stats() const noexcept override {
    return AudioDeviceStats{
        .callbacks = callbacks_.load(std::memory_order_relaxed),
        .frames = frames_.load(std::memory_order_relaxed),
        .writeFailures = writeFailures_.load(std::memory_order_relaxed),
        .xruns = 0U,
    };
  }

private:
  void closeHandle() noexcept {
    if (handle_ != nullptr && api_.simpleFree != nullptr) {
      api_.simpleFree(handle_);
    }
    handle_ = nullptr;
    opened_ = false;
  }

  PulseApi api_;
  PulseSimple* handle_{nullptr};
  AudioDeviceConfig config_;
  IAudioProcessor* processor_{nullptr};
  std::vector<float> left_;
  std::vector<float> right_;
  std::vector<std::int16_t> interleaved_;
  std::jthread worker_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> callbacks_{0U};
  std::atomic<std::uint64_t> frames_{0U};
  std::atomic<std::uint64_t> writeFailures_{0U};
  std::atomic<int> lastError_{0};
  bool opened_{false};
};

}  // namespace

std::unique_ptr<IAudioDevice> createSystemAudioDevice() {
  return std::make_unique<PulseAudioDevice>();
}

}  // namespace seam::platform

#endif
