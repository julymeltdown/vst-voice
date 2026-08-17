#include "seam/application/render_commands.hpp"

#include <algorithm>
#include <cmath>

namespace seam::application {

UpsertUnitSelectionOverrideCommand::UpsertUnitSelectionOverrideCommand(
    domain::RegionId regionId,
    domain::UnitSelectionOverride overrideValue)
    : regionId_(regionId), after_(std::move(overrideValue)) {}

core::Result<void> UpsertUnitSelectionOverrideCommand::apply(
    domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for unit selection override was not found",
                         regionId_.toString());
  }
  const auto validation = after_.validate();
  if (!validation) return validation;
  if (region->findNote(after_.startKey.noteId) == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Unit selection override references a missing note",
                         after_.startKey.toString());
  }
  auto* existing = region->findUnitSelectionOverride(after_.startKey);
  if (!captured_) {
    if (existing != nullptr) before_ = *existing;
    captured_ = true;
  }
  if (existing != nullptr) {
    *existing = after_;
  } else {
    region->unitSelectionOverrides.push_back(after_);
    std::stable_sort(region->unitSelectionOverrides.begin(),
                     region->unitSelectionOverrides.end(),
        [](const auto& lhs, const auto& rhs) {
          return lhs.startKey < rhs.startKey;
        });
  }
  return core::success();
}

core::Result<void> UpsertUnitSelectionOverrideCommand::revert(
    domain::Project& project) {
  if (!captured_) {
    return core::failure(core::ErrorCode::Conflict,
                         "Unit selection command has no captured state");
  }
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for unit selection override was not found during undo",
                         regionId_.toString());
  }
  auto* existing = region->findUnitSelectionOverride(after_.startKey);
  if (before_.has_value()) {
    if (existing != nullptr) {
      *existing = *before_;
    } else {
      region->unitSelectionOverrides.push_back(*before_);
    }
  } else {
    std::erase_if(region->unitSelectionOverrides,
                  [this](const auto& value) {
                    return value.startKey == after_.startKey;
                  });
  }
  return core::success();
}

core::Result<void> RemoveUnitSelectionOverrideCommand::apply(
    domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for unit selection override was not found",
                         regionId_.toString());
  }
  const auto iterator = std::find_if(
      region->unitSelectionOverrides.begin(), region->unitSelectionOverrides.end(),
      [this](const auto& value) { return value.startKey == startKey_; });
  if (iterator == region->unitSelectionOverrides.end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Unit selection override was not found",
                         startKey_.toString());
  }
  removed_ = *iterator;
  region->unitSelectionOverrides.erase(iterator);
  return core::success();
}

core::Result<void> RemoveUnitSelectionOverrideCommand::revert(
    domain::Project& project) {
  if (!removed_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No unit selection override was removed");
  }
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for unit selection override was not found during undo",
                         regionId_.toString());
  }
  if (region->findUnitSelectionOverride(startKey_) != nullptr) {
    return core::failure(core::ErrorCode::Conflict,
                         "Unit selection override already exists during undo",
                         startKey_.toString());
  }
  region->unitSelectionOverrides.push_back(*removed_);
  std::stable_sort(region->unitSelectionOverrides.begin(),
                   region->unitSelectionOverrides.end(),
      [](const auto& lhs, const auto& rhs) {
        return lhs.startKey < rhs.startKey;
      });
  return core::success();
}

UpsertSeamOverrideCommand::UpsertSeamOverrideCommand(
    domain::RegionId regionId,
    domain::SeamOverride overrideValue)
    : regionId_(regionId), after_(std::move(overrideValue)) {}

core::Result<void> UpsertSeamOverrideCommand::apply(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for seam override was not found",
                         regionId_.toString());
  }
  const auto validation = after_.validate();
  if (!validation) return validation;
  if (region->findNote(after_.incomingStartKey.noteId) == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Seam override references a missing note",
                         after_.incomingStartKey.toString());
  }
  auto* existing = region->findSeamOverride(after_.incomingStartKey);
  if (!captured_) {
    if (existing != nullptr) before_ = *existing;
    captured_ = true;
  }
  if (existing != nullptr) {
    *existing = after_;
  } else {
    region->seamOverrides.push_back(after_);
    std::stable_sort(region->seamOverrides.begin(), region->seamOverrides.end(),
        [](const auto& lhs, const auto& rhs) {
          return lhs.incomingStartKey < rhs.incomingStartKey;
        });
  }
  return core::success();
}

core::Result<void> UpsertSeamOverrideCommand::revert(domain::Project& project) {
  if (!captured_) {
    return core::failure(core::ErrorCode::Conflict,
                         "Seam command has no captured state");
  }
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for seam override was not found during undo",
                         regionId_.toString());
  }
  auto* existing = region->findSeamOverride(after_.incomingStartKey);
  if (before_.has_value()) {
    if (existing != nullptr) {
      *existing = *before_;
    } else {
      region->seamOverrides.push_back(*before_);
    }
  } else {
    std::erase_if(region->seamOverrides,
                  [this](const auto& value) {
                    return value.incomingStartKey == after_.incomingStartKey;
                  });
  }
  return core::success();
}

core::Result<void> RemoveSeamOverrideCommand::apply(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for seam override was not found",
                         regionId_.toString());
  }
  const auto iterator = std::find_if(
      region->seamOverrides.begin(), region->seamOverrides.end(),
      [this](const auto& value) {
        return value.incomingStartKey == incomingStartKey_;
      });
  if (iterator == region->seamOverrides.end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Seam override was not found",
                         incomingStartKey_.toString());
  }
  removed_ = *iterator;
  region->seamOverrides.erase(iterator);
  return core::success();
}

core::Result<void> RemoveSeamOverrideCommand::revert(domain::Project& project) {
  if (!removed_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No seam override was removed");
  }
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for seam override was not found during undo",
                         regionId_.toString());
  }
  if (region->findSeamOverride(incomingStartKey_) != nullptr) {
    return core::failure(core::ErrorCode::Conflict,
                         "Seam override already exists during undo",
                         incomingStartKey_.toString());
  }
  region->seamOverrides.push_back(*removed_);
  std::stable_sort(region->seamOverrides.begin(), region->seamOverrides.end(),
      [](const auto& lhs, const auto& rhs) {
        return lhs.incomingStartKey < rhs.incomingStartKey;
      });
  return core::success();
}


core::Result<void> UpsertPitchAutomationPointCommand::apply(
    domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for pitch automation was not found",
                         regionId_.toString());
  }
  const auto validation = after_.validate();
  if (!validation) return validation;
  if (after_.tick > region->durationTick) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Pitch point extends beyond its region");
  }
  if (!captured_) {
    const auto& points = region->pitchAutomation.points();
    const auto iterator = std::find_if(points.begin(), points.end(),
        [this](const auto& point) { return point.tick == after_.tick; });
    if (iterator != points.end()) before_ = *iterator;
    captured_ = true;
  }
  return region->pitchAutomation.upsert(after_);
}

core::Result<void> UpsertPitchAutomationPointCommand::revert(
    domain::Project& project) {
  if (!captured_) {
    return core::failure(core::ErrorCode::Conflict,
                         "Pitch automation command has no captured state");
  }
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for pitch automation was not found during undo",
                         regionId_.toString());
  }
  if (before_.has_value()) return region->pitchAutomation.upsert(*before_);
  static_cast<void>(region->pitchAutomation.erase(after_.tick));
  return core::success();
}

core::Result<void> RemovePitchAutomationPointCommand::apply(
    domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for pitch automation was not found",
                         regionId_.toString());
  }
  const auto& points = region->pitchAutomation.points();
  const auto iterator = std::find_if(points.begin(), points.end(),
      [this](const auto& point) { return point.tick == tick_; });
  if (iterator == points.end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Pitch automation point was not found");
  }
  removed_ = *iterator;
  static_cast<void>(region->pitchAutomation.erase(tick_));
  return core::success();
}

core::Result<void> RemovePitchAutomationPointCommand::revert(
    domain::Project& project) {
  if (!removed_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No pitch automation point was removed");
  }
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for pitch automation was not found during undo",
                         regionId_.toString());
  }
  return region->pitchAutomation.upsert(*removed_);
}


core::Result<void> SetTrackVoicebankCommand::apply(domain::Project& project) {
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Track for Voicebank selection was not found",
                         trackId_.toString());
  }
  if (after_.id.empty() || after_.version.empty() || after_.contentHash.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Voicebank selection requires ID, version, and content hash");
  }
  if (!before_.has_value()) before_ = track->voicebank;
  track->voicebank = after_;
  return core::success();
}

core::Result<void> SetTrackVoicebankCommand::revert(domain::Project& project) {
  if (!before_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Voicebank command has no captured state");
  }
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Track for Voicebank selection was not found during undo",
                         trackId_.toString());
  }
  track->voicebank = *before_;
  return core::success();
}

}  // namespace seam::application

namespace seam::application {

core::Result<void> SetVocalTrackMixCommand::apply(domain::Project& project) {
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Vocal track mix target is missing");
  }
  if (!std::isfinite(afterGainDb_) || !std::isfinite(afterPan_) ||
      afterGainDb_ < -120.0F || afterGainDb_ > 24.0F ||
      afterPan_ < -1.0F || afterPan_ > 1.0F) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Vocal track mix values are invalid");
  }
  if (!captured_) {
    beforeGainDb_ = track->gainDb;
    beforePan_ = track->pan;
    beforeMuted_ = track->muted;
    beforeSolo_ = track->solo;
    captured_ = true;
  }
  track->gainDb = afterGainDb_;
  track->pan = afterPan_;
  track->muted = afterMuted_;
  track->solo = afterSolo_;
  if (track->outputRoute.matrix.sourceChannels == 1U &&
      track->outputRoute.matrix.destinationChannels == 2U) {
    track->outputRoute.matrix = domain::RoutingMatrix::monoToStereo(afterPan_);
  }
  return core::success();
}

core::Result<void> SetVocalTrackMixCommand::revert(domain::Project& project) {
  if (!captured_) {
    return core::failure(core::ErrorCode::Conflict,
                         "Vocal track mix command has no captured state");
  }
  auto* track = project.findVocalTrack(trackId_);
  if (track == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Vocal track mix target is missing");
  }
  track->gainDb = beforeGainDb_;
  track->pan = beforePan_;
  track->muted = beforeMuted_;
  track->solo = beforeSolo_;
  if (track->outputRoute.matrix.sourceChannels == 1U &&
      track->outputRoute.matrix.destinationChannels == 2U) {
    track->outputRoute.matrix = domain::RoutingMatrix::monoToStereo(beforePan_);
  }
  return core::success();
}

core::Result<void> SetProjectRoutingCommand::apply(domain::Project& project) {
  const auto validation = after_.validate();
  if (!validation) return validation;
  if (!before_.has_value()) before_ = project.routing();
  project.routing() = after_;
  return core::success();
}

core::Result<void> SetProjectRoutingCommand::revert(domain::Project& project) {
  if (!before_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Project routing command has no captured state");
  }
  project.routing() = *before_;
  return core::success();
}

core::Result<void> SetHostStartOffsetCommand::apply(domain::Project& project) {
  if (after_ < time::Tick{0}) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Host start offset tick cannot be negative");
  }
  if (!captured_) {
    before_ = project.settings().hostStartOffsetTick;
    captured_ = true;
  }
  project.settings().hostStartOffsetTick = after_;
  return core::success();
}

core::Result<void> SetHostStartOffsetCommand::revert(domain::Project& project) {
  if (!captured_) {
    return core::failure(core::ErrorCode::Conflict,
                         "Host start offset command has no captured state");
  }
  project.settings().hostStartOffsetTick = before_;
  return core::success();
}

core::Result<void> ConfigureProjectOutputCommand::apply(domain::Project& project) {
  if (channels_ == 0U || channels_ > domain::kMaximumAudioChannels) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Project output channel count must be between one and eight");
  }
  if (!beforeRouting_.has_value()) {
    beforeRouting_ = project.routing();
    beforeRoutes_.clear();
    beforeRoutes_.reserve(project.vocalTracks().size());
    for (const auto& track : project.vocalTracks()) {
      beforeRoutes_.push_back({track.id, track.outputRoute});
    }
  }
  domain::ProjectRouting routing;
  routing.deviceOutputChannels = channels_;
  routing.masterBus = domain::BusId{1U};
  routing.buses = {domain::AudioBus{.id = domain::BusId{1U},
                                    .name = "Master",
                                    .channelCount = channels_}};
  routing.sends.clear();
  routing.deviceRoutes = {domain::DeviceOutputRoute{
      .sourceBus = domain::BusId{1U},
      .matrix = domain::RoutingMatrix::identity(channels_),
      .gainDb = 0.0F,
      .enabled = true}};
  project.routing() = routing;
  for (auto& track : project.vocalTracks()) {
    domain::RoutingMatrix matrix;
    matrix.sourceChannels = 1U;
    matrix.destinationChannels = channels_;
    matrix.gains.assign(channels_, 0.0F);
    if (channels_ == 1U) {
      matrix.gains[0] = 1.0F;
    } else {
      const auto stereo = domain::RoutingMatrix::monoToStereo(track.pan);
      matrix.gains[0] = stereo.gain(0U, 0U);
      matrix.gains[1] = stereo.gain(1U, 0U);
      for (std::uint8_t channel = 2U; channel < channels_; ++channel) {
        matrix.gains[channel] = 0.35F;
      }
    }
    track.outputRoute = domain::TrackOutputRoute{
        .bus = routing.masterBus, .matrix = std::move(matrix)};
  }
  return core::success();
}

core::Result<void> ConfigureProjectOutputCommand::revert(domain::Project& project) {
  if (!beforeRouting_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Project output command has no captured state");
  }
  project.routing() = *beforeRouting_;
  for (const auto& [trackId, route] : beforeRoutes_) {
    if (auto* track = project.findVocalTrack(trackId)) track->outputRoute = route;
  }
  return core::success();
}

}  // namespace seam::application
