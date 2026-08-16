#include "seam/native_ui/editor_scene.hpp"

#include "seam/time/tick.hpp"

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

}  // namespace

void EditorScenePainter::paint(RasterCanvas& canvas, ui::PianoRollModel& model,
                               const EditorSceneState& state) const noexcept {
  const auto width = canvas.logicalWidth();
  const auto height = canvas.logicalHeight();
  const auto contentHeight = std::max(1.0, height - layout_.contentTop() -
                                              layout_.statusHeight);
  model.setViewport(ui::PianoRollViewport{
      .bounds = ui::Rect{0.0, 0.0, width, contentHeight},
      .keyboardWidth = layout_.keyboardWidth,
  });
  model.rebuildIndex();

  canvas.clear(theme_.background);
  paintToolbar(canvas, state);
  const auto contentBottom = height - layout_.statusHeight;
  canvas.fillRect(ui::Rect{0.0, layout_.toolbarHeight, width, layout_.rulerHeight},
                  theme_.panel);
  canvas.fillRect(ui::Rect{0.0, layout_.contentTop(), width,
                           std::max(0.0, contentBottom - layout_.contentTop())},
                  theme_.background);
  paintGrid(canvas, model, contentBottom);
  paintKeyboard(canvas, model, contentBottom);
  paintNotes(canvas, model);

  if (state.boxSelection.has_value()) {
    const auto box = *state.boxSelection;
    canvas.fillRect(box, theme_.selection);
    canvas.strokeRect(box, theme_.accent, 1.0);
  }
  if (state.playheadPixel >= 0.0) {
    const auto x = layout_.keyboardWidth + state.playheadPixel;
    canvas.line(ui::Point{x, layout_.toolbarHeight},
                ui::Point{x, contentBottom}, theme_.playhead, 1.0);
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
  paintStatus(canvas, model, state);
}

void EditorScenePainter::paintToolbar(RasterCanvas& canvas,
                                      const EditorSceneState& state) const noexcept {
  const auto width = canvas.logicalWidth();
  canvas.drawVerticalGradient(
      ui::Rect{0.0, 0.0, width, layout_.toolbarHeight},
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
                  state.playing ? "PAUSE" : "PLAY",
                  theme_.primaryText, 10.0);

  canvas.fillRect(ui::Rect{448.0, 14.0, 104.0, 36.0}, Color{31, 29, 35, 255});
  canvas.strokeRect(ui::Rect{448.0, 14.0, 104.0, 36.0}, theme_.gridStrong, 1.0);
  canvas.drawText(ui::Point{463.0, 25.0}, "BPM 154", theme_.secondaryText, 9.0);

  const auto projectX = std::max(580.0, width - 420.0);
  canvas.drawText(ui::Point{projectX, 16.0}, state.projectName,
                  theme_.primaryText, 11.0);
  canvas.drawText(ui::Point{projectX, 39.0}, formatRevision(state.revision),
                  theme_.secondaryText, 8.0);
  if (state.dirty) {
    canvas.fillRect(ui::Rect{width - 22.0, 17.0, 8.0, 8.0}, theme_.accent);
  }
}

void EditorScenePainter::paintGrid(RasterCanvas& canvas,
                                   const ui::PianoRollModel& model,
                                   double contentBottom) const noexcept {
  const auto width = canvas.logicalWidth();
  const auto top = layout_.contentTop();
  const auto contentLeft = layout_.keyboardWidth;
  const auto& timeline = model.timeline();
  const auto quarter = time::Tick{timeline.ppq()};
  const auto visibleStart = timeline.pixelToTick(0.0);
  const auto visibleEnd = timeline.pixelToTick(width - contentLeft);
  auto gridTick = time::Tick{(visibleStart.value() / quarter.value()) * quarter.value()};
  if (gridTick < visibleStart) gridTick += quarter;
  auto quarterIndex = gridTick.value() / quarter.value();
  for (; gridTick <= visibleEnd; gridTick += quarter, ++quarterIndex) {
    const auto x = contentLeft + timeline.tickToPixel(gridTick);
    const auto bar = quarterIndex % 4 == 0;
    canvas.line(ui::Point{x, layout_.toolbarHeight},
                ui::Point{x, contentBottom},
                bar ? theme_.gridStrong : theme_.gridWeak,
                bar ? 1.0 : 0.5);
    if (bar) {
      canvas.drawText(ui::Point{x + 5.0, layout_.toolbarHeight + 10.0},
                      std::to_string(quarterIndex / 4 + 1),
                      theme_.secondaryText, 8.0);
    }
  }

  const auto& pitch = model.pitch();
  for (auto midi = pitch.topMidiKey(); midi >= 0; --midi) {
    const auto y = top + pitch.midiToPixel(midi);
    if (y > contentBottom) break;
    const auto cKey = midi % 12 == 0;
    canvas.line(ui::Point{contentLeft, y}, ui::Point{width, y},
                cKey ? theme_.gridStrong : theme_.gridWeak,
                cKey ? 1.0 : 0.5);
  }
}

void EditorScenePainter::paintKeyboard(RasterCanvas& canvas,
                                       const ui::PianoRollModel& model,
                                       double contentBottom) const noexcept {
  const auto top = layout_.contentTop();
  const auto& pitch = model.pitch();
  canvas.fillRect(ui::Rect{0.0, top, layout_.keyboardWidth,
                           std::max(0.0, contentBottom - top)},
                  theme_.keyboardBlack);
  for (auto midi = pitch.topMidiKey(); midi >= 0; --midi) {
    const auto y = top + pitch.midiToPixel(midi);
    if (y > contentBottom) break;
    const auto blackKey = midi % 12 == 1 || midi % 12 == 3 || midi % 12 == 6 ||
                          midi % 12 == 8 || midi % 12 == 10;
    canvas.fillRect(ui::Rect{0.0, y, layout_.keyboardWidth,
                             pitch.rowHeight()},
                    blackKey ? theme_.keyboardBlack : theme_.keyboardWhite);
    canvas.line(ui::Point{0.0, y}, ui::Point{layout_.keyboardWidth, y},
                theme_.gridWeak, 0.5);
    if (midi % 12 == 0) {
      canvas.drawText(ui::Point{12.0, y + 5.0},
                      "C" + std::to_string(midi / 12 - 1),
                      theme_.secondaryText, 8.0);
    }
  }
  canvas.line(ui::Point{layout_.keyboardWidth, top},
              ui::Point{layout_.keyboardWidth, contentBottom},
              theme_.gridStrong, 1.0);
}

void EditorScenePainter::paintNotes(RasterCanvas& canvas,
                                    const ui::PianoRollModel& model) const noexcept {
  for (const auto& note : model.visibleNotes()) {
    auto bounds = note.bounds;
    bounds.y += layout_.contentTop();
    const auto fill = note.selected
                          ? theme_.noteSelected
                          : (note.midiKey % 2U == 0U ? theme_.noteAlternate
                                                    : theme_.note);
    canvas.fillRect(bounds, fill);
    canvas.strokeRect(bounds,
                      note.selected ? theme_.noteSelectedStroke
                                    : theme_.noteStroke,
                      1.0);
    if (bounds.width > 34.0 && !note.lyric.empty()) {
      canvas.drawText(ui::Point{bounds.x + 6.0, bounds.y + 5.0}, note.lyric,
                      theme_.primaryText, 8.0);
    }
  }
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
  canvas.drawText(ui::Point{width - 340.0, top + 8.0},
                  audio + state.audioBackend, state.audioDeviceOnline
                                                    ? theme_.playhead
                                                    : theme_.secondaryText,
                  8.0);
}

}  // namespace seam::native_ui
