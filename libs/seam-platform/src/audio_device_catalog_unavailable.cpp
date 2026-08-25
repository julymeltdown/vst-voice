#include "seam/platform/audio_device_catalog.hpp"

#if !defined(SEAM_AUDIO_COREAUDIO) && !defined(SEAM_AUDIO_WASAPI)

namespace seam::platform {
namespace {

class UnavailableAudioDeviceCatalog final : public IAudioDeviceCatalog {
public:
  core::Result<AudioDeviceCatalogSnapshot> enumerate() override {
    return core::failure<AudioDeviceCatalogSnapshot>(
        core::ErrorCode::Unsupported,
        "No physical audio device catalog is available on this platform");
  }
};

}

std::unique_ptr<IAudioDeviceCatalog> createSystemAudioDeviceCatalog() {
  return std::make_unique<UnavailableAudioDeviceCatalog>();
}

}

#endif
