#pragma once

#include "seam/ui/geometry.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace seam::native_ui {

// What a painted frame changed, in logical points of the surface it was painted into. A presenter
// that keeps its previous frame on screen only has to present these rectangles; `full` means the
// whole surface changed (first frame, resize, a cached layer below the dynamic one was repainted).
// An empty damage means the frame is identical to the previous one.
struct FrameDamage final {
  // Beyond this many rectangles the damage collapses to their bounding rectangle: a presenter pays a
  // fixed cost per rectangle, and a handful of strips is what a playback frame produces.
  static constexpr std::size_t kMaximumRects = 8U;

  bool full{false};
  std::vector<ui::Rect> rects;

  [[nodiscard]] static FrameDamage everything() { return FrameDamage{true, {}}; }
  [[nodiscard]] static FrameDamage nothing() { return FrameDamage{}; }
  [[nodiscard]] bool empty() const noexcept { return !full && rects.empty(); }

  // Adds a rectangle, merging it with any rectangle it touches.
  void add(ui::Rect r) {
    if (full || r.width <= 0.0 || r.height <= 0.0) return;
    for (bool merged = true; merged;) {
      merged = false;
      for (auto it = rects.begin(); it != rects.end(); ++it) {
        if (it->x <= r.right() && r.x <= it->right() && it->y <= r.bottom() && r.y <= it->bottom()) {
          r = unite(*it, r);
          rects.erase(it);
          merged = true;
          break;
        }
      }
    }
    rects.push_back(r);
    if (rects.size() > kMaximumRects) {
      auto bounds = rects.front();
      for (const auto& rect : rects) bounds = unite(bounds, rect);
      rects.assign(1U, bounds);
    }
  }

  void add(const FrameDamage& other) {
    if (other.full) {
      full = true;
      rects.clear();
      return;
    }
    for (const auto& rect : other.rects) add(rect);
  }

  [[nodiscard]] static ui::Rect unite(ui::Rect a, ui::Rect b) noexcept {
    const auto left = std::min(a.x, b.x);
    const auto top = std::min(a.y, b.y);
    return {left, top, std::max(a.right(), b.right()) - left, std::max(a.bottom(), b.bottom()) - top};
  }
};

}  // namespace seam::native_ui
