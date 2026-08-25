#pragma once

#include <algorithm>

namespace seam::live_voice {

struct NoteExpressionState final {
  float volume{1.0F};
  float pan{0.0F};
  float tuningSemitones{0.0F};
  float vibrato{0.0F};
  float expression{1.0F};
  float brightness{0.0F};
  float pressure{0.0F};

  void applyVolume(float value) noexcept {
    volume = std::clamp(value, 0.0F, 4.0F);
  }
  void applyPan(float value) noexcept { pan = std::clamp(value, -1.0F, 1.0F); }
  void applyTuning(float value) noexcept {
    tuningSemitones = std::clamp(value, -48.0F, 48.0F);
  }
  void applyVibrato(float value) noexcept {
    vibrato = std::clamp(value, 0.0F, 1.0F);
  }
  void applyExpression(float value) noexcept {
    expression = std::clamp(value, 0.0F, 4.0F);
  }
  void applyBrightness(float value) noexcept {
    brightness = std::clamp(value, 0.0F, 1.0F);
  }
  void applyPressure(float value) noexcept {
    pressure = std::clamp(value, 0.0F, 1.0F);
  }
};

}
