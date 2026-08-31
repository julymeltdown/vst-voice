#include "voicebank_studio_production_view.hpp"

#include <algorithm>

namespace seam::native_ui {

void paintProductionAssignmentRail(
    RasterCanvas& canvas, const VoicebankStudioController& controller,
    const VoicebankStudioTheme& theme) noexcept {
  const auto* project = controller.productionProject();
  if (project == nullptr) return;
  const auto first = controller.selectedIndex() > 8U
                         ? controller.selectedIndex() - 8U : 0U;
  const auto last = std::min(
      project->unitAssignments.size(),
      first + voicebankStudioUnitRailVisibleRows(
                  canvas.logicalHeight(), true));
  for (std::size_t index = first; index < last; ++index) {
    const auto& assignment = project->unitAssignments[index];
    const auto y = 108.0 + static_cast<double>(index - first) * 36.0;
    const ui::Rect row{8.0, y, 236.0, 32.0};
    canvas.fillRect(row, index == controller.selectedIndex()
                             ? theme.selected : theme.panelAlternate);
    if (index == controller.selectedIndex()) {
      canvas.strokeRect(row, theme.accent, 1.0);
    }
    canvas.drawText(ui::Rect{16.0, y + 4.0, 150.0, 12.0},
                    assignment.coverageKey, theme.primaryText, 7.0);
    canvas.drawText(ui::Point{172.0, y + 7.0},
                    "P" + std::to_string(assignment.pitchLayer),
                    theme.secondaryText, 7.0);
    canvas.drawText(ui::Rect{16.0, y + 18.0, 208.0, 10.0},
                    voicebank_production::toString(assignment.state),
                    assignment.state == voicebank_production::UnitQueueState::Approved
                        ? theme.pitch : theme.secondaryText,
                    6.0);
  }
}

void paintProductionEmptyCanvas(
    RasterCanvas& canvas, const VoicebankStudioController& controller,
    const VoicebankStudioTheme& theme) noexcept {
  const auto width = canvas.logicalWidth();
  const auto height = canvas.logicalHeight();
  const auto right = std::max(270.0, width - 256.0);
  const ui::Rect region{270.0, 72.0, std::max(0.0, right - 270.0),
                        std::max(0.0, height - 72.0)};
  canvas.fillRect(region, theme.background);
  const auto* assignment = controller.selectedProductionAssignment();
  if (assignment == nullptr) {
    canvas.drawText(ui::Rect{region.x + 24.0, region.y + 32.0,
                             std::max(0.0, region.width - 48.0), 24.0},
                    "NO PRODUCTION UNITS", theme.primaryText, 14.0);
    return;
  }
  canvas.drawText(ui::Rect{region.x + 24.0, region.y + 32.0,
                           std::max(0.0, region.width - 48.0), 24.0},
                  "PRODUCTION INTAKE", theme.primaryText, 14.0);
  canvas.drawText(ui::Rect{region.x + 24.0, region.y + 72.0,
                           std::max(0.0, region.width - 48.0), 18.0},
                  assignment->coverageKey + "  PITCH " +
                      std::to_string(assignment->pitchLayer),
                  theme.accent, 9.0);
  canvas.drawText(ui::Rect{region.x + 24.0, region.y + 104.0,
                           std::max(0.0, region.width - 48.0), 16.0},
                  "PROMPT " + assignment->promptId,
                  theme.secondaryText, 8.0);
  canvas.drawText(ui::Rect{region.x + 24.0, region.y + 132.0,
                           std::max(0.0, region.width - 48.0), 16.0},
                  assignment->takeId.empty() ? "NO AUDIO IMPORTED"
                                             : "TAKE " + assignment->takeId,
                  theme.secondaryText, 8.0);
  canvas.drawText(ui::Rect{region.x + 24.0, region.y + 176.0,
                           std::max(0.0, region.width - 48.0), 16.0},
                  "PRESS R TO RECORD.", theme.secondaryText, 8.0);
  canvas.drawText(ui::Rect{region.x + 24.0, region.y + 196.0,
                           std::max(0.0, region.width - 48.0), 16.0},
                  "INSPECTED AUDIO ENTERS IMMUTABLE STORAGE.",
                  theme.secondaryText, 8.0);
}

void paintProductionInspector(
    RasterCanvas& canvas, const VoicebankStudioController& controller,
    const VoicebankStudioTheme& theme,
    const voicebank::Unit* selectedUnit) noexcept {
  const auto* production = controller.productionProject();
  if (production == nullptr) return;
  const auto inspectorX = canvas.logicalWidth() - 238.0;
  if (selectedUnit == nullptr) {
    canvas.fillRect(ui::Rect{inspectorX, 72.0, 238.0,
                             canvas.logicalHeight() - 72.0}, theme.panel);
  }
  const auto top = selectedUnit == nullptr ? 88.0 : 250.0;
  if (selectedUnit != nullptr) {
    const auto selectedState = controller.productionStateForUnit(*selectedUnit);
    canvas.drawText(ui::Rect{inspectorX + 12.0, top, 214.0, 14.0},
                    selectedState.has_value()
                        ? "QUEUE " + voicebank_production::toString(*selectedState)
                        : "QUEUE NOT IN INVENTORY",
                    selectedState == voicebank_production::UnitQueueState::Approved
                        ? theme.pitch : theme.secondaryText,
                    7.0);
  }
  const auto projectTop = top + (selectedUnit == nullptr ? 0.0 : 28.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, projectTop},
                  "PRODUCTION PROJECT", theme.secondaryText, 8.0);
  canvas.drawText(ui::Rect{inspectorX + 12.0, projectTop + 14.0, 214.0, 16.0},
                  production->projectId, theme.primaryText, 7.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, projectTop + 38.0},
                  "GENERATION " + std::to_string(production->lastDurableGeneration),
                  theme.secondaryText, 7.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, projectTop + 56.0},
                  voicebank_production::selectedStrategyReady(*production)
                      ? "STRATEGY RIGHTS FEASIBLE" : "STRATEGY RIGHTS BLOCKED",
                  voicebank_production::selectedStrategyReady(*production)
                      ? theme.pitch : theme.accent,
                  7.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, projectTop + 82.0},
                  "PRODUCTION QUEUES", theme.secondaryText, 8.0);
  const auto queues = controller.productionQueues();
  const auto left = inspectorX + 12.0;
  const auto right = inspectorX + 122.0;
  const auto rowOne = projectTop + 102.0;
  canvas.drawText(ui::Point{left, rowOne}, "MISSING " + std::to_string(queues.missing), theme.secondaryText, 7.0);
  canvas.drawText(ui::Point{right, rowOne}, "REJECTED " + std::to_string(queues.rejected), theme.secondaryText, 7.0);
  canvas.drawText(ui::Point{left, rowOne + 18.0}, "RETAKE " + std::to_string(queues.retake), theme.secondaryText, 7.0);
  canvas.drawText(ui::Point{right, rowOne + 18.0}, "MARKER " + std::to_string(queues.markerReview), theme.secondaryText, 7.0);
  canvas.drawText(ui::Point{left, rowOne + 36.0}, "PITCH " + std::to_string(queues.pitchReview), theme.secondaryText, 7.0);
  canvas.drawText(ui::Point{right, rowOne + 36.0}, "APPROVED " + std::to_string(queues.approved), theme.pitch, 7.0);
  canvas.drawText(ui::Point{left, rowOne + 60.0},
                  "STAGED RECOVERY " +
                      std::to_string(controller.stagedRecoveryCandidateCount()),
                  theme.secondaryText, 7.0);
}

}
