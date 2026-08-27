#include "seam/platform/accessibility_preferences.hpp"

#import <AppKit/AppKit.h>

namespace seam::platform {

AccessibilityPreferences currentAccessibilityPreferences() noexcept {
  return AccessibilityPreferences{
      .reduceMotion = NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion,
  };
}

}
