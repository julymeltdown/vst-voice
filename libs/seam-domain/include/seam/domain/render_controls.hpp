#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/phoneme.hpp"
#include "seam/time/tick.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seam::domain {

enum class UnitRendererKind {
  Inherit,
  Raw,
  ClassicPsola,
  SpectralClassic,
  Stretch,
};

[[nodiscard]] std::string_view unitRendererKindName(UnitRendererKind value) noexcept;
[[nodiscard]] UnitRendererKind parseUnitRendererKind(std::string_view value) noexcept;

struct UnitSelectionOverride final {
  PhonemeKey startKey;
  std::uint16_t tokenCount{1};
  std::string unitId;
  UnitRendererKind renderer{UnitRendererKind::Inherit};
  bool locked{true};

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const UnitSelectionOverride&,
                         const UnitSelectionOverride&) = default;
};

enum class SeamCurve {
  Smooth,
  Linear,
  EqualPower,
  HardCharacter,
};

[[nodiscard]] std::string_view seamCurveName(SeamCurve curve) noexcept;
[[nodiscard]] SeamCurve parseSeamCurve(std::string_view value) noexcept;

struct SeamOverride final {
  // The key identifies the first phoneme of the incoming unit. This remains
  // stable across automatic re-planning while that phoneme still exists.
  PhonemeKey incomingStartKey;
  std::optional<float> seamAmount;
  std::optional<time::Microseconds> overlap;
  std::optional<float> phaseReset;
  std::optional<float> envelopeBlend;
  SeamCurve curve{SeamCurve::Smooth};
  bool locked{true};

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const SeamOverride&, const SeamOverride&) = default;
};

enum class CurveInterpolation {
  Step,
  Linear,
  Smooth,
};

[[nodiscard]] std::string_view curveInterpolationName(CurveInterpolation value) noexcept;
[[nodiscard]] CurveInterpolation parseCurveInterpolation(std::string_view value) noexcept;

struct PitchAutomationPoint final {
  time::Tick tick;
  float cents{0.0F};
  CurveInterpolation interpolation{CurveInterpolation::Linear};

  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const PitchAutomationPoint&,
                         const PitchAutomationPoint&) = default;
};

class PitchAutomation final {
public:
  [[nodiscard]] const std::vector<PitchAutomationPoint>& points() const noexcept {
    return points_;
  }
  [[nodiscard]] std::vector<PitchAutomationPoint>& points() noexcept { return points_; }

  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] float valueAt(time::Tick tick) const noexcept;
  [[nodiscard]] core::Result<void> upsert(PitchAutomationPoint point);
  [[nodiscard]] bool erase(time::Tick tick) noexcept;

  friend bool operator==(const PitchAutomation&, const PitchAutomation&) = default;

private:
  std::vector<PitchAutomationPoint> points_;
};

}  // namespace seam::domain
