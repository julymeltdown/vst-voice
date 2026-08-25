#include "seam/application/arrangement_commands.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace seam::application {

namespace {

bool containsLyricId(const domain::Project& project,
                     domain::LyricTokenId lyricId) {
  for (const auto& track : project.vocalTracks()) {
    for (const auto& region : track.regions) {
      if (std::any_of(region.lyrics.begin(), region.lyrics.end(),
                      [lyricId](const auto& lyric) {
                        return lyric.id == lyricId;
                      })) {
        return true;
      }
    }
  }
  return false;
}

std::uint64_t maximumId(const domain::Project& project) noexcept {
  std::uint64_t maximum = project.id().value();
  const auto observe = [&maximum](std::uint64_t value) {
    maximum = std::max(maximum, value);
  };
  for (const auto& track : project.vocalTracks()) {
    observe(track.id.value());
    for (const auto& region : track.regions) {
      observe(region.id.value());
      for (const auto& lyric : region.lyrics) observe(lyric.id.value());
      for (const auto& note : region.notes) observe(note.id.value());
    }
  }
  for (const auto& track : project.audioTracks()) observe(track.id.value());
  for (const auto& bus : project.routing().buses) observe(bus.id.value());
  return maximum;
}

class FreshIdAllocator final {
public:
  explicit FreshIdAllocator(std::uint64_t first) noexcept : next_(first) {}

  template <typename Tag>
  core::Result<core::Id<Tag>> next() {
    if (next_ == 0U || next_ == std::numeric_limits<std::uint64_t>::max()) {
      return core::failure<core::Id<Tag>>(
          core::ErrorCode::Conflict,
          "Unable to allocate a fresh project identity");
    }
    return core::success(core::Id<Tag>{next_++});
  }

private:
  std::uint64_t next_{1U};
};

core::Result<domain::VocalRegion> cloneRegionWithFreshIds(
    const domain::VocalRegion& source, FreshIdAllocator& ids) {
  auto validation = source.validate();
  if (!validation) return core::Result<domain::VocalRegion>{validation.error()};
  auto clone = source;
  auto regionId = ids.next<domain::RegionTag>();
  if (!regionId) return core::Result<domain::VocalRegion>{regionId.error()};
  clone.id = regionId.value();

  std::unordered_map<domain::LyricTokenId, domain::LyricTokenId> lyricIds;
  lyricIds.reserve(clone.lyrics.size());
  for (auto& lyric : clone.lyrics) {
    const auto oldId = lyric.id;
    auto newId = ids.next<domain::LyricTag>();
    if (!newId) return core::Result<domain::VocalRegion>{newId.error()};
    lyric.id = newId.value();
    lyricIds.emplace(oldId, lyric.id);
  }

  std::unordered_map<domain::NoteId, domain::NoteId> noteIds;
  noteIds.reserve(clone.notes.size());
  for (auto& note : clone.notes) {
    const auto lyric = lyricIds.find(note.lyricTokenId);
    if (lyric == lyricIds.end()) {
      return core::failure<domain::VocalRegion>(
          core::ErrorCode::InvariantViolation,
          "Duplicated note references a missing lyric", note.id.toString());
    }
    const auto oldId = note.id;
    auto newId = ids.next<domain::NoteTag>();
    if (!newId) return core::Result<domain::VocalRegion>{newId.error()};
    note.id = newId.value();
    note.lyricTokenId = lyric->second;
    noteIds.emplace(oldId, note.id);
  }

  const auto remapKey = [&noteIds](domain::PhonemeKey key)
      -> core::Result<domain::PhonemeKey> {
    const auto iterator = noteIds.find(key.noteId);
    if (iterator == noteIds.end()) {
      return core::failure<domain::PhonemeKey>(
          core::ErrorCode::InvariantViolation,
          "Duplicated technical override references a missing note");
    }
    key.noteId = iterator->second;
    return core::success(key);
  };
  for (auto& value : clone.phonemeOverrides) {
    auto key = remapKey(value.key);
    if (!key) return core::Result<domain::VocalRegion>{key.error()};
    value.key = key.value();
  }
  for (auto& value : clone.unitSelectionOverrides) {
    auto key = remapKey(value.startKey);
    if (!key) return core::Result<domain::VocalRegion>{key.error()};
    value.startKey = key.value();
  }
  for (auto& value : clone.seamOverrides) {
    auto key = remapKey(value.incomingStartKey);
    if (!key) return core::Result<domain::VocalRegion>{key.error()};
    value.incomingStartKey = key.value();
  }
  return clone;
}

core::Result<domain::VocalTrack> cloneTrackWithFreshIds(
    const domain::VocalTrack& source, FreshIdAllocator& ids) {
  if (!source.id.valid()) {
    return core::failure<domain::VocalTrack>(
        core::ErrorCode::InvalidArgument,
        "Duplicated vocal track requires a valid source ID");
  }
  auto clone = source;
  auto trackId = ids.next<domain::TrackTag>();
  if (!trackId) return core::Result<domain::VocalTrack>{trackId.error()};
  clone.id = trackId.value();
  clone.regions.clear();
  clone.regions.reserve(source.regions.size());
  for (const auto& region : source.regions) {
    auto copied = cloneRegionWithFreshIds(region, ids);
    if (!copied) return core::Result<domain::VocalTrack>{copied.error()};
    clone.regions.push_back(std::move(copied).value());
  }
  return clone;
}

template <typename Track>
core::Result<void> moveTrack(std::vector<Track>& tracks,
                             domain::TrackId trackId,
                             std::size_t destinationIndex,
                             std::size_t* previousIndex = nullptr) {
  if (destinationIndex >= tracks.size()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Track destination index is outside the arrangement");
  }
  const auto iterator = std::find_if(
      tracks.begin(), tracks.end(), [trackId](const auto& track) {
        return track.id == trackId;
      });
  if (iterator == tracks.end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Track to reorder was not found", trackId.toString());
  }
  const auto currentIndex = static_cast<std::size_t>(
      std::distance(tracks.begin(), iterator));
  if (previousIndex != nullptr) *previousIndex = currentIndex;
  if (currentIndex == destinationIndex) return core::success();
  auto moved = std::move(*iterator);
  tracks.erase(iterator);
  const auto insertionIndex = std::min(destinationIndex, tracks.size());
  tracks.insert(tracks.begin() + static_cast<std::ptrdiff_t>(insertionIndex),
                std::move(moved));
  return core::success();
}

core::Result<domain::VocalRegion> makeSplitRightRegion(
    const domain::VocalRegion& source, time::Tick splitTick,
    FreshIdAllocator& ids, domain::VocalRegion& left) {
  if (splitTick <= time::Tick{0} || splitTick >= source.durationTick) {
    return core::failure<domain::VocalRegion>(
        core::ErrorCode::InvalidArgument,
        "Region split must be strictly inside the region");
  }
  const auto validation = source.validate();
  if (!validation) return core::Result<domain::VocalRegion>{validation.error()};

  left = source;
  left.durationTick = splitTick;
  left.name = source.name + " A";
  left.notes.clear();
  left.phonemeOverrides.clear();
  left.unitSelectionOverrides.clear();
  left.seamOverrides.clear();
  left.pitchAutomation.points().clear();

  auto rightId = ids.next<domain::RegionTag>();
  if (!rightId) return core::Result<domain::VocalRegion>{rightId.error()};
  domain::VocalRegion right = source;
  right.id = rightId.value();
  right.name = source.name + " B";
  right.startTick = source.startTick + splitTick;
  right.durationTick = source.durationTick - splitTick;
  right.lyrics.clear();
  right.notes.clear();
  right.phonemeOverrides.clear();
  right.unitSelectionOverrides.clear();
  right.seamOverrides.clear();
  right.pitchAutomation.points().clear();

  std::unordered_map<domain::LyricTokenId, domain::LyricTokenId> rightLyrics;
  std::unordered_map<domain::NoteId, domain::NoteId> rightNotes;
  for (const auto& note : source.notes) {
    if (note.startTick < splitTick && note.endTick() > splitTick) {
      return core::failure<domain::VocalRegion>(
          core::ErrorCode::InvalidArgument,
          "Cannot split a region through the body of a note", note.id.toString());
    }
    if (note.endTick() <= splitTick) {
      left.notes.push_back(note);
      continue;
    }
    if (note.startTick < splitTick) {
      return core::failure<domain::VocalRegion>(
          core::ErrorCode::InvalidArgument,
          "Cannot split a region through the body of a note", note.id.toString());
    }
    const auto* sourceLyric = source.findLyric(note.lyricTokenId);
    if (sourceLyric == nullptr) {
      return core::failure<domain::VocalRegion>(
          core::ErrorCode::InvariantViolation,
          "Split note references a missing lyric", note.id.toString());
    }
    auto lyricId = rightLyrics.find(sourceLyric->id);
    if (lyricId == rightLyrics.end()) {
      auto freshLyric = ids.next<domain::LyricTag>();
      if (!freshLyric) {
        return core::Result<domain::VocalRegion>{freshLyric.error()};
      }
      right.lyrics.push_back(*sourceLyric);
      right.lyrics.back().id = freshLyric.value();
      lyricId = rightLyrics.emplace(sourceLyric->id, freshLyric.value()).first;
    }
    auto copied = note;
    auto freshNote = ids.next<domain::NoteTag>();
    if (!freshNote) return core::Result<domain::VocalRegion>{freshNote.error()};
    copied.id = freshNote.value();
    copied.startTick -= splitTick;
    copied.lyricTokenId = lyricId->second;
    rightNotes.emplace(note.id, copied.id);
    right.notes.push_back(std::move(copied));
  }

  const auto remapKey = [&rightNotes](domain::PhonemeKey key)
      -> std::optional<domain::PhonemeKey> {
    const auto iterator = rightNotes.find(key.noteId);
    if (iterator == rightNotes.end()) return std::nullopt;
    key.noteId = iterator->second;
    return key;
  };
  for (const auto& value : source.phonemeOverrides) {
    if (rightNotes.contains(value.key.noteId)) {
      auto copied = value;
      copied.key = *remapKey(copied.key);
      right.phonemeOverrides.push_back(std::move(copied));
    } else {
      left.phonemeOverrides.push_back(value);
    }
  }
  for (const auto& value : source.unitSelectionOverrides) {
    if (rightNotes.contains(value.startKey.noteId)) {
      auto copied = value;
      copied.startKey = *remapKey(copied.startKey);
      right.unitSelectionOverrides.push_back(std::move(copied));
    } else {
      left.unitSelectionOverrides.push_back(value);
    }
  }
  for (const auto& value : source.seamOverrides) {
    if (rightNotes.contains(value.incomingStartKey.noteId)) {
      auto copied = value;
      copied.incomingStartKey = *remapKey(copied.incomingStartKey);
      right.seamOverrides.push_back(std::move(copied));
    } else {
      left.seamOverrides.push_back(value);
    }
  }
  for (const auto& point : source.pitchAutomation.points()) {
    if (point.tick <= splitTick) {
      left.pitchAutomation.points().push_back(point);
    } else {
      auto copied = point;
      copied.tick -= splitTick;
      right.pitchAutomation.points().push_back(std::move(copied));
    }
  }
  left.sortNotes();
  right.sortNotes();
  const auto leftValidation = left.validate();
  if (!leftValidation) return core::Result<domain::VocalRegion>{leftValidation.error()};
  const auto rightValidation = right.validate();
  if (!rightValidation) return core::Result<domain::VocalRegion>{rightValidation.error()};
  return core::success(std::move(right));
}

}

CommandImpact AddVocalTrackCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::ProjectAudio,
      .projectWide = false,
      .trackIds = {track_.id},
      .regionIds = {},
      .noteIds = {},
      .lyricIds = {},
  };
}

CommandImpact RemoveVocalTrackCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::ProjectAudio,
      .projectWide = false,
      .trackIds = {trackId_},
      .regionIds = {},
      .noteIds = {},
      .lyricIds = {},
  };
}

CommandImpact DuplicateVocalTrackCommand::impact() const {
  CommandImpact result{
      .scope = CommandAudioImpact::ProjectAudio,
      .projectWide = false,
      .trackIds = {duplicated_.has_value() ? duplicated_->id : sourceTrackId_},
      .regionIds = {},
      .noteIds = {},
      .lyricIds = {},
  };
  if (duplicated_.has_value()) {
    result.regionIds.reserve(duplicated_->regions.size());
    for (const auto& region : duplicated_->regions) {
      result.regionIds.push_back(region.id);
    }
  }
  return result;
}

CommandImpact MoveVocalTrackCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::MetadataOnly,
      .projectWide = false,
      .trackIds = {trackId_},
      .regionIds = {},
      .noteIds = {},
      .lyricIds = {},
  };
}

CommandImpact RenameVocalTrackCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::MetadataOnly,
      .projectWide = false,
      .trackIds = {trackId_},
      .regionIds = {},
      .noteIds = {},
      .lyricIds = {},
  };
}

CommandImpact AddAudioTrackCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::ProjectAudio,
      .projectWide = false,
      .trackIds = {track_.id},
      .regionIds = {},
      .noteIds = {},
      .lyricIds = {},
  };
}

CommandImpact RemoveAudioTrackCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::ProjectAudio,
      .projectWide = false,
      .trackIds = {trackId_},
      .regionIds = {},
      .noteIds = {},
      .lyricIds = {},
  };
}

CommandImpact RenameAudioTrackCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::MetadataOnly,
      .projectWide = false,
      .trackIds = {trackId_},
      .regionIds = {},
      .noteIds = {},
      .lyricIds = {},
  };
}

CommandImpact MoveAudioTrackCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::MetadataOnly,
      .projectWide = false,
      .trackIds = {trackId_},
      .regionIds = {},
      .noteIds = {},
      .lyricIds = {},
  };
}

CommandImpact ReplaceAudioTrackCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::ProjectAudio,
      .projectWide = false,
      .trackIds = {trackId_},
      .regionIds = {},
      .noteIds = {},
      .lyricIds = {},
  };
}

CommandImpact AddVocalRegionCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::ProjectAudio,
      .projectWide = false,
      .trackIds = {trackId_},
      .regionIds = {region_.id},
      .noteIds = {},
      .lyricIds = {},
  };
}

CommandImpact RemoveVocalRegionCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::ProjectAudio,
      .projectWide = false,
      .trackIds = {trackId_},
      .regionIds = {regionId_},
      .noteIds = {},
      .lyricIds = {},
  };
}

CommandImpact DuplicateVocalRegionCommand::impact() const {
  CommandImpact result{
      .scope = CommandAudioImpact::ProjectAudio,
      .projectWide = false,
      .trackIds = {trackId_},
      .regionIds = {region_.has_value() ? region_->id : sourceRegionId_},
      .noteIds = {},
      .lyricIds = {},
  };
  if (sourceTrackId_.valid() && sourceTrackId_ != trackId_) {
    result.trackIds.push_back(sourceTrackId_);
  }
  return result;
}

CommandImpact MoveVocalRegionCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::ProjectAudio,
      .projectWide = false,
      .trackIds = {trackId_},
      .regionIds = {regionId_},
      .noteIds = {},
      .lyricIds = {},
  };
}

CommandImpact ResizeVocalRegionCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::ProjectAudio,
      .projectWide = false,
      .trackIds = {trackId_},
      .regionIds = {regionId_},
      .noteIds = {},
      .lyricIds = {},
  };
}

CommandImpact RenameVocalRegionCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::MetadataOnly,
      .projectWide = false,
      .trackIds = {trackId_},
      .regionIds = {regionId_},
      .noteIds = {},
      .lyricIds = {},
  };
}

CommandImpact SplitVocalRegionCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::ProjectAudio,
      .projectWide = false,
      .trackIds = {trackId_},
      .regionIds = {regionId_, splitRegionId()},
      .noteIds = {},
      .lyricIds = {},
  };
}

core::Result<void> AddVocalTrackCommand::apply(domain::Project& project) {
  if (!track_.id.valid() || track_.name.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Vocal track requires a valid ID and name");
  }
  if (project.findVocalTrack(track_.id) != nullptr) {
    return core::failure(core::ErrorCode::Conflict,
                         "Vocal track ID already exists", track_.id.toString());
  }
  for (const auto& track : project.vocalTracks()) {
    if (track.name == track_.name) {
      return core::failure(core::ErrorCode::Conflict,
                           "Vocal track name already exists", track_.name);
    }
  }
  project.vocalTracks().push_back(track_);
  return core::success();
}

core::Result<void> AddVocalTrackCommand::revert(domain::Project& project) {
  const auto iterator = std::find_if(
      project.vocalTracks().begin(), project.vocalTracks().end(),
      [this](const auto& track) { return track.id == track_.id; });
  if (iterator == project.vocalTracks().end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Vocal track to undo was not found");
  }
  project.vocalTracks().erase(iterator);
  return core::success();
}

core::Result<void> RemoveVocalTrackCommand::apply(domain::Project& project) {
  const auto iterator = std::find_if(
      project.vocalTracks().begin(), project.vocalTracks().end(),
      [this](const auto& track) { return track.id == trackId_; });
  if (iterator == project.vocalTracks().end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Vocal track to remove was not found");
  }
  originalIndex_ = static_cast<std::size_t>(
      std::distance(project.vocalTracks().begin(), iterator));
  removed_ = *iterator;
  project.vocalTracks().erase(iterator);
  return core::success();
}

core::Result<void> RemoveVocalTrackCommand::revert(domain::Project& project) {
  if (!removed_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Vocal track removal has no captured state");
  }
  if (project.findVocalTrack(trackId_) != nullptr) {
    return core::failure(core::ErrorCode::Conflict,
                         "Vocal track already exists during undo");
  }
  const auto index = std::min(originalIndex_, project.vocalTracks().size());
  project.vocalTracks().insert(project.vocalTracks().begin() +
                                   static_cast<std::ptrdiff_t>(index),
                               *removed_);
  return core::success();
}

core::Result<void> DuplicateVocalTrackCommand::apply(domain::Project& project) {
  auto* source = project.findVocalTrack(sourceTrackId_);
  if (source == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Vocal track to duplicate was not found");
  }
  if (!duplicated_.has_value()) {
    const auto validation = source->id.valid()
                                ? core::success()
                                : core::failure(core::ErrorCode::InvalidArgument,
                                                "Vocal track ID is invalid");
    if (!validation) return validation;
    for (const auto& region : source->regions) {
      const auto regionValidation = region.validate();
      if (!regionValidation) return regionValidation;
    }
    if (sourceSnapshot_.has_value()) {
      sourceSnapshot_ = *source;
    }
    FreshIdAllocator ids{maximumId(project) + 1U};
    auto copied = cloneTrackWithFreshIds(*source, ids);
    if (!copied) return core::Result<void>{copied.error()};
    duplicated_ = std::move(copied).value();
  }
  if (project.findVocalTrack(duplicated_->id) != nullptr) {
    return core::failure(core::ErrorCode::Conflict,
                         "Duplicated vocal track ID already exists",
                         duplicated_->id.toString());
  }
  originalIndex_ = project.vocalTracks().size();
  project.vocalTracks().push_back(*duplicated_);
  return core::success();
}

core::Result<void> MoveVocalTrackCommand::apply(domain::Project& project) {
  auto result = moveTrack(project.vocalTracks(), trackId_, destinationIndex_,
                          captured_ ? nullptr : &previousIndex_);
  if (result) captured_ = true;
  return result;
}

core::Result<void> MoveVocalTrackCommand::revert(domain::Project& project) {
  if (!captured_) {
    return core::failure(core::ErrorCode::Conflict,
                         "Vocal track reorder has no captured state");
  }
  return moveTrack(project.vocalTracks(), trackId_, previousIndex_);
}

core::Result<void> DuplicateVocalTrackCommand::revert(domain::Project& project) {
  if (!duplicated_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Vocal track duplicate has no captured state");
  }
  const auto iterator = std::find_if(
      project.vocalTracks().begin(), project.vocalTracks().end(),
      [this](const auto& track) { return track.id == duplicated_->id; });
  if (iterator == project.vocalTracks().end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Duplicated vocal track was not found");
  }
  project.vocalTracks().erase(iterator);
  return core::success();
}

core::Result<void> RenameVocalTrackCommand::apply(domain::Project& project) {
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr || name_.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Renamed vocal track is invalid");
  }
  if (!previous_.has_value()) previous_ = track->name;
  track->name = name_;
  return core::success();
}

core::Result<void> RenameVocalTrackCommand::revert(domain::Project& project) {
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr || !previous_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Vocal track rename has no captured state");
  }
  track->name = *previous_;
  return core::success();
}

core::Result<void> AddAudioTrackCommand::apply(domain::Project& project) {
  if (!track_.id.valid() || track_.name.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Audio track requires a valid ID and name");
  }
  for (const auto& track : project.vocalTracks()) {
    if (track.id == track_.id) {
      return core::failure(core::ErrorCode::Conflict,
                           "Audio track ID conflicts with a vocal track",
                           track_.id.toString());
    }
  }
  for (const auto& track : project.audioTracks()) {
    if (track.id == track_.id) {
      return core::failure(core::ErrorCode::Conflict,
                           "Audio track ID already exists",
                           track_.id.toString());
    }
  }
  project.audioTracks().push_back(track_);
  return core::success();
}

core::Result<void> AddAudioTrackCommand::revert(domain::Project& project) {
  const auto iterator = std::find_if(
      project.audioTracks().begin(), project.audioTracks().end(),
      [this](const auto& track) { return track.id == track_.id; });
  if (iterator == project.audioTracks().end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Audio track to undo was not found");
  }
  project.audioTracks().erase(iterator);
  return core::success();
}

core::Result<void> RemoveAudioTrackCommand::apply(domain::Project& project) {
  const auto iterator = std::find_if(
      project.audioTracks().begin(), project.audioTracks().end(),
      [this](const auto& track) { return track.id == trackId_; });
  if (iterator == project.audioTracks().end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Audio track to remove was not found");
  }
  originalIndex_ = static_cast<std::size_t>(
      std::distance(project.audioTracks().begin(), iterator));
  removed_ = *iterator;
  project.audioTracks().erase(iterator);
  return core::success();
}

core::Result<void> RemoveAudioTrackCommand::revert(domain::Project& project) {
  if (!removed_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Audio track removal has no captured state");
  }
  if (std::any_of(project.audioTracks().begin(), project.audioTracks().end(),
                  [this](const auto& track) { return track.id == trackId_; })) {
    return core::failure(core::ErrorCode::Conflict,
                         "Audio track already exists during undo");
  }
  const auto index = std::min(originalIndex_, project.audioTracks().size());
  project.audioTracks().insert(
      project.audioTracks().begin() + static_cast<std::ptrdiff_t>(index),
      *removed_);
  return core::success();
}

core::Result<void> RenameAudioTrackCommand::apply(domain::Project& project) {
  if (!trackId_.valid() || name_.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Renamed audio track is invalid");
  }
  const auto iterator = std::find_if(
      project.audioTracks().begin(), project.audioTracks().end(),
      [this](const auto& track) { return track.id == trackId_; });
  if (iterator == project.audioTracks().end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Audio track to rename was not found");
  }
  if (!previous_.has_value()) previous_ = iterator->name;
  iterator->name = name_;
  return core::success();
}

core::Result<void> RenameAudioTrackCommand::revert(domain::Project& project) {
  const auto iterator = std::find_if(
      project.audioTracks().begin(), project.audioTracks().end(),
      [this](const auto& track) { return track.id == trackId_; });
  if (iterator == project.audioTracks().end() || !previous_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Audio track rename has no captured state");
  }
  iterator->name = *previous_;
  return core::success();
}

core::Result<void> MoveAudioTrackCommand::apply(domain::Project& project) {
  auto result = moveTrack(project.audioTracks(), trackId_, destinationIndex_,
                          captured_ ? nullptr : &previousIndex_);
  if (result) captured_ = true;
  return result;
}

core::Result<void> MoveAudioTrackCommand::revert(domain::Project& project) {
  if (!captured_) {
    return core::failure(core::ErrorCode::Conflict,
                         "Audio track reorder has no captured state");
  }
  return moveTrack(project.audioTracks(), trackId_, previousIndex_);
}

core::Result<void> ReplaceAudioTrackCommand::apply(domain::Project& project) {
  if (!trackId_.valid() || replacement_.id != trackId_ ||
      replacement_.name.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Replacement audio track identity is invalid");
  }
  const auto iterator = std::find_if(
      project.audioTracks().begin(), project.audioTracks().end(),
      [this](const auto& track) { return track.id == trackId_; });
  if (iterator == project.audioTracks().end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Audio track to replace was not found");
  }
  if (!previous_.has_value()) previous_ = *iterator;
  *iterator = replacement_;
  return core::success();
}

core::Result<void> ReplaceAudioTrackCommand::revert(domain::Project& project) {
  if (!previous_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Audio track replacement has no captured state");
  }
  const auto iterator = std::find_if(
      project.audioTracks().begin(), project.audioTracks().end(),
      [this](const auto& track) { return track.id == trackId_; });
  if (iterator == project.audioTracks().end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Audio track to restore was not found");
  }
  *iterator = *previous_;
  return core::success();
}

core::Result<void> AddVocalRegionCommand::apply(domain::Project& project) {
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr || !region_.id.valid()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Target vocal track or region is missing");
  }
  if (project.findRegion(region_.id) != nullptr) {
    return core::failure(core::ErrorCode::Conflict,
                         "Vocal region ID already exists", region_.id.toString());
  }
  const auto validation = region_.validate();
  if (!validation) return validation;
  track->regions.push_back(region_);
  return core::success();
}

core::Result<void> AddVocalRegionCommand::revert(domain::Project& project) {
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr) return core::failure(core::ErrorCode::NotFound, "Vocal track is missing");
  const auto iterator = std::find_if(
      track->regions.begin(), track->regions.end(),
      [this](const auto& region) { return region.id == region_.id; });
  if (iterator == track->regions.end()) return core::failure(core::ErrorCode::NotFound, "Vocal region is missing");
  track->regions.erase(iterator);
  return core::success();
}

core::Result<void> RemoveVocalRegionCommand::apply(domain::Project& project) {
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr) return core::failure(core::ErrorCode::NotFound, "Vocal track is missing");
  const auto iterator = std::find_if(
      track->regions.begin(), track->regions.end(),
      [this](const auto& region) { return region.id == regionId_; });
  if (iterator == track->regions.end()) return core::failure(core::ErrorCode::NotFound, "Vocal region is missing");
  originalIndex_ = static_cast<std::size_t>(std::distance(track->regions.begin(), iterator));
  removed_ = *iterator;
  track->regions.erase(iterator);
  return core::success();
}

core::Result<void> RemoveVocalRegionCommand::revert(domain::Project& project) {
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr || !removed_.has_value()) return core::failure(core::ErrorCode::Conflict, "Vocal region removal has no captured state");
  if (project.findRegion(regionId_) != nullptr) return core::failure(core::ErrorCode::Conflict, "Vocal region already exists during undo");
  const auto index = std::min(originalIndex_, track->regions.size());
  track->regions.insert(track->regions.begin() + static_cast<std::ptrdiff_t>(index), *removed_);
  return core::success();
}

core::Result<void> DuplicateVocalRegionCommand::apply(domain::Project& project) {
  auto* targetTrack = project.findVocalTrack(trackId_);
  auto* sourceTrack = project.findVocalTrack(sourceTrackId_);
  if (targetTrack == nullptr || sourceTrack == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Source or target vocal track was not found");
  }
  if (!region_.has_value()) {
    const auto* source = sourceTrack->findRegion(sourceRegionId_);
    if (source == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Vocal region to duplicate was not found");
    }
    FreshIdAllocator ids{maximumId(project) + 1U};
    auto copied = cloneRegionWithFreshIds(*source, ids);
    if (!copied) return core::Result<void>{copied.error()};
    region_ = std::move(copied).value();
  }
  if (!region_->id.valid()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Duplicated vocal region requires a valid ID");
  }
  if (project.findRegion(region_->id) != nullptr) {
    return core::failure(core::ErrorCode::Conflict,
                         "Duplicated vocal region ID already exists",
                         region_->id.toString());
  }
  const auto validation = region_->validate();
  if (!validation) return validation;
  for (const auto& note : region_->notes) {
    if (project.findNote(note.id) != nullptr) {
      return core::failure(core::ErrorCode::Conflict,
                           "Duplicated vocal region note ID already exists",
                           note.id.toString());
    }
  }
  for (const auto& lyric : region_->lyrics) {
    if (containsLyricId(project, lyric.id)) {
      return core::failure(core::ErrorCode::Conflict,
                           "Duplicated vocal region lyric ID already exists",
                           lyric.id.toString());
    }
  }
  targetTrack->regions.push_back(*region_);
  return core::success();
}

core::Result<void> DuplicateVocalRegionCommand::revert(domain::Project& project) {
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Target vocal track was not found");
  }
  const auto iterator = std::find_if(
      track->regions.begin(), track->regions.end(),
      [this](const auto& region) {
        return region_.has_value() && region.id == region_->id;
      });
  if (iterator == track->regions.end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Duplicated vocal region was not found");
  }
  track->regions.erase(iterator);
  return core::success();
}

core::Result<void> MoveVocalRegionCommand::apply(domain::Project& project) {
  if (newStart_ < time::Tick{0}) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Vocal region start cannot be negative");
  }
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Target vocal track was not found");
  }
  auto* region = track->findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Vocal region to move was not found");
  }
  const auto before = region->startTick;
  if (!previousStart_.has_value()) previousStart_ = before;
  region->startTick = newStart_;
  const auto validation = region->validate();
  if (!validation) {
    region->startTick = before;
    return validation;
  }
  return core::success();
}

core::Result<void> MoveVocalRegionCommand::revert(domain::Project& project) {
  if (!previousStart_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Vocal region move has no captured state");
  }
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Target vocal track was not found");
  }
  auto* region = track->findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Vocal region to move was not found");
  }
  const auto before = region->startTick;
  region->startTick = *previousStart_;
  const auto validation = region->validate();
  if (!validation) {
    region->startTick = before;
    return validation;
  }
  return core::success();
}

core::Result<void> ResizeVocalRegionCommand::apply(domain::Project& project) {
  if (newDuration_ <= time::Tick{0}) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Vocal region duration must be positive");
  }
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Target vocal track was not found");
  }
  auto* region = track->findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Vocal region to resize was not found");
  }
  const auto before = region->durationTick;
  if (!previousDuration_.has_value()) previousDuration_ = before;
  region->durationTick = newDuration_;
  const auto validation = region->validate();
  if (!validation) {
    region->durationTick = before;
    return validation;
  }
  return core::success();
}

core::Result<void> ResizeVocalRegionCommand::revert(domain::Project& project) {
  if (!previousDuration_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Vocal region resize has no captured state");
  }
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Target vocal track was not found");
  }
  auto* region = track->findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Vocal region to resize was not found");
  }
  const auto before = region->durationTick;
  region->durationTick = *previousDuration_;
  const auto validation = region->validate();
  if (!validation) {
    region->durationTick = before;
    return validation;
  }
  return core::success();
}

core::Result<void> RenameVocalRegionCommand::apply(domain::Project& project) {
  if (name_.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Renamed vocal region must have a name");
  }
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Target vocal track was not found");
  }
  auto* region = track->findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Vocal region to rename was not found");
  }
  if (!previous_.has_value()) previous_ = region->name;
  region->name = name_;
  return core::success();
}

core::Result<void> RenameVocalRegionCommand::revert(domain::Project& project) {
  auto* track = project.findVocalTrack(trackId_);
  auto* region = track == nullptr ? nullptr : track->findRegion(regionId_);
  if (region == nullptr || !previous_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Vocal region rename has no captured state");
  }
  region->name = *previous_;
  return core::success();
}

core::Result<void> SplitVocalRegionCommand::apply(domain::Project& project) {
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Target vocal track was not found");
  }
  auto* source = track->findRegion(regionId_);
  if (source == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Vocal region to split was not found");
  }
  if (!original_.has_value()) {
    original_ = *source;
    left_.emplace();
    FreshIdAllocator ids{maximumId(project) + 1U};
    auto right = makeSplitRightRegion(*source, splitTick_, ids, *left_);
    if (!right) {
      original_.reset();
      left_.reset();
      return core::Result<void>{right.error()};
    }
    right_ = std::move(right).value();
  }
  if (right_.has_value() && project.findRegion(right_->id) != nullptr) {
    return core::failure(core::ErrorCode::Conflict,
                         "Split region already exists", right_->id.toString());
  }
  *source = *left_;
  track->regions.push_back(*right_);
  return core::success();
}

core::Result<void> SplitVocalRegionCommand::revert(domain::Project& project) {
  if (!original_.has_value() || !right_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Region split has no captured state");
  }
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Target vocal track was not found");
  }
  auto* source = track->findRegion(regionId_);
  const auto rightIterator = std::find_if(
      track->regions.begin(), track->regions.end(), [this](const auto& region) {
        return region.id == right_->id;
      });
  if (source == nullptr || rightIterator == track->regions.end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Split region state is missing during undo");
  }
  *source = *original_;
  track->regions.erase(rightIterator);
  return core::success();
}

}
