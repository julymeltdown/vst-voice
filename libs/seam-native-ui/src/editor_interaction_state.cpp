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
  if (!hoveredNote_.has_value() && !detail_.has_value()) return false;
  hoveredNote_.reset();
  detail_.reset();
  return true;
}

}
