#pragma once

#include "seam/core/result.hpp"
#include "seam/native_ui/design/shell_strings.hpp"
#include "seam/ui/expression_lane.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace seam::native_ui {
class NativeEditorController;
struct EditorSceneState;
}

namespace seam::native_ui::design {

struct ExpressionControl final {
  const char* id;
  Str label;
};

// Published shell IDs use the short forms "breath" and "air". Project channel IDs use
// "breathiness" and "airiness". Keep the shell IDs stable across languages and workspaces.
inline constexpr std::array<ExpressionControl, ui::kExpressionChannelCount> kExpressionControls{{
    {"formant", Str::Formant}, {"breath", Str::Breath}, {"tension", Str::Tension},
    {"air", Str::Air}, {"gender", Str::Gender}, {"growl", Str::Growl},
}};

struct ExpressionKnob final {
  ui::ExpressionChannelDescriptor descriptor;
  double value{0.0};
  std::string refusal;
  std::size_t storedPoints{0U};
  bool supported{false};
};

[[nodiscard]] std::optional<std::size_t> expressionControlIndex(std::string_view id);
[[nodiscard]] std::array<ExpressionKnob, ui::kExpressionChannelCount> expressionKnobs(
    const EditorSceneState& state);
[[nodiscard]] core::Result<void> nudgeExpressionKnob(NativeEditorController& controller,
                                                     std::size_t index, int steps);

}  // namespace seam::native_ui::design
