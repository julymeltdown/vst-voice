#include "seam/platform/audio_input_device.hpp"

#if !defined(SEAM_AUDIO_PULSE) && !defined(SEAM_AUDIO_WASAPI) && \
    !defined(SEAM_AUDIO_COREAUDIO)
namespace seam::platform {
namespace {

class UnavailableAudioInputDevice final : public IAudioInputDevice {
public:
  core::Result<void> open(const AudioInputDeviceConfig&,
                          IAudioInputProcessor&) override {
    return core::failure(core::ErrorCode::Unsupported,
                         "No physical audio-input adapter is implemented for this platform");
  }
  core::Result<void> start() override {
    return core::failure(core::ErrorCode::Unsupported,
                         "No physical audio-input adapter is implemented for this platform");
  }
  void stop() noexcept override {}
  bool running() const noexcept override { return false; }
  AudioInputDeviceInfo info() const override { return {}; }
  AudioInputDeviceStats stats() const noexcept override { return {}; }
};

}  // namespace

std::unique_ptr<IAudioInputDevice> createSystemAudioInputDevice() {
  return std::make_unique<UnavailableAudioInputDevice>();
}

}  // namespace seam::platform

#endif
