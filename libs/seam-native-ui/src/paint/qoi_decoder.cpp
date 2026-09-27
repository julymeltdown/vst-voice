#include "seam/native_ui/paint/qoi.hpp"

#include "seam/core/file_io.hpp"

#include <array>
#include <string>

namespace seam::native_ui::paint {
namespace {

constexpr std::uint8_t kOpRgb = 0xFEU;
constexpr std::uint8_t kOpRgba = 0xFFU;
constexpr std::uint8_t kOpIndex = 0x00U;
constexpr std::uint8_t kOpDiff = 0x40U;
constexpr std::uint8_t kOpLuma = 0x80U;
constexpr std::uint8_t kMask2 = 0xC0U;
// A run chunk covers at most 62 pixels, so no valid stream is shorter than this many chunk bytes.
constexpr std::uint64_t kLongestRun = 62U;

struct Rgba final {
  std::uint8_t r{0U};
  std::uint8_t g{0U};
  std::uint8_t b{0U};
  std::uint8_t a{255U};
};

std::size_t hashOf(const Rgba& p) noexcept {
  return (static_cast<std::size_t>(p.r) * 3U + static_cast<std::size_t>(p.g) * 5U +
          static_cast<std::size_t>(p.b) * 7U + static_cast<std::size_t>(p.a) * 11U) %
         64U;
}

std::uint8_t byteAt(std::span<const std::byte> bytes, std::size_t index) noexcept {
  return static_cast<std::uint8_t>(bytes[index]);
}

std::uint32_t bigEndian32(std::span<const std::byte> bytes, std::size_t at) noexcept {
  return (static_cast<std::uint32_t>(byteAt(bytes, at)) << 24U) |
         (static_cast<std::uint32_t>(byteAt(bytes, at + 1U)) << 16U) |
         (static_cast<std::uint32_t>(byteAt(bytes, at + 2U)) << 8U) |
         static_cast<std::uint32_t>(byteAt(bytes, at + 3U));
}

template <typename T>
core::Result<T> refuse(core::ErrorCode code, const char* message) {
  return core::failure<T>(code, message);
}

}  // namespace

core::Result<QoiHeader> readQoiHeader(std::span<const std::byte> bytes, const QoiLimits& limits) {
  if (bytes.size() > limits.maximumEncodedBytes)
    return refuse<QoiHeader>(core::ErrorCode::Unsupported, "QOI image exceeds the encoded size limit");
  if (bytes.size() < kQoiHeaderBytes + kQoiEndMarkerBytes)
    return refuse<QoiHeader>(core::ErrorCode::ParseError, "QOI image is shorter than its header");
  if (byteAt(bytes, 0U) != 'q' || byteAt(bytes, 1U) != 'o' || byteAt(bytes, 2U) != 'i' ||
      byteAt(bytes, 3U) != 'f')
    return refuse<QoiHeader>(core::ErrorCode::ParseError, "QOI magic is missing");
  QoiHeader header;
  header.width = bigEndian32(bytes, 4U);
  header.height = bigEndian32(bytes, 8U);
  header.channels = byteAt(bytes, 12U);
  header.colorspace = byteAt(bytes, 13U);
  if (header.channels != 3U && header.channels != 4U)
    return refuse<QoiHeader>(core::ErrorCode::ParseError, "QOI channel count must be 3 or 4");
  if (header.colorspace > 1U)
    return refuse<QoiHeader>(core::ErrorCode::ParseError, "QOI colour space must be 0 or 1");
  if (header.width == 0U || header.height == 0U || header.width > limits.maximumDimension ||
      header.height > limits.maximumDimension ||
      static_cast<std::uint64_t>(header.width) * header.height > limits.maximumPixels)
    return refuse<QoiHeader>(core::ErrorCode::Unsupported,
                             "QOI dimensions are empty or exceed the limit");
  // The smallest stream that could hold this many pixels is one run byte per 62 of them.
  const auto pixels = static_cast<std::uint64_t>(header.width) * header.height;
  const auto shortest = (pixels + kLongestRun - 1U) / kLongestRun;
  if (bytes.size() - kQoiHeaderBytes - kQoiEndMarkerBytes < shortest)
    return refuse<QoiHeader>(core::ErrorCode::ParseError,
                             "QOI stream is too short for its declared size");
  return header;
}

core::Result<PixelSurface> decodeQoi(std::span<const std::byte> bytes, const QoiLimits& limits) {
  auto parsed = readQoiHeader(bytes, limits);
  if (!parsed) return core::Result<PixelSurface>{parsed.error()};
  const auto header = parsed.value();
  const auto end = bytes.size() - kQoiEndMarkerBytes;
  for (std::size_t i = 0U; i < kQoiEndMarkerBytes; ++i)
    if (byteAt(bytes, end + i) != (i + 1U == kQoiEndMarkerBytes ? 1U : 0U))
      return refuse<PixelSurface>(core::ErrorCode::ParseError, "QOI end marker is missing");

  PixelSurface surface{header.width, header.height};
  auto out = surface.pixels();
  std::array<Rgba, 64> index{};
  for (auto& entry : index) entry = Rgba{0U, 0U, 0U, 0U};
  Rgba px{};
  std::size_t cursor = kQoiHeaderBytes;
  std::size_t pixel = 0U;
  const auto need = [&](std::size_t count) { return cursor + count <= end; };
  while (pixel < out.size()) {
    if (!need(1U))
      return refuse<PixelSurface>(core::ErrorCode::ParseError, "QOI stream ends before its pixels");
    const auto op = byteAt(bytes, cursor++);
    std::size_t repeat = 1U;
    if (op == kOpRgb) {
      if (!need(3U))
        return refuse<PixelSurface>(core::ErrorCode::ParseError, "QOI RGB chunk is truncated");
      px.r = byteAt(bytes, cursor);
      px.g = byteAt(bytes, cursor + 1U);
      px.b = byteAt(bytes, cursor + 2U);
      cursor += 3U;
    } else if (op == kOpRgba) {
      if (!need(4U))
        return refuse<PixelSurface>(core::ErrorCode::ParseError, "QOI RGBA chunk is truncated");
      px.r = byteAt(bytes, cursor);
      px.g = byteAt(bytes, cursor + 1U);
      px.b = byteAt(bytes, cursor + 2U);
      px.a = byteAt(bytes, cursor + 3U);
      cursor += 4U;
    } else if ((op & kMask2) == kOpIndex) {
      px = index[op & 0x3FU];
    } else if ((op & kMask2) == kOpDiff) {
      px.r = static_cast<std::uint8_t>(px.r + ((op >> 4U) & 0x03U) - 2);
      px.g = static_cast<std::uint8_t>(px.g + ((op >> 2U) & 0x03U) - 2);
      px.b = static_cast<std::uint8_t>(px.b + (op & 0x03U) - 2);
    } else if ((op & kMask2) == kOpLuma) {
      if (!need(1U))
        return refuse<PixelSurface>(core::ErrorCode::ParseError, "QOI luma chunk is truncated");
      const auto second = byteAt(bytes, cursor++);
      const auto dg = static_cast<int>(op & 0x3FU) - 32;
      px.r = static_cast<std::uint8_t>(px.r + dg - 8 + ((second >> 4U) & 0x0F));
      px.g = static_cast<std::uint8_t>(px.g + dg);
      px.b = static_cast<std::uint8_t>(px.b + dg - 8 + (second & 0x0F));
    } else {
      // kOpRun: 1 to 62 copies of the previous pixel (63 and 64 are the RGB and RGBA tags).
      repeat = static_cast<std::size_t>(op & 0x3FU) + 1U;
      if (repeat > out.size() - pixel)
        return refuse<PixelSurface>(core::ErrorCode::ParseError, "QOI run passes the last pixel");
    }
    // As in the reference decoder, every chunk (a run included) records the current pixel.
    index[hashOf(px)] = px;
    const auto a = header.channels == 3U ? std::uint8_t{255U} : px.a;
    const auto value = Color{px.r, px.g, px.b, a}.bgra();
    for (std::size_t i = 0U; i < repeat; ++i) out[pixel++] = value;
  }
  if (cursor != end)
    return refuse<PixelSurface>(core::ErrorCode::ParseError,
                                "QOI stream has bytes after its last pixel");
  return surface;
}

core::Result<PixelSurface> loadQoi(const std::filesystem::path& path, const QoiLimits& limits) {
  auto bytes = core::readFileBytesLimited(path, limits.maximumEncodedBytes);
  if (!bytes) return core::Result<PixelSurface>{bytes.error()};
  auto decoded = decodeQoi(bytes.value(), limits);
  if (!decoded)
    return core::failure<PixelSurface>(decoded.error().code, decoded.error().message,
                                       path.string());
  return decoded;
}

}  // namespace seam::native_ui::paint
