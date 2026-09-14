#pragma once

#include "seam/core/result.hpp"
#include "seam/time/tick.hpp"

#include <cstddef>
#include <vector>

namespace seam::domain {

// The persisted unit for the breathiness channel is a normalized balance between the periodic and the
// aperiodic part of the excitation, not a noise level in decibels: zero means the source exactly as the
// recipe declared it, and one means the most breathy setting the channel admits, whatever that recipe's
// own aspiration already was.
//
// The maximum conversion is a measured bound rather than a taste. The channel converts a share of the
// excitation's periodic weight into aperiodic weight, and the procedural source's aperiodic generator
// is much louder per unit of weight than its harmonic stack, so a conversion that looks modest as a
// weight is already a large change in energy. At one fifth of the periodic weight the source is
// dominated by noise: the fundamental is no longer recovered from the rendered phrase and the level
// rises by half. Fifteen hundredths of the periodic weight is the last setting that keeps the phrase
// clearly voiced -- the measured periodicity of the render falls from about 0.98 to about 0.70 while
// the recovered fundamental stays within a few hundredths of a hertz and the level moves by about two
// percent -- so that is where the channel's maximum sits. A breathy setting is therefore a change of
// production, not a substitution of noise for a voice.
inline constexpr float kMaximumBreathiness = 1.0F;
inline constexpr float kBreathinessAperiodicShare = 0.15F;
inline constexpr std::size_t kMaximumBreathinessPoints = 16'384U;

struct BreathinessAutomationPoint final {
  time::Tick tick;
  float amount{0.0F};

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const BreathinessAutomationPoint&,
                         const BreathinessAutomationPoint&) = default;
};

class BreathinessAutomation final {
public:
  [[nodiscard]] const std::vector<BreathinessAutomationPoint>& points() const noexcept {
    return points_;
  }
  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] core::Result<void> replacePoints(std::vector<BreathinessAutomationPoint> points);
  [[nodiscard]] core::Result<void> upsert(BreathinessAutomationPoint point);
  [[nodiscard]] bool erase(time::Tick tick) noexcept;
  [[nodiscard]] float valueAt(time::Tick tick) const noexcept;

  friend bool operator==(const BreathinessAutomation&, const BreathinessAutomation&) = default;

private:
  std::vector<BreathinessAutomationPoint> points_;
};

}  // namespace seam::domain
