#include "seam/synthesis/pitch_curve.hpp"

#include <algorithm>
#include <cmath>

namespace seam::synthesis {

core::Result<void> PitchCurve::validate() const {
  time::SampleFrame previous = -1;
  for (const auto& point : points_) {
    if (point.frame < 0 || point.frame <= previous || !std::isfinite(point.cents) ||
        point.cents < -4800.0F || point.cents > 4800.0F) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Pitch curve points must be ordered and inside supported ranges");
    }
    previous = point.frame;
  }
  return core::success();
}

float PitchCurve::centsAt(time::SampleFrame frame) const noexcept {
  if (points_.empty()) return 0.0F;
  if (frame <= points_.front().frame) return points_.front().cents;
  if (frame >= points_.back().frame) return points_.back().cents;
  const auto upper = std::upper_bound(
      points_.begin(), points_.end(), frame,
      [](time::SampleFrame value, const PitchPoint& point) {
        return value < point.frame;
      });
  if (upper == points_.begin() || upper == points_.end()) return 0.0F;
  const auto& right = *upper;
  const auto& left = *(upper - 1);
  const auto denominator = static_cast<double>(right.frame - left.frame);
  if (denominator <= 0.0) return left.cents;
  auto t = static_cast<float>(
      static_cast<double>(frame - left.frame) / denominator);
  if (left.interpolation == domain::CurveInterpolation::Step) return left.cents;
  if (left.interpolation == domain::CurveInterpolation::Smooth) {
    t = t * t * (3.0F - 2.0F * t);
  }
  return left.cents * (1.0F - t) + right.cents * t;
}

}  // namespace seam::synthesis
