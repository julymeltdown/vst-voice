#include "seam/native_ui/arrangement_panel.hpp"

#include <algorithm>
#include <utility>

namespace seam::native_ui {

void ArrangementPanelModel::rebuild(const domain::Project& project,
                                    domain::TrackId selectedTrack,
                                    domain::RegionId selectedRegion) {
  tracks_.clear();
  tracks_.reserve(project.vocalTracks().size() + project.audioTracks().size());
  selectedTrack_ = selectedTrack;
  selectedRegion_ = selectedRegion;
  for (const auto& track : project.vocalTracks()) {
    ArrangementTrackItem item{
        .id = track.id,
        .name = track.name,
        .vocal = true,
        .selected = track.id == selectedTrack_,
        .regions = {},
    };
    item.regions.reserve(track.regions.size());
    for (const auto& region : track.regions) {
      item.regions.push_back(ArrangementRegionItem{
          .id = region.id,
          .name = region.name,
          .startTick = region.startTick,
          .durationTick = region.durationTick,
          .noteCount = region.notes.size(),
          .selected = region.id == selectedRegion_,
      });
    }
    tracks_.push_back(std::move(item));
  }
  for (const auto& track : project.audioTracks()) {
    tracks_.push_back(ArrangementTrackItem{
        .id = track.id,
        .name = track.name,
        .vocal = false,
        .selected = track.id == selectedTrack_,
        .regions = {},
    });
  }
}

core::Result<void> ArrangementPanelModel::selectTrack(
    const domain::Project& project, domain::TrackId trackId) {
  const auto vocal = project.findVocalTrack(trackId);
  const auto audio = std::find_if(
      project.audioTracks().begin(), project.audioTracks().end(),
      [trackId](const auto& track) { return track.id == trackId; });
  if (vocal == nullptr && audio == project.audioTracks().end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Arrangement track was not found");
  }
  selectedTrack_ = trackId;
  if (vocal == nullptr || vocal->findRegion(selectedRegion_) == nullptr) {
    selectedRegion_ = vocal == nullptr || vocal->regions.empty()
                          ? domain::RegionId{}
                          : vocal->regions.front().id;
  }
  rebuild(project, selectedTrack_, selectedRegion_);
  return core::success();
}

core::Result<void> ArrangementPanelModel::selectRegion(
    const domain::Project& project, domain::RegionId regionId) {
  const auto* region = project.findRegion(regionId);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Arrangement region was not found");
  }
  const auto owner = std::find_if(
      project.vocalTracks().begin(), project.vocalTracks().end(),
      [regionId](const auto& track) {
        return track.findRegion(regionId) != nullptr;
      });
  if (owner == project.vocalTracks().end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Arrangement region has no owning track");
  }
  selectedTrack_ = owner->id;
  selectedRegion_ = regionId;
  rebuild(project, selectedTrack_, selectedRegion_);
  return core::success();
}

}
