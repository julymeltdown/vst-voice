#pragma once

#include "seam/native_ui/design/design_tokens.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/ui/geometry.hpp"

#include <cstdint>
#include <span>

namespace seam::native_ui::design {

// The procedural background wash, rasterized in software: each look's two radial glows over the
// canvas colour, then EMO's ink strands or SCENE's sparkles from a seeded, cached geometry.
// `band` holds device rows [top, top + band.height()) of a surface fullHeight rows tall at the
// given scale, and is cleared to the canvas colour first. Every row depends only on its own
// position, so bands may be painted concurrently and any banding gives the same pixels.
void paintBackgroundWash(PixelSurface& band, double scale, std::uint32_t top,
                         std::uint32_t fullHeight, const DesignTokens& tokens);

// A glass panel's translucent fill: a rounded rectangle whose colour runs from `top` at its upper
// edge to `bottom` at its lower edge, composited at `opacity`. Logical coordinates.
struct GlassPanelFill final {
  ui::Rect rect;
  double radius{0.0};
  Color top;
  Color bottom;
  double opacity{1.0};
};

// Composites the fills, in order, over one band as paintBackgroundWash describes it. Coverage is
// exact along straight edges and sampled 8 x 8 along the corner arcs. The colour of a row is the
// gradient's value at the row's pixel centres, as a vertical axial gradient is evaluated. Wholly
// covered pixels are dithered with an 8 x 8 ordered matrix, as CoreGraphics dithers the gradients
// it draws, so a long low-contrast ramp shows no bands. Every pixel depends only on its own device
// position, so any banding gives the same pixels.
void paintGlassPanelFills(PixelSurface& band, double scale, std::uint32_t top,
                          std::span<const GlassPanelFill> fills);

}  // namespace seam::native_ui::design
