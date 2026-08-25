#include "seam/domain/project.hpp"

#include <cmath>
#include <unordered_set>
#include <set>

namespace seam::domain {

namespace {

bool isSha256(std::string_view value) noexcept {
  if (value.size() != 64U) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
  });
}

}

std::string_view mediaOwnershipName(MediaOwnership ownership) noexcept {
  switch (ownership) {
    case MediaOwnership::ExternalReference: return "external-reference";
    case MediaOwnership::ProjectCopy: return "project-copy";
  }
  return "unknown";
}

Note* VocalRegion::findNote(NoteId noteId) noexcept {
  const auto iterator = std::find_if(notes.begin(), notes.end(),
      [noteId](const Note& note) { return note.id == noteId; });
  return iterator == notes.end() ? nullptr : &*iterator;
}

const Note* VocalRegion::findNote(NoteId noteId) const noexcept {
  const auto iterator = std::find_if(notes.begin(), notes.end(),
      [noteId](const Note& note) { return note.id == noteId; });
  return iterator == notes.end() ? nullptr : &*iterator;
}

LyricToken* VocalRegion::findLyric(LyricTokenId lyricId) noexcept {
  const auto iterator = std::find_if(lyrics.begin(), lyrics.end(),
      [lyricId](const LyricToken& lyric) { return lyric.id == lyricId; });
  return iterator == lyrics.end() ? nullptr : &*iterator;
}

const LyricToken* VocalRegion::findLyric(LyricTokenId lyricId) const noexcept {
  const auto iterator = std::find_if(lyrics.begin(), lyrics.end(),
      [lyricId](const LyricToken& lyric) { return lyric.id == lyricId; });
  return iterator == lyrics.end() ? nullptr : &*iterator;
}

PhonemeOverride* VocalRegion::findPhonemeOverride(PhonemeKey key) noexcept {
  const auto iterator = std::find_if(phonemeOverrides.begin(), phonemeOverrides.end(),
      [key](const PhonemeOverride& overrideValue) { return overrideValue.key == key; });
  return iterator == phonemeOverrides.end() ? nullptr : &*iterator;
}

const PhonemeOverride* VocalRegion::findPhonemeOverride(PhonemeKey key) const noexcept {
  const auto iterator = std::find_if(phonemeOverrides.begin(), phonemeOverrides.end(),
      [key](const PhonemeOverride& overrideValue) { return overrideValue.key == key; });
  return iterator == phonemeOverrides.end() ? nullptr : &*iterator;
}

UnitSelectionOverride* VocalRegion::findUnitSelectionOverride(PhonemeKey startKey) noexcept {
  const auto iterator = std::find_if(unitSelectionOverrides.begin(),
                                     unitSelectionOverrides.end(),
      [startKey](const UnitSelectionOverride& value) {
        return value.startKey == startKey;
      });
  return iterator == unitSelectionOverrides.end() ? nullptr : &*iterator;
}

const UnitSelectionOverride* VocalRegion::findUnitSelectionOverride(
    PhonemeKey startKey) const noexcept {
  const auto iterator = std::find_if(unitSelectionOverrides.begin(),
                                     unitSelectionOverrides.end(),
      [startKey](const UnitSelectionOverride& value) {
        return value.startKey == startKey;
      });
  return iterator == unitSelectionOverrides.end() ? nullptr : &*iterator;
}

SeamOverride* VocalRegion::findSeamOverride(PhonemeKey incomingStartKey) noexcept {
  const auto iterator = std::find_if(seamOverrides.begin(), seamOverrides.end(),
      [incomingStartKey](const SeamOverride& value) {
        return value.incomingStartKey == incomingStartKey;
      });
  return iterator == seamOverrides.end() ? nullptr : &*iterator;
}

const SeamOverride* VocalRegion::findSeamOverride(
    PhonemeKey incomingStartKey) const noexcept {
  const auto iterator = std::find_if(seamOverrides.begin(), seamOverrides.end(),
      [incomingStartKey](const SeamOverride& value) {
        return value.incomingStartKey == incomingStartKey;
      });
  return iterator == seamOverrides.end() ? nullptr : &*iterator;
}

void VocalRegion::sortNotes() {
  std::stable_sort(notes.begin(), notes.end(), [](const Note& lhs, const Note& rhs) {
    if (lhs.startTick == rhs.startTick) {
      if (lhs.midiKey == rhs.midiKey) {
        return lhs.id < rhs.id;
      }
      return lhs.midiKey < rhs.midiKey;
    }
    return lhs.startTick < rhs.startTick;
  });
}

core::Result<void> VocalRegion::validate() const {
  if (!id.valid()) {
    return core::failure(core::ErrorCode::InvariantViolation, "Region ID must be valid");
  }
  if (startTick < time::Tick{0} || durationTick <= time::Tick{0}) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Region time range is invalid", id.toString());
  }

  std::unordered_set<LyricTokenId> lyricIds;
  for (const auto& lyric : lyrics) {
    if (!lyric.id.valid() || !lyricIds.insert(lyric.id).second) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Lyric IDs must be valid and unique", id.toString());
    }
  }

  std::unordered_set<NoteId> noteIds;
  for (const auto& note : notes) {
    const auto noteResult = note.validate();
    if (!noteResult) {
      return noteResult;
    }
    if (!noteIds.insert(note.id).second) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Note IDs must be unique", id.toString());
    }
    if (!lyricIds.contains(note.lyricTokenId)) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Note references a missing lyric", note.id.toString());
    }
    if (note.endTick() > durationTick) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Note extends beyond region duration", note.id.toString());
    }
  }

  std::set<PhonemeKey> overrideKeys;
  for (const auto& overrideValue : phonemeOverrides) {
    const auto overrideResult = overrideValue.validate();
    if (!overrideResult) {
      return overrideResult;
    }
    if (!noteIds.contains(overrideValue.key.noteId)) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Phoneme override references a missing note",
                           overrideValue.key.toString());
    }
    if (!overrideKeys.insert(overrideValue.key).second) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Phoneme override keys must be unique",
                           overrideValue.key.toString());
    }
  }

  std::set<PhonemeKey> unitOverrideKeys;
  for (const auto& overrideValue : unitSelectionOverrides) {
    const auto overrideResult = overrideValue.validate();
    if (!overrideResult) return overrideResult;
    if (!noteIds.contains(overrideValue.startKey.noteId)) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Unit selection override references a missing note",
                           overrideValue.startKey.toString());
    }
    if (!unitOverrideKeys.insert(overrideValue.startKey).second) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Unit selection override keys must be unique",
                           overrideValue.startKey.toString());
    }
  }

  std::set<PhonemeKey> seamOverrideKeys;
  for (const auto& overrideValue : seamOverrides) {
    const auto overrideResult = overrideValue.validate();
    if (!overrideResult) return overrideResult;
    if (!noteIds.contains(overrideValue.incomingStartKey.noteId)) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Seam override references a missing note",
                           overrideValue.incomingStartKey.toString());
    }
    if (!seamOverrideKeys.insert(overrideValue.incomingStartKey).second) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Seam override keys must be unique",
                           overrideValue.incomingStartKey.toString());
    }
  }
  const auto pitchValidation = pitchAutomation.validate();
  if (!pitchValidation) return pitchValidation;
  if (!pitchAutomation.points().empty() &&
      pitchAutomation.points().back().tick > durationTick) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Pitch automation extends beyond the region");
  }
  return core::success();
}

VocalRegion* VocalTrack::findRegion(RegionId regionId) noexcept {
  const auto iterator = std::find_if(regions.begin(), regions.end(),
      [regionId](const VocalRegion& region) { return region.id == regionId; });
  return iterator == regions.end() ? nullptr : &*iterator;
}

const VocalRegion* VocalTrack::findRegion(RegionId regionId) const noexcept {
  const auto iterator = std::find_if(regions.begin(), regions.end(),
      [regionId](const VocalRegion& region) { return region.id == regionId; });
  return iterator == regions.end() ? nullptr : &*iterator;
}

Project::Project() : Project(ProjectId{1}, "Untitled", time::kDefaultPpq) {}

Project::Project(ProjectId id, std::string name, time::Ppq ppq)
    : id_(id),
      name_(std::move(name)),
      tempoMap_(ppq),
      meterMap_(ppq) {}

VocalTrack* Project::findVocalTrack(TrackId trackId) noexcept {
  const auto iterator = std::find_if(vocalTracks_.begin(), vocalTracks_.end(),
      [trackId](const VocalTrack& track) { return track.id == trackId; });
  return iterator == vocalTracks_.end() ? nullptr : &*iterator;
}

const VocalTrack* Project::findVocalTrack(TrackId trackId) const noexcept {
  const auto iterator = std::find_if(vocalTracks_.begin(), vocalTracks_.end(),
      [trackId](const VocalTrack& track) { return track.id == trackId; });
  return iterator == vocalTracks_.end() ? nullptr : &*iterator;
}

VocalRegion* Project::findRegion(RegionId regionId) noexcept {
  for (auto& track : vocalTracks_) {
    if (auto* region = track.findRegion(regionId)) {
      return region;
    }
  }
  return nullptr;
}

const VocalRegion* Project::findRegion(RegionId regionId) const noexcept {
  for (const auto& track : vocalTracks_) {
    if (const auto* region = track.findRegion(regionId)) {
      return region;
    }
  }
  return nullptr;
}

Note* Project::findNote(NoteId noteId) noexcept {
  for (auto& track : vocalTracks_) {
    for (auto& region : track.regions) {
      if (auto* note = region.findNote(noteId)) {
        return note;
      }
    }
  }
  return nullptr;
}

const Note* Project::findNote(NoteId noteId) const noexcept {
  for (const auto& track : vocalTracks_) {
    for (const auto& region : track.regions) {
      if (const auto* note = region.findNote(noteId)) {
        return note;
      }
    }
  }
  return nullptr;
}

core::Result<void> Project::validate() const {
  if (!id_.valid()) {
    return core::failure(core::ErrorCode::InvariantViolation, "Project ID must be valid");
  }
  if (settings_.hostStartOffsetTick < time::Tick{0}) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Host start offset tick cannot be negative");
  }
  if (name_.empty()) {
    return core::failure(core::ErrorCode::InvariantViolation, "Project name must not be empty");
  }
  if (!std::isfinite(settings_.sampleRate) || settings_.sampleRate < 8000.0 ||
      settings_.sampleRate > 384000.0) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Project sample rate is unsupported");
  }
  if (settings_.snapGrid <= time::Tick{0}) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Project snap grid must be positive");
  }
  const auto routingValidation = routing_.validate();
  if (!routingValidation) return routingValidation;

  const auto validateTrackRoute = [this](const TrackOutputRoute& route,
                                         TrackId trackId) -> core::Result<void> {
    const auto routeValidation = route.validate();
    if (!routeValidation) return routeValidation;
    const auto* bus = routing_.findBus(route.bus);
    if (bus == nullptr || route.matrix.sourceChannels != 1U ||
        route.matrix.destinationChannels != bus->channelCount) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Track output route does not match its destination bus",
                           trackId.toString());
    }
    return core::success();
  };

  std::unordered_set<TrackId> trackIds;
  std::unordered_set<RegionId> regionIds;
  std::unordered_set<NoteId> noteIds;
  for (const auto& track : vocalTracks_) {
    if (!track.id.valid() || !trackIds.insert(track.id).second) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Track IDs must be valid and unique");
    }
    if (!std::isfinite(track.gainDb) || !std::isfinite(track.pan) || track.pan < -1.0F ||
        track.pan > 1.0F) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Track mix settings are invalid", track.id.toString());
    }
    const auto routeValidation = validateTrackRoute(track.outputRoute, track.id);
    if (!routeValidation) return routeValidation;
    for (const auto& region : track.regions) {
      if (!regionIds.insert(region.id).second) {
        return core::failure(core::ErrorCode::InvariantViolation,
                             "Region IDs must be globally unique", region.id.toString());
      }
      const auto regionResult = region.validate();
      if (!regionResult) {
        return regionResult;
      }
      for (const auto& note : region.notes) {
        if (!noteIds.insert(note.id).second) {
          return core::failure(core::ErrorCode::InvariantViolation,
                               "Note IDs must be globally unique", note.id.toString());
        }
      }
    }
  }
  for (const auto& track : audioTracks_) {
    if (!track.id.valid() || !trackIds.insert(track.id).second) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Audio track ID must be valid and unique");
    }
    if (!std::isfinite(track.gainDb) || !std::isfinite(track.pan) ||
        track.pan < -1.0F || track.pan > 1.0F) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Audio track mix settings are invalid", track.id.toString());
    }
    if (track.mediaOwnership == MediaOwnership::ProjectCopy &&
        (!isSha256(track.mediaHash) || track.mediaPath.empty() ||
         track.sourceSampleRate == 0U || track.sourceChannels == 0U ||
         track.sourceChannels > kMaximumAudioChannels)) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Project-owned audio track identity is incomplete",
                           track.id.toString());
    }
    if (track.trimEndFrame.has_value() &&
        *track.trimEndFrame <= track.trimStartFrame) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Audio track trim end must be after trim start",
                           track.id.toString());
    }
    if (track.sourceFrameCount != 0U &&
        (track.trimStartFrame >= track.sourceFrameCount ||
         (track.trimEndFrame.has_value() &&
          *track.trimEndFrame > track.sourceFrameCount))) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Audio track trim exceeds source frame count",
                           track.id.toString());
    }
    const auto routeValidation = validateTrackRoute(track.outputRoute, track.id);
    if (!routeValidation) return routeValidation;
  }
  return core::success();
}

std::size_t Project::noteCount() const noexcept {
  std::size_t count = 0;
  for (const auto& track : vocalTracks_) {
    for (const auto& region : track.regions) {
      count += region.notes.size();
    }
  }
  return count;
}

}  // namespace seam::domain
