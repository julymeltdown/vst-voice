#include "seam/application/note_commands.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/phonemizer/language_resolver.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>
#include <set>
#include <unordered_set>
#include <variant>

namespace seam::application {

AddNoteCommand::AddNoteCommand(
    domain::RegionId regionId, domain::LyricToken lyric, domain::Note note, LyricMode lyricMode)
    : regionId_(regionId), lyric_(std::move(lyric)), note_(std::move(note)), lyricMode_(lyricMode) {}

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
  const auto* existingLyric = region->findLyric(lyric_.id);
  if (region->findNote(note_.id) != nullptr ||
      (lyricMode_ == LyricMode::Create && existingLyric != nullptr)) {
    return core::failure(core::ErrorCode::Conflict, "Note or lyric ID already exists");
  }
  if (lyricMode_ == LyricMode::ReuseExact && (!existingLyric || *existingLyric != lyric_)) {
    return core::failure(core::ErrorCode::Conflict, "Shared lyric is missing or changed");
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

  if (region->performance.revision.pronunciation == std::numeric_limits<std::uint64_t>::max()) {
    return core::failure(core::ErrorCode::Conflict, "Pronunciation revision is exhausted");
  }
  auto candidate = *region;
  if (lyricMode_ == LyricMode::Create) candidate.lyrics.push_back(lyric_);
  candidate.notes.push_back(note_);
  candidate.sortNotes();
  if (!pronunciationCaptured_) {
    beforePronunciation_ = region->performance.pronunciation;
    beforePronunciationRevision_ = region->performance.revision.pronunciation;
    beforePhonemes_ = region->phonemeOverrides;
    beforeUnits_ = region->unitSelectionOverrides;
    beforeSeams_ = region->seamOverrides;
    reconcileRetainedNoteOverrides(*region, candidate);
    afterPhonemes_ = candidate.phonemeOverrides;
    afterUnits_ = candidate.unitSelectionOverrides;
    afterSeams_ = candidate.seamOverrides;
    const bool supported = std::all_of(candidate.notes.begin(), candidate.notes.end(), [&](const auto& note) {
      const auto* lyric = candidate.findLyric(note.lyricTokenId);
      return lyric && phonemizer::hasPronunciationService(lyric->language);
    });
    if (supported) {
      const auto resolved = phonemizer::resolvePronunciation(candidate);
      if (resolved && (resolved.value().identity.language == domain::Language::Japanese ||
                       resolved.value().pronunciation.warnings.empty())) {
        afterPronunciation_ = resolved.value().identity;
      }
    }
    pronunciationCaptured_ = true;
  }
  candidate.performance.pronunciation = afterPronunciation_;
  candidate.phonemeOverrides = afterPhonemes_;
  candidate.unitSelectionOverrides = afterUnits_;
  candidate.seamOverrides = afterSeams_;
  candidate.performance.revision.pronunciation = beforePronunciationRevision_ + 1U;
  const auto validation = candidate.validate();
  if (!validation) return validation;
  region->lyrics.swap(candidate.lyrics);
  region->notes.swap(candidate.notes);
  region->phonemeOverrides.swap(candidate.phonemeOverrides);
  region->unitSelectionOverrides.swap(candidate.unitSelectionOverrides);
  region->seamOverrides.swap(candidate.seamOverrides);
  region->performance.pronunciation.swap(candidate.performance.pronunciation);
  region->performance.revision.pronunciation = candidate.performance.revision.pronunciation;
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
  if (!lyricInUse && lyricMode_ == LyricMode::Create) {
    const auto lyricIterator = std::find_if(region->lyrics.begin(), region->lyrics.end(),
        [this](const domain::LyricToken& lyric) { return lyric.id == lyric_.id; });
    if (lyricIterator != region->lyrics.end()) {
      region->lyrics.erase(lyricIterator);
    }
  }
  region->performance.pronunciation = beforePronunciation_;
  region->phonemeOverrides = beforePhonemes_;
  region->unitSelectionOverrides = beforeUnits_;
  region->seamOverrides = beforeSeams_;
  region->performance.revision.pronunciation = beforePronunciationRevision_;
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
  performanceChanges_.clear();
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
      std::vector<domain::PerformanceNoteRemap> remaining;
      for (const auto& note : region.notes) {
        if (!selected.contains(note.id)) remaining.push_back({note.id, note.id});
      }
      if (remaining.size() != region.notes.size()) {
        auto transformed = domain::transformRegionPerformance(region.performance,
            region.notes, region.durationTick, remaining, {time::Tick{0}, region.durationTick});
        if (!transformed) return core::Result<void>{transformed.error()};
        if (region.performance.revision.pronunciation == std::numeric_limits<std::uint64_t>::max()) {
          return core::failure(core::ErrorCode::Conflict, "Pronunciation revision is exhausted");
        }
        auto candidate = region;
        std::erase_if(candidate.notes, [&](const auto& note) { return selected.contains(note.id); });
        reconcileRetainedNoteOverrides(region, candidate);
        std::erase_if(candidate.phonemeOverrides, [&](const auto& edit) { return selected.contains(edit.key.noteId); });
        std::erase_if(candidate.unitSelectionOverrides, [&](const auto& edit) { return selected.contains(edit.startKey.noteId); });
        std::erase_if(candidate.seamOverrides, [&](const auto& edit) { return selected.contains(edit.incomingStartKey.noteId); });
        for (const auto& removed : removedLyrics_) {
          if (removed.regionId == region.id) std::erase_if(candidate.lyrics, [&](const auto& lyric) { return lyric.id == removed.lyric.id; });
        }
        candidate.performance = std::move(transformed).value();
        candidate.performance.revision.pronunciation = region.performance.revision.pronunciation + 1U;
        candidate.performance.pronunciation.reset();
        const bool supported = std::all_of(candidate.notes.begin(), candidate.notes.end(), [&](const auto& note) {
          const auto* lyric = candidate.findLyric(note.lyricTokenId);
          return lyric && phonemizer::hasPronunciationService(lyric->language);
        });
        if (supported) {
          const auto resolved = phonemizer::resolvePronunciation(candidate);
          if (resolved && (resolved.value().identity.language == domain::Language::Japanese ||
                           resolved.value().pronunciation.warnings.empty())) {
            candidate.performance.pronunciation = resolved.value().identity;
          }
        }
        const auto valid = candidate.validate();
        if (!valid) return valid;
        performanceChanges_.push_back({region.id, region.performance, candidate.performance,
            region.phonemeOverrides, candidate.phonemeOverrides, region.unitSelectionOverrides,
            candidate.unitSelectionOverrides, region.seamOverrides, candidate.seamOverrides});
      }
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
  for (const auto& change : performanceChanges_) {
    if (project.findRegion(change.regionId) == nullptr) {
      return core::failure(core::ErrorCode::NotFound, "Region for deleted-note performance was not found");
    }
  }
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
  for (const auto& change : performanceChanges_) {
    auto* region = project.findRegion(change.regionId);
    region->performance = change.after;
    region->phonemeOverrides = change.afterPhonemes;
    region->unitSelectionOverrides = change.afterUnits;
    region->seamOverrides = change.afterSeams;
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

  for (const auto& change : performanceChanges_) {
    auto* region = project.findRegion(change.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound, "Region for restored-note performance was not found");
    }
    region->performance = change.before;
    region->phonemeOverrides = change.beforePhonemes;
    region->unitSelectionOverrides = change.beforeUnits;
    region->seamOverrides = change.beforeSeams;
  }
  for (auto& track : project.vocalTracks()) {
    for (auto& region : track.regions) {
      region.sortNotes();
    }
  }
  return core::success();
}

namespace {

// Stage all affected regions before publication. Note moves translate accepted
// source mappings; edge resizing trims/extends in the existing source timeline.
core::Result<void> replaceNoteGeometry(domain::Project& project,
                                      const std::vector<domain::Note>& replacements,
                                      bool translateSource,
                                      std::vector<NotePronunciationChange>& pronunciationChanges,
                                      bool& pronunciationCaptured, bool after) {
  struct StagedRegion final {
    domain::VocalRegion* target;
    domain::VocalRegion value;
  };
  std::vector<StagedRegion> staged;
  auto changes = pronunciationChanges;
  std::unordered_set<domain::NoteId> seen;
  for (const auto& replacement : replacements) {
    if (!seen.insert(replacement.id).second) {
      return core::failure(core::ErrorCode::InvalidArgument, "Repeated note geometry target");
    }
    const auto valid = replacement.validate();
    if (!valid) return valid;
    const auto location = findMutableNoteLocation(project, replacement.id);
    if (location.region == nullptr) {
      return core::failure(core::ErrorCode::NotFound, "Note geometry target was not found");
    }
    auto found = std::find_if(staged.begin(), staged.end(), [&](const auto& entry) {
      return entry.target == location.region;
    });
    if (found == staged.end()) {
      staged.push_back({location.region, *location.region});
      found = std::prev(staged.end());
    }
    auto* note = found->value.findNote(replacement.id);
    const auto oldValid = note->validate();
    if (!oldValid) return oldValid;
    if (translateSource) {
      const auto delta = note->startTick.value() - replacement.startTick.value();
      for (auto& selection : found->value.performance.accepted) {
        const auto* id = std::get_if<domain::NoteId>(&selection.scope);
        if (id == nullptr || *id != note->id) continue;
        const auto offset = selection.sourceTickOffset.value();
        if ((delta > 0 && offset > std::numeric_limits<std::int64_t>::max() - delta) ||
            (delta < 0 && offset < std::numeric_limits<std::int64_t>::min() - delta)) {
          return core::failure(core::ErrorCode::InvalidArgument,
                               "Moved performance source offset overflows");
        }
        selection.sourceTickOffset = time::Tick{offset + delta};
      }
    }
    *note = replacement;
  }
  for (auto& entry : staged) {
    entry.value.sortNotes();
    if (!pronunciationCaptured) {
      const bool changed = std::any_of(entry.value.notes.begin(), entry.value.notes.end(), [&](const auto& note) {
        const auto* original = entry.target->findNote(note.id);
        return original && (original->startTick != note.startTick || original->lyricTokenId != note.lyricTokenId ||
            original->durationTick != note.durationTick || original->articulation != note.articulation ||
            original->phoneticHint != note.phoneticHint);
      });
      if (changed) {
        const auto revision = entry.target->performance.revision.pronunciation;
        if (revision == std::numeric_limits<std::uint64_t>::max()) {
          return core::failure(core::ErrorCode::Conflict, "Pronunciation revision is exhausted");
        }
        std::optional<domain::PronunciationIdentity> identity;
        reconcileRetainedNoteOverrides(*entry.target, entry.value);
        const bool supported = std::all_of(entry.value.notes.begin(), entry.value.notes.end(), [&](const auto& note) {
          const auto* lyric = entry.value.findLyric(note.lyricTokenId);
          return lyric && phonemizer::hasPronunciationService(lyric->language);
        });
        if (supported) {
          const auto resolved = phonemizer::resolvePronunciation(entry.value);
          if (resolved && (resolved.value().identity.language == domain::Language::Japanese ||
                           resolved.value().pronunciation.warnings.empty())) {
            identity = resolved.value().identity;
          }
        }
        changes.push_back({entry.value.id, entry.target->performance.pronunciation, std::move(identity), revision,
            entry.target->phonemeOverrides, entry.value.phonemeOverrides,
            entry.target->unitSelectionOverrides, entry.value.unitSelectionOverrides,
            entry.target->seamOverrides, entry.value.seamOverrides});
      }
    }
    const auto change = std::find_if(changes.begin(), changes.end(), [&](const auto& value) { return value.regionId == entry.value.id; });
    if (change != changes.end()) {
      entry.value.phonemeOverrides = after ? change->afterPhonemes : change->beforePhonemes;
      entry.value.unitSelectionOverrides = after ? change->afterUnits : change->beforeUnits;
      entry.value.seamOverrides = after ? change->afterSeams : change->beforeSeams;
      entry.value.performance.pronunciation = after ? change->after : change->before;
      entry.value.performance.revision.pronunciation = change->beforeRevision + (after ? 1U : 0U);
    }
    const auto valid = entry.value.validate();
    if (!valid) return valid;
  }
  for (auto& entry : staged) std::swap(*entry.target, entry.value);
  pronunciationChanges = std::move(changes);
  pronunciationCaptured = true;
  return core::success();
}

}  // namespace

core::Result<void> MoveNotesCommand::set(domain::Project& project, bool after) {
  std::vector<domain::Note> replacements;
  replacements.reserve(moves_.size());
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
    auto replacement = *note;
    replacement.startTick = value;
    replacement.midiKey = key;
    replacements.push_back(std::move(replacement));
  }
  return replaceNoteGeometry(project, replacements, true, pronunciationChanges_, pronunciationCaptured_, after);
}

CommandImpact SetNoteHintsCommand::impact() const {
  CommandImpact impact; impact.scope = CommandAudioImpact::PhraseAudio;
  for (const auto& edit : edits_) impact.noteIds.push_back(edit.noteId);
  return impact;
}

core::Result<void> SetNoteHintsCommand::set(domain::Project& project, bool after) {
  if (edits_.empty() || edits_.size() > 10000U)
    return core::failure(core::ErrorCode::InvalidArgument, "Hint editing requires 1 to 10000 notes");
  std::vector<domain::Note> replacements; replacements.reserve(edits_.size());
  std::unordered_set<domain::NoteId> ids;
  for (const auto& edit : edits_) {
    if (!ids.insert(edit.noteId).second) return core::failure(core::ErrorCode::InvalidArgument, "Repeated hint target");
    const auto location = findMutableNoteLocation(project, edit.noteId);
    if (!location.note || !location.region) return core::failure(core::ErrorCode::NotFound, "Hint note was not found");
    if (location.note->phoneticHint != (after ? edit.before : edit.after))
      return core::failure(core::ErrorCode::Conflict, "Pronunciation hint changed after the edit was prepared");
    const auto& target = after ? edit.after : edit.before;
    if (after && target) {
      const auto* lyric = location.region->findLyric(location.note->lyricTokenId);
      if (!lyric || !phonemizer::hasPronunciationService(lyric->language))
        return core::failure(core::ErrorCode::Unsupported, "No registered hint validator for this note language");
      const auto valid = phonemizer::validatePhoneHintForLanguage(lyric->language, *target);
      if (!valid) return valid;
    }
    auto note = *location.note; note.phoneticHint = target; replacements.push_back(std::move(note));
  }
  return replaceNoteGeometry(project, replacements, false, pronunciationChanges_, pronunciationCaptured_, after);
}
core::Result<void> SetNoteHintsCommand::apply(domain::Project& project) { return set(project, true); }
core::Result<void> SetNoteHintsCommand::revert(domain::Project& project) {
  if (!pronunciationCaptured_) return core::failure(core::ErrorCode::Conflict, "Hint edit has no state to restore");
  return set(project, false);
}

core::Result<void> MoveNotesCommand::apply(domain::Project& project) {
  return set(project, true);
}

core::Result<void> MoveNotesCommand::revert(domain::Project& project) {
  return set(project, false);
}

core::Result<void> ResizeNotesCommand::set(domain::Project& project, bool after) {
  std::vector<domain::Note> replacements;
  replacements.reserve(resizes_.size());
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
    auto replacement = *note;
    replacement.startTick = start;
    replacement.durationTick = duration;
    replacements.push_back(std::move(replacement));
  }
  return replaceNoteGeometry(project, replacements, false, pronunciationChanges_, pronunciationCaptured_, after);
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
  std::vector<domain::Note> replacements;
  replacements.reserve(edits_.size());
  for (const auto& edit : edits_) {
    const auto location = findMutableNoteLocation(project, edit.noteId);
    const auto articulation = after ? edit.afterArticulation
                                    : edit.beforeArticulation;
    const auto slur = after ? edit.afterSlurGroup : edit.beforeSlurGroup;
    const auto lyricId = after ? edit.afterLyricTokenId
                               : edit.beforeLyricTokenId;
    auto replacement = *location.note;
    replacement.articulation = articulation;
    replacement.slurGroup = slur;
    replacement.lyricTokenId = lyricId;
    replacements.push_back(std::move(replacement));
  }
  return replaceNoteGeometry(project, replacements, false, pronunciationChanges_, pronunciationCaptured_, after);
}

core::Result<void> SetNotePerformanceCommand::apply(domain::Project& project) {
  return set(project, true);
}

core::Result<void> SetNotePerformanceCommand::revert(domain::Project& project) {
  return set(project, false);
}

}  // namespace seam::application
