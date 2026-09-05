#include "seam/domain/performance_intent.hpp"

namespace seam::domain {

bool PerformanceTimeRange::contains(time::Tick tick) const noexcept {
  return tick >= startTick && tick < endTick;
}

core::Result<void> PerformanceTimeRange::validate() const {
  if (startTick < time::Tick{0} || endTick <= startTick) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Performance ownership requires a nonempty nonnegative time range");
  }
  return core::success();
}

core::Result<void> ManualPerformanceOwnership::validate() const {
  switch (channel) {
    case PerformanceChannel::Pitch:
    case PerformanceChannel::Timing:
    case PerformanceChannel::Dynamics:
    case PerformanceChannel::Breathiness:
    case PerformanceChannel::Tension:
    case PerformanceChannel::Airiness:
    case PerformanceChannel::Formant:
    case PerformanceChannel::Gender:
    case PerformanceChannel::StyleBlend:
    case PerformanceChannel::Growl:
    case PerformanceChannel::Attack:
    case PerformanceChannel::Release: break;
    default:
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Unknown performance ownership channel");
  }
  if (mode != ManualPerformanceMode::Replace &&
      mode != ManualPerformanceMode::PitchOffset) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Unknown manual performance mode");
  }
  if (mode == ManualPerformanceMode::PitchOffset && channel != PerformanceChannel::Pitch) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Additive manual ownership is only defined for pitch in cents");
  }
  if (const auto* note = std::get_if<NoteId>(&scope)) {
    if (!note->valid()) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Performance ownership requires a valid note identity");
    }
    return core::success();
  }
  return std::get<PerformanceTimeRange>(scope).validate();
}

bool ManualPerformanceOwnership::appliesTo(NoteId noteId, time::Tick tick) const noexcept {
  if (const auto* note = std::get_if<NoteId>(&scope)) {
    return note->valid() && *note == noteId;
  }
  return std::get<PerformanceTimeRange>(scope).contains(tick);
}

core::Result<void> validatePerformanceAcceptanceRevision(
    PerformanceRevision captured, PerformanceRevision current) {
  if (captured != current) {
    return core::failure(core::ErrorCode::Conflict,
                         "Proposed performance was generated from a different musical, "
                         "pronunciation or ownership revision");
  }
  return core::success();
}

}
