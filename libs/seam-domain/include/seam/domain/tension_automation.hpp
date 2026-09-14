#pragma once

#include "seam/core/result.hpp"
#include "seam/time/tick.hpp"

#include <cstddef>
#include <vector>

namespace seam::domain {

// The persisted unit for the tension channel is a normalized change in the source's own spectrum, not a
// gain and not a formant: zero means the source exactly as the recipe declared it, and one means the most
// pressed setting the channel admits. Because it is a spectrum rather than a level, a tense phrase is
// compared to a relaxed one after level matching -- otherwise a loudness change would be mistaken for
// effort.
//
// The maximum is a measured bound rather than a taste. Tension adds tilt to the harmonic source's own
// spectral roll-off, and the measurement that answers "did the source get brighter" is the ratio of
// harmonic energy above two kilohertz to harmonic energy around the first formant -- a spectral centroid
// can be pinned by one dominant harmonic while the balance of the spectrum moves underneath it. Nine
// decibels per octave at the channel's maximum was measured first and rejected: it took the high/low
// harmonic energy ratio to about forty-six times the recipe's own and left the fundamental ten times
// weaker, which is a different instrument rather than a more pressed voice. Four decibels per octave at
// the maximum moves that ratio by about four times while the fundamental stays within a factor of about
// three and the recovered fundamental is unchanged, so that is where the channel's maximum sits.
inline constexpr float kMaximumTension = 1.0F;
inline constexpr float kTensionTiltDbPerOctave = 4.0F;
inline constexpr std::size_t kMaximumTensionPoints = 16'384U;

struct TensionAutomationPoint final {
  time::Tick tick;
  float amount{0.0F};

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const TensionAutomationPoint&,
                         const TensionAutomationPoint&) = default;
};

class TensionAutomation final {
public:
  [[nodiscard]] const std::vector<TensionAutomationPoint>& points() const noexcept {
    return points_;
  }
  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] core::Result<void> replacePoints(std::vector<TensionAutomationPoint> points);
  [[nodiscard]] core::Result<void> upsert(TensionAutomationPoint point);
  [[nodiscard]] bool erase(time::Tick tick) noexcept;
  [[nodiscard]] float valueAt(time::Tick tick) const noexcept;

  friend bool operator==(const TensionAutomation&, const TensionAutomation&) = default;

private:
  std::vector<TensionAutomationPoint> points_;
};

}  // namespace seam::domain
