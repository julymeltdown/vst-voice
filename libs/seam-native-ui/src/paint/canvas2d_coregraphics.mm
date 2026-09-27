#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/paint/presentation_color.hpp"

#import <AppKit/AppKit.h>
#import <Accelerate/Accelerate.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <ImageIO/ImageIO.h>

#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

private:
  CfRef<CGImageRef> image_;
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
  }

  [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(context_); }

  [[nodiscard]] double width() const noexcept override { return surface_.width() / scale_; }
  [[nodiscard]] double height() const noexcept override { return surface_.height() / scale_; }
  [[nodiscard]] double scale() const noexcept override { return scale_; }

  void save() override {
    CGContextSaveGState(context_.get());
    glowStack_.push_back(glow_);
  }
  void restore() override {
    CGContextRestoreGState(context_.get());
    if (!glowStack_.empty()) {
      glow_ = glowStack_.back();
      glowStack_.pop_back();
    }
  }
  void translate(double dx, double dy) override { CGContextTranslateCTM(context_.get(), dx, dy); }
  void clipRect(ui::Rect r) override {
    CGContextClipToRect(context_.get(), CGRectMake(r.x, r.y, std::max(0.0, r.width),
                                                   std::max(0.0, r.height)));
  }
  void clipPath(const Path& path) override {
    const auto p = toCgPath(path);
    CGContextAddPath(context_.get(), p.get());
    CGContextClip(context_.get());
  }
  void setAlpha(double alpha) override {
    CGContextSetAlpha(context_.get(), std::clamp(alpha, 0.0, 1.0));
  }
  void setBlend(Blend blend) override {
    CGContextSetBlendMode(context_.get(), blend == Blend::Add      ? kCGBlendModePlusLighter
                                          : blend == Blend::Screen ? kCGBlendModeScreen
                                                                   : kCGBlendModeNormal);
  }
  void setGlow(Color color, double radius) override {
    glow_ = Glow{.on = true, .color = color, .radius = std::max(0.0, radius)};
  }
  void clearGlow() override { glow_ = Glow{}; }

  void fill(const Path& path, Color color) override {
    if (path.empty()) return;
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
    drawCgImage(cg->get(), destination, opacity);
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
