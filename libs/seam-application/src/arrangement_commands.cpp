#include "seam/application/arrangement_commands.hpp"

#include <algorithm>

namespace seam::application {

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

}
