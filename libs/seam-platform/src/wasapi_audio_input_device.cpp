#include "seam/platform/audio_input_device.hpp"

#if defined(SEAM_AUDIO_WASAPI)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace seam::platform {
namespace {

using Microsoft::WRL::ComPtr;

std::string hresultText(HRESULT value) {
  std::ostringstream stream;
  stream << "HRESULT 0x" << std::hex << std::uppercase
         << static_cast<unsigned long>(value);
  return stream.str();
}

WAVEFORMATEXTENSIBLE requestedFormat(
    const AudioInputDeviceConfig& config) noexcept {
  WAVEFORMATEXTENSIBLE format{};
  format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  format.Format.nChannels = 1U;
  format.Format.nSamplesPerSec = config.sampleRate;
  format.Format.wBitsPerSample = 32U;
  format.Format.nBlockAlign = sizeof(float);
  format.Format.nAvgBytesPerSec = config.sampleRate * sizeof(float);
  format.Format.cbSize = static_cast<WORD>(
      sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX));
  format.Samples.wValidBitsPerSample = 32U;
  format.dwChannelMask = SPEAKER_FRONT_CENTER;
  format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
  return format;
}

class WasapiAudioInputDevice final : public IAudioInputDevice {
public:
  ~WasapiAudioInputDevice() override {
    stop();
    close();
  }

  core::Result<void> open(const AudioInputDeviceConfig& config,
                          IAudioInputProcessor& processor) override {
    if (running()) {
      return core::failure(core::ErrorCode::Conflict,
                           "Cannot reopen a running WASAPI input");
    }
    if (config.sampleRate < 8000U || config.sampleRate > 384000U ||
        config.blockFrames == 0U || config.blockFrames > 16384U) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "WASAPI input configuration is outside supported bounds");
    }
    close();
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(initialized)) {
      comInitialized_ = true;
    } else if (initialized != RPC_E_CHANGED_MODE) {
      return core::failure(core::ErrorCode::Internal,
                           "Unable to initialize COM for WASAPI capture",
                           hresultText(initialized));
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    auto result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                   CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(result)) return failOpen("Unable to create WASAPI device enumerator", result);
    result = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device_);
    if (FAILED(result)) return failOpen("Unable to obtain default WASAPI input", result);
    result = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                               reinterpret_cast<void**>(client_.ReleaseAndGetAddressOf()));
    if (FAILED(result)) return failOpen("Unable to activate WASAPI capture client", result);

    auto format = requestedFormat(config);
    constexpr DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    result = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0,
                                 &format.Format, nullptr);
    if (FAILED(result)) {
      return failOpen("Unable to initialize shared-mode WASAPI mono capture", result);
    }
    result = client_->GetBufferSize(&bufferFrames_);
    if (FAILED(result) || bufferFrames_ == 0U) {
      return failOpen("Unable to query WASAPI capture buffer size", result);
    }
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event_ == nullptr) {
      return failOpen("Unable to create WASAPI capture event",
                      HRESULT_FROM_WIN32(GetLastError()));
    }
    result = client_->SetEventHandle(event_);
    if (FAILED(result)) return failOpen("Unable to assign WASAPI capture event", result);
    result = client_->GetService(IID_PPV_ARGS(&captureClient_));
    if (FAILED(result)) return failOpen("Unable to obtain WASAPI capture client", result);

    config_ = config;
    processor_ = &processor;
    silence_.assign(bufferFrames_, 0.0F);
    opened_ = true;
    return core::success();
  }

  core::Result<void> start() override {
    if (!opened_ || client_ == nullptr || captureClient_ == nullptr ||
        processor_ == nullptr || event_ == nullptr) {
      return core::failure(core::ErrorCode::Conflict,
                           "WASAPI input must be opened before start");
    }
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true,
                                          std::memory_order_acq_rel)) {
      return core::failure(core::ErrorCode::Conflict,
                           "WASAPI input is already running");
    }
    const auto started = client_->Start();
    if (FAILED(started)) {
      running_.store(false, std::memory_order_release);
      return core::failure(core::ErrorCode::IoError,
                           "Unable to start WASAPI capture",
                           hresultText(started));
    }
    try {
      worker_ = std::jthread([this](std::stop_token stopToken) {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        DWORD taskIndex = 0U;
        HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
        while (!stopToken.stop_requested()) {
          const auto wait = WaitForSingleObject(event_, 100U);
          if (wait == WAIT_TIMEOUT) continue;
          if (wait != WAIT_OBJECT_0) {
            readFailures_.fetch_add(1U, std::memory_order_relaxed);
            break;
          }
          UINT32 packetFrames = 0U;
          auto result = captureClient_->GetNextPacketSize(&packetFrames);
          if (FAILED(result)) {
            readFailures_.fetch_add(1U, std::memory_order_relaxed);
            break;
          }
          while (packetFrames > 0U && !stopToken.stop_requested()) {
            BYTE* bytes = nullptr;
            UINT32 frames = 0U;
            DWORD flags = 0U;
            result = captureClient_->GetBuffer(&bytes, &frames, &flags,
                                               nullptr, nullptr);
            if (FAILED(result)) {
              readFailures_.fetch_add(1U, std::memory_order_relaxed);
              packetFrames = 0U;
              break;
            }
            std::span<const float> mono;
            if (frames > silence_.size()) {
              static_cast<void>(captureClient_->ReleaseBuffer(frames));
              readFailures_.fetch_add(1U, std::memory_order_relaxed);
              packetFrames = 0U;
              break;
            }
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0U || bytes == nullptr) {
              std::fill_n(silence_.begin(), frames, 0.0F);
              mono = std::span<const float>{silence_.data(), frames};
            } else {
              mono = std::span<const float>{reinterpret_cast<const float*>(bytes), frames};
            }
            if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0U) {
              readFailures_.fetch_add(1U, std::memory_order_relaxed);
            }
            processor_->process(AudioInputProcessContext{
                .sampleRate = static_cast<double>(config_.sampleRate),
                .frameCount = frames,
                .mono = mono,
            });
            static_cast<void>(captureClient_->ReleaseBuffer(frames));
            callbacks_.fetch_add(1U, std::memory_order_relaxed);
            processedFrames_.fetch_add(frames, std::memory_order_relaxed);
            result = captureClient_->GetNextPacketSize(&packetFrames);
            if (FAILED(result)) {
              readFailures_.fetch_add(1U, std::memory_order_relaxed);
              packetFrames = 0U;
            }
          }
        }
        if (task != nullptr) AvRevertMmThreadCharacteristics(task);
        if (SUCCEEDED(initialized)) CoUninitialize();
        running_.store(false, std::memory_order_release);
      });
    } catch (...) {
      static_cast<void>(client_->Stop());
      running_.store(false, std::memory_order_release);
      return core::failure(core::ErrorCode::Internal,
                           "Unable to create WASAPI capture worker");
    }
    return core::success();
  }

  void stop() noexcept override {
    if (worker_.joinable()) {
      worker_.request_stop();
      if (event_ != nullptr) SetEvent(event_);
      worker_.join();
    }
    if (client_ != nullptr) {
      static_cast<void>(client_->Stop());
      static_cast<void>(client_->Reset());
    }
    running_.store(false, std::memory_order_release);
  }

  bool running() const noexcept override {
    return running_.load(std::memory_order_acquire);
  }

  AudioInputDeviceInfo info() const override {
    return AudioInputDeviceInfo{
        .backend = "WASAPI shared event-driven capture",
        .deviceName = "default-capture-endpoint",
        .sampleRate = config_.sampleRate,
        .blockFrames = config_.blockFrames,
        .physical = true,
    };
  }

  AudioInputDeviceStats stats() const noexcept override {
    return AudioInputDeviceStats{
        .callbacks = callbacks_.load(std::memory_order_relaxed),
        .frames = processedFrames_.load(std::memory_order_relaxed),
        .readFailures = readFailures_.load(std::memory_order_relaxed),
    };
  }

private:
  core::Result<void> failOpen(std::string message, HRESULT result) {
    close();
    return core::failure(core::ErrorCode::IoError, std::move(message),
                         hresultText(result));
  }

  void close() noexcept {
    captureClient_.Reset();
    client_.Reset();
    device_.Reset();
    if (event_ != nullptr) {
      CloseHandle(event_);
      event_ = nullptr;
    }
    if (comInitialized_) {
      CoUninitialize();
      comInitialized_ = false;
    }
    processor_ = nullptr;
    silence_.clear();
    bufferFrames_ = 0U;
    opened_ = false;
  }

  AudioInputDeviceConfig config_;
  IAudioInputProcessor* processor_{nullptr};
  ComPtr<IMMDevice> device_;
  ComPtr<IAudioClient> client_;
  ComPtr<IAudioCaptureClient> captureClient_;
  HANDLE event_{nullptr};
  UINT32 bufferFrames_{0U};
  std::vector<float> silence_;
  std::jthread worker_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> callbacks_{0U};
  std::atomic<std::uint64_t> processedFrames_{0U};
  std::atomic<std::uint64_t> readFailures_{0U};
  bool opened_{false};
  bool comInitialized_{false};
};

}  // namespace

std::unique_ptr<IAudioInputDevice> createSystemAudioInputDevice() {
  return std::make_unique<WasapiAudioInputDevice>();
}

}  // namespace seam::platform

#endif
