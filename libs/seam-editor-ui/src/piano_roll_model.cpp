#include "seam/ui/piano_roll_model.hpp"

#include "seam/application/note_commands.hpp"
#include "seam/application/performance_commands.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/ui/note_visual_layout.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <memory>
#include <map>
#include <unordered_map>
#include <unordered_set>

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
      if (maximum == std::numeric_limits<std::uint64_t>::max()) {
        return core::failure(core::ErrorCode::Conflict, "Slur group identity is exhausted");
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
        .afterArticulation = enabled ? domain::NoteArticulation::Legato :
            (note->articulation == domain::NoteArticulation::Legato ? domain::NoteArticulation::Normal : note->articulation),
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
  std::vector<domain::PerformanceNoteRemap> performanceMapping;
  performanceMapping.reserve(selected.size());
  // Translate the selection as one phrase. Per-note duration offsets change
  // intervals (and can reorder unequal-length notes) in the duplicate.
  auto selectionEnd = selected.front()->startTick;
  for (const auto* source : selected) {
    const auto valid = source->validate();
    if (!valid) return core::Result<domain::NoteId>{valid.error()};
    selectionEnd = std::max(selectionEnd, source->endTick());
  }
  const auto span = selectionEnd - selected.front()->startTick;
  const auto gap = session_.project().settings().snapGrid;
  if (gap.value() < 0 || span.value() >
      std::numeric_limits<std::int64_t>::max() - gap.value()) {
    return core::failure<domain::NoteId>(core::ErrorCode::InvalidArgument,
                                         "Duplicated phrase offset would overflow");
  }
  const auto offset = span + gap;
  std::map<domain::LyricTokenId, domain::LyricToken> lyricCopies;
  std::map<std::uint64_t, std::uint64_t> slurCopies;
  std::uint64_t maximumSlur = 0U;
  for (const auto& existing : targetRegion->notes) {
    if (existing.slurGroup) maximumSlur = std::max(maximumSlur, *existing.slurGroup);
  }
  for (const auto* source : selected) {
    if (!source->slurGroup || slurCopies.contains(*source->slurGroup)) continue;
    if (maximumSlur == std::numeric_limits<std::uint64_t>::max()) {
      return core::failure<domain::NoteId>(core::ErrorCode::Conflict,
                                           "Duplicated slur identity is exhausted");
    }
    slurCopies.emplace(*source->slurGroup, ++maximumSlur);
  }
  for (const auto* source : selected) {
    if (source->endTick().value() >
        std::numeric_limits<std::int64_t>::max() - offset.value()) {
      return core::failure<domain::NoteId>(core::ErrorCode::InvalidArgument,
                                           "Duplicated note start would overflow");
    }
    const auto* lyric = targetRegion->findLyric(source->lyricTokenId);
    if (lyric == nullptr) {
      return core::failure<domain::NoteId>(
          core::ErrorCode::InvariantViolation,
          "Selected note references a missing lyric", source->id.toString());
    }
    auto [token, note] = factory_.makeNote(
        source->startTick + offset,
        source->durationTick, source->midiKey, lyric->surface, lyric->language);
    const auto [copiedLyric, firstUse] = lyricCopies.emplace(source->lyricTokenId, token);
    token = copiedLyric->second;
    note.lyricTokenId = token.id;
    note.articulation = source->articulation;
    if (source->slurGroup) note.slurGroup = slurCopies.at(*source->slurGroup);
    note.vibrato = source->vibrato;
    note.phoneticHint = source->phoneticHint;
    duplicatedIds.push_back(note.id);
    performanceMapping.push_back({source->id, note.id});
    composite->add(std::make_unique<application::AddNoteCommand>(
        regionId_, std::move(token), std::move(note),
        firstUse ? application::AddNoteCommand::LyricMode::Create
                 : application::AddNoteCommand::LyricMode::ReuseExact));
  }
  composite->add(std::make_unique<application::CopyNotePerformanceCommand>(
      regionId_, std::move(performanceMapping)));
  const auto result = session_.execute(std::move(composite));
  if (!result) return core::Result<domain::NoteId>{result.error()};
  session_.selection().replace(duplicatedIds);
  rebuildIndex();
  return core::success(duplicatedIds.front());
}

core::Result<LyricDistributionPlan>
PianoRollModel::planLyricDistribution(const domain::Project& project, domain::RegionId regionId,
    const std::vector<domain::NoteId>& selectedNotes, std::u32string text,
    std::optional<domain::Language> language, std::stop_token stop) {
  if (stop.stop_requested()) return core::failure<LyricDistributionPlan>(core::ErrorCode::Conflict, "Lyric distribution planning cancelled");
  const std::unordered_set<domain::NoteId> selection(selectedNotes.begin(), selectedNotes.end());
  if (selection.size() != selectedNotes.size())
    return core::failure<LyricDistributionPlan>(core::ErrorCode::Conflict, "Duplicate distribution targets");
  const auto* targetRegion = project.findRegion(regionId);
  if (targetRegion == nullptr) {
    return core::failure<LyricDistributionPlan>(
        core::ErrorCode::NotFound, "Piano-roll region was not found");
  }
  constexpr std::size_t maximumTargets = 10000U;
  constexpr std::size_t maximumScalars = 4U * 1024U * 1024U;
  if (targetRegion->notes.size() > maximumTargets || targetRegion->lyrics.size() > maximumTargets ||
      selection.size() > maximumTargets || text.size() > maximumScalars)
    return core::failure<LyricDistributionPlan>(core::ErrorCode::InvalidArgument, "Lyric distribution exceeds note or text limits");
  std::unordered_map<domain::LyricTokenId, const domain::LyricToken*> lyrics;
  for (const auto& lyric : targetRegion->lyrics) {
    if (!lyrics.emplace(lyric.id, &lyric).second)
      return core::failure<LyricDistributionPlan>(core::ErrorCode::Conflict, "Duplicate lyric identity in distribution region");
  }
  std::vector<const domain::Note*> selected;
  std::unordered_set<domain::NoteId> noteIds;
  for (const auto& note : targetRegion->notes) {
    if (!noteIds.insert(note.id).second)
      return core::failure<LyricDistributionPlan>(core::ErrorCode::Conflict, "Duplicate note identity in distribution region");
    if (selection.contains(note.id)) selected.push_back(&note);
  }
  if (selected.size() != selection.size())
    return core::failure<LyricDistributionPlan>(core::ErrorCode::Conflict, "All distribution targets must be in the active region");
  if (selected.empty()) {
    return core::failure<LyricDistributionPlan>(
        core::ErrorCode::Conflict, "No selected notes can receive lyrics");
  }
  std::stable_sort(selected.begin(), selected.end(),
                   [](const auto* lhs, const auto* rhs) {
                     if (lhs->startTick == rhs->startTick) return lhs->id < rhs->id;
                     return lhs->startTick < rhs->startTick;
                   });
  std::vector<const domain::LyricToken*> targets;
  std::unordered_set<domain::LyricTokenId> targetIds;
  for (const auto* note : selected) {
    const auto found = lyrics.find(note->lyricTokenId);
    if (found == lyrics.end())
      return core::failure<LyricDistributionPlan>(core::ErrorCode::InvariantViolation, "Selected note references a missing lyric");
    if (targetIds.insert(note->lyricTokenId).second) targets.push_back(found->second);
  }
  for (const auto& note : targetRegion->notes) {
    if (targetIds.contains(note.lyricTokenId) && !selection.contains(note.id))
      return core::failure<LyricDistributionPlan>(core::ErrorCode::Conflict, "Select every note sharing a lyric before distributing");
  }
  std::vector<std::u32string> syllables;
  std::u32string current;
  const auto isWhitespace = [](char32_t value) noexcept {
    return value == U' ' || value == U'\t' || value == U'\r' ||
           value == U'\n' || value == U'\u3000';
  };
  std::size_t scanned = 0U;
  for (const auto value : text) {
    if ((scanned++ & 4095U) == 0U && stop.stop_requested())
      return core::failure<LyricDistributionPlan>(core::ErrorCode::Conflict, "Lyric distribution planning cancelled");
    if (value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU))
      return core::failure<LyricDistributionPlan>(core::ErrorCode::InvalidArgument, "Distribution text contains invalid Unicode");
    if (isWhitespace(value)) {
      if (!current.empty()) {
        if (syllables.size() == maximumTargets)
          return core::failure<LyricDistributionPlan>(core::ErrorCode::InvalidArgument, "Distribution has more than 10000 syllables");
        syllables.push_back(std::move(current));
        current.clear();
      }
    } else {
      current.push_back(value);
    }
  }
  if (!current.empty()) {
    if (syllables.size() == maximumTargets)
      return core::failure<LyricDistributionPlan>(core::ErrorCode::InvalidArgument, "Distribution has more than 10000 syllables");
    syllables.push_back(std::move(current));
  }

  LyricDistributionReport report{
      .requestedSyllables = syllables.size(),
      .targetNotes = selected.size(),
      .appliedSyllables = 0U,
      .missingSyllables = syllables.size() < targets.size()
                              ? targets.size() - syllables.size()
                              : 0U,
      .leftoverSyllables = syllables.size() > targets.size()
                               ? syllables.size() - targets.size()
                               : 0U,
      .committed = false,
      .targetLyrics = targets.size(),
  };
  if (report.missingSyllables != 0U || report.leftoverSyllables != 0U) {
    return LyricDistributionPlan{report, {}};
  }
  std::vector<application::BatchLyricEdit> edits;
  edits.reserve(targets.size());
  for (std::size_t index = 0U; index < targets.size(); ++index) {
    const auto* lyric = targets[index];
    const auto afterLanguage = language.value_or(lyric->language);
    if (lyric->surface == syllables[index] && lyric->language == afterLanguage) continue;
    edits.push_back(application::BatchLyricEdit{
        .lyricId = lyric->id,
        .before = lyric->surface,
        .after = syllables[index],
        .language = afterLanguage,
        .beforeLanguage = lyric->language,
    });
  }
  report.changedLyrics = edits.size();
  if (stop.stop_requested()) return core::failure<LyricDistributionPlan>(core::ErrorCode::Conflict, "Lyric distribution planning cancelled");
  return LyricDistributionPlan{report, std::move(edits)};
}

core::Result<LyricDistributionReport>
PianoRollModel::distributeSelectedLyrics(std::u32string text, std::optional<domain::Language> language) {
  auto plan = planLyricDistribution(session_.project(), regionId_, session_.selection().noteIds(), std::move(text), language);
  if (!plan) return core::Result<LyricDistributionReport>{plan.error()};
  auto report = plan.value().report;
  if (report.missingSyllables || report.leftoverSyllables) return report;
  if (!plan.value().edits.empty()) {
    const auto result = session_.execute(std::make_unique<application::BatchSetLyricsCommand>(std::move(plan.value().edits)));
    if (!result) return core::Result<LyricDistributionReport>{result.error()};
    rebuildIndex();
  }
  report.appliedSyllables = report.targetLyrics; report.committed = true;
  return report;
}

}  // namespace seam::ui
