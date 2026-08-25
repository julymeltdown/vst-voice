#include "seam/platform/audio_device_catalog.hpp"

#if defined(SEAM_AUDIO_COREAUDIO)

#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace seam::platform {
namespace {

std::string cfString(CFStringRef value) {
  if (value == nullptr) return {};
  std::array<char, 1024> buffer{};
  return CFStringGetCString(value, buffer.data(), buffer.size(),
                            kCFStringEncodingUTF8)
             ? std::string{buffer.data()}
             : std::string{};
}

template <typename Value>
bool property(AudioObjectID object, AudioObjectPropertySelector selector,
              AudioObjectPropertyScope scope, AudioObjectPropertyElement element,
              Value& value) {
  AudioObjectPropertyAddress address{selector, scope, element};
  UInt32 size = sizeof(value);
  return AudioObjectGetPropertyData(object, &address, 0U, nullptr, &size,
                                    &value) == noErr;
}

std::string deviceString(AudioDeviceID device,
                         AudioObjectPropertySelector selector) {
  CFStringRef value = nullptr;
  if (!property(device, selector, kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain, value)) {
    return {};
  }
  const auto result = cfString(value);
  if (value != nullptr) CFRelease(value);
  return result;
}

class CoreAudioDeviceCatalog final : public IAudioDeviceCatalog {
public:
  core::Result<AudioDeviceCatalogSnapshot> enumerate() override {
    ++generation_;
    AudioDeviceID defaultDevice = kAudioObjectUnknown;
    if (!property(kAudioObjectSystemObject,
                  kAudioHardwarePropertyDefaultOutputDevice,
                  kAudioObjectPropertyScopeGlobal,
                  kAudioObjectPropertyElementMain, defaultDevice)) {
      return core::failure<AudioDeviceCatalogSnapshot>(
          core::ErrorCode::IoError,
          "Unable to query the CoreAudio default output device");
    }

    AudioObjectPropertyAddress address{
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    UInt32 size = 0U;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0U,
                                       nullptr, &size) != noErr ||
        size == 0U || size % sizeof(AudioDeviceID) != 0U) {
      return AudioDeviceCatalogSnapshot{.generation = generation_};
    }
    std::vector<AudioDeviceID> devices(size / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0U,
                                   nullptr, &size, devices.data()) != noErr) {
      return core::failure<AudioDeviceCatalogSnapshot>(
          core::ErrorCode::IoError,
          "Unable to enumerate CoreAudio devices");
    }

    AudioDeviceCatalogSnapshot snapshot{.generation = generation_};
    for (const auto device : devices) {
      UInt32 outputSize = 0U;
      AudioObjectPropertyAddress outputAddress{
          kAudioDevicePropertyStreamConfiguration,
          kAudioDevicePropertyScopeOutput,
          kAudioObjectPropertyElementMain,
      };
      std::uint8_t outputChannels = 0U;
      if (AudioObjectGetPropertyDataSize(device, &outputAddress, 0U, nullptr,
                                         &outputSize) == noErr &&
          outputSize >= sizeof(AudioBufferList)) {
        std::vector<std::uint8_t> bytes(outputSize);
        auto* list = reinterpret_cast<AudioBufferList*>(bytes.data());
        if (AudioObjectGetPropertyData(device, &outputAddress, 0U, nullptr,
                                       &outputSize, list) == noErr) {
          for (UInt32 index = 0U; index < list->mNumberBuffers; ++index) {
            outputChannels = static_cast<std::uint8_t>(std::min<UInt32>(
                8U, outputChannels + list->mBuffers[index].mNumberChannels));
          }
        }
      }
      if (outputChannels == 0U) continue;

      AudioValueRange bufferRange{64.0, 512.0};
      static_cast<void>(property(device, kAudioDevicePropertyBufferFrameSizeRange,
                                  kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain, bufferRange));
      AudioValueRange rateRange{44100.0, 96000.0};
      static_cast<void>(property(device,
                                  kAudioDevicePropertyNominalSampleRate,
                                  kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain,
                                  rateRange.mMinimum));
      const auto uid = deviceString(device, kAudioDevicePropertyDeviceUID);
      const auto name = deviceString(device, kAudioObjectPropertyName);
      AudioDeviceDescription description{
          .id = uid.empty() ? "coreaudio:" + std::to_string(device) : uid,
          .name = name.empty() ? "CoreAudio Output" : name,
          .isDefault = device == defaultDevice,
          .physical = true,
          .generation = generation_,
          .supportedSampleRates = {44100U, 48000U, 96000U},
          .minimumBlockFrames = static_cast<std::uint32_t>(
              std::max(1.0, bufferRange.mMinimum)),
          .maximumBlockFrames = static_cast<std::uint32_t>(
              std::max(bufferRange.mMinimum, bufferRange.mMaximum)),
          .minimumOutputChannels = 1U,
          .maximumOutputChannels = outputChannels,
      };
      snapshot.devices.push_back(std::move(description));
    }
    return snapshot;
  }

private:
  std::uint32_t generation_{0U};
};

}

std::unique_ptr<IAudioDeviceCatalog> createSystemAudioDeviceCatalog() {
  return std::make_unique<CoreAudioDeviceCatalog>();
}

}

#endif
