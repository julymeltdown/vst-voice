#include "seam/platform/audio_input_device.hpp"

#if defined(SEAM_AUDIO_COREAUDIO)

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#include <algorithm>
#include <atomic>
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

class CoreAudioInputDevice final : public IAudioInputDevice {
public:
  ~CoreAudioInputDevice() override {
    stop();
    close();
  }

  core::Result<void> open(const AudioInputDeviceConfig& config,
                          IAudioInputProcessor& processor) override {
    if (running()) {
      return core::failure(core::ErrorCode::Conflict,
                           "Cannot reopen a running CoreAudio input");
    }
    if (config.sampleRate < 8000U || config.sampleRate > 384000U ||
        config.blockFrames == 0U || config.blockFrames > 16384U) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "CoreAudio input configuration is outside supported bounds");
    }
    close();
    AudioComponentDescription description{
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_HALOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags = 0U,
        .componentFlagsMask = 0U,
    };
    const auto component = AudioComponentFindNext(nullptr, &description);
    if (component == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "CoreAudio HAL input component is unavailable");
    }
    auto status = AudioComponentInstanceNew(component, &unit_);
    if (status != noErr || unit_ == nullptr) {
      close();
      return core::failure(core::ErrorCode::IoError,
                           "Unable to create CoreAudio HAL input unit",
                           statusText(status));
    }

    UInt32 enabled = 1U;
    status = AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO,
                                  kAudioUnitScope_Input, 1U, &enabled,
                                  sizeof(enabled));
    if (status != noErr) return failOpen("Unable to enable CoreAudio input bus", status);
    UInt32 disabled = 0U;
    status = AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO,
                                  kAudioUnitScope_Output, 0U, &disabled,
                                  sizeof(disabled));
    if (status != noErr) return failOpen("Unable to disable CoreAudio output bus", status);

    AudioDeviceID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    AudioObjectPropertyAddress address{
        .mSelector = kAudioHardwarePropertyDefaultInputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain,
    };
    status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0U,
                                        nullptr, &size, &device);
    if (status != noErr || device == kAudioObjectUnknown) {
      return failOpen("Unable to obtain the default CoreAudio input device", status);
    }
    status = AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice,
                                  kAudioUnitScope_Global, 0U, &device,
                                  sizeof(device));
    if (status != noErr) return failOpen("Unable to assign CoreAudio input device", status);

    auto maximumFrames = std::max(
        static_cast<UInt32>(config.blockFrames),
        deviceBufferFrameSize(device,
                              static_cast<UInt32>(config.blockFrames)));
    if (maximumFrames > 16384U) {
      close();
      return core::failure(
          core::ErrorCode::Unsupported,
          "CoreAudio input callback slice exceeds the supported maximum");
    }

    AudioStreamBasicDescription format{};
    format.mSampleRate = static_cast<Float64>(config.sampleRate);
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags =
        static_cast<AudioFormatFlags>(kAudioFormatFlagsNativeFloatPacked) |
        static_cast<AudioFormatFlags>(kAudioFormatFlagIsNonInterleaved);
    format.mBytesPerPacket = sizeof(Float32);
    format.mFramesPerPacket = 1U;
    format.mBytesPerFrame = sizeof(Float32);
    format.mChannelsPerFrame = 1U;
    format.mBitsPerChannel = 32U;
    status = AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Output, 1U, &format,
                                  sizeof(format));
    if (status != noErr) return failOpen("Unable to set CoreAudio capture format", status);

    status = AudioUnitSetProperty(unit_, kAudioUnitProperty_MaximumFramesPerSlice,
                                  kAudioUnitScope_Global, 0U, &maximumFrames,
                                  sizeof(maximumFrames));
    if (status != noErr) return failOpen("Unable to set CoreAudio input maximum slice", status);
    AURenderCallbackStruct callback{
        .inputProc = &CoreAudioInputDevice::inputCallback,
        .inputProcRefCon = this,
    };
    status = AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_SetInputCallback,
                                  kAudioUnitScope_Global, 0U, &callback,
                                  sizeof(callback));
    if (status != noErr) return failOpen("Unable to install CoreAudio input callback", status);
    status = AudioUnitInitialize(unit_);
    if (status != noErr) return failOpen("Unable to initialize CoreAudio input", status);

    config_ = config;
    processor_ = &processor;
    mono_.assign(maximumFrames, 0.0F);
    opened_ = true;
    return core::success();
  }

  core::Result<void> start() override {
    if (!opened_ || unit_ == nullptr || processor_ == nullptr) {
      return core::failure(core::ErrorCode::Conflict,
                           "CoreAudio input must be opened before start");
    }
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true,
                                          std::memory_order_acq_rel)) {
      return core::failure(core::ErrorCode::Conflict,
                           "CoreAudio input is already running");
    }
    const auto status = AudioOutputUnitStart(unit_);
    if (status != noErr) {
      running_.store(false, std::memory_order_release);
      return core::failure(core::ErrorCode::IoError,
                           "Unable to start CoreAudio input",
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

  AudioInputDeviceInfo info() const override {
    return AudioInputDeviceInfo{
        .backend = "CoreAudio HAL capture",
        .deviceName = "default-input-device",
        .sampleRate = config_.sampleRate,
        .blockFrames = config_.blockFrames,
        .physical = true,
    };
  }

  AudioInputDeviceStats stats() const noexcept override {
    return AudioInputDeviceStats{
        .callbacks = callbacks_.load(std::memory_order_relaxed),
        .frames = frames_.load(std::memory_order_relaxed),
        .readFailures = failures_.load(std::memory_order_relaxed),
    };
  }

private:
  static OSStatus inputCallback(void* reference,
                                AudioUnitRenderActionFlags* flags,
                                const AudioTimeStamp* timestamp,
                                UInt32, UInt32 frameCount,
                                AudioBufferList*) noexcept {
    auto* self = static_cast<CoreAudioInputDevice*>(reference);
    if (self == nullptr || self->unit_ == nullptr || self->processor_ == nullptr ||
        frameCount > self->mono_.size()) {
      if (self != nullptr) self->failures_.fetch_add(1U, std::memory_order_relaxed);
      return kAudio_ParamError;
    }
    AudioBufferList list{};
    list.mNumberBuffers = 1U;
    list.mBuffers[0].mNumberChannels = 1U;
    list.mBuffers[0].mDataByteSize = static_cast<UInt32>(
        static_cast<std::size_t>(frameCount) * sizeof(float));
    list.mBuffers[0].mData = self->mono_.data();
    const auto status = AudioUnitRender(self->unit_, flags, timestamp, 1U,
                                        frameCount, &list);
    if (status != noErr) {
      self->failures_.fetch_add(1U, std::memory_order_relaxed);
      return status;
    }
    self->processor_->process(AudioInputProcessContext{
        .sampleRate = static_cast<double>(self->config_.sampleRate),
        .frameCount = frameCount,
        .mono = std::span<const float>{self->mono_.data(), frameCount},
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
    mono_.clear();
    processor_ = nullptr;
    opened_ = false;
  }

  AudioInputDeviceConfig config_;
  IAudioInputProcessor* processor_{nullptr};
  AudioUnit unit_{nullptr};
  std::vector<float> mono_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> callbacks_{0U};
  std::atomic<std::uint64_t> frames_{0U};
  std::atomic<std::uint64_t> failures_{0U};
  bool opened_{false};
};

}  // namespace

std::unique_ptr<IAudioInputDevice> createSystemAudioInputDevice() {
  return std::make_unique<CoreAudioInputDevice>();
}

}  // namespace seam::platform

#endif
