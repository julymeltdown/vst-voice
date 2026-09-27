#include "seam/native_ui/paint/layer_cache.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <string>

namespace seam::native_ui::paint {
namespace {

// Outward to whole device pixels, inside the surface.
ui::Rect snapToPixels(ui::Rect r, double scale, double width, double height) noexcept {
  const auto left = std::max(0.0, std::floor(r.x * scale) / scale);
  const auto top = std::max(0.0, std::floor(r.y * scale) / scale);
  const auto right = std::min(width, std::ceil(r.right() * scale) / scale);
  const auto bottom = std::min(height, std::ceil(r.bottom() * scale) / scale);
  if (right <= left || bottom <= top) return {};
  return {left, top, right - left, bottom - top};
}

bool containsRect(ui::Rect outer, ui::Rect inner) noexcept {
  return inner.x >= outer.x && inner.y >= outer.y && inner.right() <= outer.right() &&
         inner.bottom() <= outer.bottom();
}

// Copies a pixel-aligned logical rectangle of one surface into another of the same size.
void copyRect(const PixelSurface& from, PixelSurface& to, ui::Rect r, double scale) noexcept {
  const auto width = static_cast<std::int64_t>(from.width());
  const auto height = static_cast<std::int64_t>(from.height());
  const auto x0 = std::clamp<std::int64_t>(std::llround(r.x * scale), 0, width);
  const auto y0 = std::clamp<std::int64_t>(std::llround(r.y * scale), 0, height);
  const auto x1 = std::clamp<std::int64_t>(std::llround(r.right() * scale), 0, width);
  const auto y1 = std::clamp<std::int64_t>(std::llround(r.bottom() * scale), 0, height);
  if (x1 <= x0 || y1 <= y0) return;
  const auto source = from.pixels();
  auto target = to.pixels();
  for (auto y = y0; y < y1; ++y) {
    const auto row = static_cast<std::size_t>(y * width);
    std::copy(source.begin() + static_cast<std::ptrdiff_t>(row + static_cast<std::size_t>(x0)),
              source.begin() + static_cast<std::ptrdiff_t>(row + static_cast<std::size_t>(x1)),
              target.begin() + static_cast<std::ptrdiff_t>(row + static_cast<std::size_t>(x0)));
  }
}

}  // namespace

void LayerCache::invalidate() noexcept {
  valid_.fill(false);
  hasPrevious_ = false;
  dynamicItems_.clear();
  lastTarget_ = nullptr;
}

void LayerCache::release() noexcept {
  invalidate();
  for (auto& snapshot : snapshots_) snapshot = PixelSurface{};
  width_ = 0U;
  height_ = 0U;
  scale_ = 0.0;
}

std::size_t LayerCache::bytes() const noexcept {
  std::size_t total = 0U;
  for (const auto& snapshot : snapshots_) total += snapshot.pixels().size() * sizeof(std::uint32_t);
  return total;
}

Composition LayerCache::compose(RasterCanvas& target, const BackgroundLayer& background,
                                const RecordingCanvas& frame, bool targetRetained) {
  Composition out;
  auto& surface = target.surface();
  const auto scale = target.scale();
  const auto width = surface.width();
  const auto height = surface.height();
  // A text capture observes every line the frame draws, and a cached layer draws none: while one is
  // alive, every layer is rasterized again (to the same pixels) so the capture sees the whole frame.
  if (width != width_ || height != height_ || scale != scale_ || ScopedTextCapture::active()) {
    invalidate();
    width_ = width;
    height_ = height;
    scale_ = scale;
  }
  const std::array<std::uint64_t, kSnapshots> wanted{
      background.key, frame.layerHash(Layer::Grid), frame.layerHash(Layer::Content)};
  bool below = false;
  for (std::size_t i = 0U; i < kSnapshots; ++i) {
    if (!below && valid_[i] && keys_[i] == wanted[i]) continue;
    below = true;
    auto& snapshot = snapshots_[i];
    if ((snapshot.width() != width || snapshot.height() != height) &&
        !snapshot.resize(width, height)) {
      release();
      surface.clear(background.clear);
      out.damage = FrameDamage::everything();
      out.rasterized.fill(true);
      return out;
    }
    if (i == 0U) {
      snapshot.clear(background.clear);
    } else {
      const auto& lower = snapshots_[i - 1U].pixels();
      std::copy(lower.begin(), lower.end(), snapshot.pixels().begin());
    }
    auto canvas = makeCanvas(snapshot, scale);
    if (canvas != nullptr) {
      RasterCanvas raster{snapshot, scale, nullptr};
      if (i == 0U) {
        if (background.paint) background.paint(*canvas);
      } else {
        frame.replay(static_cast<Layer>(i), *canvas, raster);
      }
      canvas->flush();
    }
    keys_[i] = wanted[i];
    valid_[i] = true;
    out.rasterized[i] = true;
  }

  auto items = frame.items(Layer::Dynamic);
  const auto logicalWidth = static_cast<double>(width) / scale;
  const auto logicalHeight = static_cast<double>(height) / scale;
  const auto snapped = [&](ui::Rect r) { return snapToPixels(r, scale, logicalWidth, logicalHeight); };
  if (!hasPrevious_ || below) {
    out.damage = FrameDamage::everything();
  } else {
    const auto damage = [&](ui::Rect r) { out.damage.add(snapped(r)); };
    std::unordered_map<std::string, const LayerItem*> previous;
    for (const auto& item : dynamicItems_) previous.emplace(item.name, &item);
    for (const auto& item : items) {
      const auto found = previous.find(item.name);
      if (found == previous.end()) {
        damage(item.bounds);
        continue;
      }
      if (found->second->hash != item.hash) {
        damage(found->second->bounds);
        damage(item.bounds);
      }
      previous.erase(found);
    }
    for (const auto& entry : previous) damage(entry.second->bounds);
  }

  // A partial composition needs a target that still holds the previous frame, nothing changed below
  // the dynamic layer, and a whole-number scale, so a snapped rectangle is an exact pixel clip.
  const auto partial = targetRetained && !out.damage.full && lastTarget_ == surface.pixels().data() &&
                       scale == std::round(scale);
  if (partial) {
    // The raster front ignores clips: any raster drawing a damaged rectangle meets is restored and
    // redrawn whole, which may grow the damage until no rectangle cuts through one.
    const auto rasters = frame.rasterBounds(Layer::Dynamic);
    for (bool grew = true; grew && !out.damage.full;) {
      grew = false;
      for (const auto& raster : rasters) {
        const auto r = snapped(raster);
        for (const auto& rect : out.damage.rects) {
          if (rect.intersects(r) && !containsRect(rect, r)) {
            out.damage.add(r);
            grew = true;
            break;
          }
        }
        if (grew) break;
      }
    }
  }
  const auto& contentSnapshot = snapshots_[kSnapshots - 1U];
  if (partial && !out.damage.full) {
    if (!out.damage.rects.empty()) {
      for (const auto& rect : out.damage.rects) copyRect(contentSnapshot, surface, rect, scale);
      if (auto canvas = makeCanvas(surface, scale); canvas != nullptr) {
        for (const auto& rect : out.damage.rects) frame.replay(Layer::Dynamic, *canvas, target, &rect);
        canvas->flush();
      }
      out.rasterized[static_cast<std::size_t>(Layer::Dynamic)] = true;
    }
  } else {
    const auto& content = contentSnapshot.pixels();
    std::copy(content.begin(), content.end(), surface.pixels().begin());
    if (frame.layerSize(Layer::Dynamic) > 0U) {
      if (auto canvas = makeCanvas(surface, scale); canvas != nullptr) {
        frame.replay(Layer::Dynamic, *canvas, target);
        canvas->flush();
      }
    }
    out.rasterized[static_cast<std::size_t>(Layer::Dynamic)] = true;
  }
  dynamicItems_ = std::move(items);
  hasPrevious_ = true;
  lastTarget_ = surface.pixels().data();
  return out;
}

}  // namespace seam::native_ui::paint
