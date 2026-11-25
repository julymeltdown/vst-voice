#include "expression_controls.hpp"

#include "seam/native_ui/editor_controller.hpp"

namespace seam::native_ui::design {

std::optional<std::size_t> expressionControlIndex(std::string_view id) {
  for (std::size_t i = 0; i < kExpressionControls.size(); ++i) {
    if (id == kExpressionControls[i].id) return i;
  }
  return std::nullopt;
}

std::array<ExpressionKnob, ui::kExpressionChannelCount> expressionKnobs(const EditorSceneState& state) {
  std::array<ExpressionKnob, ui::kExpressionChannelCount> knobs;
  for (std::size_t i = 0U; i < knobs.size(); ++i) {
    const auto channel = ui::expressionChannelAt(i);
    auto& knob = knobs[i];
    knob.descriptor = ui::describeExpressionChannel(channel);
    knob.value = knob.descriptor.neutral;
    knob.refusal = state.inspector.valid ? "" : tr(Str::NoVocalTrackIsSelected);
    for (const auto& row : state.inspector.expressionCapabilities) {
      if (row.channel != channel) continue;
      knob.supported = row.refusal.empty();
      knob.value = row.valueAtPlayhead;
      knob.refusal = row.refusal;
      knob.storedPoints = row.storedPoints;
    }
    // The knob still reads the region's edge value, but an edit "at the playhead" is refused.
    if (knob.refusal.empty() && !state.playheadInsideRegion)
      knob.refusal = tr(Str::ThePlayheadIsOutsideTheSelected);
  }
  return knobs;
}

core::Result<void> nudgeExpressionKnob(NativeEditorController& controller, std::size_t index, int steps) {
  if (steps == 0) return core::success();
  switch (ui::expressionChannelAt(index)) {
    case ui::ExpressionChannel::Formant: return controller.nudgeFormantShift(steps);
    case ui::ExpressionChannel::Breathiness: return controller.nudgeBreathiness(steps);
    case ui::ExpressionChannel::Tension: return controller.nudgeTension(steps);
    case ui::ExpressionChannel::Airiness: return controller.nudgeAiriness(steps);
    case ui::ExpressionChannel::Gender: return controller.nudgeGender(steps);
    case ui::ExpressionChannel::Growl: return controller.nudgeGrowl(steps);
  }
  return core::success();
}

}  // namespace seam::native_ui::design
