#include "seam/domain/gender_automation.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <utility>

namespace seam::domain {
namespace {

core::Result<void> validatePoints(std::span<const GenderAutomationPoint> points) {
  if (points.size() > kMaximumGenderPoints) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Gender curve exceeds the supported point count");
  }
  std::optional<time::Tick> previous;
  for (const auto& point : points) {
    const auto valid = point.validate();
    if (!valid) return valid;
    if (previous.has_value() && point.tick <= *previous) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Gender points must be strictly ordered and unique");
    }
    previous = point.tick;
  }
  return core::success();
}

}  // namespace

core::Result<void> GenderAutomationPoint::validate() const {
  if (tick < time::Tick{0} || !std::isfinite(amount) ||
      std::abs(amount) > kMaximumGender) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Gender requires nonnegative time and a finite value within its bipolar range");
  }
  return core::success();
}

core::Result<void> GenderAutomation::validate() const {
  return validatePoints(points_);
}

core::Result<void> GenderAutomation::replacePoints(
    std::vector<GenderAutomationPoint> points) {
  const auto valid = validatePoints(points);
  if (!valid) return valid;
  points_ = std::move(points);
  return core::success();
}

core::Result<void> GenderAutomation::upsert(GenderAutomationPoint point) {
  const auto valid = point.validate();
  if (!valid) return valid;
  const auto iterator = std::lower_bound(
      points_.begin(), points_.end(), point.tick,
      [](const GenderAutomationPoint& candidate, time::Tick tick) {
        return candidate.tick < tick;
      });
  if (iterator != points_.end() && iterator->tick == point.tick) {
    *iterator = point;
  } else {
    if (points_.size() >= kMaximumGenderPoints) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Gender curve exceeds the supported point count");
    }
    points_.insert(iterator, point);
  }
  return core::success();
}

bool GenderAutomation::erase(time::Tick tick) noexcept {
  const auto iterator = std::lower_bound(
      points_.begin(), points_.end(), tick,
      [](const GenderAutomationPoint& point, time::Tick value) {
        return point.tick < value;
      });
  if (iterator == points_.end() || iterator->tick != tick) return false;
  points_.erase(iterator);
  return true;
}

float GenderAutomation::valueAt(time::Tick tick) const noexcept {
  if (points_.empty()) return 0.0F;
  if (tick <= points_.front().tick) return points_.front().amount;
  if (tick >= points_.back().tick) return points_.back().amount;
  const auto right = std::upper_bound(
      points_.begin(), points_.end(), tick,
      [](time::Tick value, const GenderAutomationPoint& point) {
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
