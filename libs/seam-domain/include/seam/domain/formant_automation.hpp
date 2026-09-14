#pragma once

#include "seam/core/result.hpp"
#include "seam/time/tick.hpp"

#include <cstddef>
#include <vector>

namespace seam::domain {

// The persisted unit for the formant channel is a semitone shift of the vocal tract's resonance
// frequencies, not a raw frequency: the same value means the same tract change whatever the speaker's
// own formants are, and zero is exactly neutral. The bound is deliberately wider than any musically
// useful setting so that a value outside it is a mistake rather than a taste.
inline constexpr float kMaximumFormantShiftSemitones = 24.0F;
inline constexpr std::size_t kMaximumFormantPoints = 16'384U;

struct FormantAutomationPoint final {
  time::Tick tick;
  float semitones{0.0F};

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const FormantAutomationPoint&,
                         const FormantAutomationPoint&) = default;
};

class FormantAutomation final {
public:
  [[nodiscard]] const std::vector<FormantAutomationPoint>& points() const noexcept {
    return points_;
  }
  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] core::Result<void> replacePoints(std::vector<FormantAutomationPoint> points);
  [[nodiscard]] core::Result<void> upsert(FormantAutomationPoint point);
  [[nodiscard]] bool erase(time::Tick tick) noexcept;
  [[nodiscard]] float valueAt(time::Tick tick) const noexcept;

  friend bool operator==(const FormantAutomation&, const FormantAutomation&) = default;

private:
  std::vector<FormantAutomationPoint> points_;
};

}  // namespace seam::domain
