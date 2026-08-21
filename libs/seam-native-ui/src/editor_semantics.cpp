#include "seam/native_ui/editor_semantics.hpp"

#include <algorithm>

namespace seam::native_ui {

std::string_view semanticRoleName(SemanticRole role) noexcept {
  switch (role) {
    case SemanticRole::Window: return "window";
    case SemanticRole::Toolbar: return "toolbar";
    case SemanticRole::Button: return "button";
    case SemanticRole::Timeline: return "timeline";
    case SemanticRole::Note: return "note";
    case SemanticRole::Lane: return "lane";
    case SemanticRole::Status: return "status";
    case SemanticRole::TextField: return "text-field";
  }
  return "unknown";
}

std::string_view semanticActionName(SemanticAction action) noexcept {
  switch (action) {
    case SemanticAction::Activate: return "activate";
    case SemanticAction::SetFocus: return "set-focus";
    case SemanticAction::EditText: return "edit-text";
    case SemanticAction::Toggle: return "toggle";
  }
  return "unknown";
}

SemanticNode EditorSemanticTree::build(const EditorSceneState& state,
                                       const ui::PianoRollModel& model,
                                       EditorSceneLayout layout) {
  SemanticNode root{
      .id = "editor",
      .role = SemanticRole::Window,
      .name = "Project SEAM editor",
      .value = state.projectName,
      .bounds = ui::Rect{0.0, 0.0, model.viewport().bounds.width,
                         model.viewport().bounds.height + layout.contentTop()},
      .enabled = true,
      .focused = false,
      .actions = {SemanticAction::SetFocus},
      .children = {},
  };
  root.children.push_back(SemanticNode{
      .id = "toolbar.transport",
      .role = SemanticRole::Toolbar,
      .name = "Transport",
      .value = state.playing ? "Playing" : "Stopped",
      .bounds = ui::Rect{0.0, 0.0, root.bounds.width, layout.toolbarHeight},
      .enabled = true,
      .focused = false,
      .actions = {SemanticAction::Activate, SemanticAction::Toggle},
  });
  root.children.push_back(SemanticNode{
      .id = "timeline",
      .role = SemanticRole::Timeline,
      .name = "Arrangement timeline",
      .value = std::to_string(state.revision),
      .bounds = ui::Rect{0.0, layout.contentTop(), root.bounds.width,
                         model.viewport().bounds.height},
      .enabled = true,
      .focused = false,
      .actions = {SemanticAction::SetFocus},
  });
  for (const auto& note : model.visibleNotes()) {
    root.children.push_back(SemanticNode{
        .id = "note." + note.noteId.toString(),
        .role = SemanticRole::Note,
        .name = note.lyric.empty() ? "Note" : "Note " + note.lyric,
        .value = std::to_string(note.midiKey),
        .bounds = note.bounds,
        .enabled = true,
        .focused = note.selected,
        .actions = {SemanticAction::Activate, SemanticAction::SetFocus,
                    SemanticAction::EditText},
    });
  }
  for (const auto& lane : {std::pair<std::string, double>{"phoneme", layout.phonemeLaneHeight},
                           {"unit", layout.unitLaneHeight},
                           {"seam", layout.seamLaneHeight},
                           {"pitch", layout.automationLaneHeight}}) {
    root.children.push_back(SemanticNode{
        .id = "lane." + lane.first,
        .role = SemanticRole::Lane,
        .name = lane.first + " lane",
        .value = {},
        .bounds = ui::Rect{0.0, 0.0, root.bounds.width, lane.second},
        .enabled = true,
        .focused = false,
        .actions = {SemanticAction::SetFocus},
    });
  }
  root.children.push_back(SemanticNode{
      .id = "status.render",
      .role = SemanticRole::Status,
      .name = "Render and audio status",
      .value = state.audioDeviceOnline ? state.audioBackend : "Audio unavailable",
      .bounds = ui::Rect{0.0, root.bounds.height - layout.statusHeight,
                         root.bounds.width, layout.statusHeight},
      .enabled = true,
      .focused = false,
      .actions = {SemanticAction::SetFocus},
  });
  return root;
}

bool EditorSemanticTree::containsId(const SemanticNode& root,
                                    std::string_view id) noexcept {
  if (root.id == id) return true;
  return std::any_of(root.children.begin(), root.children.end(),
                     [id](const auto& child) {
                       return EditorSemanticTree::containsId(child, id);
                     });
}

}
