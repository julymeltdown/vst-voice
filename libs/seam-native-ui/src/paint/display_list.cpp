#include "seam/native_ui/paint/display_list.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <type_traits>
#include <utility>

namespace seam::native_ui::paint {
namespace {

constexpr std::uint64_t kMultiplier = 0x9E3779B97F4A7C15ULL;

std::uint64_t mix(std::uint64_t hash, std::uint64_t word) noexcept {
  hash ^= word;
  hash *= kMultiplier;
  return hash ^ (hash >> 29U);
}

bool emptyRect(ui::Rect r) noexcept { return !(r.width > 0.0) || !(r.height > 0.0); }

ui::Rect unite(ui::Rect a, ui::Rect b) noexcept {
  if (emptyRect(a)) return b;
  if (emptyRect(b)) return a;
  const auto left = std::min(a.x, b.x);
  const auto top = std::min(a.y, b.y);
  return {left, top, std::max(a.right(), b.right()) - left, std::max(a.bottom(), b.bottom()) - top};
}

ui::Rect intersect(ui::Rect a, ui::Rect b) noexcept {
  const auto left = std::max(a.x, b.x);
  const auto top = std::max(a.y, b.y);
  const auto right = std::min(a.right(), b.right());
  const auto bottom = std::min(a.bottom(), b.bottom());
  if (right <= left || bottom <= top) return {};
  return {left, top, right - left, bottom - top};
}

ui::Rect outset(ui::Rect r, double by) noexcept {
  return {r.x - by, r.y - by, r.width + 2.0 * by, r.height + 2.0 * by};
}

ui::Rect pathBounds(const Path& path) noexcept {
  bool any = false;
  double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
  const auto include = [&](ui::Point p) {
    if (!any) {
      left = right = p.x;
      top = bottom = p.y;
      any = true;
      return;
    }
    left = std::min(left, p.x);
    right = std::max(right, p.x);
    top = std::min(top, p.y);
    bottom = std::max(bottom, p.y);
  };
  for (const auto& e : path.elements()) {
    switch (e.verb) {
      case Path::Verb::Move:
      case Path::Verb::Line: include(e.a); break;
      case Path::Verb::Quad: include(e.a); include(e.b); break;
      case Path::Verb::Cubic: include(e.a); include(e.b); include(e.c); break;
      case Path::Verb::Close: break;
    }
  }
  if (!any) return {};
  // A degenerate path (a vertical line) still has a stroke's width.
  return {left, top, std::max(right - left, 1e-3), std::max(bottom - top, 1e-3)};
}

void hashPath(ContentHash& h, const Path& path) {
  h.add(static_cast<std::uint64_t>(path.elements().size()));
  for (const auto& e : path.elements()) {
    h.add(static_cast<std::uint64_t>(e.verb));
    switch (e.verb) {
      case Path::Verb::Move:
      case Path::Verb::Line: h.add(e.a); break;
      case Path::Verb::Quad: h.add(e.a).add(e.b); break;
      case Path::Verb::Cubic: h.add(e.a).add(e.b).add(e.c); break;
      case Path::Verb::Close: break;
    }
  }
}

void hashStops(ContentHash& h, const std::vector<GradientStop>& stops) {
  h.add(static_cast<std::uint64_t>(stops.size()));
  for (const auto& stop : stops) h.add(stop.offset).add(stop.color);
}

void hashStroke(ContentHash& h, const StrokeStyle& style) {
  h.add(style.width).add(style.roundCaps).add(static_cast<std::uint64_t>(style.dash.size()));
  for (const auto d : style.dash) h.add(d);
}

void hashTextStyle(ContentHash& h, const TextStyle& style) {
  h.add(static_cast<std::uint64_t>(style.role)).add(style.size).add(style.tracking)
      .add(static_cast<std::uint64_t>(style.align)).add(style.uppercase);
}

std::size_t index(Layer layer) noexcept { return static_cast<std::size_t>(layer); }

}  // namespace

ContentHash& ContentHash::bytes(const void* data, std::size_t size) noexcept {
  const auto* p = static_cast<const unsigned char*>(data);
  auto hash = mix(value_, static_cast<std::uint64_t>(size));
  while (size >= 8U) {
    std::uint64_t word = 0U;
    std::memcpy(&word, p, 8U);
    hash = mix(hash, word);
    p += 8U;
    size -= 8U;
  }
  if (size > 0U) {
    std::uint64_t word = 0U;
    std::memcpy(&word, p, size);
    hash = mix(hash, word);
  }
  value_ = hash;
  return *this;
}

ContentHash& ContentHash::add(double value) noexcept {
  // Both zeros draw the same, so they hash the same.
  if (value == 0.0) value = 0.0;
  value_ = mix(value_, std::bit_cast<std::uint64_t>(value));
  return *this;
}

ContentHash& ContentHash::add(std::string_view text) noexcept {
  return bytes(text.data(), text.size());
}

ContentHash& ContentHash::add(const void* pointer) noexcept {
  value_ = mix(value_, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pointer)));
  return *this;
}

RecordingCanvas::RecordingCanvas(double width, double height, double scale, Measure measure,
                                 std::uint64_t seed)
    : width_(width), height_(height), scale_(scale), measure_(std::move(measure)) {
  for (auto& layer : layers_) {
    layer.hash = ContentHash{seed}.add(static_cast<std::uint64_t>(&layer - layers_.data())).value();
    layer.ops.reserve(256U);
  }
}

void RecordingCanvas::setLayer(Layer layer, std::string_view item) {
  layer_ = layer;
  item_.assign(item);
}

void RecordingCanvas::save() { saved_.push_back(state_); }

void RecordingCanvas::restore() {
  if (saved_.empty()) return;
  state_ = saved_.back();
  saved_.pop_back();
}

void RecordingCanvas::translate(double dx, double dy) {
  state_.dx += dx;
  state_.dy += dy;
}

void RecordingCanvas::clipRect(ui::Rect r) {
  Clip clip;
  clip.parent = state_.clip;
  clip.rect = {r.x, r.y, std::max(0.0, r.width), std::max(0.0, r.height)};
  clip.dx = state_.dx;
  clip.dy = state_.dy;
  const auto* parent = clip.parent >= 0 ? &clips_[static_cast<std::size_t>(clip.parent)] : nullptr;
  clip.depth = parent != nullptr ? parent->depth + 1U : 1U;
  clip.chain = ContentHash{parent != nullptr ? parent->chain : 0U}
                   .add(false).add(clip.rect).add(clip.dx).add(clip.dy).value();
  const ui::Rect own{clip.rect.x + clip.dx, clip.rect.y + clip.dy, clip.rect.width, clip.rect.height};
  clip.bounds = parent != nullptr ? intersect(parent->bounds, own) : own;
  clips_.push_back(std::move(clip));
  state_.clip = static_cast<std::int32_t>(clips_.size() - 1U);
}

void RecordingCanvas::clipPath(const Path& path) {
  Clip clip;
  clip.parent = state_.clip;
  clip.isPath = true;
  clip.path = path;
  clip.dx = state_.dx;
  clip.dy = state_.dy;
  const auto* parent = clip.parent >= 0 ? &clips_[static_cast<std::size_t>(clip.parent)] : nullptr;
  clip.depth = parent != nullptr ? parent->depth + 1U : 1U;
  ContentHash h{parent != nullptr ? parent->chain : 0U};
  h.add(true);
  hashPath(h, path);
  clip.chain = h.add(clip.dx).add(clip.dy).value();
  auto own = pathBounds(path);
  own.x += clip.dx;
  own.y += clip.dy;
  clip.bounds = parent != nullptr ? intersect(parent->bounds, own) : own;
  clips_.push_back(std::move(clip));
  state_.clip = static_cast<std::int32_t>(clips_.size() - 1U);
}

void RecordingCanvas::setAlpha(double alpha) { state_.alpha = std::clamp(alpha, 0.0, 1.0); }
void RecordingCanvas::setBlend(Blend blend) { state_.blend = blend; }

void RecordingCanvas::setGlow(Color color, double radius) {
  if (glowless_) return;
  state_.glow = true;
  state_.glowColor = color;
  state_.glowRadius = std::max(0.0, radius);
}

void RecordingCanvas::clearGlow() {
  state_.glow = false;
  state_.glowColor = {};
  state_.glowRadius = 0.0;
}

std::uint64_t RecordingCanvas::stateHash(const State& state) const noexcept {
  ContentHash h{state.clip >= 0 ? clips_[static_cast<std::size_t>(state.clip)].chain : 0U};
  h.add(state.dx).add(state.dy).add(state.alpha).add(static_cast<std::uint64_t>(state.blend));
  if (state.glow) h.add(true).add(state.glowColor).add(state.glowRadius);
  return h.value();
}

ui::Rect RecordingCanvas::deviceBounds(ui::Rect local, double extra) const noexcept {
  // One point of anti-aliasing, and a glow's blur reaches about twice its radius.
  auto by = extra + 1.0;
  if (state_.glow) by += 2.0 * state_.glowRadius + 2.0;
  auto r = outset({local.x + state_.dx, local.y + state_.dy, local.width, local.height}, by);
  if (state_.clip >= 0) r = intersect(r, clips_[static_cast<std::size_t>(state_.clip)].bounds);
  return r;
}

void RecordingCanvas::record(Payload payload, std::uint64_t payloadHash, ui::Rect bounds) {
  auto& target = layers_[index(layer_)];
  std::uint32_t item = 0U;
  for (; item < target.items.size(); ++item)
    if (target.items[item].name == item_) break;
  if (item == target.items.size()) target.items.push_back(LayerItem{item_, 0U, {}});
  const auto opHash = mix(payloadHash, stateHash(state_));
  target.hash = mix(target.hash, opHash);
  auto& named = target.items[item];
  named.hash = mix(named.hash, opHash);
  named.bounds = unite(named.bounds, bounds);
  target.ops.push_back(Op{state_, item, bounds, std::move(payload), opHash});
}

void RecordingCanvas::drawRaster(ui::Rect bounds, std::uint64_t contentHash, RasterDrawing drawing) {
  if (!drawing) return;
  const auto h = ContentHash{contentHash}.add(std::string_view{"raster"}).add(bounds).value();
  record(DrawRaster{std::move(drawing)}, h, deviceBounds(bounds, 0.0));
}

void RecordingCanvas::fill(const Path& path, Color color) {
  if (path.empty()) return;
  ContentHash h;
  hashPath(h.add(std::string_view{"fill"}).add(color), path);
  const auto bounds = deviceBounds(pathBounds(path), 0.0);
  record(FillColor{path, color}, h.value(), bounds);
}

void RecordingCanvas::fill(const Path& path, const LinearGradient& gradient) {
  if (path.empty() || gradient.stops.empty()) return;
  ContentHash h;
  h.add(std::string_view{"linear"}).add(gradient.from).add(gradient.to);
  hashStops(h, gradient.stops);
  hashPath(h, path);
  const auto bounds = deviceBounds(pathBounds(path), 0.0);
  record(FillLinear{path, gradient}, h.value(), bounds);
}

void RecordingCanvas::fill(const Path& path, const RadialGradient& gradient) {
  if (path.empty() || gradient.stops.empty()) return;
  ContentHash h;
  h.add(std::string_view{"radial"}).add(gradient.center).add(gradient.radius);
  hashStops(h, gradient.stops);
  hashPath(h, path);
  const auto bounds = deviceBounds(pathBounds(path), 0.0);
  record(FillRadial{path, gradient}, h.value(), bounds);
}

void RecordingCanvas::stroke(const Path& path, Color color, const StrokeStyle& style) {
  if (path.empty()) return;
  ContentHash h;
  hashStroke(h.add(std::string_view{"stroke"}).add(color), style);
  hashPath(h, path);
  const auto bounds = deviceBounds(pathBounds(path), std::max(0.0, style.width) * 0.5);
  record(StrokeColor{path, color, style}, h.value(), bounds);
}

void RecordingCanvas::stroke(const Path& path, const LinearGradient& gradient,
                             const StrokeStyle& style) {
  if (path.empty() || gradient.stops.empty()) return;
  ContentHash h;
  h.add(std::string_view{"stroke-linear"}).add(gradient.from).add(gradient.to);
  hashStops(h, gradient.stops);
  hashStroke(h, style);
  hashPath(h, path);
  const auto bounds = deviceBounds(pathBounds(path), std::max(0.0, style.width) * 0.5);
  record(StrokeLinear{path, gradient, style}, h.value(), bounds);
}

void RecordingCanvas::drawImage(const Image& image, ui::Rect destination, double opacity) {
  if (destination.width <= 0.0 || destination.height <= 0.0) return;
  const auto h = ContentHash{}
                     .add(std::string_view{"image"}).add(static_cast<const void*>(&image))
                     .add(static_cast<std::uint64_t>(image.width()))
                     .add(static_cast<std::uint64_t>(image.height()))
                     .add(destination).add(opacity).value();
  record(DrawImage{&image, {}, destination, opacity, false}, h, deviceBounds(destination, 0.0));
}

void RecordingCanvas::drawImage(const Image& image, ui::Rect source, ui::Rect destination,
                                double opacity) {
  if (destination.width <= 0.0 || destination.height <= 0.0) return;
  const auto h = ContentHash{}
                     .add(std::string_view{"image-crop"}).add(static_cast<const void*>(&image))
                     .add(static_cast<std::uint64_t>(image.width()))
                     .add(static_cast<std::uint64_t>(image.height()))
                     .add(source).add(destination).add(opacity).value();
  record(DrawImage{&image, source, destination, opacity, true}, h, deviceBounds(destination, 0.0));
}

double RecordingCanvas::text(ui::Rect bounds, std::string_view utf8, const TextStyle& style,
                             Color color) {
  if (utf8.empty() || bounds.width <= 1.0 || bounds.height <= 0.0) return 0.0;
  ContentHash h;
  hashTextStyle(h.add(std::string_view{"text"}).add(bounds).add(utf8).add(color), style);
  // The backend clips a line to its bounds widened by 2 points and heightened by 4 on each side.
  const auto area = deviceBounds({bounds.x - 2.0, bounds.y - 4.0, bounds.width + 4.0,
                                  bounds.height + 8.0}, 0.0);
  record(DrawText{bounds, std::string{utf8}, style, color}, h.value(), area);
  return std::min(measure(utf8, style), bounds.width);
}

double RecordingCanvas::measure(std::string_view utf8, const TextStyle& style) {
  return measure_ ? measure_(utf8, style) : 0.0;
}

std::uint64_t RecordingCanvas::layerHash(Layer layer) const noexcept {
  return layers_[index(layer)].hash;
}

std::size_t RecordingCanvas::layerSize(Layer layer) const noexcept {
  return layers_[index(layer)].ops.size();
}

std::vector<LayerItem> RecordingCanvas::items(Layer layer) const { return layers_[index(layer)].items; }

std::vector<RecordingCanvas::OpKey> RecordingCanvas::opKeys(Layer layer) const {
  const auto& ops = layers_[index(layer)].ops;
  std::vector<OpKey> out;
  out.reserve(ops.size());
  for (const auto& op : ops) out.push_back(OpKey{op.hash, op.bounds});
  return out;
}

std::vector<ui::Rect> RecordingCanvas::rasterBounds(Layer layer) const {
  std::vector<ui::Rect> out;
  for (const auto& op : layers_[index(layer)].ops)
    if (std::holds_alternative<DrawRaster>(op.payload) && !emptyRect(op.bounds))
      out.push_back(op.bounds);
  return out;
}

void RecordingCanvas::applyClip(Canvas2D& target, const Clip& clip) const {
  const auto moved = clip.dx != 0.0 || clip.dy != 0.0;
  if (moved) target.translate(clip.dx, clip.dy);
  if (clip.isPath) target.clipPath(clip.path);
  else target.clipRect(clip.rect);
  if (moved) target.translate(-clip.dx, -clip.dy);
}

void RecordingCanvas::replay(Layer layer, Canvas2D& target, RasterCanvas& raster,
                             const ui::Rect* clip, const std::vector<ui::Rect>* touching) const {
  const auto& ops = layers_[index(layer)].ops;
  if (ops.empty()) return;
  // Clips are nested save levels; the drawing attributes live in one more level above them, so a
  // change of attributes never costs a clip and a change of clip resets the attributes.
  target.save();
  if (clip != nullptr) target.clipRect(*clip);
  std::vector<std::int32_t> applied;
  std::vector<std::int32_t> chain;
  bool attributes = false;
  State current{};
  for (const auto& op : ops) {
    // Drawing that cannot reach the clip is skipped outright; its bounds include every stroke,
    // glow and anti-aliased edge it may touch.
    if (clip != nullptr && emptyRect(intersect(op.bounds, *clip))) continue;
    if (touching != nullptr &&
        std::none_of(touching->begin(), touching->end(),
                     [&](const ui::Rect& r) { return !emptyRect(intersect(op.bounds, r)); }))
      continue;
    const auto& s = op.state;
    chain.clear();
    for (auto c = s.clip; c >= 0; c = clips_[static_cast<std::size_t>(c)].parent) chain.push_back(c);
    std::reverse(chain.begin(), chain.end());
    if (chain != applied) {
      if (attributes) {
        target.restore();
        attributes = false;
      }
      std::size_t common = 0U;
      while (common < chain.size() && common < applied.size() && chain[common] == applied[common])
        ++common;
      while (applied.size() > common) {
        target.restore();
        applied.pop_back();
      }
      for (auto k = common; k < chain.size(); ++k) {
        target.save();
        applyClip(target, clips_[static_cast<std::size_t>(chain[k])]);
        applied.push_back(chain[k]);
      }
    }
    if (attributes && (current.dx != s.dx || current.dy != s.dy)) {
      target.restore();
      attributes = false;
    }
    if (!attributes) {
      target.save();
      attributes = true;
      current = State{};
      current.dx = s.dx;
      current.dy = s.dy;
      if (s.dx != 0.0 || s.dy != 0.0) target.translate(s.dx, s.dy);
    }
    if (current.alpha != s.alpha) {
      target.setAlpha(s.alpha);
      current.alpha = s.alpha;
    }
    if (current.blend != s.blend) {
      target.setBlend(s.blend);
      current.blend = s.blend;
    }
    if (current.glow != s.glow || (s.glow && (current.glowColor != s.glowColor ||
                                              current.glowRadius != s.glowRadius))) {
      if (s.glow) target.setGlow(s.glowColor, s.glowRadius);
      else target.clearGlow();
      current.glow = s.glow;
      current.glowColor = s.glowColor;
      current.glowRadius = s.glowRadius;
    }
    std::visit(
        [&](const auto& p) {
          using T = std::decay_t<decltype(p)>;
          if constexpr (std::is_same_v<T, FillColor>) target.fill(p.path, p.color);
          else if constexpr (std::is_same_v<T, FillLinear>) target.fill(p.path, p.gradient);
          else if constexpr (std::is_same_v<T, FillRadial>) target.fill(p.path, p.gradient);
          else if constexpr (std::is_same_v<T, StrokeColor>) target.stroke(p.path, p.color, p.style);
          else if constexpr (std::is_same_v<T, StrokeLinear>)
            target.stroke(p.path, p.gradient, p.style);
          else if constexpr (std::is_same_v<T, DrawImage>) {
            if (p.cropped) target.drawImage(*p.image, p.source, p.destination, p.opacity);
            else target.drawImage(*p.image, p.destination, p.opacity);
          } else if constexpr (std::is_same_v<T, DrawText>)
            static_cast<void>(target.text(p.bounds, p.text, p.style, p.color));
          else if constexpr (std::is_same_v<T, DrawRaster>) {
            // The raster front writes pixels directly: the vector work before it must be on the
            // surface first, which is the documented contract between the two fronts.
            target.flush();
            // The drawing runs against the canvas it is replayed on, not through this recorder,
            // so a glowless recording (High Contrast) keeps its glow off here too.
            if (glowless_) {
              GlowlessCanvas glowless{target};
              p.drawing(glowless, raster);
            } else {
              p.drawing(target, raster);
            }
          }
        },
        op.payload);
  }
  if (attributes) target.restore();
  while (!applied.empty()) {
    target.restore();
    applied.pop_back();
  }
  target.restore();
}

LayerScope::LayerScope(Canvas2D& canvas, Layer layer, std::string_view item)
    : recorder_(dynamic_cast<RecordingCanvas*>(&canvas)) {
  if (recorder_ == nullptr) return;
  previous_ = recorder_->layer();
  previousItem_ = recorder_->item();
  recorder_->setLayer(layer, item);
}

LayerScope::~LayerScope() {
  if (recorder_ != nullptr) recorder_->setLayer(previous_, previousItem_);
}

}  // namespace seam::native_ui::paint
