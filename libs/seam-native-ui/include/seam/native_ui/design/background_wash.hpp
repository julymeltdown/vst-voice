#pragma once

#include "seam/native_ui/design/design_tokens.hpp"
#include "seam/native_ui/pixel_surface.hpp"

#include <cstdint>

namespace seam::native_ui::design {

// The procedural background wash, rasterized in software: each look's two radial glows over the
// canvas colour, then EMO's ink strands or SCENE's sparkles from a seeded, cached geometry.
// `band` holds device rows [top, top + band.height()) of a surface fullHeight rows tall at the
// given scale, and is cleared to the canvas colour first. Every row depends only on its own
// position, so bands may be painted concurrently and any banding gives the same pixels.
void paintBackgroundWash(PixelSurface& band, double scale, std::uint32_t top,
                         std::uint32_t fullHeight, const DesignTokens& tokens);

}  // namespace seam::native_ui::design
