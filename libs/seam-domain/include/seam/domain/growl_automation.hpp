#pragma once

#include "seam/core/result.hpp"
#include "seam/time/tick.hpp"

#include <cstddef>
#include <vector>

namespace seam::domain {

// The persisted unit for the growl channel is normalized, and zero means the source exactly as the recipe
// declared it.
//
// Growl is the one channel that is defined by behaving, not by being smooth: it is a roughness of the
// source, produced by amplitude-modulating the periodic part of the excitation at exactly half the note's
// fundamental. A separate half-rate phase accumulator follows the same per-sample frequency and resets
// with the note's reattack. The bounded modulation depth keeps the periodic gain between 0.5 and 1.0;
// phase locking defines the frequency relationship, not the amplitude bound.
//
// The modulation rate is why the channel is audible at all. A subharmonic tone added to the excitation is
// filtered away by the vocal tract before it reaches the output -- at the fixture's own settings the tract
// puts half the fundamental about sixty decibels down, which is a channel that stores a curve and changes
// little measured subharmonic energy. Modulating at the subharmonic rate instead breaks each harmonic into sidebands
// half a fundamental apart, and those sidebands sit inside the same formant the harmonic does, so the
// roughness survives the same filtering that removes a plain subharmonic.
//
// The depth is deliberately below one. A growl that replaced the note would be a different sound rather
// than a rough version of the same one, and the product's own contract for this channel asks that it stay
// finite and bounded rather than that it be extreme.
inline constexpr float kMaximumGrowl = 1.0F;
inline constexpr float kGrowlSubharmonicDepth = 0.5F;
inline constexpr std::size_t kMaximumGrowlPoints = 16'384U;

struct GrowlAutomationPoint final {
  time::Tick tick;
  float amount{0.0F};

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const GrowlAutomationPoint&,
                         const GrowlAutomationPoint&) = default;
};

class GrowlAutomation final {
public:
  [[nodiscard]] const std::vector<GrowlAutomationPoint>& points() const noexcept {
    return points_;
  }
  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] core::Result<void> replacePoints(std::vector<GrowlAutomationPoint> points);
  [[nodiscard]] core::Result<void> upsert(GrowlAutomationPoint point);
  [[nodiscard]] bool erase(time::Tick tick) noexcept;
  [[nodiscard]] float valueAt(time::Tick tick) const noexcept;

  friend bool operator==(const GrowlAutomation&, const GrowlAutomation&) = default;

private:
  std::vector<GrowlAutomationPoint> points_;
};

}  // namespace seam::domain
