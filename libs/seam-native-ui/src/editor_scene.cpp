#include "seam/native_ui/editor_scene.hpp"

#include "seam/time/tick.hpp"
#include "seam/ui/phoneme_lane_model.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace seam::native_ui {
namespace {

std::string formatRevision(std::uint64_t revision) {
  return "REV " + std::to_string(revision);
}

std::string visibleNoteLabel(std::size_t count) {
  return std::to_string(count) + " VISIBLE NOTES";
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

}  // namespace

void EditorScenePainter::paint(RasterCanvas& canvas, ui::PianoRollModel& model,
                               const EditorSceneState& state) const noexcept {
  const auto width = canvas.logicalWidth();
  const auto height = canvas.logicalHeight();
  const auto characterFull = state.characterMode == domain::CharacterDisplayMode::Full &&
                             state.characterPortrait != nullptr;
  const auto editorRight = std::max(layout_.keyboardWidth + 180.0,
                                    width - (characterFull ? layout_.characterDockWidth : 0.0));
  const auto statusTop = height - layout_.statusHeight;
  const auto pianoBottom = std::max(layout_.contentTop() + 100.0,
                                    statusTop - layout_.lanesHeight());
  const auto contentHeight = std::max(1.0, pianoBottom - layout_.contentTop());
  model.setViewport(ui::PianoRollViewport{
      .bounds = ui::Rect{0.0, 0.0, editorRight, contentHeight},
      .keyboardWidth = layout_.keyboardWidth,
  });
  model.rebuildIndex();

  canvas.clear(theme_.background);
  paintToolbar(canvas, state);
  canvas.fillRect(ui::Rect{0.0, layout_.toolbarHeight, editorRight,
                           layout_.rulerHeight}, theme_.panel);
  canvas.fillRect(ui::Rect{0.0, layout_.contentTop(), editorRight,
                           std::max(0.0, pianoBottom - layout_.contentTop())},
                  theme_.background);
  paintGrid(canvas, model, editorRight, pianoBottom);
  paintKeyboard(canvas, model, pianoBottom);
  paintNotes(canvas, model);
  paintTechnicalLanes(canvas, model, state, editorRight, pianoBottom);

  if (state.boxSelection.has_value()) {
    const auto box = *state.boxSelection;
    canvas.fillRect(box, theme_.selection);
    canvas.strokeRect(box, theme_.accent, 1.0);
  }
  if (state.playheadPixel >= 0.0) {
    const auto x = layout_.keyboardWidth + state.playheadPixel;
    canvas.line(ui::Point{x, layout_.toolbarHeight},
                ui::Point{x, statusTop}, theme_.playhead, 1.0);
  }
  if (state.lyricEditor.has_value()) {
    canvas.fillRect(*state.lyricEditor, Color{12, 11, 14, 238});
    canvas.strokeRect(*state.lyricEditor, theme_.accent, 2.0);
    if (!state.compositionPreview.empty()) {
      canvas.drawText(ui::Point{state.lyricEditor->x + 8.0,
                                state.lyricEditor->y + 7.0},
                      state.compositionPreview, theme_.primaryText, 11.0);
    }
  }
  paintCharacter(canvas, state, editorRight, statusTop);
  paintStatus(canvas, model, state);
}

void EditorScenePainter::paintToolbar(RasterCanvas& canvas,
                                      const EditorSceneState& state) const noexcept {
  const auto width = canvas.logicalWidth();
  canvas.drawVerticalGradient(ui::Rect{0.0, 0.0, width, layout_.toolbarHeight},
                              theme_.toolbarTop, theme_.toolbarBottom);
  canvas.drawText(ui::Point{20.0, 17.0}, "PROJECT SEAM", theme_.primaryText, 15.0);
  canvas.drawText(ui::Point{20.0, 40.0}, "NATIVE EDITOR / SAMPLE SEAMS",
                  theme_.secondaryText, 9.0);

  const auto transportX = 330.0;
  canvas.fillRect(ui::Rect{transportX, 14.0, 102.0, 36.0},
                  state.playing ? Color{83, 42, 61, 255}
                                : Color{31, 29, 35, 255});
  canvas.strokeRect(ui::Rect{transportX, 14.0, 102.0, 36.0}, theme_.accent, 1.0);
  canvas.drawText(ui::Point{transportX + 15.0, 25.0},
                  state.playing ? "PAUSE" : "PLAY", theme_.primaryText, 10.0);

  canvas.fillRect(ui::Rect{448.0, 14.0, 104.0, 36.0}, Color{31, 29, 35, 255});
  canvas.strokeRect(ui::Rect{448.0, 14.0, 104.0, 36.0}, theme_.gridStrong, 1.0);
  canvas.drawText(ui::Point{463.0, 25.0}, "BPM 154", theme_.secondaryText, 9.0);

  const auto projectX = std::max(580.0, width - 430.0);
  canvas.drawText(ui::Point{projectX, 16.0}, state.projectName,
                  theme_.primaryText, 11.0);
  canvas.drawText(ui::Point{projectX, 39.0}, formatRevision(state.revision),
                  theme_.secondaryText, 8.0);
  if (state.characterMode == domain::CharacterDisplayMode::Minimal &&
      state.characterPortrait != nullptr) {
    const auto side = 48.0;
    canvas.drawImageNearest(ui::Rect{width - 62.0, 8.0, side, side},
                            *state.characterPortrait, 1.0);
    canvas.strokeRect(ui::Rect{width - 62.0, 8.0, side, side}, theme_.accent, 1.0);
  } else if (state.dirty) {
    canvas.fillRect(ui::Rect{width - 22.0, 17.0, 8.0, 8.0}, theme_.accent);
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
  for (; gridTick <= visibleEnd; gridTick += quarter, ++quarterIndex) {
    const auto x = contentLeft + timeline.tickToPixel(gridTick);
    const auto bar = quarterIndex % 4 == 0;
    canvas.line(ui::Point{x, layout_.toolbarHeight}, ui::Point{x, contentBottom},
                bar ? theme_.gridStrong : theme_.gridWeak, bar ? 1.0 : 0.5);
    if (bar) {
      canvas.drawText(ui::Point{x + 5.0, layout_.toolbarHeight + 10.0},
                      std::to_string(quarterIndex / 4 + 1), theme_.secondaryText, 8.0);
    }
  }

  const auto& pitch = model.pitch();
  for (auto midi = pitch.topMidiKey(); midi >= 0; --midi) {
    const auto y = top + pitch.midiToPixel(midi);
    if (y > contentBottom) break;
    const auto cKey = midi % 12 == 0;
    canvas.line(ui::Point{contentLeft, y}, ui::Point{editorRight, y},
                cKey ? theme_.gridStrong : theme_.gridWeak, cKey ? 1.0 : 0.5);
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
                theme_.gridWeak, 0.5);
    if (midi % 12 == 0) {
      canvas.drawText(ui::Point{12.0, y + 5.0}, "C" + std::to_string(midi / 12 - 1),
                      theme_.secondaryText, 8.0);
    }
  }
  canvas.line(ui::Point{layout_.keyboardWidth, top},
              ui::Point{layout_.keyboardWidth, contentBottom}, theme_.gridStrong, 1.0);
}

void EditorScenePainter::paintNotes(RasterCanvas& canvas,
                                    const ui::PianoRollModel& model) const noexcept {
  for (const auto& note : model.visibleNotes()) {
    auto bounds = note.bounds;
    bounds.y += layout_.contentTop();
    const auto fill = note.selected ? theme_.noteSelected
                                    : (note.midiKey % 2U == 0U ? theme_.noteAlternate
                                                               : theme_.note);
    canvas.fillRect(bounds, fill);
    canvas.strokeRect(bounds, note.selected ? theme_.noteSelectedStroke
                                            : theme_.noteStroke, 1.0);
    if (bounds.width > 34.0 && !note.lyric.empty()) {
      canvas.drawText(ui::Point{bounds.x + 6.0, bounds.y + 5.0}, note.lyric,
                      theme_.primaryText, 8.0);
    }
  }
}

void EditorScenePainter::paintTechnicalLanes(
    RasterCanvas& canvas, const ui::PianoRollModel& model,
    const EditorSceneState& state, double editorRight,
    double pianoBottom) const noexcept {
  const auto left = layout_.keyboardWidth;
  const auto phonemeTop = pianoBottom;
  const auto unitTop = phonemeTop + layout_.phonemeLaneHeight;
  const auto automationTop = unitTop + layout_.unitLaneHeight;
  const auto laneWidth = std::max(0.0, editorRight - left);

  canvas.fillRect(ui::Rect{0.0, phonemeTop, editorRight, layout_.phonemeLaneHeight},
                  Color{22, 20, 26, 255});
  canvas.fillRect(ui::Rect{0.0, unitTop, editorRight, layout_.unitLaneHeight},
                  Color{20, 23, 27, 255});
  canvas.fillRect(ui::Rect{0.0, automationTop, editorRight,
                           layout_.automationLaneHeight}, Color{25, 21, 27, 255});
  canvas.line(ui::Point{0.0, phonemeTop}, ui::Point{editorRight, phonemeTop},
              theme_.gridStrong, 1.0);
  canvas.line(ui::Point{0.0, unitTop}, ui::Point{editorRight, unitTop},
              theme_.gridStrong, 1.0);
  canvas.line(ui::Point{0.0, automationTop}, ui::Point{editorRight, automationTop},
              theme_.gridStrong, 1.0);
  canvas.drawText(ui::Point{8.0, phonemeTop + 12.0}, "PHONEME", theme_.secondaryText, 8.0);
  canvas.drawText(ui::Point{8.0, unitTop + 12.0}, "UNIT", theme_.secondaryText, 8.0);
  canvas.drawText(ui::Point{8.0, automationTop + 12.0}, "PITCH", theme_.secondaryText, 8.0);

  ui::PhonemeLaneModel phonemeLane;
  phonemeLane.rebuild(model, state.phonemes, phonemeTop + 4.0,
                      layout_.phonemeLaneHeight - 8.0);
  const auto& phonemeVisuals = phonemeLane.visuals();
  for (const auto& visual : phonemeVisuals) {
    auto bounds = visual.bounds;
    bounds.x += left;
    bounds.x = std::max(left, bounds.x);
    if (bounds.x >= editorRight) continue;
    bounds.width = std::min(bounds.width, editorRight - bounds.x);
    const auto fill = visual.locked ? theme_.accentSecondary : theme_.phoneme;
    canvas.fillRect(bounds, fill);
    canvas.strokeRect(bounds, visual.timingOverridden ? theme_.accent : theme_.gridStrong, 1.0);
    canvas.drawText(ui::Point{bounds.x + 5.0, bounds.y + 9.0}, visual.symbol,
                    theme_.primaryText, 8.0);
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
    const ui::Rect bounds{std::max(left, x), unitTop + 5.0,
                          std::max(3.0, std::min(editorRight, right) - std::max(left, x)),
                          layout_.unitLaneHeight - 10.0};
    canvas.fillRect(bounds, theme_.unit);
    canvas.strokeRect(bounds, override.locked ? theme_.accent : theme_.gridStrong, 1.0);
    auto label = override.unitId;
    if (label.size() > 20U) label = label.substr(0U, 20U);
    canvas.drawText(ui::Point{bounds.x + 5.0, bounds.y + 7.0}, label,
                    theme_.primaryText, 7.0);
    canvas.drawText(ui::Point{bounds.x + 5.0, bounds.y + 22.0},
                    rendererLabel(override.renderer), theme_.secondaryText, 7.0);
  }

  const auto centerY = automationTop + layout_.automationLaneHeight * 0.5;
  canvas.line(ui::Point{left, centerY}, ui::Point{editorRight, centerY},
              theme_.gridStrong, 0.5);
  if (!state.pitchAutomation.empty()) {
    std::optional<ui::Point> previous;
    for (const auto& point : state.pitchAutomation) {
      const auto x = left + model.timeline().tickToPixel(point.tick);
      const auto normalized = std::clamp(static_cast<double>(point.cents) / 600.0, -1.0, 1.0);
      const auto y = centerY - normalized * (layout_.automationLaneHeight * 0.38);
      if (x < left || x > editorRight) continue;
      const ui::Point current{x, y};
      if (previous.has_value()) canvas.line(*previous, current, theme_.automation, 1.5);
      canvas.fillRect(ui::Rect{x - 2.5, y - 2.5, 5.0, 5.0}, theme_.automation);
      previous = current;
    }
  } else {
    canvas.drawText(ui::Point{left + 8.0, automationTop + 26.0},
                    "NO PITCH OVERRIDES", theme_.secondaryText, 7.0);
  }
  static_cast<void>(laneWidth);
}

void EditorScenePainter::paintCharacter(RasterCanvas& canvas,
                                        const EditorSceneState& state,
                                        double editorRight,
                                        double contentBottom) const noexcept {
  if (state.characterMode != domain::CharacterDisplayMode::Full ||
      state.characterPortrait == nullptr || editorRight >= canvas.logicalWidth()) {
    return;
  }
  const auto width = canvas.logicalWidth() - editorRight;
  canvas.fillRect(ui::Rect{editorRight, layout_.toolbarHeight, width,
                           contentBottom - layout_.toolbarHeight},
                  Color{18, 16, 21, 255});
  canvas.line(ui::Point{editorRight, layout_.toolbarHeight},
              ui::Point{editorRight, contentBottom}, theme_.accentSecondary, 1.0);
  const auto padding = 12.0;
  const auto portraitHeight = std::max(160.0, contentBottom - layout_.toolbarHeight - 164.0);
  canvas.drawImageNearest(ui::Rect{editorRight + padding, layout_.toolbarHeight + 12.0,
                                   width - padding * 2.0, portraitHeight},
                          *state.characterPortrait, 1.0);
  canvas.strokeRect(ui::Rect{editorRight + padding, layout_.toolbarHeight + 12.0,
                             width - padding * 2.0, portraitHeight},
                    theme_.gridStrong, 1.0);
  const auto textTop = layout_.toolbarHeight + portraitHeight + 30.0;
  canvas.drawText(ui::Point{editorRight + 14.0, textTop},
                  state.characterName.empty() ? "CHARACTER 01" : state.characterName,
                  theme_.primaryText, 9.0);
  canvas.drawText(ui::Point{editorRight + 14.0, textTop + 18.0},
                  "VOICEBANK AVATAR", theme_.secondaryText, 7.0);
  canvas.drawText(ui::Point{editorRight + 14.0, textTop + 34.0},
                  "STATE " + characterStateLabel(state.characterState),
                  theme_.accent, 7.0);
  canvas.drawText(ui::Point{editorRight + 14.0, textTop + 50.0},
                  "C: FULL / MIN / OFF", theme_.secondaryText, 7.0);
}

void EditorScenePainter::paintStatus(RasterCanvas& canvas,
                                     const ui::PianoRollModel& model,
                                     const EditorSceneState& state) const noexcept {
  const auto width = canvas.logicalWidth();
  const auto height = canvas.logicalHeight();
  const auto top = height - layout_.statusHeight;
  canvas.fillRect(ui::Rect{0.0, top, width, layout_.statusHeight}, theme_.panel);
  canvas.line(ui::Point{0.0, top}, ui::Point{width, top}, theme_.gridStrong, 1.0);
  canvas.drawText(ui::Point{14.0, top + 8.0},
                  visibleNoteLabel(model.visibleNotes().size()),
                  theme_.secondaryText, 8.0);
  const auto audio = state.audioDeviceOnline ? "AUDIO " : "AUDIO FALLBACK ";
  canvas.drawText(ui::Point{width - 360.0, top + 8.0}, audio + state.audioBackend,
                  state.audioDeviceOnline ? theme_.playhead : theme_.secondaryText, 8.0);
}

}  // namespace seam::native_ui
