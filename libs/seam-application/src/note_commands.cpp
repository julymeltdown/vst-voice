#include "seam/application/note_commands.hpp"

#include <algorithm>
#include <cstddef>
#include <set>
#include <unordered_set>

namespace seam::application {

AddNoteCommand::AddNoteCommand(
    domain::RegionId regionId, domain::LyricToken lyric, domain::Note note)
    : regionId_(regionId), lyric_(std::move(lyric)), note_(std::move(note)) {}

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
    }
  }

  noteIds_ = std::move(uniqueIds);
  captured_ = true;
  return core::success();
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

}  // namespace seam::application
