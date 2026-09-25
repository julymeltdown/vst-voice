#include "seam/clap_editor/editor_runtime.hpp"

namespace seam::clap_editor {

EditorRuntime::AccessibilitySnapshot EditorRuntime::accessibilitySnapshot() {
  std::lock_guard lock(mutex_);
  if (controller_ == nullptr) return {};
  controller_->rebuildAccessibilityTree();
  if (shell_.presentedLastFrame()) {
    // The presented SING shell publishes its own tree, in its own geometry, with no virtual notes.
    shell_.rebuildSemantics(*controller_, sceneState());
    return AccessibilitySnapshot{.children = shell_.accessibilityTree().root().children,
                                 .virtualizedNoteCount = 0U};
  }
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
  if (shell_.presentedLastFrame()) shell_.rebuildSemantics(*controller_, sceneState());
  const auto* focused = shell_.presentedLastFrame() ? shell_.accessibilityTree().focusedNode()
                                                    : controller_->accessibilityTree().focusedNode();
  return focused == nullptr ? std::nullopt
                            : std::optional<native_ui::SemanticNode>{*focused};
}

std::vector<native_ui::SemanticNode> EditorRuntime::accessibilityNotes(
    std::size_t offset, std::size_t limit) const {
  std::lock_guard lock(mutex_);
  if (controller_ == nullptr || shell_.presentedLastFrame()) return {};
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
  if (shell_.presentedLastFrame() && native_ui::design::SingShell::ownsSemantic(id)) {
    const auto before = session_.revision();
    auto result = shell_.dispatchSemantic(*controller_, id, action);
    if (session_.revision() != before) requestRenderAfterEdit();
    return result;
  }
  shell_.controllerFocusTaken();
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
