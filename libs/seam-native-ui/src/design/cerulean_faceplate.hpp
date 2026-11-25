#pragma once

namespace seam::native_ui {
struct RegionWaveform;
namespace paint {
class Canvas2D;
}
}

namespace seam::native_ui::design {
struct DesignTokens;
struct SingLayout;

// The shell owns geometry and render publication. These painters only consume a frame snapshot.
void paintCeruleanFaceplate(paint::Canvas2D& canvas, const DesignTokens& tokens,
                            const SingLayout& layout);
void paintCeruleanWaveform(paint::Canvas2D& canvas, const DesignTokens& tokens,
                           const SingLayout& layout, const RegionWaveform& waveform);
}  // namespace seam::native_ui::design
