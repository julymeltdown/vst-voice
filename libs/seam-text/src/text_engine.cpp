#include "seam/text/text_engine.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/text/unicode.hpp"

#include "stb_truetype.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <limits>
#include <locale>
#include <memory>
#include <new>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace seam::text {
namespace {

constexpr std::uint64_t kMaximumTextBytes = 1024ULL * 1024ULL;
constexpr std::size_t kMaximumCodePoints = 262144U;
constexpr std::size_t kMaximumCacheEntries = 512U;
constexpr std::uint64_t kMaximumCacheBytes = 32ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kMaximumBitmapDimension = 16384U;
constexpr std::uint64_t kMaximumBitmapPixels = 64ULL * 1024ULL * 1024ULL;
constexpr std::array<char32_t, 6U> kCoverageProbes{
    U'A', U'가', U'あ', U'中', U'…', U'ß'};

PreferredScript inferredScript() {
  const char* locale = std::getenv("LC_ALL");
  if (locale == nullptr || *locale == '\0') locale = std::getenv("LC_CTYPE");
  if (locale == nullptr || *locale == '\0') locale = std::getenv("LANG");
  const std::string value = locale == nullptr ? std::string{} : std::string{locale};
  if (value.starts_with("ko")) return PreferredScript::Korean;
  if (value.starts_with("ja")) return PreferredScript::Japanese;
  if (value.starts_with("zh_TW") || value.starts_with("zh_HK") ||
      value.find("Hant") != std::string::npos) {
    return PreferredScript::TraditionalChinese;
  }
  if (value.starts_with("zh")) return PreferredScript::SimplifiedChinese;
  return PreferredScript::Auto;
}

char32_t preferredProbe(PreferredScript script) noexcept {
  switch (script) {
    case PreferredScript::Korean: return U'가';
    case PreferredScript::Japanese: return U'あ';
    case PreferredScript::SimplifiedChinese:
    case PreferredScript::TraditionalChinese: return U'中';
    case PreferredScript::Auto: return U'A';
  }
  return U'A';
}

std::vector<std::filesystem::path> systemFontCandidates(
    PreferredScript preferred) {
  std::vector<std::filesystem::path> paths;
#if !defined(_WIN32) && !defined(__APPLE__)
  static_cast<void>(preferred);
#endif
#if defined(_WIN32)
  if (preferred == PreferredScript::Korean) {
    paths.emplace_back("C:/Windows/Fonts/malgun.ttf");
  } else if (preferred == PreferredScript::Japanese) {
    paths.emplace_back("C:/Windows/Fonts/YuGothM.ttc");
    paths.emplace_back("C:/Windows/Fonts/meiryo.ttc");
  }
  paths.emplace_back("C:/Windows/Fonts/malgun.ttf");
  paths.emplace_back("C:/Windows/Fonts/YuGothM.ttc");
  paths.emplace_back("C:/Windows/Fonts/meiryo.ttc");
  paths.emplace_back("C:/Windows/Fonts/msyh.ttc");
  paths.emplace_back("C:/Windows/Fonts/msjh.ttc");
  paths.emplace_back("C:/Windows/Fonts/segoeui.ttf");
#elif defined(__APPLE__)
  if (preferred == PreferredScript::Korean) {
    paths.emplace_back("/System/Library/Fonts/AppleSDGothicNeo.ttc");
  } else if (preferred == PreferredScript::Japanese) {
    paths.emplace_back("/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc");
    paths.emplace_back("/System/Library/Fonts/ヒラギノ角ゴシック W4.ttc");
  }
  paths.emplace_back("/System/Library/Fonts/AppleSDGothicNeo.ttc");
  paths.emplace_back("/System/Library/Fonts/PingFang.ttc");
  paths.emplace_back("/System/Library/Fonts/Apple Symbols.ttf");
  paths.emplace_back("/System/Library/Fonts/Helvetica.ttc");
  paths.emplace_back("/Library/Fonts/Arial Unicode.ttf");
#else
  paths.emplace_back("/usr/share/opentype/noto/NotoSansCJK-Regular.ttc");
  paths.emplace_back("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
  paths.emplace_back("/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc");
  paths.emplace_back("/usr/share/fonts/truetype/nanum/NanumGothic.ttf");
  paths.emplace_back("/usr/share/fonts/truetype/nanum/NanumGothicCoding.ttf");
  paths.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
  paths.emplace_back("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf");
#endif
  return paths;
}

core::Result<std::filesystem::path> trustedRegularPath(
    const std::filesystem::path& path) {
  if (path.empty() || !path.is_absolute()) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::InvalidArgument,
        "Trusted font path must be absolute", path.string());
  }
  std::error_code error;
  const auto canonical = std::filesystem::canonical(path, error);
  if (error) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::NotFound, "Unable to resolve trusted font path",
        path.string());
  }
  const auto status = std::filesystem::status(canonical, error);
  if (error || !std::filesystem::is_regular_file(status)) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::InvalidArgument,
        "Trusted font path is not a regular file", canonical.string());
  }
  return canonical;
}

std::vector<std::uint8_t> byteVector(std::vector<std::byte> bytes) {
  std::vector<std::uint8_t> output(bytes.size());
  std::transform(bytes.begin(), bytes.end(), output.begin(),
                 [](std::byte value) { return std::to_integer<std::uint8_t>(value); });
  return output;
}

core::Result<void> validateStyle(const TextStyle& style) {
  if (!std::isfinite(style.pixelHeight) || style.pixelHeight < 4.0F ||
      style.pixelHeight > 256.0F || !std::isfinite(style.letterSpacing) ||
      style.letterSpacing < -32.0F || style.letterSpacing > 128.0F ||
      !std::isfinite(style.lineSpacing) || style.lineSpacing < 0.5F ||
      style.lineSpacing > 4.0F || style.maximumWidth > kMaximumBitmapDimension ||
      style.maximumLines == 0U || style.maximumLines > 128U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Text style is outside the supported limits");
  }
  return core::success();
}

std::string cacheKey(std::string_view text, const TextStyle& style) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream.precision(std::numeric_limits<float>::max_digits10);
  stream << style.pixelHeight << '|' << style.letterSpacing << '|'
         << style.lineSpacing << '|' << style.maximumWidth << '|'
         << style.maximumLines << '|' << (style.ellipsize ? 1 : 0) << '|'
         << text.size() << ':';
  auto key = stream.str();
  key.append(text.data(), text.size());
  return key;
}

}  // namespace

class TextEngine::Impl final {
public:
  struct Face final {
    std::vector<std::uint8_t> bytes;
    stbtt_fontinfo font{};
    FontInfo info;
    std::size_t preferenceScore{0U};
  };

  struct GlyphChoice final {
    std::size_t face{0U};
    int glyph{0};
  };

  struct PlacedGlyph final {
    std::size_t face{0U};
    int glyph{0};
    std::size_t line{0U};
    float penX{0.0F};
    float scale{1.0F};
    int boxX0{0};
    int boxY0{0};
    int boxX1{0};
    int boxY1{0};
    float advance{0.0F};
    bool combining{false};
  };

  struct CacheEntry final {
    std::shared_ptr<const RenderedText> value;
    std::uint64_t lastAccess{0U};
    std::uint64_t bytes{0U};
  };

  std::vector<std::unique_ptr<Face>> faces;
  std::vector<FontInfo> fontInfos;

  mutable std::mutex cacheMutex;
  std::unordered_map<std::string, CacheEntry> cache;
  std::uint64_t accessCounter{0U};
  std::uint64_t cacheBytes{0U};
  std::uint64_t cacheHits{0U};
  std::uint64_t cacheMisses{0U};
  std::uint64_t cacheEvictions{0U};

  [[nodiscard]] std::optional<GlyphChoice> chooseGlyph(
      char32_t codePoint) const noexcept {
    const auto value = static_cast<int>(codePoint);
    for (std::size_t index = 0U; index < faces.size(); ++index) {
      const auto glyph = stbtt_FindGlyphIndex(&faces[index]->font, value);
      if (glyph != 0) return GlyphChoice{index, glyph};
    }
    for (const auto fallback : {U'\uFFFD', U'?'}) {
      for (std::size_t index = 0U; index < faces.size(); ++index) {
        const auto glyph = stbtt_FindGlyphIndex(
            &faces[index]->font, static_cast<int>(fallback));
        if (glyph != 0) return GlyphChoice{index, glyph};
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] core::Result<RenderedText> renderUncached(
      std::string_view utf8, const TextStyle& style) const {
    const auto validStyle = validateStyle(style);
    if (!validStyle) return core::Result<RenderedText>{validStyle.error()};
    if (utf8.size() > kMaximumTextBytes) {
      return core::failure<RenderedText>(core::ErrorCode::Unsupported,
                                         "Text exceeds the rendering byte limit");
    }
    const auto decoded = decodeUtf8Strict(utf8);
    if (!decoded) return core::Result<RenderedText>{decoded.error()};
    if (decoded.value().size() > kMaximumCodePoints) {
      return core::failure<RenderedText>(
          core::ErrorCode::Unsupported,
          "Text exceeds the rendering code-point limit");
    }
    if (faces.empty()) {
      return core::failure<RenderedText>(core::ErrorCode::NotFound,
                                         "No usable font face is loaded");
    }

    const auto primaryScale =
        stbtt_ScaleForPixelHeight(&faces.front()->font, style.pixelHeight);
    int primaryAscent = 0;
    int primaryDescent = 0;
    int primaryLineGap = 0;
    stbtt_GetFontVMetrics(&faces.front()->font, &primaryAscent,
                          &primaryDescent, &primaryLineGap);
    const auto baseline = std::max(
        1, static_cast<int>(std::ceil(static_cast<float>(primaryAscent) *
                                      primaryScale)));
    const auto naturalHeight = static_cast<float>(primaryAscent - primaryDescent +
                                                   primaryLineGap) *
                               primaryScale;
    const auto lineHeight = std::max(
        static_cast<int>(std::ceil(style.pixelHeight * style.lineSpacing)),
        static_cast<int>(std::ceil(naturalHeight)));

    std::vector<PlacedGlyph> placements;
    placements.reserve(decoded.value().size());
    std::vector<float> lineWidths(style.maximumLines, 0.0F);
    std::size_t line = 0U;
    float penX = 0.0F;
    float lastBaseX = 0.0F;
    float lastBaseAdvance = 0.0F;
    std::optional<GlyphChoice> previous;
    bool truncated = false;

    auto newLine = [&]() -> bool {
      lineWidths[line] = std::max(lineWidths[line], penX);
      if (line + 1U >= style.maximumLines) {
        truncated = true;
        return false;
      }
      ++line;
      penX = 0.0F;
      lastBaseX = 0.0F;
      lastBaseAdvance = 0.0F;
      previous.reset();
      return true;
    };

    for (const auto codePoint : decoded.value()) {
      if (codePoint == U'\r') continue;
      if (codePoint == U'\n') {
        if (!newLine()) break;
        continue;
      }
      const auto choice = chooseGlyph(codePoint);
      if (!choice.has_value()) continue;
      const auto& face = *faces[choice->face];
      const auto scale = stbtt_ScaleForPixelHeight(&face.font, style.pixelHeight);
      int advanceUnits = 0;
      int bearing = 0;
      stbtt_GetGlyphHMetrics(&face.font, choice->glyph, &advanceUnits, &bearing);
      static_cast<void>(bearing);
      const bool combining = isCombiningMark(codePoint);
      float kern = 0.0F;
      if (!combining && previous.has_value() && previous->face == choice->face) {
        kern = static_cast<float>(stbtt_GetGlyphKernAdvance(
                   &face.font, previous->glyph, choice->glyph)) *
               scale;
      }
      const auto advance = combining
                               ? 0.0F
                               : std::max(0.0F, static_cast<float>(advanceUnits) *
                                                    scale + style.letterSpacing);
      const auto proposed = penX + kern + advance;
      if (!combining && style.maximumWidth > 0U && penX > 0.0F &&
          proposed > static_cast<float>(style.maximumWidth)) {
        if (!newLine()) break;
        if (codePoint == U' ' || codePoint == U'\t') continue;
        kern = 0.0F;
      }

      int x0 = 0;
      int y0 = 0;
      int x1 = 0;
      int y1 = 0;
      stbtt_GetGlyphBitmapBoxSubpixel(&face.font, choice->glyph, scale, scale,
                                      0.0F, 0.0F, &x0, &y0, &x1, &y1);
      const auto glyphPen = combining
                                ? lastBaseX + lastBaseAdvance * 0.5F
                                : penX + kern;
      placements.push_back(PlacedGlyph{
          .face = choice->face,
          .glyph = choice->glyph,
          .line = line,
          .penX = glyphPen,
          .scale = scale,
          .boxX0 = x0,
          .boxY0 = y0,
          .boxX1 = x1,
          .boxY1 = y1,
          .advance = advance,
          .combining = combining,
      });
      if (!combining) {
        lastBaseX = glyphPen;
        lastBaseAdvance = advance;
        penX = glyphPen + advance;
        previous = choice;
      }
      lineWidths[line] = std::max(lineWidths[line], penX);
    }

    if (truncated && style.ellipsize) {
      const auto ellipsis = chooseGlyph(U'…');
      if (ellipsis.has_value()) {
        const auto& face = *faces[ellipsis->face];
        const auto scale =
            stbtt_ScaleForPixelHeight(&face.font, style.pixelHeight);
        int advanceUnits = 0;
        int bearing = 0;
        stbtt_GetGlyphHMetrics(&face.font, ellipsis->glyph, &advanceUnits,
                               &bearing);
        static_cast<void>(bearing);
        const auto advance = static_cast<float>(advanceUnits) * scale +
                             style.letterSpacing;
        const auto limit = style.maximumWidth == 0U
                               ? std::numeric_limits<float>::max()
                               : static_cast<float>(style.maximumWidth);
        while (!placements.empty() && placements.back().line == line &&
               lineWidths[line] + advance > limit) {
          const auto removed = placements.back();
          placements.pop_back();
          if (!removed.combining) lineWidths[line] = removed.penX;
        }
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetGlyphBitmapBoxSubpixel(&face.font, ellipsis->glyph, scale,
                                        scale, 0.0F, 0.0F, &x0, &y0, &x1,
                                        &y1);
        placements.push_back(PlacedGlyph{
            .face = ellipsis->face,
            .glyph = ellipsis->glyph,
            .line = line,
            .penX = lineWidths[line],
            .scale = scale,
            .boxX0 = x0,
            .boxY0 = y0,
            .boxX1 = x1,
            .boxY1 = y1,
            .advance = advance,
            .combining = false,
        });
        lineWidths[line] += advance;
      }
    }

    const auto lineCount = line + 1U;
    float logicalWidth = 0.0F;
    for (std::size_t index = 0U; index < lineCount; ++index) {
      logicalWidth = std::max(logicalWidth, lineWidths[index]);
    }
    int minimumX = 0;
    int maximumX = static_cast<int>(std::ceil(logicalWidth));
    int minimumY = 0;
    int maximumY = static_cast<int>(lineCount) * lineHeight;
    for (const auto& placement : placements) {
      const auto glyphX = static_cast<int>(std::floor(placement.penX)) +
                          placement.boxX0;
      const auto glyphY = static_cast<int>(placement.line) * lineHeight +
                          baseline + placement.boxY0;
      minimumX = std::min(minimumX, glyphX);
      maximumX = std::max(maximumX,
                          static_cast<int>(std::ceil(placement.penX)) +
                              placement.boxX1);
      minimumY = std::min(minimumY, glyphY);
      maximumY = std::max(maximumY,
                          static_cast<int>(placement.line) * lineHeight +
                              baseline + placement.boxY1);
    }
    constexpr int padding = 2;
    const auto width = static_cast<std::uint32_t>(
        std::max(1, maximumX - minimumX + padding * 2));
    const auto height = static_cast<std::uint32_t>(
        std::max(1, maximumY - minimumY + padding * 2));
    if (width > kMaximumBitmapDimension || height > kMaximumBitmapDimension ||
        static_cast<std::uint64_t>(width) * height > kMaximumBitmapPixels) {
      return core::failure<RenderedText>(
          core::ErrorCode::Unsupported,
          "Rendered text bitmap exceeds the supported size limit");
    }

    AlphaBitmap bitmap{
        .width = width,
        .height = height,
        .baseline = baseline - minimumY + padding,
        .alpha = std::vector<std::uint8_t>(
            static_cast<std::size_t>(width) * height, 0U),
    };
    for (const auto& placement : placements) {
      const auto glyphWidth = placement.boxX1 - placement.boxX0;
      const auto glyphHeight = placement.boxY1 - placement.boxY0;
      if (glyphWidth <= 0 || glyphHeight <= 0) continue;
      std::vector<std::uint8_t> glyph(
          static_cast<std::size_t>(glyphWidth) *
              static_cast<std::size_t>(glyphHeight),
          0U);
      const auto& face = *faces[placement.face];
      stbtt_MakeGlyphBitmapSubpixel(
          &face.font, glyph.data(), glyphWidth, glyphHeight, glyphWidth,
          placement.scale, placement.scale, 0.0F, 0.0F, placement.glyph);
      const auto destinationX =
          static_cast<int>(std::floor(placement.penX)) + placement.boxX0 -
          minimumX + padding;
      const auto destinationY = static_cast<int>(placement.line) * lineHeight +
                                baseline + placement.boxY0 - minimumY +
                                padding;
      for (int glyphY = 0; glyphY < glyphHeight; ++glyphY) {
        const auto outputY = destinationY + glyphY;
        if (outputY < 0 || outputY >= static_cast<int>(height)) continue;
        for (int glyphX = 0; glyphX < glyphWidth; ++glyphX) {
          const auto outputX = destinationX + glyphX;
          if (outputX < 0 || outputX >= static_cast<int>(width)) continue;
          const auto source = glyph[static_cast<std::size_t>(glyphY) *
                                        static_cast<std::size_t>(glyphWidth) +
                                    static_cast<std::size_t>(glyphX)];
          auto& destination = bitmap.alpha[
              static_cast<std::size_t>(outputY) * width +
              static_cast<std::size_t>(outputX)];
          const auto combined = static_cast<std::uint32_t>(source) +
                                static_cast<std::uint32_t>(destination) -
                                (static_cast<std::uint32_t>(source) *
                                 static_cast<std::uint32_t>(destination) +
                                 127U) /
                                    255U;
          destination = static_cast<std::uint8_t>(std::min(255U, combined));
        }
      }
    }

    return RenderedText{
        .bitmap = std::move(bitmap),
        .metrics = TextMetrics{
            .width = static_cast<double>(logicalWidth),
            .height = static_cast<double>(lineCount *
                                          static_cast<std::size_t>(lineHeight)),
            .baseline = static_cast<double>(baseline),
            .lines = lineCount,
            .truncated = truncated,
        },
    };
  }

  void evictIfNeeded() {
    while (cache.size() > kMaximumCacheEntries ||
           cacheBytes > kMaximumCacheBytes) {
      auto oldest = cache.end();
      for (auto iterator = cache.begin(); iterator != cache.end(); ++iterator) {
        if (oldest == cache.end() ||
            iterator->second.lastAccess < oldest->second.lastAccess) {
          oldest = iterator;
        }
      }
      if (oldest == cache.end()) break;
      cacheBytes -= oldest->second.bytes;
      cache.erase(oldest);
      ++cacheEvictions;
    }
  }
};

namespace {

struct LoadedCandidate final {
  std::unique_ptr<TextEngine::Impl::Face> face;
  std::size_t order{0U};
};

core::Result<std::unique_ptr<TextEngine::Impl::Face>> loadBestFace(
    const std::filesystem::path& requestedPath,
    std::uint64_t maximumFontBytes,
    PreferredScript preferred) {
  const auto trusted = trustedRegularPath(requestedPath);
  if (!trusted) {
    return core::Result<std::unique_ptr<TextEngine::Impl::Face>>{
        trusted.error()};
  }
  const auto bytes = core::readFileBytesLimited(trusted.value(), maximumFontBytes);
  if (!bytes) {
    return core::Result<std::unique_ptr<TextEngine::Impl::Face>>{bytes.error()};
  }
  auto data = byteVector(bytes.value());
  const auto numberOfFonts = stbtt_GetNumberOfFonts(data.data());
  if (numberOfFonts <= 0 || numberOfFonts > 256) {
    return core::failure<std::unique_ptr<TextEngine::Impl::Face>>(
        core::ErrorCode::ParseError, "Font container has no usable faces",
        trusted.value().string());
  }
  int bestIndex = -1;
  std::size_t bestCoverage = 0U;
  std::size_t bestPreference = 0U;
  for (int faceIndex = 0; faceIndex < numberOfFonts; ++faceIndex) {
    const auto offset = stbtt_GetFontOffsetForIndex(data.data(), faceIndex);
    if (offset < 0) continue;
    stbtt_fontinfo info{};
    if (stbtt_InitFont(&info, data.data(), offset) == 0) continue;
    std::size_t coverage = 0U;
    for (const auto probe : kCoverageProbes) {
      if (stbtt_FindGlyphIndex(&info, static_cast<int>(probe)) != 0) ++coverage;
    }
    const auto preference =
        stbtt_FindGlyphIndex(&info, static_cast<int>(preferredProbe(preferred))) != 0
            ? 1U
            : 0U;
    if (bestIndex < 0 || preference > bestPreference ||
        (preference == bestPreference && coverage > bestCoverage)) {
      bestIndex = faceIndex;
      bestCoverage = coverage;
      bestPreference = preference;
    }
  }
  if (bestIndex < 0 || bestCoverage == 0U) {
    return core::failure<std::unique_ptr<TextEngine::Impl::Face>>(
        core::ErrorCode::Unsupported, "No supported Unicode face was found",
        trusted.value().string());
  }
  auto face = std::make_unique<TextEngine::Impl::Face>();
  face->bytes = std::move(data);
  const auto offset = stbtt_GetFontOffsetForIndex(face->bytes.data(), bestIndex);
  if (offset < 0 || stbtt_InitFont(&face->font, face->bytes.data(), offset) == 0) {
    return core::failure<std::unique_ptr<TextEngine::Impl::Face>>(
        core::ErrorCode::ParseError, "Unable to initialize selected font face",
        trusted.value().string());
  }
  const auto hash = core::sha256File(trusted.value(), maximumFontBytes);
  if (!hash) {
    return core::Result<std::unique_ptr<TextEngine::Impl::Face>>{hash.error()};
  }
  face->info = FontInfo{
      .path = trusted.value(),
      .sha256 = hash.value(),
      .faceIndex = bestIndex,
      .probeCoverage = bestCoverage,
  };
  face->preferenceScore = bestPreference;
  return face;
}

core::Result<std::unique_ptr<TextEngine::Impl>> createEngineImpl(
    const FontSearchOptions& inputOptions, bool includeSystemCandidates) {
  auto options = inputOptions;
  if (options.maximumFontBytes == 0U ||
      options.maximumFontBytes > 256ULL * 1024ULL * 1024ULL ||
      options.maximumFaces == 0U || options.maximumFaces > 16U) {
    return core::failure<std::unique_ptr<TextEngine::Impl>>(
        core::ErrorCode::InvalidArgument,
        "Font search limits are outside the supported range");
  }
  if (options.preferred == PreferredScript::Auto) {
    options.preferred = inferredScript();
  }
  std::vector<std::filesystem::path> candidates = options.additionalCandidates;
  if (includeSystemCandidates) {
    auto system = systemFontCandidates(options.preferred);
    candidates.insert(candidates.end(), system.begin(), system.end());
  }
  if (candidates.empty()) {
    return core::failure<std::unique_ptr<TextEngine::Impl>>(
        core::ErrorCode::InvalidArgument, "No trusted font candidates were supplied");
  }

  std::vector<LoadedCandidate> loaded;
  std::vector<std::filesystem::path> seen;
  for (std::size_t order = 0U; order < candidates.size(); ++order) {
    if (!candidates[order].is_absolute()) {
      if (!includeSystemCandidates || order < options.additionalCandidates.size()) {
        return core::failure<std::unique_ptr<TextEngine::Impl>>(
            core::ErrorCode::InvalidArgument,
            "Additional trusted font path must be absolute",
            candidates[order].string());
      }
      continue;
    }
    std::error_code canonicalError;
    const auto canonical = std::filesystem::canonical(candidates[order], canonicalError);
    if (canonicalError) continue;
    if (std::find(seen.begin(), seen.end(), canonical) != seen.end()) continue;
    seen.push_back(canonical);
    auto face = loadBestFace(canonical, options.maximumFontBytes,
                             options.preferred);
    if (!face) continue;
    loaded.push_back(LoadedCandidate{std::move(face.value()), order});
  }
  if (loaded.empty()) {
    return core::failure<std::unique_ptr<TextEngine::Impl>>(
        core::ErrorCode::NotFound,
        "No usable system or trusted Unicode font could be loaded");
  }
  std::stable_sort(loaded.begin(), loaded.end(),
                   [](const LoadedCandidate& left,
                      const LoadedCandidate& right) {
                     if (left.face->preferenceScore !=
                         right.face->preferenceScore) {
                       return left.face->preferenceScore >
                              right.face->preferenceScore;
                     }
                     if (left.face->info.probeCoverage !=
                         right.face->info.probeCoverage) {
                       return left.face->info.probeCoverage >
                              right.face->info.probeCoverage;
                     }
                     return left.order < right.order;
                   });
  auto implementation = std::make_unique<TextEngine::Impl>();
  const auto count = std::min(options.maximumFaces, loaded.size());
  implementation->faces.reserve(count);
  implementation->fontInfos.reserve(count);
  for (std::size_t index = 0U; index < count; ++index) {
    implementation->fontInfos.push_back(loaded[index].face->info);
    implementation->faces.push_back(std::move(loaded[index].face));
  }
  return implementation;
}

}  // namespace

TextEngine::TextEngine(std::unique_ptr<Impl> implementation) noexcept
    : implementation_(std::move(implementation)) {}

TextEngine::~TextEngine() = default;
TextEngine::TextEngine(TextEngine&&) noexcept = default;
TextEngine& TextEngine::operator=(TextEngine&&) noexcept = default;

core::Result<std::unique_ptr<TextEngine>> TextEngine::createSystem(
    const FontSearchOptions& options) {
  auto implementation = createEngineImpl(options, true);
  if (!implementation) {
    return core::Result<std::unique_ptr<TextEngine>>{implementation.error()};
  }
  return std::unique_ptr<TextEngine>{
      new TextEngine{std::move(implementation.value())}};
}

core::Result<std::unique_ptr<TextEngine>> TextEngine::createFromTrustedFiles(
    const FontSearchOptions& options) {
  auto implementation = createEngineImpl(options, false);
  if (!implementation) {
    return core::Result<std::unique_ptr<TextEngine>>{implementation.error()};
  }
  return std::unique_ptr<TextEngine>{
      new TextEngine{std::move(implementation.value())}};
}

core::Result<RenderedText> TextEngine::render(std::string_view utf8,
                                              const TextStyle& style) {
  auto rendered = renderShared(utf8, style);
  if (!rendered) return core::Result<RenderedText>{rendered.error()};
  return *rendered.value();
}

core::Result<std::shared_ptr<const RenderedText>> TextEngine::renderShared(
    std::string_view utf8, const TextStyle& style) {
  if (implementation_ == nullptr) {
    return core::failure<std::shared_ptr<const RenderedText>>(
        core::ErrorCode::Internal, "Text engine is not initialized");
  }
  const auto key = cacheKey(utf8, style);
  {
    std::scoped_lock lock{implementation_->cacheMutex};
    const auto found = implementation_->cache.find(key);
    if (found != implementation_->cache.end()) {
      found->second.lastAccess = ++implementation_->accessCounter;
      ++implementation_->cacheHits;
      return found->second.value;
    }
    ++implementation_->cacheMisses;
  }
  core::Result<RenderedText> rendered = core::failure<RenderedText>(
      core::ErrorCode::Internal, "Text rendering failed before execution");
  try {
    rendered = implementation_->renderUncached(utf8, style);
  } catch (const std::bad_alloc&) {
    return core::failure<std::shared_ptr<const RenderedText>>(
        core::ErrorCode::Unsupported, "Text rendering exceeded memory limits");
  } catch (const std::exception& exception) {
    return core::failure<std::shared_ptr<const RenderedText>>(
        core::ErrorCode::Internal, "Unexpected text rendering failure",
        exception.what());
  }
  if (!rendered) {
    return core::Result<std::shared_ptr<const RenderedText>>{rendered.error()};
  }
  std::shared_ptr<const RenderedText> shared;
  try {
    shared = std::make_shared<const RenderedText>(std::move(rendered.value()));
  } catch (const std::bad_alloc&) {
    return core::failure<std::shared_ptr<const RenderedText>>(
        core::ErrorCode::Unsupported, "Text rendering exceeded memory limits");
  }
  const auto byteCount = static_cast<std::uint64_t>(
      shared->bitmap.alpha.size() + key.size() + sizeof(Impl::CacheEntry));
  {
    std::scoped_lock lock{implementation_->cacheMutex};
    auto [iterator, inserted] = implementation_->cache.emplace(
        key, Impl::CacheEntry{
                 .value = shared,
                 .lastAccess = ++implementation_->accessCounter,
                 .bytes = byteCount,
             });
    if (!inserted) {
      implementation_->cacheBytes -= iterator->second.bytes;
      iterator->second = Impl::CacheEntry{
          .value = shared,
          .lastAccess = implementation_->accessCounter,
          .bytes = byteCount,
      };
    }
    implementation_->cacheBytes += byteCount;
    implementation_->evictIfNeeded();
  }
  return shared;
}

core::Result<TextMetrics> TextEngine::measure(std::string_view utf8,
                                              const TextStyle& style) {
  auto rendered = renderShared(utf8, style);
  if (!rendered) return core::Result<TextMetrics>{rendered.error()};
  return rendered.value()->metrics;
}

std::span<const FontInfo> TextEngine::fonts() const noexcept {
  if (implementation_ == nullptr) return {};
  return implementation_->fontInfos;
}

TextCacheStats TextEngine::cacheStats() const noexcept {
  if (implementation_ == nullptr) return {};
  std::scoped_lock lock{implementation_->cacheMutex};
  return TextCacheStats{
      .hits = implementation_->cacheHits,
      .misses = implementation_->cacheMisses,
      .evictions = implementation_->cacheEvictions,
      .entries = implementation_->cache.size(),
      .bytes = implementation_->cacheBytes,
  };
}

void TextEngine::clearCache() noexcept {
  if (implementation_ == nullptr) return;
  std::scoped_lock lock{implementation_->cacheMutex};
  implementation_->cache.clear();
  implementation_->cacheBytes = 0U;
}

}  // namespace seam::text
