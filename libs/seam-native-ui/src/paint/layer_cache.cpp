#include "seam/native_ui/paint/layer_cache.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

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

// Whether two rectangles share area: the test a replay uses to pick the calls that meet a rectangle.
bool meets(ui::Rect a, ui::Rect b) noexcept {
  const auto left = std::max(a.x, b.x);
  const auto top = std::max(a.y, b.y);
  return std::min(a.right(), b.right()) > left && std::min(a.bottom(), b.bottom()) > top;
}

struct DeviceRect final {
  std::int64_t x0{0};
  std::int64_t y0{0};
  std::int64_t x1{0};
  std::int64_t y1{0};
  [[nodiscard]] bool empty() const noexcept { return x1 <= x0 || y1 <= y0; }
};

DeviceRect toDevice(ui::Rect r, double scale, std::int64_t width, std::int64_t height) noexcept {
  const auto clampX = [&](double v) { return std::clamp<std::int64_t>(static_cast<std::int64_t>(v), 0, width); };
  const auto clampY = [&](double v) { return std::clamp<std::int64_t>(static_cast<std::int64_t>(v), 0, height); };
  return {clampX(std::floor(r.x * scale)), clampY(std::floor(r.y * scale)),
          clampX(std::ceil(r.right() * scale)), clampY(std::ceil(r.bottom() * scale))};
}

// The bounds a frame must redraw when its calls are compared with the previous frame's: every call
// one frame has and the other has not (as many of a repeated call as one side has more of), and every
// shared call that changed its order against the others (the calls outside a longest run that kept
// its order). Outside those bounds only shared calls paint, in the same order, so the pixels agree.
std::vector<ui::Rect> changedBounds(const std::vector<RecordingCanvas::OpKey>& before,
                                    const std::vector<RecordingCanvas::OpKey>& after) {
  // Each call of the previous frame by content, in drawing order within a content.
  std::vector<std::pair<std::uint64_t, std::size_t>> earlier;
  earlier.reserve(before.size());
  for (std::size_t i = 0U; i < before.size(); ++i) earlier.emplace_back(before[i].hash, i);
  std::sort(earlier.begin(), earlier.end());
  std::vector<std::size_t> taken(earlier.size(), 0U);  // per content run: how many were matched
  std::vector<bool> matched(before.size(), false);
  std::vector<ui::Rect> changed;
  // The shared calls in this frame's order, as positions in the previous frame.
  std::vector<std::size_t> positions;
  std::vector<ui::Rect> sharedBounds;
  positions.reserve(after.size());
  for (const auto& op : after) {
    const auto run = std::lower_bound(earlier.begin(), earlier.end(),
                                      std::pair<std::uint64_t, std::size_t>{op.hash, 0U});
    if (run == earlier.end() || run->first != op.hash) {
      changed.push_back(op.bounds);
      continue;
    }
    const auto first = static_cast<std::size_t>(run - earlier.begin());
    const auto k = taken[first]++;
    if (first + k >= earlier.size() || earlier[first + k].first != op.hash) {
      changed.push_back(op.bounds);
      continue;
    }
    const auto position = earlier[first + k].second;
    matched[position] = true;
    positions.push_back(position);
    sharedBounds.push_back(op.bounds);
  }
  for (std::size_t i = 0U; i < before.size(); ++i)
    if (!matched[i]) changed.push_back(before[i].bounds);
  // A longest run of shared calls in increasing previous positions keeps its order; the rest moved.
  std::vector<std::size_t> tails;      // index into positions of the smallest tail per length
  std::vector<std::size_t> parent(positions.size(), positions.size());
  for (std::size_t i = 0U; i < positions.size(); ++i) {
    const auto slot = std::lower_bound(tails.begin(), tails.end(), positions[i],
                                       [&](std::size_t tail, std::size_t value) {
                                         return positions[tail] < value;
                                       });
    if (slot != tails.begin()) parent[i] = *(slot - 1);
    if (slot == tails.end()) tails.push_back(i);
    else *slot = i;
  }
  std::vector<bool> kept(positions.size(), false);
  for (auto i = tails.empty() ? positions.size() : tails.back(); i < positions.size(); i = parent[i])
    kept[i] = true;
  for (std::size_t i = 0U; i < positions.size(); ++i)
    if (!kept[i]) changed.push_back(sharedBounds[i]);
  return changed;
}

double area(const FrameDamage& damage) noexcept {
  double total = 0.0;
  for (const auto& r : damage.rects) total += r.width * r.height;
  return total;
}

}  // namespace

void LayerCache::invalidate() noexcept {
  valid_.fill(false);
  hasOps_.fill(false);
  hasPrevious_ = false;
  dynamicItems_.clear();
  lastTarget_ = nullptr;
}

void LayerCache::release() noexcept {
  invalidate();
  for (auto& snapshot : snapshots_) snapshot = PixelSurface{};
  for (auto& ops : previousOps_) OpKeys{}.swap(ops);
  std::vector<std::uint32_t>{}.swap(backup_);
  width_ = 0U;
  height_ = 0U;
  scale_ = 0.0;
}

std::size_t LayerCache::bytes() const noexcept {
  std::size_t total = backup_.capacity() * sizeof(std::uint32_t);
  for (const auto& snapshot : snapshots_) total += snapshot.pixels().size() * sizeof(std::uint32_t);
  for (const auto& ops : previousOps_) total += ops.capacity() * sizeof(RecordingCanvas::OpKey);
  return total;
}

void LayerCache::redraw(PixelSurface& dest, const PixelSurface& base, RasterCanvas& raster,
                        const RecordingCanvas& frame, Layer layer, const OpKeys& ops,
                        const std::vector<ui::Rect>& damage, double scale) {
  const auto width = static_cast<std::int64_t>(dest.width());
  const auto height = static_cast<std::int64_t>(dest.height());
  std::vector<DeviceRect> holes;
  for (const auto& r : damage)
    if (const auto d = toDevice(r, scale, width, height); !d.empty()) holes.push_back(d);
  // Where the calls that meet the damage may paint: the damage and each such call's own bounds.
  auto cover = holes;
  for (const auto& op : ops) {
    if (std::none_of(damage.begin(), damage.end(), [&](ui::Rect r) { return meets(op.bounds, r); }))
      continue;
    if (const auto d = toDevice(op.bounds, scale, width, height); !d.empty()) cover.push_back(d);
  }
  if (cover.empty()) return;
  // Horizontal bands between the rectangles' edges, each with its merged spans.
  std::vector<std::int64_t> edges;
  for (const auto& c : cover) {
    edges.push_back(c.y0);
    edges.push_back(c.y1);
  }
  std::sort(edges.begin(), edges.end());
  edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
  const auto spansIn = [](const std::vector<DeviceRect>& rects, std::int64_t y0, std::int64_t y1,
                          std::vector<std::pair<std::int64_t, std::int64_t>>& out) {
    out.clear();
    for (const auto& r : rects)
      if (r.y0 <= y0 && r.y1 >= y1) out.emplace_back(r.x0, r.x1);
    std::sort(out.begin(), out.end());
    std::size_t merged = 0U;
    for (const auto& span : out) {
      if (merged > 0U && span.first <= out[merged - 1U].second) {
        out[merged - 1U].second = std::max(out[merged - 1U].second, span.second);
      } else {
        out[merged++] = span;
      }
    }
    out.resize(merged);
  };
  struct Band final {
    std::int64_t y0;
    std::int64_t y1;
    std::vector<std::pair<std::int64_t, std::int64_t>> spans;
    std::vector<std::pair<std::int64_t, std::int64_t>> holes;
  };
  std::vector<Band> bands;
  std::size_t saved = 0U;
  for (std::size_t k = 0U; k + 1U < edges.size(); ++k) {
    Band band{edges[k], edges[k + 1U], {}, {}};
    spansIn(cover, band.y0, band.y1, band.spans);
    if (band.spans.empty()) continue;
    spansIn(holes, band.y0, band.y1, band.holes);
    for (const auto& span : band.spans)
      saved += static_cast<std::size_t>((span.second - span.first) * (band.y1 - band.y0));
    bands.push_back(std::move(band));
  }
  backup_.resize(saved);
  auto destination = dest.pixels();
  const auto source = base.pixels();
  const auto at = [&](std::int64_t x, std::int64_t y) {
    return static_cast<std::ptrdiff_t>(y * width + x);
  };
  // Keep what the spans hold, and lay the snapshot below under them.
  auto next = backup_.begin();
  for (const auto& band : bands) {
    for (auto y = band.y0; y < band.y1; ++y) {
      for (const auto& [x0, x1] : band.spans) {
        const auto count = x1 - x0;
        next = std::copy_n(destination.begin() + at(x0, y), count, next);
        std::copy_n(source.begin() + at(x0, y), count, destination.begin() + at(x0, y));
      }
    }
  }
  // The calls that meet the damage, unclipped, as a whole frame draws them.
  if (auto canvas = makeCanvas(dest, scale); canvas != nullptr) {
    frame.replay(layer, *canvas, raster, nullptr, &damage);
    canvas->flush();
  }
  // Outside the damage, the spans go back to what they held.
  auto held = backup_.cbegin();
  for (const auto& band : bands) {
    for (auto y = band.y0; y < band.y1; ++y) {
      for (const auto& [x0, x1] : band.spans) {
        auto x = x0;
        for (const auto& [h0, h1] : band.holes) {
          if (h1 <= x || h0 >= x1) continue;
          if (h0 > x) std::copy(held + (x - x0), held + (h0 - x0), destination.begin() + at(x, y));
          x = std::max(x, h1);
        }
        if (x < x1) std::copy(held + (x - x0), held + (x1 - x0), destination.begin() + at(x, y));
        held += x1 - x0;
      }
    }
  }
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
  const auto logicalWidth = static_cast<double>(width) / scale;
  const auto logicalHeight = static_cast<double>(height) / scale;
  const auto halfSurface = logicalWidth * logicalHeight * 0.5;
  const auto snapped = [&](ui::Rect r) { return snapToPixels(r, scale, logicalWidth, logicalHeight); };
  const std::array<std::uint64_t, kSnapshots> wanted{
      background.key, frame.layerHash(Layer::Grid), frame.layerHash(Layer::Content)};
  // What the snapshots composed so far changed: nothing, some rectangles, or everything.
  FrameDamage below;
  for (std::size_t i = 0U; i < kSnapshots; ++i) {
    const auto unchanged = valid_[i] && keys_[i] == wanted[i];
    if (!below.full && below.rects.empty() && unchanged) continue;
    OpKeys ops;
    if (i > 0U) ops = frame.opKeys(static_cast<Layer>(i));
    auto damage = below;
    if (!damage.full && i > 0U && valid_[i] && hasOps_[i - 1U]) {
      if (!unchanged) {
        const auto changed = changedBounds(previousOps_[i - 1U], ops);
        for (const auto& r : changed) damage.add(snapped(r));
      }
      if (!damage.full && area(damage) > halfSurface) damage = FrameDamage::everything();
    } else {
      damage = FrameDamage::everything();
    }
    auto& snapshot = snapshots_[i];
    if (!damage.full) {
      if (!damage.rects.empty()) {
        RasterCanvas raster{snapshot, scale, nullptr};
        redraw(snapshot, snapshots_[i - 1U], raster, frame, static_cast<Layer>(i), ops,
               damage.rects, scale);
        out.rasterized[i] = true;
      }
    } else {
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
      out.rasterized[i] = true;
    }
    below = std::move(damage);
    keys_[i] = wanted[i];
    valid_[i] = true;
    if (i > 0U) {
      previousOps_[i - 1U] = std::move(ops);
      hasOps_[i - 1U] = true;
    }
  }

  auto items = frame.items(Layer::Dynamic);
  auto damage = hasPrevious_ ? below : FrameDamage::everything();
  if (!damage.full) {
    const auto add = [&](ui::Rect r) { damage.add(snapped(r)); };
    std::unordered_map<std::string, const LayerItem*> previous;
    for (const auto& item : dynamicItems_) previous.emplace(item.name, &item);
    for (const auto& item : items) {
      const auto found = previous.find(item.name);
      if (found == previous.end()) {
        add(item.bounds);
        continue;
      }
      if (found->second->hash != item.hash) {
        add(found->second->bounds);
        add(item.bounds);
      }
      previous.erase(found);
    }
    for (const auto& entry : previous) add(entry.second->bounds);
    if (!damage.full && area(damage) > halfSurface) damage = FrameDamage::everything();
  }
  const auto& content = snapshots_[kSnapshots - 1U];
  // The target is redrawn in rectangles only while it still holds the previous frame.
  const auto retained = targetRetained && lastTarget_ == surface.pixels().data();
  if (retained && !damage.full) {
    if (!damage.rects.empty()) {
      redraw(surface, content, target, frame, Layer::Dynamic, frame.opKeys(Layer::Dynamic),
             damage.rects, scale);
      out.rasterized[static_cast<std::size_t>(Layer::Dynamic)] = true;
    }
  } else {
    const auto& pixels = content.pixels();
    std::copy(pixels.begin(), pixels.end(), surface.pixels().begin());
    if (frame.layerSize(Layer::Dynamic) > 0U) {
      if (auto canvas = makeCanvas(surface, scale); canvas != nullptr) {
        frame.replay(Layer::Dynamic, *canvas, target);
        canvas->flush();
      }
    }
    out.rasterized[static_cast<std::size_t>(Layer::Dynamic)] = true;
  }
  out.damage = std::move(damage);
  dynamicItems_ = std::move(items);
  hasPrevious_ = true;
  lastTarget_ = surface.pixels().data();
  return out;
}

}  // namespace seam::native_ui::paint
