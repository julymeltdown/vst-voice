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
  // Beyond this many rectangles the two whose union adds the least area are merged, until this many
  // remain: a presenter pays a fixed cost per rectangle, and a handful of strips is what a playback
  // frame produces. Two far-apart strips stay two strips.
  static constexpr std::size_t kMaximumRects = 8U;

  bool full{false};
  std::vector<ui::Rect> rects;

  [[nodiscard]] static FrameDamage everything() { return FrameDamage{true, {}}; }
  [[nodiscard]] static FrameDamage nothing() { return FrameDamage{}; }
  [[nodiscard]] bool empty() const noexcept { return !full && rects.empty(); }

  // Adds a rectangle, merging it with any rectangle it touches.
  void add(ui::Rect r) {
    if (full || r.width <= 0.0 || r.height <= 0.0) return;
    insert(r);
    while (rects.size() > kMaximumRects) {
      std::size_t first = 0U;
      std::size_t second = 1U;
      double cheapest = -1.0;
      for (std::size_t i = 0U; i < rects.size(); ++i) {
        for (std::size_t j = i + 1U; j < rects.size(); ++j) {
          const auto merged = unite(rects[i], rects[j]);
          const auto added = area(merged) - area(rects[i]) - area(rects[j]);
          if (cheapest < 0.0 || added < cheapest) {
            cheapest = added;
            first = i;
            second = j;
          }
        }
      }
      const auto merged = unite(rects[first], rects[second]);
      rects.erase(rects.begin() + static_cast<std::ptrdiff_t>(second));
      rects.erase(rects.begin() + static_cast<std::ptrdiff_t>(first));
      insert(merged);
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

  [[nodiscard]] static double area(ui::Rect r) noexcept { return r.width * r.height; }

private:
  // Adds r, merged with every rectangle it touches (transitively).
  void insert(ui::Rect r) {
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
  }
};

}  // namespace seam::native_ui
