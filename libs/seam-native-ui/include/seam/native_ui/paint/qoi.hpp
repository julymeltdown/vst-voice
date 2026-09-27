#pragma once

#include "seam/core/result.hpp"
#include "seam/native_ui/pixel_surface.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace seam::native_ui::paint {

// A bounded decoder for QOI ("Quite OK Image", qoiformat.org; the MIT reference algorithm by
// Dominic Szablewski, reimplemented), the character package's schema-4 image format (redesign plan
// section 3.5). It adds no codec dependency, which is why the package does not ship PNG: the raster
// front that draws package art must decode it on every platform the shell builds for.
//
// Everything is checked before a pixel is allocated: the magic, the channel count and colour space,
// the declared size against the limits, and the smallest stream that could encode that many pixels
// (so a 14-byte file cannot claim 4096 x 4096). Decoding accounts for every byte: a chunk that runs
// past the payload, a run past the last pixel, a missing end marker and trailing bytes are all
// refused rather than tolerated.
//
// The result is straight-alpha BGRA in a PixelSurface, the convention the raster front's blit and
// PixelSurface::loadPpm share; imageFromPixels premultiplies for the vector backend. A three-channel
// image decodes opaque. The colour-space byte is informational, as in the specification.
struct QoiLimits final {
  std::uint64_t maximumEncodedBytes{32ULL * 1024ULL * 1024ULL};
  std::uint32_t maximumDimension{4096U};
  std::uint64_t maximumPixels{4096ULL * 4096ULL};
};

struct QoiHeader final {
  std::uint32_t width{0U};
  std::uint32_t height{0U};
  std::uint8_t channels{4U};
  std::uint8_t colorspace{0U};
};

inline constexpr std::size_t kQoiHeaderBytes = 14U;
inline constexpr std::size_t kQoiEndMarkerBytes = 8U;

// The header alone, validated against the limits and against the encoded size it arrived in.
[[nodiscard]] core::Result<QoiHeader> readQoiHeader(std::span<const std::byte> bytes,
                                                    const QoiLimits& limits = {});
[[nodiscard]] core::Result<PixelSurface> decodeQoi(std::span<const std::byte> bytes,
                                                   const QoiLimits& limits = {});
[[nodiscard]] core::Result<PixelSurface> loadQoi(const std::filesystem::path& path,
                                                 const QoiLimits& limits = {});

}  // namespace seam::native_ui::paint
