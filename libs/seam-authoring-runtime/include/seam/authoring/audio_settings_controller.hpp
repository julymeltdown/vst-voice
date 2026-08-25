#pragma once

#include "seam/authoring/audio_settings.hpp"

#include <functional>

namespace seam::authoring {

using AudioSettingsApply = std::function<core::Result<void>(
    const AudioSettings&)>;

[[nodiscard]] bool isBetaAudioSettings(const AudioSettings& settings) noexcept;

class AudioSettingsController final {
public:
  AudioSettingsController(AudioSettings initial,
                          AudioSettingsApply apply);

  [[nodiscard]] core::Result<AudioSettings> apply(
      AudioSettings requested);
  [[nodiscard]] const AudioSettings& current() const noexcept {
    return current_;
  }

private:
  AudioSettings current_;
  AudioSettingsApply apply_;
};

}
