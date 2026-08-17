#include "seam/native_ui/editor_controller.hpp"

#include "seam/application/lyric_commands.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace seam::native_ui {

NativeEditorController::NativeEditorController(
    application::EditorSession& session,
    application::ProjectFactory& factory,
    domain::RegionId regionId,
    EditorHostCallbacks callbacks)
    : session_(session),
      factory_(factory),
      regionId_(regionId),
      pianoRoll_(session, factory, regionId),
      callbacks_(std::move(callbacks)) {
  resize(logicalWidth_, logicalHeight_);
}

EditorSceneState NativeEditorController::sceneState() const {
  EditorSceneState state{
      .projectName = session_.project().name(),
      .revision = session_.revision(),
      .playing = playing_,
      .dirty = dirty_,
      .audioDeviceOnline = audioOnline_,
      .audioBackend = audioBackend_,
      .boxSelection = std::nullopt,
      .lyricEditor = std::nullopt,
      .compositionPreview = {},
      .playheadPixel = playheadPixel_,
      .phonemes = {},
      .unitOverrides = {},
      .seamOverrides = {},
      .pitchAutomation = {},
      .characterMode = session_.project().settings().characterDisplay,
      .characterState = playing_ ? character::State::Focused
                                 : (dirty_ ? character::State::Warning
                                           : character::State::Neutral),
      .characterName = characterName_,
      .characterStyle = characterStyle_,
      .characterPortrait = nullptr,
  };
  if (dragMode_ == DragMode::BoxSelect) {
    const auto left = std::min(dragStart_.x, dragCurrent_.x);
    const auto top = std::min(dragStart_.y, dragCurrent_.y);
    state.boxSelection = ui::Rect{left, top,
                                  std::abs(dragCurrent_.x - dragStart_.x),
                                  std::abs(dragCurrent_.y - dragStart_.y)};
  }
  if (composition_.active()) {
    if (const auto* region = session_.project().findRegion(regionId_); region != nullptr) {
      const auto note = std::find_if(region->notes.begin(), region->notes.end(),
                                     [this](const domain::Note& candidate) {
                                       return candidate.lyricTokenId == composition_.lyricId();
                                     });
      if (note != region->notes.end()) {
        if (const auto bounds = noteWindowBounds(note->id); bounds.has_value()) {
          state.lyricEditor = *bounds;
        }
      }
    }
    state.compositionPreview = domain::toUtf8(composition_.compositionText());
  }
  if (const auto* region = session_.project().findRegion(regionId_); region != nullptr) {
    phonemizer::JapaneseKanaPhonemizer phonemizer;
    state.phonemes = phonemizer.phonemize(*region);
    state.unitOverrides = region->unitSelectionOverrides;
    state.seamOverrides = region->seamOverrides;
    state.pitchAutomation = region->pitchAutomation.points();
  }
  return state;
}

void NativeEditorController::resize(double logicalWidth,
                                    double logicalHeight) noexcept {
  logicalWidth_ = std::max(480.0, logicalWidth);
  logicalHeight_ = std::max(320.0, logicalHeight);
  const auto contentHeight = std::max(
      1.0, logicalHeight_ - layout_.contentTop() - layout_.statusHeight -
               layout_.lanesHeight());
  pianoRoll_.setViewport(ui::PianoRollViewport{
      .bounds = ui::Rect{0.0, 0.0, logicalWidth_, contentHeight},
      .keyboardWidth = layout_.keyboardWidth,
  });
  pianoRoll_.rebuildIndex();
  repaint();
}

ui::Point NativeEditorController::modelPoint(ui::Point windowPoint) const noexcept {
  return ui::Point{windowPoint.x, windowPoint.y - layout_.contentTop()};
}

void NativeEditorController::repaint() const {
  if (callbacks_.requestRepaint) callbacks_.requestRepaint();
}

void NativeEditorController::finishTextInput() const {
  if (callbacks_.endTextInput) callbacks_.endTextInput();
}

core::Result<void> NativeEditorController::pointerDown(
    const PointerEvent& event) {
  if (event.button != PointerButton::Left) return core::success();
  const auto pianoBottom = logicalHeight_ - layout_.statusHeight - layout_.lanesHeight();
  const auto seamTop = pianoBottom + layout_.phonemeLaneHeight + layout_.unitLaneHeight;
  if (event.position.y >= seamTop &&
      event.position.y < seamTop + layout_.seamLaneHeight) {
    auto* region = session_.project().findRegion(regionId_);
    if (region == nullptr || region->notes.empty()) return core::success();
    const auto localX = event.position.x - layout_.keyboardWidth;
    const auto tick = pianoRoll_.timeline().pixelToTick(localX);
    const auto nearest = std::min_element(
        region->notes.begin(), region->notes.end(),
        [tick](const domain::Note& lhs, const domain::Note& rhs) {
          return std::abs(lhs.startTick.value() - tick.value()) <
                 std::abs(rhs.startTick.value() - tick.value());
        });
    if (nearest == region->notes.end()) return core::success();
    const auto normalized = std::clamp(
        1.0 - (event.position.y - seamTop) / layout_.seamLaneHeight, 0.0, 1.0);
    domain::SeamOverride value{
        .incomingStartKey = domain::PhonemeKey{.noteId = nearest->id, .ordinal = 0U},
        .seamAmount = static_cast<float>(normalized),
        .overlap = std::nullopt,
        .phaseReset = std::nullopt,
        .envelopeBlend = std::nullopt,
        .curve = domain::SeamCurve::Smooth,
        .locked = true,
    };
    const auto updated = session_.execute(
        std::make_unique<application::UpsertSeamOverrideCommand>(regionId_, value));
    if (updated) dirty_ = true;
    repaint();
    return updated;
  }
  if (event.position.y < layout_.contentTop() ||
      event.position.y >= pianoBottom) {
    return core::success();
  }
  const auto point = modelPoint(event.position);
  if (const auto hit = pianoRoll_.hitTest(point); hit.has_value()) {
    if (event.clickCount >= 2) {
      return beginLyricEdit(*hit);
    }
    if (event.modifiers.shift) {
      session_.selection().toggle(*hit);
    } else if (!session_.selection().contains(*hit)) {
      session_.selection().selectOnly(*hit);
    }
    dragMode_ = DragMode::MoveNotes;
    dragStart_ = event.position;
    dragCurrent_ = event.position;
    repaint();
    return core::success();
  }
  if (event.clickCount >= 2) {
    const auto drawn = pianoRoll_.drawNote(point, session_.project().settings().snapGrid,
                                           U"a");
    if (!drawn) return core::Result<void>{drawn.error()};
    dirty_ = true;
    repaint();
    return core::success();
  }
  dragMode_ = DragMode::BoxSelect;
  dragStart_ = event.position;
  dragCurrent_ = event.position;
  dragAdditive_ = event.modifiers.shift;
  if (!dragAdditive_) session_.selection().clear();
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::pointerMove(
    const PointerEvent& event) {
  if (dragMode_ == DragMode::None) return core::success();
  dragCurrent_ = event.position;
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::pointerUp(
    const PointerEvent& event) {
  if (event.button != PointerButton::Left || dragMode_ == DragMode::None) {
    return core::success();
  }
  dragCurrent_ = event.position;
  core::Result<void> result = core::success();
  if (dragMode_ == DragMode::MoveNotes) {
    const auto deltaX = dragCurrent_.x - dragStart_.x;
    const auto deltaY = dragCurrent_.y - dragStart_.y;
    const auto originTick = pianoRoll_.timeline().pixelToTick(0.0);
    const auto movedTick = pianoRoll_.timeline().pixelToTick(deltaX);
    const auto deltaTick = movedTick - originTick;
    const auto semitone = -static_cast<std::int32_t>(
        std::lround(deltaY / pianoRoll_.pitch().rowHeight()));
    if (deltaTick != time::Tick{0} || semitone != 0) {
      result = pianoRoll_.moveSelection(deltaTick, semitone);
      if (result) dirty_ = true;
    }
  } else {
    const auto left = std::min(dragStart_.x, dragCurrent_.x);
    const auto top = std::min(dragStart_.y, dragCurrent_.y) - layout_.contentTop();
    const auto box = ui::Rect{left, top,
                              std::abs(dragCurrent_.x - dragStart_.x),
                              std::abs(dragCurrent_.y - dragStart_.y)};
    pianoRoll_.selectInBox(box, dragAdditive_);
  }
  dragMode_ = DragMode::None;
  repaint();
  return result;
}

core::Result<void> NativeEditorController::keyDown(const KeyEvent& event) {
  if (composition_.active()) {
    if (event.key == NativeKey::Escape) {
      cancelTextComposition();
      return core::success();
    }
    if (event.key == NativeKey::Enter) {
      return commitTextComposition(composition_.compositionText());
    }
  }

  core::Result<void> result = core::success();
  if (event.modifiers.primaryShortcut() && event.key == NativeKey::Z) {
    result = event.modifiers.shift ? session_.redo() : session_.undo();
    if (result) {
      pianoRoll_.rebuildIndex();
      dirty_ = true;
    }
  } else if (event.modifiers.primaryShortcut() && event.key == NativeKey::Y) {
    result = session_.redo();
    if (result) {
      pianoRoll_.rebuildIndex();
      dirty_ = true;
    }
  } else if (event.key == NativeKey::Delete || event.key == NativeKey::Backspace) {
    if (!session_.selection().empty()) {
      result = pianoRoll_.deleteSelection();
      if (result) dirty_ = true;
    }
  } else if (event.key == NativeKey::Space) {
    playing_ = !playing_;
    if (callbacks_.setPlaying) callbacks_.setPlaying(playing_);
  } else if (event.key == NativeKey::Enter) {
    const auto selected = session_.selection().noteIds();
    if (!selected.empty()) result = beginLyricEdit(selected.front());
  } else if (event.key == NativeKey::C) {
    auto& mode = session_.project().settings().characterDisplay;
    if (mode == domain::CharacterDisplayMode::Full) {
      mode = domain::CharacterDisplayMode::Minimal;
    } else if (mode == domain::CharacterDisplayMode::Minimal) {
      mode = domain::CharacterDisplayMode::Off;
    } else {
      mode = domain::CharacterDisplayMode::Full;
    }
    dirty_ = true;
  } else if (event.key == NativeKey::Plus || event.key == NativeKey::Minus) {
    pianoRoll_.timeline().zoomAround(
        (logicalWidth_ - layout_.keyboardWidth) * 0.5,
        event.key == NativeKey::Plus ? 1.25 : 0.8);
  } else if (event.key == NativeKey::Left || event.key == NativeKey::Right ||
             event.key == NativeKey::Up || event.key == NativeKey::Down) {
    const auto tick = event.key == NativeKey::Left
                          ? time::Tick{-session_.project().settings().snapGrid.value()}
                          : event.key == NativeKey::Right
                                ? session_.project().settings().snapGrid
                                : time::Tick{0};
    const auto semitone = event.key == NativeKey::Up
                              ? 1
                              : event.key == NativeKey::Down ? -1 : 0;
    if (!session_.selection().empty()) {
      result = pianoRoll_.moveSelection(tick, semitone);
      if (result) dirty_ = true;
    }
  }
  repaint();
  return result;
}

void NativeEditorController::scroll(double deltaX, double deltaY,
                                    ui::Point anchor,
                                    InputModifiers modifiers) noexcept {
  if (modifiers.control || modifiers.command) {
    pianoRoll_.timeline().zoomAround(
        std::max(0.0, anchor.x - layout_.keyboardWidth),
        deltaY < 0.0 ? 1.12 : 0.89);
  } else {
    pianoRoll_.timeline().panPixels(deltaX + deltaY);
  }
  repaint();
}

std::optional<ui::Rect> NativeEditorController::noteWindowBounds(
    domain::NoteId noteId) const {
  for (const auto& visual : pianoRoll_.visibleNotes()) {
    if (visual.noteId != noteId) continue;
    auto bounds = visual.bounds;
    bounds.y += layout_.contentTop();
    return bounds;
  }
  return std::nullopt;
}

core::Result<void> NativeEditorController::beginLyricEdit(domain::NoteId noteId) {
  const auto* note = session_.project().findNote(noteId);
  auto* region = session_.project().findRegion(regionId_);
  if (note == nullptr || region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Cannot edit lyric for a missing note");
  }
  const auto* lyric = region->findLyric(note->lyricTokenId);
  if (lyric == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Cannot edit a missing lyric token");
  }
  if (composition_.active()) composition_.cancel();
  auto begun = composition_.begin(lyric->id, lyric->surface);
  if (!begun) return begun;
  if (callbacks_.beginTextInput) {
    callbacks_.beginTextInput(TextInputRequest{
        .lyricId = lyric->id,
        .logicalBounds = noteWindowBounds(noteId).value_or(
            ui::Rect{layout_.keyboardWidth, layout_.contentTop(), 160.0, 30.0}),
        .currentText = lyric->surface,
    });
  }
  repaint();
  return core::success();
}

core::Result<void> NativeEditorController::updateTextComposition(
    std::u32string text, ui::CompositionSelection selection) {
  const auto result = composition_.update(std::move(text), selection);
  repaint();
  return result;
}

core::Result<void> NativeEditorController::commitTextComposition(
    std::u32string text) {
  if (!composition_.active()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No native text composition is active");
  }
  auto updated = composition_.update(std::move(text),
                                      ui::CompositionSelection{});
  if (!updated) return updated;
  auto commit = composition_.commit();
  if (!commit) return core::Result<void>{commit.error()};

  domain::Language language = domain::Language::Unspecified;
  for (const auto& track : session_.project().vocalTracks()) {
    for (const auto& region : track.regions) {
      if (const auto* lyric = region.findLyric(commit.value().lyricId)) {
        language = lyric->language;
      }
    }
  }
  const auto result = session_.execute(
      std::make_unique<application::SetLyricCommand>(
          commit.value().lyricId, std::move(commit.value().text), language));
  if (result) {
    dirty_ = true;
    pianoRoll_.rebuildIndex();
  }
  finishTextInput();
  repaint();
  return result;
}

void NativeEditorController::cancelTextComposition() noexcept {
  composition_.cancel();
  finishTextInput();
  repaint();
}

void NativeEditorController::setAudioState(bool online, std::string backend) {
  audioOnline_ = online;
  audioBackend_ = std::move(backend);
  repaint();
}

void NativeEditorController::setDirty(bool dirty) noexcept {
  dirty_ = dirty;
  repaint();
}

void NativeEditorController::setPlayheadPixel(double value) noexcept {
  playheadPixel_ = std::max(0.0, value);
  repaint();
}

}  // namespace seam::native_ui
