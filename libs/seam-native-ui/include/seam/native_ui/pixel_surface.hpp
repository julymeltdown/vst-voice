#pragma once

#include "seam/core/result.hpp"
#include "seam/ui/geometry.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace seam::native_ui {

struct Color final {
  std::uint8_t red{0};
  std::uint8_t green{0};
  std::uint8_t blue{0};
  std::uint8_t alpha{255};

  [[nodiscard]] constexpr std::uint32_t bgra() const noexcept {
    return static_cast<std::uint32_t>(blue) |
           (static_cast<std::uint32_t>(green) << 8U) |
           (static_cast<std::uint32_t>(red) << 16U) |
           (static_cast<std::uint32_t>(alpha) << 24U);
  }

  friend constexpr bool operator==(const Color&, const Color&) = default;
};

class PixelSurface final {
public:
  PixelSurface() = default;
  PixelSurface(std::uint32_t width, std::uint32_t height);

  [[nodiscard]] core::Result<void> resize(std::uint32_t width,
                                           std::uint32_t height);
  void clear(Color color) noexcept;

  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
  [[nodiscard]] std::size_t strideBytes() const noexcept {
    return static_cast<std::size_t>(width_) * sizeof(std::uint32_t);
  }
  [[nodiscard]] std::span<std::uint32_t> pixels() noexcept { return pixels_; }
  [[nodiscard]] std::span<const std::uint32_t> pixels() const noexcept {
    return pixels_;
  }
  [[nodiscard]] std::uint64_t checksum() const noexcept;
  [[nodiscard]] core::Result<void> writePpm(
      const std::filesystem::path& path) const;
  [[nodiscard]] static core::Result<PixelSurface> loadPpm(
      const std::filesystem::path& path,
      std::uint64_t maximumBytes = 16ULL * 1024ULL * 1024ULL);

private:
  std::uint32_t width_{0};
  std::uint32_t height_{0};
  std::vector<std::uint32_t> pixels_;
};

class RasterCanvas final {
public:
  explicit RasterCanvas(PixelSurface& surface, double scale = 1.0) noexcept;

  [[nodiscard]] double scale() const noexcept { return scale_; }
  [[nodiscard]] double logicalWidth() const noexcept;
  [[nodiscard]] double logicalHeight() const noexcept;
  [[nodiscard]] PixelSurface& surface() noexcept { return surface_; }

  void clear(Color color) noexcept;
  void fillRect(ui::Rect rect, Color color) noexcept;
  void strokeRect(ui::Rect rect, Color color, double thickness = 1.0) noexcept;
  void line(ui::Point start, ui::Point end, Color color,
            double thickness = 1.0) noexcept;
  void drawText(ui::Point origin, std::string_view text, Color color,
                double size = 12.0) noexcept;
  void drawVerticalGradient(ui::Rect rect, Color top, Color bottom) noexcept;
  void drawImageNearest(ui::Rect destination, const PixelSurface& image,
                        double opacity = 1.0) noexcept;

private:
  void blendPixel(std::int32_t x, std::int32_t y, Color color) noexcept;
  void fillPhysicalRect(std::int32_t left, std::int32_t top,
                        std::int32_t right, std::int32_t bottom,
                        Color color) noexcept;
  void drawGlyph(std::int32_t x, std::int32_t y, char character,
                 std::int32_t pixelSize, Color color) noexcept;

  PixelSurface& surface_;
  double scale_{1.0};
};

}  // namespace seam::native_ui
