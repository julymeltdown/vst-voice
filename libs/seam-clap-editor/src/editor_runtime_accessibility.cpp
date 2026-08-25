#include "seam/clap_editor/editor_runtime.hpp"

#include <algorithm>

namespace {

void applyFocusedId(seam::native_ui::SemanticNode& root,
                    std::string_view focusedId) {
  const auto apply = [focusedId](seam::native_ui::SemanticNode& node,
                                 const auto& self) -> void {
    node.focused = !focusedId.empty() && node.id == focusedId;
    for (auto& child : node.children) self(child, self);
  };
  apply(root, apply);
}

const seam::native_ui::SemanticNode* findNode(
    const seam::native_ui::SemanticNode& root, std::string_view id) {
  if (root.id == id) return &root;
  for (const auto& child : root.children) {
    if (const auto* match = findNode(child, id); match != nullptr) {
      return match;
    }
  }
  return nullptr;
}

}

namespace seam::clap_editor {

EditorRuntime::AccessibilitySnapshot EditorRuntime::accessibilitySnapshot() {
  std::lock_guard lock(mutex_);
  if (controller_ == nullptr) return {};
  if (microscopeUnitId_.has_value()) {
    auto tree = native_ui::EditorSemanticTree::build(
        sceneState(), controller_->pianoRoll());
    applyFocusedId(tree, microscopeFocusedId_);
    return AccessibilitySnapshot{
        .children = tree.children,
        .virtualizedNoteCount = 0U,
    };
  }
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
  if (microscopeUnitId_.has_value()) {
    auto tree = native_ui::EditorSemanticTree::build(
        sceneState(), controller_->pianoRoll());
    applyFocusedId(tree, microscopeFocusedId_);
    const auto* focused = findNode(tree, microscopeFocusedId_);
    return focused == nullptr
               ? std::nullopt
               : std::optional<native_ui::SemanticNode>{*focused};
  }
  controller_->rebuildAccessibilityTree();
  const auto* focused = controller_->accessibilityTree().focusedNode();
  return focused == nullptr ? std::nullopt
                            : std::optional<native_ui::SemanticNode>{*focused};
}

std::vector<native_ui::SemanticNode> EditorRuntime::accessibilityNotes(
    std::size_t offset, std::size_t limit) const {
  std::lock_guard lock(mutex_);
  if (controller_ == nullptr) return {};
  if (microscopeUnitId_.has_value()) return {};
  return controller_->accessibilityTree().materializeNotes(offset, limit);
}

core::Result<void> EditorRuntime::dispatchAccessibility(
    std::string_view id, native_ui::SemanticAction action) {
  std::lock_guard lock(mutex_);
  if (controller_ == nullptr) {
    return core::failure(core::ErrorCode::InvalidState,
                         "CLAP editor accessibility is unavailable");
  }
  if (microscopeUnitId_.has_value()) {
    if (action == native_ui::SemanticAction::Activate &&
        id == "microscope.close") {
      microscopeUnitId_.reset();
      requestRepaint();
      return core::success();
    }
    if (action == native_ui::SemanticAction::SetFocus &&
        id.rfind("microscope.", 0U) == 0U) {
      const auto tree = native_ui::EditorSemanticTree::build(
          sceneState(), controller_->pianoRoll());
      const auto* node = findNode(tree, id);
      if (node == nullptr ||
          std::find(node->actions.begin(), node->actions.end(), action) ==
              node->actions.end()) {
        return core::failure(core::ErrorCode::InvalidArgument,
                             "Microscope accessibility focus target is unavailable");
      }
      microscopeFocusedId_ = std::string{id};
      requestRepaint();
      return core::success();
    }
    return core::failure(core::ErrorCode::Unsupported,
                         "Microscope accessibility action is unavailable");
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
