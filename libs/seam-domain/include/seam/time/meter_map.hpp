#pragma once

#include "seam/core/result.hpp"
#include "seam/time/tick.hpp"

#include <cstdint>
#include <vector>

namespace seam::time {

struct MeterEvent final {
  Tick tick;
  std::uint8_t numerator{4};
  std::uint8_t denominator{4};

  friend bool operator==(const MeterEvent&, const MeterEvent&) = default;
};

struct BarBeat final {
  std::int64_t bar{1};
  std::int32_t beat{1};
  Tick tickInBeat{0};

  friend bool operator==(const BarBeat&, const BarBeat&) = default;
};

class MeterMap final {
public:
  explicit MeterMap(Ppq ppq = kDefaultPpq);

  [[nodiscard]] Ppq ppq() const noexcept { return ppq_; }
  [[nodiscard]] const std::vector<MeterEvent>& events() const noexcept { return events_; }

  [[nodiscard]] core::Result<void> addOrReplace(
      Tick tick, std::uint8_t numerator, std::uint8_t denominator);
  [[nodiscard]] core::Result<void> remove(Tick tick);

  [[nodiscard]] MeterEvent meterAt(Tick tick) const noexcept;
  [[nodiscard]] Tick ticksPerBeat(const MeterEvent& meter) const noexcept;
  [[nodiscard]] Tick ticksPerBar(const MeterEvent& meter) const noexcept;
  [[nodiscard]] BarBeat barBeatAt(Tick tick) const noexcept;

  friend bool operator==(const MeterMap&, const MeterMap&) = default;

private:
  [[nodiscard]] static bool validDenominator(std::uint8_t denominator) noexcept;
  [[nodiscard]] std::size_t eventIndexAt(Tick tick) const noexcept;

  Ppq ppq_;
  std::vector<MeterEvent> events_;
};

}  // namespace seam::time
