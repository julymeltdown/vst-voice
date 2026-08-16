#pragma once

#include "seam/time/tick.hpp"

#include <algorithm>
#include <cstdint>

namespace seam::time {

enum class SnapDirection { Nearest, Floor, Ceil };

class Quantizer final {
public:
  explicit constexpr Quantizer(Tick grid = Tick{kDefaultPpq / 4}) noexcept
      : grid_(grid.value() > 0 ? grid : Tick{1}) {}

  [[nodiscard]] constexpr Tick grid() const noexcept { return grid_; }

  [[nodiscard]] Tick snap(Tick value, SnapDirection direction = SnapDirection::Nearest) const noexcept {
    const auto gridValue = grid_.value();
    const auto raw = value.value();
    auto quotient = raw / gridValue;
    auto remainder = raw % gridValue;
    if (remainder < 0) {
      remainder += gridValue;
      --quotient;
    }

    switch (direction) {
      case SnapDirection::Floor:
        return Tick{quotient * gridValue};
      case SnapDirection::Ceil:
        return remainder == 0 ? Tick{raw} : Tick{(quotient + 1) * gridValue};
      case SnapDirection::Nearest:
        return remainder * 2 < gridValue
            ? Tick{quotient * gridValue}
            : Tick{(quotient + 1) * gridValue};
    }
    return value;
  }

private:
  Tick grid_;
};

}  // namespace seam::time
