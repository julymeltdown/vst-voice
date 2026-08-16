#include "seam/native_ui/pixel_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>

namespace seam::native_ui {
namespace {

constexpr std::uint32_t kMaximumDimension = 16384U;
constexpr std::uint64_t kMaximumPixels = 128ULL * 1024ULL * 1024ULL;

using Glyph = std::array<std::uint8_t, 7>;

Glyph glyphFor(char value) noexcept {
  const auto character = (value >= 'a' && value <= 'z')
                             ? static_cast<char>(value - 'a' + 'A')
                             : value;
  switch (character) {
    case 'A': return {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11};
    case 'B': return {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E};
    case 'C': return {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E};
    case 'D': return {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E};
    case 'E': return {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F};
    case 'F': return {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};
    case 'G': return {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F};
    case 'H': return {0x11,0x11,0x11,0x1F,0x11,0x11,0x11};
    case 'I': return {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E};
    case 'J': return {0x07,0x02,0x02,0x02,0x12,0x12,0x0C};
    case 'K': return {0x11,0x12,0x14,0x18,0x14,0x12,0x11};
    case 'L': return {0x10,0x10,0x10,0x10,0x10,0x10,0x1F};
    case 'M': return {0x11,0x1B,0x15,0x15,0x11,0x11,0x11};
    case 'N': return {0x11,0x19,0x15,0x13,0x11,0x11,0x11};
    case 'O': return {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E};
    case 'P': return {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};
    case 'Q': return {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D};
    case 'R': return {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11};
    case 'S': return {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E};
    case 'T': return {0x1F,0x04,0x04,0x04,0x04,0x04,0x04};
    case 'U': return {0x11,0x11,0x11,0x11,0x11,0x11,0x0E};
    case 'V': return {0x11,0x11,0x11,0x11,0x11,0x0A,0x04};
    case 'W': return {0x11,0x11,0x11,0x15,0x15,0x15,0x0A};
    case 'X': return {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11};
    case 'Y': return {0x11,0x11,0x0A,0x04,0x04,0x04,0x04};
    case 'Z': return {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F};
    case '0': return {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E};
    case '1': return {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E};
    case '2': return {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F};
    case '3': return {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E};
    case '4': return {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02};
    case '5': return {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E};
    case '6': return {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E};
    case '7': return {0x1F,0x01,0x02,0x04,0x08,0x08,0x08};
    case '8': return {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E};
    case '9': return {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E};
    case '-': return {0x00,0x00,0x00,0x1F,0x00,0x00,0x00};
    case '_': return {0x00,0x00,0x00,0x00,0x00,0x00,0x1F};
    case '.': return {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C};
    case ':': return {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00};
    case '/': return {0x01,0x02,0x02,0x04,0x08,0x08,0x10};
    case '+': return {0x00,0x04,0x04,0x1F,0x04,0x04,0x00};
    case '>': return {0x10,0x08,0x04,0x02,0x04,0x08,0x10};
    case '<': return {0x01,0x02,0x04,0x08,0x04,0x02,0x01};
    case '[': return {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E};
    case ']': return {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E};
    case '(': return {0x02,0x04,0x08,0x08,0x08,0x04,0x02};
    case ')': return {0x08,0x04,0x02,0x02,0x02,0x04,0x08};
    case '!': return {0x04,0x04,0x04,0x04,0x04,0x00,0x04};
    case '?': return {0x0E,0x11,0x01,0x02,0x04,0x00,0x04};
    case '=': return {0x00,0x1F,0x00,0x1F,0x00,0x00,0x00};
    case '|': return {0x04,0x04,0x04,0x04,0x04,0x04,0x04};
    case ' ': return {0,0,0,0,0,0,0};
    default:  return {0x1F,0x11,0x15,0x11,0x15,0x11,0x1F};
  }
}

Color interpolate(Color lhs, Color rhs, double t) noexcept {
  const auto blend = [t](std::uint8_t a, std::uint8_t b) {
    return static_cast<std::uint8_t>(std::clamp(
        std::lround(static_cast<double>(a) +
                    (static_cast<double>(b) - static_cast<double>(a)) * t),
        0L, 255L));
  };
  return Color{blend(lhs.red, rhs.red), blend(lhs.green, rhs.green),
               blend(lhs.blue, rhs.blue), blend(lhs.alpha, rhs.alpha)};
}

}  // namespace

PixelSurface::PixelSurface(std::uint32_t width, std::uint32_t height) {
  const auto resized = resize(width, height);
  if (!resized) {
    width_ = 0;
    height_ = 0;
    pixels_.clear();
  }
}

core::Result<void> PixelSurface::resize(std::uint32_t width,
                                         std::uint32_t height) {
  if (width == 0U || height == 0U || width > kMaximumDimension ||
      height > kMaximumDimension ||
      static_cast<std::uint64_t>(width) * height > kMaximumPixels) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Pixel surface dimensions are outside the supported range");
  }
  width_ = width;
  height_ = height;
  pixels_.assign(static_cast<std::size_t>(width) * height, 0U);
  return core::success();
}

void PixelSurface::clear(Color color) noexcept {
  std::fill(pixels_.begin(), pixels_.end(), color.bgra());
}

std::uint64_t PixelSurface::checksum() const noexcept {
  std::uint64_t value = 1469598103934665603ULL;
  for (const auto pixel : pixels_) {
    value ^= pixel;
    value *= 1099511628211ULL;
  }
  value ^= width_;
  value *= 1099511628211ULL;
  value ^= height_;
  return value;
}

core::Result<void> PixelSurface::writePpm(
    const std::filesystem::path& path) const {
  if (pixels_.empty()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Cannot export an empty pixel surface");
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to create PPM output", path.string());
  }
  stream << "P6\n" << width_ << ' ' << height_ << "\n255\n";
  for (const auto pixel : pixels_) {
    const std::array<char, 3> rgb{
        static_cast<char>((pixel >> 16U) & 0xFFU),
        static_cast<char>((pixel >> 8U) & 0xFFU),
        static_cast<char>(pixel & 0xFFU),
    };
    stream.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
  }
  stream.flush();
  if (!stream) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to write PPM output", path.string());
  }
  return core::success();
}

RasterCanvas::RasterCanvas(PixelSurface& surface, double scale) noexcept
    : surface_(surface),
      scale_(std::isfinite(scale) ? std::clamp(scale, 0.5, 4.0) : 1.0) {}

double RasterCanvas::logicalWidth() const noexcept {
  return static_cast<double>(surface_.width()) / scale_;
}

double RasterCanvas::logicalHeight() const noexcept {
  return static_cast<double>(surface_.height()) / scale_;
}

void RasterCanvas::clear(Color color) noexcept { surface_.clear(color); }

void RasterCanvas::blendPixel(std::int32_t x, std::int32_t y,
                              Color color) noexcept {
  if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(surface_.width()) ||
      y >= static_cast<std::int32_t>(surface_.height())) {
    return;
  }
  auto& destination = surface_.pixels()[
      static_cast<std::size_t>(y) * surface_.width() +
      static_cast<std::size_t>(x)];
  if (color.alpha == 255U) {
    destination = color.bgra();
    return;
  }
  const auto alpha = static_cast<std::uint32_t>(color.alpha);
  const auto inverse = 255U - alpha;
  const auto destinationBlue = destination & 0xFFU;
  const auto destinationGreen = (destination >> 8U) & 0xFFU;
  const auto destinationRed = (destination >> 16U) & 0xFFU;
  const auto blue = (static_cast<std::uint32_t>(color.blue) * alpha +
                     destinationBlue * inverse + 127U) / 255U;
  const auto green = (static_cast<std::uint32_t>(color.green) * alpha +
                      destinationGreen * inverse + 127U) / 255U;
  const auto red = (static_cast<std::uint32_t>(color.red) * alpha +
                    destinationRed * inverse + 127U) / 255U;
  destination = blue | (green << 8U) | (red << 16U) | 0xFF000000U;
}

void RasterCanvas::fillPhysicalRect(std::int32_t left, std::int32_t top,
                                    std::int32_t right, std::int32_t bottom,
                                    Color color) noexcept {
  left = std::clamp(left, 0, static_cast<std::int32_t>(surface_.width()));
  right = std::clamp(right, 0, static_cast<std::int32_t>(surface_.width()));
  top = std::clamp(top, 0, static_cast<std::int32_t>(surface_.height()));
  bottom = std::clamp(bottom, 0, static_cast<std::int32_t>(surface_.height()));
  if (left >= right || top >= bottom) return;
  if (color.alpha == 255U) {
    for (auto y = top; y < bottom; ++y) {
      auto row = surface_.pixels().subspan(
          static_cast<std::size_t>(y) * surface_.width() +
              static_cast<std::size_t>(left),
          static_cast<std::size_t>(right - left));
      std::fill(row.begin(), row.end(), color.bgra());
    }
    return;
  }
  for (auto y = top; y < bottom; ++y) {
    for (auto x = left; x < right; ++x) blendPixel(x, y, color);
  }
}

void RasterCanvas::fillRect(ui::Rect rect, Color color) noexcept {
  const auto left = static_cast<std::int32_t>(std::floor(rect.x * scale_));
  const auto top = static_cast<std::int32_t>(std::floor(rect.y * scale_));
  const auto right = static_cast<std::int32_t>(std::ceil(rect.right() * scale_));
  const auto bottom = static_cast<std::int32_t>(std::ceil(rect.bottom() * scale_));
  fillPhysicalRect(left, top, right, bottom, color);
}

void RasterCanvas::strokeRect(ui::Rect rect, Color color,
                              double thickness) noexcept {
  const auto value = std::max(1.0 / scale_, thickness);
  fillRect(ui::Rect{rect.x, rect.y, rect.width, value}, color);
  fillRect(ui::Rect{rect.x, rect.bottom() - value, rect.width, value}, color);
  fillRect(ui::Rect{rect.x, rect.y, value, rect.height}, color);
  fillRect(ui::Rect{rect.right() - value, rect.y, value, rect.height}, color);
}

void RasterCanvas::line(ui::Point start, ui::Point end, Color color,
                        double thickness) noexcept {
  auto x0 = static_cast<std::int32_t>(std::lround(start.x * scale_));
  auto y0 = static_cast<std::int32_t>(std::lround(start.y * scale_));
  const auto x1 = static_cast<std::int32_t>(std::lround(end.x * scale_));
  const auto y1 = static_cast<std::int32_t>(std::lround(end.y * scale_));
  const auto radius = std::max(0, static_cast<std::int32_t>(
      std::lround(thickness * scale_ * 0.5)));
  const auto deltaX = std::abs(x1 - x0);
  const auto stepX = x0 < x1 ? 1 : -1;
  const auto deltaY = -std::abs(y1 - y0);
  const auto stepY = y0 < y1 ? 1 : -1;
  auto error = deltaX + deltaY;
  for (;;) {
    fillPhysicalRect(x0 - radius, y0 - radius, x0 + radius + 1,
                     y0 + radius + 1, color);
    if (x0 == x1 && y0 == y1) break;
    const auto twice = error * 2;
    if (twice >= deltaY) {
      error += deltaY;
      x0 += stepX;
    }
    if (twice <= deltaX) {
      error += deltaX;
      y0 += stepY;
    }
  }
}

void RasterCanvas::drawGlyph(std::int32_t x, std::int32_t y, char character,
                             std::int32_t pixelSize, Color color) noexcept {
  const auto glyph = glyphFor(character);
  for (std::size_t row = 0U; row < glyph.size(); ++row) {
    for (std::size_t column = 0U; column < 5U; ++column) {
      if ((glyph[row] & (1U << (4U - column))) == 0U) continue;
      fillPhysicalRect(x + static_cast<std::int32_t>(column) * pixelSize,
                       y + static_cast<std::int32_t>(row) * pixelSize,
                       x + static_cast<std::int32_t>(column + 1U) * pixelSize,
                       y + static_cast<std::int32_t>(row + 1U) * pixelSize,
                       color);
    }
  }
}

void RasterCanvas::drawText(ui::Point origin, std::string_view text,
                            Color color, double size) noexcept {
  const auto glyphPixel = std::max(1, static_cast<std::int32_t>(
      std::lround(size * scale_ / 7.0)));
  auto x = static_cast<std::int32_t>(std::lround(origin.x * scale_));
  auto y = static_cast<std::int32_t>(std::lround(origin.y * scale_));
  const auto advance = glyphPixel * 6;
  for (const auto character : text) {
    if (character == '\n') {
      x = static_cast<std::int32_t>(std::lround(origin.x * scale_));
      y += glyphPixel * 9;
      continue;
    }
    drawGlyph(x, y, character, glyphPixel, color);
    x += advance;
  }
}

void RasterCanvas::drawVerticalGradient(ui::Rect rect, Color top,
                                        Color bottom) noexcept {
  const auto physicalTop = static_cast<std::int32_t>(std::floor(rect.y * scale_));
  const auto physicalBottom = static_cast<std::int32_t>(std::ceil(rect.bottom() * scale_));
  const auto physicalLeft = static_cast<std::int32_t>(std::floor(rect.x * scale_));
  const auto physicalRight = static_cast<std::int32_t>(std::ceil(rect.right() * scale_));
  const auto height = std::max(1, physicalBottom - physicalTop);
  for (auto y = physicalTop; y < physicalBottom; ++y) {
    const auto t = static_cast<double>(y - physicalTop) /
                   static_cast<double>(height - 1 > 0 ? height - 1 : 1);
    fillPhysicalRect(physicalLeft, y, physicalRight, y + 1,
                     interpolate(top, bottom, t));
  }
}

}  // namespace seam::native_ui
