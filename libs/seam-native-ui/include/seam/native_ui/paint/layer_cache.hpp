#pragma once

#include "seam/native_ui/frame_damage.hpp"
#include "seam/native_ui/paint/display_list.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace seam::native_ui::paint {

// The background layer: everything it depends on folded into one key (window size, scale, look,
// contrast, panel geometry, artwork), the colour the surface starts from, and its painter.
struct BackgroundLayer final {
  std::uint64_t key{0U};
  Color clear{};
  std::function<void(Canvas2D&)> paint;
  // Paint the base into one device-pixel band before replaying vector chrome. top is the
  // band's first device row in the full surface; fullHeight is the full device height.
  std::function<void(PixelSurface&, double scale, std::uint32_t top,
                     std::uint32_t fullHeight)> paintBase;
  // Only set when paintBase overwrites every pixel of the band, including its opaque base colour.
  // Partial or no-op base painters still need the normal clear before vector replay.
  bool paintBaseOverwritesBand{false};
};

struct Composition final {
  // Logical points of the target, snapped outward to whole pixels.
  FrameDamage damage;
  // Which layers this frame rasterized; the dynamic layer is drawn on every frame.
  std::array<bool, kLayerCount> rasterized{};
};

// The frame compositor of redesign plan section 10. A cold frame keeps its content snapshot and
// builds intermediate cumulative snapshots on the first later grid/content edit. Once built, a
// layer is rasterized again only when its key changes (the background) or its recorded drawing
// hashes differently (grid, content). The dynamic layer is drawn over the content snapshot into
// the target, so the target's bytes always
// equal a composition from nothing.
//
// A changed grid or content layer is compared with the previous frame's call by call: a call that
// changed, appeared or disappeared damages its old and new bounds, and only the damage is redrawn
// (with every snapshot above it, in the same rectangles). The dynamic layer's named items are
// compared the same way. A new background, a new size or scale, a first frame, calls that changed
// their order, or damage over half the surface redraw everything.
//
// A damaged rectangle is redrawn exactly as a whole frame draws it: the snapshot below is restored
// under every call that meets the rectangle, those calls run unclipped (a clip changes how the
// backend rasterizes an edge), and whatever they painted outside the rectangle is put back. The
// target is redrawn in rectangles only when the caller says it still holds the previous frame (a
// presenter's retained surface); otherwise it is written whole.
class LayerCache final {
public:
  Composition compose(RasterCanvas& target, const BackgroundLayer& background,
                      const RecordingCanvas& frame, bool targetRetained = false);
  // Drops composition validity. L0's pixels remain reusable while its complete background key
  // matches, so an explicit full-frame invalidation need not repaint unchanged artwork.
  void invalidate() noexcept;
  // Drops L0's retained pixels as well; the next composition runs its painter.
  void invalidateBackground() noexcept;
  // Frees the snapshots as well.
  void release() noexcept;
  [[nodiscard]] std::size_t bytes() const noexcept;
  // The previous composed background, grid and content. A short shell transition may snapshot it
  // before changing workspace or look; the caller never mutates this cache-owned surface.
  [[nodiscard]] const PixelSurface* contentSnapshot() const noexcept {
    return valid_[2U] ? &snapshots_[2U] : nullptr;
  }

private:
  static constexpr std::size_t kSnapshots = 3U;
  using OpKeys = std::vector<RecordingCanvas::OpKey>;
  // Restores base under the damage (and under every call that meets it) and replays those calls of
  // layer onto dest, then puts back everything outside the damage.
  void redraw(PixelSurface& dest, const PixelSurface& base, RasterCanvas& raster,
              const RecordingCanvas& frame, Layer layer, const OpKeys& ops,
              const std::vector<ui::Rect>& damage, double scale);
  std::array<PixelSurface, kSnapshots> snapshots_{};
  // L0 pixels remain reusable after an explicit full-frame invalidation when its key is unchanged.
  std::uint64_t backgroundSnapshotKey_{0U};
  bool backgroundSnapshotValid_{false};
  bool materializeIntermediate_{false};
  std::array<std::uint64_t, kSnapshots> keys_{};
  std::array<bool, kSnapshots> valid_{};
  // The previous frame's calls of the grid and content layers, when their snapshot holds them.
  std::array<OpKeys, kSnapshots - 1U> previousOps_{};
  std::array<bool, kSnapshots - 1U> hasOps_{};
  std::vector<std::uint32_t> backup_;
  std::uint32_t width_{0U};
  std::uint32_t height_{0U};
  double scale_{0.0};
  std::vector<LayerItem> dynamicItems_;
  bool hasPrevious_{false};
  // The pixels the previous frame was composed into, so a retained target is recognised.
  const std::uint32_t* lastTarget_{nullptr};
};

}  // namespace seam::native_ui::paint
