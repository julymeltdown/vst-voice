#include "voicebank_studio_production_view.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace seam::native_ui {
namespace {
std::string_view sourceQualificationLabel(const voicebank_production::VoicebankProductionProject& project) {
  return voicebank_production::selectedStrategyReady(project)
      ? "SOURCE QUALIFICATION READY" : "SOURCE QUALIFICATION PENDING";
}
} // namespace

std::vector<StudioSampleReviewControl> studioSampleReviewControls(
    const VoicebankStudioController& controller, double width) {
  const bool available = !controller.proceduralImportBusy();
  const bool selected = controller.selectedUnit() && controller.selectedProductionAssignment();
  const auto& inspection = controller.sampleReviewInspection();
  const bool captured = inspection && controller.validateSampleReviewContext(inspection->context);
  const auto buttonWidth = std::max(1.0, (width - 48.0) / 6.0);
  std::vector<StudioSampleReviewControl> controls;
  const auto add = [&](const char* id, const char* label, bool enabled) {
    const auto index = controls.size();
    controls.push_back({id, label, {24.0 + static_cast<double>(index % 6U) * buttonWidth,
        66.0 + static_cast<double>(index / 6U) * 30.0, buttonWidth - 4.0, 24.0}, enabled});
  };
  add("capture", "CAPTURE UNIT", available && selected);
  add("reviewer", "CHOOSE REVIEWER", available && captured && !inspection->reviewers.empty());
  add("accept", "ACCEPT...", available && captured && !controller.sampleReviewerId().empty());
  add("reject", "REJECT...", available && captured && !controller.sampleReviewerId().empty());
  add("publish", "PUBLISH NEW...", available && selected);
  add("back", "BACK TO EDITOR", available);
  add("previous-unit", "PREVIOUS UNIT", available && controller.selectedIndex() > 0U);
  add("next-unit", "NEXT UNIT", available && controller.selectedIndex() + 1U < controller.selectableUnitCount());
  add("previous-page", "PREVIOUS DATA", true);
  add("next-page", "MORE DATA", true);
  add("play", "PLAY / STOP", available && captured && inspection->audio);
  add("cancel", "CANCEL WORK", !available);
  controls.push_back({"open-manifest", "OPEN MANIFEST...", {width - 190.0, 16.0, 166.0, 22.0}, available && controller.productionProject() && !controller.dirty()});
  const auto* project = controller.productionProject();
  const bool hasTake = project && std::any_of(project->unitAssignments.begin(), project->unitAssignments.end(),
      [](const auto& assignment) { return !assignment.takeId.empty(); });
  controls.push_back({"create-draft", "CREATE DRAFT...", {width - 366.0, 16.0, 168.0, 22.0}, available && hasTake && !controller.dirty()});
  controls.push_back({"source-evidence", "SOURCE EVIDENCE...", {width - 366.0, 40.0, 168.0, 22.0}, available && hasTake});
  const auto& source = controller.sourceQualityInspection();
  const bool sourceCaptured = source && controller.validateSampleReviewContext(source->context);
  controls.push_back({"source-decision", "ASSESS SOURCE...", {width - 190.0, 40.0, 166.0, 22.0}, available && sourceCaptured && !source->reviewers.empty()});
  controls.push_back({"source-license", "L CAPTURE SOURCE LICENSE...", {24.0,126.0,(width-52.0)/2.0,24.0}, available && project && project->schemaVersion>=2});
  const auto& registration = controller.sourceRegistrationInspection();
  controls.push_back({"source-register", "S REGISTER NEW SOURCE...", {26.0+(width-48.0)/2.0,126.0,(width-52.0)/2.0,24.0},
      available && registration && controller.validateSampleReviewContext(registration->context)});
  return controls;
}

std::size_t studioSampleReviewVisibleLines(double height) noexcept {
  return static_cast<std::size_t>(std::max(1.0, (height - 316.0) / 18.0));
}

std::vector<std::string> studioSampleReviewDetailLines(const VoicebankStudioController& controller, double width) {
  std::vector<std::string> values{
      controller.sampleReviewStatus(),
      "Capture is read-only. Choose the actual registered reviewer, inspect/listen, then explicitly accept or reject.",
      "Publication requires complete accepted coverage; it creates a new engineering directory, not a signed release.",
      "Template export remains a separate preparation operation; it is not this publication workflow."};
  if (const auto* project = controller.productionProject()) values.emplace_back(sourceQualificationLabel(*project));
  if (const auto& registration = controller.sourceRegistrationInspection()) {
    values.push_back("CAPTURED SOURCE LICENSE / PRODUCER " + registration->producerId);
    values.push_back("LICENSE " + registration->evidencePath.generic_string());
    values.push_back("LICENSE SHA256 " + registration->evidenceSha256);
    values.push_back("REGISTER NEW SOURCE records explicit declarations only; existing take ownership stays unchanged.");
  }
  if (const auto& receipt = controller.sourceRegistrationReceipt()) {
    values.push_back("LAST SOURCE REGISTRATION " + std::to_string(receipt->committedGeneration) + " / " + receipt->committedProjectSha256);
    if (!receipt->diagnostic.empty()) values.push_back(receipt->diagnostic);
  }
  values.push_back("I CAPTURE SOURCE EVIDENCE / D RECORD SOURCE QUALITY / SOURCE DECISIONS NEVER APPROVE UNITS");
  if (const auto& source = controller.sourceQualityInspection()) values.insert(values.end(),source->details.begin(),source->details.end());
  if (const auto& receipt = controller.sourceQualityReceipt()) {
    values.push_back("LAST SOURCE QUALITY COMMIT " + std::to_string(receipt->committedGeneration) + " / " + receipt->committedProjectSha256);
    if (!receipt->diagnostic.empty()) values.push_back(receipt->diagnostic);
  }
  if (const auto& draft = controller.createdSampleManifestDraft()) {
    values.push_back("LAST CREATED EDITABLE DRAFT " + draft->root.string());
    values.push_back("INITIAL MARKERS AND PITCH: ESTIMATED / UNAPPROVED / NO SOURCE OR RELEASE QUALIFICATION");
    values.push_back("DRAFT SHA256 " + draft->draftSha256);
    values.push_back("INITIAL MANIFEST SHA256 " + draft->manifestSha256);
    values.push_back("DRAFT SOURCE GENERATION " + std::to_string(draft->sourceGeneration) + " / " + draft->sourceProjectSha256);
    values.push_back(draft->durabilityConfirmed ? "DRAFT DIRECTORY DURABILITY CONFIRMED" : "DRAFT COMMITTED / DURABILITY UNCONFIRMED / INSPECT BEFORE RETRY");
    for (const auto& missing : draft->missingAssignments) values.push_back("MISSING ASSIGNMENT " + missing);
    values.insert(values.end(), draft->diagnostics.begin(), draft->diagnostics.end());
    if (!controller.sampleManifestDraftLoadDiagnostic().empty()) values.push_back(controller.sampleManifestDraftLoadDiagnostic());
  }
  if (const auto& published = controller.publishedSampleCandidate()) {
    values.push_back("LAST PUBLISHED " + published->root.string());
    values.push_back("CANDIDATE SHA256 " + published->candidateSha256);
    values.push_back("CONTENT SHA256 " + published->contentSha256);
    values.push_back("MANIFEST SHA256 " + published->manifestSha256);
    values.push_back("PUBLICATION GENERATION " + std::to_string(published->sourceGeneration));
    if (!published->diagnostic.empty()) values.push_back(published->diagnostic);
  }
  if (const auto& receipt = controller.sampleReviewReceipt()) {
    values.push_back("LAST REVIEW COMMIT " + std::to_string(receipt->committedGeneration) + " / " + receipt->committedProjectSha256);
    for (const auto& review : receipt->reviews)
      values.push_back("REVIEW " + review.reviewId + " / " + review.reviewerId + " / " + review.result + " / " + review.reviewedAtUtc);
    if (!receipt->diagnostic.empty()) values.push_back(receipt->diagnostic);
  }
  if (const auto& inspection = controller.sampleReviewInspection()) {
    if (!controller.validateSampleReviewContext(inspection->context)) values.push_back("STALE CAPTURE: Recapture after edits. Acceptance and playback are disabled.");
    values.insert(values.end(), inspection->details.begin(), inspection->details.end());
    values.push_back("REGISTERED REVIEWERS (registration does not prove independence):");
    values.insert(values.end(), inspection->reviewers.begin(), inspection->reviewers.end());
  } else values.push_back("CREATE DRAFT uses current producer takes with explicit bank/language/style identity. OPEN MANIFEST loads an existing bank model. Back to Editor exposes marker/pitch editing; then CAPTURE UNIT for review.");
  // Character-cell wrapping also keeps long hashes and filesystem locators
  // inspectable at the minimum 720px width instead of clipping their suffixes.
  const auto columns = static_cast<std::size_t>(std::max(16.0, (width - 56.0) / 12.0));
  std::vector<std::string> lines;
  for (const auto& value : values) {
    if (value.empty()) { lines.emplace_back(); continue; }
    std::size_t offset = 0U;
    while (offset < value.size()) {
      auto end = std::min(value.size(), offset + columns);
      while (end < value.size() && end > offset && (static_cast<unsigned char>(value[end]) & 0xc0U) == 0x80U) --end;
      if (end == offset) end = std::min(value.size(), offset + columns);
      // Prefer complete words, retaining the separator so concatenating the
      // rows reconstructs every original byte. Long hashes still hard-wrap.
      if (end < value.size()) {
        const auto space = value.find_last_of(" \t", end - 1U);
        if (space != std::string::npos && space > offset) end = space + 1U;
      }
      lines.push_back(value.substr(offset, end - offset)); offset = end;
    }
  }
  return lines;
}

void paintStudioSampleReview(RasterCanvas& canvas, const VoicebankStudioController& controller,
    std::size_t firstDetailLine, std::string_view interactionStatus) {
  const VoicebankStudioTheme theme;
  const auto width = canvas.logicalWidth(), height = canvas.logicalHeight();
  canvas.fillRect({0.0, 0.0, width, height}, theme.background);
  canvas.drawText({24.0, 16.0, width - 406.0, 20.0}, "UNIT REVIEW / ENGINEERING ONLY", theme.primaryText, 14.0);
  canvas.drawText({24.0, 40.0, width - 406.0, 16.0},
      "UNIT REVIEWER: " + (controller.sampleReviewerId().empty() ? std::string{"NONE SELECTED"} : controller.sampleReviewerId()), theme.accent, 11.0);
  for (const auto& control : studioSampleReviewControls(controller, width)) {
    canvas.fillRect(control.bounds, control.enabled ? theme.selected : theme.panelAlternate);
    canvas.drawText({control.bounds.x + 4.0, control.bounds.y + 4.0, control.bounds.width - 8.0, control.bounds.height - 8.0},
        control.label, control.enabled ? theme.primaryText : theme.secondaryText, 10.0);
  }
  canvas.drawText({24.0, 158.0, width - 48.0, 18.0},
      "UNIT " + std::to_string(controller.selectableUnitCount() == 0U ? 0U : controller.selectedIndex() + 1U) + " / " + std::to_string(controller.selectableUnitCount()) +
      " | N DRAFT | O OPEN | C CAPTURE | R REVIEWER | A/X REVIEW | I/D SOURCE | E PUBLISH | SPACE PLAY", theme.secondaryText, 10.0);
  const ui::Rect wave{24.0, 184.0, width - 48.0, 54.0};
  canvas.fillRect(wave, theme.panelAlternate);
  if (const auto& inspection = controller.sampleReviewInspection(); inspection && !inspection->peaks.empty()) {
    const auto step = wave.width / static_cast<double>(inspection->peaks.size());
    for (std::size_t i = 0U; i < inspection->peaks.size(); ++i) {
      const auto [low, high] = inspection->peaks[i];
      const auto x = wave.x + (static_cast<double>(i) + 0.5) * step;
      canvas.line({x, wave.y + wave.height*0.5 - wave.height*0.45 * static_cast<double>(high)},
          {x, wave.y + wave.height*0.5 - wave.height*0.45 * static_cast<double>(low)}, theme.waveform);
    }
    const auto& unit = inspection->packet.manifest.units.front();
    const auto frames = static_cast<double>(inspection->audio->frameCount());
    const auto drawMarker = [&](time::SampleFrame frame, Color color) {
      const auto x = wave.x + std::clamp(static_cast<double>(frame) / frames, 0.0, 1.0) * wave.width;
      canvas.line({x, wave.y}, {x, wave.bottom()}, color);
    };
    drawMarker(unit.markers.audioOffset, theme.accent); drawMarker(unit.markers.consonantEnd, theme.accent);
    drawMarker(unit.markers.vowelOnset, theme.accent); drawMarker(unit.markers.stableStart, theme.pitch);
    drawMarker(unit.markers.audioEnd, theme.accent);
    if (unit.markers.loopStart) drawMarker(*unit.markers.loopStart, theme.pitch);
    if (unit.markers.loopEnd) drawMarker(*unit.markers.loopEnd, theme.pitch);
    if (unit.markers.releaseStart) drawMarker(*unit.markers.releaseStart, theme.accent);
    // Exact pitch origins remain paged below; avoid drawing thousands of
    // indistinguishable pitch lines over an 80px waveform.
  }
  const auto lines = studioSampleReviewDetailLines(controller, width);
  const auto first = std::min(firstDetailLine, lines.empty() ? 0U : lines.size() - 1U);
  const auto count = std::min(studioSampleReviewVisibleLines(height), lines.size() - first);
  canvas.drawText({24.0, 246.0, width - 48.0, 16.0},
      "REVIEW DATA " + std::to_string(first + 1U) + "-" + std::to_string(first + count) + " / " + std::to_string(lines.size()) +
          " | LEFT/RIGHT PAGE | UP/DOWN UNIT", theme.secondaryText, 10.0);
  for (std::size_t i = 0U; i < count; ++i)
    canvas.drawText({24.0, 274.0 + static_cast<double>(i) * 18.0, width - 48.0, 16.0}, lines[first + i], theme.primaryText, 11.0);
  canvas.drawText({24.0, height - 28.0, width - 48.0, 18.0}, interactionStatus.empty() ? controller.sampleReviewStatus() : interactionStatus, theme.accent, 11.0);
}

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

std::vector<StudioSampleReviewControl> studioGenerationControls(
    const VoicebankStudioController& controller, double width, bool recordingActive) {
  if (!controller.productionProject() || !controller.manifest().units.empty()) return {};
  const auto areaWidth=std::max(0.0,width-574.0);
  if (areaWidth<120.0) return {};
  const auto cellWidth=areaWidth/2.0;
  const bool busy=controller.proceduralImportBusy();
  const bool enabled=controller.selectedProductionAssignment() && !busy && !recordingActive;
  return {{"prepare","Prepare",{294.0,268.0,cellWidth-2.0,18.0},enabled},
          {"generate","Run job",{294.0+cellWidth,268.0,cellWidth-2.0,18.0},enabled},
          {"assemble","Make batch",{294.0,286.0,cellWidth-2.0,18.0},enabled},
          {busy?"cancel":"batch",busy?"Cancel work":"Run batch",{294.0+cellWidth,286.0,cellWidth-2.0,18.0},busy || enabled}};
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
                  region.width < 320.0 ? "INTAKE" : "PRODUCTION INTAKE", theme.primaryText, 14.0);
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
                  "R REC / CMD/CTRL-I IMPORT / SHIFT-B BUILD", theme.secondaryText, 6.0);
  canvas.drawText(ui::Rect{region.x + 24.0, region.y + 196.0,
                           std::max(0.0, region.width - 48.0), 16.0},
                  "CMD/CTRL-SHIFT: I JOB / B BATCH",
                  theme.secondaryText, 7.0);
  canvas.drawText(ui::Rect{region.x + 24.0, region.y + 214.0,
                           std::max(0.0, region.width - 48.0), 16.0},
                  "SHIFT-P PREPARE / P WAVE / N F0", theme.secondaryText, 7.0);
  const auto left = region.x + 24.0;
  const auto contentWidth = std::max(0.0, region.width - 48.0);
  if (!controller.candidateMarkerError().empty()) {
    canvas.drawText(ui::Rect{left, region.y + 232.0, contentWidth, 24.0},
        controller.candidateMarkerError(), theme.accent, 8.0);
    return;
  }
  const auto& preview = controller.candidateMarkerPreview();
  if (!preview) return;
  canvas.drawText(ui::Rect{left, region.y + 232.0, contentWidth, 16.0},
      "LEFT/RIGHT GESTURE", theme.primaryText, 7.0);
  canvas.drawText(ui::Rect{left, region.y + 252.0, contentWidth, 16.0},
      "NOT MEASURED", theme.accent, 7.0);
  canvas.drawText(ui::Rect{left, region.y + 270.0, contentWidth, 16.0},
      "NOT APPROVED", theme.accent, 7.0);
  const auto& waveform = controller.candidateWaveform();
  const auto rowTop = std::min(region.y + (waveform ? 404.0 : 318.0), height - 64.0);
  const auto [first, visible] = controller.candidateMarkerWindow(static_cast<std::size_t>(
      std::max(0.0, height - rowTop - 16.0) / 48.0));
  canvas.drawText(ui::Rect{left, region.y + 290.0, contentWidth, 16.0},
      std::to_string(controller.selectedCandidateMarker() + 1U) + " OF " + std::to_string(preview->markers.size()) +
          (controller.candidateMarkerDragging() ? " DRAFT" : (controller.candidateMarkersEdited() ? " MANUAL" : " PLANNED")),
      theme.secondaryText, 7.0);
  if (!controller.candidateWaveformError().empty()) {
    canvas.drawText(ui::Rect{left, region.y + 310.0, contentWidth, 16.0},
        "RAW AUDIO LOAD FAILED", theme.accent, 7.0);
    return;
  }
  if (waveform && !waveform->peaks.empty()) {
    const auto wave = VoicebankStudioController::candidateWaveformBounds(width, height);
    canvas.fillRect(wave, theme.panelAlternate);
    const auto middle = wave.y + wave.height / 2.0;
    canvas.line({wave.x, middle}, {wave.right(), middle}, theme.grid);
    const auto& peaks = controller.candidateWaveformPeaks();
    const auto [viewStart, viewEnd] = controller.candidateWaveformView();
    const auto step = wave.width / static_cast<double>(peaks.size());
    double amplitude = 0.0;
    for (const auto& [low, high] : peaks) {
      amplitude = std::max({amplitude, std::abs(static_cast<double>(low)), std::abs(static_cast<double>(high))});
    }
    if (amplitude == 0.0) amplitude = 1.0;
    if (!controller.candidatePitchView()) for (std::size_t index = 0U; index < peaks.size(); ++index) {
      const auto [low, high] = peaks[index];
      const auto x = wave.x + (static_cast<double>(index) + 0.5) * step;
      const auto gain = std::max(0.0, wave.height / 2.0 - 4.0);
      canvas.line({x, middle - static_cast<double>(high) / amplitude * gain},
                  {x, middle - static_cast<double>(low) / amplitude * gain}, theme.waveform);
    }
    const auto scale = wave.width / static_cast<double>(viewEnd - viewStart);
    if (controller.candidatePitchView()) {
      std::optional<ui::Point> previous;
      for (const auto& point : controller.candidatePitchInspection()->contour) {
        if (point.sourceFrame < static_cast<double>(viewStart) || point.sourceFrame >= static_cast<double>(viewEnd)) {
          previous.reset(); continue;
        }
        const ui::Point plotted{wave.x + (point.sourceFrame - static_cast<double>(viewStart)) * scale,
            middle - std::clamp(point.centsFromTarget / 1200.0, -1.0, 1.0) * std::max(0.0, wave.height / 2.0 - 4.0)};
        if (previous && point.connectedToPrevious) canvas.line(*previous, plotted, theme.pitch);
        canvas.fillRect({plotted.x - 1.0, plotted.y - 1.0, 3.0, 3.0},
            std::abs(point.centsFromTarget) > 1200.0 ? theme.accent : theme.pitch);
        previous = plotted;
      }
    }
    for (std::size_t index = 0U; index < preview->markers.size(); ++index) {
      const auto& marker = preview->markers[index];
      if (marker.ownedSpan.end <= viewStart || marker.ownedSpan.start >= viewEnd) continue;
      const auto x = wave.x + static_cast<double>(std::max(marker.ownedSpan.start, viewStart) - viewStart) * scale;
      const auto end = wave.x + static_cast<double>(std::min(marker.ownedSpan.end, viewEnd) - viewStart) * scale;
      canvas.strokeRect({x, wave.y, end - x, wave.height},
          index == controller.selectedCandidateMarker() ? theme.pitch : theme.accent, 1.0);
      if (index == controller.selectedCandidateMarker()) {
        if (marker.ownedSpan.start >= viewStart) canvas.fillRect({x - 2.0, wave.y + 2.0, 4.0, 8.0}, theme.pitch);
        if (marker.ownedSpan.end <= viewEnd) canvas.fillRect({end - 2.0, wave.bottom() - 10.0, 4.0, 8.0}, theme.pitch);
      }
    }
    canvas.drawText(ui::Rect{left, wave.bottom() + 4.0, contentWidth, 12.0},
        controller.candidatePitchView() ? "F0 EST +/-1200C / B WAVE" :
            std::to_string(viewStart) + " TO " + std::to_string(viewEnd) + " / AUTO", theme.secondaryText, 6.0);
  }
  for (std::size_t index = 0U; index < visible; ++index) {
    const auto& marker = preview->markers[first + index];
    const auto top = rowTop + static_cast<double>(index) * 48.0;
    if (first + index == controller.selectedCandidateMarker())
      canvas.fillRect({left, top, contentWidth, 44.0}, theme.selected);
    canvas.drawText(ui::Rect{left, top, contentWidth, 14.0},
        marker.phone + "  [" + std::to_string(marker.ownedSpan.start) + ", " +
            std::to_string(marker.ownedSpan.end) + ") FRAMES", theme.primaryText, 7.0);
    canvas.drawText(ui::Rect{left, top + 16.0, contentWidth, 12.0}, marker.key.toString(), theme.secondaryText, 6.0);
    canvas.fillRect(ui::Rect{left, top + 32.0, contentWidth, 5.0}, theme.grid);
    const auto scale = contentWidth / static_cast<double>(preview->frameCount);
    canvas.fillRect(ui::Rect{left + static_cast<double>(marker.ownedSpan.start) * scale, top + 32.0,
        static_cast<double>(marker.ownedSpan.end - marker.ownedSpan.start) * scale, 5.0}, theme.accent);
  }
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
                  sourceQualificationLabel(*production),
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
  const auto& waveform = controller.candidateWaveform();
  if (selectedUnit == nullptr && waveform) {
    const auto topOfMeasurements = rowOne + 94.0;
    const auto format = [](double value) {
      std::ostringstream stream;
      stream << std::scientific << std::setprecision(3) << value;
      return stream.str();
    };
    const auto line = [&](double offset, const std::string& value, Color color) {
      if (topOfMeasurements + offset + 14.0 <= canvas.logicalHeight() - 12.0)
        canvas.drawText(ui::Rect{left, topOfMeasurements + offset, 214.0, 14.0}, value, color, 7.0);
    };
    line(0.0, "MEASURED RAW SIGNAL", theme.primaryText);
    line(18.0, "WHOLE TAKE / NOT APPROVAL", theme.accent);
    line(42.0, "PEAK " + format(waveform->statistics.peak), theme.secondaryText);
    line(60.0, "RMS " + format(waveform->statistics.rms), theme.secondaryText);
    line(78.0, "DC " + format(waveform->statistics.dcOffset), theme.secondaryText);
    line(96.0, "ABS >= 0.9999: " + std::to_string(waveform->statistics.clippedSamples), theme.secondaryText);
    line(120.0, "RATE " + std::to_string(waveform->sampleRate) + " HZ", theme.secondaryText);
    line(138.0, "FRAMES " + std::to_string(waveform->frameCount), theme.secondaryText);
    const auto& pitch = controller.candidatePitchInspection();
    if (pitch) {
      line(162.0, "F0 EST. VOICED " + std::to_string(pitch->voicedFrames) + "/" + std::to_string(pitch->frames.size()), theme.primaryText);
      line(180.0, pitch->medianHz ? "MEDIAN HZ " + format(*pitch->medianHz) : "NO VOICED ESTIMATE", theme.secondaryText);
      line(198.0, pitch->centsFromTarget ? "VS MIDI " + std::to_string(pitch->targetMidi) + ": " + format(*pitch->centsFromTarget) + " C" :
          "TARGET MIDI " + std::to_string(pitch->targetMidi), theme.secondaryText);
    } else if (!controller.candidatePitchError().empty()) {
      line(162.0, "PITCH INSPECTION FAILED", theme.accent);
    }
  }
}

}
