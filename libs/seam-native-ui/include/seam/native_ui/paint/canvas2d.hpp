#pragma once

#include "seam/native_ui/pixel_surface.hpp"
#include "seam/ui/geometry.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace seam::native_ui::paint {

// A resolution-independent path in logical points.
class Path final {
public:
  enum class Verb : std::uint8_t { Move, Line, Quad, Cubic, Close };
  struct Element final {
    Verb verb{Verb::Move};
    ui::Point a, b, c;
  };

  Path& moveTo(ui::Point p);
  Path& lineTo(ui::Point p);
  Path& quadTo(ui::Point control, ui::Point p);
  Path& cubicTo(ui::Point c1, ui::Point c2, ui::Point p);
  // Clockwise on screen (y down) when sweep is positive.
  Path& arc(ui::Point center, double radius, double startRadians, double sweepRadians);
  Path& close();

  [[nodiscard]] static Path rect(ui::Rect r);
  [[nodiscard]] static Path roundedRect(ui::Rect r, double radius);
  [[nodiscard]] static Path circle(ui::Point center, double radius);
  [[nodiscard]] static Path capsule(ui::Rect r);

  [[nodiscard]] const std::vector<Element>& elements() const noexcept { return elements_; }
  [[nodiscard]] bool empty() const noexcept { return elements_.empty(); }

private:
  std::vector<Element> elements_;
};

struct GradientStop final {
  double offset{0.0};
  Color color;
};

struct LinearGradient final {
  ui::Point from, to;
  std::vector<GradientStop> stops;
};

struct RadialGradient final {
  ui::Point center;
  double radius{1.0};
  std::vector<GradientStop> stops;
};

struct StrokeStyle final {
  StrokeStyle() = default;
  explicit StrokeStyle(double strokeWidth, bool round = true, std::vector<double> dashPattern = {})
      : width{strokeWidth}, roundCaps{round}, dash{std::move(dashPattern)} {}

  double width{1.0};
  bool roundCaps{true};
  std::vector<double> dash;
};

enum class Blend : std::uint8_t { Normal, Add, Screen };

enum class FontRole : std::uint8_t { Ui, UiMedium, UiSemibold, UiBold, Mono, Display };
enum class TextAlign : std::uint8_t { Left, Center, Right };

struct TextStyle final {
  FontRole role{FontRole::Ui};
  double size{13.0};
  double tracking{0.0};
  TextAlign align{TextAlign::Left};
  bool uppercase{false};
};

// A decoded, premultiplied image owned by the backend. Loading validates dimensions and bytes
// before decoding into memory.
class Image {
public:
  virtual ~Image() = default;
  [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;
  [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;
};

struct ImageLimits final {
  std::uint64_t maximumEncodedBytes{8ULL * 1024ULL * 1024ULL};
  std::uint64_t maximumPixels{4096ULL * 4096ULL};
};

[[nodiscard]] std::shared_ptr<const Image> loadImage(const std::filesystem::path& path,
                                                     ImageLimits limits = {});

// Anti-aliased vector drawing in logical top-left coordinates over an opaque PixelSurface.
// The legacy RasterCanvas may draw into the same surface between flush() calls.
class Canvas2D {
public:
  virtual ~Canvas2D() = default;

  [[nodiscard]] virtual double width() const noexcept = 0;
  [[nodiscard]] virtual double height() const noexcept = 0;
  [[nodiscard]] virtual double scale() const noexcept = 0;

  virtual void save() = 0;
  virtual void restore() = 0;
  virtual void translate(double dx, double dy) = 0;
  virtual void clipRect(ui::Rect r) = 0;
  virtual void clipPath(const Path& path) = 0;
  virtual void setAlpha(double alpha) = 0;
  virtual void setBlend(Blend blend) = 0;
  // Everything drawn until the next restore() also casts a soft glow of this color.
  virtual void setGlow(Color color, double radius) = 0;
  virtual void clearGlow() = 0;

  virtual void fill(const Path& path, Color color) = 0;
  virtual void fill(const Path& path, const LinearGradient& gradient) = 0;
  virtual void fill(const Path& path, const RadialGradient& gradient) = 0;
  virtual void stroke(const Path& path, Color color, const StrokeStyle& style) = 0;
  virtual void stroke(const Path& path, const LinearGradient& gradient,
                      const StrokeStyle& style) = 0;
  virtual void drawImage(const Image& image, ui::Rect destination, double opacity = 1.0) = 0;
  // Draws the source rectangle (in image pixels) of the image into destination.
  virtual void drawImage(const Image& image, ui::Rect source, ui::Rect destination,
                         double opacity) = 0;
  // Draws single-line text vertically centered in bounds; truncates with an ellipsis. Returns the
  // drawn width in logical points.
  virtual double text(ui::Rect bounds, std::string_view utf8, const TextStyle& style,
                      Color color) = 0;
  [[nodiscard]] virtual double measure(std::string_view utf8, const TextStyle& style) = 0;
  // Completes pending drawing so the surface bytes are current.
  virtual void flush() = 0;
};

// Null when the platform has no vector backend (Windows and X11 builds stay on the legacy painter).
[[nodiscard]] std::unique_ptr<Canvas2D> makeCanvas(PixelSurface& surface, double scale);
[[nodiscard]] bool vectorBackendAvailable() noexcept;
// Contents/Resources of the bundle whose binary contains this code (the app, or the plug-in inside
// a host), or empty when it cannot be determined.
[[nodiscard]] std::filesystem::path codeBundleResources();

}  // namespace seam::native_ui::paint
