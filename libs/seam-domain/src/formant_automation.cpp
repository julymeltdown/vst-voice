#include "seam/domain/formant_automation.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <utility>

namespace seam::domain {
namespace {

core::Result<void> validatePoints(std::span<const FormantAutomationPoint> points) {
  if (points.size() > kMaximumFormantPoints) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Formant curve exceeds the supported point count");
  }
  std::optional<time::Tick> previous;
  for (const auto& point : points) {
    const auto valid = point.validate();
    if (!valid) return valid;
    if (previous.has_value() && point.tick <= *previous) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Formant points must be strictly ordered and unique");
    }
    previous = point.tick;
  }
  return core::success();
}

}  // namespace

core::Result<void> FormantAutomationPoint::validate() const {
  if (tick < time::Tick{0} || !std::isfinite(semitones) ||
      std::abs(semitones) > kMaximumFormantShiftSemitones) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Formant requires nonnegative time and a finite shift within two octaves");
  }
  return core::success();
}

core::Result<void> FormantAutomation::validate() const {
  return validatePoints(points_);
}

core::Result<void> FormantAutomation::replacePoints(std::vector<FormantAutomationPoint> points) {
  const auto valid = validatePoints(points);
  if (!valid) return valid;
  points_ = std::move(points);
  return core::success();
}

core::Result<void> FormantAutomation::upsert(FormantAutomationPoint point) {
  const auto valid = point.validate();
  if (!valid) return valid;
  const auto iterator = std::lower_bound(
      points_.begin(), points_.end(), point.tick,
      [](const FormantAutomationPoint& candidate, time::Tick tick) {
        return candidate.tick < tick;
      });
  if (iterator != points_.end() && iterator->tick == point.tick) {
    *iterator = point;
  } else {
    if (points_.size() >= kMaximumFormantPoints) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Formant curve exceeds the supported point count");
    }
    points_.insert(iterator, point);
  }
  return core::success();
}

bool FormantAutomation::erase(time::Tick tick) noexcept {
  const auto iterator = std::lower_bound(
      points_.begin(), points_.end(), tick,
      [](const FormantAutomationPoint& point, time::Tick value) {
        return point.tick < value;
      });
  if (iterator == points_.end() || iterator->tick != tick) return false;
  points_.erase(iterator);
  return true;
}

float FormantAutomation::valueAt(time::Tick tick) const noexcept {
  if (points_.empty()) return 0.0F;
  if (tick <= points_.front().tick) return points_.front().semitones;
  if (tick >= points_.back().tick) return points_.back().semitones;
  const auto right = std::upper_bound(
      points_.begin(), points_.end(), tick,
      [](time::Tick value, const FormantAutomationPoint& point) {
        return value < point.tick;
      });
  const auto left = right - 1;
  const auto position = static_cast<float>(
      static_cast<double>((tick - left->tick).value()) /
      static_cast<double>((right->tick - left->tick).value()));
  return std::lerp(left->semitones, right->semitones, position);
}

}  // namespace seam::domain
