#pragma once

#include "seam/native_ui/editor_scene.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace seam::native_ui {

enum class SemanticRole {
  Window,
  Panel,
  Toolbar,
  Button,
  Timeline,
  Note,
  Lane,
  Status,
  TextField,
  // A continuous control with a numeric value, range and step (expression knobs).
  Slider,
  // One of a mutually exclusive set; selected marks the chosen one (EMO / SCENE).
  RadioButton,
  // A tab in a tab strip; selected marks the shown tab (workspaces, lane channels).
  Tab,
  // Determinate progress with a numeric value in [minimum, maximum] (render progress).
  ProgressIndicator,
};

enum class SemanticAction { Activate, SetFocus, EditText, Toggle, Increment, Decrement };

[[nodiscard]] std::string vibratoHandleSemanticId(
    domain::NoteId noteId, VibratoHandleKind kind);

struct SemanticNode final {
  std::string id;
  SemanticRole role{SemanticRole::Window};
  std::string name;
  std::string value;
  ui::Rect bounds;
  bool enabled{true};
  bool focused{false};
  bool selected{false};
  std::vector<SemanticAction> actions;
  std::vector<SemanticNode> children;
  std::size_t virtualizedChildCount{0U};
  std::string editableValue;
  std::string description;
  // Numeric presentation for Slider and ProgressIndicator nodes, in display units (for example
  // 68.0 for a 0.68 breath share). value stays the spoken text with its unit.
  std::optional<double> numericValue;
  std::optional<double> numericMinimum;
  std::optional<double> numericMaximum;
  std::optional<double> numericStep;
};

class EditorSemanticTree final {
public:
  [[nodiscard]] static SemanticNode build(const EditorSceneState& state,
                                           const ui::PianoRollModel& model,
                                           EditorSceneLayout layout = {},
                                           bool includeOffscreenNotes = false,
                                           bool includeNotes = true);
  [[nodiscard]] static SemanticNode noteNode(const ui::NoteVisual& note,
                                              EditorSceneLayout layout = {});
  [[nodiscard]] static bool containsId(const SemanticNode& root,
                                       std::string_view id) noexcept;
};

[[nodiscard]] std::string_view semanticRoleName(SemanticRole role) noexcept;
[[nodiscard]] std::string_view semanticActionName(SemanticAction action) noexcept;

}
