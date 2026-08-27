#include "seam/ui/piano_roll_model.hpp"

#include "seam/application/note_commands.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/ui/note_visual_layout.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
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
  return Rect{x + 1.0, y + 1.0, std::max(1.0, width - 2.0),
              pitch_.rowHeight() - 2.0};
}

NoteVisual PianoRollModel::makeNoteVisual(const IndexedNote& indexed) const {
  const auto* note = session_.project().findNote(indexed.noteId);
  const auto* targetRegion = session_.project().findRegion(indexed.regionId);
  std::string lyric;
  if (note != nullptr && targetRegion != nullptr) {
    if (const auto* token = targetRegion->findLyric(note->lyricTokenId)) {
      lyric = domain::toUtf8(token->surface);
    }
  }
  return NoteVisual{
      .noteId = indexed.noteId,
      .bounds = noteBounds(indexed),
      .timelineBounds = noteBounds(indexed),
      .hitBounds = noteBounds(indexed),
      .midiKey = indexed.midiKey,
      .absoluteStart = indexed.absoluteStart,
      .duration = indexed.absoluteEnd - indexed.absoluteStart,
      .selected = session_.selection().contains(indexed.noteId),
      .lyric = std::move(lyric),
  };
}

namespace {

void applyVisualLayout(std::vector<NoteVisual>& visuals) {
  std::vector<NoteVisualLayoutItem> items;
  items.reserve(visuals.size());
  for (const auto& visual : visuals) {
    items.push_back(NoteVisualLayoutItem{
        .noteId = visual.noteId,
        .midiKey = visual.midiKey,
        .start = visual.absoluteStart,
        .end = visual.absoluteStart + visual.duration,
        .timelineBounds = visual.timelineBounds,
    });
  }
  const auto layouts = layoutNoteVisuals(items);
  for (std::size_t index = 0U; index < visuals.size(); ++index) {
    const auto& layout = layouts[index];
    visuals[index].bounds = layout.paintBounds;
    visuals[index].hitBounds = layout.hitBounds;
    visuals[index].overlapGroup = layout.groupIndex;
    visuals[index].overlapMemberCount = layout.groupMemberCount;
    visuals[index].overlapBand = layout.bandIndex;
    visuals[index].visibleOverlapBands = layout.visibleBandCount;
    visuals[index].hiddenOverlapMembers = layout.hiddenMemberCount;
    visuals[index].hiddenByOverlapDensity = layout.hiddenByDensity;
    visuals[index].drawsOverlapIndicator = layout.drawsOverflowIndicator;
  }
}

}

std::vector<NoteVisual> PianoRollModel::allNotes() const {
  const auto* targetRegion = region();
  if (targetRegion == nullptr) return {};
  std::vector<NoteVisual> visuals;
  visuals.reserve(targetRegion->notes.size());
  for (const auto& note : targetRegion->notes) {
    visuals.push_back(makeNoteVisual(IndexedNote{
        .noteId = note.id,
        .regionId = targetRegion->id,
        .absoluteStart = targetRegion->startTick + note.startTick,
        .absoluteEnd = targetRegion->startTick + note.endTick(),
        .midiKey = note.midiKey,
    }));
  }
  applyVisualLayout(visuals);
  return visuals;
}

std::size_t PianoRollModel::noteCount() const noexcept {
  const auto* targetRegion = region();
  return targetRegion == nullptr ? 0U : targetRegion->notes.size();
}

std::optional<NoteVisual> PianoRollModel::noteAt(std::size_t index) const {
  const auto* targetRegion = region();
  if (targetRegion == nullptr || index >= targetRegion->notes.size()) {
    return std::nullopt;
  }
  const auto& note = targetRegion->notes[index];
  return makeNoteVisual(IndexedNote{
      .noteId = note.id,
      .regionId = targetRegion->id,
      .absoluteStart = targetRegion->startTick + note.startTick,
      .absoluteEnd = targetRegion->startTick + note.endTick(),
      .midiKey = note.midiKey,
  });
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
    if (indexed.regionId != regionId_) continue;
    visuals.push_back(makeNoteVisual(indexed));
  }
  applyVisualLayout(visuals);
  return visuals;
}

std::optional<domain::NoteId> PianoRollModel::hitTest(Point point) const {
  const auto visuals = visibleNotes();
  for (auto iterator = visuals.rbegin(); iterator != visuals.rend(); ++iterator) {
    if (iterator->bounds.contains(point)) {
      return iterator->noteId;
    }
  }
  const NoteVisual* closest = nullptr;
  auto closestDistance = std::numeric_limits<double>::infinity();
  for (const auto& visual : visuals) {
    if (!visual.hitBounds.contains(point)) continue;
    const auto center = visual.hitBounds.x + visual.hitBounds.width * 0.5;
    const auto distance = std::abs(point.x - center);
    if (closest == nullptr || distance < closestDistance ||
        (distance == closestDistance && visual.noteId < closest->noteId)) {
      closest = &visual;
      closestDistance = distance;
    }
  }
  if (closest != nullptr) return closest->noteId;
  return std::nullopt;
}

std::vector<domain::NoteId> PianoRollModel::overlapCandidatesAt(Point point) const {
  const auto hit = hitTest(point);
  if (!hit.has_value()) return {};
  const auto visuals = visibleNotes();
  const auto target = std::find_if(
      visuals.begin(), visuals.end(), [hit](const NoteVisual& visual) {
        return visual.noteId == *hit;
      });
  if (target == visuals.end()) return {*hit};
  std::vector<const NoteVisual*> group;
  for (const auto& visual : visuals) {
    if (visual.overlapGroup == target->overlapGroup &&
        visual.overlapMemberCount > 1U) {
      group.push_back(&visual);
    }
  }
  if (group.empty()) return {*hit};
  std::sort(group.begin(), group.end(), [](const NoteVisual* lhs,
                                           const NoteVisual* rhs) {
    if (lhs->absoluteStart != rhs->absoluteStart) {
      return lhs->absoluteStart < rhs->absoluteStart;
    }
    if (lhs->duration != rhs->duration) return lhs->duration < rhs->duration;
    return lhs->noteId < rhs->noteId;
  });
  std::vector<domain::NoteId> result;
  result.reserve(group.size());
  for (const auto* visual : group) result.push_back(visual->noteId);
  return result;
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

core::Result<void> PianoRollModel::quantizeSelection(time::Tick grid) {
  auto* targetRegion = region();
  if (targetRegion == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Piano-roll region was not found");
  }
  if (grid <= time::Tick{0}) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Quantize grid must be positive");
  }
  const time::Quantizer quantizer(grid);
  std::vector<application::NoteResize> resizes;
  for (const auto noteId : session_.selection().noteIds()) {
    const auto* note = targetRegion->findNote(noteId);
    if (note == nullptr) continue;
    auto start = quantizer.snap(note->startTick);
    start = std::max(time::Tick{0}, start);
    if (start >= targetRegion->durationTick) {
      start = std::max(time::Tick{0}, targetRegion->durationTick - grid);
    }
    auto end = quantizer.snap(note->endTick());
    end = std::min(targetRegion->durationTick, end);
    if (end <= start) end = std::min(targetRegion->durationTick, start + grid);
    if (end <= start) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Quantize grid cannot represent a selected note",
                           noteId.toString());
    }
    resizes.push_back(application::NoteResize{
        .noteId = noteId,
        .beforeStart = note->startTick,
        .beforeDuration = note->durationTick,
        .afterStart = start,
        .afterDuration = end - start,
    });
  }
  if (resizes.empty()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No selected notes can be quantized");
  }
  const auto result = session_.execute(
      std::make_unique<application::ResizeNotesCommand>(std::move(resizes)));
  if (result) rebuildIndex();
  return result;
}

core::Result<void> PianoRollModel::setSelectionSlur(bool enabled) {
  auto* targetRegion = region();
  if (targetRegion == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Piano-roll region was not found");
  }
  std::vector<const domain::Note*> selected;
  for (const auto noteId : session_.selection().noteIds()) {
    if (const auto* note = targetRegion->findNote(noteId); note != nullptr) {
      selected.push_back(note);
    }
  }
  if (selected.empty()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No selected notes can receive a slur");
  }
  std::stable_sort(selected.begin(), selected.end(), [](const auto* lhs,
                                                        const auto* rhs) {
    if (lhs->startTick == rhs->startTick) return lhs->id < rhs->id;
    return lhs->startTick < rhs->startTick;
  });
  std::optional<std::uint64_t> group;
  if (enabled) {
    for (const auto* note : selected) {
      if (note->slurGroup.has_value()) {
        group = note->slurGroup;
        break;
      }
    }
    if (!group.has_value()) {
      std::uint64_t maximum = 0U;
      for (const auto& note : targetRegion->notes) {
        if (note.slurGroup.has_value()) maximum = std::max(maximum, *note.slurGroup);
      }
      group = maximum + 1U;
    }
  }
  std::vector<application::NotePerformanceEdit> edits;
  edits.reserve(selected.size());
  for (const auto* note : selected) {
    edits.push_back(application::NotePerformanceEdit{
        .noteId = note->id,
        .beforeArticulation = note->articulation,
        .afterArticulation = domain::NoteArticulation::Legato,
        .beforeSlurGroup = note->slurGroup,
        .afterSlurGroup = group,
        .beforeLyricTokenId = note->lyricTokenId,
        .afterLyricTokenId = note->lyricTokenId,
    });
  }
  return session_.execute(std::make_unique<application::SetNotePerformanceCommand>(
      std::move(edits)));
}

core::Result<void> PianoRollModel::setSelectionMelisma() {
  auto* targetRegion = region();
  if (targetRegion == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Piano-roll region was not found");
  }
  std::vector<const domain::Note*> selected;
  for (const auto noteId : session_.selection().noteIds()) {
    if (const auto* note = targetRegion->findNote(noteId); note != nullptr) {
      selected.push_back(note);
    }
  }
  if (selected.size() < 2U) {
    return core::failure(core::ErrorCode::Conflict,
                         "A melisma requires at least two selected notes");
  }
  std::stable_sort(selected.begin(), selected.end(), [](const auto* lhs,
                                                        const auto* rhs) {
    if (lhs->startTick == rhs->startTick) return lhs->id < rhs->id;
    return lhs->startTick < rhs->startTick;
  });
  const auto lyricId = selected.front()->lyricTokenId;
  if (targetRegion->findLyric(lyricId) == nullptr) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Melisma source note references a missing lyric");
  }
  std::vector<application::NotePerformanceEdit> edits;
  edits.reserve(selected.size());
  for (const auto* note : selected) {
    edits.push_back(application::NotePerformanceEdit{
        .noteId = note->id,
        .beforeArticulation = note->articulation,
        .afterArticulation = domain::NoteArticulation::Legato,
        .beforeSlurGroup = note->slurGroup,
        .afterSlurGroup = note->slurGroup,
        .beforeLyricTokenId = note->lyricTokenId,
        .afterLyricTokenId = lyricId,
    });
  }
  const auto result = session_.execute(
      std::make_unique<application::SetNotePerformanceCommand>(
          std::move(edits)));
  if (result) rebuildIndex();
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

core::Result<domain::NoteId> PianoRollModel::duplicateSelection() {
  auto* targetRegion = region();
  if (targetRegion == nullptr) {
    return core::failure<domain::NoteId>(core::ErrorCode::NotFound,
                                         "Piano-roll region was not found");
  }
  std::vector<const domain::Note*> selected;
  for (const auto noteId : session_.selection().noteIds()) {
    const auto* note = targetRegion->findNote(noteId);
    if (note != nullptr) selected.push_back(note);
  }
  if (selected.empty()) {
    return core::failure<domain::NoteId>(core::ErrorCode::Conflict,
                                         "No selected notes can be duplicated");
  }
  std::stable_sort(selected.begin(), selected.end(),
                   [](const auto* lhs, const auto* rhs) {
                     if (lhs->startTick == rhs->startTick) return lhs->id < rhs->id;
                     return lhs->startTick < rhs->startTick;
                   });
  auto composite = std::make_unique<application::CompositeCommand>(
      "Duplicate notes");
  std::vector<domain::NoteId> duplicatedIds;
  duplicatedIds.reserve(selected.size());
  const auto offset = session_.project().settings().snapGrid;
  for (const auto* source : selected) {
    const auto* lyric = targetRegion->findLyric(source->lyricTokenId);
    if (lyric == nullptr) {
      return core::failure<domain::NoteId>(
          core::ErrorCode::InvariantViolation,
          "Selected note references a missing lyric", source->id.toString());
    }
    auto [token, note] = factory_.makeNote(
        source->startTick + source->durationTick + offset,
        source->durationTick, source->midiKey, lyric->surface, lyric->language);
    note.articulation = source->articulation;
    note.slurGroup = source->slurGroup;
    duplicatedIds.push_back(note.id);
    composite->add(std::make_unique<application::AddNoteCommand>(
        regionId_, std::move(token), std::move(note)));
  }
  const auto result = session_.execute(std::move(composite));
  if (!result) return core::Result<domain::NoteId>{result.error()};
  session_.selection().replace(duplicatedIds);
  rebuildIndex();
  return core::success(duplicatedIds.front());
}

core::Result<LyricDistributionReport>
PianoRollModel::distributeSelectedLyrics(std::u32string text,
                                         domain::Language language) {
  auto* targetRegion = region();
  if (targetRegion == nullptr) {
    return core::failure<LyricDistributionReport>(
        core::ErrorCode::NotFound, "Piano-roll region was not found");
  }
  std::vector<const domain::Note*> selected;
  for (const auto noteId : session_.selection().noteIds()) {
    if (const auto* note = targetRegion->findNote(noteId); note != nullptr) {
      selected.push_back(note);
    }
  }
  if (selected.empty()) {
    return core::failure<LyricDistributionReport>(
        core::ErrorCode::Conflict, "No selected notes can receive lyrics");
  }
  std::stable_sort(selected.begin(), selected.end(),
                   [](const auto* lhs, const auto* rhs) {
                     if (lhs->startTick == rhs->startTick) return lhs->id < rhs->id;
                     return lhs->startTick < rhs->startTick;
                   });
  std::vector<std::u32string> syllables;
  std::u32string current;
  const auto isWhitespace = [](char32_t value) noexcept {
    return value == U' ' || value == U'\t' || value == U'\r' ||
           value == U'\n' || value == U'\u3000';
  };
  for (const auto value : text) {
    if (isWhitespace(value)) {
      if (!current.empty()) {
        syllables.push_back(std::move(current));
        current.clear();
      }
    } else {
      current.push_back(value);
    }
  }
  if (!current.empty()) syllables.push_back(std::move(current));

  LyricDistributionReport report{
      .requestedSyllables = syllables.size(),
      .targetNotes = selected.size(),
      .appliedSyllables = 0U,
      .missingSyllables = syllables.size() < selected.size()
                              ? selected.size() - syllables.size()
                              : 0U,
      .leftoverSyllables = syllables.size() > selected.size()
                               ? syllables.size() - selected.size()
                               : 0U,
      .committed = false,
  };
  if (report.missingSyllables != 0U || report.leftoverSyllables != 0U) {
    return report;
  }
  std::vector<application::BatchLyricEdit> edits;
  edits.reserve(selected.size());
  for (std::size_t index = 0U; index < selected.size(); ++index) {
    const auto* lyric = targetRegion->findLyric(selected[index]->lyricTokenId);
    if (lyric == nullptr) {
      return core::failure<LyricDistributionReport>(
          core::ErrorCode::InvariantViolation,
          "Selected note references a missing lyric",
          selected[index]->id.toString());
    }
    edits.push_back(application::BatchLyricEdit{
        .lyricId = lyric->id,
        .before = lyric->surface,
        .after = syllables[index],
        .language = language,
        .beforeLanguage = lyric->language,
    });
  }
  const auto result = session_.execute(
      std::make_unique<application::BatchSetLyricsCommand>(std::move(edits)));
  if (!result) return core::Result<LyricDistributionReport>{result.error()};
  report.appliedSyllables = selected.size();
  report.committed = true;
  rebuildIndex();
  return report;
}

}  // namespace seam::ui
