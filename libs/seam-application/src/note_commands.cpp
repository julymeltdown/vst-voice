#include "seam/application/note_commands.hpp"

#include <algorithm>
#include <cstddef>
#include <set>
#include <unordered_set>

namespace seam::application {

AddNoteCommand::AddNoteCommand(
    domain::RegionId regionId, domain::LyricToken lyric, domain::Note note)
    : regionId_(regionId), lyric_(std::move(lyric)), note_(std::move(note)) {}

CommandImpact AddNoteCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::PhraseAudio,
      .projectWide = false,
      .trackIds = {},
      .regionIds = {regionId_},
      .noteIds = {note_.id},
      .lyricIds = {lyric_.id},
  };
}

core::Result<void> AddNoteCommand::apply(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound, "Target region was not found");
  }
  if (region->findNote(note_.id) != nullptr || region->findLyric(lyric_.id) != nullptr) {
    return core::failure(core::ErrorCode::Conflict, "Note or lyric ID already exists");
  }
  const auto noteResult = note_.validate();
  if (!noteResult) {
    return noteResult;
  }
  if (note_.lyricTokenId != lyric_.id) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Added note must reference the provided lyric token");
  }
  if (note_.endTick() > region->durationTick) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Added note extends beyond its region");
  }

  region->lyrics.push_back(lyric_);
  region->notes.push_back(note_);
  region->sortNotes();
  return core::success();
}

core::Result<void> AddNoteCommand::revert(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound, "Target region was not found");
  }
  const auto noteIterator = std::find_if(region->notes.begin(), region->notes.end(),
      [this](const domain::Note& note) { return note.id == note_.id; });
  if (noteIterator == region->notes.end()) {
    return core::failure(core::ErrorCode::NotFound, "Note to undo was not found");
  }
  region->notes.erase(noteIterator);

  const bool lyricInUse = std::any_of(region->notes.begin(), region->notes.end(),
      [this](const domain::Note& note) { return note.lyricTokenId == lyric_.id; });
  if (!lyricInUse) {
    const auto lyricIterator = std::find_if(region->lyrics.begin(), region->lyrics.end(),
        [this](const domain::LyricToken& lyric) { return lyric.id == lyric_.id; });
    if (lyricIterator != region->lyrics.end()) {
      region->lyrics.erase(lyricIterator);
    }
  }
  return core::success();
}

namespace {

struct NoteLocation final {
  const domain::VocalRegion* region{nullptr};
  std::size_t noteIndex{0};
};

NoteLocation findNoteLocation(const domain::Project& project, domain::NoteId noteId) {
  for (const auto& track : project.vocalTracks()) {
    for (const auto& region : track.regions) {
      for (std::size_t index = 0; index < region.notes.size(); ++index) {
        if (region.notes[index].id == noteId) {
          return NoteLocation{&region, index};
        }
      }
    }
  }
  return {};
}

struct MutableNoteLocation final {
  domain::VocalRegion* region{nullptr};
  domain::Note* note{nullptr};
};

MutableNoteLocation findMutableNoteLocation(domain::Project& project,
                                            domain::NoteId noteId) {
  for (auto& track : project.vocalTracks()) {
    for (auto& region : track.regions) {
      if (auto* note = region.findNote(noteId)) {
        return MutableNoteLocation{&region, note};
      }
    }
  }
  return {};
}

}  // namespace

core::Result<void> RemoveNotesCommand::capture(const domain::Project& project) {
  if (noteIds_.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "At least one note must be selected for deletion");
  }

  std::unordered_set<domain::NoteId> selected;
  selected.reserve(noteIds_.size());
  std::vector<domain::NoteId> uniqueIds;
  uniqueIds.reserve(noteIds_.size());
  for (const auto noteId : noteIds_) {
    if (noteId.valid() && selected.insert(noteId).second) {
      uniqueIds.push_back(noteId);
    }
  }
  if (uniqueIds.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Selected note IDs must be valid");
  }

  removedNotes_.clear();
  removedLyrics_.clear();
  removedOverrides_.clear();
  removedUnitOverrides_.clear();
  removedSeamOverrides_.clear();
  for (const auto noteId : uniqueIds) {
    const auto location = findNoteLocation(project, noteId);
    if (location.region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "A note selected for deletion was not found",
                           noteId.toString());
    }
    removedNotes_.push_back(RemovedNote{
        .regionId = location.region->id,
        .note = location.region->notes[location.noteIndex],
        .originalIndex = location.noteIndex,
    });
  }

  std::set<std::pair<domain::RegionId, domain::LyricTokenId>> capturedLyricKeys;
  for (const auto& removed : removedNotes_) {
    const auto* region = project.findRegion(removed.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Region for a selected note was not found",
                           removed.regionId.toString());
    }
    const auto lyricId = removed.note.lyricTokenId;
    const bool usedByRemainingNote = std::any_of(
        region->notes.begin(), region->notes.end(),
        [&selected, lyricId](const domain::Note& note) {
          return note.lyricTokenId == lyricId && !selected.contains(note.id);
        });
    if (usedByRemainingNote) {
      continue;
    }
    if (!capturedLyricKeys.insert({removed.regionId, lyricId}).second) {
      continue;
    }
    const auto iterator = std::find_if(
        region->lyrics.begin(), region->lyrics.end(),
        [lyricId](const domain::LyricToken& lyric) { return lyric.id == lyricId; });
    if (iterator == region->lyrics.end()) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "A selected note references a missing lyric",
                           removed.note.id.toString());
    }
    removedLyrics_.push_back(RemovedLyric{
        .regionId = removed.regionId,
        .lyric = *iterator,
        .originalIndex = static_cast<std::size_t>(std::distance(region->lyrics.begin(), iterator)),
    });
  }

  for (const auto& track : project.vocalTracks()) {
    for (const auto& region : track.regions) {
      for (std::size_t index = 0; index < region.phonemeOverrides.size(); ++index) {
        const auto& overrideValue = region.phonemeOverrides[index];
        if (selected.contains(overrideValue.key.noteId)) {
          removedOverrides_.push_back(RemovedPhonemeOverride{
              .regionId = region.id,
              .overrideValue = overrideValue,
              .originalIndex = index,
          });
        }
      }
      for (std::size_t index = 0; index < region.unitSelectionOverrides.size(); ++index) {
        const auto& overrideValue = region.unitSelectionOverrides[index];
        if (selected.contains(overrideValue.startKey.noteId)) {
          removedUnitOverrides_.push_back(RemovedUnitSelectionOverride{
              .regionId = region.id,
              .overrideValue = overrideValue,
              .originalIndex = index,
          });
        }
      }
      for (std::size_t index = 0; index < region.seamOverrides.size(); ++index) {
        const auto& overrideValue = region.seamOverrides[index];
        if (selected.contains(overrideValue.incomingStartKey.noteId)) {
          removedSeamOverrides_.push_back(RemovedSeamOverride{
              .regionId = region.id,
              .overrideValue = overrideValue,
              .originalIndex = index,
          });
        }
      }
    }
  }

  noteIds_ = std::move(uniqueIds);
  captured_ = true;
  return core::success();
}

CommandImpact MoveNotesCommand::impact() const {
  CommandImpact result;
  result.scope = CommandAudioImpact::PhraseAudio;
  result.noteIds.reserve(moves_.size());
  for (const auto& move : moves_) result.noteIds.push_back(move.noteId);
  return result;
}

CommandImpact RemoveNotesCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::PhraseAudio,
      .projectWide = false,
      .trackIds = {},
      .regionIds = {},
      .noteIds = noteIds_,
      .lyricIds = {},
  };
}

CommandImpact ResizeNotesCommand::impact() const {
  CommandImpact result;
  result.scope = CommandAudioImpact::PhraseAudio;
  result.noteIds.reserve(resizes_.size());
  for (const auto& resize : resizes_) result.noteIds.push_back(resize.noteId);
  return result;
}

core::Result<void> RemoveNotesCommand::removeCaptured(domain::Project& project) const {
  for (const auto& removed : removedNotes_) {
    auto* region = project.findRegion(removed.regionId);
    if (region == nullptr || region->findNote(removed.note.id) == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "A note to delete was not found",
                           removed.note.id.toString());
    }
  }

  for (const auto& removed : removedOverrides_) {
    auto* region = project.findRegion(removed.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Region for a phoneme override was not found",
                           removed.regionId.toString());
    }
    std::erase_if(region->phonemeOverrides,
                  [&removed](const domain::PhonemeOverride& value) {
                    return value.key == removed.overrideValue.key;
                  });
  }
  for (const auto& removed : removedUnitOverrides_) {
    auto* region = project.findRegion(removed.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Region for a unit selection override was not found",
                           removed.regionId.toString());
    }
    std::erase_if(region->unitSelectionOverrides,
                  [&removed](const domain::UnitSelectionOverride& value) {
                    return value.startKey == removed.overrideValue.startKey;
                  });
  }
  for (const auto& removed : removedSeamOverrides_) {
    auto* region = project.findRegion(removed.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Region for a seam override was not found",
                           removed.regionId.toString());
    }
    std::erase_if(region->seamOverrides,
                  [&removed](const domain::SeamOverride& value) {
                    return value.incomingStartKey ==
                           removed.overrideValue.incomingStartKey;
                  });
  }

  for (const auto& removed : removedNotes_) {
    auto* region = project.findRegion(removed.regionId);
    std::erase_if(region->notes,
                  [&removed](const domain::Note& note) { return note.id == removed.note.id; });
  }
  for (const auto& removed : removedLyrics_) {
    auto* region = project.findRegion(removed.regionId);
    const bool stillUsed = std::any_of(
        region->notes.begin(), region->notes.end(),
        [&removed](const domain::Note& note) {
          return note.lyricTokenId == removed.lyric.id;
        });
    if (!stillUsed) {
      std::erase_if(region->lyrics,
                    [&removed](const domain::LyricToken& lyric) {
                      return lyric.id == removed.lyric.id;
                    });
    }
  }
  return core::success();
}

core::Result<void> RemoveNotesCommand::apply(domain::Project& project) {
  if (!captured_) {
    const auto captured = capture(project);
    if (!captured) {
      return captured;
    }
  }
  return removeCaptured(project);
}

core::Result<void> RemoveNotesCommand::revert(domain::Project& project) {
  if (!captured_) {
    return core::failure(core::ErrorCode::Conflict,
                         "Delete command has not captured project state");
  }

  auto lyrics = removedLyrics_;
  std::stable_sort(lyrics.begin(), lyrics.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.regionId == rhs.regionId) {
      return lhs.originalIndex < rhs.originalIndex;
    }
    return lhs.regionId < rhs.regionId;
  });
  for (const auto& removed : lyrics) {
    auto* region = project.findRegion(removed.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Region for a deleted lyric was not found",
                           removed.regionId.toString());
    }
    if (region->findLyric(removed.lyric.id) == nullptr) {
      const auto index = std::min(removed.originalIndex, region->lyrics.size());
      region->lyrics.insert(region->lyrics.begin() + static_cast<std::ptrdiff_t>(index),
                            removed.lyric);
    }
  }

  auto notes = removedNotes_;
  std::stable_sort(notes.begin(), notes.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.regionId == rhs.regionId) {
      return lhs.originalIndex < rhs.originalIndex;
    }
    return lhs.regionId < rhs.regionId;
  });
  for (const auto& removed : notes) {
    auto* region = project.findRegion(removed.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Region for a deleted note was not found",
                           removed.regionId.toString());
    }
    if (region->findNote(removed.note.id) != nullptr) {
      return core::failure(core::ErrorCode::Conflict,
                           "Deleted note already exists during undo",
                           removed.note.id.toString());
    }
    const auto index = std::min(removed.originalIndex, region->notes.size());
    region->notes.insert(region->notes.begin() + static_cast<std::ptrdiff_t>(index),
                         removed.note);
  }
  auto overrides = removedOverrides_;
  std::stable_sort(overrides.begin(), overrides.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.regionId == rhs.regionId) {
      return lhs.originalIndex < rhs.originalIndex;
    }
    return lhs.regionId < rhs.regionId;
  });
  for (const auto& removed : overrides) {
    auto* region = project.findRegion(removed.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Region for a deleted phoneme override was not found",
                           removed.regionId.toString());
    }
    if (region->findPhonemeOverride(removed.overrideValue.key) == nullptr) {
      const auto index = std::min(removed.originalIndex, region->phonemeOverrides.size());
      region->phonemeOverrides.insert(
          region->phonemeOverrides.begin() + static_cast<std::ptrdiff_t>(index),
          removed.overrideValue);
    }
  }

  auto unitOverrides = removedUnitOverrides_;
  std::stable_sort(unitOverrides.begin(), unitOverrides.end(),
      [](const auto& lhs, const auto& rhs) {
        if (lhs.regionId == rhs.regionId) return lhs.originalIndex < rhs.originalIndex;
        return lhs.regionId < rhs.regionId;
      });
  for (const auto& removed : unitOverrides) {
    auto* region = project.findRegion(removed.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Region for a deleted unit selection override was not found",
                           removed.regionId.toString());
    }
    if (region->findUnitSelectionOverride(removed.overrideValue.startKey) == nullptr) {
      const auto index = std::min(removed.originalIndex,
                                  region->unitSelectionOverrides.size());
      region->unitSelectionOverrides.insert(
          region->unitSelectionOverrides.begin() +
              static_cast<std::ptrdiff_t>(index),
          removed.overrideValue);
    }
  }

  auto seamOverrides = removedSeamOverrides_;
  std::stable_sort(seamOverrides.begin(), seamOverrides.end(),
      [](const auto& lhs, const auto& rhs) {
        if (lhs.regionId == rhs.regionId) return lhs.originalIndex < rhs.originalIndex;
        return lhs.regionId < rhs.regionId;
      });
  for (const auto& removed : seamOverrides) {
    auto* region = project.findRegion(removed.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Region for a deleted seam override was not found",
                           removed.regionId.toString());
    }
    if (region->findSeamOverride(removed.overrideValue.incomingStartKey) == nullptr) {
      const auto index = std::min(removed.originalIndex, region->seamOverrides.size());
      region->seamOverrides.insert(
          region->seamOverrides.begin() + static_cast<std::ptrdiff_t>(index),
          removed.overrideValue);
    }
  }

  for (auto& track : project.vocalTracks()) {
    for (auto& region : track.regions) {
      region.sortNotes();
    }
  }
  return core::success();
}

core::Result<void> MoveNotesCommand::set(domain::Project& project, bool after) {
  for (const auto& move : moves_) {
    auto* note = project.findNote(move.noteId);
    if (note == nullptr) {
      return core::failure(core::ErrorCode::NotFound, "Moved note was not found",
                           move.noteId.toString());
    }
    const auto value = after ? move.after : move.before;
    const auto key = after ? move.afterKey : move.beforeKey;
    if (value < time::Tick{0}) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "A note cannot be moved before tick zero", move.noteId.toString());
    }
    if (key > 127) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "A moved note must remain in the MIDI range", move.noteId.toString());
    }
  }
  for (const auto& move : moves_) {
    auto* note = project.findNote(move.noteId);
    note->startTick = after ? move.after : move.before;
    note->midiKey = after ? move.afterKey : move.beforeKey;
  }
  for (auto& track : project.vocalTracks()) {
    for (auto& region : track.regions) {
      region.sortNotes();
    }
  }
  return core::success();
}

core::Result<void> MoveNotesCommand::apply(domain::Project& project) {
  return set(project, true);
}

core::Result<void> MoveNotesCommand::revert(domain::Project& project) {
  return set(project, false);
}

core::Result<void> ResizeNotesCommand::set(domain::Project& project, bool after) {
  for (const auto& resize : resizes_) {
    auto* note = project.findNote(resize.noteId);
    if (note == nullptr) {
      return core::failure(core::ErrorCode::NotFound, "Resized note was not found",
                           resize.noteId.toString());
    }
    const auto start = after ? resize.afterStart : resize.beforeStart;
    const auto duration = after ? resize.afterDuration : resize.beforeDuration;
    if (start < time::Tick{0} || duration <= time::Tick{0}) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "A resized note must have a non-negative start and positive duration",
                           resize.noteId.toString());
    }
  }
  for (const auto& resize : resizes_) {
    auto* note = project.findNote(resize.noteId);
    note->startTick = after ? resize.afterStart : resize.beforeStart;
    note->durationTick = after ? resize.afterDuration : resize.beforeDuration;
  }
  for (auto& track : project.vocalTracks()) {
    for (auto& region : track.regions) {
      region.sortNotes();
    }
  }
  return core::success();
}

core::Result<void> ResizeNotesCommand::apply(domain::Project& project) {
  return set(project, true);
}

core::Result<void> ResizeNotesCommand::revert(domain::Project& project) {
  return set(project, false);
}

CommandImpact SetNotePerformanceCommand::impact() const {
  CommandImpact result;
  result.scope = CommandAudioImpact::PhraseAudio;
  result.noteIds.reserve(edits_.size());
  for (const auto& edit : edits_) {
    result.noteIds.push_back(edit.noteId);
    if (edit.beforeLyricTokenId.valid()) {
      result.lyricIds.push_back(edit.beforeLyricTokenId);
    }
    if (edit.afterLyricTokenId.valid()) {
      result.lyricIds.push_back(edit.afterLyricTokenId);
    }
  }
  std::sort(result.lyricIds.begin(), result.lyricIds.end());
  result.lyricIds.erase(std::unique(result.lyricIds.begin(), result.lyricIds.end()),
                        result.lyricIds.end());
  return result;
}

core::Result<void> SetNotePerformanceCommand::set(domain::Project& project,
                                                  bool after) {
  if (edits_.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "At least one note performance edit is required");
  }
  std::unordered_set<domain::NoteId> seen;
  seen.reserve(edits_.size());
  for (const auto& edit : edits_) {
    if (!edit.noteId.valid() || !seen.insert(edit.noteId).second) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Note performance edit IDs must be valid and unique");
    }
    const auto location = findMutableNoteLocation(project, edit.noteId);
    if (location.note == nullptr || location.region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Note performance target was not found",
                           edit.noteId.toString());
    }
    const auto lyricId = after ? edit.afterLyricTokenId
                               : edit.beforeLyricTokenId;
    if (!lyricId.valid() || location.region->findLyric(lyricId) == nullptr) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Note performance edit references a missing lyric",
                           edit.noteId.toString());
    }
  }
  for (const auto& edit : edits_) {
    const auto location = findMutableNoteLocation(project, edit.noteId);
    const auto articulation = after ? edit.afterArticulation
                                    : edit.beforeArticulation;
    const auto slur = after ? edit.afterSlurGroup : edit.beforeSlurGroup;
    const auto lyricId = after ? edit.afterLyricTokenId
                               : edit.beforeLyricTokenId;
    location.note->articulation = articulation;
    location.note->slurGroup = slur;
    location.note->lyricTokenId = lyricId;
  }
  return core::success();
}

core::Result<void> SetNotePerformanceCommand::apply(domain::Project& project) {
  return set(project, true);
}

core::Result<void> SetNotePerformanceCommand::revert(domain::Project& project) {
  return set(project, false);
}

}  // namespace seam::application
