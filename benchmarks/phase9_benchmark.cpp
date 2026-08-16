#include "seam/text/text_engine.hpp"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

int main() {
  auto engine = seam::text::TextEngine::createSystem();
  if (!engine) return 1;
  const std::vector<std::string> samples{
      "가나다라마바사 / こんにちは / 中文 / SEAM",
      "목소리의 경계를 지우지 않고 편집한다",
      "音素の継ぎ目を消さずに編集する",
      "连接声音的边界，而不是隐藏它",
  };
  const seam::text::TextStyle style{
      .pixelHeight = 24.0F,
      .letterSpacing = 0.0F,
      .lineSpacing = 1.2F,
      .maximumWidth = 720U,
      .maximumLines = 2U,
      .ellipsize = true,
  };

  const auto coldStart = std::chrono::steady_clock::now();
  for (const auto& sample : samples) {
    if (!engine.value()->render(sample, style)) return 2;
  }
  const auto coldElapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - coldStart);

  constexpr std::size_t iterations = 2000U;
  const auto warmStart = std::chrono::steady_clock::now();
  for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
    const auto& sample = samples[iteration % samples.size()];
    if (!engine.value()->render(sample, style)) return 3;
  }
  const auto warmElapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - warmStart);
  const auto stats = engine.value()->cacheStats();
  std::cout << "{\n"
            << "  \"phase\": \"9.0\",\n"
            << "  \"fontFaces\": " << engine.value()->fonts().size() << ",\n"
            << "  \"coldStrings\": " << samples.size() << ",\n"
            << "  \"coldTotalMs\": " << coldElapsed.count() << ",\n"
            << "  \"warmIterations\": " << iterations << ",\n"
            << "  \"warmTotalMs\": " << warmElapsed.count() << ",\n"
            << "  \"warmAverageUs\": "
            << warmElapsed.count() * 1000.0 /
                   static_cast<double>(iterations)
            << ",\n"
            << "  \"cacheHits\": " << stats.hits << ",\n"
            << "  \"cacheMisses\": " << stats.misses << ",\n"
            << "  \"cacheEntries\": " << stats.entries << "\n"
            << "}\n";
  return stats.hits >= iterations ? 0 : 4;
}
