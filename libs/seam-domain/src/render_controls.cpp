#include "seam/domain/render_controls.hpp"

#include <algorithm>
#include <cmath>

namespace seam::domain {
namespace {

bool validNormalized(const std::optional<float>& value) noexcept {
  return !value.has_value() ||
         (std::isfinite(*value) && *value >= 0.0F && *value <= 1.0F);
}

float smoothstep(float value) noexcept {
  const auto clamped = std::clamp(value, 0.0F, 1.0F);
  return clamped * clamped * (3.0F - 2.0F * clamped);
}

}  // namespace

std::string_view unitRendererKindName(UnitRendererKind value) noexcept {
  switch (value) {
    case UnitRendererKind::Inherit: return "inherit";
    case UnitRendererKind::Raw: return "raw";
    case UnitRendererKind::ClassicPsola: return "classic-psola";
    case UnitRendererKind::SpectralClassic: return "spectral-classic";
    case UnitRendererKind::Stretch: return "stretch";
  }
  return "inherit";
}

UnitRendererKind parseUnitRendererKind(std::string_view value) noexcept {
  if (value == "raw") return UnitRendererKind::Raw;
  if (value == "classic-psola") return UnitRendererKind::ClassicPsola;
  if (value == "spectral-classic") return UnitRendererKind::SpectralClassic;
  if (value == "stretch") return UnitRendererKind::Stretch;
  return UnitRendererKind::Inherit;
}

core::Result<void> UnitSelectionOverride::validate() const {
  if (!startKey.noteId.valid()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Unit selection override must reference a valid note");
  }
  if (tokenCount == 0) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Unit selection override token count must be positive",
                         startKey.toString());
  }
  if (unitId.empty()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Unit selection override must identify a voicebank unit",
                         startKey.toString());
  }
  return core::success();
}

std::string_view seamCurveName(SeamCurve curve) noexcept {
  switch (curve) {
    case SeamCurve::Smooth: return "smooth";
    case SeamCurve::Linear: return "linear";
    case SeamCurve::EqualPower: return "equal-power";
    case SeamCurve::HardCharacter: return "hard-character";
  }
  return "smooth";
}

SeamCurve parseSeamCurve(std::string_view value) noexcept {
  if (value == "linear") return SeamCurve::Linear;
  if (value == "equal-power") return SeamCurve::EqualPower;
  if (value == "hard-character") return SeamCurve::HardCharacter;
  return SeamCurve::Smooth;
}

core::Result<void> SeamOverride::validate() const {
  if (!incomingStartKey.noteId.valid()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Seam override must reference a valid incoming phoneme");
  }
  if (!validNormalized(seamAmount) || !validNormalized(phaseReset) ||
      !validNormalized(envelopeBlend)) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Seam override normalized values must be in [0, 1]",
                         incomingStartKey.toString());
  }
  constexpr time::Microseconds kMaximumOverlapUs = 1'000'000;
  if (overlap.has_value() && (*overlap < 0 || *overlap > kMaximumOverlapUs)) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Seam overlap must be between zero and one second",
                         incomingStartKey.toString());
  }
  if (!seamAmount.has_value() && !overlap.has_value() && !phaseReset.has_value() &&
      !envelopeBlend.has_value()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Seam override must change at least one value",
                         incomingStartKey.toString());
  }
  return core::success();
}

std::string_view curveInterpolationName(CurveInterpolation value) noexcept {
  switch (value) {
    case CurveInterpolation::Step: return "step";
    case CurveInterpolation::Linear: return "linear";
    case CurveInterpolation::Smooth: return "smooth";
  }
  return "linear";
}

CurveInterpolation parseCurveInterpolation(std::string_view value) noexcept {
  if (value == "step") return CurveInterpolation::Step;
  if (value == "smooth") return CurveInterpolation::Smooth;
  return CurveInterpolation::Linear;
}

core::Result<void> PitchAutomationPoint::validate() const {
  if (tick < time::Tick{0}) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Pitch automation tick must not be negative");
  }
  if (!std::isfinite(cents) || cents < -4800.0F || cents > 4800.0F) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Pitch automation cents must be finite and within four octaves");
  }
  return core::success();
}

core::Result<void> PitchAutomation::validate() const {
  std::optional<time::Tick> previous;
  for (const auto& point : points_) {
    const auto validation = point.validate();
    if (!validation) return validation;
    if (previous.has_value() && point.tick <= *previous) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Pitch automation points must be strictly ordered and unique");
    }
    previous = point.tick;
  }
  return core::success();
}

float PitchAutomation::valueAt(time::Tick tick) const noexcept {
  if (points_.empty()) return 0.0F;
  if (tick <= points_.front().tick) return points_.front().cents;
  if (tick >= points_.back().tick) return points_.back().cents;

  const auto right = std::upper_bound(
      points_.begin(), points_.end(), tick,
      [](time::Tick value, const PitchAutomationPoint& point) {
        return value < point.tick;
      });
  const auto left = right - 1;
  if (left->interpolation == CurveInterpolation::Step) return left->cents;

  const auto span = (right->tick - left->tick).value();
  if (span <= 0) return right->cents;
  auto position = static_cast<float>((tick - left->tick).value()) /
                  static_cast<float>(span);
  if (left->interpolation == CurveInterpolation::Smooth) {
    position = smoothstep(position);
  }
  return left->cents * (1.0F - position) + right->cents * position;
}

core::Result<void> PitchAutomation::upsert(PitchAutomationPoint point) {
  const auto validation = point.validate();
  if (!validation) return validation;
  const auto iterator = std::lower_bound(
      points_.begin(), points_.end(), point.tick,
      [](const PitchAutomationPoint& candidate, time::Tick tick) {
        return candidate.tick < tick;
      });
  if (iterator != points_.end() && iterator->tick == point.tick) {
    *iterator = point;
  } else {
    points_.insert(iterator, point);
  }
  return core::success();
}

bool PitchAutomation::erase(time::Tick tick) noexcept {
  const auto iterator = std::lower_bound(
      points_.begin(), points_.end(), tick,
      [](const PitchAutomationPoint& candidate, time::Tick value) {
        return candidate.tick < value;
      });
  if (iterator == points_.end() || iterator->tick != tick) return false;
  points_.erase(iterator);
  return true;
}

}  // namespace seam::domain
