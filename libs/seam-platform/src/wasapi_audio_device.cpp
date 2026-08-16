#include "seam/platform/audio_device.hpp"

#if defined(SEAM_AUDIO_WASAPI)

#include "seam/domain/routing.hpp"

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
#include <chrono>
#include <cmath>
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

core::Result<void> validateConfig(const AudioDeviceConfig& config) {
  if (config.sampleRate < 8000U || config.sampleRate > 384000U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "WASAPI sample rate is outside supported bounds");
  }
  if (config.blockFrames == 0U || config.blockFrames > 16384U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "WASAPI block size is outside supported bounds");
  }
  if (config.outputChannels == 0U ||
      config.outputChannels > domain::kMaximumAudioChannels) {
    return core::failure(core::ErrorCode::Unsupported,
                         "WASAPI output channel count is outside supported bounds");
  }
  return core::success();
}

DWORD channelMask(std::uint8_t channels) noexcept {
  switch (channels) {
    case 1U: return SPEAKER_FRONT_CENTER;
    case 2U: return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    case 3U: return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT |
                    SPEAKER_FRONT_CENTER;
    case 4U: return KSAUDIO_SPEAKER_QUAD;
    case 5U: return KSAUDIO_SPEAKER_SURROUND;
    case 6U: return KSAUDIO_SPEAKER_5POINT1;
    case 7U: return KSAUDIO_SPEAKER_7POINT0;
    case 8U: return KSAUDIO_SPEAKER_7POINT1;
    default: return 0U;
  }
}

WAVEFORMATEXTENSIBLE requestedFormat(const AudioDeviceConfig& config) noexcept {
  WAVEFORMATEXTENSIBLE format{};
  format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  format.Format.nChannels = config.outputChannels;
  format.Format.nSamplesPerSec = config.sampleRate;
  format.Format.wBitsPerSample = 32U;
  format.Format.nBlockAlign = static_cast<WORD>(
      config.outputChannels * sizeof(float));
  format.Format.nAvgBytesPerSec =
      format.Format.nSamplesPerSec * format.Format.nBlockAlign;
  format.Format.cbSize = static_cast<WORD>(
      sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX));
  format.Samples.wValidBitsPerSample = 32U;
  format.dwChannelMask = channelMask(config.outputChannels);
  format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
  return format;
}

class WasapiAudioDevice final : public IAudioDevice {
public:
  ~WasapiAudioDevice() override {
    stop();
    close();
  }

  core::Result<void> open(const AudioDeviceConfig& config,
                          IAudioProcessor& processor) override {
    if (running()) {
      return core::failure(core::ErrorCode::Conflict,
                           "Cannot reopen a running WASAPI device");
    }
    close();
    const auto valid = validateConfig(config);
    if (!valid) return valid;

    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(initialized)) {
      comInitialized_ = true;
    } else if (initialized != RPC_E_CHANGED_MODE) {
      return core::failure(core::ErrorCode::Internal,
                           "Unable to initialize COM for WASAPI",
                           hresultText(initialized));
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    auto result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                   CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(result)) return failOpen("Unable to create WASAPI device enumerator", result);
    result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
    if (FAILED(result)) return failOpen("Unable to obtain default WASAPI output", result);
    result = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                               reinterpret_cast<void**>(client_.ReleaseAndGetAddressOf()));
    if (FAILED(result)) return failOpen("Unable to activate WASAPI audio client", result);

    auto format = requestedFormat(config);
    constexpr DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    result = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0,
                                 &format.Format, nullptr);
    if (FAILED(result)) {
      return failOpen("Unable to initialize shared-mode WASAPI float stream", result);
    }
    result = client_->GetBufferSize(&bufferFrames_);
    if (FAILED(result) || bufferFrames_ == 0U) {
      return failOpen("Unable to query WASAPI buffer size", result);
    }
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event_ == nullptr) {
      return failOpen("Unable to create WASAPI callback event",
                      HRESULT_FROM_WIN32(GetLastError()));
    }
    result = client_->SetEventHandle(event_);
    if (FAILED(result)) return failOpen("Unable to assign WASAPI callback event", result);
    result = client_->GetService(IID_PPV_ARGS(&renderClient_));
    if (FAILED(result)) return failOpen("Unable to obtain WASAPI render client", result);

    config_ = config;
    processor_ = &processor;
    channels_.assign(config.outputChannels,
                     std::vector<float>(bufferFrames_, 0.0F));
    outputViews_.resize(config.outputChannels);
    opened_ = true;
    return core::success();
  }

  core::Result<void> start() override {
    if (!opened_ || client_ == nullptr || renderClient_ == nullptr ||
        processor_ == nullptr || event_ == nullptr) {
      return core::failure(core::ErrorCode::Conflict,
                           "WASAPI device must be opened before start");
    }
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true,
                                          std::memory_order_acq_rel)) {
      return core::failure(core::ErrorCode::Conflict,
                           "WASAPI device is already running");
    }
    const auto started = client_->Start();
    if (FAILED(started)) {
      running_.store(false, std::memory_order_release);
      return core::failure(core::ErrorCode::IoError,
                           "Unable to start WASAPI output",
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
            writeFailures_.fetch_add(1U, std::memory_order_relaxed);
            break;
          }
          UINT32 padding = 0U;
          auto result = client_->GetCurrentPadding(&padding);
          if (FAILED(result) || padding > bufferFrames_) {
            writeFailures_.fetch_add(1U, std::memory_order_relaxed);
            break;
          }
          UINT32 available = bufferFrames_ - padding;
          while (available > 0U && !stopToken.stop_requested()) {
            const auto frames = std::min<UINT32>(
                available, static_cast<UINT32>(config_.blockFrames));
            BYTE* bytes = nullptr;
            result = renderClient_->GetBuffer(frames, &bytes);
            if (FAILED(result) || bytes == nullptr) {
              writeFailures_.fetch_add(1U, std::memory_order_relaxed);
              available = 0U;
              break;
            }
            for (std::size_t channel = 0U; channel < channels_.size(); ++channel) {
              outputViews_[channel] = std::span<float>{channels_[channel].data(), frames};
              std::fill(outputViews_[channel].begin(), outputViews_[channel].end(), 0.0F);
            }
            processor_->process(AudioProcessContext{
                .sampleRate = static_cast<double>(config_.sampleRate),
                .frameCount = frames,
                .left = outputViews_.empty() ? std::span<float>{} : outputViews_[0],
                .right = outputViews_.size() < 2U ? std::span<float>{} : outputViews_[1],
                .outputs = outputViews_,
            });
            auto* interleaved = reinterpret_cast<float*>(bytes);
            for (UINT32 frame = 0U; frame < frames; ++frame) {
              for (std::size_t channel = 0U; channel < channels_.size(); ++channel) {
                const auto sample = channels_[channel][frame];
                interleaved[static_cast<std::size_t>(frame) * channels_.size() + channel] =
                    std::clamp(std::isfinite(sample) ? sample : 0.0F, -1.0F, 1.0F);
              }
            }
            result = renderClient_->ReleaseBuffer(frames, 0U);
            if (FAILED(result)) {
              writeFailures_.fetch_add(1U, std::memory_order_relaxed);
              available = 0U;
              break;
            }
            callbacks_.fetch_add(1U, std::memory_order_relaxed);
            processedFrames_.fetch_add(frames, std::memory_order_relaxed);
            available -= frames;
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
                           "Unable to create WASAPI callback worker");
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

  AudioDeviceInfo info() const override {
    return AudioDeviceInfo{
        .backend = "WASAPI shared event-driven",
        .deviceName = "default-render-endpoint",
        .sampleRate = config_.sampleRate,
        .blockFrames = config_.blockFrames,
        .outputChannels = config_.outputChannels,
        .physical = true,
    };
  }

  AudioDeviceStats stats() const noexcept override {
    return AudioDeviceStats{
        .callbacks = callbacks_.load(std::memory_order_relaxed),
        .frames = processedFrames_.load(std::memory_order_relaxed),
        .writeFailures = writeFailures_.load(std::memory_order_relaxed),
        .xruns = xruns_.load(std::memory_order_relaxed),
    };
  }

private:
  core::Result<void> failOpen(std::string message, HRESULT result) {
    close();
    return core::failure(core::ErrorCode::IoError, std::move(message),
                         hresultText(result));
  }

  void close() noexcept {
    renderClient_.Reset();
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
    opened_ = false;
    processor_ = nullptr;
    channels_.clear();
    outputViews_.clear();
    bufferFrames_ = 0U;
  }

  AudioDeviceConfig config_;
  IAudioProcessor* processor_{nullptr};
  ComPtr<IMMDevice> device_;
  ComPtr<IAudioClient> client_;
  ComPtr<IAudioRenderClient> renderClient_;
  HANDLE event_{nullptr};
  UINT32 bufferFrames_{0U};
  std::vector<std::vector<float>> channels_;
  std::vector<std::span<float>> outputViews_;
  std::jthread worker_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> callbacks_{0U};
  std::atomic<std::uint64_t> processedFrames_{0U};
  std::atomic<std::uint64_t> writeFailures_{0U};
  std::atomic<std::uint64_t> xruns_{0U};
  bool opened_{false};
  bool comInitialized_{false};
};

}  // namespace

std::unique_ptr<IAudioDevice> createSystemAudioDevice() {
  return std::make_unique<WasapiAudioDevice>();
}

}  // namespace seam::platform

#endif
