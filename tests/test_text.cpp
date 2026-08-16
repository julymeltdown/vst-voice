#include "test_framework.hpp"

#include "seam/native_ui/pixel_surface.hpp"
#include "seam/text/text_engine.hpp"
#include "seam/text/unicode.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

TEST_CASE("strict UTF-8 codec round-trips Korean Japanese Chinese and emoji") {
  const std::string source = "SEAM / 가나다 / こんにちは / 中文 / 🖤";
  const auto decoded = seam::text::decodeUtf8Strict(source);
  CHECK(decoded);
  const auto encoded = seam::text::encodeUtf8Strict(decoded.value());
  CHECK(encoded);
  CHECK(encoded.value() == source);
}

TEST_CASE("strict UTF-8 codec rejects overlong surrogate and out-of-range input") {
  CHECK(!seam::text::decodeUtf8Strict(std::string{"\xC0\xAF", 2U}));
  CHECK(!seam::text::decodeUtf8Strict(std::string{"\xED\xA0\x80", 3U}));
  CHECK(!seam::text::decodeUtf8Strict(std::string{"\xF4\x90\x80\x80", 4U}));
  CHECK(!seam::text::decodeUtf8Strict(std::string{"\xE3\x81", 2U}));
  CHECK(!seam::text::encodeUtf8Strict(
      std::u32string{static_cast<char32_t>(0xD800U)}));
}

TEST_CASE("system text engine resolves a trusted Unicode font") {
  const auto engine = seam::text::TextEngine::createSystem();
  CHECK(engine);
  CHECK(!engine.value()->fonts().empty());
  CHECK(engine.value()->fonts().front().path.is_absolute());
  CHECK(engine.value()->fonts().front().sha256.size() == 64U);
  CHECK(engine.value()->fonts().front().probeCoverage >= 4U);
}

TEST_CASE("text engine renders CJK glyph alpha and reports coherent metrics") {
  auto engine = seam::text::TextEngine::createSystem();
  CHECK(engine);
  const seam::text::TextStyle style{.pixelHeight = 30.0F,
                                    .letterSpacing = 0.0F,
                                    .lineSpacing = 1.2F,
                                    .maximumWidth = 600U,
                                    .maximumLines = 2U,
                                    .ellipsize = true};
  const auto rendered = engine.value()->render(
      "가나다라마바사 / こんにちは / 中文 / SEAM", style);
  CHECK(rendered);
  CHECK(rendered.value().bitmap.width > 32U);
  CHECK(rendered.value().bitmap.height > 16U);
  CHECK(std::any_of(rendered.value().bitmap.alpha.begin(),
                    rendered.value().bitmap.alpha.end(),
                    [](std::uint8_t value) { return value > 0U; }));
  CHECK(rendered.value().metrics.width > 0.0);
  CHECK(rendered.value().metrics.height > 0.0);
  const auto measured = engine.value()->measure(
      "가나다라마바사 / こんにちは / 中文 / SEAM", style);
  CHECK(measured);
  CHECK_NEAR(measured.value().width, rendered.value().metrics.width, 0.001);
  CHECK_NEAR(measured.value().height, rendered.value().metrics.height, 0.001);
}

TEST_CASE("text engine wraps and ellipsizes within explicit bounds") {
  auto engine = seam::text::TextEngine::createSystem();
  CHECK(engine);
  const auto rendered = engine.value()->render(
      "목소리의 경계를 지우지 않고 편집합니다。音素の継ぎ目を残します。",
      seam::text::TextStyle{.pixelHeight = 24.0F,
                            .letterSpacing = 0.0F,
                            .lineSpacing = 1.15F,
                            .maximumWidth = 180U,
                            .maximumLines = 2U,
                            .ellipsize = true});
  CHECK(rendered);
  CHECK(rendered.value().metrics.lines == 2U);
  CHECK(rendered.value().metrics.truncated);
  CHECK(rendered.value().metrics.width <= 190.0);
}

TEST_CASE("text render cache reports repeat hits and can be cleared") {
  auto engine = seam::text::TextEngine::createSystem();
  CHECK(engine);
  const seam::text::TextStyle style{.pixelHeight = 18.0F};
  CHECK(engine.value()->render("継ぎ目", style));
  const auto first = engine.value()->cacheStats();
  CHECK(engine.value()->render("継ぎ目", style));
  const auto second = engine.value()->cacheStats();
  CHECK(second.hits == first.hits + 1U);
  CHECK(second.entries >= 1U);
  engine.value()->clearCache();
  CHECK(engine.value()->cacheStats().entries == 0U);
}

TEST_CASE("trusted font API rejects relative paths") {
  seam::text::FontSearchOptions options;
  options.additionalCandidates.emplace_back("relative-font.ttf");
  const auto engine = seam::text::TextEngine::createFromTrustedFiles(options);
  CHECK(!engine);
}

TEST_CASE("RasterCanvas uses Unicode engine instead of byte-glyph fallback") {
  auto engine = seam::text::TextEngine::createSystem();
  CHECK(engine);
  seam::native_ui::PixelSurface fallback{360U, 100U};
  fallback.clear(seam::native_ui::Color{16U, 15U, 19U, 255U});
  seam::native_ui::RasterCanvas fallbackCanvas{fallback, 1.0};
  fallbackCanvas.drawText(seam::ui::Point{12.0, 20.0}, "가나다 / かな",
                          seam::native_ui::Color{240U, 235U, 242U, 255U},
                          26.0);

  seam::native_ui::PixelSurface unicode{360U, 100U};
  unicode.clear(seam::native_ui::Color{16U, 15U, 19U, 255U});
  seam::native_ui::RasterCanvas unicodeCanvas{unicode, 1.0,
                                               engine.value().get()};
  CHECK(unicodeCanvas.unicodeTextEnabled());
  unicodeCanvas.drawText(seam::ui::Point{12.0, 20.0}, "가나다 / かな",
                         seam::native_ui::Color{240U, 235U, 242U, 255U},
                         26.0);
  CHECK(unicode.checksum() != fallback.checksum());
  CHECK(unicode.checksum() != 0U);
}
