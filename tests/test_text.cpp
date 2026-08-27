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

TEST_CASE("Unicode display width preserves combining marks and wide scalars") {
  CHECK(seam::text::utf8DisplayWidth("Á") == 1U);
  CHECK(seam::text::utf8DisplayWidth("かな") == 4U);
  CHECK(seam::text::utf8DisplayWidth("中文") == 4U);
  CHECK(seam::text::utf8DisplayWidth("🖤") == 2U);
  CHECK(seam::text::utf8DisplayWidth("👩‍🎤") == 2U);
  CHECK(seam::text::utf8DisplayWidth("🇯🇵") == 2U);
  CHECK(seam::text::utf8DisplayWidth("Ａ") == 2U);

  const std::string mixed = "Á日本語";
  CHECK(seam::text::truncateUtf8ToDisplayWidth(mixed, 1U) == "Á");
  CHECK(seam::text::truncateUtf8ToDisplayWidth(mixed, 3U) == "Á日");
  CHECK(seam::text::truncateUtf8ToDisplayWidth(mixed, 5U) == "Á日本");
  const std::string emoji = "A👩‍🎤B";
  CHECK(seam::text::truncateUtf8ToDisplayWidth(emoji, 2U) == "A");
  CHECK(seam::text::truncateUtf8ToDisplayWidth(emoji, 3U) == "A👩‍🎤");
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

TEST_CASE("unsupported emoji does not render a misleading replacement glyph") {
  auto engine = seam::text::TextEngine::createSystem();
  CHECK(engine);
  const auto rendered = engine.value()->render(
      "🎤", seam::text::TextStyle{.pixelHeight = 18.0F});
  CHECK(rendered);
  CHECK(std::none_of(rendered.value().bitmap.alpha.begin(),
                     rendered.value().bitmap.alpha.end(),
                     [](std::uint8_t value) { return value != 0U; }));
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

TEST_CASE("text render cache shares immutable bitmap storage on repeat hits") {
  auto engine = seam::text::TextEngine::createSystem();
  CHECK(engine);
  const seam::text::TextStyle style{
      .pixelHeight = 14.0F,
      .maximumWidth = 240U,
      .maximumLines = 1U,
      .ellipsize = true,
  };
  const auto first = engine.value()->renderShared("こんにちは 안녕 你好", style);
  const auto second = engine.value()->renderShared("こんにちは 안녕 你好", style);
  CHECK(first);
  CHECK(second);
  if (first && second) {
    CHECK(first.value().get() == second.value().get());
    CHECK(!first.value()->bitmap.alpha.empty());
  }
}

TEST_CASE("text render cache keeps distinct styles isolated") {
  auto engine = seam::text::TextEngine::createSystem();
  CHECK(engine);
  const seam::text::TextStyle compact{
      .pixelHeight = 14.0F,
      .maximumWidth = 240U,
      .maximumLines = 1U,
      .ellipsize = true,
  };
  auto spacious = compact;
  spacious.letterSpacing = 1.5F;
  const auto first = engine.value()->renderShared("same text", compact);
  const auto second = engine.value()->renderShared("same text", spacious);
  CHECK(first);
  CHECK(second);
  if (first && second) {
    CHECK(first.value().get() != second.value().get());
    CHECK(engine.value()->cacheStats().entries == 2U);
  }
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

TEST_CASE("RasterCanvas keeps bounded Unicode text inside its owning rectangle") {
  const auto engine = seam::text::TextEngine::createSystem();
  CHECK(engine);
  const auto background = seam::native_ui::Color{16U, 15U, 19U, 255U};
  const auto text = seam::native_ui::Color{240U, 235U, 242U, 255U};
  const auto bounds = seam::ui::Rect{40.0, 20.0, 84.0, 26.0};
  const std::array<std::string, 6U> samples{{
      "long Latin label that must ellipsize",
      "가나다라마바사라마바사",
      "こんにちは世界こんにちは世界",
      "中文歌词需要保持在边界内",
      "Á emoji 👨‍👩‍👧‍👦",
      std::string{"bad\xFFutf8", 8U},
  }};
  for (const auto unicode : {false, true}) {
    seam::native_ui::PixelSurface surface{160U, 80U};
    surface.clear(background);
    seam::native_ui::RasterCanvas canvas{
        surface, 1.0, unicode ? engine.value().get() : nullptr};
    for (const auto& sample : samples) {
      canvas.drawText(bounds, sample, text, 16.0);
    }
    for (std::uint32_t y = 0U; y < surface.height(); ++y) {
      for (std::uint32_t x = 0U; x < surface.width(); ++x) {
        const auto inside = x >= 40U && x < 124U && y >= 20U && y < 46U;
        if (!inside) {
          CHECK(surface.pixels()[static_cast<std::size_t>(y) * surface.width() + x] ==
                background.bgra());
        }
      }
    }
  }
}
