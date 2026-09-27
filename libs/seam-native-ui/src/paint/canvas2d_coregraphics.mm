#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/paint/presentation_color.hpp"

#import <AppKit/AppKit.h>
#import <Accelerate/Accelerate.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <ImageIO/ImageIO.h>

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace seam::native_ui::paint {
namespace {

template <typename T>
class CfRef final {
public:
  CfRef() = default;
  explicit CfRef(T value) noexcept : value_(value) {}
  CfRef(const CfRef&) = delete;
  CfRef& operator=(const CfRef&) = delete;
  CfRef(CfRef&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
  CfRef& operator=(CfRef&& other) noexcept {
    if (this != &other) {
      reset();
      value_ = std::exchange(other.value_, nullptr);
    }
    return *this;
  }
  ~CfRef() { reset(); }
  void reset() noexcept {
    if (value_ != nullptr) CFRelease(value_);
    value_ = nullptr;
  }
  [[nodiscard]] T get() const noexcept { return value_; }
  explicit operator bool() const noexcept { return value_ != nullptr; }

private:
  T value_{nullptr};
};

CGColorSpaceRef srgb() {
  static CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  return space;
}

CfRef<CGColorRef> cgColor(Color c) {
  const CGFloat components[4] = {c.red / 255.0, c.green / 255.0, c.blue / 255.0, c.alpha / 255.0};
  return CfRef<CGColorRef>{CGColorCreate(srgb(), components)};
}

class CoreGraphicsImage final : public Image {
public:
  explicit CoreGraphicsImage(CfRef<CGImageRef> image) : image_(std::move(image)) {}
  [[nodiscard]] std::uint32_t width() const noexcept override {
    return static_cast<std::uint32_t>(CGImageGetWidth(image_.get()));
  }
  [[nodiscard]] std::uint32_t height() const noexcept override {
    return static_cast<std::uint32_t>(CGImageGetHeight(image_.get()));
  }
  [[nodiscard]] CGImageRef get() const noexcept { return image_.get(); }

  // The image resampled once to width x height device pixels (high quality), kept for the next frame
  // that draws it at that size: artwork drawn smaller than its pixels is resampled once rather than
  // on every frame.
  [[nodiscard]] CGImageRef scaled(std::size_t width, std::size_t height) const {
    const std::lock_guard lock{mutex_};
    return scaledLocked(width, height);
  }
  // The same copy's premultiplied pixels (row 0 at the top), or null when it could not be made.
  [[nodiscard]] const std::uint32_t* scaledPixels(std::size_t width, std::size_t height) const {
    const std::lock_guard lock{mutex_};
    return scaledLocked(width, height) != image_.get() ? scaledPixels_.data() : nullptr;
  }

private:
  CGImageRef scaledLocked(std::size_t width, std::size_t height) const {
    if (scaled_ && scaledWidth_ == width && scaledHeight_ == height) return scaled_.get();
    scaledPixels_.assign(width * height, 0U);
    CfRef<CGContextRef> bitmap{CGBitmapContextCreate(
        scaledPixels_.data(), width, height, 8U, width * sizeof(std::uint32_t), srgb(),
        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst) | kCGBitmapByteOrder32Little)};
    if (!bitmap) return image_.get();
    CGContextSetInterpolationQuality(bitmap.get(), kCGInterpolationHigh);
    CGContextDrawImage(bitmap.get(),
                       CGRectMake(0.0, 0.0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)),
                       image_.get());
    scaled_ = CfRef<CGImageRef>{CGBitmapContextCreateImage(bitmap.get())};
    scaledWidth_ = width;
    scaledHeight_ = height;
    return scaled_ ? scaled_.get() : image_.get();
  }

  CfRef<CGImageRef> image_;
  mutable std::mutex mutex_;
  mutable CfRef<CGImageRef> scaled_;
  mutable std::vector<std::uint32_t> scaledPixels_;
  mutable std::size_t scaledWidth_{0U};
  mutable std::size_t scaledHeight_{0U};
};

CfRef<CGPathRef> toCgPath(const Path& path) {
  CGMutablePathRef result = CGPathCreateMutable();
  for (const auto& e : path.elements()) {
    switch (e.verb) {
      case Path::Verb::Move: CGPathMoveToPoint(result, nullptr, e.a.x, e.a.y); break;
      case Path::Verb::Line: CGPathAddLineToPoint(result, nullptr, e.a.x, e.a.y); break;
      case Path::Verb::Quad:
        CGPathAddQuadCurveToPoint(result, nullptr, e.a.x, e.a.y, e.b.x, e.b.y);
        break;
      case Path::Verb::Cubic:
        CGPathAddCurveToPoint(result, nullptr, e.a.x, e.a.y, e.b.x, e.b.y, e.c.x, e.c.y);
        break;
      case Path::Verb::Close: CGPathCloseSubpath(result); break;
    }
  }
  return CfRef<CGPathRef>{result};
}

CfRef<CGGradientRef> toCgGradient(const std::vector<GradientStop>& stops) {
  if (stops.empty()) return {};
  std::vector<CGFloat> components;
  std::vector<CGFloat> locations;
  components.reserve(stops.size() * 4U);
  for (const auto& stop : stops) {
    components.push_back(stop.color.red / 255.0);
    components.push_back(stop.color.green / 255.0);
    components.push_back(stop.color.blue / 255.0);
    components.push_back(stop.color.alpha / 255.0);
    locations.push_back(std::clamp(stop.offset, 0.0, 1.0));
  }
  return CfRef<CGGradientRef>{CGGradientCreateWithColorComponents(
      srgb(), components.data(), locations.data(), stops.size())};
}

NSFont* fontFor(const TextStyle& style) {
  const auto size = static_cast<CGFloat>(std::clamp(style.size, 4.0, 256.0));
  switch (style.role) {
    case FontRole::Ui: return [NSFont systemFontOfSize:size weight:NSFontWeightRegular];
    case FontRole::UiMedium: return [NSFont systemFontOfSize:size weight:NSFontWeightMedium];
    case FontRole::UiSemibold: return [NSFont systemFontOfSize:size weight:NSFontWeightSemibold];
    case FontRole::UiBold: return [NSFont systemFontOfSize:size weight:NSFontWeightBold];
    case FontRole::Mono:
      return [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightMedium];
    case FontRole::Display: {
      NSFont* condensed = [NSFont fontWithName:@"AvenirNextCondensed-DemiBold" size:size];
      return condensed != nil ? condensed : [NSFont systemFontOfSize:size weight:NSFontWeightHeavy];
    }
  }
  return [NSFont systemFontOfSize:size];
}

class CoreGraphicsCanvas final : public Canvas2D {
public:
  CoreGraphicsCanvas(PixelSurface& surface, double scale)
      : surface_(surface), scale_(std::isfinite(scale) ? std::clamp(scale, 0.5, 4.0) : 1.0) {
    context_ = CfRef<CGContextRef>{CGBitmapContextCreate(
        surface.pixels().data(), surface.width(), surface.height(), 8U, surface.strideBytes(),
        srgb(),
        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst) | kCGBitmapByteOrder32Little)};
    if (!context_) return;
    auto* ctx = context_.get();
    CGContextTranslateCTM(ctx, 0.0, static_cast<CGFloat>(surface.height()));
    CGContextScaleCTM(ctx, 1.0, -1.0);
    CGContextScaleCTM(ctx, scale_, scale_);
    CGContextSetShouldAntialias(ctx, true);
    CGContextSetAllowsAntialiasing(ctx, true);
    CGContextSetShouldSmoothFonts(ctx, false);
    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    CGContextSetLineJoin(ctx, kCGLineJoinRound);
    baseInverse_ = CGAffineTransformInvert(CGContextGetCTM(ctx));
    tracked_.clip = CGRectMake(0.0, 0.0, static_cast<CGFloat>(surface.width()),
                               static_cast<CGFloat>(surface.height()));
  }

  [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(context_); }

  [[nodiscard]] double width() const noexcept override { return surface_.width() / scale_; }
  [[nodiscard]] double height() const noexcept override { return surface_.height() / scale_; }
  [[nodiscard]] double scale() const noexcept override { return scale_; }

  void save() override {
    CGContextSaveGState(context_.get());
    glowStack_.push_back(glow_);
    trackedStack_.push_back(tracked_);
  }
  void restore() override {
    CGContextRestoreGState(context_.get());
    if (!glowStack_.empty()) {
      glow_ = glowStack_.back();
      glowStack_.pop_back();
    }
    if (!trackedStack_.empty()) {
      tracked_ = trackedStack_.back();
      trackedStack_.pop_back();
    }
  }
  void translate(double dx, double dy) override { CGContextTranslateCTM(context_.get(), dx, dy); }
  void clipRect(ui::Rect r) override {
    const auto rect = CGRectMake(r.x, r.y, std::max(0.0, r.width), std::max(0.0, r.height));
    CGContextClipToRect(context_.get(), rect);
    if (!tracked_.rectClip) return;
    // Tracked as one device rectangle while it stays on whole pixels, for the software paths.
    const auto device = CGRectApplyAffineTransform(rect, CGContextGetCTM(context_.get()));
    const auto whole = [](CGFloat v) { return std::abs(v - std::round(v)) < 1e-6; };
    if (!whole(CGRectGetMinX(device)) || !whole(CGRectGetMinY(device)) ||
        !whole(CGRectGetMaxX(device)) || !whole(CGRectGetMaxY(device))) {
      tracked_.rectClip = false;
      return;
    }
    const auto clipped = CGRectIntersection(
        tracked_.clip, CGRectMake(std::round(CGRectGetMinX(device)), std::round(CGRectGetMinY(device)),
                                  std::round(device.size.width), std::round(device.size.height)));
    tracked_.clip = CGRectIsNull(clipped) ? CGRectZero : clipped;
  }
  void clipPath(const Path& path) override {
    const auto p = toCgPath(path);
    CGContextAddPath(context_.get(), p.get());
    CGContextClip(context_.get());
    tracked_.rectClip = false;
  }
  void setAlpha(double alpha) override {
    CGContextSetAlpha(context_.get(), std::clamp(alpha, 0.0, 1.0));
    tracked_.alpha = std::clamp(alpha, 0.0, 1.0);
  }
  void setBlend(Blend blend) override {
    CGContextSetBlendMode(context_.get(), blend == Blend::Add      ? kCGBlendModePlusLighter
                                          : blend == Blend::Screen ? kCGBlendModeScreen
                                                                   : kCGBlendModeNormal);
    tracked_.normalBlend = blend == Blend::Normal;
  }
  void setGlow(Color color, double radius) override {
    glow_ = Glow{.on = true, .color = color, .radius = std::max(0.0, radius)};
  }
  void clearGlow() override { glow_ = Glow{}; }

  void fill(const Path& path, Color color) override {
    if (path.empty()) return;
    if (fillRectInSoftware(path, color)) return;
    const auto p = toCgPath(path);
    const auto c = cgColor(color);
    glowed(CGPathGetBoundingBox(p.get()), [&](CGContextRef ctx) {
      CGContextAddPath(ctx, p.get());
      CGContextSetFillColorWithColor(ctx, c.get());
      CGContextFillPath(ctx);
    });
  }
  void fill(const Path& path, const LinearGradient& gradient) override {
    const auto g = toCgGradient(gradient.stops);
    if (path.empty() || !g) return;
    const auto p = toCgPath(path);
    glowed(CGPathGetBoundingBox(p.get()), [&](CGContextRef ctx) {
      CGContextSaveGState(ctx);
      CGContextAddPath(ctx, p.get());
      CGContextClip(ctx);
      CGContextDrawLinearGradient(ctx, g.get(), CGPointMake(gradient.from.x, gradient.from.y),
                                  CGPointMake(gradient.to.x, gradient.to.y),
                                  kCGGradientDrawsBeforeStartLocation |
                                      kCGGradientDrawsAfterEndLocation);
      CGContextRestoreGState(ctx);
    });
  }
  void fill(const Path& path, const RadialGradient& gradient) override {
    const auto g = toCgGradient(gradient.stops);
    if (path.empty() || !g) return;
    const auto p = toCgPath(path);
    glowed(CGPathGetBoundingBox(p.get()), [&](CGContextRef ctx) {
      CGContextSaveGState(ctx);
      CGContextAddPath(ctx, p.get());
      CGContextClip(ctx);
      const auto c = CGPointMake(gradient.center.x, gradient.center.y);
      CGContextDrawRadialGradient(ctx, g.get(), c, 0.0, c, std::max(0.0, gradient.radius),
                                  kCGGradientDrawsAfterEndLocation);
      CGContextRestoreGState(ctx);
    });
  }
  void stroke(const Path& path, Color color, const StrokeStyle& style) override {
    if (path.empty()) return;
    const auto p = toCgPath(path);
    const auto c = cgColor(color);
    glowed(strokeBounds(p.get(), style), [&](CGContextRef ctx) {
      CGContextSaveGState(ctx);
      applyStroke(ctx, style);
      CGContextAddPath(ctx, p.get());
      CGContextSetStrokeColorWithColor(ctx, c.get());
      CGContextStrokePath(ctx);
      CGContextRestoreGState(ctx);
    });
  }
  void stroke(const Path& path, const LinearGradient& gradient,
              const StrokeStyle& style) override {
    const auto g = toCgGradient(gradient.stops);
    if (path.empty() || !g) return;
    const auto p = toCgPath(path);
    glowed(strokeBounds(p.get(), style), [&](CGContextRef ctx) {
      CGContextSaveGState(ctx);
      applyStroke(ctx, style);
      CGContextAddPath(ctx, p.get());
      CGContextReplacePathWithStrokedPath(ctx);
      CGContextClip(ctx);
      CGContextDrawLinearGradient(ctx, g.get(), CGPointMake(gradient.from.x, gradient.from.y),
                                  CGPointMake(gradient.to.x, gradient.to.y),
                                  kCGGradientDrawsBeforeStartLocation |
                                      kCGGradientDrawsAfterEndLocation);
      CGContextRestoreGState(ctx);
    });
  }
  void drawImage(const Image& image, ui::Rect destination, double opacity) override {
    const auto* cg = dynamic_cast<const CoreGraphicsImage*>(&image);
    if (cg == nullptr || destination.width <= 0.0 || destination.height <= 0.0) return;
    // Artwork drawn well below its own size is drawn from a copy resampled to the device size.
    const auto deviceWidth = static_cast<std::size_t>(std::lround(destination.width * scale_));
    const auto deviceHeight = static_cast<std::size_t>(std::lround(destination.height * scale_));
    const auto shrinks = deviceWidth > 0U && deviceHeight > 0U &&
                         static_cast<double>(cg->width()) > 1.25 * static_cast<double>(deviceWidth) &&
                         static_cast<double>(cg->height()) > 1.25 * static_cast<double>(deviceHeight);
    if (!shrinks) {
      drawCgImage(cg->get(), destination, opacity);
      return;
    }
    // The resampled copy lands on whole device pixels (less than half a pixel from the destination),
    // so it is copied rather than resampled again.
    auto* ctx = context_.get();
    const auto ctm = CGContextGetCTM(ctx);
    const auto device = CGRectApplyAffineTransform(
        CGRectMake(destination.x, destination.y, destination.width, destination.height), ctm);
    if (!glow_.on && softwareState()) {
      if (const auto* pixels = cg->scaledPixels(deviceWidth, deviceHeight); pixels != nullptr) {
        compositeImage(pixels, static_cast<std::int64_t>(deviceWidth),
                       static_cast<std::int64_t>(deviceHeight),
                       static_cast<std::int64_t>(std::round(device.origin.x)),
                       static_cast<std::int64_t>(std::round(device.origin.y)), opacity);
        return;
      }
    }
    CGContextSaveGState(ctx);
    CGContextConcatCTM(ctx, CGAffineTransformInvert(ctm));
    applyShadow(ctx);
    CGContextSetAlpha(ctx, std::clamp(opacity, 0.0, 1.0));
    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
    CGContextDrawImage(ctx,
                       CGRectMake(std::round(device.origin.x), std::round(device.origin.y),
                                  static_cast<CGFloat>(deviceWidth),
                                  static_cast<CGFloat>(deviceHeight)),
                       cg->scaled(deviceWidth, deviceHeight));
    CGContextRestoreGState(ctx);
  }
  void drawImage(const Image& image, ui::Rect source, ui::Rect destination,
                 double opacity) override {
    const auto* cg = dynamic_cast<const CoreGraphicsImage*>(&image);
    if (cg == nullptr || destination.width <= 0.0 || destination.height <= 0.0) return;
    const auto bounded = CGRectIntersection(
        CGRectMake(source.x, source.y, source.width, source.height),
        CGRectMake(0.0, 0.0, cg->width(), cg->height()));
    if (CGRectIsEmpty(bounded)) return;
    CfRef<CGImageRef> cropped{CGImageCreateWithImageInRect(cg->get(), bounded)};
    if (cropped) drawCgImage(cropped.get(), destination, opacity);
  }
  double text(ui::Rect bounds, std::string_view utf8, const TextStyle& style,
              Color color) override {
    if (utf8.empty() || bounds.width <= 1.0 || bounds.height <= 0.0) return 0.0;
    CfRef<CTLineRef> line = makeLine(utf8, style, color);
    if (!line) return 0.0;
    CGFloat ascent = 0.0;
    CGFloat descent = 0.0;
    auto width = CTLineGetTypographicBounds(line.get(), &ascent, &descent, nullptr);
    const auto naturalWidth = width;
    auto elided = false;
    if (width > bounds.width) {
      CfRef<CTLineRef> ellipsis = makeLine("\u2026", style, color);
      CfRef<CTLineRef> truncated{
          CTLineCreateTruncatedLine(line.get(), bounds.width, kCTLineTruncationEnd,
                                    ellipsis.get())};
      if (truncated) {
        line = std::move(truncated);
        width = CTLineGetTypographicBounds(line.get(), &ascent, &descent, nullptr);
        elided = true;
      }
    }
    auto x = bounds.x;
    if (style.align == TextAlign::Center) x = bounds.x + (bounds.width - width) * 0.5;
    if (style.align == TextAlign::Right) x = bounds.right() - width;
    const auto baseline = bounds.y + (bounds.height + ascent - descent) * 0.5;
    auto* ctx = context_.get();
    recordText(bounds, {x, baseline - ascent, width, ascent + descent}, utf8, naturalWidth, elided);
    CGContextSaveGState(ctx);
    applyShadow(ctx);
    CGContextClipToRect(ctx, CGRectMake(bounds.x - 2.0, bounds.y - 4.0, bounds.width + 4.0,
                                        bounds.height + 8.0));
    CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1.0, -1.0));
    CGContextSetTextPosition(ctx, x, baseline);
    CTLineDraw(line.get(), ctx);
    CGContextRestoreGState(ctx);
    return width;
  }
  double measure(std::string_view utf8, const TextStyle& style) override {
    CfRef<CTLineRef> line = makeLine(utf8, style, Color{});
    if (!line) return 0.0;
    return CTLineGetTypographicBounds(line.get(), nullptr, nullptr, nullptr);
  }
  void flush() override { CGContextFlush(context_.get()); }

private:
  // Hands a drawn line to a live ScopedTextCapture, in the canvas's logical coordinates.
  void recordText(ui::Rect bounds, ui::Rect ink, std::string_view utf8, double naturalWidth,
                  bool elided) {
    if (!ScopedTextCapture::active()) return;
    const auto logical = [&](CGRect user) {
      // The current transform relative to the one the canvas started with is exactly the
      // translation the painters applied, so this lands in the canvas's logical coordinates.
      const auto r = CGRectApplyAffineTransform(
          user, CGAffineTransformConcat(CGContextGetCTM(context_.get()), baseInverse_));
      return ui::Rect{r.origin.x, r.origin.y, r.size.width, r.size.height};
    };
    const auto rect = [](ui::Rect r) { return CGRectMake(r.x, r.y, r.width, r.height); };
    ScopedTextCapture::record(TextRecord{
        .bounds = logical(rect(bounds)),
        .ink = logical(rect(ink)),
        .clip = logical(CGContextGetClipBoundingBox(context_.get())),
        .text = std::string{utf8},
        .naturalWidth = naturalWidth,
        .elided = elided});
  }

  // The glow as the canvas holds it: CoreGraphics' shadow is set only around the drawing that casts
  // it, so a glow can also be drawn another way.
  struct Glow final {
    bool on{false};
    Color color{};
    double radius{0.0};
  };

  // Sets the current glow as the context's shadow (blur in device space, not affected by the CTM).
  void applyShadow(CGContextRef ctx, double blurScale = 1.0) const {
    if (!glow_.on) return;
    const auto c = cgColor(glow_.color);
    CGContextSetShadowWithColor(ctx, CGSizeZero, glow_.radius * scale_ * blurScale, c.get());
  }

  static CGRect strokeBounds(CGPathRef path, const StrokeStyle& style) {
    const auto half = std::max(0.0, style.width);
    return CGRectInset(CGPathGetBoundingBox(path), -half, -half);
  }

  // Draws a shape (draw paints it into the context it is given) with the current glow. A glow over
  // a large area is blurred at half resolution (plan section 3.4): the shape's shadow alone is cast
  // into a half-resolution layer anchored on even device pixels, drawn back upscaled, and the shape
  // is drawn crisp over it. The layer depends only on the shape and on the clip grown well past the
  // blur, so a frame drawn in damaged rectangles and a frame drawn whole agree pixel for pixel.
  template <typename Draw>
  void glowed(CGRect userBounds, Draw&& draw) {
    auto* ctx = context_.get();
    if (!glow_.on || glow_.radius <= 0.0) {
      if (glow_.on) {
        CGContextSaveGState(ctx);
        applyShadow(ctx);
        draw(ctx);
        CGContextRestoreGState(ctx);
      } else {
        draw(ctx);
      }
      return;
    }
    const auto blur = glow_.radius * scale_;  // device pixels
    const auto ctm = CGContextGetCTM(ctx);
    const auto reach = 2.0 * blur + 2.0;
    const auto device = CGRectInset(CGRectApplyAffineTransform(userBounds, ctm), -reach, -reach);
    if (halfResolutionGlowDisabled() ||
        device.size.width * device.size.height < kHalfResolutionGlowArea) {
      CGContextSaveGState(ctx);
      applyShadow(ctx);
      draw(ctx);
      CGContextRestoreGState(ctx);
      return;
    }
    // The layer covers the whole glow whatever the clip: a clip only decides which of its pixels
    // land, never how they are computed.
    const auto clip = CGRectApplyAffineTransform(CGContextGetClipBoundingBox(ctx), ctm);
    const auto region = device;
    if (CGRectIntersectsRect(region, clip)) {
      // Anchored on a coarse device grid on every side, so the blur sees the same layer grid
      // whichever part of the shape a clip asks for.
      const auto x0 = std::floor(region.origin.x / kGlowGrid) * kGlowGrid;
      const auto y0 = std::floor(region.origin.y / kGlowGrid) * kGlowGrid;
      const auto x1 = std::ceil(CGRectGetMaxX(region) / kGlowGrid) * kGlowGrid;
      const auto y1 = std::ceil(CGRectGetMaxY(region) / kGlowGrid) * kGlowGrid;
      const auto width = static_cast<std::size_t>((x1 - x0) / 2.0);
      const auto height = static_cast<std::size_t>((y1 - y0) / 2.0);
      glowMask_.assign(width * height, 0U);
      glowScratch_.assign(width * height, 0U);
      // The shape's coverage (times its paint's alpha, as a shadow takes it) at half resolution.
      CfRef<CGContextRef> mask{CGBitmapContextCreate(glowMask_.data(), width, height, 8U, width,
                                                     nullptr, kCGImageAlphaOnly)};
      if (mask) {
        auto* m = mask.get();
        CGContextSetShouldAntialias(m, true);
        CGContextSetAllowsAntialiasing(m, true);
        CGContextSetLineJoin(m, kCGLineJoinRound);
        auto toMask = CGAffineTransformConcat(ctm, CGAffineTransformMakeTranslation(-x0, -y0));
        toMask = CGAffineTransformConcat(toMask, CGAffineTransformMakeScale(0.5, 0.5));
        CGContextConcatCTM(m, toMask);
        draw(m);
        CGContextFlush(m);
        // Three box passes approximate the shadow's Gaussian (sigma about half the blur).
        const auto sigma = blur * 0.25;  // half-resolution pixels
        auto box = static_cast<std::uint32_t>(std::lround(std::sqrt(4.0 * sigma * sigma + 1.0)));
        if (box % 2U == 0U) ++box;
        vImage_Buffer a{glowMask_.data(), height, width, width};
        vImage_Buffer b{glowScratch_.data(), height, width, width};
        if (box > 1U) {
          for (int pass = 0; pass < 3; ++pass) {
            if (vImageBoxConvolve_Planar8(&a, &b, nullptr, 0, 0, box, box, 0,
                                          kvImageBackgroundColorFill) != kvImageNoError)
              break;
            std::swap(a, b);
          }
        }
        // Upsampled to device resolution here (vImage), so CoreGraphics composites it 1:1.
        const auto deviceWidth = width * 2U;
        const auto deviceHeight = height * 2U;
        glowDevice_.resize(deviceWidth * deviceHeight);
        vImage_Buffer up{glowDevice_.data(), deviceHeight, deviceWidth, deviceWidth};
        if (vImageScale_Planar8(&a, &up, nullptr, kvImageNoFlags) != kvImageNoError)
          std::fill(glowDevice_.begin(), glowDevice_.end(), std::uint8_t{0});
        // The glow colour painted through the blurred coverage.
        if (softwareState()) {
          compositeCoverage(glowDevice_.data(), static_cast<std::int64_t>(deviceWidth),
                            static_cast<std::int64_t>(deviceHeight), static_cast<std::int64_t>(x0),
                            static_cast<std::int64_t>(y0), glow_.color);
          draw(ctx);
          return;
        }
        CfRef<CGDataProviderRef> provider{CGDataProviderCreateWithData(
            nullptr, up.data, deviceWidth * deviceHeight, nullptr)};
        // An image mask paints the fill colour where its samples say (decoded so 255 paints).
        static constexpr CGFloat kDecode[2] = {1.0, 0.0};
        CfRef<CGImageRef> coverage{CGImageMaskCreate(deviceWidth, deviceHeight, 8U, 8U,
                                                     deviceWidth, provider.get(), kDecode, false)};
        if (coverage) {
          const auto c = cgColor(glow_.color);
          const auto rect = CGRectMake(x0, y0, x1 - x0, y1 - y0);
          CGContextSaveGState(ctx);
          CGContextConcatCTM(ctx, CGAffineTransformInvert(ctm));
          // Bilinear, as the plan asks: the canvas's high-quality filter costs more than the glow.
          CGContextSetInterpolationQuality(ctx, kCGInterpolationLow);
          CGContextSetFillColorWithColor(ctx, c.get());
          CGContextDrawImage(ctx, rect, coverage.get());
          CGContextRestoreGState(ctx);
        }
      }
    }
    draw(ctx);
  }

  static constexpr double kHalfResolutionGlowArea = 256.0 * 256.0;
  static constexpr double kGlowGrid = 64.0;
  static bool halfResolutionGlowDisabled() noexcept { return ScopedFullResolutionGlow::active(); }

  // ---- software paths ----------------------------------------------------------------------------
  // Where the state is plain (the clip one whole-pixel rectangle, normal blending), a few costly
  // CoreGraphics calls are done directly on the pixels: a translucent rectangle fill, a glow's
  // coverage and a resampled image drawn 1:1. They are deterministic, so a frame drawn whole and one
  // drawn in rectangles still agree, and they differ from CoreGraphics only in rounding.

  [[nodiscard]] bool softwareState() const noexcept {
    return tracked_.rectClip && tracked_.normalBlend;
  }

  // Premultiplied source over a premultiplied destination: destination * (255 - source alpha) / 255,
  // rounded, two channels at a time, plus the source (which cannot overflow a channel).
  static std::uint32_t over(std::uint32_t source, std::uint32_t destination) noexcept {
    const auto inverse = 255U - (source >> 24U);
    auto rb = (destination & 0x00FF00FFU) * inverse + 0x00800080U;
    rb = ((rb + ((rb >> 8U) & 0x00FF00FFU)) >> 8U) & 0x00FF00FFU;
    auto ag = ((destination >> 8U) & 0x00FF00FFU) * inverse + 0x00800080U;
    ag = (ag + ((ag >> 8U) & 0x00FF00FFU)) & 0xFF00FF00U;
    return source + (rb | ag);
  }

  // The colour premultiplied at an alpha of coverage (0..1) times its own and the canvas's.
  [[nodiscard]] std::uint32_t premultiplied(Color color, double coverage) const noexcept {
    const auto a = std::clamp(coverage * (color.alpha / 255.0) * tracked_.alpha, 0.0, 1.0);
    const auto c = [&](std::uint8_t v) {
      return static_cast<std::uint32_t>(std::lround(static_cast<double>(v) * a));
    };
    return c(color.blue) | (c(color.green) << 8U) | (c(color.red) << 16U) |
           (static_cast<std::uint32_t>(std::lround(255.0 * a)) << 24U);
  }

  // The device pixel row (memory order) of a device y measured upward.
  [[nodiscard]] std::uint32_t* row(std::int64_t upwardY) noexcept {
    const auto height = static_cast<std::int64_t>(surface_.height());
    return surface_.pixels().data() + (height - 1 - upwardY) * static_cast<std::int64_t>(surface_.width());
  }

  // An axis-aligned rectangle filled with a flat colour, its fractional edges covered by area.
  bool fillRectInSoftware(const Path& path, Color color) {
    if (glow_.on || !softwareState()) return false;
    const auto& e = path.elements();
    if (e.size() != 5U || e[0].verb != Path::Verb::Move || e[1].verb != Path::Verb::Line ||
        e[2].verb != Path::Verb::Line || e[3].verb != Path::Verb::Line ||
        e[4].verb != Path::Verb::Close)
      return false;
    const auto a = e[0].a, b = e[1].a, c = e[2].a, d = e[3].a;
    const auto horizontalFirst = a.y == b.y && b.x == c.x && c.y == d.y && d.x == a.x;
    const auto verticalFirst = a.x == b.x && b.y == c.y && c.x == d.x && d.y == a.y;
    if (!horizontalFirst && !verticalFirst) return false;
    const auto left = std::min(a.x, c.x), right = std::max(a.x, c.x);
    const auto top = std::min(a.y, c.y), bottom = std::max(a.y, c.y);
    const auto device = CGRectApplyAffineTransform(CGRectMake(left, top, right - left, bottom - top),
                                                   CGContextGetCTM(context_.get()));
    const auto& clip = tracked_.clip;
    const auto fx0 = std::max(CGRectGetMinX(device), CGRectGetMinX(clip));
    const auto fx1 = std::min(CGRectGetMaxX(device), CGRectGetMaxX(clip));
    const auto fy0 = std::max(CGRectGetMinY(device), CGRectGetMinY(clip));
    const auto fy1 = std::min(CGRectGetMaxY(device), CGRectGetMaxY(clip));
    if (fx1 <= fx0 || fy1 <= fy0 || color.alpha == 0U || tracked_.alpha <= 0.0) return true;
    const auto x0 = static_cast<std::int64_t>(std::floor(fx0));
    const auto x1 = static_cast<std::int64_t>(std::ceil(fx1));
    const auto y0 = static_cast<std::int64_t>(std::floor(fy0));
    const auto y1 = static_cast<std::int64_t>(std::ceil(fy1));
    // Whole columns between the fractional edge columns share one source per row.
    const auto inner0 = std::min(x1, static_cast<std::int64_t>(std::ceil(fx0)));
    const auto inner1 = std::max(inner0, static_cast<std::int64_t>(std::floor(fx1)));
    for (auto y = y0; y < y1; ++y) {
      const auto cy = std::min(static_cast<double>(y + 1), fy1) - std::max(static_cast<double>(y), fy0);
      auto* pixels = row(y);
      const auto edge = [&](std::int64_t x) {
        const auto cx = std::min(static_cast<double>(x + 1), fx1) - std::max(static_cast<double>(x), fx0);
        pixels[x] = over(premultiplied(color, cx * cy), pixels[x]);
      };
      for (auto x = x0; x < inner0; ++x) edge(x);
      const auto source = premultiplied(color, std::min(1.0, cy));
      for (auto x = inner0; x < inner1; ++x) pixels[x] = over(source, pixels[x]);
      for (auto x = inner1; x < x1; ++x) edge(x);
    }
    return true;
  }

  // The colour painted through a coverage mask (row 0 at the top) whose lower-left device pixel is
  // (left, bottom), inside the tracked clip.
  void compositeCoverage(const std::uint8_t* coverage, std::int64_t width, std::int64_t height,
                         std::int64_t left, std::int64_t bottom, Color color) {
    std::array<std::uint32_t, 256U> source{};
    for (std::size_t m = 1U; m < source.size(); ++m)
      source[m] = premultiplied(color, static_cast<double>(m) / 255.0);
    const auto& clip = tracked_.clip;
    const auto cx0 = std::max(left, static_cast<std::int64_t>(CGRectGetMinX(clip)));
    const auto cx1 = std::min(left + width, static_cast<std::int64_t>(CGRectGetMaxX(clip)));
    const auto cy0 = std::max(bottom, static_cast<std::int64_t>(CGRectGetMinY(clip)));
    const auto cy1 = std::min(bottom + height, static_cast<std::int64_t>(CGRectGetMaxY(clip)));
    for (auto y = cy0; y < cy1; ++y) {
      const auto* mask = coverage + (bottom + height - 1 - y) * width - left;
      auto* pixels = row(y);
      for (auto x = cx0; x < cx1; ++x) {
        const auto m = mask[x];
        if (m != 0U) pixels[x] = over(source[m], pixels[x]);
      }
    }
  }

  // A premultiplied image (row 0 at the top) drawn 1:1 with its lower-left device pixel at
  // (left, bottom), at an opacity, inside the tracked clip.
  void compositeImage(const std::uint32_t* image, std::int64_t width, std::int64_t height,
                      std::int64_t left, std::int64_t bottom, double opacity) {
    const auto alpha = static_cast<std::uint32_t>(std::lround(std::clamp(opacity, 0.0, 1.0) * 255.0));
    if (alpha == 0U) return;
    const auto& clip = tracked_.clip;
    const auto cx0 = std::max(left, static_cast<std::int64_t>(CGRectGetMinX(clip)));
    const auto cx1 = std::min(left + width, static_cast<std::int64_t>(CGRectGetMaxX(clip)));
    const auto cy0 = std::max(bottom, static_cast<std::int64_t>(CGRectGetMinY(clip)));
    const auto cy1 = std::min(bottom + height, static_cast<std::int64_t>(CGRectGetMaxY(clip)));
    for (auto y = cy0; y < cy1; ++y) {
      const auto* source = image + (bottom + height - 1 - y) * width - left;
      auto* pixels = row(y);
      for (auto x = cx0; x < cx1; ++x) {
        auto s = source[x];
        if (s == 0U) continue;
        if (alpha != 255U) {
          auto rb = (s & 0x00FF00FFU) * alpha + 0x00800080U;
          rb = ((rb + ((rb >> 8U) & 0x00FF00FFU)) >> 8U) & 0x00FF00FFU;
          auto ag = ((s >> 8U) & 0x00FF00FFU) * alpha + 0x00800080U;
          ag = (ag + ((ag >> 8U) & 0x00FF00FFU)) & 0xFF00FF00U;
          s = rb | ag;
        }
        pixels[x] = over(s, pixels[x]);
      }
    }
  }

  static void applyStroke(CGContextRef ctx, const StrokeStyle& style) {
    CGContextSetLineWidth(ctx, std::max(0.0, style.width));
    CGContextSetLineCap(ctx, style.roundCaps ? kCGLineCapRound : kCGLineCapButt);
    if (!style.dash.empty()) {
      std::vector<CGFloat> lengths(style.dash.begin(), style.dash.end());
      CGContextSetLineDash(ctx, 0.0, lengths.data(), lengths.size());
    }
  }

  void drawCgImage(CGImageRef image, ui::Rect destination, double opacity) {
    auto* ctx = context_.get();
    CGContextSaveGState(ctx);
    applyShadow(ctx);
    CGContextSetAlpha(ctx, std::clamp(opacity, 0.0, 1.0));
    // The canvas is y-down; CGContextDrawImage puts row 0 at the rectangle's lower edge.
    CGContextTranslateCTM(ctx, destination.x, destination.y + destination.height);
    CGContextScaleCTM(ctx, 1.0, -1.0);
    CGContextDrawImage(ctx, CGRectMake(0.0, 0.0, destination.width, destination.height), image);
    CGContextRestoreGState(ctx);
  }

  CfRef<CTLineRef> makeLine(std::string_view utf8, const TextStyle& style, Color color) {
    NSString* string = [[NSString alloc] initWithBytes:utf8.data()
                                                length:utf8.size()
                                              encoding:NSUTF8StringEncoding];
    if (string == nil) return {};
    if (style.uppercase) string = [string uppercaseString];
    const auto c = cgColor(color);
    NSDictionary* attributes = @{
      (__bridge id)kCTFontAttributeName : fontFor(style),
      (__bridge id)kCTForegroundColorAttributeName : (__bridge id)c.get(),
      (__bridge id)kCTKernAttributeName : @(style.tracking),
    };
    NSAttributedString* attributed = [[NSAttributedString alloc] initWithString:string
                                                                     attributes:attributes];
    return CfRef<CTLineRef>{
        CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)attributed)};
  }

  PixelSurface& surface_;
  CGAffineTransform baseInverse_{CGAffineTransformIdentity};
  double scale_{1.0};
  CfRef<CGContextRef> context_;
  Glow glow_{};
  std::vector<Glow> glowStack_;
  // What the software paths need to know of the state, kept alongside CoreGraphics' own.
  struct Tracked final {
    bool rectClip{true};  // the clip is the one whole-pixel rectangle below
    CGRect clip{};        // device pixels, measured upward
    double alpha{1.0};
    bool normalBlend{true};
  };
  Tracked tracked_{};
  std::vector<Tracked> trackedStack_;
  std::vector<std::uint8_t> glowMask_;
  std::vector<std::uint8_t> glowScratch_;
  std::vector<std::uint8_t> glowDevice_;
};

}  // namespace

std::shared_ptr<const Image> loadImage(const std::filesystem::path& path, ImageLimits limits) {
  std::error_code error;
  const auto bytes = std::filesystem::file_size(path, error);
  if (error || bytes == 0U || bytes > limits.maximumEncodedBytes) return nullptr;
  @autoreleasepool {
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.string().c_str()]];
    CfRef<CGImageSourceRef> source{CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr)};
    if (!source || CGImageSourceGetCount(source.get()) < 1U) return nullptr;
    // Refuse oversized images from the header, before any pixel is decoded.
    CfRef<CFDictionaryRef> properties{
        CGImageSourceCopyPropertiesAtIndex(source.get(), 0U, nullptr)};
    if (!properties) return nullptr;
    NSDictionary* info = (__bridge NSDictionary*)properties.get();
    const auto w = [info[(__bridge NSString*)kCGImagePropertyPixelWidth] unsignedLongLongValue];
    const auto h = [info[(__bridge NSString*)kCGImagePropertyPixelHeight] unsignedLongLongValue];
    if (w == 0U || h == 0U || w * h > limits.maximumPixels) return nullptr;
    CfRef<CGImageRef> decoded{CGImageSourceCreateImageAtIndex(source.get(), 0U, nullptr)};
    if (!decoded) return nullptr;
    // Decode once into premultiplied sRGB so drawing never pays a first-use decode.
    CfRef<CGContextRef> bitmap{CGBitmapContextCreate(
        nullptr, w, h, 8U, 0U, srgb(),
        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst) | kCGBitmapByteOrder32Little)};
    if (!bitmap) return nullptr;
    CGContextDrawImage(bitmap.get(),
                       CGRectMake(0.0, 0.0, static_cast<CGFloat>(w), static_cast<CGFloat>(h)),
                       decoded.get());
    CfRef<CGImageRef> premultiplied{CGBitmapContextCreateImage(bitmap.get())};
    if (!premultiplied) return nullptr;
    return std::make_shared<CoreGraphicsImage>(std::move(premultiplied));
  }
}

std::unique_ptr<Canvas2D> makeCanvas(PixelSurface& surface, double scale) {
  if (surface.width() == 0U || surface.height() == 0U) return nullptr;
  auto canvas = std::make_unique<CoreGraphicsCanvas>(surface, scale);
  if (!canvas->valid()) return nullptr;
  return canvas;
}

bool vectorBackendAvailable() noexcept { return true; }

CGColorSpaceRef presentationColorSpace() noexcept { return srgb(); }

std::filesystem::path codeBundleResources() {
  Dl_info info{};
  if (dladdr(reinterpret_cast<const void*>(&codeBundleResources), &info) == 0 ||
      info.dli_fname == nullptr)
    return {};
  // .../Name.app/Contents/MacOS/binary or .../Name.clap/Contents/MacOS/binary
  const std::filesystem::path binary{info.dli_fname};
  const auto contents = binary.parent_path().parent_path();
  if (contents.filename() != "Contents") return {};
  return contents / "Resources";
}

}  // namespace seam::native_ui::paint
