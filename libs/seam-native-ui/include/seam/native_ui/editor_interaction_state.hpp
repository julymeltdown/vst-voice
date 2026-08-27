#pragma once

#include "seam/domain/ids.hpp"

#include <optional>
#include <string>

namespace seam::native_ui {

enum class EditorDetailKind { Note, Phoneme, Unit, Overlap, Voice, Diagnostic };

struct EditorDetail final {
  EditorDetailKind kind{EditorDetailKind::Note};
  std::string stableId;
  std::string value;
};

class EditorInteractionState final {
public:
  [[nodiscard]] bool updateHoveredNote(domain::NoteId noteId,
                                       std::string value);
  [[nodiscard]] bool clearHover() noexcept;
  [[nodiscard]] bool updateFocusedNote(domain::NoteId noteId,
                                       std::string value);
  [[nodiscard]] bool clearFocus() noexcept;
  [[nodiscard]] const std::optional<domain::NoteId>& hoveredNote() const noexcept {
    return hoveredNote_;
  }
  [[nodiscard]] const std::optional<EditorDetail>& detail() const noexcept {
    return detail_;
  }
  [[nodiscard]] const std::optional<domain::NoteId>& focusedNote() const noexcept {
    return focusedNote_;
  }

private:
  std::optional<domain::NoteId> hoveredNote_;
  std::optional<domain::NoteId> focusedNote_;
  std::optional<EditorDetail> detail_;
  std::optional<EditorDetail> focusedDetail_;
};

}
