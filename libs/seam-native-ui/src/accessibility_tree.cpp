#include "seam/native_ui/accessibility_tree.hpp"

#include <algorithm>

namespace seam::native_ui {

void AccessibilityTree::rebuild(const EditorSceneState& state,
                                const ui::PianoRollModel& model,
                                AccessibilityTreeConfig config) {
  state_ = state;
  model_ = &model;
  config_ = config;
  root_ = EditorSemanticTree::build(state, model, {}, false, false);
  std::vector<SemanticNode> retained;
  retained.reserve(root_.children.size());
  for (auto& child : root_.children) {
    if (child.role != SemanticRole::Note) {
      retained.push_back(std::move(child));
    }
  }
  virtualizedNoteCount_ = model.noteCount();
  const auto keep = std::min(config_.maximumMaterializedNotes,
                             virtualizedNoteCount_);
  for (std::size_t index = 0U; index < keep; ++index) {
    if (const auto note = noteNodeAt(index); note.has_value()) {
      retained.push_back(*note);
    }
  }
  root_.children = std::move(retained);
  root_.virtualizedChildCount = virtualizedNoteCount_;
  applyFocusedId();
}

std::vector<SemanticNode> AccessibilityTree::materializeNotes(
    std::size_t offset, std::size_t limit) const {
  if (offset >= virtualizedNoteCount_ || limit == 0U) return {};
  const auto end = std::min(virtualizedNoteCount_, offset + limit);
  std::vector<SemanticNode> result;
  result.reserve(end - offset);
  for (std::size_t index = offset; index < end; ++index) {
    if (const auto note = noteNodeAt(index); note.has_value()) {
      result.push_back(*note);
    }
  }
  return result;
}

std::size_t AccessibilityTree::materializedNoteCount() const noexcept {
  return static_cast<std::size_t>(std::count_if(
      root_.children.begin(), root_.children.end(), [](const auto& node) {
        return node.role == SemanticRole::Note;
      }));
}

std::optional<SemanticNode> AccessibilityTree::noteNodeAt(
    std::size_t index) const {
  if (model_ == nullptr) return std::nullopt;
  const auto visual = model_->noteAt(index);
  if (!visual.has_value()) return std::nullopt;
  auto node = EditorSemanticTree::noteNode(*visual);
  if (node.id == focusedId_) node.focused = true;
  return node;
}

std::optional<std::size_t> AccessibilityTree::noteIndexForId(
    std::string_view id) const {
  constexpr auto prefix = std::string_view{"note."};
  if (model_ == nullptr || !id.starts_with(prefix)) return std::nullopt;
  const auto* region = model_->project().findRegion(model_->regionId());
  if (region == nullptr) return std::nullopt;
  const auto suffix = id.substr(prefix.size());
  for (std::size_t index = 0U; index < region->notes.size(); ++index) {
    if (region->notes[index].id.toString() == suffix) return index;
  }
  return std::nullopt;
}

const SemanticNode* AccessibilityTree::focusedNode() const {
  const auto findFocused = [](const SemanticNode& node,
                              const auto& self) -> const SemanticNode* {
    if (node.focused) return &node;
    for (const auto& child : node.children) {
      if (const auto* focused = self(child, self); focused != nullptr) {
        return focused;
      }
    }
    return nullptr;
  };
  if (const auto* focused = findFocused(root_, findFocused); focused != nullptr) {
    return focused;
  }
  if (const auto index = noteIndexForId(focusedId_); index.has_value()) {
    focusedScratch_ = noteNodeAt(*index);
    if (focusedScratch_.has_value()) {
      focusedScratch_->focused = true;
      return &*focusedScratch_;
    }
  }
  return nullptr;
}

core::Result<void> AccessibilityTree::focusNext(bool reverse) {
  std::vector<const SemanticNode*> focusable;
  const auto collect = [&focusable](const SemanticNode& node,
                                    const auto& self) -> void {
    if (std::find(node.actions.begin(), node.actions.end(),
                  SemanticAction::SetFocus) != node.actions.end()) {
      focusable.push_back(&node);
    }
    for (const auto& child : node.children) self(child, self);
  };
  for (const auto& child : root_.children) {
    if (child.role == SemanticRole::Note) continue;
    collect(child, collect);
  }
  const auto focusControl = [this](const SemanticNode& node) {
    focusedId_ = node.id;
    applyFocusedId();
    return core::success();
  };
  const auto focusNote = [this](std::size_t index) {
    const auto note = noteNodeAt(index);
    if (!note.has_value()) {
      return core::failure(core::ErrorCode::NotFound,
                           "Accessibility note target is unavailable");
    }
    focusedId_ = note->id;
    applyFocusedId();
    return core::success();
  };
  const auto noteCount = virtualizedNoteCount_;
  const auto currentNote = noteIndexForId(focusedId_);
  if (currentNote.has_value()) {
    if (!reverse) {
      if (*currentNote + 1U < noteCount) return focusNote(*currentNote + 1U);
      if (!focusable.empty()) return focusControl(*focusable.front());
      return focusNote(0U);
    }
    if (*currentNote > 0U) return focusNote(*currentNote - 1U);
    if (!focusable.empty()) return focusControl(*focusable.back());
    return focusNote(noteCount - 1U);
  }
  if (focusable.empty() && noteCount == 0U) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Accessibility tree has no focusable elements");
  }
  const auto current = std::find_if(
      focusable.begin(), focusable.end(), [this](const auto* node) {
        return node->id == focusedId_;
      });
  if (current == focusable.end()) {
    if (reverse) {
      if (noteCount > 0U) return focusNote(noteCount - 1U);
      return focusControl(*focusable.back());
    }
    if (!focusable.empty()) return focusControl(*focusable.front());
    return focusNote(0U);
  }
  const auto index = static_cast<std::size_t>(
      std::distance(focusable.begin(), current));
  if (reverse) {
    if (index > 0U) return focusControl(*focusable[index - 1U]);
    if (noteCount > 0U) return focusNote(noteCount - 1U);
    return focusControl(*focusable.back());
  }
  if (index + 1U < focusable.size()) return focusControl(*focusable[index + 1U]);
  if (noteCount > 0U) return focusNote(0U);
  return focusControl(*focusable.front());
}

core::Result<void> AccessibilityTree::setFocus(std::string_view id) {
  const auto hasFocusableId = [id](const SemanticNode& node,
                                   const auto& self) -> bool {
    if (node.id == id) {
      return std::find(node.actions.begin(), node.actions.end(),
                       SemanticAction::SetFocus) != node.actions.end();
    }
    return std::any_of(node.children.begin(), node.children.end(),
                       [&self](const auto& child) {
                         return self(child, self);
                       });
  };
  const auto virtualMatch = noteIndexForId(id).has_value();
  if (!hasFocusableId(root_, hasFocusableId) && !virtualMatch) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Accessibility focus target is unavailable");
  }
  focusedId_ = std::string{id};
  applyFocusedId();
  return core::success();
}

void AccessibilityTree::applyFocusedId() {
  if (focusedId_.empty()) return;
  bool found = false;
  const auto apply = [this, &found](SemanticNode& node,
                                    const auto& self) -> void {
    if (node.id == focusedId_) {
      node.focused = true;
      found = true;
    } else {
      node.focused = false;
    }
    for (auto& child : node.children) self(child, self);
  };
  apply(root_, apply);
  if (!found && !noteIndexForId(focusedId_).has_value()) focusedId_.clear();
}

core::Result<void> AccessibilityTree::dispatch(
    std::string_view id, SemanticAction action,
    const std::function<core::Result<void>(std::string_view, SemanticAction)>&
        handler) const {
  if (!handler) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Accessibility action handler is unavailable");
  }
  const auto hasAction = [id, action](const SemanticNode& node,
                                      const auto& self) -> bool {
    if (node.id == id) {
      return std::find(node.actions.begin(), node.actions.end(), action) !=
             node.actions.end();
    }
    return std::any_of(node.children.begin(), node.children.end(),
                       [&self](const auto& child) {
                         return self(child, self);
                       });
  };
  const auto virtualMatch = [this, id, action]() {
    const auto index = noteIndexForId(id);
    if (!index.has_value()) return false;
    const auto node = noteNodeAt(*index);
    return node.has_value() &&
           std::find(node->actions.begin(), node->actions.end(), action) !=
               node->actions.end();
  }();
  if (!hasAction(root_, hasAction) && !virtualMatch) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Accessibility action is not available");
  }
  return handler(id, action);
}

}
