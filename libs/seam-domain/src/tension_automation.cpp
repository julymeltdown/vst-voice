#include "seam/domain/tension_automation.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <utility>

namespace seam::domain {
namespace {

core::Result<void> validatePoints(std::span<const TensionAutomationPoint> points) {
  if (points.size() > kMaximumTensionPoints) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Tension curve exceeds the supported point count");
  }
  std::optional<time::Tick> previous;
  for (const auto& point : points) {
    const auto valid = point.validate();
    if (!valid) return valid;
    if (previous.has_value() && point.tick <= *previous) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Tension points must be strictly ordered and unique");
    }
    previous = point.tick;
  }
  return core::success();
}

}  // namespace

core::Result<void> TensionAutomationPoint::validate() const {
  if (tick < time::Tick{0} || !std::isfinite(amount) || amount < 0.0F ||
      amount > kMaximumTension) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Tension requires nonnegative time and a finite amount between zero and one");
  }
  return core::success();
}

core::Result<void> TensionAutomation::validate() const {
  return validatePoints(points_);
}

core::Result<void> TensionAutomation::replacePoints(
    std::vector<TensionAutomationPoint> points) {
  const auto valid = validatePoints(points);
  if (!valid) return valid;
  points_ = std::move(points);
  return core::success();
}

core::Result<void> TensionAutomation::upsert(TensionAutomationPoint point) {
  const auto valid = point.validate();
  if (!valid) return valid;
  const auto iterator = std::lower_bound(
      points_.begin(), points_.end(), point.tick,
      [](const TensionAutomationPoint& candidate, time::Tick tick) {
        return candidate.tick < tick;
      });
  if (iterator != points_.end() && iterator->tick == point.tick) {
    *iterator = point;
  } else {
    if (points_.size() >= kMaximumTensionPoints) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Tension curve exceeds the supported point count");
    }
    points_.insert(iterator, point);
  }
  return core::success();
}

bool TensionAutomation::erase(time::Tick tick) noexcept {
  const auto iterator = std::lower_bound(
      points_.begin(), points_.end(), tick,
      [](const TensionAutomationPoint& point, time::Tick value) {
        return point.tick < value;
      });
  if (iterator == points_.end() || iterator->tick != tick) return false;
  points_.erase(iterator);
  return true;
}

float TensionAutomation::valueAt(time::Tick tick) const noexcept {
  if (points_.empty()) return 0.0F;
  if (tick <= points_.front().tick) return points_.front().amount;
  if (tick >= points_.back().tick) return points_.back().amount;
  const auto right = std::upper_bound(
      points_.begin(), points_.end(), tick,
      [](time::Tick value, const TensionAutomationPoint& point) {
        return value < point.tick;
      });
  const auto left = right - 1;
  const auto position = static_cast<float>(
      static_cast<double>((tick - left->tick).value()) /
      static_cast<double>((right->tick - left->tick).value()));
  const auto span = right->amount - left->amount;
  return static_cast<float>(static_cast<double>(left->amount) +
                            static_cast<double>(span) * static_cast<double>(position));
}

}  // namespace seam::domain
