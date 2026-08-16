#include "seam/platform/audio_device.hpp"

#if !defined(SEAM_AUDIO_PULSE)

namespace seam::platform {
namespace {

class UnavailableAudioDevice final : public IAudioDevice {
public:
  core::Result<void> open(const AudioDeviceConfig&, IAudioProcessor&) override {
    return core::failure(core::ErrorCode::Unsupported,
                         "No physical audio adapter is available on this platform");
  }
  core::Result<void> start() override {
    return core::failure(core::ErrorCode::Conflict,
                         "Unavailable audio device cannot start");
  }
  void stop() noexcept override {}
  bool running() const noexcept override { return false; }
  AudioDeviceInfo info() const override {
    return AudioDeviceInfo{.backend = "unavailable"};
  }
  AudioDeviceStats stats() const noexcept override { return {}; }
};

}  // namespace

std::unique_ptr<IAudioDevice> createSystemAudioDevice() {
  return std::make_unique<UnavailableAudioDevice>();
}

}  // namespace seam::platform

#endif
