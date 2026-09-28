#include "seam/native_ui/design/background_wash.hpp"

#include "seam/native_ui/paint/round_rect_fill.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
#include <vector>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace seam::native_ui::design {
namespace {

constexpr Color kWhite{255, 255, 255, 255};

// The procedural wash has no text or layout-dependent chrome. Device rows are independent, so
// each compositor band can rasterize it without creating a CoreGraphics path or sharing pixels.
struct WashShapes final {
  struct Segment final {
    ui::Point before;
    ui::Point after;
    double vx;
    double vy;
    double length2;
  };
  struct Strand final {
    std::array<ui::Point, 65U> points;
    std::array<Segment, 64U> segments;
    Color color;
    double width;
  };
  struct Sparkle final {
    ui::Point point;
    Color color;
    double radius;
    bool circle;
  };
  double width{-1.0};
  double height{-1.0};
  double scale{-1.0};
  const DesignTokens* tokens{nullptr};
  std::vector<Strand> strands;
  std::vector<Sparkle> sparkles;
};

std::shared_ptr<const WashShapes> washShapes(double W, double H, double scale, const DesignTokens& t) {
  // Bands replay concurrently. Build the immutable seeded geometry once for the entire surface,
  // rather than repeating 170 cubic subdivisions on every worker or every band.
  static std::mutex mutex;
  static std::shared_ptr<const WashShapes> cached;
  const std::lock_guard lock{mutex};
  if (cached && cached->width == W && cached->height == H &&
      cached->scale == scale && cached->tokens == &t) return cached;
  auto result = std::make_shared<WashShapes>();
  auto& shapes = *result;
  shapes.width = W;
  shapes.height = H;
  shapes.scale = scale;
  shapes.tokens = &t;
  std::mt19937 rng{t.mode == DesignMode::Emo ? 0x5EA1u : 0x5CE7u};
  const auto uniform = [&](double lo, double hi) {
    return std::uniform_real_distribution<double>{lo, hi}(rng);
  };
  if (t.mode == DesignMode::Emo) {
    shapes.strands.reserve(170U);
    for (int i = 0; i < 170; ++i) {
      const ui::Point a{uniform(-0.1, 1.1) * W, uniform(-0.1, 1.1) * H};
      const ui::Point d{uniform(-0.1, 1.1) * W, uniform(-0.1, 1.1) * H};
      const ui::Point b{uniform(0, W), uniform(0, H)};
      const ui::Point c{uniform(0, W), uniform(0, H)};
      const auto red = uniform(0.0, 1.0) < 0.7;
      WashShapes::Strand strand{};
      strand.color = withAlpha(red ? t.color.texturePrimary : t.color.textureSecondary,
                               uniform(0.25, 1.0) * t.light.textureAlpha * (red ? 1.9 : 0.8));
      strand.width = uniform(0.5, 1.4);
      for (std::size_t s = 0; s < strand.points.size(); ++s) {
        const auto u = static_cast<double>(s) / 64.0;
        const auto v = 1.0 - u;
        strand.points[s] = {v*v*v*a.x + 3*v*v*u*b.x + 3*v*u*u*c.x + u*u*u*d.x,
                            v*v*v*a.y + 3*v*v*u*b.y + 3*v*u*u*c.y + u*u*u*d.y};
      }
      for (std::size_t s = 0; s < strand.segments.size(); ++s) {
        const ui::Point before{strand.points[s].x * scale, strand.points[s].y * scale};
        const ui::Point after{strand.points[s + 1U].x * scale, strand.points[s + 1U].y * scale};
        const auto vx = after.x - before.x;
        const auto vy = after.y - before.y;
        strand.segments[s] = {before, after, vx, vy, vx * vx + vy * vy};
      }
      shapes.strands.push_back(std::move(strand));
    }
  } else {
    shapes.sparkles.reserve(1400U);
    const std::array<Color, 4U> colors{t.color.accent, t.color.accentCurve,
                                       t.color.accentAlt1, kWhite};
    for (int i = 0; i < 1400; ++i) {
      const ui::Point point{uniform(0, W), uniform(0, H)};
      const auto color = withAlpha(colors[static_cast<std::size_t>(uniform(0, 3.999))],
                                   uniform(0.15, 1.0) * t.light.textureAlpha * 4.0);
      const auto circle = uniform(0.0, 1.0) < 0.82;
      shapes.sparkles.push_back({point, color,
                                 circle ? uniform(0.35, 1.1) : uniform(2.0, 4.5), circle});
    }
  }
  cached = result;
  return result;
}

}  // namespace

void paintBackgroundWash(PixelSurface& surface, double scale, std::uint32_t top,
                         std::uint32_t fullHeight, const DesignTokens& t) {
  const auto width = surface.width();
  const auto height = surface.height();
  const double W = static_cast<double>(width) / scale;
  const double H = static_cast<double>(fullHeight) / scale;
  surface.clear(t.color.canvas);
  auto pixels = surface.pixels();
  const auto blend = [&](std::size_t index, Color color, double coverage) {
    const double a = std::clamp(coverage * (color.alpha / 255.0), 0.0, 1.0);
    if (a <= 0.0) return;
    const auto old = pixels[index];
    const auto channel = [&](unsigned shift, std::uint8_t source) {
      return static_cast<std::uint32_t>(std::lround(source * a +
          static_cast<double>((old >> shift) & 255U) * (1.0 - a)));
    };
    pixels[index] = channel(0U, color.blue) | (channel(8U, color.green) << 8U) |
                    (channel(16U, color.red) << 16U) | 0xFF000000U;
  };
  struct GradientSample final { std::uint16_t blue, green, red, inverse; };
  struct GradientTable final {
    bool valid{false};
    std::uint32_t color{0U};
    std::array<GradientSample, 4097U> rampSamples{};
    // Index by squared radius so the inner pixel loop needs no square root. Each entry is the
    // ramp sample the original lookup picks at that squared radius, so the two differ only where
    // rounding the squared radius moves the ramp index.
    std::array<GradientSample, 32769U> radiusSamples{};
  };
  thread_local std::array<GradientTable, 2U> gradientTables;
  std::size_t gradientIndex = 0U;
  // Read once per band: the environment lock is shared by every worker.
  const auto exactSqrt = std::getenv("SEAM_WASH_EXACT_SQRT") != nullptr;
  const auto gradient = [&](ui::Point center, double radius, Color color) {
    auto& table = gradientTables[gradientIndex++];
    if (!table.valid || table.color != color.bgra()) {
      table.valid = true;
      table.color = color.bgra();
      const auto sample = [&](double ramp) {
        const auto alpha = (color.alpha / 255.0) * ramp;
        return GradientSample{
            static_cast<std::uint16_t>(std::lround(color.blue * ramp * alpha)),
            static_cast<std::uint16_t>(std::lround(color.green * ramp * alpha)),
            static_cast<std::uint16_t>(std::lround(color.red * ramp * alpha)),
            static_cast<std::uint16_t>(255U - static_cast<unsigned>(std::lround(alpha * 255.0)))};
      };
      for (std::size_t i = 0; i < table.rampSamples.size(); ++i)
        table.rampSamples[i] = sample(static_cast<double>(i) / 4096.0);
      for (std::size_t i = 0; i < table.radiusSamples.size(); ++i)
        table.radiusSamples[i] = table.rampSamples[std::min(4096U, static_cast<unsigned>(
            (1.0 - std::sqrt(static_cast<double>(i) / 32768.0)) * 4096.0 + 0.5))];
    }
    std::vector<double> xDistance2(width);
    for (std::uint32_t x = 0; x < width; ++x) {
      const auto dx = (static_cast<double>(x) + 0.5) / scale - center.x;
      xDistance2[x] = dx * dx;
    }
    const auto radius2 = radius * radius;
    const auto radiusIndexScale = 32768.0 / radius2;
    for (std::uint32_t y = 0; y < height; ++y) {
      const auto py = (static_cast<double>(top + y) + 0.5) / scale;
      const auto dy = py - center.y;
      const auto dy2 = dy * dy;
      if (dy2 >= radius2) continue;
      const auto halfRow = std::sqrt(radius2 - dy2) * scale;
      const auto centerX = center.x * scale - 0.5;
      const auto x0 = static_cast<std::uint32_t>(std::clamp(
          std::ceil(centerX - halfRow), 0.0, static_cast<double>(width)));
      const auto x1 = static_cast<std::uint32_t>(std::clamp(
          std::floor(centerX + halfRow) + 1.0, 0.0, static_cast<double>(width)));
      auto x = x0;
#if defined(__ARM_NEON)
      if (!exactSqrt) {
        // Two pixels at a time, with the scalar loop's arithmetic: the same double distance and
        // table index, then the same rounded blend, where (p * inverse + 127) / 255 is computed as
        // (v + 1 + (v >> 8)) >> 8, equal to v / 255 for every v up to 255 * 255 + 127. A pixel
        // outside the circle keeps its value.
        static_assert(sizeof(GradientSample) == 4U * sizeof(std::uint16_t));
        const auto dy2Lanes = vdupq_n_f64(dy2);
        const auto radius2Lanes = vdupq_n_f64(radius2);
        const auto indexScale = vdupq_n_f64(radiusIndexScale);
        const auto half = vdupq_n_f64(0.5);
        const auto rounding = vdupq_n_u16(127U);
        const auto one = vdupq_n_u16(1U);
        const auto lastIndex = vdup_n_u32(32768U);
        const auto opaque = vdup_n_u32(0xFF000000U);
        auto* row = pixels.data() + static_cast<std::size_t>(y) * width;
        for (; x + 2U <= x1; x += 2U) {
          const auto distance2 = vaddq_f64(vld1q_f64(xDistance2.data() + x), dy2Lanes);
          const auto inside = vmovn_u64(vcltq_f64(distance2, radius2Lanes));
          const auto index = vmin_u32(
              vmovn_u64(vcvtq_u64_f64(vfmaq_f64(half, distance2, indexScale))), lastIndex);
          std::uint16_t lanes[8];
          std::memcpy(lanes, &table.radiusSamples[vget_lane_u32(inside, 0) != 0U
                                                      ? vget_lane_u32(index, 0) : 0U], 8U);
          std::memcpy(lanes + 4, &table.radiusSamples[vget_lane_u32(inside, 1) != 0U
                                                          ? vget_lane_u32(index, 1) : 0U], 8U);
          const auto samples = vld1q_u16(lanes);
          const auto inverse = vcombine_u16(vdup_lane_u16(vget_low_u16(samples), 3),
                                            vdup_lane_u16(vget_high_u16(samples), 3));
          const auto held = vld1_u32(row + x);
          const auto product = vmlaq_u16(rounding, vmovl_u8(vreinterpret_u8_u32(held)), inverse);
          const auto kept =
              vshrq_n_u16(vaddq_u16(vaddq_u16(product, one), vshrq_n_u16(product, 8)), 8);
          const auto blended = vorr_u32(
              vreinterpret_u32_u8(vmovn_u16(vaddq_u16(kept, samples))), opaque);
          vst1_u32(row + x, vbsl_u32(inside, blended, held));
        }
      }
#endif
      for (; x < x1; ++x) {
        const auto distance2 = xDistance2[x] + dy2;
        if (distance2 < radius2) {
          const auto s = exactSqrt
              ? table.rampSamples[std::min(4096U, static_cast<unsigned>(
                    (1.0 - std::sqrt(distance2) / radius) * 4096.0 + 0.5))]
              : table.radiusSamples[std::min(32768U, static_cast<unsigned>(
                    distance2 * radiusIndexScale + 0.5))];
          if (s.inverse == 255U && s.blue == 0U && s.green == 0U && s.red == 0U) continue;
          auto& pixel = pixels[static_cast<std::size_t>(y) * width + x];
          const auto channel = [&](unsigned shift, std::uint16_t source) {
            return static_cast<std::uint32_t>(source +
                (((pixel >> shift) & 255U) * s.inverse + 127U) / 255U);
          };
          pixel = channel(0U, s.blue) | (channel(8U, s.green) << 8U) |
                  (channel(16U, s.red) << 16U) | 0xFF000000U;
        }
      }
    }
  };
  if (t.mode == DesignMode::Emo) {
    gradient({W * 0.18, H * 0.05}, W * 0.55, withAlpha(t.color.accentDeep, 0.35));
    gradient({W * 0.86, H * 0.42}, W * 0.40, withAlpha(t.color.accent, 0.18));
  } else {
    gradient({W * 0.12, H * 0.1}, W * 0.50, withAlpha(t.color.accentAlt1, 0.32));
    gradient({W * 0.92, H * 0.85}, W * 0.45, withAlpha(t.color.accent, 0.22));
  }
  if (t.light.textureAlpha <= 0.0) return;

  const auto shapes = washShapes(W, H, scale, t);
  const auto localY = [&](double logical) { return logical * scale - static_cast<double>(top); };
  if (t.mode == DesignMode::Emo) {
    // A strand is one translucent shape. Keep the largest coverage at joins and composite once.
    // Every entry is back to zero after each strand, so a worker keeps its buffer between bands.
    thread_local std::vector<float> coverage;
    thread_local std::vector<std::size_t> touched;
    if (coverage.size() < pixels.size()) coverage.resize(pixels.size(), 0.0F);
    for (const auto& strand : shapes->strands) {
      const auto strokeWidth = strand.width * scale;
      touched.clear();
      for (const auto& segment : strand.segments) {
        const auto before = ui::Point{segment.before.x, segment.before.y - static_cast<double>(top)};
        const auto after = ui::Point{segment.after.x, segment.after.y - static_cast<double>(top)};
        const auto radius = strokeWidth * 0.5;
        const auto outside2 = (radius + 0.5) * (radius + 0.5);
        const auto inside2 = radius >= 0.5 ? (radius - 0.5) * (radius - 0.5) : -1.0;
        if (std::max(before.y, after.y) + radius + 1.0 < 0.0 ||
            std::min(before.y, after.y) - radius - 1.0 >= height) {
          continue;
        }
        const auto vx = segment.vx;
        const auto vy = segment.vy;
        const auto length2 = segment.length2;
        const auto x0 = std::max(0, static_cast<int>(std::floor(std::min(before.x, after.x) - radius - 1)));
        const auto x1 = std::min(static_cast<int>(width), static_cast<int>(std::ceil(std::max(before.x, after.x) + radius + 1)));
        const auto y0 = std::max(0, static_cast<int>(std::floor(std::min(before.y, after.y) - radius - 1)));
        const auto y1 = std::min(static_cast<int>(height), static_cast<int>(std::ceil(std::max(before.y, after.y) + radius + 1)));
        // A painted pixel lies within radius + 0.5 of the segment, so within that distance of its
        // line: |px * vy - py * vx| < (radius + 0.5) * length. On each row that is one span of x.
        // The span below keeps another half pixel of distance and one pixel each side; the pixels
        // it skips fail the distance test, so the strand's pixels are unchanged.
        const auto length = std::sqrt(length2);
        const auto spanned = length >= 1.0e-3;
        const auto reach = (radius + 1.0) * length;
        for (auto y = y0; y < y1; ++y) {
          const auto py = y + 0.5 - before.y;
          auto rowX0 = x0;
          auto rowX1 = x1;
          if (spanned) {
            const auto across = py * vx;
            if (std::abs(vy) > 1.0e-9) {
              const auto a = (across - reach) / vy;
              const auto b = (across + reach) / vy;
              // px = x + 0.5 - before.x
              const auto lo = std::floor(std::min(a, b) + before.x - 0.5) - 1.0;
              const auto hi = std::ceil(std::max(a, b) + before.x - 0.5) + 2.0;
              rowX0 = static_cast<int>(std::clamp(lo, static_cast<double>(x0), static_cast<double>(x1)));
              rowX1 = static_cast<int>(std::clamp(hi, static_cast<double>(x0), static_cast<double>(x1)));
            } else if (std::abs(across) >= reach) {
              continue;
            }
          }
          for (auto x = rowX0; x < rowX1; ++x) {
            const auto px = x + 0.5 - before.x;
            const auto u = length2 > 0.0 ? std::clamp((px * vx + py * vy) / length2, 0.0, 1.0) : 0.0;
            const auto dx = px - u * vx;
            const auto dy = py - u * vy;
            const auto distance2 = dx * dx + dy * dy;
            if (distance2 >= outside2) continue;
            const auto value = static_cast<float>(distance2 <= inside2 ? 1.0
                : radius + 0.5 - std::sqrt(distance2));
            if (value <= 0.0F) continue;
            const auto index = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
            if (coverage[index] == 0.0F) { coverage[index] = value; touched.push_back(index); }
            else coverage[index] = std::max(coverage[index], value);
          }
        }
      }
      for (const auto index : touched) {
        blend(index, strand.color, coverage[index]);
        coverage[index] = 0.0F;
      }
    }
  } else {
    // A subsample's offset from the centre depends on its column or its row alone. Each is
    // computed once per sparkle, with the same expression, and shared by the tests that use it.
    thread_local std::vector<double> columns;
    for (const auto& sparkle : shapes->sparkles) {
      const auto r = sparkle.radius;
      const auto cx = sparkle.point.x * scale;
      const auto cy = localY(sparkle.point.y);
      const auto radius = r * scale;
      const auto x0 = std::max(0, static_cast<int>(std::floor(cx - radius - 1)));
      const auto x1 = std::min(static_cast<int>(width), static_cast<int>(std::ceil(cx + radius + 1)));
      const auto y0 = std::max(0, static_cast<int>(std::floor(cy - radius - 1)));
      const auto y1 = std::min(static_cast<int>(height), static_cast<int>(std::ceil(cy + radius + 1)));
      if (x0 >= x1 || y0 >= y1) continue;
      columns.resize(static_cast<std::size_t>(x1 - x0) * 4U);
      for (auto x = x0; x < x1; ++x)
        for (int sx = 0; sx < 4; ++sx)
          columns[static_cast<std::size_t>(x - x0) * 4U + static_cast<std::size_t>(sx)] =
              (x + (sx + 0.5) * 0.25 - cx) / scale;
      for (auto y = y0; y < y1; ++y) {
        std::array<double, 4U> rows{};
        for (int sy = 0; sy < 4; ++sy)
          rows[static_cast<std::size_t>(sy)] = (y + (sy + 0.5) * 0.25 - cy) / scale;
        for (auto x = x0; x < x1; ++x) {
          const auto* offsets = columns.data() + static_cast<std::size_t>(x - x0) * 4U;
          double covered = 0.0;
          for (int sy = 0; sy < 4; ++sy) for (int sx = 0; sx < 4; ++sx) {
            const auto dx = offsets[sx];
            const auto dy = rows[static_cast<std::size_t>(sy)];
            if (sparkle.circle) covered += dx * dx + dy * dy <= r * r ? 1.0 : 0.0;
            else {
              // In one quadrant the polygon runs (0,r) -> (.22r,.22r) -> (r,0).
              const auto ax = std::abs(dx);
              const auto ay = std::abs(dy);
              const auto inside = ax <= r && ay <=
                  (ax <= r * 0.22 ? r - ax * (0.78 / 0.22) : (r - ax) * (0.22 / 0.78));
              covered += inside ? 1.0 : 0.0;
            }
          }
          if (covered > 0.0) blend(static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x),
                                   sparkle.color, covered / 16.0);
        }
      }
    }
  }
}

void paintGlassPanelFills(PixelSurface& band, double scale, std::uint32_t top,
                          std::span<const GlassPanelFill> fills) {
  // The band's rows lie `top` device rows down the surface; the dither follows the whole surface.
  const paint::DeviceFillTarget target{.clipLeft = 0,
                                       .clipTop = 0,
                                       .clipRight = band.width(),
                                       .clipBottom = band.height(),
                                       .ditherColumn = 0,
                                       .ditherRow = top};
  const auto offset = static_cast<double>(top);
  for (const auto& fill : fills) {
    const auto& r = fill.rect;
    if (r.width <= 0.0 || r.height <= 0.0 || fill.opacity <= 0.0) continue;
    const paint::DeviceRoundRect shape{.left = r.x * scale,
                                       .top = r.y * scale - offset,
                                       .right = r.right() * scale,
                                       .bottom = r.bottom() * scale - offset,
                                       .radius = fill.radius * scale};
    paint::fillRoundRectVertical(band, shape, target, fill.top, fill.bottom, shape.top, shape.bottom,
                                 fill.opacity);
  }
}

}  // namespace seam::native_ui::design
