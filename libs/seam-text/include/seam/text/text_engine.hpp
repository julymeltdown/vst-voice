#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace seam::text {

enum class PreferredScript {
  Auto,
  Korean,
  Japanese,
  SimplifiedChinese,
  TraditionalChinese,
};

struct FontSearchOptions final {
  std::vector<std::filesystem::path> additionalCandidates;
  PreferredScript preferred{PreferredScript::Auto};
  std::uint64_t maximumFontBytes{64ULL * 1024ULL * 1024ULL};
  std::size_t maximumFaces{4U};
};

struct FontInfo final {
  std::filesystem::path path;
  std::string sha256;
  std::int32_t faceIndex{0};
  std::size_t probeCoverage{0U};

  friend bool operator==(const FontInfo&, const FontInfo&) = default;
};

struct TextStyle final {
  float pixelHeight{14.0F};
  float letterSpacing{0.0F};
  float lineSpacing{1.20F};
  std::uint32_t maximumWidth{0U};
  std::size_t maximumLines{1U};
  bool ellipsize{false};

  friend bool operator==(const TextStyle&, const TextStyle&) = default;
};

struct AlphaBitmap final {
  std::uint32_t width{0U};
  std::uint32_t height{0U};
  std::int32_t baseline{0};
  std::vector<std::uint8_t> alpha;
};

struct TextMetrics final {
  double width{0.0};
  double height{0.0};
  double baseline{0.0};
  std::size_t lines{0U};
  bool truncated{false};
};

struct RenderedText final {
  AlphaBitmap bitmap;
  TextMetrics metrics;
};

struct TextCacheStats final {
  std::uint64_t hits{0U};
  std::uint64_t misses{0U};
  std::uint64_t evictions{0U};
  std::size_t entries{0U};
  std::uint64_t bytes{0U};
};

class TextEngine final {
public:
  class Impl;
  ~TextEngine();
  TextEngine(TextEngine&&) noexcept;
  TextEngine& operator=(TextEngine&&) noexcept;
  TextEngine(const TextEngine&) = delete;
  TextEngine& operator=(const TextEngine&) = delete;

  [[nodiscard]] static core::Result<std::unique_ptr<TextEngine>> createSystem(
      const FontSearchOptions& options = {});
  [[nodiscard]] static core::Result<std::unique_ptr<TextEngine>>
  createFromTrustedFiles(const FontSearchOptions& options);

  [[nodiscard]] core::Result<RenderedText> render(
      std::string_view utf8, const TextStyle& style);
  [[nodiscard]] core::Result<TextMetrics> measure(
      std::string_view utf8, const TextStyle& style);

  [[nodiscard]] std::span<const FontInfo> fonts() const noexcept;
  [[nodiscard]] TextCacheStats cacheStats() const noexcept;
  void clearCache() noexcept;

private:
  explicit TextEngine(std::unique_ptr<Impl> implementation) noexcept;
  std::unique_ptr<Impl> implementation_;
};

}  // namespace seam::text
