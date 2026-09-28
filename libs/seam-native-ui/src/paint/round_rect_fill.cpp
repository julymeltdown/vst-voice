#include "seam/native_ui/paint/round_rect_fill.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace seam::native_ui::paint {
namespace {

// The share of a unit pixel on the inside of a straight edge whose unit normal is (nx, ny), when
// the pixel's centre lies `inside` pixels inside it (negative outside): the distribution of a
// square's points along a direction is a trapezoid, linear where one side dominates and quadratic
// where the corners cross.
double halfPlaneCoverage(double inside, double nx, double ny) noexcept {
  const auto a = std::max(std::abs(nx), std::abs(ny));
  const auto b = std::min(std::abs(nx), std::abs(ny));
  const auto outer = (a + b) * 0.5;
  const auto inner = (a - b) * 0.5;
  if (inside <= -outer) return 0.0;
  if (inside >= outer) return 1.0;
  if (inside < -inner) {
    const auto u = inside + outer;
    return u * u / (2.0 * a * b);
  }
  if (inside > inner) {
    const auto u = outer - inside;
    return 1.0 - u * u / (2.0 * a * b);
  }
  return 0.5 + inside / a;
}

// Visits the pixels a rounded rectangle covers inside a target, a row at a time. `run(row, x0, x1,
// coverage)` receives the columns wholly inside the shape horizontally, whose coverage is the share
// of the row the shape spans; `edge(row, x, coverage)` receives every other column the shape touches.
template <class Run, class Edge>
void forEachCovered(const DeviceRoundRect& s, const DeviceFillTarget& target, Run&& run, Edge&& edge) {
  if (!(s.right > s.left) || !(s.bottom > s.top)) return;
  const auto radius = std::clamp(s.radius, 0.0, std::min(s.right - s.left, s.bottom - s.top) * 0.5);
  const auto arcTop = s.top + radius;
  const auto arcBottom = s.bottom - radius;
  const auto centreX = (s.left + s.right) * 0.5;
  const auto centreY = (s.top + s.bottom) * 0.5;
  const auto halfWidth = (s.right - s.left) * 0.5 - radius;
  const auto halfHeight = (s.bottom - s.top) * 0.5 - radius;
  // The left boundary at height y; the right boundary mirrors it.
  const auto boundary = [&](double y) {
    const auto dy = y < arcTop ? arcTop - y : y > arcBottom ? y - arcBottom : 0.0;
    if (dy <= 0.0) return s.left;
    return s.left + radius - std::sqrt(std::max(0.0, radius * radius - dy * dy));
  };
  // Area coverage of a pixel crossed only by vertical edges. Elsewhere the boundary nearest the
  // pixel's centre is taken as straight through it, along the arc's tangent or a straight side, and
  // its exact half-plane share is used; that also covers a straight top or bottom edge crossing
  // the same pixel.
  const auto covered = [&](std::int64_t x, std::int64_t row, double ya, double yb) {
    const auto x0 = static_cast<double>(x);
    if (ya >= arcTop && yb <= arcBottom) {
      const auto width = std::min(x0 + 1.0, s.right) - std::max(x0, s.left);
      return std::clamp(width, 0.0, 1.0) * (yb - ya);
    }
    const auto qx = std::abs(x0 + 0.5 - centreX) - halfWidth;
    const auto qy = std::abs(static_cast<double>(row) + 0.5 - centreY) - halfHeight;
    if (qx > 0.0 && qy > 0.0) {
      const auto length = std::sqrt(qx * qx + qy * qy);
      return halfPlaneCoverage(radius - length, qx / length, qy / length);
    }
    return qx > qy ? halfPlaneCoverage(radius - qx, 1.0, 0.0) : halfPlaneCoverage(radius - qy, 0.0, 1.0);
  };
  const auto first = std::max<std::int64_t>(target.clipTop, static_cast<std::int64_t>(std::floor(s.top)));
  const auto last = std::min<std::int64_t>(target.clipBottom, static_cast<std::int64_t>(std::ceil(s.bottom)));
  const auto clampX = [&](double x) {
    return static_cast<std::int64_t>(std::clamp(x, static_cast<double>(target.clipLeft),
                                                static_cast<double>(target.clipRight)));
  };
  for (auto row = first; row < last; ++row) {
    const auto y0 = static_cast<double>(row);
    const auto ya = std::max(y0, s.top);
    const auto yb = std::min(y0 + 1.0, s.bottom);
    if (yb <= ya) continue;
    // The boundary moves outward toward the straight part, so its extremes over the row lie at the
    // row's ends.
    const auto straight = ya <= arcBottom && yb >= arcTop;
    const auto innerLeft = std::max(boundary(ya), boundary(yb));
    const auto outerLeft = straight ? s.left : std::min(boundary(ya), boundary(yb));
    const auto innerRight = s.left + s.right - innerLeft;
    const auto outerRight = s.left + s.right - outerLeft;
    const auto outerStart = clampX(std::floor(outerLeft));
    const auto outerEnd = clampX(std::ceil(outerRight));
    auto fullStart = clampX(std::ceil(innerLeft));
    auto fullEnd = clampX(std::floor(innerRight));
    if (fullEnd < fullStart) fullStart = fullEnd = outerEnd;
    for (auto x = outerStart; x < fullStart; ++x) edge(row, x, covered(x, row, ya, yb));
    if (fullStart < fullEnd) run(row, fullStart, fullEnd, yb - ya);
    for (auto x = std::max(fullEnd, fullStart); x < outerEnd; ++x) edge(row, x, covered(x, row, ya, yb));
  }
}

// One pixel: the unpremultiplied colour at alpha a (0..1) over a premultiplied pixel, in 16-bit
// fixed point, floored after adding the dither threshold.
std::uint32_t composite(std::uint32_t held, const double (&color)[3], double a,
                        std::uint32_t threshold) noexcept {
  const auto weight = static_cast<std::uint32_t>(std::lround(std::clamp(a, 0.0, 1.0) * 65536.0));
  const auto keep = 65536U - weight;
  const auto channel = [&](double source, unsigned shift) {
    const auto s = static_cast<std::uint32_t>(std::lround(source * static_cast<double>(weight)));
    return ((s + ((held >> shift) & 255U) * keep + threshold) >> 16U) << shift;
  };
  return channel(color[0], 0U) | channel(color[1], 8U) | channel(color[2], 16U) |
         (((255U * weight + (held >> 24U) * keep + threshold) >> 16U) << 24U);
}

// A row's dither thresholds, turned so that column x reads entry x & 7.
std::array<std::uint32_t, 8U> ditherRow(const DeviceFillTarget& target, std::int64_t row) {
  const auto& source = kOrderedDither[static_cast<std::size_t>((row + target.ditherRow) & 7)];
  const auto turn = static_cast<std::size_t>(target.ditherColumn & 7);
  std::array<std::uint32_t, 8U> turned{};
  for (std::size_t x = 0; x < 8U; ++x) turned[x] = source[(x + turn) & 7U];
  return turned;
}

// The three colour channels of a run pixel, each source term already scaled by the weight; an
// opaque destination stays opaque, as the blended alpha would give.
std::uint32_t blendRun(std::uint32_t held, std::uint32_t blue, std::uint32_t green, std::uint32_t red,
                       std::uint32_t opacity, std::uint32_t keep, std::uint32_t d) noexcept {
  const auto colour = ((blue + (held & 255U) * keep + d) >> 16U) |
                      (((green + ((held >> 8U) & 255U) * keep + d) >> 16U) << 8U) |
                      (((red + ((held >> 16U) & 255U) * keep + d) >> 16U) << 16U);
  return colour | (held >= 0xFF000000U ? 0xFF000000U
                                        : (((opacity + (held >> 24U) * keep + d) >> 16U) << 24U));
}

}  // namespace

void fillRoundRectVertical(PixelSurface& surface, const DeviceRoundRect& shape,
                           const DeviceFillTarget& clipped, Color top, Color bottom, double gradientTop,
                           double gradientBottom, double alpha) {
  if (!(alpha > 0.0) || (top.alpha == 0U && bottom.alpha == 0U)) return;
  auto target = clipped;
  target.clipLeft = std::max<std::int64_t>(target.clipLeft, 0);
  target.clipTop = std::max<std::int64_t>(target.clipTop, 0);
  target.clipRight = std::min<std::int64_t>(target.clipRight, surface.width());
  target.clipBottom = std::min<std::int64_t>(target.clipBottom, surface.height());
  const auto width = static_cast<std::size_t>(surface.width());
  auto pixels = surface.pixels();
  // The colour of a row is the gradient's value at the row's pixel centres.
  struct RowColour final {
    std::int64_t row{-1};
    double color[3]{};
    double alpha{0.0};
    std::array<std::uint32_t, 8U> dither{};
  } current;
  const auto rowColour = [&](std::int64_t row) -> const RowColour& {
    if (current.row == row) return current;
    const auto span = gradientBottom - gradientTop;
    const auto t = span == 0.0 ? (static_cast<double>(row) + 0.5 < gradientTop ? 0.0 : 1.0)
                               : std::clamp((static_cast<double>(row) + 0.5 - gradientTop) / span, 0.0, 1.0);
    const auto mix = [t](std::uint8_t a, std::uint8_t b) {
      return static_cast<double>(a) + (static_cast<double>(b) - static_cast<double>(a)) * t;
    };
    current.row = row;
    current.color[0] = mix(top.blue, bottom.blue);
    current.color[1] = mix(top.green, bottom.green);
    current.color[2] = mix(top.red, bottom.red);
    current.alpha = mix(top.alpha, bottom.alpha) / 255.0 * alpha;
    current.dither = ditherRow(target, row);
    return current;
  };
  forEachCovered(
      shape, target,
      [&](std::int64_t row, std::int64_t x0, std::int64_t x1, double coverage) {
        const auto& c = rowColour(row);
        const auto weight =
            static_cast<std::uint32_t>(std::lround(std::clamp(c.alpha * coverage, 0.0, 1.0) * 65536.0));
        if (weight == 0U) return;
        // One weight and one source term per row.
        const auto keep = 65536U - weight;
        const auto source = [&](double value) {
          return static_cast<std::uint32_t>(std::lround(value * static_cast<double>(weight)));
        };
        const auto blue = source(c.color[0]);
        const auto green = source(c.color[1]);
        const auto red = source(c.color[2]);
        const auto opacity = 255U * weight;
        auto* line = pixels.data() + static_cast<std::size_t>(row) * width;
        for (auto x = x0; x < x1; ++x)
          line[x] = blendRun(line[x], blue, green, red, opacity, keep,
                             c.dither[static_cast<std::size_t>(x) & 7U]);
      },
      [&](std::int64_t row, std::int64_t x, double coverage) {
        const auto& c = rowColour(row);
        if (coverage <= 0.0) return;
        auto& pixel = pixels[static_cast<std::size_t>(row) * width + static_cast<std::size_t>(x)];
        pixel = composite(pixel, c.color, c.alpha * coverage, c.dither[static_cast<std::size_t>(x) & 7U]);
      });
}

void fillRoundRectRadial(PixelSurface& surface, const DeviceRoundRect& shape,
                         const DeviceFillTarget& clipped, Color inner, Color outer, double centerX,
                         double centerY, double radius, double alpha) {
  if (!(alpha > 0.0) || !(radius > 0.0) || (inner.alpha == 0U && outer.alpha == 0U)) return;
  auto target = clipped;
  target.clipLeft = std::max<std::int64_t>(target.clipLeft, 0);
  target.clipTop = std::max<std::int64_t>(target.clipTop, 0);
  target.clipRight = std::min<std::int64_t>(target.clipRight, surface.width());
  target.clipBottom = std::min<std::int64_t>(target.clipBottom, surface.height());
  const auto width = static_cast<std::size_t>(surface.width());
  auto pixels = surface.pixels();
  const auto inverseRadius = 1.0 / radius;
  const auto step = [](std::uint8_t a, std::uint8_t b) {
    return static_cast<double>(b) - static_cast<double>(a);
  };
  // The gradient's value at a pixel centre.
  const auto at = [&](std::int64_t row, std::int64_t x) {
    const auto dx = static_cast<double>(x) + 0.5 - centerX;
    const auto dy = static_cast<double>(row) + 0.5 - centerY;
    return std::min(1.0, std::sqrt(dx * dx + dy * dy) * inverseRadius);
  };
  const auto paint = [&](std::int64_t row, std::int64_t x, double coverage) {
    if (coverage <= 0.0) return;
    const auto t = at(row, x);
    const auto mix = [t](std::uint8_t a, std::uint8_t b) {
      return static_cast<double>(a) + (static_cast<double>(b) - static_cast<double>(a)) * t;
    };
    const double color[3]{mix(inner.blue, outer.blue), mix(inner.green, outer.green),
                          mix(inner.red, outer.red)};
    auto& pixel = pixels[static_cast<std::size_t>(row) * width + static_cast<std::size_t>(x)];
    pixel = composite(pixel, color, mix(inner.alpha, outer.alpha) / 255.0 * alpha * coverage,
                      ditherRow(target, row)[static_cast<std::size_t>(x) & 7U]);
  };
  forEachCovered(
      shape, target,
      [&](std::int64_t row, std::int64_t x0, std::int64_t x1, double coverage) {
        if (inner.alpha != outer.alpha) {
          for (auto x = x0; x < x1; ++x) paint(row, x, coverage);
          return;
        }
        // One alpha: one weight per row, and each channel's scaled source a line in t.
        const auto weight = static_cast<std::uint32_t>(
            std::lround(std::clamp(inner.alpha / 255.0 * alpha * coverage, 0.0, 1.0) * 65536.0));
        if (weight == 0U) return;
        const auto keep = 65536U - weight;
        const auto scale = static_cast<double>(weight);
        const double base[3]{inner.blue * scale, inner.green * scale, inner.red * scale};
        const double slope[3]{step(inner.blue, outer.blue) * scale, step(inner.green, outer.green) * scale,
                              step(inner.red, outer.red) * scale};
        const auto dither = ditherRow(target, row);
        auto* line = pixels.data() + static_cast<std::size_t>(row) * width;
        for (auto x = x0; x < x1; ++x) {
          const auto t = at(row, x);
          const auto source = [&](std::size_t c) {
            return static_cast<std::uint32_t>(base[c] + slope[c] * t + 0.5);
          };
          line[x] = blendRun(line[x], source(0U), source(1U), source(2U), 255U * weight, keep,
                             dither[static_cast<std::size_t>(x) & 7U]);
        }
      },
      paint);
}

}  // namespace seam::native_ui::paint
