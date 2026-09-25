#pragma once

#include "seam/native_ui/editor_semantics.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seam::native_ui {

struct AccessibilityTreeConfig final {
  std::size_t maximumMaterializedNotes{512U};
};

class AccessibilityTree;

// Lets a custom surface (the redesigned shell) publish the editor's notes without copying them:
// notes stay virtualized in the source tree and are presented through the surface's geometry.
// The source must outlive every use of the tree that holds it until the next rebuild.
struct VirtualNoteSource final {
  const AccessibilityTree* tree{nullptr};
  std::function<void(SemanticNode&)> present;
};

class AccessibilityTree final {
public:
  // Bounded non-score surfaces (for example Voice Designer) share the native
  // bridge without fabricating a piano-roll model or virtual notes.
  void rebuildCustom(SemanticNode root, std::string focusedId = {},
                     VirtualNoteSource notes = {});
  void rebuild(const EditorSceneState& state, const ui::PianoRollModel& model,
               AccessibilityTreeConfig config = {});
  [[nodiscard]] const SemanticNode& root() const noexcept { return root_; }
  [[nodiscard]] std::size_t virtualizedNoteCount() const noexcept {
    return virtualizedNoteCount_;
  }
  [[nodiscard]] std::vector<SemanticNode> materializeNotes(
      std::size_t offset, std::size_t limit) const;
  [[nodiscard]] std::size_t materializedNoteCount() const noexcept;
  [[nodiscard]] const SemanticNode* focusedNode() const;
  // True when this tree currently publishes the element: a node in the tree or one of its
  // virtual notes. A retained host element for anything else is stale.
  [[nodiscard]] bool publishes(std::string_view id) const;
  [[nodiscard]] core::Result<void> focusNext(bool reverse);
  [[nodiscard]] core::Result<void> setFocus(std::string_view id);
  [[nodiscard]] core::Result<void> dispatch(
      std::string_view id, SemanticAction action,
      const std::function<core::Result<void>(std::string_view, SemanticAction)>&
          handler) const;

private:
  EditorSceneState state_;
  const ui::PianoRollModel* model_{nullptr};
  AccessibilityTreeConfig config_;
  SemanticNode root_;
  std::size_t virtualizedNoteCount_{0U};
  std::string focusedId_;
  VirtualNoteSource noteSource_;
  mutable std::optional<SemanticNode> focusedScratch_;

  [[nodiscard]] std::optional<SemanticNode> noteNodeAt(
      std::size_t index) const;
  [[nodiscard]] std::optional<std::size_t> noteIndexForId(
      std::string_view id) const;
  void applyFocusedId();
};

[[nodiscard]] core::Result<void> installAccessibilityBridge(
    void* nativeView);

}
