#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/text/text_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct Options final {
  std::filesystem::path output;
  std::optional<std::filesystem::path> font;
};

std::optional<Options> parse(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--output" && index + 1 < argc) {
      options.output = std::filesystem::path{argv[++index]};
      continue;
    }
    if (argument == "--font" && index + 1 < argc) {
      options.font = std::filesystem::path{argv[++index]};
      continue;
    }
    if (argument == "--help") return std::nullopt;
    std::cerr << "Unknown argument: " << argument << '\n';
    return std::nullopt;
  }
  if (options.output.empty()) return std::nullopt;
  return options;
}

seam::formats::JsonValue fontJson(const seam::text::FontInfo& font) {
  return seam::formats::JsonValue::Object{
      {"path", font.path.string()},
      {"sha256", font.sha256},
      {"faceIndex", static_cast<std::int64_t>(font.faceIndex)},
      {"probeCoverage", static_cast<std::int64_t>(font.probeCoverage)},
  };
}

void usage() {
  std::cout << "Usage: seam_phase9_demo --output DIRECTORY [--font ABSOLUTE_PATH]\n";
}

}  // namespace

int main(int argc, char** argv) {
  const auto options = parse(argc, argv);
  if (!options.has_value()) {
    usage();
    return argc > 1 && std::string_view{argv[1]} == "--help" ? 0 : 2;
  }
  std::error_code error;
  std::filesystem::create_directories(options->output, error);
  if (error) {
    std::cerr << "Unable to create output directory: " << error.message() << '\n';
    return 3;
  }

  seam::text::FontSearchOptions fontOptions;
  if (options->font.has_value()) {
    fontOptions.additionalCandidates.push_back(*options->font);
  }
  auto engine = seam::text::TextEngine::createSystem(fontOptions);
  if (!engine) {
    std::cerr << "Text engine initialization failed: "
              << engine.error().message << '\n';
    return 4;
  }

  seam::native_ui::PixelSurface surface{1280U, 720U};
  seam::native_ui::RasterCanvas canvas{surface, 1.0, engine.value().get()};
  const seam::native_ui::Color background{18U, 17U, 21U, 255U};
  const seam::native_ui::Color panel{29U, 27U, 34U, 255U};
  const seam::native_ui::Color primary{239U, 233U, 240U, 255U};
  const seam::native_ui::Color secondary{174U, 164U, 181U, 255U};
  const seam::native_ui::Color purple{156U, 105U, 184U, 255U};
  const seam::native_ui::Color burgundy{148U, 77U, 106U, 255U};
  const seam::native_ui::Color teal{92U, 189U, 186U, 255U};

  canvas.clear(background);
  canvas.fillRect(seam::ui::Rect{32.0, 28.0, 1216.0, 664.0}, panel);
  canvas.fillRect(seam::ui::Rect{32.0, 28.0, 12.0, 664.0}, burgundy);
  canvas.drawText(seam::ui::Point{72.0, 58.0},
                  "PROJECT SEAM / PHASE 9", primary, 28.0);
  canvas.drawText(seam::ui::Point{72.0, 104.0},
                  "시스템 폰트 기반 CJK 텍스트 렌더링", primary, 34.0);
  canvas.drawText(seam::ui::Point{72.0, 156.0},
                  "音素の継ぎ目を消さず、表現として編集する。", primary, 28.0);
  canvas.drawText(seam::ui::Point{72.0, 202.0},
                  "连接声音的边界，而不是隐藏它。", primary, 28.0);
  canvas.drawText(seam::ui::Point{72.0, 250.0},
                  "Hangul · かな · 漢字 · Latin / UTF-8", teal, 23.0);

  canvas.fillRect(seam::ui::Rect{72.0, 310.0, 1130.0, 142.0},
                  seam::native_ui::Color{22U, 21U, 27U, 255U});
  canvas.drawText(seam::ui::Point{92.0, 330.0},
                  "PIANO ROLL / 가사 / 歌詞", secondary, 18.0);
  const std::string lyrics = "こ え 를 이 어 붙 인 경 계";
  canvas.drawText(seam::ui::Point{92.0, 372.0}, lyrics, primary, 30.0);
  canvas.line(seam::ui::Point{92.0, 422.0},
              seam::ui::Point{1164.0, 422.0}, purple, 2.0);

  canvas.drawText(seam::ui::Point{72.0, 490.0},
                  "System-installed fonts only · no font redistribution",
                  secondary, 18.0);
  canvas.drawText(seam::ui::Point{72.0, 524.0},
                  "Trusted absolute paths · bounded cache · strict UTF-8",
                  secondary, 18.0);

  const auto rendered = engine.value()->render(
      "가나다라마바사 / こんにちは / 中文 / SEAM",
      seam::text::TextStyle{.pixelHeight = 32.0F,
                            .letterSpacing = 0.0F,
                            .lineSpacing = 1.2F,
                            .maximumWidth = 520U,
                            .maximumLines = 2U,
                            .ellipsize = true});
  if (!rendered) {
    std::cerr << "Text render failed: " << rendered.error().message << '\n';
    return 5;
  }
  // Render the same key again to prove that the bounded cache is active.
  const auto cached = engine.value()->render(
      "가나다라마바사 / こんにちは / 中文 / SEAM",
      seam::text::TextStyle{.pixelHeight = 32.0F,
                            .letterSpacing = 0.0F,
                            .lineSpacing = 1.2F,
                            .maximumWidth = 520U,
                            .maximumLines = 2U,
                            .ellipsize = true});
  if (!cached) return 6;

  const auto imagePath = options->output / "phase9-cjk-text.ppm";
  const auto imageResult = surface.writePpm(imagePath);
  if (!imageResult) {
    std::cerr << "Unable to write Phase 9 image: "
              << imageResult.error().message << '\n';
    return 7;
  }

  seam::formats::JsonValue::Array fonts;
  for (const auto& font : engine.value()->fonts()) fonts.push_back(fontJson(font));
  const auto stats = engine.value()->cacheStats();
  const auto& metrics = rendered.value().metrics;
  const seam::formats::JsonValue summary{
      seam::formats::JsonValue::Object{
          {"phase", static_cast<std::int64_t>(9)},
          {"unicodeRenderer", true},
          {"fontFilesRedistributed", false},
          {"fonts", std::move(fonts)},
          {"metrics", seam::formats::JsonValue::Object{
                          {"width", metrics.width},
                          {"height", metrics.height},
                          {"baseline", metrics.baseline},
                          {"lines", static_cast<std::int64_t>(metrics.lines)},
                          {"truncated", metrics.truncated},
                      }},
          {"cache", seam::formats::JsonValue::Object{
                        {"hits", static_cast<std::int64_t>(stats.hits)},
                        {"misses", static_cast<std::int64_t>(stats.misses)},
                        {"evictions", static_cast<std::int64_t>(stats.evictions)},
                        {"entries", static_cast<std::int64_t>(stats.entries)},
                        {"bytes", static_cast<std::int64_t>(stats.bytes)},
                    }},
          {"surfaceChecksum", static_cast<std::int64_t>(
                                  surface.checksum() & 0x7FFFFFFFFFFFFFFFULL)},
      }};
  const auto summaryText = seam::formats::stringifyJson(summary, true);
  const auto summaryResult = seam::core::durableAtomicWriteText(
      options->output / "phase9-summary.json", summaryText);
  if (!summaryResult) {
    std::cerr << "Unable to write summary: " << summaryResult.error().message
              << '\n';
    return 8;
  }

  std::cout << "fonts=" << engine.value()->fonts().size() << '\n'
            << "cacheHits=" << stats.hits << '\n'
            << "cacheMisses=" << stats.misses << '\n'
            << "bitmap=" << rendered.value().bitmap.width << 'x'
            << rendered.value().bitmap.height << '\n'
            << "surfaceChecksum=" << surface.checksum() << '\n';
  return 0;
}
