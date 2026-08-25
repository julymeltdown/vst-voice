#include "seam/platform/audio_device.hpp"

#if defined(SEAM_AUDIO_COREAUDIO)

#include "seam/domain/routing.hpp"

#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace seam::platform {
namespace {

std::string statusText(OSStatus status) {
  const auto value = static_cast<std::uint32_t>(status);
  char text[5]{
      static_cast<char>((value >> 24U) & 0xFFU),
      static_cast<char>((value >> 16U) & 0xFFU),
      static_cast<char>((value >> 8U) & 0xFFU),
      static_cast<char>(value & 0xFFU),
      '\0'};
  const bool printable = std::all_of(text, text + 4, [](unsigned char c) {
    return c >= 32U && c <= 126U;
  });
  return printable ? std::string{text} : std::to_string(status);
}

AudioStreamBasicDescription streamDescription(
    const AudioDeviceConfig& config) noexcept {
  AudioStreamBasicDescription format{};
  format.mSampleRate = static_cast<Float64>(config.sampleRate);
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags =
      static_cast<AudioFormatFlags>(kAudioFormatFlagsNativeFloatPacked) |
      static_cast<AudioFormatFlags>(kAudioFormatFlagIsNonInterleaved);
  format.mBytesPerPacket = sizeof(Float32);
  format.mFramesPerPacket = 1U;
  format.mBytesPerFrame = sizeof(Float32);
  format.mChannelsPerFrame = config.outputChannels;
  format.mBitsPerChannel = 32U;
  return format;
}

AudioDeviceID resolveDevice(const std::string& requested) {
  AudioDeviceID defaultDevice = kAudioObjectUnknown;
  AudioObjectPropertyAddress defaultAddress{
      kAudioHardwarePropertyDefaultOutputDevice,
      kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMain,
  };
  UInt32 defaultSize = sizeof(defaultDevice);
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultAddress, 0U,
                                 nullptr, &defaultSize,
                                 &defaultDevice) != noErr) {
    return kAudioObjectUnknown;
  }
  if (requested.empty()) return defaultDevice;

  AudioObjectPropertyAddress address{
      kAudioHardwarePropertyDevices,
      kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMain,
  };
  UInt32 size = 0U;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0U,
                                     nullptr, &size) != noErr ||
      size == 0U || size % sizeof(AudioDeviceID) != 0U) {
    return kAudioObjectUnknown;
  }
  std::vector<AudioDeviceID> devices(size / sizeof(AudioDeviceID));
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0U,
                                 nullptr, &size, devices.data()) != noErr) {
    return kAudioObjectUnknown;
  }
  for (const auto device : devices) {
    CFStringRef value = nullptr;
    AudioObjectPropertyAddress uidAddress{
        kAudioDevicePropertyDeviceUID,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    UInt32 uidSize = sizeof(value);
    if (AudioObjectGetPropertyData(device, &uidAddress, 0U, nullptr, &uidSize,
                                   &value) == noErr) {
      std::array<char, 1024> buffer{};
      const auto matches = value != nullptr &&
                           CFStringGetCString(value, buffer.data(),
                                              buffer.size(),
                                              kCFStringEncodingUTF8) &&
                           requested == buffer.data();
      if (value != nullptr) CFRelease(value);
      if (matches) return device;
    }
    if (requested == "coreaudio:" + std::to_string(device)) return device;
  }
  return kAudioObjectUnknown;
}

UInt32 deviceBufferFrameSize(AudioDeviceID device,
                             UInt32 fallback) noexcept {
  AudioObjectPropertyAddress address{
      kAudioDevicePropertyBufferFrameSize,
      kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMain,
  };
  UInt32 frames = 0U;
  UInt32 size = sizeof(frames);
  const auto status = AudioObjectGetPropertyData(
      device, &address, 0U, nullptr, &size, &frames);
  return status == noErr && frames != 0U ? frames : fallback;
}

class CoreAudioDevice final : public IAudioDevice {
public:
  ~CoreAudioDevice() override {
    stop();
    close();
  }

  core::Result<void> open(const AudioDeviceConfig& config,
                          IAudioProcessor& processor) override {
    if (running()) {
      return core::failure(core::ErrorCode::Conflict,
                           "Cannot reopen a running CoreAudio device");
    }
    if (config.sampleRate < 8000U || config.sampleRate > 384000U ||
        config.blockFrames == 0U || config.blockFrames > 16384U ||
        config.outputChannels == 0U ||
        config.outputChannels > domain::kMaximumAudioChannels) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "CoreAudio output configuration is outside supported bounds");
    }
    close();
    const auto device = resolveDevice(config.deviceId);
    if (device == kAudioObjectUnknown) {
      return core::failure(
          core::ErrorCode::NotFound,
          config.deviceId.empty()
              ? "Default CoreAudio output device is unavailable"
              : "Requested CoreAudio output device is unavailable");
    }
    auto maximumFrames = std::max(
        static_cast<UInt32>(config.blockFrames),
        deviceBufferFrameSize(device,
                              static_cast<UInt32>(config.blockFrames)));
    if (maximumFrames > 16384U) {
      return core::failure(
          core::ErrorCode::Unsupported,
          "CoreAudio device callback slice exceeds the supported maximum");
    }
    AudioComponentDescription description{
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_DefaultOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags = 0U,
        .componentFlagsMask = 0U,
    };
    const auto component = AudioComponentFindNext(nullptr, &description);
    if (component == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Default CoreAudio output component is unavailable");
    }
    auto status = AudioComponentInstanceNew(component, &unit_);
    if (status != noErr || unit_ == nullptr) {
      close();
      return core::failure(core::ErrorCode::IoError,
                           "Unable to create CoreAudio output unit",
                           statusText(status));
    }
    status = AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice,
                                  kAudioUnitScope_Global, 0U, &device,
                                  sizeof(device));
    if (status != noErr) {
      return failOpen("Unable to select CoreAudio output device", status);
    }
    auto format = streamDescription(config);
    status = AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input, 0U, &format,
                                  sizeof(format));
    if (status != noErr) return failOpen("Unable to set CoreAudio output format", status);
    status = AudioUnitSetProperty(unit_, kAudioUnitProperty_MaximumFramesPerSlice,
                                  kAudioUnitScope_Global, 0U, &maximumFrames,
                                  sizeof(maximumFrames));
    if (status != noErr) return failOpen("Unable to set CoreAudio maximum slice", status);
    AURenderCallbackStruct callback{
        .inputProc = &CoreAudioDevice::renderCallback,
        .inputProcRefCon = this,
    };
    status = AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback,
                                  kAudioUnitScope_Input, 0U, &callback,
                                  sizeof(callback));
    if (status != noErr) return failOpen("Unable to install CoreAudio callback", status);
    status = AudioUnitInitialize(unit_);
    if (status != noErr) return failOpen("Unable to initialize CoreAudio output", status);
    config_ = config;
    maximumFramesPerSlice_ = maximumFrames;
    if (config_.deviceId.empty()) {
      config_.deviceId = "coreaudio:" + std::to_string(device);
    }
    processor_ = &processor;
    opened_ = true;
    return core::success();
  }

  core::Result<void> start() override {
    if (!opened_ || unit_ == nullptr || processor_ == nullptr) {
      return core::failure(core::ErrorCode::Conflict,
                           "CoreAudio device must be opened before start");
    }
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true,
                                          std::memory_order_acq_rel)) {
      return core::failure(core::ErrorCode::Conflict,
                           "CoreAudio device is already running");
    }
    const auto status = AudioOutputUnitStart(unit_);
    if (status != noErr) {
      running_.store(false, std::memory_order_release);
      return core::failure(core::ErrorCode::IoError,
                           "Unable to start CoreAudio output",
                           statusText(status));
    }
    return core::success();
  }

  void stop() noexcept override {
    if (unit_ != nullptr && running()) {
      static_cast<void>(AudioOutputUnitStop(unit_));
    }
    running_.store(false, std::memory_order_release);
  }

  bool running() const noexcept override {
    return running_.load(std::memory_order_acquire);
  }

  AudioDeviceInfo info() const override {
    return AudioDeviceInfo{
        .backend = "CoreAudio DefaultOutput AudioUnit",
        .deviceId = config_.deviceId,
        .deviceName = "default-output-device",
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
        .writeFailures = failures_.load(std::memory_order_relaxed),
        .xruns = 0U,
    };
  }

private:
  static OSStatus renderCallback(void* reference,
                                 AudioUnitRenderActionFlags*,
                                 const AudioTimeStamp*, UInt32,
                                 UInt32 frameCount,
                                 AudioBufferList* output) noexcept {
    auto* self = static_cast<CoreAudioDevice*>(reference);
    if (self == nullptr || output == nullptr || self->processor_ == nullptr) {
      return kAudio_ParamError;
    }
    const auto channels = static_cast<std::size_t>(self->config_.outputChannels);
    if (frameCount > self->maximumFramesPerSlice_ ||
        output->mNumberBuffers < channels) {
      for (UInt32 index = 0U; index < output->mNumberBuffers; ++index) {
        if (output->mBuffers[index].mData != nullptr) {
          std::fill_n(static_cast<float*>(output->mBuffers[index].mData),
                      output->mBuffers[index].mDataByteSize / sizeof(float),
                      0.0F);
        }
      }
      self->failures_.fetch_add(1U, std::memory_order_relaxed);
      return noErr;
    }
    for (std::size_t channel = 0U; channel < channels; ++channel) {
      auto& buffer = output->mBuffers[channel];
      if (buffer.mData == nullptr ||
          buffer.mDataByteSize < frameCount * sizeof(float)) {
        self->failures_.fetch_add(1U, std::memory_order_relaxed);
        return kAudio_ParamError;
      }
      self->views_[channel] = std::span<float>{
          static_cast<float*>(buffer.mData), frameCount};
      std::fill(self->views_[channel].begin(), self->views_[channel].end(), 0.0F);
    }
    auto views = std::span<std::span<float>>{self->views_.data(), channels};
    self->processor_->process(AudioProcessContext{
        .sampleRate = static_cast<double>(self->config_.sampleRate),
        .frameCount = frameCount,
        .left = views.empty() ? std::span<float>{} : views[0],
        .right = views.size() < 2U ? std::span<float>{} : views[1],
        .outputs = views,
    });
    self->callbacks_.fetch_add(1U, std::memory_order_relaxed);
    self->frames_.fetch_add(frameCount, std::memory_order_relaxed);
    return noErr;
  }

  core::Result<void> failOpen(std::string message, OSStatus status) {
    close();
    return core::failure(core::ErrorCode::IoError, std::move(message),
                         statusText(status));
  }

  void close() noexcept {
    if (unit_ != nullptr) {
      static_cast<void>(AudioUnitUninitialize(unit_));
      static_cast<void>(AudioComponentInstanceDispose(unit_));
      unit_ = nullptr;
    }
    opened_ = false;
    processor_ = nullptr;
    maximumFramesPerSlice_ = 0U;
  }

  AudioDeviceConfig config_;
  IAudioProcessor* processor_{nullptr};
  AudioUnit unit_{nullptr};
  UInt32 maximumFramesPerSlice_{0U};
  std::array<std::span<float>, domain::kMaximumAudioChannels> views_{};
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> callbacks_{0U};
  std::atomic<std::uint64_t> frames_{0U};
  std::atomic<std::uint64_t> failures_{0U};
  bool opened_{false};
};

}  // namespace

std::unique_ptr<IAudioDevice> createSystemAudioDevice() {
  return std::make_unique<CoreAudioDevice>();
}

}  // namespace seam::platform

#endif
