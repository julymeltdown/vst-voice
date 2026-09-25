#pragma once

#if defined(__APPLE__)
#include <CoreGraphics/CGColorSpace.h>

namespace seam::native_ui::paint {

// The colour space every SEAM frame is authored in: the design tokens, the character art and the
// CoreGraphics canvas are all sRGB. Present a painted surface in it so the window server
// colour-matches the frame to the display. Device RGB would hand the sRGB values to the display
// unmanaged, which oversaturates every EMO/SCENE colour on a wide-gamut (Display P3) screen.
// Process lifetime; callers must not release it.
[[nodiscard]] CGColorSpaceRef presentationColorSpace() noexcept;

}  // namespace seam::native_ui::paint
#endif
