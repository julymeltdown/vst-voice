#pragma once
#include "seam/core/result.hpp"
#include "seam/time/tempo_map.hpp"
#include "seam/time/meter_map.hpp"
#include "seam/domain/project.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <algorithm>
#include <optional>

namespace seam::native_ui {
// Shortest round-trip decimal: opening and committing an unchanged field must
// never quantize a saved BPM to std::to_string's six fractional digits.
inline std::string tempoEditText(double bpm) {
  std::array<char, 64> bytes{};
  const auto result = std::to_chars(bytes.data(), bytes.data() + bytes.size(), bpm);
  return result.ec == std::errc{} ? std::string(bytes.data(), result.ptr) : std::string{};
}

inline core::Result<double> parseTempoEditText(std::string_view text) {
  if (text.empty() || text.size() > 64U)
    return core::failure<double>(core::ErrorCode::InvalidArgument, "Enter a bounded numeric BPM value");
  double bpm = 0.0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), bpm);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
      !std::isfinite(bpm) || bpm <= 0.0 || bpm > 1000.0)
    return core::failure<double>(core::ErrorCode::InvalidArgument, "Enter a finite BPM in (0, 1000]");
  return core::success(bpm);
}

struct TempoEditContext final {
  std::uint64_t revision;
  time::Tick tick;
  bool meter{false};
  bool chooseTick{false};
  bool insert{false};
};

inline core::Result<time::Tick> parseTimeMapTick(std::string_view text) {
  if (text.empty() || text.size() > 19U)
    return core::failure<time::Tick>(core::ErrorCode::InvalidArgument, "Enter a nonnegative integer tick");
  std::int64_t tick = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), tick);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || tick < 0)
    return core::failure<time::Tick>(core::ErrorCode::InvalidArgument, "Tick must be a complete nonnegative int64 value");
  return core::success(time::Tick{tick});
}

inline std::string meterEditText(time::MeterEvent meter) {
  return std::to_string(meter.numerator) + "/" + std::to_string(meter.denominator);
}
inline core::Result<time::MeterEvent> parseMeterEditText(std::string_view text) {
  const auto slash = text.find('/');
  unsigned numerator = 0U, denominator = 0U;
  if (text.empty() || text.size() > 16U || slash == std::string_view::npos)
    return core::failure<time::MeterEvent>(core::ErrorCode::InvalidArgument, "Enter a time signature such as 4/4");
  const auto first = std::from_chars(text.data(), text.data() + slash, numerator);
  const auto second = std::from_chars(text.data() + slash + 1U, text.data() + text.size(), denominator);
  if (first.ec != std::errc{} || first.ptr != text.data() + slash ||
      second.ec != std::errc{} || second.ptr != text.data() + text.size() ||
      numerator < 1U || numerator > 32U || denominator > 32U)
    return core::failure<time::MeterEvent>(core::ErrorCode::InvalidArgument, "Invalid time signature");
  time::MeterMap validation;
  const auto valid = validation.addOrReplace(time::Tick{0}, static_cast<std::uint8_t>(numerator), static_cast<std::uint8_t>(denominator));
  if (!valid) return core::Result<time::MeterEvent>{valid.error()};
  return core::success(validation.events().front());
}

struct TimeMapEventRow final {
  time::Tick tick;
  bool meter;
  std::string value;
  [[nodiscard]] bool removable() const noexcept { return tick != time::Tick{0}; }
};

// Immutable event snapshot with mutable selection. UI pages never truncate the
// stored map or use an event's row number as its mutation identity.
class TempoMeterModel final {
public:
  static constexpr std::size_t pageSize = 6U;
  static constexpr std::size_t maximumEvents = 16384U;
  [[nodiscard]] static core::Result<TempoMeterModel> capture(const domain::Project& project, std::uint64_t revision) {
    const auto tempoCount = project.tempoMap().events().size();
    const auto meterCount = project.meterMap().events().size();
    if (tempoCount > maximumEvents || meterCount > maximumEvents - tempoCount)
      return core::failure<TempoMeterModel>(core::ErrorCode::InvalidArgument, "Time-map editor supports at most 16384 combined events");
    TempoMeterModel model{project, revision};
    model.rows_.reserve(tempoCount + meterCount);
    for (const auto& event : project.tempoMap().events()) model.rows_.push_back({event.tick, false, tempoEditText(event.bpm)});
    for (const auto& event : project.meterMap().events()) model.rows_.push_back({event.tick, true, meterEditText(event)});
    std::sort(model.rows_.begin(), model.rows_.end(), [](const auto& a, const auto& b) {
      return a.tick != b.tick ? a.tick < b.tick : a.meter < b.meter;
    });
    return core::success(std::move(model));
  }
  [[nodiscard]] bool matches(const domain::Project& project, std::uint64_t revision) const noexcept {
    return project.id() == projectId_ && revision == revision_ &&
        project.tempoMap() == tempo_ && project.meterMap() == meter_;
  }
  [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
  [[nodiscard]] domain::ProjectId projectId() const noexcept { return projectId_; }
  [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }
  [[nodiscard]] std::size_t selectedIndex() const noexcept { return selected_; }
  [[nodiscard]] const TimeMapEventRow& selected() const { return rows_.at(selected_); }
  [[nodiscard]] core::Result<void> select(std::size_t index) {
    if (index >= rows_.size()) return core::failure(core::ErrorCode::InvalidArgument, "Time-map row is unavailable");
    selected_ = index; return core::success();
  }
  // Retain the exact tick/type when present. After deletion choose its sorted
  // successor, or the final remaining row if there is no successor.
  void selectNearest(time::Tick tick, bool meter) noexcept {
    const auto found = std::lower_bound(rows_.begin(), rows_.end(), std::pair{tick, meter},
        [](const auto& row, const auto& key) { return row.tick != key.first ? row.tick < key.first : row.meter < key.second; });
    if (rows_.empty()) { selected_ = 0U; return; }
    selected_ = found == rows_.end() ? rows_.size() - 1U : static_cast<std::size_t>(found - rows_.begin());
  }
  [[nodiscard]] std::vector<TimeMapEventRow> page(std::size_t pageIndex) const {
    if (pageIndex > rows_.size() / pageSize) return {};
    const auto first = pageIndex * pageSize;
    const auto last = std::min(rows_.size(), first + pageSize);
    return {rows_.begin() + static_cast<std::ptrdiff_t>(first), rows_.begin() + static_cast<std::ptrdiff_t>(last)};
  }
private:
  TempoMeterModel(const domain::Project& project, std::uint64_t revision)
      : projectId_(project.id()), revision_(revision), tempo_(project.tempoMap()), meter_(project.meterMap()) {}
  domain::ProjectId projectId_;
  std::uint64_t revision_;
  time::TempoMap tempo_;
  time::MeterMap meter_;
  std::vector<TimeMapEventRow> rows_;
  std::size_t selected_{0U};
};
}
