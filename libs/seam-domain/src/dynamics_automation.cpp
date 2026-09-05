#include "seam/domain/dynamics_automation.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <utility>

namespace seam::domain {
namespace {

core::Result<void> validatePoints(std::span<const DynamicsAutomationPoint> points) {
  if (points.size() > kMaximumDynamicsPoints) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Dynamics curve exceeds the supported point count");
  }
  std::optional<time::Tick> previous;
  for (const auto& point : points) {
    const auto valid = point.validate();
    if (!valid) return valid;
    if (previous.has_value() && point.tick <= *previous) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Dynamics points must be strictly ordered and unique");
    }
    previous = point.tick;
  }
  return core::success();
}

}

core::Result<void> DynamicsAutomationPoint::validate() const {
  if (tick < time::Tick{0} || !std::isfinite(linearGain) ||
      linearGain < 0.0F || linearGain > kMaximumDynamicsGain) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Dynamics requires nonnegative time and finite gain from silence to +12 dB");
  }
  return core::success();
}

core::Result<void> DynamicsAutomation::validate() const {
  return validatePoints(points_);
}

core::Result<void> DynamicsAutomation::replacePoints(
    std::vector<DynamicsAutomationPoint> points) {
  const auto valid = validatePoints(points);
  if (!valid) return valid;
  points_ = std::move(points);
  return core::success();
}

core::Result<void> DynamicsAutomation::upsert(DynamicsAutomationPoint point) {
  const auto valid = point.validate();
  if (!valid) return valid;
  const auto iterator = std::lower_bound(
      points_.begin(), points_.end(), point.tick,
      [](const DynamicsAutomationPoint& candidate, time::Tick tick) {
        return candidate.tick < tick;
      });
  if (iterator != points_.end() && iterator->tick == point.tick) {
    *iterator = point;
  } else {
    if (points_.size() >= kMaximumDynamicsPoints) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Dynamics curve exceeds the supported point count");
    }
    points_.insert(iterator, point);
  }
  return core::success();
}

bool DynamicsAutomation::erase(time::Tick tick) noexcept {
  const auto iterator = std::lower_bound(
      points_.begin(), points_.end(), tick,
      [](const DynamicsAutomationPoint& point, time::Tick value) {
        return point.tick < value;
      });
  if (iterator == points_.end() || iterator->tick != tick) return false;
  points_.erase(iterator);
  return true;
}

float DynamicsAutomation::valueAt(time::Tick tick) const noexcept {
  if (points_.empty()) return 1.0F;
  if (tick <= points_.front().tick) return points_.front().linearGain;
  if (tick >= points_.back().tick) return points_.back().linearGain;
  const auto right = std::upper_bound(
      points_.begin(), points_.end(), tick,
      [](time::Tick value, const DynamicsAutomationPoint& point) {
        return value < point.tick;
      });
  const auto left = right - 1;
  const auto position = static_cast<float>(
      static_cast<double>((tick - left->tick).value()) /
      static_cast<double>((right->tick - left->tick).value()));
  return std::lerp(left->linearGain, right->linearGain, position);
}

}
