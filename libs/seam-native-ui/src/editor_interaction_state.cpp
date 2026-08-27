#include "seam/native_ui/editor_interaction_state.hpp"

namespace seam::native_ui {

bool EditorInteractionState::updateHoveredNote(domain::NoteId noteId,
                                                std::string value) {
  if (hoveredNote_ == noteId && detail_.has_value() &&
      detail_->value == value) {
    return false;
  }
  hoveredNote_ = noteId;
  detail_ = EditorDetail{.kind = EditorDetailKind::Note,
                         .stableId = noteId.toString(),
                         .value = std::move(value)};
  return true;
}

bool EditorInteractionState::clearHover() noexcept {
  if (!hoveredNote_.has_value()) return false;
  hoveredNote_.reset();
  detail_ = focusedDetail_;
  return true;
}

bool EditorInteractionState::updateFocusedNote(domain::NoteId noteId,
                                                std::string value) {
  if (focusedNote_ == noteId && focusedDetail_.has_value() &&
      focusedDetail_->value == value) {
    return false;
  }
  focusedNote_ = noteId;
  focusedDetail_ = EditorDetail{.kind = EditorDetailKind::Note,
                                .stableId = noteId.toString(),
                                .value = std::move(value)};
  if (!hoveredNote_.has_value()) detail_ = focusedDetail_;
  return true;
}

bool EditorInteractionState::clearFocus() noexcept {
  if (!focusedNote_.has_value()) return false;
  focusedNote_.reset();
  focusedDetail_.reset();
  if (!hoveredNote_.has_value()) detail_.reset();
  return true;
}

}
