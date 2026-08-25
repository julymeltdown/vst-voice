#include "seam/authoring/audio_settings_controller.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace seam::authoring {
namespace {

template <typename Value, std::size_t Size>
bool contains(const std::array<Value, Size>& values, Value value) noexcept {
  return std::find(values.begin(), values.end(), value) != values.end();
}

}

bool isBetaAudioSettings(const AudioSettings& settings) noexcept {
  static constexpr std::array rates{44100U, 48000U, 96000U};
  static constexpr std::array<std::size_t, 4U> blocks{64U, 128U, 256U, 512U};
  static constexpr std::array channels{std::uint8_t{1U}, std::uint8_t{2U},
                                       std::uint8_t{4U}, std::uint8_t{8U}};
  return contains(rates, settings.sampleRate) &&
         contains(blocks, settings.blockFrames) &&
         contains(channels, settings.outputChannels) &&
         settings.revision != 0U;
}

AudioSettingsController::AudioSettingsController(AudioSettings initial,
                                                 AudioSettingsApply apply)
    : current_(std::move(initial)), apply_(std::move(apply)) {
  if (current_.revision == 0U) current_.revision = 1U;
}

core::Result<AudioSettings> AudioSettingsController::apply(
    AudioSettings requested) {
  if (!isBetaAudioSettings(requested)) {
    return core::failure<AudioSettings>(
        core::ErrorCode::InvalidArgument,
        "Requested audio settings are outside the supported Beta matrix");
  }
  requested.revision = current_.revision + 1U;
  if (requested.deviceId == current_.deviceId &&
      requested.sampleRate == current_.sampleRate &&
      requested.blockFrames == current_.blockFrames &&
      requested.outputChannels == current_.outputChannels) {
    return current_;
  }
  const auto previous = current_;
  if (apply_) {
    const auto opened = apply_(requested);
    if (!opened) {
      const auto restored = apply_(previous);
      if (!restored) {
        return core::failure<AudioSettings>(
            core::ErrorCode::IoError,
            "Audio settings restart failed and previous configuration could not be restored",
            restored.error().message);
      }
      return core::Result<AudioSettings>{opened.error()};
    }
  }
  current_ = requested;
  return current_;
}

}
