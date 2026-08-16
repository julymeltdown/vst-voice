#pragma once

#include <compare>
#include <cstdint>
#include <limits>

namespace seam::time {

class Tick final {
public:
  constexpr Tick() noexcept = default;
  explicit constexpr Tick(std::int64_t value) noexcept : value_(value) {}

  [[nodiscard]] constexpr std::int64_t value() const noexcept { return value_; }

  constexpr Tick& operator+=(Tick other) noexcept {
    value_ += other.value_;
    return *this;
  }
  constexpr Tick& operator-=(Tick other) noexcept {
    value_ -= other.value_;
    return *this;
  }

  friend constexpr Tick operator+(Tick lhs, Tick rhs) noexcept { return lhs += rhs; }
  friend constexpr Tick operator-(Tick lhs, Tick rhs) noexcept { return lhs -= rhs; }
  friend constexpr bool operator==(Tick, Tick) noexcept = default;
  friend constexpr auto operator<=>(Tick, Tick) noexcept = default;

private:
  std::int64_t value_{0};
};

using SampleFrame = std::int64_t;
using Microseconds = std::int64_t;
using Ppq = std::int32_t;

constexpr Ppq kDefaultPpq = 960;
constexpr Tick kZeroTick{0};

}  // namespace seam::time
