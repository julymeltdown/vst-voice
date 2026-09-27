#pragma once

#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/pixel_surface.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace seam::native_ui::paint {

// The frame layers of the redesign plan (section 10), bottom to top. The background is painted
// into its own cached surface; the other three are recorded every frame and rasterized only when
// what they draw changed.
enum class Layer : std::uint8_t { Background = 0, Grid = 1, Content = 2, Dynamic = 3 };
inline constexpr std::size_t kLayerCount = 4U;

// 64-bit FNV-1a over what a layer draws. Images are hashed by identity, so a hash is meaningful
// only inside one process and is never persisted.
class ContentHash final {
public:
  ContentHash() = default;
  explicit ContentHash(std::uint64_t seed) noexcept : value_(seed) {}
  ContentHash& bytes(const void* data, std::size_t size) noexcept;
  ContentHash& add(std::uint64_t value) noexcept { return bytes(&value, sizeof value); }
  ContentHash& add(double value) noexcept;
  ContentHash& add(bool value) noexcept { return add(static_cast<std::uint64_t>(value ? 1U : 0U)); }
  ContentHash& add(std::string_view text) noexcept;
  ContentHash& add(const void* pointer) noexcept;
  ContentHash& add(ui::Point point) noexcept { return add(point.x).add(point.y); }
  ContentHash& add(ui::Rect rect) noexcept {
    return add(rect.x).add(rect.y).add(rect.width).add(rect.height);
  }
  ContentHash& add(Color color) noexcept { return add(static_cast<std::uint64_t>(color.bgra())); }
  [[nodiscard]] std::uint64_t value() const noexcept { return value_; }

private:
  std::uint64_t value_{0xcbf29ce484222325ULL};
};

// Drawing that reaches the frame through the raster front as well as the vector canvas: the
// character package's decoded PPM portraits and mouth sprites, which the vector backend cannot
// address. It is recorded with a hash of everything it depends on and the bounds it may touch, and
// runs at replay against the surface being composed.
using RasterDrawing = std::function<void(Canvas2D& vector, RasterCanvas& raster)>;

// A named part of a layer, for damage: the dynamic layer's playhead, meter, ring and so on. Its
// hash covers only its own drawing, and its bounds everything that drawing may touch (strokes,
// glows and anti-aliasing included).
struct LayerItem final {
  std::string name;
  std::uint64_t hash{0U};
  ui::Rect bounds{};
};

// A Canvas2D that records instead of drawing. Every drawing call lands in the current layer, and
// each recorded call carries the complete canvas state it was issued in (clips, alpha, blend,
// glow), so one layer replays on its own: a clip or glow set up around one layer's drawing never
// appears in, or changes the hash of, another layer. Measuring text asks the supplied function.
class RecordingCanvas final : public Canvas2D {
public:
  using Measure = std::function<double(std::string_view, const TextStyle&)>;

  RecordingCanvas(double width, double height, double scale, Measure measure,
                  std::uint64_t seed = 0U);

  // Where drawing goes from now on. An item name groups dynamic drawing for damage.
  void setLayer(Layer layer, std::string_view item = {});
  [[nodiscard]] Layer layer() const noexcept { return layer_; }
  [[nodiscard]] const std::string& item() const noexcept { return item_; }
  // Drops every setGlow() from now on, exactly as GlowlessCanvas does (High Contrast draws no glow);
  // the recorder itself does it so layer scopes still find the recorder behind the canvas.
  void setGlowless(bool glowless) noexcept { glowless_ = glowless; }

  void drawRaster(ui::Rect bounds, std::uint64_t contentHash, RasterDrawing drawing);

  [[nodiscard]] std::uint64_t layerHash(Layer layer) const noexcept;
  [[nodiscard]] std::size_t layerSize(Layer layer) const noexcept;
  // The layer's named items in first-drawn order.
  [[nodiscard]] std::vector<LayerItem> items(Layer layer) const;
  // The bounds of the layer's raster drawings. The raster front writes pixels without the vector
  // canvas's clip, so a partial composition must restore and redraw each one whole.
  [[nodiscard]] std::vector<ui::Rect> rasterBounds(Layer layer) const;
  // Draws one recorded layer onto target, whose surface the raster front addresses. With a clip,
  // only drawing whose bounds meet it runs, clipped to it; a raster drawing that meets the clip
  // must lie inside it (see rasterBounds).
  void replay(Layer layer, Canvas2D& target, RasterCanvas& raster,
              const ui::Rect* clip = nullptr) const;

  [[nodiscard]] double width() const noexcept override { return width_; }
  [[nodiscard]] double height() const noexcept override { return height_; }
  [[nodiscard]] double scale() const noexcept override { return scale_; }
  void save() override;
  void restore() override;
  void translate(double dx, double dy) override;
  void clipRect(ui::Rect r) override;
  void clipPath(const Path& path) override;
  void setAlpha(double alpha) override;
  void setBlend(Blend blend) override;
  void setGlow(Color color, double radius) override;
  void clearGlow() override;
  void fill(const Path& path, Color color) override;
  void fill(const Path& path, const LinearGradient& gradient) override;
  void fill(const Path& path, const RadialGradient& gradient) override;
  void stroke(const Path& path, Color color, const StrokeStyle& style) override;
  void stroke(const Path& path, const LinearGradient& gradient, const StrokeStyle& style) override;
  void drawImage(const Image& image, ui::Rect destination, double opacity = 1.0) override;
  void drawImage(const Image& image, ui::Rect source, ui::Rect destination,
                 double opacity) override;
  double text(ui::Rect bounds, std::string_view utf8, const TextStyle& style,
              Color color) override;
  [[nodiscard]] double measure(std::string_view utf8, const TextStyle& style) override;
  void flush() override {}

private:
  struct Clip final {
    std::int32_t parent{-1};
    bool isPath{false};
    ui::Rect rect{};
    Path path;
    double dx{0.0};
    double dy{0.0};
    std::uint64_t chain{0U};
    ui::Rect bounds{};  // the intersection of this clip and its parents, in canvas points
    std::uint32_t depth{0U};
  };
  struct State final {
    std::int32_t clip{-1};
    double dx{0.0};
    double dy{0.0};
    double alpha{1.0};
    Blend blend{Blend::Normal};
    bool glow{false};
    Color glowColor{};
    double glowRadius{0.0};
  };
  struct FillColor final { Path path; Color color; };
  struct FillLinear final { Path path; LinearGradient gradient; };
  struct FillRadial final { Path path; RadialGradient gradient; };
  struct StrokeColor final { Path path; Color color; StrokeStyle style; };
  struct StrokeLinear final { Path path; LinearGradient gradient; StrokeStyle style; };
  struct DrawImage final { const Image* image; ui::Rect source; ui::Rect destination; double opacity; bool cropped; };
  struct DrawText final { ui::Rect bounds; std::string text; TextStyle style; Color color; };
  struct DrawRaster final { RasterDrawing drawing; };
  using Payload = std::variant<FillColor, FillLinear, FillRadial, StrokeColor, StrokeLinear,
                               DrawImage, DrawText, DrawRaster>;
  struct Op final {
    State state;
    std::uint32_t item{0U};
    ui::Rect bounds{};
    Payload payload;
  };
  struct LayerRecord final {
    std::vector<Op> ops;
    std::uint64_t hash{0U};
    std::vector<LayerItem> items;
  };

  void record(Payload payload, std::uint64_t payloadHash, ui::Rect bounds);
  [[nodiscard]] std::uint64_t stateHash(const State& state) const noexcept;
  [[nodiscard]] ui::Rect deviceBounds(ui::Rect local, double outset) const noexcept;
  void applyClip(Canvas2D& target, const Clip& clip) const;

  double width_;
  double height_;
  double scale_;
  Measure measure_;
  Layer layer_{Layer::Content};
  std::string item_;
  bool glowless_{false};
  State state_{};
  std::vector<State> saved_;
  std::vector<Clip> clips_;
  std::array<LayerRecord, kLayerCount> layers_{};
};

// Sets a RecordingCanvas's layer for a scope and restores the previous one; does nothing on a canvas
// that draws directly.
class LayerScope final {
public:
  LayerScope(Canvas2D& canvas, Layer layer, std::string_view item = {});
  ~LayerScope();
  LayerScope(const LayerScope&) = delete;
  LayerScope& operator=(const LayerScope&) = delete;

private:
  RecordingCanvas* recorder_{nullptr};
  Layer previous_{Layer::Content};
  std::string previousItem_;
};

}  // namespace seam::native_ui::paint
