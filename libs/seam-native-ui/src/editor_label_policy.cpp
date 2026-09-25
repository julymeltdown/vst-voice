#include "seam/native_ui/editor_label_policy.hpp"

#include "seam/text/unicode.hpp"

#include <algorithm>
#include <cmath>

namespace seam::native_ui {
namespace {

EditorLabel choose(std::string_view value, double availableWidth,
                   double minimumWidth, double fullWidth,
                   double compactCharacterWidth) noexcept {
  EditorLabel result;
  result.fullText = std::string{value};
  if (value.empty() || !std::isfinite(availableWidth) ||
      availableWidth < minimumWidth) {
    return result;
  }
  result.mode = availableWidth >= fullWidth ? EditorLabelMode::Full
                                            : EditorLabelMode::Compact;
  const auto columns = static_cast<std::size_t>(std::max(
      1.0, std::floor(availableWidth / compactCharacterWidth)));
  const auto truncated = text::utf8DisplayWidth(value) > columns;
  // Keep the compact note label bounded while making omitted lyric text
  // visible to sighted creators. `fullText` remains intact for the detail
  // surface and accessibility tree.
  result.text = text::truncateUtf8ToDisplayWidth(
      value, truncated && columns > 0U ? columns - 1U : columns);
  if (truncated) result.text += "…";
  return result;
}

}

EditorLabel EditorLabelPolicy::note(std::string_view value,
                                    double availableWidth) noexcept {
  return choose(value, availableWidth, 24.0, 96.0, 6.0);
}

EditorLabel EditorLabelPolicy::technical(std::string_view value,
                                         double availableWidth,
                                         double minimumWidth,
                                         double fullWidth) noexcept {
  return choose(value, availableWidth, minimumWidth, fullWidth, 6.0);
}

}
