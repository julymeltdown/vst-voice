#include "seam/time/meter_map.hpp"

#include <algorithm>

namespace seam::time {

MeterMap::MeterMap(Ppq ppq) : ppq_(ppq > 0 ? ppq : kDefaultPpq), events_{{Tick{0}, 4, 4}} {}

bool MeterMap::validDenominator(std::uint8_t denominator) noexcept {
  return denominator == 1 || denominator == 2 || denominator == 4 ||
         denominator == 8 || denominator == 16 || denominator == 32;
}

core::Result<void> MeterMap::addOrReplace(
    Tick tick, std::uint8_t numerator, std::uint8_t denominator) {
  if (tick < Tick{0}) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Meter event tick must not be negative");
  }
  if (numerator == 0 || numerator > 32 || !validDenominator(denominator)) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Unsupported time signature");
  }

  const auto iterator = std::lower_bound(events_.begin(), events_.end(), tick,
      [](const MeterEvent& event, Tick value) { return event.tick < value; });
  if (iterator != events_.end() && iterator->tick == tick) {
    iterator->numerator = numerator;
    iterator->denominator = denominator;
  } else {
    events_.insert(iterator, MeterEvent{tick, numerator, denominator});
  }
  return core::success();
}

core::Result<void> MeterMap::remove(Tick tick) {
  if (tick == Tick{0}) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "The initial meter event cannot be removed");
  }
  const auto iterator = std::lower_bound(events_.begin(), events_.end(), tick,
      [](const MeterEvent& event, Tick value) { return event.tick < value; });
  if (iterator == events_.end() || iterator->tick != tick) {
    return core::failure(core::ErrorCode::NotFound, "Meter event was not found");
  }
  events_.erase(iterator);
  return core::success();
}

std::size_t MeterMap::eventIndexAt(Tick tick) const noexcept {
  const auto iterator = std::upper_bound(events_.begin(), events_.end(), tick,
      [](Tick value, const MeterEvent& event) { return value < event.tick; });
  if (iterator == events_.begin()) {
    return 0;
  }
  return static_cast<std::size_t>(std::distance(events_.begin(), iterator - 1));
}

MeterEvent MeterMap::meterAt(Tick tick) const noexcept {
  return events_[eventIndexAt(tick)];
}

Tick MeterMap::ticksPerBeat(const MeterEvent& meter) const noexcept {
  const auto ticks = static_cast<std::int64_t>(ppq_) * 4 / meter.denominator;
  return Tick{ticks};
}

Tick MeterMap::ticksPerBar(const MeterEvent& meter) const noexcept {
  return Tick{ticksPerBeat(meter).value() * meter.numerator};
}

BarBeat MeterMap::barBeatAt(Tick tick) const noexcept {
  if (tick < Tick{0}) {
    const auto meter = events_.front();
    const auto beatTicks = ticksPerBeat(meter).value();
    const auto barTicks = ticksPerBar(meter).value();
    const auto absolute = tick.value();
    const auto barsBack = ((-absolute) + barTicks - 1) / barTicks;
    const auto shifted = absolute + barsBack * barTicks;
    return BarBeat{1 - barsBack,
                   static_cast<std::int32_t>(shifted / beatTicks) + 1,
                   Tick{shifted % beatTicks}};
  }

  std::int64_t barBase = 1;
  for (std::size_t index = 0; index < events_.size(); ++index) {
    const auto& event = events_[index];
    const auto segmentEnd = index + 1 < events_.size() ? events_[index + 1].tick : tick;
    const auto effectiveEnd = tick < segmentEnd ? tick : segmentEnd;
    if (tick < event.tick) {
      break;
    }

    const auto barTicks = ticksPerBar(event).value();
    const auto beatTicks = ticksPerBeat(event).value();
    const auto segmentTicks = (effectiveEnd - event.tick).value();
    if (tick < segmentEnd || index + 1 >= events_.size()) {
      const auto barOffset = segmentTicks / barTicks;
      const auto inBar = segmentTicks % barTicks;
      return BarBeat{barBase + barOffset,
                     static_cast<std::int32_t>(inBar / beatTicks) + 1,
                     Tick{inBar % beatTicks}};
    }

    const auto fullSegmentTicks = (segmentEnd - event.tick).value();
    barBase += (fullSegmentTicks + barTicks - 1) / barTicks;
  }
  return BarBeat{};
}

}  // namespace seam::time
