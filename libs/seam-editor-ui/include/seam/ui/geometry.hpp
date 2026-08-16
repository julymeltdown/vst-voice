#pragma once

namespace seam::ui {

struct Point final {
  double x{0.0};
  double y{0.0};
};

struct Size final {
  double width{0.0};
  double height{0.0};
};

struct Rect final {
  double x{0.0};
  double y{0.0};
  double width{0.0};
  double height{0.0};

  [[nodiscard]] constexpr double right() const noexcept { return x + width; }
  [[nodiscard]] constexpr double bottom() const noexcept { return y + height; }
  [[nodiscard]] constexpr bool contains(Point point) const noexcept {
    return point.x >= x && point.x <= right() && point.y >= y && point.y <= bottom();
  }
  [[nodiscard]] constexpr bool intersects(const Rect& other) const noexcept {
    return x < other.right() && right() > other.x && y < other.bottom() && bottom() > other.y;
  }
};

}  // namespace seam::ui
