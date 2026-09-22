#include "seam/clap_editor/editor_runtime.hpp"

namespace seam::clap_editor {

EditorRuntime::AccessibilitySnapshot EditorRuntime::accessibilitySnapshot() {
  std::lock_guard lock(mutex_);
  if (controller_ == nullptr) return {};
  controller_->rebuildAccessibilityTree();
  const auto& tree = controller_->accessibilityTree();
  return AccessibilitySnapshot{
      .children = tree.root().children,
      .virtualizedNoteCount = tree.virtualizedNoteCount(),
  };
}

std::optional<native_ui::SemanticNode> EditorRuntime::accessibilityFocusedNode() {
  std::lock_guard lock(mutex_);
  if (controller_ == nullptr) return std::nullopt;
  controller_->rebuildAccessibilityTree();
  const auto* focused = controller_->accessibilityTree().focusedNode();
  return focused == nullptr ? std::nullopt
                            : std::optional<native_ui::SemanticNode>{*focused};
}

std::vector<native_ui::SemanticNode> EditorRuntime::accessibilityNotes(
    std::size_t offset, std::size_t limit) const {
  std::lock_guard lock(mutex_);
  if (controller_ == nullptr) return {};
  return controller_->accessibilityTree().materializeNotes(offset, limit);
}

core::Result<void> EditorRuntime::dispatchAccessibility(
    std::string_view id, native_ui::SemanticAction action) {
  std::lock_guard lock(mutex_);
  if (controller_ == nullptr) {
    return core::failure(core::ErrorCode::InvalidState,
                         "CLAP editor accessibility is unavailable");
  }
  controller_->rebuildAccessibilityTree();
  return controller_->dispatchAccessibility(id, action);
}

core::Result<void> EditorRuntime::setAccessibilityValue(
    std::string_view id, std::string_view value) {
  std::lock_guard lock(mutex_);
  if (controller_ == nullptr) {
    return core::failure(core::ErrorCode::InvalidState,
                         "CLAP editor accessibility is unavailable");
  }
  controller_->rebuildAccessibilityTree();
  return controller_->setAccessibilityValue(id, value);
}

}
