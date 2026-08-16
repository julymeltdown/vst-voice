#include "seam/application/render_commands.hpp"

#include <algorithm>

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

}  // namespace seam::application
