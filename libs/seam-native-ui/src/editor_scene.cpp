#include "seam/native_ui/editor_scene.hpp"

#include "seam/native_ui/editor_frame_layout.hpp"
#include "seam/native_ui/editor_label_policy.hpp"
#include "seam/native_ui/diagnostic_presentation.hpp"

#include "seam/time/tick.hpp"
#include "seam/text/unicode.hpp"
#include "seam/ui/phoneme_lane_model.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <string_view>
#include <utility>

namespace seam::native_ui {
namespace {

std::string formatRevision(std::uint64_t revision) {
  return "REV " + std::to_string(revision);
}

std::string visibleNoteLabel(std::size_t count) {
  return std::to_string(count) + " VISIBLE NOTES";
}

std::string fitUtf8Text(std::string_view value, double width,
                        double characterWidth = 6.0) {
  const auto maxCharacters = static_cast<std::size_t>(std::max(
      0.0, std::floor(width / characterWidth)));
  if (maxCharacters == 0U) return {};
  if (text::utf8DisplayWidth(value) <= maxCharacters) return std::string{value};
  constexpr std::string_view staleSuffix{" STALE AUDIO"};
  if (value.ends_with(staleSuffix) &&
      maxCharacters > text::utf8DisplayWidth(staleSuffix) + 3U) {
    const auto prefixLength = maxCharacters -
                              text::utf8DisplayWidth(staleSuffix) - 3U;
    return text::truncateUtf8ToDisplayWidth(
               value.substr(0U, value.size() - staleSuffix.size()), prefixLength) +
           "..." +
           std::string{staleSuffix};
  }
  if (maxCharacters <= 3U) {
    return text::truncateUtf8ToDisplayWidth(value, maxCharacters);
  }
  return text::truncateUtf8ToDisplayWidth(value, maxCharacters - 3U) + "...";
}

std::string fitStatusText(std::string text, double width) {
  return fitUtf8Text(text, width);
}

struct StatusBarColumns final {
  double noteX{0.0};
  double noteWidth{0.0};
  double renderX{0.0};
  double renderWidth{0.0};
  double audioX{0.0};
  double audioWidth{0.0};
};

StatusBarColumns statusBarColumns(double width,
                                  const EditorSceneLayout& layout) noexcept {
  const auto padding = layout.statusHorizontalPadding;
  const auto contentWidth = std::max(0.0, width - padding * 2.0);
  const auto gap = std::min(layout.statusGap, contentWidth / 6.0);
  const auto segmentWidth = std::max(0.0, contentWidth - gap * 2.0);
  const auto audioWidth = segmentWidth * layout.statusAudioFraction;
  const auto renderWidth = std::min(layout.statusRenderMaxWidth,
                                    segmentWidth * layout.statusRenderFraction);
  const auto noteWidth = std::max(0.0, segmentWidth - audioWidth - renderWidth);
  const auto renderX = padding + noteWidth + gap;
  return StatusBarColumns{
      .noteX = padding,
      .noteWidth = noteWidth,
      .renderX = renderX,
      .renderWidth = renderWidth,
      .audioX = renderX + renderWidth + gap,
      .audioWidth = audioWidth,
  };
}

std::string rendererLabel(domain::UnitRendererKind kind) {
  switch (kind) {
    case domain::UnitRendererKind::Raw: return "RAW";
    case domain::UnitRendererKind::ClassicPsola: return "PSOLA";
    case domain::UnitRendererKind::SpectralClassic: return "SPEC";
    case domain::UnitRendererKind::Stretch: return "STR";
    case domain::UnitRendererKind::Inherit: return "AUTO";
  }
  return "AUTO";
}

std::string characterStateLabel(character::State state) {
  return std::string{character::stateName(state)};
}

double diagnosticHeight(const EditorSceneState& state,
                        const EditorSceneLayout& layout) noexcept {
  return layout.diagnosticHeight(!state.diagnostics.empty());
}

double exportHeight(const EditorSceneState& state,
                    const EditorSceneLayout& layout) noexcept {
  return layout.exportHeight(state.exportProgress.totalFiles != 0U);
}

bool exportCancellable(authoring::ExportState state) noexcept {
  return state == authoring::ExportState::Preflight ||
         state == authoring::ExportState::Staging ||
         state == authoring::ExportState::Prepared;
}

double overlayHeight(const EditorSceneState& state,
                     const EditorSceneLayout& layout) noexcept {
  return diagnosticHeight(state, layout) + exportHeight(state, layout);
}

}  // namespace

TechnicalLaneHeights resolveEditorTechnicalLaneHeights(
    const EditorSceneState& state, const EditorSceneLayout& layout,
    double contentBottom) noexcept {
  auto technical = resolveTechnicalLaneHeights(TechnicalLaneLayoutInput{
      .presentation = state.technicalLanes,
      .populated = {
          !state.phonemes.tokens.empty() || state.technicalLaneAvailable[0U],
          !state.unitOverrides.empty() || state.technicalLaneAvailable[1U],
          !state.seamOverrides.empty() || state.technicalLaneAvailable[2U],
          !state.pitchAutomation.empty() || state.technicalLaneAvailable[3U],
      },
      .previewHeights = { layout.phonemeLaneHeight, layout.unitLaneHeight,
                          layout.seamLaneHeight, layout.automationLaneHeight },
      .contentTop = layout.contentTop(),
      .contentBottom = contentBottom,
  });
  if (!state.technicalLaneHeightsOverride.has_value()) return technical;
  technical.values = *state.technicalLaneHeightsOverride;
  const auto used = std::accumulate(technical.values.begin(),
                                    technical.values.end(), 0.0);
  technical.pianoBottom = std::max(layout.contentTop(), contentBottom - used);
  return technical;
}

bool editorDockVisible(const EditorSceneState& state) noexcept {
  const auto characterFull =
      state.characterMode == domain::CharacterDisplayMode::Full &&
      state.voiceIdentity.characterActive && state.characterPortrait != nullptr;
  const auto arrangementVisible =
      !state.voicebankBrowserVisible && !state.audioSettings.visible &&
      state.characterMode == domain::CharacterDisplayMode::Off &&
      !state.arrangementTracks.empty();
  return state.audioSettings.visible || characterFull || arrangementVisible ||
         state.voicebankBrowserVisible;
}

double resolveEditorDockWidth(const EditorSceneState& state,
                              const EditorSceneLayout& layout) noexcept {
  if (state.dockWidthOverride.has_value()) {
    return std::clamp(*state.dockWidthOverride, 0.0, layout.characterDockWidth);
  }
  return editorDockVisible(state) ? layout.characterDockWidth : 0.0;
}

void EditorScenePainter::paint(RasterCanvas& canvas, ui::PianoRollModel& model,
                               const EditorSceneState& state) const noexcept {
  const auto width = canvas.logicalWidth();
  const auto height = canvas.logicalHeight();
  const auto characterFull =
      state.voiceIdentity.characterActive && state.characterPortrait != nullptr &&
      (state.characterMode == domain::CharacterDisplayMode::Full ||
       (state.characterMode == domain::CharacterDisplayMode::Minimal &&
        state.dockWidthOverride.value_or(0.0) > 0.0));
  const auto audioSettingsVisible = state.audioSettings.visible;
  const auto arrangementVisible =
      !state.voicebankBrowserVisible && !audioSettingsVisible &&
      state.characterMode == domain::CharacterDisplayMode::Off &&
      !state.arrangementTracks.empty();
  const auto dockWidth = resolveEditorDockWidth(state, layout_);
  const auto dockVisible = dockWidth > 0.0;
  const auto statusTop = height - layout_.statusHeight;
  const auto overlayInset = overlayHeight(state, layout_);
  const auto technical = resolveEditorTechnicalLaneHeights(
      state, layout_, height - layout_.statusHeight - overlayInset);
  const auto pianoBottom = technical.pianoBottom;
  const auto contentHeight = std::max(1.0, pianoBottom - layout_.contentTop());
  const auto laneGeometry = EditorSceneLayout::TechnicalLaneGeometry{
      .pianoBottom = pianoBottom,
      .phonemeTop = pianoBottom,
      .phonemeHeight = technical.values[0U],
      .unitTop = pianoBottom + technical.values[0U],
      .unitHeight = technical.values[1U],
      .seamTop = pianoBottom + technical.values[0U] + technical.values[1U],
      .seamHeight = technical.values[2U],
      .pitchTop = pianoBottom + technical.values[0U] + technical.values[1U] +
                  technical.values[2U],
      .pitchHeight = technical.values[3U],
      .bottom = height - layout_.statusHeight - overlayInset,
  };
  const auto frame = buildEditorFrameLayout(EditorFrameLayoutInput{
      .logicalWidth = width,
      .logicalHeight = height,
      .toolbarHeight = layout_.toolbarHeight,
      .rulerHeight = layout_.rulerHeight,
      .statusHeight = layout_.statusHeight,
      .keyboardWidth = layout_.keyboardWidth,
      .minimumTimelineWidth = layout_.minimumTimelineWidth,
      .dockWidth = dockWidth,
      .bottomInset = overlayHeight(state, layout_),
      .pianoBottom = pianoBottom,
      .phonemeHeight = laneGeometry.phonemeHeight,
      .unitHeight = laneGeometry.unitHeight,
      .seamHeight = laneGeometry.seamHeight,
      .pitchHeight = laneGeometry.pitchHeight,
      .dockVisible = dockVisible,
  });
  model.setViewport(ui::PianoRollViewport{
      .bounds = ui::Rect{0.0, 0.0, frame.editorRight, contentHeight},
      .keyboardWidth = layout_.keyboardWidth,
  });
  model.rebuildIndex();

  canvas.clear(theme_.background);
  paintToolbar(canvas, state);
  canvas.fillRect(ui::Rect{0.0, layout_.toolbarHeight, frame.editorRight,
                           layout_.rulerHeight}, theme_.panel);
  canvas.fillRect(ui::Rect{0.0, layout_.contentTop(), frame.editorRight,
                           std::max(0.0, pianoBottom - layout_.contentTop())},
                  theme_.background);
  paintGrid(canvas, model, frame.editorRight, pianoBottom);
  paintKeyboard(canvas, model, pianoBottom);
  paintNotes(canvas, model, state);
  if (model.visibleNotes().empty()) {
    paintEmptyPianoRoll(canvas, frame.editorRight, pianoBottom);
  }
  paintTechnicalLanes(canvas, model, state, frame.editorRight);

  if (state.boxSelection.has_value()) {
    const auto box = *state.boxSelection;
    canvas.fillRect(box, theme_.selection);
    canvas.strokeRect(box, theme_.accent, layout_.boxSelectionStrokeWidth);
  }
  if (state.playheadPixel >= 0.0) {
    const auto x = layout_.keyboardWidth + state.playheadPixel;
    canvas.line(ui::Point{x, layout_.toolbarHeight},
                ui::Point{x, statusTop}, theme_.playhead,
                layout_.playheadStrokeWidth);
  }
  if (state.lyricEditor.has_value()) {
    canvas.fillRect(*state.lyricEditor, theme_.lyricEditorBackground);
    canvas.strokeRect(*state.lyricEditor, theme_.accent,
                      layout_.lyricEditorBorderWidth);
    if (!state.compositionPreview.empty()) {
      canvas.drawText(
          ui::Point{state.lyricEditor->x + layout_.lyricEditorInsetX,
                    state.lyricEditor->y + layout_.lyricEditorBaselineOffset},
          state.compositionPreview, theme_.primaryText,
          layout_.lyricEditorFontSize);
    }
  }
  if (audioSettingsVisible) {
    paintAudioSettings(canvas, state, frame.editorRight,
                       statusTop - overlayHeight(state, layout_));
  } else if (state.voicebankBrowserVisible) {
    paintVoicebankBrowser(canvas, state, frame.editorRight,
                          statusTop - overlayHeight(state, layout_));
  } else if (characterFull) {
    paintCharacter(canvas, state, frame.editorRight,
                   statusTop - overlayHeight(state, layout_));
  } else if (arrangementVisible) {
    paintArrangement(canvas, state, frame.editorRight,
                     statusTop - overlayHeight(state, layout_));
  }
  paintExportProgress(canvas, state);
  paintDiagnostics(canvas, state);
  paintStatus(canvas, model, state);
  paintSampleMicroscope(canvas, state);
  if (state.focusedElementBounds.has_value()) {
    auto focusBounds = *state.focusedElementBounds;
    focusBounds.x -= layout_.focusRingInset;
    focusBounds.y -= layout_.focusRingInset;
    focusBounds.width += layout_.focusRingInset * 2.0;
    focusBounds.height += layout_.focusRingInset * 2.0;
    canvas.strokeRect(focusBounds, theme_.focusRing,
                      layout_.focusRingStrokeWidth);
  }
}

void EditorScenePainter::paintToolbar(RasterCanvas& canvas,
                                      const EditorSceneState& state) const noexcept {
  const auto width = canvas.logicalWidth();
  const auto canPlay = state.renderStatus.hasAudibleAudio;
  const auto transportBounds = layout_.transportBoundsForWidth(width);
  const auto compactProjectBounds =
      layout_.compactProjectTitleBoundsForWidth(width);
  const auto subtitle = compactProjectBounds.has_value()
                            ? fitUtf8Text(
                                  state.projectName.empty() ? "Untitled"
                                                             : state.projectName,
                                  compactProjectBounds->width)
                            : std::string{"NATIVE EDITOR / SAMPLE SEAMS"};
  canvas.drawVerticalGradient(ui::Rect{0.0, 0.0, width, layout_.toolbarHeight},
                              theme_.toolbarTop, theme_.toolbarBottom);
  canvas.drawText(ui::Point{layout_.toolbarTitleX, layout_.toolbarTitleBaseline},
                  "PROJECT SEAM", theme_.primaryText,
                  layout_.toolbarTitleFontSize);
  canvas.drawText(ui::Point{layout_.toolbarTitleX, layout_.toolbarSubtitleBaseline},
                  subtitle, theme_.secondaryText,
                  layout_.toolbarSubtitleFontSize);

  canvas.fillRect(transportBounds,
                  state.playing ? theme_.transportPlaying
                                : (canPlay ? theme_.transportIdle
                                           : theme_.transportDisabled));
  canvas.strokeRect(transportBounds,
                    canPlay ? theme_.accent : theme_.gridStrong,
                    layout_.controlStrokeWidth);
  canvas.drawText(ui::Point{transportBounds.x + layout_.transportTextInsetX,
                            layout_.transportTextBaseline},
                  state.playing ? "PAUSE" : "PLAY",
                  canPlay ? theme_.primaryText : theme_.secondaryText,
                  layout_.transportFontSize);

  const auto stopBounds = layout_.stopBoundsForWidth(width);
  canvas.fillRect(stopBounds, theme_.panel);
  canvas.strokeRect(stopBounds, theme_.gridStrong, layout_.controlStrokeWidth);
  canvas.drawText(ui::Point{stopBounds.x + layout_.transportTextInsetX,
                            layout_.transportTextBaseline},
                  "STOP", theme_.secondaryText, layout_.stopFontSize);

  const auto bpmBounds = layout_.bpmBoundsForWidth(width);
  canvas.fillRect(bpmBounds, theme_.panel);
  canvas.strokeRect(bpmBounds, theme_.gridStrong, layout_.controlStrokeWidth);
  canvas.drawText(ui::Point{bpmBounds.x + layout_.transportTextInsetX,
                            layout_.transportTextBaseline},
                  "BPM " + std::to_string(static_cast<int>(std::lround(
                      state.tempoBpm))),
                  theme_.secondaryText, layout_.stopFontSize);

  const auto portraitVisible =
      !layout_.compactToolbar(width) &&
      state.characterMode == domain::CharacterDisplayMode::Minimal &&
      state.voiceIdentity.characterActive &&
      state.characterPortrait != nullptr;
  const auto batchLyricsBounds =
      layout_.batchLyricsBoundsForWidth(width, portraitVisible);
  if (batchLyricsBounds.width > 0.0) {
    canvas.fillRect(batchLyricsBounds, theme_.panel);
    canvas.strokeRect(batchLyricsBounds, theme_.gridStrong,
                      layout_.controlStrokeWidth);
    canvas.drawText(
        ui::Point{batchLyricsBounds.x + layout_.transportTextInsetX,
                  layout_.transportTextBaseline},
        "LYRICS", state.selectedNoteCount > 0U ? theme_.primaryText
                                                : theme_.secondaryText,
        layout_.batchLyricsFontSize);
  }
  const auto loopBounds = layout_.loopBoundsForWidth(width, portraitVisible);
  if (state.loopAvailable && loopBounds.width > 0.0) {
    canvas.fillRect(loopBounds,
                    state.loopEnabled ? theme_.transportPlaying : theme_.panel);
    canvas.strokeRect(loopBounds, theme_.gridStrong,
                      layout_.controlStrokeWidth);
    canvas.drawText(
        ui::Point{loopBounds.x + layout_.transportTextInsetX,
                  layout_.transportTextBaseline},
        state.loopEnabled ? "LOOP ON" : "LOOP", theme_.secondaryText,
        layout_.loopFontSize);
  }
  const auto projectBounds =
      layout_.projectHeaderBoundsForWidth(width, portraitVisible);
  if (projectBounds.has_value()) {
    canvas.drawText(ui::Point{projectBounds->x, layout_.projectTitleBaseline},
                    fitUtf8Text(state.projectName, projectBounds->width),
                    theme_.primaryText, layout_.projectTitleFontSize);
    canvas.drawText(ui::Point{projectBounds->x,
                              layout_.projectRevisionBaseline},
                    formatRevision(state.revision), theme_.secondaryText,
                    layout_.projectRevisionFontSize);
  }
  if (const auto identityBounds = layout_.voiceIdentityBoundsForWidth(width);
      identityBounds.has_value()) {
    canvas.drawText(*identityBounds, state.voiceIdentity.name,
                    state.voiceIdentity.state == VoiceIdentityState::Missing ||
                            state.voiceIdentity.state == VoiceIdentityState::Error
                        ? theme_.diagnosticWarning
                        : theme_.secondaryText,
                    layout_.voiceIdentityTitleFontSize);
    canvas.drawText(ui::Rect{identityBounds->x, layout_.voiceIdentityStateTop,
                             identityBounds->width,
                             layout_.voiceIdentityStateHeight},
                    std::string{voiceIdentityStateName(state.voiceIdentity.state)},
                    theme_.secondaryText, layout_.voiceIdentityStateFontSize);
  }
  if (portraitVisible) {
    const auto side = layout_.portraitSide;
    const ui::Rect portraitBounds{width - layout_.portraitRightInset,
                                  layout_.portraitTop, side, side};
    canvas.drawImageNearest(portraitBounds, *state.characterPortrait,
                            layout_.imageScale);
    canvas.strokeRect(portraitBounds, theme_.accent, layout_.controlStrokeWidth);
  } else if (state.dirty) {
    canvas.fillRect(ui::Rect{width - layout_.dirtyIndicatorRightInset,
                             layout_.dirtyIndicatorTop,
                             layout_.dirtyIndicatorSize,
                             layout_.dirtyIndicatorSize},
                    theme_.accent);
  }
}

void EditorScenePainter::paintGrid(RasterCanvas& canvas,
                                   const ui::PianoRollModel& model,
                                   double editorRight,
                                   double contentBottom) const noexcept {
  const auto top = layout_.contentTop();
  const auto contentLeft = layout_.keyboardWidth;
  const auto& timeline = model.timeline();
  const auto quarter = time::Tick{timeline.ppq()};
  const auto visibleStart = timeline.pixelToTick(0.0);
  const auto visibleEnd = timeline.pixelToTick(editorRight - contentLeft);
  auto gridTick = time::Tick{(visibleStart.value() / quarter.value()) * quarter.value()};
  if (gridTick < visibleStart) gridTick += quarter;
  auto quarterIndex = gridTick.value() / quarter.value();
  const auto quartersPerBar = static_cast<std::int64_t>(std::max<std::size_t>(
      1U, layout_.gridQuartersPerBar));
  for (; gridTick <= visibleEnd; gridTick += quarter, ++quarterIndex) {
    const auto x = contentLeft + timeline.tickToPixel(gridTick);
    const auto bar = quarterIndex % quartersPerBar == 0;
    canvas.line(ui::Point{x, layout_.toolbarHeight}, ui::Point{x, contentBottom},
                bar ? theme_.gridStrong : theme_.gridWeak,
                layout_.gridStrokeWidth(bar));
    if (bar) {
      canvas.drawText(
          ui::Point{x + layout_.gridBarLabelInsetX,
                    layout_.toolbarHeight + layout_.gridBarLabelBaselineOffset},
          std::to_string(quarterIndex / quartersPerBar + 1),
          theme_.secondaryText, layout_.gridBarLabelFontSize);
    }
  }

  const auto& pitch = model.pitch();
  for (auto midi = pitch.topMidiKey(); midi >= 0; --midi) {
    const auto y = top + pitch.midiToPixel(midi);
    if (y > contentBottom) break;
    const auto cKey = midi % 12 == 0;
    canvas.line(ui::Point{contentLeft, y}, ui::Point{editorRight, y},
                cKey ? theme_.gridStrong : theme_.gridWeak,
                layout_.gridStrokeWidth(cKey));
  }
}

void EditorScenePainter::paintKeyboard(RasterCanvas& canvas,
                                       const ui::PianoRollModel& model,
                                       double contentBottom) const noexcept {
  const auto top = layout_.contentTop();
  const auto& pitch = model.pitch();
  canvas.fillRect(ui::Rect{0.0, top, layout_.keyboardWidth,
                           std::max(0.0, contentBottom - top)}, theme_.keyboardBlack);
  for (auto midi = pitch.topMidiKey(); midi >= 0; --midi) {
    const auto y = top + pitch.midiToPixel(midi);
    if (y > contentBottom) break;
    const auto blackKey = midi % 12 == 1 || midi % 12 == 3 || midi % 12 == 6 ||
                          midi % 12 == 8 || midi % 12 == 10;
    canvas.fillRect(ui::Rect{0.0, y, layout_.keyboardWidth, pitch.rowHeight()},
                    blackKey ? theme_.keyboardBlack : theme_.keyboardWhite);
    canvas.line(ui::Point{0.0, y}, ui::Point{layout_.keyboardWidth, y},
                theme_.gridWeak, layout_.keyboardGridStrokeWidth);
    if (midi % 12 == 0) {
      canvas.drawText(
          ui::Point{layout_.keyboardLabelInsetX,
                    y + layout_.keyboardLabelBaselineOffset},
          "C" + std::to_string(midi / 12 - 1), theme_.secondaryText,
          layout_.keyboardLabelFontSize);
    }
  }
  canvas.line(ui::Point{layout_.keyboardWidth, top},
              ui::Point{layout_.keyboardWidth, contentBottom},
              theme_.gridStrong, layout_.keyboardDividerStrokeWidth);
}

void EditorScenePainter::paintNotes(RasterCanvas& canvas,
                                    const ui::PianoRollModel& model,
                                    const EditorSceneState& state) const noexcept {
  const auto notes = model.visibleNotes();
  struct OverlapGroup final {
    std::size_t index{0U};
    std::size_t hiddenMembers{0U};
    ui::Rect bounds;
    bool initialized{false};
  };
  std::vector<OverlapGroup> overlapGroups;
  for (const auto& note : notes) {
    if (note.overlapMemberCount > 1U) {
      auto group = std::find_if(
          overlapGroups.begin(), overlapGroups.end(), [&note](const auto& value) {
            return value.index == note.overlapGroup;
          });
      if (group == overlapGroups.end()) {
        overlapGroups.push_back(OverlapGroup{
            .index = note.overlapGroup,
            .hiddenMembers = note.hiddenOverlapMembers,
        });
        group = std::prev(overlapGroups.end());
      }
      auto memberBounds = note.bounds;
      memberBounds.y += layout_.contentTop();
      if (!group->initialized) {
        group->bounds = memberBounds;
        group->initialized = true;
      } else {
        const auto left = std::min(group->bounds.x, memberBounds.x);
        const auto top = std::min(group->bounds.y, memberBounds.y);
        const auto right = std::max(group->bounds.right(), memberBounds.right());
        const auto bottom = std::max(group->bounds.bottom(), memberBounds.bottom());
        group->bounds = ui::Rect{left, top, right - left, bottom - top};
      }
    }
    if (note.hiddenByOverlapDensity) continue;
    auto bounds = note.bounds;
    bounds.y += layout_.contentTop();
    const auto fill = note.selected ? theme_.noteSelected
                                    : (note.midiKey % 2U == 0U ? theme_.noteAlternate
                                                               : theme_.note);
    canvas.fillRect(bounds, fill);
    canvas.strokeRect(bounds, note.selected ? theme_.noteSelectedStroke
                                            : theme_.noteStroke,
                      layout_.noteStrokeWidth);
    if (!note.lyric.empty() && note.overlapMemberCount == 1U) {
      const auto labelBounds = layout_.noteLabelBounds(bounds);
      if (!labelBounds.has_value()) continue;
      const auto label = EditorLabelPolicy::note(note.lyric, labelBounds->width);
      if (label.mode != EditorLabelMode::Hidden) {
        canvas.drawText(*labelBounds, label.text, theme_.primaryText,
                        layout_.noteFontSize);
      }
    }
  }
  for (const auto& group : overlapGroups) {
    if (!group.initialized || group.hiddenMembers == 0U) continue;
    if (state.overlapDetail.has_value() &&
        state.overlapDetail->groupIndex == group.index) {
      continue;
    }
    const auto badge = layout_.overlapBadgeBounds(
        group.bounds, model.viewport().bounds.right());
    canvas.fillRect(badge, theme_.panel);
    canvas.strokeRect(badge, theme_.focusRing, layout_.controlStrokeWidth);
    canvas.drawText(badge, "+" + std::to_string(group.hiddenMembers),
                    theme_.focusRing, layout_.noteFontSize);
  }
  for (const auto& note : notes) {
    const auto revealsDetail = state.hoveredNote == note.noteId ||
                               (!state.hoveredNote.has_value() &&
                                state.focusedNote == note.noteId);
    if (!revealsDetail) continue;
    auto bounds = note.bounds;
    bounds.y += layout_.contentTop();
    if (!note.hiddenByOverlapDensity) {
      canvas.strokeRect(bounds, theme_.focusRing,
                        layout_.noteStrokeWidth + 1.0);
    }
    if (state.detail.has_value() &&
        state.detail->kind == EditorDetailKind::Note &&
        state.detail->stableId == note.noteId.toString() &&
        !state.detail->value.empty()) {
      const auto detailBounds = layout_.noteDetailBounds(
          bounds, model.viewport().bounds.right());
      canvas.fillRect(detailBounds, theme_.panel);
      canvas.strokeRect(detailBounds, theme_.focusRing,
                        layout_.controlStrokeWidth);
      canvas.drawText(detailBounds, state.detail->value, theme_.primaryText,
                      layout_.noteFontSize);
    }
  }
  if (state.overlapDetail.has_value()) {
    const auto group = std::find_if(
        overlapGroups.begin(), overlapGroups.end(), [&state](const auto& value) {
          return value.index == state.overlapDetail->groupIndex;
        });
    if (group != overlapGroups.end() && group->initialized) {
      const auto detailBounds = layout_.overlapDetailBounds(
          group->bounds, model.viewport().bounds.right(),
          state.overlapDetail->members.size());
      canvas.fillRect(detailBounds, theme_.panel);
      canvas.strokeRect(detailBounds, theme_.focusRing,
                        layout_.controlStrokeWidth);
      canvas.drawText(
          ui::Rect{detailBounds.x + 6.0, detailBounds.y,
                   std::max(1.0, detailBounds.width - 12.0),
                   layout_.overlapDetailTitleHeight},
          std::to_string(state.overlapDetail->members.size()) +
              " OVERLAPPING NOTES",
          theme_.focusRing, layout_.noteFontSize);
      for (std::size_t index = 0U;
           index < state.overlapDetail->members.size(); ++index) {
        const auto& member = state.overlapDetail->members[index];
        auto row = layout_.overlapDetailRowBounds(detailBounds, index);
        if (member.selected) canvas.fillRect(row, theme_.noteSelected);
        row.x += 6.0;
        row.width = std::max(1.0, row.width - 12.0);
        const auto prefix = member.selected ? "> " : "  ";
        canvas.drawText(row,
                        prefix + member.lyric + " / MIDI " +
                            std::to_string(member.midiKey),
                        member.selected ? theme_.primaryText
                                        : theme_.secondaryText,
                        layout_.noteFontSize);
      }
    }
  }
}

void EditorScenePainter::paintEmptyPianoRoll(RasterCanvas& canvas,
                                             double editorRight,
                                             double pianoBottom) const noexcept {
  const auto availableWidth = std::max(0.0, editorRight - layout_.keyboardWidth);
  const auto centerX = layout_.keyboardWidth + availableWidth * 0.5;
  const auto centerY = layout_.contentTop() +
                       std::max(0.0, pianoBottom - layout_.contentTop()) * 0.42;
  const auto titleBounds = ui::Rect{centerX - 96.0, centerY - 18.0, 192.0, 18.0};
  const auto detailBounds = ui::Rect{centerX - 150.0, centerY + 10.0, 300.0, 14.0};
  canvas.drawText(titleBounds, "No notes yet", theme_.primaryText, 16.0);
  canvas.drawText(detailBounds, "Double-click the grid to add a note",
                  theme_.secondaryText, 11.0);
}

void EditorScenePainter::paintTechnicalLanes(
    RasterCanvas& canvas, const ui::PianoRollModel& model,
    const EditorSceneState& state, double editorRight) const noexcept {
  const auto left = layout_.keyboardWidth;
  const auto inset = overlayHeight(state, layout_);
  const auto technical = resolveEditorTechnicalLaneHeights(
      state, layout_, canvas.logicalHeight() - layout_.statusHeight - inset);
  const auto geometry = EditorSceneLayout::TechnicalLaneGeometry{
      .pianoBottom = technical.pianoBottom,
      .phonemeTop = technical.pianoBottom,
      .phonemeHeight = technical.values[0U],
      .unitTop = technical.pianoBottom + technical.values[0U],
      .unitHeight = technical.values[1U],
      .seamTop = technical.pianoBottom + technical.values[0U] + technical.values[1U],
      .seamHeight = technical.values[2U],
      .pitchTop = technical.pianoBottom + technical.values[0U] + technical.values[1U] +
                  technical.values[2U],
      .pitchHeight = technical.values[3U],
      .bottom = canvas.logicalHeight() - layout_.statusHeight - inset,
  };
  const auto phonemeHeight = geometry.phonemeHeight;
  const auto unitHeight = geometry.unitHeight;
  const auto seamHeight = geometry.seamHeight;
  const auto automationHeight = geometry.pitchHeight;
  const auto phonemeTop = geometry.phonemeTop;
  const auto unitTop = geometry.unitTop;
  const auto seamTop = geometry.seamTop;
  const auto automationTop = geometry.pitchTop;
  const auto laneWidth = std::max(0.0, editorRight - left);

  canvas.fillRect(ui::Rect{0.0, phonemeTop, editorRight, phonemeHeight},
                  theme_.phonemeLaneBackground);
  canvas.fillRect(ui::Rect{0.0, unitTop, editorRight, unitHeight},
                  theme_.unitLaneBackground);
  canvas.fillRect(ui::Rect{0.0, seamTop, editorRight,
                           seamHeight}, theme_.seamLaneBackground);
  canvas.fillRect(ui::Rect{0.0, automationTop, editorRight,
                           automationHeight}, theme_.automationLaneBackground);
  canvas.line(ui::Point{0.0, phonemeTop}, ui::Point{editorRight, phonemeTop},
              theme_.gridStrong, layout_.technicalLaneDividerStrokeWidth);
  canvas.line(ui::Point{0.0, unitTop}, ui::Point{editorRight, unitTop},
              theme_.gridStrong, layout_.technicalLaneDividerStrokeWidth);
  canvas.line(ui::Point{0.0, seamTop}, ui::Point{editorRight, seamTop},
              theme_.gridStrong, layout_.technicalLaneDividerStrokeWidth);
  canvas.line(ui::Point{0.0, automationTop}, ui::Point{editorRight, automationTop},
              theme_.gridStrong, layout_.technicalLaneDividerStrokeWidth);
  const auto laneLabel = [this](RasterCanvas& target, double top, double height,
                                std::string_view label) {
    if (height < layout_.laneLabelFontSize +
                     layout_.laneLabelBottomPadding) {
      return;
    }
    const auto textTop = top +
                         std::max(1.0,
                                  (height - layout_.laneLabelFontSize) * 0.5);
    target.drawText(
        ui::Rect{layout_.laneLabelX, textTop,
                 std::max(1.0, layout_.keyboardWidth -
                                   layout_.laneLabelX -
                                   layout_.laneLabelBottomPadding),
                 std::max(1.0, top + height - textTop)},
        label, theme_.secondaryText, layout_.laneLabelFontSize);
  };
  laneLabel(canvas, phonemeTop, phonemeHeight, "PHONEME");
  laneLabel(canvas, unitTop, unitHeight, "UNIT");
  const auto unitInstructionVisible =
      layout_.unitInstructionVisible(unitHeight);
  if (unitInstructionVisible) {
    canvas.drawText(ui::Point{left + layout_.laneInstructionInsetX,
                              unitTop + unitHeight -
                                  layout_.laneInstructionBottomPadding},
                    "DOUBLE CLICK: MICROSCOPE / ALT: VARIANT",
                    theme_.primaryText, layout_.laneInstructionFontSize);
  }
  laneLabel(canvas, seamTop, seamHeight, "SEAM");
  const auto seamInstructionVisible =
      layout_.seamInstructionVisible(seamHeight);
  if (seamInstructionVisible) {
    canvas.drawText(
        ui::Point{left + layout_.laneInstructionInsetX,
                  seamTop + seamHeight - layout_.laneInstructionBottomPadding},
        state.selectedSeam.has_value()
            ? (std::string{"ALT: EDIT / N: CLEAN / A: CHAR / P: PHASE / B: "} +
               (state.seamPreviewAlternate ? "ALT" : "BASE"))
            : "CLICK TO SET BOUNDARY CHARACTER",
        theme_.primaryText, layout_.laneInstructionFontSize);
  }
  laneLabel(canvas, automationTop, automationHeight, "PITCH");

  ui::PhonemeLaneModel phonemeLane;
  phonemeLane.rebuild(model, state.phonemes,
                      layout_.phonemeContentTop(phonemeTop),
                      layout_.phonemeContentHeight(phonemeHeight));
  const auto& phonemeVisuals = phonemeLane.visuals();
  for (const auto& visual : phonemeVisuals) {
    auto bounds = visual.bounds;
    bounds.x += left;
    bounds.x = std::max(left, bounds.x);
    if (bounds.x >= editorRight) continue;
    bounds.width = std::min(bounds.width, editorRight - bounds.x);
    const auto fill = visual.locked ? theme_.accentSecondary : theme_.phoneme;
    canvas.fillRect(bounds, fill);
    canvas.strokeRect(bounds,
                      visual.timingOverridden ? theme_.accent
                                               : theme_.gridStrong,
                      layout_.technicalLaneItemStrokeWidth);
    const ui::Rect textBounds{
        bounds.x + layout_.phonemeTextInsetX,
        bounds.y + layout_.phonemeTextBaselineOffset,
        std::max(0.0, bounds.width - layout_.phonemeTextInsetX * 2.0),
        std::max(0.0, bounds.bottom() -
                          (bounds.y + layout_.phonemeTextBaselineOffset))};
    const auto label = EditorLabelPolicy::technical(
        visual.symbol, textBounds.width, 10.0, 42.0);
    if (label.mode != EditorLabelMode::Hidden) {
      canvas.drawText(textBounds, label.text, theme_.primaryText,
                      layout_.phonemeTextFontSize);
    }
  }

  for (const auto& override : state.unitOverrides) {
    const auto start = std::find_if(phonemeVisuals.begin(), phonemeVisuals.end(),
                                    [&override](const ui::PhonemeVisual& visual) {
                                      return visual.key == override.startKey;
                                    });
    if (start == phonemeVisuals.end()) continue;
    auto end = start;
    for (std::uint16_t count = 1U; count < override.tokenCount && end + 1 != phonemeVisuals.end(); ++count) {
      ++end;
    }
    const auto x = left + start->bounds.x;
    const auto right = left + end->bounds.right();
    if (x >= editorRight || right <= left) continue;
    const ui::Rect bounds{std::max(left, x),
                          layout_.unitContentTop(unitTop),
                          std::max(layout_.unitCardMinimumWidth,
                                   std::min(editorRight, right) -
                                       std::max(left, x)),
                          layout_.unitContentHeight(unitHeight)};
    canvas.fillRect(bounds, theme_.unit);
    canvas.strokeRect(bounds,
                      override.locked ? theme_.accent : theme_.gridStrong,
                      layout_.technicalLaneItemStrokeWidth);
    const auto textWidth = bounds.width - layout_.unitTextInsetX * 2.0;
    if (textWidth >= layout_.panelTextCharacterWidth) {
      const auto label = EditorLabelPolicy::technical(
          override.unitId, textWidth, layout_.panelTextCharacterWidth, 96.0);
      const auto renderer = EditorLabelPolicy::technical(
          rendererLabel(override.renderer), textWidth,
          layout_.panelTextCharacterWidth, 42.0);
      if (label.mode != EditorLabelMode::Hidden) {
        canvas.drawText(
            ui::Rect{bounds.x + layout_.unitTextInsetX,
                     bounds.y + std::min(
                                    layout_.unitLabelBaselineOffset,
                                    std::max(1.0, bounds.height -
                                                      layout_.unitTextFontSize -
                                                      layout_.unitTextBottomPadding)),
                     textWidth, layout_.unitTextFontSize},
            label.text, theme_.primaryText, layout_.unitTextFontSize);
      }
      if (renderer.mode != EditorLabelMode::Hidden &&
          bounds.height >= layout_.unitRendererBaselineOffset +
                               layout_.unitTextFontSize) {
        canvas.drawText(
            ui::Rect{bounds.x + layout_.unitTextInsetX,
                     bounds.y + layout_.unitRendererBaselineOffset,
                     textWidth, layout_.unitTextFontSize},
            renderer.text, theme_.secondaryText, layout_.unitTextFontSize);
      }
    }
  }

  for (const auto& seam : state.seamOverrides) {
    const auto visual = std::find_if(
        phonemeVisuals.begin(), phonemeVisuals.end(),
        [&seam](const ui::PhonemeVisual& candidate) {
          return candidate.key == seam.incomingStartKey;
        });
    if (visual == phonemeVisuals.end()) continue;
    const auto x = left + visual->bounds.x;
    if (x < left || x > editorRight) continue;
    const auto amount = std::clamp(
        static_cast<double>(seam.seamAmount.value_or(0.5F)), 0.0, 1.0);
    const auto railBottomPadding =
        layout_.seamRailBottomPaddingForHeight(seamHeight);
    const ui::Rect rail{x - layout_.seamRailWidth * 0.5,
                        seamTop + layout_.seamRailInset,
                        layout_.seamRailWidth,
                        std::max(layout_.seamRailMinHeight,
                                 seamHeight - layout_.seamRailInset -
                                     railBottomPadding)};
    const auto barHeight = std::max(
        layout_.seamRailMinHeight, rail.height * amount);
    canvas.fillRect(rail, theme_.seamRail);
    canvas.fillRect(ui::Rect{x - layout_.seamRailWidth * 0.5,
                             rail.bottom() - barHeight,
                             layout_.seamRailWidth, barHeight}, theme_.accent);
    canvas.strokeRect(rail,
                      seam.locked ? theme_.noteSelectedStroke
                                   : theme_.gridStrong,
                      layout_.technicalLaneItemStrokeWidth);
  }
  const auto centerY = automationTop +
                       automationHeight * layout_.automationCenterFraction;
  canvas.line(ui::Point{left, centerY}, ui::Point{editorRight, centerY},
              theme_.gridStrong, layout_.automationGridStrokeWidth);
  if (!state.pitchAutomation.empty()) {
    std::optional<std::pair<domain::PitchAutomationPoint, ui::Point>> previous;
    for (const auto& point : state.pitchAutomation) {
      const auto x = left + model.timeline().tickToPixel(point.tick);
      const auto normalized = std::clamp(
          static_cast<double>(point.cents) /
              layout_.pitchAutomationCentsRange,
          -1.0, 1.0);
      const auto y = centerY - normalized *
                                      (automationHeight *
                                       layout_.pitchAutomationVerticalScale);
      if (x < left || x > editorRight) continue;
      const ui::Point current{x, y};
      if (previous.has_value()) {
        const auto& [previousPoint, previousPosition] = *previous;
        switch (previousPoint.interpolation) {
          case domain::CurveInterpolation::Step:
            canvas.line(previousPosition,
                        ui::Point{current.x, previousPosition.y},
                        theme_.automation, layout_.automationCurveStrokeWidth);
            canvas.line(ui::Point{current.x, previousPosition.y}, current,
                        theme_.automation, layout_.automationCurveStrokeWidth);
            break;
          case domain::CurveInterpolation::Smooth: {
            constexpr int segments = 8;
            auto last = previousPosition;
            for (int segment = 1; segment <= segments; ++segment) {
              const auto t = static_cast<double>(segment) /
                             static_cast<double>(segments);
              const auto smooth = t * t * (3.0 - 2.0 * t);
              const ui::Point next{
                  previousPosition.x +
                      (current.x - previousPosition.x) * t,
                  previousPosition.y +
                      (current.y - previousPosition.y) * smooth,
              };
              canvas.line(last, next, theme_.automation,
                          layout_.automationCurveStrokeWidth);
              last = next;
            }
            break;
          }
          case domain::CurveInterpolation::Linear:
            canvas.line(previousPosition, current, theme_.automation,
                        layout_.automationCurveStrokeWidth);
            break;
        }
      }
      const auto pointSize = layout_.automationPointHalfSize * 2.0;
      canvas.fillRect(ui::Rect{x - layout_.automationPointHalfSize,
                               y - layout_.automationPointHalfSize,
                               pointSize, pointSize},
                      theme_.automation);
      previous = std::make_pair(point, current);
    }
  } else {
    const auto baseline = std::min(
        layout_.pitchEmptyTextBaselineOffset,
        std::max(1.0, automationHeight - layout_.pitchEmptyTextFontSize -
                              layout_.pitchEmptyTextBottomPadding));
    canvas.drawText(
        ui::Point{left + layout_.pitchEmptyTextInsetX,
                  automationTop + baseline},
        "CLICK: ADD / DRAG: MOVE / SHIFT+CLICK: DELETE",
        theme_.secondaryText,
        layout_.pitchEmptyTextFontSize);
  }
  static_cast<void>(laneWidth);
}

void EditorScenePainter::paintCharacter(RasterCanvas& canvas,
                                        const EditorSceneState& state,
                                        double editorRight,
                                        double contentBottom) const noexcept {
  if (state.characterMode != domain::CharacterDisplayMode::Full ||
      !state.voiceIdentity.characterActive || state.characterPortrait == nullptr ||
      editorRight >= canvas.logicalWidth()) {
    return;
  }
  const auto width = canvas.logicalWidth() - editorRight;
  canvas.fillRect(ui::Rect{editorRight, layout_.toolbarHeight, width,
                           contentBottom - layout_.toolbarHeight},
                  theme_.characterBackground);
  canvas.line(ui::Point{editorRight, layout_.toolbarHeight},
              ui::Point{editorRight, contentBottom}, theme_.accentSecondary,
              layout_.characterDockDividerStrokeWidth);
  const auto portraitBounds = layout_.characterDockPortraitBounds(
      editorRight, contentBottom, canvas.logicalWidth());
  canvas.drawImageNearest(portraitBounds, *state.characterPortrait,
                          layout_.characterDockPortraitScale);
  canvas.strokeRect(portraitBounds, theme_.gridStrong,
                    layout_.characterDockPortraitBorderWidth);
  const auto textTop = layout_.characterDockMetadataTop(portraitBounds);
  const auto textX = editorRight + layout_.characterDockTextInsetX;
  canvas.drawText(ui::Point{textX, textTop},
                  state.characterName.empty() ? "CHARACTER 01" : state.characterName,
                  theme_.primaryText, layout_.characterDockNameFontSize);
  const auto roleTop = textTop + layout_.characterDockNameToRoleAdvance;
  canvas.drawText(ui::Point{textX, roleTop}, "VOICEBANK AVATAR",
                  theme_.secondaryText, layout_.characterDockDetailFontSize);
  const auto stateTop = roleTop + layout_.characterDockRoleToStateAdvance;
  canvas.drawText(ui::Point{textX, stateTop},
                  "STATE " + characterStateLabel(state.characterState),
                  theme_.accent, layout_.characterDockDetailFontSize);
  const auto modeTop = stateTop + layout_.characterDockStateToModeAdvance;
  canvas.drawText(ui::Point{textX, modeTop},
                  "C: FULL / MIN / OFF", theme_.secondaryText,
                  layout_.characterDockDetailFontSize);
}

void EditorScenePainter::paintArrangement(
    RasterCanvas& canvas, const EditorSceneState& state,
    double editorRight, double contentBottom) const noexcept {
  if (editorRight >= canvas.logicalWidth()) return;
  const auto width = canvas.logicalWidth() - editorRight;
  canvas.fillRect(ui::Rect{editorRight, layout_.toolbarHeight, width,
                           contentBottom - layout_.toolbarHeight},
                  theme_.panel);
  canvas.line(ui::Point{editorRight, layout_.toolbarHeight},
              ui::Point{editorRight, contentBottom}, theme_.accentSecondary,
              layout_.panelDividerStrokeWidth);
  canvas.drawText(ui::Point{editorRight + layout_.panelTitleInsetX,
                            layout_.toolbarHeight + layout_.panelTitleBaseline},
                  "ARRANGEMENT", theme_.primaryText,
                  layout_.panelTitleFontSize);
  canvas.drawText(ui::Point{editorRight + layout_.panelTitleInsetX,
                            layout_.toolbarHeight +
                                layout_.panelInstructionBaselineOffset},
                  "E RENAME / SHIFT+UP/DOWN REORDER", theme_.primaryText,
                  layout_.panelInstructionFontSize);
  const std::array<std::string_view, 5> actionLabels{"T+", "R+", "E", "^",
                                                       "v"};
  for (std::size_t index = 0U; index < actionLabels.size(); ++index) {
    const ui::Rect bounds =
        layout_.arrangementActionBoundsForWidth(canvas.logicalWidth(), index);
    canvas.strokeRect(bounds, theme_.gridStrong, layout_.panelDividerStrokeWidth);
    canvas.drawText(
        ui::Point{bounds.x + layout_.arrangementActionTextInsetX,
                  bounds.y + layout_.arrangementActionTextBaselineOffset},
        actionLabels[index], theme_.primaryText,
        layout_.arrangementActionFontSize);
  }
  double y = layout_.toolbarHeight + layout_.trackListTop;
  for (const auto& track : state.arrangementTracks) {
    if (y + layout_.trackRowHeight > contentBottom) break;
    const auto trackColor = track.selected ? theme_.accent : theme_.primaryText;
    canvas.fillRect(ui::Rect{editorRight + layout_.trackRowInsetX,
                             y + layout_.trackRowTopOffset,
                             width - layout_.trackRowInsetX * 2.0,
                             layout_.trackRowHeight},
                    track.selected ? theme_.selection : theme_.background);
    canvas.drawText(ui::Point{editorRight + layout_.trackTextInsetX,
                              y + layout_.trackTextBaselineOffset},
                    fitUtf8Text((track.vocal ? "V " : "A ") + track.name,
                                width - layout_.trackTextWidthInset,
                                layout_.panelTextCharacterWidth),
                    trackColor, layout_.trackTextFontSize);
    y += layout_.trackRowAdvance;
    for (const auto& region : track.regions) {
      if (y + layout_.regionAdvance - layout_.regionBottomPadding >
          contentBottom) break;
      const auto regionColor = theme_.primaryText;
      canvas.drawText(ui::Point{editorRight + layout_.regionTextInsetX,
                                y + layout_.regionTextBaselineOffset},
                      fitUtf8Text(region.name + " (" +
                                      std::to_string(region.noteCount) + ")",
                                  width - layout_.regionTextWidthInset,
                                  layout_.panelTextCharacterWidth),
                      regionColor, layout_.regionTextFontSize);
      y += layout_.regionAdvance;
    }
  }
  if (state.inspector.valid && y + layout_.inspectorHeight < contentBottom) {
    const auto top = contentBottom - layout_.inspectorHeight;
    canvas.line(ui::Point{editorRight + layout_.inspectorDividerInset,
                          top - layout_.inspectorDividerInset},
                ui::Point{canvas.logicalWidth() - layout_.inspectorDividerInset,
                          top - layout_.inspectorDividerInset},
                theme_.gridStrong, layout_.panelDividerStrokeWidth);
    const auto inspectorX = editorRight + layout_.inspectorTextInsetX;
    canvas.drawText(ui::Point{inspectorX,
                              top + layout_.inspectorLabelBaseline},
                    "INSPECTOR", theme_.primaryText,
                    layout_.inspectorNameFontSize);
    canvas.drawText(ui::Point{inspectorX, top + layout_.inspectorNameBaseline},
                    fitUtf8Text(state.inspector.name,
                                width - layout_.inspectorTextWidthInset,
                                layout_.panelTextCharacterWidth),
                    theme_.primaryText, layout_.inspectorNameFontSize);
    const auto firstFieldBaseline =
        top + layout_.inspectorNameBaseline +
        layout_.inspectorNameToFirstFieldAdvance;
    canvas.drawText(ui::Point{inspectorX, firstFieldBaseline},
                    "GAIN " + std::to_string(state.inspector.gainDb) + " dB",
                    theme_.primaryText, layout_.inspectorFontSize);
    canvas.drawText(ui::Point{inspectorX,
                              firstFieldBaseline + layout_.inspectorFieldAdvance},
                    "PAN " + std::to_string(state.inspector.pan),
                    theme_.primaryText, layout_.inspectorFontSize);
    canvas.drawText(ui::Point{inspectorX,
                              firstFieldBaseline + layout_.inspectorFieldAdvance * 2.0},
                    state.inspector.vocal
                        ? (state.inspector.voicebank.id.empty()
                               ? "BANK UNSELECTED"
                               : fitUtf8Text("BANK " + state.inspector.voicebank.id,
                                             width - layout_.inspectorTextWidthInset,
                                             layout_.panelTextCharacterWidth))
                        : (state.inspector.mediaOwnership ==
                                   domain::MediaOwnership::ProjectCopy
                               ? "MEDIA PROJECT COPY"
                               : "MEDIA REFERENCE"),
                    theme_.accent, layout_.inspectorFontSize);
    canvas.drawText(ui::Point{inspectorX,
                              firstFieldBaseline + layout_.inspectorFieldAdvance * 3.0},
                    std::string{"MUTE "} + (state.inspector.muted ? "ON" : "OFF") +
                        " / SOLO " + (state.inspector.solo ? "ON" : "OFF"),
                    theme_.primaryText, layout_.inspectorFontSize);
    canvas.drawText(ui::Point{inspectorX,
                              firstFieldBaseline + layout_.inspectorFieldAdvance * 4.0},
                    "BUS " + state.inspector.outputRoute.bus.toString(),
                    theme_.primaryText, layout_.inspectorFontSize);
    if (!state.inspector.mediaHash.empty()) {
      const auto hash = state.inspector.mediaHash.substr(
          0U, std::min<std::size_t>(12U, state.inspector.mediaHash.size()));
      canvas.drawText(ui::Point{inspectorX,
                                firstFieldBaseline + layout_.inspectorFieldAdvance * 5.0},
                      "MEDIA " + hash,
                      theme_.primaryText, layout_.inspectorFontSize);
    }
  }
}

void EditorScenePainter::paintVoicebankBrowser(
    RasterCanvas& canvas, const EditorSceneState& state, double editorRight,
    double contentBottom) const noexcept {
  if (!state.voicebankBrowserVisible || editorRight >= canvas.logicalWidth()) {
    return;
  }
  const auto width = canvas.logicalWidth() - editorRight;
  canvas.fillRect(ui::Rect{editorRight, layout_.toolbarHeight, width,
                           contentBottom - layout_.toolbarHeight},
                  theme_.panel);
  canvas.line(ui::Point{editorRight, layout_.toolbarHeight},
              ui::Point{editorRight, contentBottom}, theme_.accentSecondary,
              layout_.panelDividerStrokeWidth);
  canvas.drawText(ui::Point{editorRight + layout_.panelTitleInsetX,
                            layout_.toolbarHeight + layout_.panelTitleBaseline},
                  "VOICEBANKS", theme_.primaryText, layout_.panelTitleFontSize);
  canvas.drawText(ui::Point{editorRight + layout_.panelTitleInsetX,
                            layout_.toolbarHeight +
                                layout_.panelInstructionBaselineOffset},
                  fitUtf8Text("V CLOSE / R REFRESH / O OPEN APP",
                              width - layout_.panelTitleInsetX * 2.0,
                              layout_.secondaryTextCharacterWidth),
                  theme_.secondaryText,
                  layout_.panelSecondaryInstructionFontSize);
  if (state.voicebankCards.empty()) {
    canvas.drawText(ui::Point{editorRight + layout_.panelTitleInsetX,
                              layout_.toolbarHeight + layout_.voicebankCardTop},
                    "NO INSTALLED VOICEBANKS", theme_.secondaryText,
                    layout_.voicebankCardFontSize);
    canvas.drawText(
        ui::Point{editorRight + layout_.panelTitleInsetX,
                  layout_.toolbarHeight + layout_.voicebankCardTop +
                      layout_.voicebankCardDetailAdvance},
        fitUtf8Text("PRESS O TO OPEN THE STANDALONE APP",
                    width - layout_.panelTitleInsetX * 2.0,
                    layout_.secondaryTextCharacterWidth),
        theme_.secondaryText, layout_.voicebankCardFontSize);
    canvas.drawText(
        ui::Point{editorRight + layout_.panelTitleInsetX,
                  layout_.toolbarHeight + layout_.voicebankCardTop +
                      layout_.voicebankCardDetailAdvance * 2.0},
        fitUtf8Text("INSTALL A BANK, THEN PRESS R",
                    width - layout_.panelTitleInsetX * 2.0,
                    layout_.secondaryTextCharacterWidth),
        theme_.secondaryText, layout_.voicebankCardFontSize);
    return;
  }
  const auto cardX = editorRight + layout_.voicebankCardInsetX;
  const auto cardWidth = std::max(
      1.0, width - layout_.voicebankCardInsetX * 2.0);
  for (std::size_t index = 0U; index < state.voicebankCards.size(); ++index) {
    const auto& card = state.voicebankCards[index];
    const auto y = layout_.toolbarHeight + layout_.voicebankCardTop +
                   static_cast<double>(index) *
                       (layout_.voicebankCardHeight + layout_.voicebankCardGap);
    if (y >= contentBottom) break;
    const auto height = std::min(layout_.voicebankCardHeight,
                                 contentBottom - y);
    if (height <= 0.0) break;
    const auto selected = state.inspector.valid && state.inspector.vocal &&
                          state.inspector.voicebank.id == card.id &&
                          state.inspector.voicebank.version == card.version &&
                          state.inspector.voicebank.contentHash == card.contentHash;
    const auto fill = selected ? theme_.transportPlaying : theme_.background;
    const auto stroke = card.selectable ? theme_.gridStrong : theme_.diagnosticWarning;
    const ui::Rect bounds{cardX, y, cardWidth, height};
    canvas.fillRect(bounds, fill);
    canvas.strokeRect(bounds, stroke, layout_.panelDividerStrokeWidth);
    const auto textX = bounds.x + layout_.voicebankCardTextInsetX;
    const auto textWidth = std::max(1.0, bounds.width -
                                             layout_.voicebankCardTextInsetX * 2.0);
    canvas.drawText(ui::Point{textX, bounds.y + layout_.voicebankCardTitleBaseline},
                    fitUtf8Text(card.displayName + " " + card.version,
                                textWidth,
                                layout_.secondaryTextCharacterWidth),
                    theme_.primaryText, layout_.voicebankCardFontSize);
    canvas.drawText(ui::Point{textX,
                              bounds.y + layout_.voicebankCardTitleBaseline +
                                  layout_.voicebankCardDetailAdvance},
                    fitUtf8Text(card.language + " / " + card.trustLabel,
                                textWidth,
                                layout_.secondaryTextCharacterWidth),
                    card.selectable ? theme_.accent : theme_.secondaryText,
                    layout_.voicebankCardFontSize);
    canvas.drawText(ui::Point{textX,
                              bounds.y + layout_.voicebankCardTitleBaseline +
                                  layout_.voicebankCardDetailAdvance * 2.0},
                    "UNITS " + std::to_string(card.enabledUnitCount) +
                        " / DISABLED " + std::to_string(card.disabledUnitCount),
                    theme_.secondaryText, layout_.voicebankCardFontSize);
    std::string features;
    if (card.hasSustain) features += "SUSTAIN ";
    if (card.hasRelease) features += "RELEASE ";
    if (card.hasBreath) features += "BREATH ";
    if (features.empty()) features = "NO RELEASE/SUSTAIN DATA";
    canvas.drawText(ui::Point{textX,
                              bounds.y + layout_.voicebankCardTitleBaseline +
                                  layout_.voicebankCardDetailAdvance * 3.0},
                    fitUtf8Text(features, textWidth,
                                layout_.secondaryTextCharacterWidth),
                    theme_.secondaryText, layout_.voicebankCardFontSize);
    canvas.drawText(ui::Point{textX,
                              bounds.y + layout_.voicebankCardTitleBaseline +
                                  layout_.voicebankCardDetailAdvance * 4.0},
                    "HASH " + card.contentHashAbbreviation,
                    theme_.secondaryText, layout_.voicebankCardFontSize);
    if (!card.diagnostics.empty() &&
        height >= layout_.voicebankCardDiagnosticMinimumHeight) {
      canvas.drawText(ui::Point{textX,
                                bounds.y + layout_.voicebankCardTitleBaseline +
                                    layout_.voicebankCardDetailAdvance * 5.0},
                      fitUtf8Text(card.diagnostics.front(), textWidth,
                                  layout_.secondaryTextCharacterWidth),
                      theme_.diagnosticWarning, layout_.voicebankCardFontSize);
    }
  }
}

void EditorScenePainter::paintAudioSettings(
    RasterCanvas& canvas, const EditorSceneState& state, double editorRight,
    double contentBottom) const noexcept {
  if (!state.audioSettings.visible || editorRight >= canvas.logicalWidth()) {
    return;
  }
  const auto width = canvas.logicalWidth() - editorRight;
  canvas.fillRect(ui::Rect{editorRight, layout_.toolbarHeight, width,
                           contentBottom - layout_.toolbarHeight},
                  theme_.panel);
  canvas.line(ui::Point{editorRight, layout_.toolbarHeight},
              ui::Point{editorRight, contentBottom}, theme_.accentSecondary,
              layout_.panelDividerStrokeWidth);
  const auto titleX = editorRight + layout_.panelTitleInsetX;
  canvas.drawText(ui::Point{titleX,
                            layout_.toolbarHeight + layout_.panelTitleBaseline},
                  "AUDIO SETTINGS", theme_.primaryText,
                  layout_.panelTitleFontSize);
  canvas.drawText(ui::Point{titleX,
                            layout_.toolbarHeight +
                                layout_.panelInstructionBaselineOffset},
                  "I TO CLOSE / APPLY LIVE", theme_.secondaryText,
                  layout_.panelSecondaryInstructionFontSize);

  const auto rowX = editorRight + layout_.audioSettingsInsetX;
  const auto rowWidth = std::max(
      1.0, width - layout_.audioSettingsInsetX * 2.0);
  const auto rowY = layout_.toolbarHeight + layout_.audioSettingsRowTop;
  for (std::size_t index = 0U; index < state.audioSettings.devices.size();
       ++index) {
    const auto& device = state.audioSettings.devices[index];
    const auto y = rowY + static_cast<double>(index) *
                             (layout_.audioSettingsRowHeight +
                              layout_.audioSettingsRowGap);
    if (y >= contentBottom) break;
    const auto rowHeight = std::min(layout_.audioSettingsRowHeight,
                                    contentBottom - y);
    if (rowHeight <= 0.0) break;
    const ui::Rect bounds{rowX, y, rowWidth, rowHeight};
    canvas.fillRect(bounds, device.selected ? theme_.transportPlaying
                                            : theme_.background);
    canvas.strokeRect(bounds,
                      device.selected ? theme_.accent : theme_.gridStrong,
                      layout_.panelDividerStrokeWidth);
    const auto label = device.name.empty() ? device.id : device.name;
    const auto detail = device.physical ? "PHYSICAL" : "FALLBACK";
    canvas.drawText(ui::Point{bounds.x + layout_.panelRowTextInsetX,
                              bounds.y + layout_.panelRowTextBaselineOffset},
                    fitUtf8Text(label + " / " + detail,
                                bounds.width - layout_.panelRowTextWidthInset,
                                layout_.panelRowTextCharacterWidth),
                    device.selected ? theme_.primaryText : theme_.secondaryText,
                    layout_.audioSettingsFontSize);
  }

  const auto fieldsTop = rowY + static_cast<double>(state.audioSettings.devices.size()) *
                                           (layout_.audioSettingsRowHeight +
                                            layout_.audioSettingsRowGap);
  const std::array<std::string, 3> fields{
      "SAMPLE RATE " + std::to_string(state.audioSettings.current.sampleRate) +
          " HZ",
      "BLOCK " + std::to_string(state.audioSettings.current.blockFrames) +
          " FRAMES",
      "OUTPUT " + std::to_string(state.audioSettings.current.outputChannels) +
          " CHANNELS",
  };
  for (std::size_t index = 0U; index < fields.size(); ++index) {
    const auto y = fieldsTop + static_cast<double>(index) *
                                     (layout_.audioSettingsRowHeight +
                                      layout_.audioSettingsRowGap);
    if (y >= contentBottom) break;
    const auto rowHeight = std::min(layout_.audioSettingsRowHeight,
                                    contentBottom - y);
    if (rowHeight <= 0.0) break;
    const ui::Rect bounds{rowX, y, rowWidth, rowHeight};
    canvas.fillRect(bounds, theme_.background);
    canvas.strokeRect(bounds, theme_.gridStrong,
                      layout_.panelDividerStrokeWidth);
    canvas.drawText(ui::Point{bounds.x + layout_.panelRowTextInsetX,
                              bounds.y + layout_.panelRowTextBaselineOffset},
                    fields[index], theme_.secondaryText,
                    layout_.audioSettingsFontSize);
  }

  const auto statsY = std::max(
      fieldsTop + static_cast<double>(fields.size()) *
                     (layout_.audioSettingsRowHeight + layout_.audioSettingsRowGap) +
                 layout_.audioSettingsStatsTopGap,
      contentBottom - layout_.audioSettingsStatsBottomInset);
  if (statsY < contentBottom) {
    canvas.drawText(
        ui::Point{rowX, statsY},
        "UNDERFLOW " + std::to_string(state.audioSettings.underflowFrames) +
            " / XRUN " + std::to_string(state.audioSettings.xruns),
        theme_.secondaryText, layout_.audioSettingsFontSize);
  }
  if (!state.audioSettings.diagnostic.empty() &&
      contentBottom - layout_.audioSettingsDiagnosticVisibilityInset >
          layout_.toolbarHeight) {
    canvas.drawText(
        ui::Point{rowX, contentBottom -
                           layout_.audioSettingsDiagnosticBaselineInset},
        fitUtf8Text(state.audioSettings.diagnostic, rowWidth,
                    layout_.secondaryTextCharacterWidth),
        theme_.diagnosticWarning, layout_.audioSettingsFontSize);
  }
}

void EditorScenePainter::paintStatus(RasterCanvas& canvas,
                                     const ui::PianoRollModel& model,
                                     const EditorSceneState& state) const noexcept {
  const auto width = canvas.logicalWidth();
  const auto height = canvas.logicalHeight();
  const auto top = height - layout_.statusHeight;
  canvas.fillRect(ui::Rect{0.0, top, width, layout_.statusHeight}, theme_.panel);
  canvas.line(ui::Point{0.0, top}, ui::Point{width, top}, theme_.gridStrong,
              layout_.controlStrokeWidth);
  const auto columns = statusBarColumns(width, layout_);
  canvas.drawText(ui::Point{columns.noteX, top + layout_.statusTextBaseline},
                  fitStatusText(visibleNoteLabel(model.visibleNotes().size()),
                                columns.noteWidth),
                  theme_.secondaryText, layout_.statusFontSize);
  RenderStatusPanelModel renderStatus;
  renderStatus.update(state.renderStatus);
  canvas.drawText(ui::Point{columns.renderX, top + layout_.statusTextBaseline},
                  fitStatusText(renderStatus.label(), columns.renderWidth),
                  renderStatus.isStale() ? theme_.automation
                                         : theme_.secondaryText,
                  layout_.statusFontSize);
  const auto audio = state.audioDeviceOnline ? "AUDIO " : "AUDIO FALLBACK ";
  canvas.drawText(ui::Point{columns.audioX, top + layout_.statusTextBaseline},
                  fitStatusText(audio + state.audioBackend, columns.audioWidth),
                  state.audioDeviceOnline ? theme_.playhead : theme_.secondaryText,
                  layout_.statusFontSize);
}

void EditorScenePainter::paintDiagnostics(
    RasterCanvas& canvas, const EditorSceneState& state) const noexcept {
  if (state.diagnostics.empty()) return;
  const auto width = canvas.logicalWidth();
  const auto height = canvas.logicalHeight();
  const auto panelBounds = layout_.diagnosticBounds(
      width, height, state.exportProgress.totalFiles != 0U);
  const auto top = panelBounds.y;
  const auto& diagnostic = state.diagnostics.front();
  const auto color = diagnostic.severity == authoring::DiagnosticSeverity::Critical
                         ? theme_.diagnosticCritical
                         : diagnostic.severity == authoring::DiagnosticSeverity::Warning
                               ? theme_.diagnosticWarning
                               : theme_.diagnosticInfo;
  canvas.fillRect(panelBounds, color);
  const auto presentation = presentDiagnostic(diagnostic);
  auto label = presentation.title + " / " + presentation.impact;
  auto title = presentation.title;
  if (state.diagnostics.size() > 1U) {
    title += " +" + std::to_string(state.diagnostics.size() - 1U) + " more";
  }
  if (diagnostic.occurrenceCount > 1U) {
    label += " (" + std::to_string(diagnostic.occurrenceCount) + ")";
  }
  const auto actionCount = std::min<std::size_t>(2U, presentation.primaryActions.size());
  const auto actionsWidth = actionCount == 0U
                                ? 0.0
                                : actionCount * layout_.diagnosticActionWidth +
                                      (actionCount - 1U) * layout_.diagnosticActionGap;
  const auto textWidth = std::max(
      1.0, width - layout_.diagnosticTextInsetX * 2.0 - actionsWidth);
  canvas.drawText(ui::Rect{layout_.diagnosticTextInsetX,
                           top + layout_.diagnosticTitleTop, textWidth,
                           layout_.diagnosticTitleFontSize},
                  title, theme_.primaryText,
                  layout_.diagnosticTitleFontSize);
  canvas.drawText(ui::Rect{layout_.diagnosticTextInsetX,
                           top + layout_.diagnosticImpactTop, textWidth,
                           layout_.diagnosticImpactFontSize},
                  fitUtf8Text(label, textWidth,
                              layout_.secondaryTextCharacterWidth),
                  theme_.primaryText, layout_.diagnosticImpactFontSize);
  for (std::size_t index = 0U; index < actionCount; ++index) {
    const auto actionBounds = layout_.diagnosticActionBounds(
        width, height, state.exportProgress.totalFiles != 0U, actionCount, index);
    canvas.fillRect(actionBounds, theme_.panel);
    canvas.strokeRect(actionBounds, theme_.primaryText, layout_.controlStrokeWidth);
    canvas.drawText(actionBounds, presentation.primaryActions[index],
                    theme_.primaryText, layout_.diagnosticMoreFontSize);
  }
}

void EditorScenePainter::paintExportProgress(
    RasterCanvas& canvas, const EditorSceneState& state) const noexcept {
  if (state.exportProgress.totalFiles == 0U) return;
  const auto width = canvas.logicalWidth();
  const auto height = canvas.logicalHeight();
  const auto top = height - layout_.statusHeight - layout_.exportStripHeight;
  const auto fraction = std::clamp(
      static_cast<double>(state.exportProgress.completedFiles) /
          static_cast<double>(state.exportProgress.totalFiles),
      0.0, 1.0);
  canvas.fillRect(ui::Rect{0.0, top, width, layout_.exportProgressBarHeight},
                  theme_.gridStrong);
  canvas.fillRect(ui::Rect{0.0, top, width * fraction,
                           layout_.exportProgressBarHeight}, theme_.playhead);
  if (state.exportProgress.state != authoring::ExportState::Committed &&
      state.exportProgress.state != authoring::ExportState::Recovered) {
    auto label = "EXPORT " + std::string{
        authoring::exportStateName(state.exportProgress.state)};
    if (!state.exportProgress.currentOutput.empty()) {
      label += " / " + state.exportProgress.currentOutput;
    }
    const auto cancelBounds = layout_.exportCancelBounds(width, height);
    const auto cancellable = exportCancellable(state.exportProgress.state) &&
                             state.exportProgress.totalFiles != 0U;
    const auto textWidth = cancellable
                               ? std::max(0.0, cancelBounds.x -
                                                  layout_.exportTextInsetX * 2.0)
                               : width - layout_.exportTextInsetX * 2.0;
    canvas.drawText(ui::Point{layout_.exportTextInsetX,
                              top + layout_.exportTextBaseline},
                    fitUtf8Text(label, textWidth,
                                layout_.secondaryTextCharacterWidth),
                    state.exportProgress.state == authoring::ExportState::Failed ||
                            state.exportProgress.state ==
                                authoring::ExportState::RollbackRequired
                        ? theme_.diagnosticWarning
                        : theme_.secondaryText,
                    layout_.exportFontSize);
    if (cancellable && cancelBounds.width > 0.0 && cancelBounds.height > 0.0) {
      canvas.fillRect(cancelBounds, theme_.panel);
      canvas.strokeRect(cancelBounds, theme_.gridStrong,
                        layout_.controlStrokeWidth);
      canvas.drawText(
          ui::Point{cancelBounds.x + layout_.exportTextInsetX,
                    cancelBounds.y + layout_.exportTextBaseline},
          "CANCEL", theme_.primaryText, layout_.exportFontSize);
    }
  } else if (state.lastExport.has_value()) {
    const auto& result = *state.lastExport;
    const auto destination = result.setPath.empty()
                                 ? result.masterPath
                                 : result.setPath;
    canvas.drawText(
        ui::Point{layout_.exportTextInsetX, top + layout_.exportTextBaseline},
        fitUtf8Text("EXPORT COMPLETE / " + destination.string(),
                    width - layout_.exportTextInsetX * 2.0,
                    layout_.secondaryTextCharacterWidth),
        theme_.playhead, layout_.exportFontSize);
  }
}

void EditorScenePainter::paintSampleMicroscope(
    RasterCanvas& canvas, const EditorSceneState& state) const noexcept {
  if (!state.sampleMicroscope.has_value() ||
      state.sampleMicroscope->model == nullptr) {
    return;
  }
  const auto& view = *state.sampleMicroscope;
  const auto& model = *view.model;
  const auto width = canvas.logicalWidth();
  const auto height = canvas.logicalHeight();
  const auto panel = layout_.microscopePanelBounds(width, height);
  canvas.fillRect(panel, theme_.microscopeOverlay);
  canvas.strokeRect(panel, theme_.microscopeBorder,
                    layout_.microscopeBorderWidth);
  const auto closeBounds = layout_.microscopeCloseBounds(width, height);
  const auto closeX = closeBounds.x;
  const auto title = fitUtf8Text("SAMPLE MICROSCOPE / " + view.unitId,
                                std::max(layout_.microscopeTitleMinimumWidth,
                                         closeX -
                                             layout_.microscopeTitleRightGap),
                                8.0);
  const auto context = fitUtf8Text(
      view.destinationContext.empty() ? "DESTINATION UNKNOWN"
                                      : view.destinationContext,
      std::max(layout_.microscopeContextMinimumWidth,
               width - layout_.microscopeContextRightInset),
      5.0);
  const auto headerX = panel.x + layout_.microscopeHeaderInsetX;
  canvas.drawText(
      ui::Point{headerX, panel.y + layout_.microscopeTitleBaselineOffset},
      title, theme_.primaryText, layout_.microscopeTitleFontSize);
  canvas.drawText(
      ui::Point{headerX, panel.y + layout_.microscopeContextBaselineOffset},
      context, theme_.secondaryText, layout_.microscopeContextFontSize);
  canvas.drawText(
      ui::Point{closeX, panel.y + layout_.microscopeTitleBaselineOffset},
      "ESC / DOUBLE CLICK CLOSE", theme_.secondaryText,
      layout_.microscopeCloseFontSize);

  const auto wave = model.waveformBounds();
  canvas.fillRect(wave, theme_.microscopeWaveBackground);
  canvas.strokeRect(wave, theme_.gridStrong,
                    layout_.microscopePlotBorderWidth);
  canvas.drawText(
      ui::Point{wave.x + layout_.microscopePlotLabelInsetX,
                wave.y + layout_.microscopePlotLabelBaselineOffset},
      "WAVEFORM", theme_.secondaryText, layout_.microscopePlotLabelFontSize);
  const auto center = wave.y + wave.height * 0.5;
  for (const auto& column : model.waveform()) {
    canvas.line(
        ui::Point{column.x, center - column.maximum * wave.height * 0.45},
        ui::Point{column.x, center - column.minimum * wave.height * 0.45},
        theme_.microscopeWaveform, layout_.microscopeWaveformStrokeWidth);
  }

  const auto spectrogramBounds = model.spectrogramBounds();
  canvas.fillRect(spectrogramBounds, theme_.microscopeSpectrogramBackground);
  canvas.strokeRect(spectrogramBounds, theme_.gridStrong,
                    layout_.microscopePlotBorderWidth);
  const auto& spectrogram = model.spectrogram();
  if (spectrogram.columns > 0U && spectrogram.bins > 0U &&
      spectrogram.decibels.size() == spectrogram.columns * spectrogram.bins) {
    const auto columnStep = std::max<std::size_t>(
        1U, spectrogram.columns / layout_.microscopeSpectrogramMaxColumns);
    const auto binStep = std::max<std::size_t>(
        1U, spectrogram.bins / layout_.microscopeSpectrogramMaxBins);
    const auto logBinScale = std::log1p(
        static_cast<double>(std::max<std::size_t>(1U, spectrogram.bins - 1U)));
    for (std::size_t column = 0U; column < spectrogram.columns;
         column += columnStep) {
      for (std::size_t bin = 0U; bin < spectrogram.bins; bin += binStep) {
        const auto db = std::clamp(
            (spectrogram.at(column, bin) + 90.0F) / 84.0F, 0.0F, 1.0F);
        if (db < 0.04F) continue;
        const auto intensity = std::pow(
            std::clamp((db - 0.04F) / 0.96F, 0.0F, 1.0F), 0.7F);
        const auto x = spectrogramBounds.x +
                       static_cast<double>(column) /
                           static_cast<double>(spectrogram.columns) *
                           spectrogramBounds.width;
        const auto lowerBin = static_cast<double>(bin);
        const auto upperBin = static_cast<double>(std::min(
            spectrogram.bins - 1U, bin + binStep));
        const auto lowerPosition = std::log1p(lowerBin) / logBinScale;
        const auto upperPosition = std::log1p(upperBin) / logBinScale;
        const auto y = spectrogramBounds.bottom() - upperPosition *
                                                     spectrogramBounds.height;
        const auto cellHeight = std::max(
            1.0, (upperPosition - lowerPosition) * spectrogramBounds.height);
        canvas.fillRect(
            ui::Rect{x, y,
                     std::max(1.0, spectrogramBounds.width *
                                      static_cast<double>(columnStep) /
                                      static_cast<double>(spectrogram.columns)),
                     cellHeight},
            Color{
                static_cast<std::uint8_t>(
                    std::lround(theme_.microscopeSpectrogramLow.red +
                                (theme_.microscopeSpectrogramHigh.red -
                                 theme_.microscopeSpectrogramLow.red) * intensity)),
                static_cast<std::uint8_t>(
                    std::lround(theme_.microscopeSpectrogramLow.green +
                                (theme_.microscopeSpectrogramHigh.green -
                                 theme_.microscopeSpectrogramLow.green) * intensity)),
                static_cast<std::uint8_t>(
                    std::lround(theme_.microscopeSpectrogramLow.blue +
                                (theme_.microscopeSpectrogramHigh.blue -
                                 theme_.microscopeSpectrogramLow.blue) * intensity)),
                255});
      }
    }
  }
  const auto gridDivisions = std::max<std::size_t>(
      2U, layout_.microscopeSpectrogramGridDivisions);
  for (std::size_t line = 1U; line < gridDivisions; ++line) {
    const auto y = spectrogramBounds.y + spectrogramBounds.height *
        static_cast<double>(line) / static_cast<double>(gridDivisions);
    canvas.line(ui::Point{spectrogramBounds.x, y},
                ui::Point{spectrogramBounds.right(), y},
                theme_.gridWeak, layout_.microscopeSpectrogramGridStrokeWidth);
  }
  canvas.drawText(
      ui::Point{spectrogramBounds.x + layout_.microscopePlotLabelInsetX,
                spectrogramBounds.y +
                    layout_.microscopePlotLabelBaselineOffset},
      "SPECTROGRAM", theme_.secondaryText,
      layout_.microscopePlotLabelFontSize);

  for (const auto& marker : model.markers()) {
    canvas.line(ui::Point{marker.x, wave.y},
                ui::Point{marker.x, spectrogramBounds.bottom()},
                marker.kind == ui::AcousticMarkerKind::VowelOnset
                    ? theme_.playhead
                    : theme_.microscopeMarker,
                layout_.microscopeMarkerStrokeWidth);
    canvas.drawText(
        ui::Point{marker.x + layout_.microscopeMarkerLabelInsetX,
                  wave.y + layout_.microscopeMarkerLabelBaselineOffset},
        marker.label, theme_.microscopeMarkerLabel,
        layout_.microscopeMarkerLabelFontSize);
  }
  for (const auto& mark : model.pitchMarks()) {
    canvas.line(ui::Point{mark.x, wave.y},
                ui::Point{mark.x, wave.bottom()},
                mark.locked ? theme_.microscopePitchMark : theme_.accentSecondary,
                layout_.microscopePitchMarkWidth);
  }
}

}  // namespace seam::native_ui
