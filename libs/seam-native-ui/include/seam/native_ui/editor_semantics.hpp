#pragma once

#include "seam/native_ui/editor_scene.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace seam::native_ui {

enum class SemanticRole {
  Window,
  Toolbar,
  Button,
  Timeline,
  Note,
  Lane,
  Status,
  TextField,
};

enum class SemanticAction { Activate, SetFocus, EditText, Toggle };

struct SemanticNode final {
  std::string id;
  SemanticRole role{SemanticRole::Window};
  std::string name;
  std::string value;
  ui::Rect bounds;
  bool enabled{true};
  bool focused{false};
  std::vector<SemanticAction> actions;
  std::vector<SemanticNode> children;
};

class EditorSemanticTree final {
public:
  [[nodiscard]] static SemanticNode build(const EditorSceneState& state,
                                           const ui::PianoRollModel& model,
                                           EditorSceneLayout layout = {});
  [[nodiscard]] static bool containsId(const SemanticNode& root,
                                       std::string_view id) noexcept;
};

[[nodiscard]] std::string_view semanticRoleName(SemanticRole role) noexcept;
[[nodiscard]] std::string_view semanticActionName(SemanticAction action) noexcept;

}
