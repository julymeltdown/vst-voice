#pragma once

namespace seam::platform {

struct AccessibilityPreferences final {
  bool reduceMotion{false};
};

[[nodiscard]] AccessibilityPreferences currentAccessibilityPreferences() noexcept;

}
