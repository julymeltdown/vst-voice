#include "seam/native_ui/accessibility_tree.hpp"

#import <AppKit/AppKit.h>

namespace seam::native_ui {

core::Result<void> installAccessibilityBridge(void* nativeView) {
  if (nativeView == nullptr) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "AppKit accessibility bridge requires a native view");
  }
  NSView* view = (__bridge NSView*)nativeView;
  view.accessibilityElement = YES;
  view.accessibilityRole = NSAccessibilityGroupRole;
  return core::success();
}

}
