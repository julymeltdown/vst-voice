#pragma once

#include "seam/native_ui/pixel_surface.hpp"
#include "seam/ui/geometry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
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

// The faces a painter may ask for. Each maps to a bundled face (assets/fonts) and falls back to a
// system face when that face is absent or refused: Ui* to the system font at the weight, Mono to the
// system monospaced font, Display (EMO's condensed display) to a condensed system face and
// DisplayRounded (SCENE's rounded display) to the system's rounded design.
enum class FontRole : std::uint8_t { Ui, UiMedium, UiSemibold, UiBold, Mono, Display, DisplayRounded };
inline constexpr std::size_t kFontRoleCount = 7U;
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
// A backend image from decoded straight-alpha pixels (a QOI or PPM the character package ships),
// premultiplied once here so drawing never pays for it. Nothing without a vector backend.
[[nodiscard]] std::shared_ptr<const Image> imageFromPixels(const PixelSurface& pixels);

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

// Forwards every call to another canvas except glow, which it drops. High Contrast paints through
// it (plan section 4.2: textures and glow off), so no halo softens an edge or a glyph.
class GlowlessCanvas final : public Canvas2D {
public:
  explicit GlowlessCanvas(Canvas2D& inner) noexcept : inner_(inner) {}
  [[nodiscard]] double width() const noexcept override { return inner_.width(); }
  [[nodiscard]] double height() const noexcept override { return inner_.height(); }
  [[nodiscard]] double scale() const noexcept override { return inner_.scale(); }
  void save() override { inner_.save(); }
  void restore() override { inner_.restore(); }
  void translate(double dx, double dy) override { inner_.translate(dx, dy); }
  void clipRect(ui::Rect r) override { inner_.clipRect(r); }
  void clipPath(const Path& path) override { inner_.clipPath(path); }
  void setAlpha(double alpha) override { inner_.setAlpha(alpha); }
  void setBlend(Blend blend) override { inner_.setBlend(blend); }
  void setGlow(Color, double) override {}
  void clearGlow() override { inner_.clearGlow(); }
  void fill(const Path& path, Color color) override { inner_.fill(path, color); }
  void fill(const Path& path, const LinearGradient& gradient) override {
    inner_.fill(path, gradient);
  }
  void fill(const Path& path, const RadialGradient& gradient) override {
    inner_.fill(path, gradient);
  }
  void stroke(const Path& path, Color color, const StrokeStyle& style) override {
    inner_.stroke(path, color, style);
  }
  void stroke(const Path& path, const LinearGradient& gradient, const StrokeStyle& style) override {
    inner_.stroke(path, gradient, style);
  }
  void drawImage(const Image& image, ui::Rect destination, double opacity = 1.0) override {
    inner_.drawImage(image, destination, opacity);
  }
  void drawImage(const Image& image, ui::Rect source, ui::Rect destination,
                 double opacity) override {
    inner_.drawImage(image, source, destination, opacity);
  }
  double text(ui::Rect bounds, std::string_view utf8, const TextStyle& style,
              Color color) override {
    return inner_.text(bounds, utf8, style, color);
  }
  [[nodiscard]] double measure(std::string_view utf8, const TextStyle& style) override {
    return inner_.measure(utf8, style);
  }
  void flush() override { inner_.flush(); }

private:
  Canvas2D& inner_;
};

// One line of text a canvas was asked to draw, as the text engine measured it. Rectangles are in
// the canvas's own logical coordinates with the translation in effect applied: bounds is the box
// the painter gave, ink the line as drawn (after any elision, from ascent to descent), and clip the
// clip in force around the call, which the ink must stay inside to be seen whole. naturalWidth is
// the measured width of the whole string, and elided says the line was drawn truncated with an
// ellipsis because naturalWidth exceeded the bounds.
struct TextRecord final {
  ui::Rect bounds;
  ui::Rect ink;
  ui::Rect clip;
  std::string text;
  double naturalWidth{0.0};
  bool elided{false};
};

// Test and evidence hook: while one is alive, every Canvas2D::text call on this thread is also
// recorded here. Scopes nest; the innermost one receives the records. Painting is unchanged.
class ScopedTextCapture final {
public:
  ScopedTextCapture() noexcept;
  ~ScopedTextCapture();
  ScopedTextCapture(const ScopedTextCapture&) = delete;
  ScopedTextCapture& operator=(const ScopedTextCapture&) = delete;
  [[nodiscard]] const std::vector<TextRecord>& records() const noexcept { return records_; }
  void clear() noexcept { records_.clear(); }
  // Called by a canvas backend for each drawn line; a no-op when no capture is alive.
  static void record(TextRecord record);
  [[nodiscard]] static bool active() noexcept;

private:
  std::vector<TextRecord> records_;
  ScopedTextCapture* previous_{nullptr};
};

[[nodiscard]] bool vectorBackendAvailable() noexcept;

// Bytes held by the backend's shared cache of small glow sprites (bounded, rebuilt on use).
[[nodiscard]] std::size_t glowSpriteCacheBytes() noexcept;

// Test and evidence hook: while one is alive on this thread, every glow is drawn at full resolution
// (the backend's own shadow), so a test can measure the half-resolution glow against it.
class ScopedFullResolutionGlow final {
public:
  ScopedFullResolutionGlow() noexcept;
  ~ScopedFullResolutionGlow();
  ScopedFullResolutionGlow(const ScopedFullResolutionGlow&) = delete;
  ScopedFullResolutionGlow& operator=(const ScopedFullResolutionGlow&) = delete;
  [[nodiscard]] static bool active() noexcept;

private:
  bool previous_{false};
};

// While one is alive on this thread, canvases made on it may draw at the same time as canvases on
// other threads (each on its own surface). CoreGraphics' own drawing of translucent solid colours
// is not safe to run on several threads at once (its anti-aliased edges come out differently), so
// such a canvas draws them as opaque coverage in an alpha-only mask, which is, and composites the
// colour itself. Gradients, images, text and alpha-only masks are drawn as usual.
class ScopedConcurrentCanvas final {
public:
  ScopedConcurrentCanvas() noexcept;
  ~ScopedConcurrentCanvas();
  ScopedConcurrentCanvas(const ScopedConcurrentCanvas&) = delete;
  ScopedConcurrentCanvas& operator=(const ScopedConcurrentCanvas&) = delete;
  [[nodiscard]] static bool active() noexcept;

private:
  bool previous_{false};
};
// True when every character of utf8 draws with a real glyph, from the face the style names or from
// its fallback, and never with the platform's last-resort placeholder: the check that a language's
// script is covered (Hangul falls back to Apple SD Gothic Neo in every role). False where there is
// no vector backend.
[[nodiscard]] bool textRenderable(std::string_view utf8, const TextStyle& style);
// Contents/Resources of the bundle whose binary contains this code (the app, or the plug-in inside
// a host), or empty when it cannot be determined.
[[nodiscard]] std::filesystem::path codeBundleResources();

// The bundled UI faces (assets/fonts, SIL OFL). Registration is process-local
// (kCTFontManagerScopeProcess), so neither the app nor a plug-in inside a host changes the fonts
// any other process sees. A face is registered only when its size and SHA-256 match manifest.json;
// a missing, altered or unlisted face is refused and its roles draw with the system fallback.
struct BundledFonts final {
  // Where the faces were read from; empty when none was found or SEAM_UI_FONTS=system.
  std::filesystem::path directory;
  // PostScript names of the faces in use, per role; empty for a role on its system fallback.
  std::array<std::string, kFontRoleCount> face{};
  // One line per face that was not used, with the reason.
  std::vector<std::string> refused;
};
// Finds the faces: SEAM_UI_FONTS (a directory, or "system" for none), fonts/ in this code's bundle
// Resources, then the source tree in development builds. Empty when none has a manifest.
[[nodiscard]] std::filesystem::path locateBundledFonts();
// Verifies and registers the faces in a directory for this process and returns what it used. Safe
// to call again: a face this process already registered is reused.
[[nodiscard]] BundledFonts registerBundledFonts(const std::filesystem::path& directory);
// The process's faces: located and registered on first use (the shell's activation, or the first
// text drawn), then fixed for the life of the process.
const BundledFonts& bundledFonts();
// The PostScript name of the face a role draws with at the moment (bundled or fallback).
[[nodiscard]] std::string fontFaceName(FontRole role);
// The resolved file behind that face, when CoreText exposes it.
[[nodiscard]] std::filesystem::path fontFaceFile(FontRole role);

}  // namespace seam::native_ui::paint
