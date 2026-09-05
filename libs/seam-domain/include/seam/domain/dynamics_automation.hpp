#pragma once

#include "seam/core/result.hpp"
#include "seam/time/tick.hpp"

#include <cstddef>
#include <vector>

namespace seam::domain {

inline constexpr float kMaximumDynamicsGain = 3.9810717055F;
inline constexpr std::size_t kMaximumDynamicsPoints = 16'384U;

struct DynamicsAutomationPoint final {
  time::Tick tick;
  float linearGain{1.0F};

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const DynamicsAutomationPoint&,
                         const DynamicsAutomationPoint&) = default;
};

class DynamicsAutomation final {
public:
  [[nodiscard]] const std::vector<DynamicsAutomationPoint>& points() const noexcept {
    return points_;
  }
  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] core::Result<void> replacePoints(std::vector<DynamicsAutomationPoint> points);
  [[nodiscard]] core::Result<void> upsert(DynamicsAutomationPoint point);
  [[nodiscard]] bool erase(time::Tick tick) noexcept;
  [[nodiscard]] float valueAt(time::Tick tick) const noexcept;

  friend bool operator==(const DynamicsAutomation&, const DynamicsAutomation&) = default;

private:
  std::vector<DynamicsAutomationPoint> points_;
};

}
