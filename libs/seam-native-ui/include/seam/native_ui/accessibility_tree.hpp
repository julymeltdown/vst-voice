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

class AccessibilityTree final {
public:
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
