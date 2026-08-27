#include "seam/native_ui/pixel_surface.hpp"

#include "seam/core/file_io.hpp"
#include "seam/text/text_engine.hpp"
#include "seam/text/unicode.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <string>

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


core::Result<PixelSurface> PixelSurface::loadPpm(
    const std::filesystem::path& path, std::uint64_t maximumBytes) {
  auto bytes = core::readFileBytesLimited(path, maximumBytes);
  if (!bytes) return core::Result<PixelSurface>{bytes.error()};
  const auto& data = bytes.value();
  std::size_t cursor = 0U;
  const auto skip = [&] {
    for (;;) {
      while (cursor < data.size()) {
        const auto ch = static_cast<char>(data[cursor]);
        if (ch == '#') {
          while (cursor < data.size() && static_cast<char>(data[cursor]) != '\n') ++cursor;
          continue;
        }
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
          ++cursor;
          continue;
        }
        break;
      }
      break;
    }
  };
  const auto token = [&]() -> std::string {
    skip();
    const auto start = cursor;
    while (cursor < data.size()) {
      const auto ch = static_cast<char>(data[cursor]);
      if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '#') break;
      ++cursor;
    }
    return std::string{reinterpret_cast<const char*>(data.data() + start), cursor - start};
  };
  if (token() != "P6") {
    return core::failure<PixelSurface>(core::ErrorCode::ParseError,
                                       "Only binary P6 PPM images are supported",
                                       path.string());
  }
  std::uint64_t width = 0U;
  std::uint64_t height = 0U;
  std::uint64_t maximum = 0U;
  try {
    width = std::stoull(token());
    height = std::stoull(token());
    maximum = std::stoull(token());
  } catch (...) {
    return core::failure<PixelSurface>(core::ErrorCode::ParseError,
                                       "PPM header dimensions are invalid",
                                       path.string());
  }
  if (width == 0U || height == 0U || width > kMaximumDimension ||
      height > kMaximumDimension || width * height > kMaximumPixels || maximum != 255U) {
    return core::failure<PixelSurface>(core::ErrorCode::Unsupported,
                                       "PPM dimensions or color range are unsupported",
                                       path.string());
  }
  skip();
  if (width * height > std::numeric_limits<std::size_t>::max() / 3U) {
    return core::failure<PixelSurface>(core::ErrorCode::Unsupported,
                                       "PPM payload is too large", path.string());
  }
  const auto payload = static_cast<std::size_t>(width * height * 3U);
  if (cursor > data.size() || data.size() - cursor != payload) {
    return core::failure<PixelSurface>(core::ErrorCode::ParseError,
                                       "PPM payload size does not match the header",
                                       path.string());
  }
  PixelSurface surface{static_cast<std::uint32_t>(width),
                       static_cast<std::uint32_t>(height)};
  auto pixels = surface.pixels();
  for (std::size_t index = 0U; index < pixels.size(); ++index) {
    const auto base = cursor + index * 3U;
    const auto red = static_cast<std::uint8_t>(data[base]);
    const auto green = static_cast<std::uint8_t>(data[base + 1U]);
    const auto blue = static_cast<std::uint8_t>(data[base + 2U]);
    pixels[index] = Color{red, green, blue, 255U}.bgra();
  }
  return surface;
}

RasterCanvas::RasterCanvas(PixelSurface& surface, double scale,
                           text::TextEngine* textEngine) noexcept
    : surface_(surface),
      scale_(std::isfinite(scale) ? std::clamp(scale, 0.5, 4.0) : 1.0),
      textEngine_(textEngine) {}

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
  drawText(ui::Rect{origin.x, origin.y,
                    std::max(0.0, logicalWidth() - origin.x),
                    std::max(0.0, logicalHeight() - origin.y)},
           text, color, size);
}

void RasterCanvas::drawText(ui::Rect bounds, std::string_view text,
                            Color color, double size) noexcept {
  if (bounds.width <= 0.0 || bounds.height <= 0.0 || text.empty() ||
      !std::isfinite(size) || size <= 0.0) {
    return;
  }
  const auto left = static_cast<std::int32_t>(std::floor(bounds.x * scale_));
  const auto top = static_cast<std::int32_t>(std::floor(bounds.y * scale_));
  const auto right = static_cast<std::int32_t>(std::ceil(bounds.right() * scale_));
  const auto bottom = static_cast<std::int32_t>(std::ceil(bounds.bottom() * scale_));
  const auto maximumWidth = static_cast<std::uint32_t>(std::max(
      1, right - left));
  if (textEngine_ != nullptr && !text.empty() && std::isfinite(size) &&
      size > 0.0) {
    try {
      const auto rendered = textEngine_->render(
          text, text::TextStyle{
                    .pixelHeight = static_cast<float>(size * scale_),
                    .letterSpacing = 0.0F,
                    .lineSpacing = 1.20F,
                    .maximumWidth = maximumWidth,
                    .maximumLines = 1U,
                    .ellipsize = true,
                });
      if (rendered) {
        const auto& bitmap = rendered.value().bitmap;
        for (std::uint32_t row = 0U; row < bitmap.height; ++row) {
          for (std::uint32_t column = 0U; column < bitmap.width; ++column) {
            const auto coverage = bitmap.alpha[
                static_cast<std::size_t>(row) * bitmap.width + column];
            if (coverage == 0U) continue;
            const auto x = left + static_cast<std::int32_t>(column);
            const auto y = top + static_cast<std::int32_t>(row);
            if (x < left || x >= right || y < top || y >= bottom) continue;
            const auto combinedAlpha = static_cast<std::uint8_t>(
                (static_cast<std::uint32_t>(coverage) * color.alpha + 127U) /
                255U);
            blendPixel(x, y,
                       Color{color.red, color.green, color.blue,
                             combinedAlpha});
          }
        }
        return;
      }
    } catch (...) {
    }
  }

  const auto glyphPixel = std::max(1, static_cast<std::int32_t>(
      std::lround(size * scale_ / 7.0)));
  if (top + glyphPixel * 7 > bottom) return;
  const auto advance = glyphPixel * 6;
  const auto safe = text::truncateUtf8ToDisplayWidth(
      text, static_cast<std::size_t>(std::max(0, right - left) / advance));
  auto x = left;
  for (const auto character : safe) {
    if (x + advance > right) break;
    drawGlyph(x, top, character, glyphPixel, color);
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


void RasterCanvas::drawImageNearest(ui::Rect destination,
                                    const PixelSurface& image,
                                    double opacity) noexcept {
  if (destination.width <= 0.0 || destination.height <= 0.0 ||
      image.width() == 0U || image.height() == 0U || !std::isfinite(opacity)) {
    return;
  }
  const auto alpha = std::clamp(opacity, 0.0, 1.0);
  if (alpha <= 0.0) return;
  const auto left = static_cast<std::int32_t>(std::floor(destination.x * scale_));
  const auto top = static_cast<std::int32_t>(std::floor(destination.y * scale_));
  const auto right = static_cast<std::int32_t>(std::ceil(destination.right() * scale_));
  const auto bottom = static_cast<std::int32_t>(std::ceil(destination.bottom() * scale_));
  const auto physicalWidth = std::max(1, right - left);
  const auto physicalHeight = std::max(1, bottom - top);
  const auto source = image.pixels();
  for (auto y = std::max(0, top); y < std::min(bottom, static_cast<std::int32_t>(surface_.height())); ++y) {
    const auto localY = y - top;
    const auto sourceY = std::min<std::uint32_t>(
        image.height() - 1U,
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(localY) * image.height()) /
                                   static_cast<std::uint64_t>(physicalHeight)));
    for (auto x = std::max(0, left); x < std::min(right, static_cast<std::int32_t>(surface_.width())); ++x) {
      const auto localX = x - left;
      const auto sourceX = std::min<std::uint32_t>(
          image.width() - 1U,
          static_cast<std::uint32_t>((static_cast<std::uint64_t>(localX) * image.width()) /
                                     static_cast<std::uint64_t>(physicalWidth)));
      const auto pixel = source[static_cast<std::size_t>(sourceY) * image.width() + sourceX];
      const auto blue = static_cast<std::uint8_t>(pixel & 0xFFU);
      const auto green = static_cast<std::uint8_t>((pixel >> 8U) & 0xFFU);
      const auto red = static_cast<std::uint8_t>((pixel >> 16U) & 0xFFU);
      blendPixel(x, y, Color{red, green, blue,
                             static_cast<std::uint8_t>(std::lround(alpha * 255.0))});
    }
  }
}

}  // namespace seam::native_ui
