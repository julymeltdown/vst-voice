#include "seam/domain/note_vibrato.hpp"

#include <cmath>

namespace seam::domain {

core::Result<void> NoteVibrato::validate() const {
  const auto fraction = [](float value) {
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
  };
  if (!fraction(startFraction) || !fraction(fadeInFraction) ||
      !fraction(fadeOutFraction) || fadeInFraction + fadeOutFraction > 1.0F) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Vibrato fractions and combined fades must fit their span");
  }
  if (!std::isfinite(depthCents) || depthCents < 0.0F || depthCents > 200.0F ||
      !std::isfinite(periodMilliseconds) || periodMilliseconds < 5.0F ||
      periodMilliseconds > 500.0F || !fraction(phaseTurns) || phaseTurns == 1.0F) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Vibrato depth, period or phase is outside supported bounds");
  }
  return core::success();
}

}
