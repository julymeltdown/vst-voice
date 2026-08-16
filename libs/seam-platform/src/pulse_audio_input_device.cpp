#include "seam/platform/audio_input_device.hpp"

#if defined(SEAM_AUDIO_PULSE)

#include <atomic>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
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
using PulseSimpleRead = int (*)(PulseSimple*, void*, std::size_t, int*);
using PulseStrError = const char* (*)(int);

constexpr int kPulseRecord = 2;
constexpr int kPulseS16Le = 3;

class SharedLibrary final {
public:
  SharedLibrary() = default;
  explicit SharedLibrary(const char* name)
      : handle_(dlopen(name, RTLD_NOW | RTLD_LOCAL)) {}
  ~SharedLibrary() {
    if (handle_ != nullptr) dlclose(handle_);
  }
  SharedLibrary(const SharedLibrary&) = delete;
  SharedLibrary& operator=(const SharedLibrary&) = delete;
  SharedLibrary(SharedLibrary&& other) noexcept
      : handle_(std::exchange(other.handle_, nullptr)) {}
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

class PulseInputApi final {
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
    simpleRead = functionFromSymbol<PulseSimpleRead>(
        simpleLibrary_.symbol("pa_simple_read"));
    if (pulseLibrary_.valid()) {
      strError = functionFromSymbol<PulseStrError>(pulseLibrary_.symbol("pa_strerror"));
    }
    if (simpleNew == nullptr || simpleFree == nullptr || simpleRead == nullptr) {
      return core::failure(core::ErrorCode::Unsupported,
                           "PulseAudio Simple runtime is missing capture symbols");
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
  PulseSimpleRead simpleRead{nullptr};
  PulseStrError strError{nullptr};

private:
  SharedLibrary simpleLibrary_;
  SharedLibrary pulseLibrary_;
};

class PulseAudioInputDevice final : public IAudioInputDevice {
public:
  ~PulseAudioInputDevice() override {
    stop();
    closeHandle();
  }

  core::Result<void> open(const AudioInputDeviceConfig& config,
                          IAudioInputProcessor& processor) override {
    if (running()) {
      return core::failure(core::ErrorCode::Conflict,
                           "Cannot reopen a running PulseAudio input");
    }
    if (config.sampleRate < 8000U || config.sampleRate > 384000U ||
        config.blockFrames == 0U || config.blockFrames > 16384U) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "PulseAudio input configuration is outside supported bounds");
    }
    closeHandle();
    const auto loaded = api_.load();
    if (!loaded) return loaded;
    const PulseSampleSpec spec{
        .format = kPulseS16Le,
        .rate = config.sampleRate,
        .channels = 1U,
    };
    int error = 0;
    handle_ = api_.simpleNew(nullptr, config.applicationName.c_str(),
                             kPulseRecord, nullptr, config.streamName.c_str(),
                             &spec, nullptr, nullptr, &error);
    if (handle_ == nullptr) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to open the default PulseAudio input",
                           api_.errorText(error));
    }
    config_ = config;
    processor_ = &processor;
    raw_.assign(config.blockFrames, 0);
    mono_.assign(config.blockFrames, 0.0F);
    opened_ = true;
    return core::success();
  }

  core::Result<void> start() override {
    if (!opened_ || handle_ == nullptr || processor_ == nullptr) {
      return core::failure(core::ErrorCode::Conflict,
                           "PulseAudio input must be opened before start");
    }
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true,
                                          std::memory_order_acq_rel)) {
      return core::failure(core::ErrorCode::Conflict,
                           "PulseAudio input is already running");
    }
    try {
      worker_ = std::jthread([this](std::stop_token stopToken) {
        while (!stopToken.stop_requested()) {
          int error = 0;
          const auto bytes = raw_.size() * sizeof(std::int16_t);
          if (api_.simpleRead(handle_, raw_.data(), bytes, &error) < 0) {
            readFailures_.fetch_add(1U, std::memory_order_relaxed);
            break;
          }
          for (std::size_t index = 0U; index < raw_.size(); ++index) {
            mono_[index] = static_cast<float>(raw_[index]) / 32768.0F;
          }
          processor_->process(AudioInputProcessContext{
              .sampleRate = static_cast<double>(config_.sampleRate),
              .frameCount = config_.blockFrames,
              .mono = mono_,
          });
          callbacks_.fetch_add(1U, std::memory_order_relaxed);
          frames_.fetch_add(config_.blockFrames, std::memory_order_relaxed);
        }
        running_.store(false, std::memory_order_release);
      });
    } catch (...) {
      running_.store(false, std::memory_order_release);
      return core::failure(core::ErrorCode::Internal,
                           "Unable to create PulseAudio input thread");
    }
    return core::success();
  }

  void stop() noexcept override {
    if (worker_.joinable()) {
      worker_.request_stop();
      // pa_simple_read can block until the server provides the requested block.
      // A bounded block size keeps shutdown latency finite on a healthy server.
      worker_.join();
    }
    running_.store(false, std::memory_order_release);
  }

  bool running() const noexcept override {
    return running_.load(std::memory_order_acquire);
  }

  AudioInputDeviceInfo info() const override {
    return AudioInputDeviceInfo{
        .backend = "PulseAudio Simple Capture",
        .deviceName = "default",
        .sampleRate = config_.sampleRate,
        .blockFrames = config_.blockFrames,
        .physical = true,
    };
  }

  AudioInputDeviceStats stats() const noexcept override {
    return AudioInputDeviceStats{
        .callbacks = callbacks_.load(std::memory_order_relaxed),
        .frames = frames_.load(std::memory_order_relaxed),
        .readFailures = readFailures_.load(std::memory_order_relaxed),
    };
  }

private:
  void closeHandle() noexcept {
    if (handle_ != nullptr && api_.simpleFree != nullptr) api_.simpleFree(handle_);
    handle_ = nullptr;
    opened_ = false;
  }

  PulseInputApi api_;
  PulseSimple* handle_{nullptr};
  AudioInputDeviceConfig config_;
  IAudioInputProcessor* processor_{nullptr};
  std::vector<std::int16_t> raw_;
  std::vector<float> mono_;
  std::jthread worker_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> callbacks_{0U};
  std::atomic<std::uint64_t> frames_{0U};
  std::atomic<std::uint64_t> readFailures_{0U};
  bool opened_{false};
};

}  // namespace

std::unique_ptr<IAudioInputDevice> createSystemAudioInputDevice() {
  return std::make_unique<PulseAudioInputDevice>();
}

}  // namespace seam::platform

#endif
