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
