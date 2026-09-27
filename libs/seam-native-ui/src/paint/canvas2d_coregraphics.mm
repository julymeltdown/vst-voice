#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/paint/presentation_color.hpp"

#import <AppKit/AppKit.h>
#import <Accelerate/Accelerate.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <ImageIO/ImageIO.h>
#include <CommonCrypto/CommonDigest.h>

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <tuple>
#include <unordered_map>
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

std::size_t roleIndex(FontRole role) noexcept { return static_cast<std::size_t>(role); }

std::optional<FontRole> parseRole(NSString* name) {
  static const std::array<std::pair<NSString*, FontRole>, kFontRoleCount> kRoles{{
      {@"Ui", FontRole::Ui},
      {@"UiMedium", FontRole::UiMedium},
      {@"UiSemibold", FontRole::UiSemibold},
      {@"UiBold", FontRole::UiBold},
      {@"Mono", FontRole::Mono},
      {@"Display", FontRole::Display},
      {@"DisplayRounded", FontRole::DisplayRounded},
  }};
  for (const auto& [text, role] : kRoles)
    if ([name isEqualToString:text]) return role;
  return std::nullopt;
}

std::string hexSha256(NSData* data) {
  std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
  CC_SHA256(data.bytes, static_cast<CC_LONG>(data.length), digest.data());
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(digest.size() * 2U);
  for (const auto byte : digest) {
    out.push_back(kHex[byte >> 4U]);
    out.push_back(kHex[byte & 0x0FU]);
  }
  return out;
}

// The descriptors of the bundled faces, per role, for the process. Written once under the call_once
// in bundledFonts() and read-only afterwards.
struct RoleDescriptors final {
  std::array<CfRef<CTFontDescriptorRef>, kFontRoleCount> descriptor;
};
RoleDescriptors& processDescriptors() {
  static RoleDescriptors descriptors;
  return descriptors;
}

std::uint32_t axisTag(NSString* tag) {
  const char* t = tag.UTF8String;
  std::uint32_t code = 0U;
  for (std::size_t i = 0U; i < 4U; ++i)
    code = (code << 8U) | static_cast<std::uint32_t>(static_cast<unsigned char>(t[i]));
  return code;
}

BundledFonts registerInto(const std::filesystem::path& directory, RoleDescriptors* out) {
  BundledFonts result;
  if (directory.empty()) return result;
  constexpr NSUInteger kMaximumFaceBytes = 4U * 1024U * 1024U;
  @autoreleasepool {
    NSString* root = [NSString stringWithUTF8String:directory.string().c_str()];
    NSData* manifestData =
        [NSData dataWithContentsOfFile:[root stringByAppendingPathComponent:@"manifest.json"]];
    id manifest = manifestData == nil ? nil
                                      : [NSJSONSerialization JSONObjectWithData:manifestData
                                                                        options:0
                                                                          error:nil];
    NSArray* faces = [manifest isKindOfClass:[NSDictionary class]] ? manifest[@"faces"] : nil;
    if (![faces isKindOfClass:[NSArray class]]) {
      result.refused.emplace_back("manifest.json is missing or unreadable");
      return result;
    }
    result.directory = directory;
    for (id entry in faces) {
      if (![entry isKindOfClass:[NSDictionary class]]) continue;
      NSDictionary* face = entry;
      NSString* file = face[@"file"];
      NSString* postScript = face[@"postScriptName"];
      NSString* expected = face[@"sha256"];
      NSArray* roles = face[@"roles"];
      if (![file isKindOfClass:[NSString class]] || ![postScript isKindOfClass:[NSString class]] ||
          ![expected isKindOfClass:[NSString class]] || ![roles isKindOfClass:[NSArray class]] ||
          [file containsString:@".."] || [file hasPrefix:@"/"]) {
        result.refused.emplace_back("a manifest entry is incomplete or leaves the directory");
        continue;
      }
      const std::string name = file.UTF8String;
      NSString* path = [root stringByAppendingPathComponent:file];
      NSData* bytes = [NSData dataWithContentsOfFile:path options:NSDataReadingMappedIfSafe error:nil];
      if (bytes == nil || bytes.length == 0U || bytes.length > kMaximumFaceBytes) {
        result.refused.push_back(name + ": missing, empty or too large");
        continue;
      }
      if (hexSha256(bytes) != std::string{expected.UTF8String}) {
        result.refused.push_back(name + ": SHA-256 differs from the manifest");
        continue;
      }
      NSURL* url = [NSURL fileURLWithPath:path];
      CFErrorRef error = nullptr;
      if (!CTFontManagerRegisterFontsForURL((__bridge CFURLRef)url, kCTFontManagerScopeProcess,
                                            &error)) {
        const auto code = error != nullptr ? CFErrorGetCode(error) : 0;
        if (error != nullptr) CFRelease(error);
        // The same file registered earlier in this process (a second plug-in instance) is fine.
        if (code != kCTFontManagerErrorAlreadyRegistered) {
          result.refused.push_back(name + ": registration failed (" + std::to_string(code) + ")");
          continue;
        }
      }
      // The face is taken from this file's own descriptors, so a same-named face installed on the
      // system can never stand in for the pinned one.
      CfRef<CFArrayRef> descriptors{CTFontManagerCreateFontDescriptorsFromURL((__bridge CFURLRef)url)};
      CTFontDescriptorRef match = nullptr;
      for (CFIndex i = 0; descriptors && i < CFArrayGetCount(descriptors.get()); ++i) {
        auto* candidate = static_cast<CTFontDescriptorRef>(
            const_cast<void*>(CFArrayGetValueAtIndex(descriptors.get(), i)));
        CfRef<CFStringRef> candidateName{static_cast<CFStringRef>(
            CTFontDescriptorCopyAttribute(candidate, kCTFontNameAttribute))};
        if (candidateName && [(__bridge NSString*)candidateName.get() isEqualToString:postScript]) {
          match = candidate;
          break;
        }
      }
      if (match == nullptr) {
        result.refused.push_back(name + ": no face named " + postScript.UTF8String);
        continue;
      }
      CfRef<CTFontDescriptorRef> descriptor{static_cast<CTFontDescriptorRef>(CFRetain(match))};
      if (NSDictionary* variation = face[@"variation"];
          [variation isKindOfClass:[NSDictionary class]] && variation.count > 0U) {
        NSMutableDictionary* axes = [NSMutableDictionary dictionary];
        for (NSString* tag in variation) {
          NSNumber* value = variation[tag];
          if (![tag isKindOfClass:[NSString class]] || tag.length != 4U ||
              ![value isKindOfClass:[NSNumber class]])
            continue;
          axes[@(axisTag(tag))] = value;
        }
        descriptor = CfRef<CTFontDescriptorRef>{CTFontDescriptorCreateCopyWithAttributes(
            descriptor.get(),
            (__bridge CFDictionaryRef)@{(__bridge id)kCTFontVariationAttribute : axes})};
      }
      for (NSString* roleName in roles) {
        const auto role = [roleName isKindOfClass:[NSString class]] ? parseRole(roleName)
                                                                   : std::nullopt;
        if (!role.has_value()) {
          result.refused.push_back(name + ": unknown role");
          continue;
        }
        result.face[roleIndex(*role)] = postScript.UTF8String;
        if (out != nullptr)
          out->descriptor[roleIndex(*role)] =
              CfRef<CTFontDescriptorRef>{static_cast<CTFontDescriptorRef>(CFRetain(descriptor.get()))};
      }
    }
  }
  return result;
}

NSFont* systemFontFor(FontRole role, CGFloat size) {
  switch (role) {
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
    case FontRole::DisplayRounded: {
      NSFont* base = [NSFont systemFontOfSize:size weight:NSFontWeightSemibold];
      NSFontDescriptor* rounded =
          [base.fontDescriptor fontDescriptorWithDesign:NSFontDescriptorSystemDesignRounded];
      NSFont* font = rounded != nil ? [NSFont fontWithDescriptor:rounded size:size] : nil;
      return font != nil ? font : base;
    }
  }
  return [NSFont systemFontOfSize:size];
}

// The blurred coverage of a small glowing shape, keyed by the shape exactly as it is rasterized: its
// device geometry relative to a whole device pixel, in 1/64 pixel steps, its paint and its blur.
// A sprite is a pure function of its key, so a frame drawn whole and one drawn in rectangles agree
// whichever of them drew the sprite first.
struct GlowSprite final {
  std::vector<std::int64_t> key;
  std::int64_t width{0};
  std::int64_t height{0};
  std::vector<std::uint8_t> coverage;  // row 0 at the top
};

class GlowSprites final {
public:
  static GlowSprites& shared() {
    static GlowSprites sprites;
    return sprites;
  }
  std::shared_ptr<const GlowSprite> find(std::uint64_t hash, const std::vector<std::int64_t>& key) {
    const std::lock_guard lock{mutex_};
    const auto found = sprites_.find(hash);
    if (found == sprites_.end() || found->second->key != key) return nullptr;
    return found->second;
  }
  void insert(std::uint64_t hash, std::shared_ptr<const GlowSprite> sprite) {
    const std::lock_guard lock{mutex_};
    const auto size = sprite->coverage.size() + sprite->key.size() * sizeof(std::int64_t);
    // A bounded set: when it would outgrow its budget it starts over (the sprites are redrawn on use).
    if (bytes_ + size > kBudgetBytes) {
      sprites_.clear();
      bytes_ = 0U;
    }
    if (auto& slot = sprites_[hash]; slot != nullptr) {
      bytes_ -= slot->coverage.size() + slot->key.size() * sizeof(std::int64_t);
      slot = std::move(sprite);
    } else {
      slot = std::move(sprite);
    }
    bytes_ += size;
  }
  [[nodiscard]] std::size_t bytes() {
    const std::lock_guard lock{mutex_};
    return bytes_;
  }

private:
  static constexpr std::size_t kBudgetBytes = 6U * 1024U * 1024U;
  std::mutex mutex_;
  std::unordered_map<std::uint64_t, std::shared_ptr<const GlowSprite>> sprites_;
  std::size_t bytes_{0U};
};
NSFont* fontFor(const TextStyle& style) {
  const auto size = static_cast<CGFloat>(std::clamp(style.size, 4.0, 256.0));
  static_cast<void>(bundledFonts());
  const auto role = roleIndex(style.role) < kFontRoleCount ? style.role : FontRole::Ui;
  const auto& descriptor = processDescriptors().descriptor[roleIndex(role)];
  if (!descriptor) return systemFontFor(role, size);
  // CoreText keeps its own cache of fonts made from a descriptor, and cascades to the system
  // fallback list for characters the face lacks (Hangul, kana, symbols).
  CTFontRef font = CTFontCreateWithFontDescriptor(descriptor.get(), size, nullptr);
  if (font == nullptr) return systemFontFor(role, size);
  return (__bridge_transfer NSFont*)font;
}

// Hangul behind every face the shell draws with. The system UI faces already fall back to Apple SD
// Gothic Neo; naming it as the cascade makes that hold for any face a role uses (a bundled Latin
// face has no Hangul), at the weight the role asks for.
NSString* hangulFace(FontRole role) {
  switch (role) {
    case FontRole::UiMedium:
    case FontRole::Mono: return @"AppleSDGothicNeo-Medium";
    case FontRole::UiSemibold: return @"AppleSDGothicNeo-SemiBold";
    case FontRole::UiBold:
    case FontRole::Display: return @"AppleSDGothicNeo-Bold";
    case FontRole::DisplayRounded: return @"AppleSDGothicNeo-Bold";
    case FontRole::Ui: break;
  }
  return @"AppleSDGothicNeo-Regular";
}

// The face a line is set in: the role's face with the Hangul cascade, made once per face and size.
NSFont* textFont(const TextStyle& style) {
  NSFont* base = fontFor(style);
  static std::mutex mutex;
  static std::map<std::tuple<std::string, double, int>, NSFont*> cache;
  const char* faceName = base.fontName.UTF8String;
  const auto key = std::make_tuple(std::string{faceName != nullptr ? faceName : ""},
                                   static_cast<double>(base.pointSize), static_cast<int>(style.role));
  const std::lock_guard lock{mutex};
  if (const auto found = cache.find(key); found != cache.end()) return found->second;
  NSFontDescriptor* hangul = [NSFontDescriptor fontDescriptorWithName:hangulFace(style.role)
                                                                 size:base.pointSize];
  NSFontDescriptor* descriptor =
      [base.fontDescriptor fontDescriptorByAddingAttributes:@{NSFontCascadeListAttribute : @[ hangul ]}];
  NSFont* font = [NSFont fontWithDescriptor:descriptor size:base.pointSize];
  if (font == nil) font = base;
  if (cache.size() >= 512U) cache.clear();
  cache.emplace(key, font);
  return font;
}

class CoreGraphicsCanvas final : public Canvas2D {
public:
  CoreGraphicsCanvas(PixelSurface& surface, double scale)
      : surface_(surface), scale_(std::isfinite(scale) ? std::clamp(scale, 0.5, 4.0) : 1.0),
        concurrent_(ScopedConcurrentCanvas::active()) {
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
    const GlowShape shape{.path = &path, .stroke = nullptr, .color = color};
    glowed(CGPathGetBoundingBox(p.get()), [&](CGContextRef ctx) {
      if (ctx == context_.get() && coverageDraw(shape, p.get())) return;
      CGContextAddPath(ctx, p.get());
      CGContextSetFillColorWithColor(ctx, c.get());
      CGContextFillPath(ctx);
    }, shape);
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
    const GlowShape shape{.path = &path, .stroke = &style, .color = color};
    glowed(strokeBounds(p.get(), style), [&](CGContextRef ctx) {
      if (ctx == context_.get() && coverageDraw(shape, p.get())) return;
      CGContextSaveGState(ctx);
      applyStroke(ctx, style);
      CGContextAddPath(ctx, p.get());
      CGContextSetStrokeColorWithColor(ctx, c.get());
      CGContextStrokePath(ctx);
      CGContextRestoreGState(ctx);
    }, shape);
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

  // A shape painted in one flat colour, which a small glow can be cast from in software.
  struct GlowShape final {
    const Path* path{nullptr};
    const StrokeStyle* stroke{nullptr};  // null: the path is filled
    Color color{};
  };

  // On a concurrent canvas, a translucent shape without a CoreGraphics shadow is drawn as opaque
  // coverage in an alpha-only mask (see ScopedConcurrentCanvas) and its colour composited here.
  bool coverageDraw(const GlowShape& shape, CGPathRef path) {
    if (!concurrent_ || cgShadow_ || !softwareState()) return false;
    if (shape.color.alpha == 255U && tracked_.alpha >= 1.0) return false;
    auto* ctx = context_.get();
    const auto ctm = CGContextGetCTM(ctx);
    // Only where the shape can paint inside the clip: the union of its pieces' control hulls,
    // grown by the stroke and the anti-aliased edge, so a long thin curve costs its length.
    const auto margin =
        (shape.stroke != nullptr ? 0.5 * std::max(0.0, shape.stroke->width) *
                                       std::max(std::abs(ctm.a) + std::abs(ctm.c),
                                                std::abs(ctm.b) + std::abs(ctm.d))
                                 : 0.0) +
        2.0;
    const auto device = hullBounds(*shape.path, ctm, margin, tracked_.clip);
    if (CGRectIsNull(device) || CGRectIsEmpty(device)) return true;
    const auto x0 = static_cast<std::int64_t>(std::floor(CGRectGetMinX(device)));
    const auto y0 = static_cast<std::int64_t>(std::floor(CGRectGetMinY(device)));
    const auto x1 = static_cast<std::int64_t>(std::ceil(CGRectGetMaxX(device)));
    const auto y1 = static_cast<std::int64_t>(std::ceil(CGRectGetMaxY(device)));
    // One alpha-only mask the size of the surface serves every shape: only the region is cleared,
    // drawn and read.
    const auto width = static_cast<std::int64_t>(surface_.width());
    const auto height = static_cast<std::int64_t>(surface_.height());
    if (!mask_) {
      coverage_.assign(static_cast<std::size_t>(width * height), 0U);
      mask_ = CfRef<CGContextRef>{CGBitmapContextCreate(coverage_.data(), static_cast<std::size_t>(width),
                                                        static_cast<std::size_t>(height), 8U,
                                                        static_cast<std::size_t>(width), nullptr,
                                                        kCGImageAlphaOnly)};
      if (!mask_) return false;
      CGContextSetShouldAntialias(mask_.get(), true);
      CGContextSetAllowsAntialiasing(mask_.get(), true);
      CGContextSetLineJoin(mask_.get(), kCGLineJoinRound);
    }
    for (auto y = y0; y < y1; ++y)
      std::memset(coverage_.data() + (height - 1 - y) * width + x0, 0, static_cast<std::size_t>(x1 - x0));
    auto* m = mask_.get();
    CGContextSaveGState(m);
    CGContextClipToRect(m, CGRectMake(static_cast<CGFloat>(x0), static_cast<CGFloat>(y0),
                                      static_cast<CGFloat>(x1 - x0), static_cast<CGFloat>(y1 - y0)));
    CGContextConcatCTM(m, ctm);
    // A long undashed stroke is given to CoreGraphics only where it passes near the region: the
    // run of its pieces that reach it, and one piece more at each cut end, so no cut end and no cap
    // lands in the region.
    CfRef<CGPathRef> reduced;
    if (shape.stroke != nullptr && shape.stroke->dash.empty())
      reduced = reducedStroke(*shape.path, ctm, margin, device);
    CGContextAddPath(m, reduced ? reduced.get() : path);
    if (shape.stroke != nullptr) {
      applyStroke(m, *shape.stroke);
      CGContextSetGrayStrokeColor(m, 1.0, 1.0);
      CGContextStrokePath(m);
    } else {
      CGContextSetGrayFillColor(m, 1.0, 1.0);
      CGContextFillPath(m);
    }
    CGContextRestoreGState(m);
    CGContextFlush(m);
    compositeCoverage(coverage_.data(), width, height, 0, 0, shape.color,
                      CGRectMake(static_cast<CGFloat>(x0), static_cast<CGFloat>(y0),
                                 static_cast<CGFloat>(x1 - x0), static_cast<CGFloat>(y1 - y0)));
    return true;
  }

  // Device bounds of what a path may paint inside clip: each piece's control points (cubics split
  // in eight) grown by margin, united.
  static CGRect hullBounds(const Path& path, CGAffineTransform ctm, double margin, CGRect clip) {
    auto result = CGRectNull;
    CGPoint last = CGPointZero;
    CGPoint start = CGPointZero;
    const auto add = [&](std::initializer_list<CGPoint> points) {
      auto minX = std::numeric_limits<double>::infinity();
      auto minY = minX;
      auto maxX = -minX;
      auto maxY = -minX;
      for (const auto& p : points) {
        minX = std::min<double>(minX, p.x);
        maxX = std::max<double>(maxX, p.x);
        minY = std::min<double>(minY, p.y);
        maxY = std::max<double>(maxY, p.y);
      }
      const auto piece = CGRectIntersection(
          CGRectMake(minX - margin, minY - margin, maxX - minX + 2.0 * margin, maxY - minY + 2.0 * margin),
          clip);
      if (!CGRectIsNull(piece) && !CGRectIsEmpty(piece)) result = CGRectUnion(result, piece);
    };
    const auto device = [&](ui::Point p) { return CGPointApplyAffineTransform(CGPointMake(p.x, p.y), ctm); };
    const auto lerp = [](CGPoint a, CGPoint b, double t) {
      return CGPointMake(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
    };
    for (const auto& e : path.elements()) {
      switch (e.verb) {
        case Path::Verb::Move:
          last = start = device(e.a);
          add({last});
          break;
        case Path::Verb::Line: {
          const auto p = device(e.a);
          add({last, p});
          last = p;
          break;
        }
        case Path::Verb::Quad: {
          const auto c = device(e.a);
          const auto p = device(e.b);
          add({last, c, p});
          last = p;
          break;
        }
        case Path::Verb::Cubic: {
          std::array<CGPoint, 4> q{last, device(e.a), device(e.b), device(e.c)};
          // Eight pieces: the first 1/8 of what remains, seven times, then the rest.
          for (int k = 0; k < 8; ++k) {
            if (k == 7) {
              add({q[0], q[1], q[2], q[3]});
              break;
            }
            const auto t = 1.0 / static_cast<double>(8 - k);
            const auto ab = lerp(q[0], q[1], t), bc = lerp(q[1], q[2], t), cd = lerp(q[2], q[3], t);
            const auto abc = lerp(ab, bc, t), bcd = lerp(bc, cd, t), mid = lerp(abc, bcd, t);
            add({q[0], ab, abc, mid});
            q = {mid, bcd, cd, q[3]};
          }
          last = device(e.c);
          break;
        }
        case Path::Verb::Close:
          add({last, start});
          last = start;
          break;
      }
    }
    return result;
  }

  // The pieces of a stroked path (lines, quads, cubics split in eight) near region, as runs of
  // whole pieces each with one more piece at either end; null when every piece is kept.
  static CfRef<CGPathRef> reducedStroke(const Path& path, CGAffineTransform ctm, double margin,
                                        CGRect region) {
    struct Piece final {
      int kind;  // 1 line, 2 quad, 3 cubic
      std::array<CGPoint, 4> p;  // user space
      bool near;
      bool starts;  // first piece of its subpath
      bool closes;  // the closing line of its subpath
    };
    std::vector<Piece> pieces;
    const auto nearRegion = [&](std::initializer_list<CGPoint> points) {
      auto minX = std::numeric_limits<double>::infinity();
      auto minY = minX;
      auto maxX = -minX;
      auto maxY = -minX;
      for (const auto& u : points) {
        const auto d = CGPointApplyAffineTransform(u, ctm);
        minX = std::min<double>(minX, d.x);
        maxX = std::max<double>(maxX, d.x);
        minY = std::min<double>(minY, d.y);
        maxY = std::max<double>(maxY, d.y);
      }
      return CGRectIntersectsRect(CGRectMake(minX - margin, minY - margin, maxX - minX + 2.0 * margin,
                                             maxY - minY + 2.0 * margin),
                                  region);
    };
    const auto lerp = [](CGPoint a, CGPoint b, double t) {
      return CGPointMake(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
    };
    const auto point = [](ui::Point p) { return CGPointMake(p.x, p.y); };
    CGPoint last = CGPointZero;
    CGPoint start = CGPointZero;
    bool first = true;
    for (const auto& e : path.elements()) {
      switch (e.verb) {
        case Path::Verb::Move:
          last = start = point(e.a);
          first = true;
          break;
        case Path::Verb::Line: {
          const auto p = point(e.a);
          pieces.push_back({1, {last, p, p, p}, nearRegion({last, p}), first, false});
          first = false;
          last = p;
          break;
        }
        case Path::Verb::Quad: {
          const auto c = point(e.a);
          const auto p = point(e.b);
          pieces.push_back({2, {last, c, p, p}, nearRegion({last, c, p}), first, false});
          first = false;
          last = p;
          break;
        }
        case Path::Verb::Cubic: {
          std::array<CGPoint, 4> q{last, point(e.a), point(e.b), point(e.c)};
          for (int k = 0; k < 8; ++k) {
            std::array<CGPoint, 4> piece = q;
            if (k < 7) {
              const auto t = 1.0 / static_cast<double>(8 - k);
              const auto ab = lerp(q[0], q[1], t), bc = lerp(q[1], q[2], t), cd = lerp(q[2], q[3], t);
              const auto abc = lerp(ab, bc, t), bcd = lerp(bc, cd, t), mid = lerp(abc, bcd, t);
              piece = {q[0], ab, abc, mid};
              q = {mid, bcd, cd, q[3]};
            }
            pieces.push_back({3, piece, nearRegion({piece[0], piece[1], piece[2], piece[3]}), first, false});
            first = false;
          }
          last = point(e.c);
          break;
        }
        case Path::Verb::Close:
          // A closed outline keeps its joins only whole: it is not reduced.
          return {};
      }
    }
    std::vector<bool> keep(pieces.size(), false);
    for (std::size_t i = 0U; i < pieces.size(); ++i) {
      if (!pieces[i].near) continue;
      keep[i] = true;
      if (i > 0U && !pieces[i].starts) keep[i - 1U] = true;
      if (i + 1U < pieces.size() && !pieces[i + 1U].starts) keep[i + 1U] = true;
    }
    if (std::all_of(keep.begin(), keep.end(), [](bool k) { return k; })) return {};
    CGMutablePathRef reduced = CGPathCreateMutable();
    bool open = false;
    for (std::size_t i = 0U; i < pieces.size(); ++i) {
      if (!keep[i]) {
        open = false;
        continue;
      }
      const auto& p = pieces[i];
      if (!open || p.starts) CGPathMoveToPoint(reduced, nullptr, p.p[0].x, p.p[0].y);
      open = true;
      if (p.kind == 1) CGPathAddLineToPoint(reduced, nullptr, p.p[1].x, p.p[1].y);
      else if (p.kind == 2) CGPathAddQuadCurveToPoint(reduced, nullptr, p.p[1].x, p.p[1].y, p.p[2].x, p.p[2].y);
      else CGPathAddCurveToPoint(reduced, nullptr, p.p[1].x, p.p[1].y, p.p[2].x, p.p[2].y, p.p[3].x, p.p[3].y);
    }
    return CfRef<CGPathRef>{reduced};
  }

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
  void glowed(CGRect userBounds, Draw&& draw, std::optional<GlowShape> shape = std::nullopt) {
    auto* ctx = context_.get();
    if (!glow_.on || glow_.radius <= 0.0) {
      if (glow_.on) {
        CGContextSaveGState(ctx);
        applyShadow(ctx);
        cgShadow_ = true;
        draw(ctx);
        cgShadow_ = false;
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
      if (shape && !halfResolutionGlowDisabled() && softwareState() && spriteGlow(*shape, blur, ctm)) {
        draw(ctx);
        return;
      }
      CGContextSaveGState(ctx);
      applyShadow(ctx);
      cgShadow_ = true;
      draw(ctx);
      cgShadow_ = false;
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
  // A sprite glow's faintest coverage, which the shadow's tail never shows, is dropped.
  static constexpr std::size_t kGlowFloor = 2U;
  // Where a sprite key keeps what it says (see spriteGlow).
  static constexpr std::size_t kKeyKind = 0U;
  static constexpr std::size_t kKeyAlpha = 1U;
  static constexpr std::size_t kKeyLineWidth = 7U;
  static constexpr std::size_t kKeyCaps = 8U;
  static constexpr std::size_t kKeyDashes = 9U;
  static constexpr double kGlowGrid = 64.0;
  static bool halfResolutionGlowDisabled() noexcept { return ScopedFullResolutionGlow::active(); }

  // A small glow cast in software: the shape's coverage (times its paint's alpha, as a shadow takes
  // it), blurred at full resolution by three box passes (the shadow's Gaussian, sigma half the
  // blur), is kept as a sprite and painted in the glow colour. The shape is rasterized from its
  // device geometry snapped to 1/64 pixel, the sprite's key, so the sprite depends on nothing else.
  bool spriteGlow(const GlowShape& shape, double blur, CGAffineTransform ctm) {
    if (ctm.b != 0.0 || ctm.c != 0.0 || ctm.a == 0.0 || std::abs(ctm.a) != std::abs(ctm.d))
      return false;
    const auto boxes = boxesFor(blur * 0.5);
    const auto s = std::abs(ctm.a);
    const auto half = shape.stroke != nullptr ? 0.5 * std::max(0.0, shape.stroke->width) * s : 0.0;
    const auto pad = boxes[0] / 2 + boxes[1] / 2 + boxes[2] / 2 + 2;  // the blur's reach, and the edge
    auto minX = std::numeric_limits<double>::infinity();
    auto minY = minX;
    auto maxX = -minX;
    auto maxY = -minX;
    const auto& elements = shape.path->elements();
    const auto points = [](Path::Verb verb) {
      switch (verb) {
        case Path::Verb::Move:
        case Path::Verb::Line: return 1;
        case Path::Verb::Quad: return 2;
        case Path::Verb::Cubic: return 3;
        case Path::Verb::Close: return 0;
      }
      return 0;
    };
    const auto point = [&](const Path::Element& e, int k) {
      const auto& p = k == 0 ? e.a : k == 1 ? e.b : e.c;
      return CGPointMake(ctm.a * p.x + ctm.tx, ctm.d * p.y + ctm.ty);
    };
    for (const auto& e : elements) {
      for (int k = 0; k < points(e.verb); ++k) {
        const auto d = point(e, k);
        if (!std::isfinite(d.x) || !std::isfinite(d.y)) return false;
        minX = std::min(minX, d.x);
        maxX = std::max(maxX, d.x);
        minY = std::min(minY, d.y);
        maxY = std::max(maxY, d.y);
      }
    }
    if (!(minX <= maxX)) return true;
    const auto x0 = static_cast<std::int64_t>(std::floor(minX - half)) - pad;
    const auto y0 = static_cast<std::int64_t>(std::floor(minY - half)) - pad;
    const auto x1 = static_cast<std::int64_t>(std::ceil(maxX + half)) + pad;
    const auto y1 = static_cast<std::int64_t>(std::ceil(maxY + half)) + pad;
    const auto width = x1 - x0;
    const auto height = y1 - y0;
    if (width * height > 2 * static_cast<std::int64_t>(kHalfResolutionGlowArea)) return false;
    const auto& clip = tracked_.clip;
    if (static_cast<double>(x1) <= CGRectGetMinX(clip) || static_cast<double>(x0) >= CGRectGetMaxX(clip) ||
        static_cast<double>(y1) <= CGRectGetMinY(clip) || static_cast<double>(y0) >= CGRectGetMaxY(clip))
      return true;  // none of the glow lands
    const auto q = [](double v) { return static_cast<std::int64_t>(std::llround(v * 64.0)); };
    std::vector<std::int64_t> key;
    key.reserve(16U + elements.size() * 7U);
    key.push_back(shape.stroke != nullptr ? 1 : 0);  // kKeyKind
    key.push_back(shape.color.alpha);                // kKeyAlpha
    key.push_back(width);                            // kKeyWidth
    key.push_back(height);                           // kKeyHeight
    for (const auto b : boxes) key.push_back(b);     // kKeyBoxes, three of them
    if (shape.stroke != nullptr) {
      key.push_back(q(std::max(0.0, shape.stroke->width) * s));             // kKeyLineWidth
      key.push_back(shape.stroke->roundCaps ? 1 : 0);                       // kKeyCaps
      key.push_back(static_cast<std::int64_t>(shape.stroke->dash.size()));  // kKeyDashes, then each
      for (const auto length : shape.stroke->dash) key.push_back(q(length * s));
    }
    const auto geometry = key.size();
    for (const auto& e : elements) {
      key.push_back(static_cast<std::int64_t>(e.verb));
      for (int k = 0; k < points(e.verb); ++k) {
        const auto d = point(e, k);
        key.push_back(q(d.x - static_cast<double>(x0)));
        key.push_back(q(d.y - static_cast<double>(y0)));
      }
    }
    std::uint64_t hash = 0x9E3779B97F4A7C15ULL;
    for (const auto v : key) {
      hash ^= static_cast<std::uint64_t>(v) + 0x9E3779B97F4A7C15ULL + (hash << 6U) + (hash >> 2U);
      hash *= 0xBF58476D1CE4E5B9ULL;
    }
    auto& sprites = GlowSprites::shared();
    auto sprite = sprites.find(hash, key);
    if (sprite == nullptr) {
      sprite = drawSprite(std::move(key), geometry, boxes, width, height);
      if (sprite == nullptr) return false;
      sprites.insert(hash, sprite);
    }
    // Without its faintest tail, as the shadow has none: a glow never tints what lies past its reach.
    compositeCoverage(sprite->coverage.data(), width, height, x0, y0, glow_.color, CGRectInfinite,
                      true);
    return true;
  }

  // Rasterizes a sprite's shape from its key alone and blurs it.
  static std::shared_ptr<const GlowSprite> drawSprite(std::vector<std::int64_t> key,
                                                      std::size_t geometry,
                                                      std::array<std::int64_t, 3> boxes,
                                                      std::int64_t width, std::int64_t height) {
    auto sprite = std::make_shared<GlowSprite>();
    sprite->width = width;
    sprite->height = height;
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    sprite->coverage.assign(w * h, 0U);
    CfRef<CGContextRef> mask{CGBitmapContextCreate(sprite->coverage.data(), w, h, 8U, w, nullptr,
                                                   kCGImageAlphaOnly)};
    if (!mask) return nullptr;
    auto* m = mask.get();
    CGContextSetShouldAntialias(m, true);
    CGContextSetAllowsAntialiasing(m, true);
    CGContextSetLineJoin(m, kCGLineJoinRound);
    const auto at = [&](std::size_t i) { return static_cast<CGFloat>(key[i]) / 64.0; };
    CfRef<CGMutablePathRef> path{CGPathCreateMutable()};
    for (auto i = geometry; i < key.size();) {
      switch (static_cast<Path::Verb>(key[i++])) {
        case Path::Verb::Move: CGPathMoveToPoint(path.get(), nullptr, at(i), at(i + 1U)); i += 2U; break;
        case Path::Verb::Line: CGPathAddLineToPoint(path.get(), nullptr, at(i), at(i + 1U)); i += 2U; break;
        case Path::Verb::Quad:
          CGPathAddQuadCurveToPoint(path.get(), nullptr, at(i), at(i + 1U), at(i + 2U), at(i + 3U));
          i += 4U;
          break;
        case Path::Verb::Cubic:
          CGPathAddCurveToPoint(path.get(), nullptr, at(i), at(i + 1U), at(i + 2U), at(i + 3U),
                                at(i + 4U), at(i + 5U));
          i += 6U;
          break;
        case Path::Verb::Close: CGPathCloseSubpath(path.get()); break;
      }
    }
    const auto alpha = static_cast<CGFloat>(key[kKeyAlpha]) / 255.0;
    CGContextAddPath(m, path.get());
    if (key[kKeyKind] == 1) {
      CGContextSetLineWidth(m, at(kKeyLineWidth));
      CGContextSetLineCap(m, key[kKeyCaps] != 0 ? kCGLineCapRound : kCGLineCapButt);
      if (const auto dashes = static_cast<std::size_t>(key[kKeyDashes]); dashes > 0U) {
        std::vector<CGFloat> lengths;
        for (std::size_t k = 0U; k < dashes; ++k) lengths.push_back(at(kKeyDashes + 1U + k));
        CGContextSetLineDash(m, 0.0, lengths.data(), lengths.size());
      }
      CGContextSetGrayStrokeColor(m, 1.0, alpha);
      CGContextStrokePath(m);
    } else {
      CGContextSetGrayFillColor(m, 1.0, alpha);
      CGContextFillPath(m);
    }
    CGContextFlush(m);
    std::vector<std::uint8_t> scratch(w * h, 0U);
    vImage_Buffer a{sprite->coverage.data(), h, w, w};
    vImage_Buffer b{scratch.data(), h, w, w};
    for (const auto box : boxes) {
      if (box <= 1) continue;
      const auto size = static_cast<std::uint32_t>(box);
      if (vImageBoxConvolve_Planar8(&a, &b, nullptr, 0, 0, size, size, 0,
                                    kvImageBackgroundColorFill) != kvImageNoError)
        return nullptr;
      std::swap(a, b);
    }
    if (a.data != sprite->coverage.data()) sprite->coverage.swap(scratch);
    sprite->key = std::move(key);
    return sprite;
  }

  // Three odd box widths whose passes together blur like a Gaussian of this sigma: m passes of the
  // widest odd width at most the ideal one and the rest two wider, m chosen for the nearest variance.
  static std::array<std::int64_t, 3> boxesFor(double sigma) {
    if (!(sigma > 0.0)) return {1, 1, 1};
    const auto ideal = std::sqrt(4.0 * sigma * sigma + 1.0);
    auto lower = static_cast<std::int64_t>(std::floor(ideal));
    if (lower % 2 == 0) --lower;
    lower = std::max<std::int64_t>(1, lower);
    const auto l = static_cast<double>(lower);
    const auto m = std::clamp<std::int64_t>(
        std::llround((12.0 * sigma * sigma - 3.0 * l * l - 12.0 * l - 9.0) / (-4.0 * l - 4.0)), 0, 3);
    std::array<std::int64_t, 3> boxes{};
    for (std::int64_t i = 0; i < 3; ++i) boxes[static_cast<std::size_t>(i)] = i < m ? lower : lower + 2;
    return boxes;
  }

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
                         std::int64_t left, std::int64_t bottom, Color color,
                         CGRect limit = CGRectInfinite, bool glowTail = false) {
    std::array<std::uint32_t, 256U> source{};
    for (std::size_t m = 1U; m < source.size(); ++m)
      source[m] = glowTail && m <= kGlowFloor ? 0U : premultiplied(color, static_cast<double>(m) / 255.0);
    const auto clip = CGRectIntersection(tracked_.clip, limit);
    if (CGRectIsNull(clip) || CGRectIsEmpty(clip)) return;
    const auto cx0 = std::max(left, static_cast<std::int64_t>(CGRectGetMinX(clip)));
    const auto cx1 = std::min(left + width, static_cast<std::int64_t>(CGRectGetMaxX(clip)));
    const auto cy0 = std::max(bottom, static_cast<std::int64_t>(CGRectGetMinY(clip)));
    const auto cy1 = std::min(bottom + height, static_cast<std::int64_t>(CGRectGetMaxY(clip)));
    for (auto y = cy0; y < cy1; ++y) {
      const auto* mask = coverage + (bottom + height - 1 - y) * width - left;
      auto* pixels = row(y);
      for (auto x = cx0; x < cx1;) {
        // A glow's coverage is mostly empty: eight empty samples are passed over at once.
        if (x + 8 <= cx1) {
          std::uint64_t eight = 0U;
          std::memcpy(&eight, mask + x, sizeof eight);
          if (eight == 0U) {
            x += 8;
            continue;
          }
        }
        const auto m = mask[x];
        if (m != 0U) pixels[x] = over(source[m], pixels[x]);
        ++x;
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
      (__bridge id)kCTFontAttributeName : textFont(style),
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
  bool concurrent_{false};
  bool cgShadow_{false};  // a CoreGraphics shadow is set around the drawing in progress
  std::vector<std::uint8_t> coverage_;
  CfRef<CGContextRef> mask_;  // alpha-only, over coverage_, made on first use
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

std::size_t glowSpriteCacheBytes() noexcept { return GlowSprites::shared().bytes(); }
bool textRenderable(std::string_view utf8, const TextStyle& style) {
  @autoreleasepool {
    NSString* string = [[NSString alloc] initWithBytes:utf8.data()
                                                length:utf8.size()
                                              encoding:NSUTF8StringEncoding];
    if (string == nil || string.length == 0) return false;
    NSAttributedString* attributed = [[NSAttributedString alloc]
        initWithString:string
            attributes:@{(__bridge id)kCTFontAttributeName : textFont(style)}];
    CfRef<CTLineRef> line{CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)attributed)};
    if (!line) return false;
    const CFArrayRef runs = CTLineGetGlyphRuns(line.get());
    CFIndex glyphs = 0;
    for (CFIndex i = 0; i < CFArrayGetCount(runs); ++i) {
      const auto run = static_cast<CTRunRef>(CFArrayGetValueAtIndex(runs, i));
      const auto font = static_cast<CTFontRef>(
          CFDictionaryGetValue(CTRunGetAttributes(run), kCTFontAttributeName));
      if (font == nullptr) return false;
      CfRef<CFStringRef> name{CTFontCopyPostScriptName(font)};
      if (name && CFStringCompare(name.get(), CFSTR("LastResort"), 0) == kCFCompareEqualTo) return false;
      const auto count = CTRunGetGlyphCount(run);
      std::vector<CGGlyph> ids(static_cast<std::size_t>(count));
      CTRunGetGlyphs(run, CFRangeMake(0, count), ids.data());
      if (std::find(ids.begin(), ids.end(), CGGlyph{0}) != ids.end()) return false;
      glyphs += count;
    }
    return glyphs > 0;
  }
}

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

std::filesystem::path locateBundledFonts() {
  std::vector<std::filesystem::path> candidates;
  if (const char* root = std::getenv("SEAM_UI_FONTS"); root != nullptr && *root != '\0') {
    if (std::string_view{root} == "system") return {};
    candidates.emplace_back(root);
  }
  if (const auto own = codeBundleResources(); !own.empty()) candidates.push_back(own / "fonts");
#if defined(SEAM_UI_FONTS_SOURCE)
  candidates.emplace_back(SEAM_UI_FONTS_SOURCE);
#endif
  for (const auto& candidate : candidates) {
    std::error_code error;
    if (std::filesystem::is_regular_file(candidate / "manifest.json", error)) return candidate;
  }
  return {};
}

BundledFonts registerBundledFonts(const std::filesystem::path& directory) {
  return registerInto(directory, nullptr);
}

const BundledFonts& bundledFonts() {
  static BundledFonts fonts;
  static std::once_flag once;
  std::call_once(once, [] { fonts = registerInto(locateBundledFonts(), &processDescriptors()); });
  return fonts;
}

std::string fontFaceName(FontRole role) {
  @autoreleasepool {
    NSFont* font = fontFor(TextStyle{.role = role, .size = 13.0});
    return font.fontName != nil ? std::string{font.fontName.UTF8String} : std::string{};
  }
}

}  // namespace seam::native_ui::paint
