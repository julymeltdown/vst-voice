#include "seam/ui/piano_roll_model.hpp"

#include "seam/application/note_commands.hpp"

#include <algorithm>
#include <memory>

namespace seam::ui {

PianoRollModel::PianoRollModel(application::EditorSession& session,
                               application::ProjectFactory& factory,
                               domain::RegionId regionId)
    : session_(session),
      factory_(factory),
      regionId_(regionId),
      timeline_(session.project().ppq(), 112.0, time::Tick{0}) {
  rebuildIndex();
}

const domain::VocalRegion* PianoRollModel::region() const noexcept {
  return session_.project().findRegion(regionId_);
}

domain::VocalRegion* PianoRollModel::region() noexcept {
  return session_.project().findRegion(regionId_);
}


double PianoRollModel::pixelAtMicrosecondOffset(
    time::Tick absoluteStart, time::Microseconds offset) const noexcept {
  const auto startSeconds = session_.project().tempoMap().secondsAt(absoluteStart);
  const auto offsetSeconds = static_cast<double>(offset) / 1'000'000.0;
  const auto tick = session_.project().tempoMap().tickAtSeconds(startSeconds + offsetSeconds);
  return viewport_.bounds.x + viewport_.keyboardWidth + timeline_.tickToPixel(tick);
}

void PianoRollModel::rebuildIndex() {
  index_.rebuild(session_.project());
}

Rect PianoRollModel::noteBounds(const IndexedNote& indexed) const noexcept {
  const auto contentX = viewport_.bounds.x + viewport_.keyboardWidth;
  const auto x = contentX + timeline_.tickToPixel(indexed.absoluteStart);
  const auto width = std::max(2.0, timeline_.durationToPixels(indexed.absoluteEnd - indexed.absoluteStart));
  const auto y = viewport_.bounds.y + pitch_.midiToPixel(indexed.midiKey);
  return Rect{x + 1.0, y + 1.0, width - 2.0, pitch_.rowHeight() - 2.0};
}

std::vector<NoteVisual> PianoRollModel::visibleNotes() const {
  const auto contentWidth = std::max(0.0, viewport_.bounds.width - viewport_.keyboardWidth);
  const auto start = timeline_.pixelToTick(0.0);
  const auto end = timeline_.pixelToTick(contentWidth);
  const auto lowest = pitch_.pixelToMidi(viewport_.bounds.height);
  const auto highest = pitch_.pixelToMidi(0.0);
  const auto visible = index_.query(start, end, lowest, highest);

  std::vector<NoteVisual> visuals;
  visuals.reserve(visible.size());
  for (const auto& indexed : visible) {
    const auto* note = session_.project().findNote(indexed.noteId);
    const auto* targetRegion = session_.project().findRegion(indexed.regionId);
    std::string lyric;
    if (note != nullptr && targetRegion != nullptr) {
      if (const auto* token = targetRegion->findLyric(note->lyricTokenId)) {
        lyric = domain::toUtf8(token->surface);
      }
    }
    visuals.push_back(NoteVisual{
        .noteId = indexed.noteId,
        .bounds = noteBounds(indexed),
        .midiKey = indexed.midiKey,
        .absoluteStart = indexed.absoluteStart,
        .duration = indexed.absoluteEnd - indexed.absoluteStart,
        .selected = session_.selection().contains(indexed.noteId),
        .lyric = std::move(lyric),
    });
  }
  return visuals;
}

std::optional<domain::NoteId> PianoRollModel::hitTest(Point point) const {
  const auto visuals = visibleNotes();
  for (auto iterator = visuals.rbegin(); iterator != visuals.rend(); ++iterator) {
    if (iterator->bounds.contains(point)) {
      return iterator->noteId;
    }
  }
  return std::nullopt;
}

std::vector<domain::NoteId> PianoRollModel::notesInBox(Rect box) const {
  std::vector<domain::NoteId> result;
  for (const auto& visual : visibleNotes()) {
    if (visual.bounds.intersects(box)) {
      result.push_back(visual.noteId);
    }
  }
  return result;
}

void PianoRollModel::selectInBox(Rect box, bool additive) {
  const auto noteIds = notesInBox(box);
  if (!additive) {
    session_.selection().replace(noteIds);
    return;
  }
  for (const auto noteId : noteIds) {
    session_.selection().add(noteId);
  }
}

core::Result<domain::NoteId> PianoRollModel::drawNote(
    Point point, time::Tick duration, std::u32string lyric) {
  auto* targetRegion = region();
  if (targetRegion == nullptr) {
    return core::failure<domain::NoteId>(core::ErrorCode::NotFound,
                                         "Piano-roll region was not found");
  }

  const auto localX = point.x - viewport_.bounds.x - viewport_.keyboardWidth;
  const auto localY = point.y - viewport_.bounds.y;
  auto start = timeline_.pixelToTick(localX) - targetRegion->startTick;
  const time::Quantizer quantizer(session_.project().settings().snapGrid);
  if (session_.project().settings().snapEnabled) {
    start = quantizer.snap(start);
    duration = quantizer.snap(duration);
  }
  duration = duration <= time::Tick{0} ? session_.project().settings().snapGrid : duration;
  const auto midi = static_cast<std::uint8_t>(pitch_.pixelToMidi(localY));
  auto [token, note] = factory_.makeNote(start, duration, midi, std::move(lyric));
  const auto noteId = note.id;
  const auto result = session_.execute(
      std::make_unique<application::AddNoteCommand>(regionId_, std::move(token), std::move(note)));
  if (!result) {
    return core::Result<domain::NoteId>{result.error()};
  }
  session_.selection().selectOnly(noteId);
  rebuildIndex();
  return core::success(noteId);
}

core::Result<void> PianoRollModel::moveSelection(
    time::Tick deltaTick, std::int32_t deltaSemitones) {
  const time::Quantizer quantizer(session_.project().settings().snapGrid);
  if (session_.project().settings().snapEnabled) {
    deltaTick = quantizer.snap(deltaTick);
  }
  std::vector<application::NoteMove> moves;
  for (const auto noteId : session_.selection().noteIds()) {
    const auto* note = session_.project().findNote(noteId);
    if (note == nullptr) {
      continue;
    }
    const auto newKey = std::clamp(static_cast<std::int32_t>(note->midiKey) + deltaSemitones, 0, 127);
    moves.push_back(application::NoteMove{
        .noteId = noteId,
        .before = note->startTick,
        .after = note->startTick + deltaTick,
        .beforeKey = note->midiKey,
        .afterKey = static_cast<std::uint8_t>(newKey),
    });
  }
  if (moves.empty()) {
    return core::failure(core::ErrorCode::Conflict, "No selected notes can be moved");
  }
  const auto result = session_.execute(std::make_unique<application::MoveNotesCommand>(std::move(moves)));
  if (result) {
    rebuildIndex();
  }
  return result;
}

core::Result<void> PianoRollModel::resizeSelection(
    time::Tick deltaStart, time::Tick deltaEnd) {
  const time::Quantizer quantizer(session_.project().settings().snapGrid);
  if (session_.project().settings().snapEnabled) {
    deltaStart = quantizer.snap(deltaStart);
    deltaEnd = quantizer.snap(deltaEnd);
  }
  std::vector<application::NoteResize> resizes;
  for (const auto noteId : session_.selection().noteIds()) {
    const auto* note = session_.project().findNote(noteId);
    if (note == nullptr) {
      continue;
    }
    const auto newStart = note->startTick + deltaStart;
    const auto newEnd = note->endTick() + deltaEnd;
    resizes.push_back(application::NoteResize{
        .noteId = noteId,
        .beforeStart = note->startTick,
        .beforeDuration = note->durationTick,
        .afterStart = newStart,
        .afterDuration = newEnd - newStart,
    });
  }
  if (resizes.empty()) {
    return core::failure(core::ErrorCode::Conflict, "No selected notes can be resized");
  }
  const auto result = session_.execute(
      std::make_unique<application::ResizeNotesCommand>(std::move(resizes)));
  if (result) {
    rebuildIndex();
  }
  return result;
}

core::Result<void> PianoRollModel::deleteSelection() {
  const auto selected = session_.selection().noteIds();
  if (selected.empty()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No selected notes can be deleted");
  }
  const auto result = session_.execute(
      std::make_unique<application::RemoveNotesCommand>(selected));
  if (result) {
    session_.selection().clear();
    rebuildIndex();
  }
  return result;
}

}  // namespace seam::ui
