// Times the voicebank catalogue scan a plug-in instance runs when it opens: the first scan in a
// process (every unit audio file hashed) and the repeats (digests reused while files are unchanged).
// The operating system's file cache is not flushed, so "first" means first in this process.
//
// Usage: seam_voicebank_scan_benchmark --root DIR [--root DIR ...] [--repeats N]
//                                      [--first-budget-ms MS] [--repeat-budget-ms MS]
// Prints one JSON object; exits 1 when a stated budget is exceeded, 2 on a usage or scan error.
#include "seam/voicebank/catalog.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::optional<double> parseNumber(const char* text) {
  try {
    std::size_t used = 0;
    const auto value = std::stod(text, &used);
    if (used != std::string_view{text}.size() || value < 0.0) return std::nullopt;
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<seam::voicebank::VoicebankSearchRoot> roots;
  std::size_t repeats = 5;
  std::optional<double> firstBudget;
  std::optional<double> repeatBudget;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    const bool hasValue = index + 1 < argc;
    if (argument == "--root" && hasValue) {
      roots.push_back({.path = std::filesystem::path{argv[++index]},
                       .kind = seam::voicebank::VoicebankRootKind::Installed});
    } else if (argument == "--repeats" && hasValue) {
      const auto value = parseNumber(argv[++index]);
      if (!value || *value < 1.0 || *value > 1000.0) return 2;
      repeats = static_cast<std::size_t>(*value);
    } else if (argument == "--first-budget-ms" && hasValue) {
      firstBudget = parseNumber(argv[++index]);
      if (!firstBudget) return 2;
    } else if (argument == "--repeat-budget-ms" && hasValue) {
      repeatBudget = parseNumber(argv[++index]);
      if (!repeatBudget) return 2;
    } else {
      std::cerr << "Usage: seam_voicebank_scan_benchmark --root DIR [--root DIR ...] "
                   "[--repeats N] [--first-budget-ms MS] [--repeat-budget-ms MS]\n";
      return 2;
    }
  }
  if (roots.empty()) {
    std::cerr << "At least one --root is required\n";
    return 2;
  }

  const seam::voicebank::VoicebankCatalog catalog;
  auto start = Clock::now();
  const auto first = catalog.scan(roots);
  const auto firstMs = millisecondsSince(start);
  if (!first) {
    std::cerr << "Scan failed: " << first.error().message << '\n';
    return 2;
  }
  std::size_t units = 0;
  for (const auto& candidate : first.value()) units += candidate.manifest.units.size();

  std::vector<double> repeatMs;
  for (std::size_t run = 0; run < repeats; ++run) {
    start = Clock::now();
    const auto again = catalog.scan(roots);
    repeatMs.push_back(millisecondsSince(start));
    if (!again || again.value().size() != first.value().size()) {
      std::cerr << "Repeat scan disagreed with the first\n";
      return 2;
    }
  }
  std::sort(repeatMs.begin(), repeatMs.end());
  const auto median = repeatMs[repeatMs.size() / 2U];
  const auto worst = repeatMs.back();
  const bool firstOk = !firstBudget || firstMs <= *firstBudget;
  const bool repeatOk = !repeatBudget || worst <= *repeatBudget;

  std::cout << "{\"candidates\": " << first.value().size() << ", \"units\": " << units
            << ", \"firstScanMs\": " << firstMs << ", \"repeatScanMedianMs\": " << median
            << ", \"repeatScanWorstMs\": " << worst << ", \"repeats\": " << repeats
            << ", \"withinBudget\": " << ((firstOk && repeatOk) ? "true" : "false") << "}\n";
  return firstOk && repeatOk ? 0 : 1;
}
