#include "seam/time/tempo_map.hpp"

#include <algorithm>
#include <cmath>

namespace seam::time {

TempoMap::TempoMap(Ppq ppq) : ppq_(ppq > 0 ? ppq : kDefaultPpq), events_{{Tick{0}, 120.0}} {}

core::Result<void> TempoMap::addOrReplace(Tick tick, double bpm) {
  if (tick < Tick{0}) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Tempo event tick must not be negative");
  }
  if (!std::isfinite(bpm) || bpm <= 0.0 || bpm > 1000.0) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Tempo BPM must be finite and in the range (0, 1000]");
  }

  const auto iterator = std::lower_bound(events_.begin(), events_.end(), tick,
      [](const TempoEvent& event, Tick value) { return event.tick < value; });
  if (iterator != events_.end() && iterator->tick == tick) {
    iterator->bpm = bpm;
  } else {
    events_.insert(iterator, TempoEvent{tick, bpm});
  }
  return core::success();
}

core::Result<void> TempoMap::remove(Tick tick) {
  if (tick == Tick{0}) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "The initial tempo event cannot be removed");
  }
  const auto iterator = std::lower_bound(events_.begin(), events_.end(), tick,
      [](const TempoEvent& event, Tick value) { return event.tick < value; });
  if (iterator == events_.end() || iterator->tick != tick) {
    return core::failure(core::ErrorCode::NotFound, "Tempo event was not found");
  }
  events_.erase(iterator);
  return core::success();
}

std::size_t TempoMap::eventIndexAt(Tick tick) const noexcept {
  const auto iterator = std::upper_bound(events_.begin(), events_.end(), tick,
      [](Tick value, const TempoEvent& event) { return value < event.tick; });
  if (iterator == events_.begin()) {
    return 0;
  }
  return static_cast<std::size_t>(std::distance(events_.begin(), iterator - 1));
}

double TempoMap::secondsPerTick(double bpm) const noexcept {
  return 60.0 / (bpm * static_cast<double>(ppq_));
}

double TempoMap::secondsAt(Tick tick) const noexcept {
  if (tick <= Tick{0}) {
    return static_cast<double>(tick.value()) * secondsPerTick(events_.front().bpm);
  }

  double seconds = 0.0;
  for (std::size_t index = 0; index < events_.size(); ++index) {
    const auto segmentStart = events_[index].tick;
    const auto segmentEnd = index + 1 < events_.size() ? events_[index + 1].tick : tick;
    if (tick <= segmentStart) {
      break;
    }
    const auto effectiveEnd = tick < segmentEnd ? tick : segmentEnd;
    const auto ticks = effectiveEnd - segmentStart;
    seconds += static_cast<double>(ticks.value()) * secondsPerTick(events_[index].bpm);
    if (tick <= segmentEnd) {
      break;
    }
  }
  return seconds;
}

Tick TempoMap::tickAtSeconds(double seconds) const noexcept {
  if (!std::isfinite(seconds)) {
    return Tick{0};
  }
  if (seconds <= 0.0) {
    return Tick{static_cast<std::int64_t>(
        std::llround(seconds / secondsPerTick(events_.front().bpm)))};
  }

  double elapsed = 0.0;
  for (std::size_t index = 0; index < events_.size(); ++index) {
    const auto start = events_[index].tick;
    if (index + 1 >= events_.size()) {
      const auto extraTicks = static_cast<std::int64_t>(std::llround(
          (seconds - elapsed) / secondsPerTick(events_[index].bpm)));
      return start + Tick{extraTicks};
    }

    const auto end = events_[index + 1].tick;
    const auto segmentSeconds = static_cast<double>((end - start).value()) *
                                secondsPerTick(events_[index].bpm);
    if (seconds <= elapsed + segmentSeconds) {
      const auto extraTicks = static_cast<std::int64_t>(std::llround(
          (seconds - elapsed) / secondsPerTick(events_[index].bpm)));
      return start + Tick{extraTicks};
    }
    elapsed += segmentSeconds;
  }
  return Tick{0};
}

SampleFrame TempoMap::sampleFrameAt(Tick tick, double sampleRate) const noexcept {
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0) {
    return 0;
  }
  return static_cast<SampleFrame>(std::llround(secondsAt(tick) * sampleRate));
}

Tick TempoMap::tickAtSampleFrame(SampleFrame frame, double sampleRate) const noexcept {
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0) {
    return Tick{0};
  }
  return tickAtSeconds(static_cast<double>(frame) / sampleRate);
}

double TempoMap::bpmAt(Tick tick) const noexcept {
  return events_[eventIndexAt(tick)].bpm;
}

}  // namespace seam::time
