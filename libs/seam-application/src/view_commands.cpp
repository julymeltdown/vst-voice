#include "seam/application/view_commands.hpp"

#include <cmath>

namespace seam::application {
namespace {

std::size_t laneIndex(domain::TechnicalLane lane) noexcept {
  return static_cast<std::size_t>(lane);
}

core::Result<void> validate(domain::TechnicalLanePresentation presentation) {
  if (!std::isfinite(presentation.expandedHeight) ||
      presentation.expandedHeight < 96.0 || presentation.expandedHeight > 640.0) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Technical lane height is outside the supported range");
  }
  return core::success();
}

}

core::Result<void> SetTechnicalLanePresentationCommand::apply(
    domain::Project& project) {
  const auto validation = validate(after_);
  if (!validation) return validation;
  auto& value = project.settings().technicalLanes[laneIndex(lane_)];
  if (!before_.has_value()) before_ = value;
  value = after_;
  return core::success();
}

core::Result<void> SetTechnicalLanePresentationCommand::revert(
    domain::Project& project) {
  if (!before_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Technical lane presentation command has no prior state");
  }
  project.settings().technicalLanes[laneIndex(lane_)] = *before_;
  return core::success();
}

}
