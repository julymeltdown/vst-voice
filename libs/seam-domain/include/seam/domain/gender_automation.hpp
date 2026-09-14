#pragma once

#include "seam/core/result.hpp"
#include "seam/time/tick.hpp"

#include <cstddef>
#include <vector>

namespace seam::domain {

// The persisted unit for the gender channel is bipolar, and zero is exactly neutral: a negative value
// moves the voice one way and a positive value the other, with no preference for either direction.
//
// Gender is a mapping, not a fourth single-domain control. The tract and the source are two halves of one
// voice, and the channels beside this one deliberately move one half each: the formant channel shifts the
// tract's resonances and leaves the source alone, and the tension channel tilts the source and leaves the
// tract alone. Gender moves both, by a fixed ratio, so that the two halves of the voice stay consistent
// with each other instead of drifting apart. That coupling is the whole content of the channel, which is
// why the test for it measures both halves and also measures that neither neighbouring channel produces
// the pair.
//
// The two amounts are deliberately modest. Three semitones of tract shift and two decibels per octave of
// source tilt at the channel's extreme are each large enough to measure and small enough that the result
// is still the same voice at a different size rather than a different instrument. The direction is
// defined so that a positive value raises both, which is the direction a listener hears as lighter.
inline constexpr float kMaximumGender = 1.0F;
inline constexpr float kGenderFormantSemitones = 3.0F;
inline constexpr float kGenderTiltDbPerOctave = 2.0F;
inline constexpr std::size_t kMaximumGenderPoints = 16'384U;

struct GenderAutomationPoint final {
  time::Tick tick;
  float amount{0.0F};

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const GenderAutomationPoint&,
                         const GenderAutomationPoint&) = default;
};

class GenderAutomation final {
public:
  [[nodiscard]] const std::vector<GenderAutomationPoint>& points() const noexcept {
    return points_;
  }
  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] core::Result<void> replacePoints(std::vector<GenderAutomationPoint> points);
  [[nodiscard]] core::Result<void> upsert(GenderAutomationPoint point);
  [[nodiscard]] bool erase(time::Tick tick) noexcept;
  [[nodiscard]] float valueAt(time::Tick tick) const noexcept;

  friend bool operator==(const GenderAutomation&, const GenderAutomation&) = default;

private:
  std::vector<GenderAutomationPoint> points_;
};

}  // namespace seam::domain
