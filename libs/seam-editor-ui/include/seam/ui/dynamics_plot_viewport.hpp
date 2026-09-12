#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace seam::ui {
class DynamicsPlotViewport final {
public:
  struct Range { std::int64_t start, end; };
  enum class Action { ZoomIn, ZoomOut, Fit, Left, Right };
  [[nodiscard]] static std::optional<std::int64_t> tickAtFraction(Range range, double fraction) noexcept {
    if (range.start < 0 || range.end <= range.start || !std::isfinite(fraction)) return std::nullopt;
    const auto span = range.end - range.start;
    const auto scaled = std::round(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(span));
    return range.start + (scaled >= static_cast<double>(span) ? span : static_cast<std::int64_t>(scaled));
  }
  [[nodiscard]] Range resolve(std::int64_t extent) const noexcept {
    extent = std::max(std::int64_t{1}, extent);
    if (!range_) return {0, extent};
    const auto start = std::clamp(range_->start, std::int64_t{0}, extent - 1);
    return {start, std::clamp(range_->end, start + 1, extent)};
  }
  void reset() noexcept { range_.reset(); }
  void navigate(Action action, std::int64_t extent, double anchor = 0.5) noexcept {
    if (!std::isfinite(anchor)) return;
    extent = std::max(std::int64_t{1}, extent);
    if (action == Action::Fit) { reset(); return; }
    auto range = resolve(extent); const auto span = range.end - range.start;
    if (action == Action::Left || action == Action::Right) {
      const auto step = std::max(std::int64_t{1}, span / 4);
      const auto start = action == Action::Left ? range.start - std::min(range.start, step)
          : range.start + std::min(extent - range.end, step);
      range_ = Range{start, start + span}; return;
    }
    anchor = std::clamp(anchor, 0.0, 1.0);
    const auto scaled = [&](std::int64_t ticks) {
      const auto value = static_cast<double>(ticks) * anchor;
      return value >= static_cast<double>(ticks) ? ticks : static_cast<std::int64_t>(value);
    };
    const auto nextSpan = action == Action::ZoomIn ? std::max(std::int64_t{1}, span / 2)
        : span > extent / 2 ? extent : span * 2;
    const auto start = std::clamp(range.start + scaled(span) - scaled(nextSpan), std::int64_t{0}, extent - nextSpan);
    range_ = Range{start, start + nextSpan};
  }
private:
  std::optional<Range> range_;
};
}
