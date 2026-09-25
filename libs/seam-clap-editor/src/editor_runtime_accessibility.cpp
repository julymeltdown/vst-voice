#include "seam/clap_editor/editor_runtime.hpp"

namespace seam::clap_editor {

EditorRuntime::AccessibilitySnapshot EditorRuntime::accessibilitySnapshot() {
  std::lock_guard lock(mutex_);
  if (controller_ == nullptr) return {};
  controller_->rebuildAccessibilityTree();
  if (shell_.presentedLastFrame()) {
    // The presented SING shell publishes its own tree in its own geometry; its notes stay
    // virtualized and are paged through accessibilityNotes below.
    shell_.rebuildSemantics(*controller_, sceneState());
    const auto& shellTree = shell_.accessibilityTree();
    return AccessibilitySnapshot{.children = shellTree.root().children,
                                 .virtualizedNoteCount = shellTree.virtualizedNoteCount()};
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
  if (controller_ == nullptr) return {};
  if (shell_.presentedLastFrame()) return shell_.accessibilityTree().materializeNotes(offset, limit);
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
  auto result = controller_->dispatchAccessibility(id, action);
  // Only a focus transfer that happened moves focus away from a shell control.
  if (result && action == native_ui::SemanticAction::SetFocus) shell_.controllerFocusTaken();
  return result;
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
