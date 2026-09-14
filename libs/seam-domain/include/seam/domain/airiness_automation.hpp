#pragma once

#include "seam/core/result.hpp"
#include "seam/time/tick.hpp"

#include <cstddef>
#include <vector>

namespace seam::domain {

// The persisted unit for the airiness channel is normalized, and zero means the source exactly as the
// recipe declared it.
//
// Airiness and breathiness are not the same request, and the difference is bandwidth rather than amount.
// Breathiness is a balance: it moves share from the periodic part of the excitation into the aspiration
// the recipe already has, which is a dark, low-passed noise, and it does so in the band the voice is
// already using. Airiness adds a small amount of the *high-frequency* part of the same noise stream
// instead, above the aspiration filter's own corner, where the periodic source has almost no energy. The
// two channels can therefore be raised together without one being a duplicate of the other, and a
// maximally airy source is still a voiced one: the measured periodicity of the render is expected to stay
// above 0.9, where a maximally breathy one falls to about 0.7.
//
// The share is deliberately small. The noise generator is loud per unit of weight compared with the
// harmonic stack, so a share that reads as a fraction in the mix is already a clear change in the
// rendered band -- which is measured rather than assumed.
inline constexpr float kMaximumAiriness = 1.0F;
inline constexpr float kAirinessNoiseShare = 0.05F;
inline constexpr std::size_t kMaximumAirinessPoints = 16'384U;

struct AirinessAutomationPoint final {
  time::Tick tick;
  float amount{0.0F};

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const AirinessAutomationPoint&,
                         const AirinessAutomationPoint&) = default;
};

class AirinessAutomation final {
public:
  [[nodiscard]] const std::vector<AirinessAutomationPoint>& points() const noexcept {
    return points_;
  }
  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] core::Result<void> replacePoints(std::vector<AirinessAutomationPoint> points);
  [[nodiscard]] core::Result<void> upsert(AirinessAutomationPoint point);
  [[nodiscard]] bool erase(time::Tick tick) noexcept;
  [[nodiscard]] float valueAt(time::Tick tick) const noexcept;

  friend bool operator==(const AirinessAutomation&, const AirinessAutomation&) = default;

private:
  std::vector<AirinessAutomationPoint> points_;
};

}  // namespace seam::domain
