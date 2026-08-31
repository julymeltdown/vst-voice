#pragma once

#include "seam/native_ui/voicebank_studio.hpp"

namespace seam::native_ui {

void paintProductionAssignmentRail(
    RasterCanvas& canvas, const VoicebankStudioController& controller,
    const VoicebankStudioTheme& theme) noexcept;
void paintProductionEmptyCanvas(
    RasterCanvas& canvas, const VoicebankStudioController& controller,
    const VoicebankStudioTheme& theme) noexcept;
void paintProductionInspector(
    RasterCanvas& canvas, const VoicebankStudioController& controller,
    const VoicebankStudioTheme& theme,
    const voicebank::Unit* selectedUnit) noexcept;

}
