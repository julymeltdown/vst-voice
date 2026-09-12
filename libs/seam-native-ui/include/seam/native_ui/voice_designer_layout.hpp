#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace seam::native_ui {
struct VoiceDesignerLayout final {
  static constexpr double controlsTop = 174.0, rowHeight = 32.0;
  static constexpr double textSize = 15.0;
  std::size_t visibleRows;
  bool compact;
  double summaryY, statusY, helpY, referenceY, prepareY, fricationY, errorY;
  [[nodiscard]] std::size_t firstRow(std::size_t count, std::size_t selected) const noexcept {
    if (count <= visibleRows) return 0U;
    selected = std::min(selected,count-1U);
    return selected > visibleRows/2U ? std::min(selected-visibleRows/2U,count-visibleRows) : 0U;
  }
};
// Geometry supports the base window contract's 320-pixel lower bound.
// Studio currently enforces a stricter 720x520 window minimum.
[[nodiscard]] inline VoiceDesignerLayout voiceDesignerLayout(double height) noexcept {
  if (!std::isfinite(height)) height = 320.0;
  height = std::clamp(height,320.0,8192.0);
  const bool compact = height < 600.0;
  // Keep all action bars reachable at Studio's 520-pixel minimum. Only the
  // noninteractive reference identity line is omitted in compact mode.
  // The base backend's smaller, unsupported-by-Studio sizes retain a fallback.
  const bool sourceActions = height >= 400.0;
  const double footer = compact ? (sourceActions ? 136.0 : 84.0) : 188.0;
  const auto rows = static_cast<std::size_t>(std::clamp(std::floor((height-VoiceDesignerLayout::controlsTop-footer)/VoiceDesignerLayout::rowHeight),1.0,256.0));
  const auto summary = VoiceDesignerLayout::controlsTop+static_cast<double>(rows)*VoiceDesignerLayout::rowHeight+4.0;
  return {rows,compact,summary,summary+26.0,summary+52.0,
      compact ? -1.0 : summary+78.0,
      !sourceActions ? -1.0 : summary+(compact ? 78.0 : 104.0),
      !sourceActions ? -1.0 : summary+(compact ? 104.0 : 130.0),
      compact ? summary+26.0 : summary+156.0};
}
}
