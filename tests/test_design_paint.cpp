// Plan section 14.1: measure painted coverage and colour, not path bounds.
#include "test_framework.hpp"

#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/pixel_surface.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>

namespace {

using seam::native_ui::Color;
using seam::native_ui::PixelSurface;
using seam::native_ui::paint::Path;
namespace paint = seam::native_ui::paint;
namespace ui = seam::ui;

constexpr Color kBlack{0, 0, 0, 255};
constexpr Color kWhite{255, 255, 255, 255};

bool backendOrSkip() {
  if (paint::vectorBackendAvailable()) return true;
  std::cout << "[SKIP] vector paint backend unavailable\n";
  return false;
}

// Opaque white over opaque black yields each channel = round(255 * coverage).
// The integer sum divided by 255 unmixes coverage; edge quantization contributes
// at most 1/510 per sampled pixel rather than counting every touched pixel as one.
double whiteCoverage(const PixelSurface& surface) {
  double sum = 0.0;
  for (const auto pixel : surface.pixels()) sum += static_cast<double>((pixel >> 16U) & 255U) / 255.0;
  return sum;
}

double whiteCoverageAtX(const PixelSurface& surface, std::uint32_t x) {
  double sum = 0.0;
  for (std::uint32_t y = 0; y < surface.height(); ++y)
    sum += static_cast<double>((surface.pixels()[static_cast<std::size_t>(y) * surface.width() + x] >> 16U) & 255U) / 255.0;
  return sum;
}

int channel(std::uint32_t pixel, unsigned shift) { return static_cast<int>((pixel >> shift) & 255U); }

// Sum positive channel contributions above an opaque black background, in
// 8-bit RGB channel units. Subtracting an identical unglowed drawing isolates
// the visible halo. The opaque shape masks any glow beneath it.
double glowEnergy(const PixelSurface& glowed, const PixelSurface& plain) {
  double energy = 0.0;
  for (std::size_t i = 0; i < glowed.pixels().size(); ++i) {
    for (const unsigned shift : {0U, 8U, 16U})
      energy += std::max(0, channel(glowed.pixels()[i], shift) - channel(plain.pixels()[i], shift));
  }
  return energy;
}

void drawReference(PixelSurface& surface, bool glow) {
  surface.clear(kBlack);
  auto canvas = paint::makeCanvas(surface, 1.0);
  CHECK(canvas != nullptr);
  if (glow) canvas->setGlow(Color{240, 80, 20, 192}, 8.0);
  canvas->fill(Path::circle({64.0, 64.0}, 15.0), Color{255, 255, 255, 255});
  canvas->flush();
}

}  // namespace

TEST_CASE("paint: white-on-black circle coverage is within 0.5 percent of pi r squared") {
  if (!backendOrSkip()) return;
  PixelSurface surface{192, 192};
  surface.clear(kBlack);
  auto canvas = paint::makeCanvas(surface, 1.0);
  CHECK(canvas != nullptr);
  constexpr double radius = 55.0;
  canvas->fill(Path::circle({96.0, 96.0}, radius), kWhite);
  canvas->flush();
  const double measured = whiteCoverage(surface);
  const double analytic = std::numbers::pi * radius * radius;
  std::cout << "circle coverage=" << measured << " analytic=" << analytic
            << " error_percent=" << 100.0 * std::abs(measured / analytic - 1.0) << '\n';
  CHECK(std::abs(measured / analytic - 1.0) <= 0.005);
}

TEST_CASE("paint: white-on-black rounded rectangle coverage is within 0.5 percent") {
  if (!backendOrSkip()) return;
  PixelSurface surface{240, 180};
  surface.clear(kBlack);
  auto canvas = paint::makeCanvas(surface, 1.0);
  CHECK(canvas != nullptr);
  constexpr double width = 150.0, height = 90.0, radius = 25.0;
  canvas->fill(Path::roundedRect({45.0, 45.0, width, height}, radius), kWhite);
  canvas->flush();
  const double measured = whiteCoverage(surface);
  // Four square corner cutouts minus four quarter circles.
  const double analytic = width * height - (4.0 - std::numbers::pi) * radius * radius;
  std::cout << "rounded_rect coverage=" << measured << " analytic=" << analytic
            << " error_percent=" << 100.0 * std::abs(measured / analytic - 1.0) << '\n';
  CHECK(std::abs(measured / analytic - 1.0) <= 0.005);
}

TEST_CASE("paint: white-on-black perpendicular stroke coverage is within 0.25 pixels") {
  if (!backendOrSkip()) return;
  PixelSurface surface{160, 100};
  surface.clear(kBlack);
  auto canvas = paint::makeCanvas(surface, 1.0);
  CHECK(canvas != nullptr);
  Path line;
  line.moveTo({20.0, 50.0}).lineTo({140.0, 50.0});
  constexpr double requested = 7.4;
  canvas->stroke(line, kWhite, paint::StrokeStyle{requested, false});
  canvas->flush();
  const double measured = whiteCoverageAtX(surface, 80);
  std::cout << "stroke width=" << measured << " requested=" << requested
            << " error_px=" << std::abs(measured - requested) << '\n';
  CHECK(std::abs(measured - requested) <= 0.25);
}

TEST_CASE("paint: first and last linear-gradient pixels exactly match sRGB endpoint colours") {
  if (!backendOrSkip()) return;
  PixelSurface surface{128, 32};
  surface.clear(kBlack);
  auto canvas = paint::makeCanvas(surface, 1.0);
  CHECK(canvas != nullptr);
  // Sample mixed, non-primary colours beyond the stop positions: endpoint extension
  // must reproduce them exactly. A sample at a stop boundary can include a fractional
  // contribution from the adjacent gradient pixel, so it is not a pure endpoint sample.
  constexpr Color first{240, 48, 12, 255}, last{16, 96, 224, 255};
  paint::LinearGradient gradient{{11.5, 16.0}, {109.5, 16.0}, {{0.0, first}, {1.0, last}}};
  canvas->fill(Path::rect({10.0, 8.0, 101.0, 16.0}), gradient);
  canvas->flush();
  const auto a = surface.pixels()[16U * surface.width() + 10U];
  const auto b = surface.pixels()[16U * surface.width() + 110U];
  std::cout << "gradient first_rgb=" << channel(a, 16) << ',' << channel(a, 8) << ',' << channel(a, 0)
            << " last_rgb=" << channel(b, 16) << ',' << channel(b, 8) << ',' << channel(b, 0) << '\n';
  // These declared sRGB endpoints must survive the colour conversion exactly.
  for (const auto [pixel, color] : {std::pair{a, first}, std::pair{b, last}}) {
    CHECK(channel(pixel, 16) == color.red);
    CHECK(channel(pixel, 8) == color.green);
    CHECK(channel(pixel, 0) == color.blue);
    CHECK(channel(pixel, 24) == 255);
  }
}

TEST_CASE("paint: glow additive RGB energy is within 2 percent of reference") {
  if (!backendOrSkip()) return;
  PixelSurface glowed{128, 128}, plain{128, 128};
  drawReference(glowed, true);
  drawReference(plain, false);
  // Regression reference measured from the current macOS CoreGraphics backend
  // for the fixed 15 px white circle, RGBA(240, 80, 20, 192) glow and 8 px
  // radius above. This is an output baseline, not an independent blur oracle.
  // It includes all visible RGB channel deltas within the 128 x 128 surface.
  constexpr double reference = 36756.0;
  const double measured = glowEnergy(glowed, plain);
  std::cout << "glow energy=" << measured << " reference=" << reference << '\n';
  CHECK(std::abs(measured / reference - 1.0) <= 0.02);
}

TEST_CASE("paint: repeated identical drawing has identical pixel hashes") {
  if (!backendOrSkip()) return;
  PixelSurface a{128, 128}, b{128, 128};
  drawReference(a, true);
  drawReference(b, true);
  std::cout << "repeat hashes=" << a.checksum() << ',' << b.checksum() << '\n';
  CHECK(a.checksum() == b.checksum());
  CHECK(std::equal(a.pixels().begin(), a.pixels().end(), b.pixels().begin()));
}

namespace {

// Note capsules at fractional positions, a translucent glass panel and knob bodies (one at 0.4
// opacity, as a refused knob draws) over bars of varied colour: the fills the canvas draws in
// software, over a background that makes every coverage and blend visible.
void drawGradientShapes(paint::Canvas2D& c) {
  for (int i = 0; i < 16; ++i) {
    const auto step = static_cast<std::uint8_t>(i);
    c.fill(Path::rect({i * 16.0, 0.0, 16.0, 120.0}),
           Color{static_cast<std::uint8_t>(20U + step * 13U), static_cast<std::uint8_t>(200U - step * 9U),
                 static_cast<std::uint8_t>(90U + step * 5U), 255});
  }
  for (int i = 0; i < 5; ++i) {
    const ui::Rect r{7.3 + i * 47.1, 10.45 + i * 3.7, 38.6, 13.9};
    c.fill(Path::roundedRect(r, 6.95),
           paint::LinearGradient{{r.x, r.y}, {r.x, r.bottom()},
                                 {{0.0, Color{205, 60, 90, 255}}, {1.0, Color{25, 20, 30, 255}}}});
  }
  c.save();
  c.setAlpha(0.9);
  c.fill(Path::roundedRect({4.0, 40.0, 200.0, 70.0}, 12.0),
         paint::LinearGradient{{4.0, 40.0}, {4.0, 110.0},
                               {{0.0, Color{27, 24, 29, 255}}, {1.0, Color{19, 17, 21, 255}}}});
  c.restore();
  for (int i = 0; i < 3; ++i) {
    const ui::Point k{40.0 + i * 70.25, 80.5};
    c.save();
    c.setAlpha(i == 2 ? 0.4 : 1.0);
    c.fill(Path::circle(k, 19.0),
           paint::RadialGradient{{k.x - 6.0, k.y - 8.0}, 26.0,
                                 {{0.0, Color{90, 80, 110, 255}}, {1.0, Color{30, 26, 38, 255}}}});
    c.restore();
  }
}

}  // namespace

TEST_CASE("paint: software gradient fills of round shapes match the backend and any split of themselves") {
  if (!backendOrSkip()) return;
  constexpr double scale = 2.0;
  constexpr std::uint32_t width = 512U, height = 240U;
  // A surface whose first row lies `top` logical points down the drawing.
  const auto draw = [&](PixelSurface& surface, double top) {
    auto c = paint::makeCanvas(surface, scale);
    CHECK(c != nullptr);
    if (c == nullptr) return;
    c->translate(0.0, -top);
    drawGradientShapes(*c);
    c->flush();
  };
  PixelSurface software{width, height};
  draw(software, 0.0);
  PixelSurface backend{width, height};
  {
    const paint::ScopedBackendGradients vector;
    draw(backend, 0.0);
  }
  int maximum = 0;
  std::size_t aboveTwo = 0U;
  double total = 0.0;
  for (std::size_t i = 0U; i < software.pixels().size(); ++i) {
    int pixelMax = 0;
    for (const unsigned shift : {0U, 8U, 16U, 24U}) {
      const auto d = std::abs(channel(software.pixels()[i], shift) - channel(backend.pixels()[i], shift));
      pixelMax = std::max(pixelMax, d);
      total += d;
    }
    maximum = std::max(maximum, pixelMax);
    if (pixelMax > 2) ++aboveTwo;
  }
  const auto pixels = static_cast<double>(software.pixels().size());
  std::cout << "software gradient fills against the backend: max=" << maximum
            << " mean=" << total / (4.0 * pixels)
            << " above2=" << 100.0 * static_cast<double>(aboveTwo) / pixels << "%\n";
  // A bound, not an identity: along a short steep ramp such as a note capsule's, the backend steps
  // through a coarse colour table without dithering and strays up to about three levels from the
  // exact gradient (the next test measures that), and arc coverage differs slightly as estimated.
  CHECK(maximum <= 6);
  CHECK(total / (4.0 * pixels) < 0.5);
  // Two clip rectangles, and surfaces moved down by whole device rows (one not a multiple of the
  // dither's eight), reassemble the whole drawing exactly.
  PixelSurface clipped{width, height};
  {
    auto c = paint::makeCanvas(clipped, scale);
    CHECK(c != nullptr);
    if (c != nullptr) {
      for (const auto& piece : {ui::Rect{0.0, 0.0, 256.0, 61.5}, ui::Rect{0.0, 61.5, 256.0, 58.5}}) {
        c->save();
        c->clipRect(piece);
        drawGradientShapes(*c);
        c->restore();
      }
      c->flush();
    }
  }
  CHECK(clipped.checksum() == software.checksum());
  for (const std::uint32_t split : {56U, 20U}) {
    PixelSurface upper{width, split}, lower{width, height - split};
    draw(upper, 0.0);
    draw(lower, static_cast<double>(split) / scale);
    CHECK(std::equal(upper.pixels().begin(), upper.pixels().end(), software.pixels().begin()));
    CHECK(std::equal(lower.pixels().begin(), lower.pixels().end(),
                     software.pixels().begin() + static_cast<std::ptrdiff_t>(split) * width));
  }
}

TEST_CASE("paint: a software capsule gradient is the exact ramp at each row's pixel centres") {
  if (!backendOrSkip()) return;
  // A note capsule over black at 2x: a 180-level ramp over 27.8 device rows. Averaging 50 columns
  // of a row cancels the ordered dither, leaving the row's colour.
  const ui::Rect r{4.0, 4.0, 56.0, 13.9};
  const auto draw = [&](PixelSurface& surface) {
    surface.clear(kBlack);
    auto c = paint::makeCanvas(surface, 2.0);
    CHECK(c != nullptr);
    if (c == nullptr) return;
    c->fill(Path::roundedRect(r, 6.95),
            paint::LinearGradient{{r.x, r.y}, {r.x, r.bottom()},
                                  {{0.0, Color{205, 60, 90, 255}}, {1.0, Color{25, 20, 30, 255}}}});
    c->flush();
  };
  PixelSurface software{128, 64}, backend{128, 64};
  draw(software);
  {
    const paint::ScopedBackendGradients vector;
    draw(backend);
  }
  double worstSoftware = 0.0, worstBackend = 0.0;
  for (std::uint32_t y = 8U; y < 36U; ++y) {
    double sumSoftware = 0.0, sumBackend = 0.0;
    for (std::uint32_t x = 40U; x < 90U; ++x) {
      sumSoftware += channel(software.pixels()[y * 128U + x], 16);
      sumBackend += channel(backend.pixels()[y * 128U + x], 16);
    }
    // Rows 8 to 34 are whole; the shape covers 0.8 of row 35.
    const auto coverage = std::min(1.0, 35.8 - static_cast<double>(y));
    const auto t = std::clamp((static_cast<double>(y) + 0.5 - 8.0) / 27.8, 0.0, 1.0);
    const auto exact = (205.0 - 180.0 * t) * coverage;
    worstSoftware = std::max(worstSoftware, std::abs(sumSoftware / 50.0 - exact));
    worstBackend = std::max(worstBackend, std::abs(sumBackend / 50.0 - exact));
  }
  std::cout << "capsule row-mean error against the exact ramp: software=" << worstSoftware
            << " backend=" << worstBackend << '\n';
  CHECK(worstSoftware <= 0.35);
}
