#pragma once

#include "seam/native_ui/editor_controller.hpp"

#include <algorithm>

namespace seam::clap_editor {

// Cocoa places converted lyric fields exactly where the shell paints them, in logical points.
// Keep the historical minimum only for requests whose geometry has not been converted.
[[nodiscard]] inline ui::Rect embeddedTextInputBounds(const native_ui::TextInputRequest& request) noexcept {
  auto bounds = request.currentLogicalBounds();
  if (!request.presentedLyricBounds) {
    bounds.width = std::max(80.0, bounds.width);
    bounds.height = std::max(24.0, bounds.height);
  }
  return bounds;
}

}  // namespace seam::clap_editor
