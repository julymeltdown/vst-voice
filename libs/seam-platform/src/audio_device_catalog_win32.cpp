#include "seam/platform/audio_device_catalog.hpp"

#if defined(SEAM_AUDIO_WASAPI)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <mmdeviceapi.h>
#include <propkey.h>
#include <wrl/client.h>

#include <string>

namespace seam::platform {
namespace {

using Microsoft::WRL::ComPtr;

class WasapiDeviceCatalog final : public IAudioDeviceCatalog {
public:
  core::Result<AudioDeviceCatalogSnapshot> enumerate() override {
    ++generation_;
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool ownsCom = SUCCEEDED(initialized);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
      return core::failure<AudioDeviceCatalogSnapshot>(
          core::ErrorCode::IoError, "Unable to initialize WASAPI catalog");
    }
    ComPtr<IMMDeviceEnumerator> enumerator;
    auto result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                   CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(result)) {
      if (ownsCom) CoUninitialize();
      return core::failure<AudioDeviceCatalogSnapshot>(
          core::ErrorCode::IoError, "Unable to create WASAPI device catalog");
    }
    ComPtr<IMMDevice> defaultDevice;
    std::wstring defaultId;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                      &defaultDevice))) {
      LPWSTR raw = nullptr;
      if (SUCCEEDED(defaultDevice->GetId(&raw))) {
        defaultId = raw;
        CoTaskMemFree(raw);
      }
    }
    ComPtr<IMMDeviceCollection> collection;
    result = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE,
                                            &collection);
    if (FAILED(result)) {
      if (ownsCom) CoUninitialize();
      return core::failure<AudioDeviceCatalogSnapshot>(
          core::ErrorCode::IoError, "Unable to enumerate WASAPI outputs");
    }
    UINT count = 0U;
    collection->GetCount(&count);
    AudioDeviceCatalogSnapshot snapshot{.generation = generation_};
    for (UINT index = 0U; index < count; ++index) {
      ComPtr<IMMDevice> device;
      if (FAILED(collection->Item(index, &device))) continue;
      LPWSTR rawId = nullptr;
      if (FAILED(device->GetId(&rawId))) continue;
      const std::wstring id{rawId};
      CoTaskMemFree(rawId);
      ComPtr<IPropertyStore> store;
      std::wstring name = L"WASAPI Output";
      if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store))) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) &&
            value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
          name = value.pwszVal;
        }
        PropVariantClear(&value);
      }
      snapshot.devices.push_back(AudioDeviceDescription{
          .id = std::string{id.begin(), id.end()},
          .name = std::string{name.begin(), name.end()},
          .isDefault = id == defaultId,
          .physical = true,
          .generation = generation_,
          .supportedSampleRates = {44100U, 48000U, 96000U},
          .minimumBlockFrames = 64U,
          .maximumBlockFrames = 512U,
          .minimumOutputChannels = 1U,
          .maximumOutputChannels = 8U,
      });
    }
    if (ownsCom) CoUninitialize();
    return snapshot;
  }

private:
  std::uint32_t generation_{0U};
};

}

std::unique_ptr<IAudioDeviceCatalog> createSystemAudioDeviceCatalog() {
  return std::make_unique<WasapiDeviceCatalog>();
}

}

#endif
