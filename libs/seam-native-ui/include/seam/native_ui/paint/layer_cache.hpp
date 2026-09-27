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
};

struct Composition final {
  // Logical points of the target, snapped outward to whole pixels.
  FrameDamage damage;
  // Which layers this frame rasterized; the dynamic layer is drawn on every frame.
  std::array<bool, kLayerCount> rasterized{};
};

// The frame compositor of redesign plan section 10. It keeps cumulative snapshots: the background,
// the background with the grid, and those with the content. A layer is rasterized again only when
// its key changed (the background) or its recorded drawing hashes differently (grid, content), and
// then every snapshot above it is rebuilt from it. The dynamic layer is drawn over the content
// snapshot into the target, so the target's bytes always equal a composition from nothing.
//
// When the caller says the target still holds the previous frame (a presenter's retained surface)
// and nothing below the dynamic layer changed, only the damaged rectangles are restored from the
// content snapshot and redrawn, clipped to themselves. Otherwise the whole target is written.
//
// Damage compares the dynamic layer's named items with the previous frame's: an item whose drawing
// changed, appeared or disappeared damages its old and new bounds. Any change below the dynamic
// layer, a new size or scale, or a first frame damages everything.
class LayerCache final {
public:
  Composition compose(RasterCanvas& target, const BackgroundLayer& background,
                      const RecordingCanvas& frame, bool targetRetained = false);
  // Drops every snapshot's validity (not its memory); the next frame is composed from nothing.
  void invalidate() noexcept;
  // Frees the snapshots as well.
  void release() noexcept;
  [[nodiscard]] std::size_t bytes() const noexcept;

private:
  static constexpr std::size_t kSnapshots = 3U;
  std::array<PixelSurface, kSnapshots> snapshots_{};
  std::array<std::uint64_t, kSnapshots> keys_{};
  std::array<bool, kSnapshots> valid_{};
  std::uint32_t width_{0U};
  std::uint32_t height_{0U};
  double scale_{0.0};
  std::vector<LayerItem> dynamicItems_;
  bool hasPrevious_{false};
  // The pixels the previous frame was composed into, so a retained target is recognised.
  const std::uint32_t* lastTarget_{nullptr};
};

}  // namespace seam::native_ui::paint
