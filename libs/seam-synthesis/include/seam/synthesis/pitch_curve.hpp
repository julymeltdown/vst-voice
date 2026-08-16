#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/render_controls.hpp"
#include "seam/time/tick.hpp"

#include <span>
#include <vector>

namespace seam::synthesis {

struct PitchPoint final {
  time::SampleFrame frame{0};
  float cents{0.0F};
  domain::CurveInterpolation interpolation{domain::CurveInterpolation::Linear};

  friend bool operator==(const PitchPoint&, const PitchPoint&) = default;
};

class PitchCurve final {
public:
  PitchCurve() = default;
  explicit PitchCurve(std::vector<PitchPoint> points)
      : points_(std::move(points)) {}

  [[nodiscard]] std::span<const PitchPoint> points() const noexcept {
    return points_;
  }
  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] float centsAt(time::SampleFrame frame) const noexcept;

private:
  std::vector<PitchPoint> points_;
};

}  // namespace seam::synthesis
