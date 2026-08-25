#include "seam/native_ui/accessibility_tree.hpp"

namespace seam::native_ui {

core::Result<void> installAccessibilityBridge(void*) {
  return core::failure(core::ErrorCode::Unsupported,
                       "Native accessibility bridge is unavailable");
}

}
