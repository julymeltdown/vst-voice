#pragma once

#include "seam/native_ui/pixel_surface.hpp"

#include <array>
#include <cstdint>

namespace seam::native_ui::paint {

// An 8 x 8 ordered-dither matrix as 16-bit thresholds, (index + 0.5) / 64 of one channel step.
// CoreGraphics dithers the gradients it draws; the software gradient fills do the same, so a long
// low-contrast ramp does not step in visible bands.
inline constexpr std::array<std::array<std::uint32_t, 8U>, 8U> kOrderedDither = [] {
  constexpr std::array<std::array<std::uint32_t, 8U>, 8U> index{{
      {0U, 32U, 8U, 40U, 2U, 34U, 10U, 42U},
      {48U, 16U, 56U, 24U, 50U, 18U, 58U, 26U},
      {12U, 44U, 4U, 36U, 14U, 46U, 6U, 38U},
      {60U, 28U, 52U, 20U, 62U, 30U, 54U, 22U},
      {3U, 35U, 11U, 43U, 1U, 33U, 9U, 41U},
      {51U, 19U, 59U, 27U, 49U, 17U, 57U, 25U},
      {15U, 47U, 7U, 39U, 13U, 45U, 5U, 37U},
      {63U, 31U, 55U, 23U, 61U, 29U, 53U, 21U},
  }};
  std::array<std::array<std::uint32_t, 8U>, 8U> thresholds{};
  for (std::size_t y = 0; y < 8U; ++y)
    for (std::size_t x = 0; x < 8U; ++x) thresholds[y][x] = index[y][x] * 1024U + 512U;
  return thresholds;
}();

// A rounded rectangle in a surface's device pixels, rows counted downward from the surface's first
// row. A circle is a square whose radius is half its side.
struct DeviceRoundRect final {
  double left{0.0};
  double top{0.0};
  double right{0.0};
  double bottom{0.0};
  double radius{0.0};
};

// The pixels a fill may write, and where its dither lines up. The dither of a pixel follows its
// global device position (the surface's column 0 and row 0 lie at ditherColumn and ditherRow), so a
// fill drawn whole, in bands or through clip rectangles gives the same pixels.
struct DeviceFillTarget final {
  std::int64_t clipLeft{0};
  std::int64_t clipTop{0};
  std::int64_t clipRight{0};
  std::int64_t clipBottom{0};
  std::int64_t ditherColumn{0};
  std::int64_t ditherRow{0};
};

// Composites the shape over the surface's premultiplied pixels with a colour that varies by row
// only: `top` for rows whose centre lies at or before `gradientTop`, `bottom` at or past
// `gradientBottom`, linearly between (either may be the larger), as an axial gradient that extends
// its end colours is evaluated. Colour and alpha interpolate unpremultiplied, and `alpha` scales
// both stops. Coverage is exact along straight edges and follows the distance to the arc at the
// corners. Every pixel adds its ordered-dither threshold before it is floored.
void fillRoundRectVertical(PixelSurface& surface, const DeviceRoundRect& shape,
                           const DeviceFillTarget& target, Color top, Color bottom, double gradientTop,
                           double gradientBottom, double alpha);

// As fillRoundRectVertical, with the colour running from `inner` at the centre to `outer` at
// `radius` device pixels and beyond, as a radial gradient from a zero-radius start circle that
// extends its end colour is evaluated.
void fillRoundRectRadial(PixelSurface& surface, const DeviceRoundRect& shape,
                         const DeviceFillTarget& target, Color inner, Color outer, double centerX,
                         double centerY, double radius, double alpha);

}  // namespace seam::native_ui::paint
