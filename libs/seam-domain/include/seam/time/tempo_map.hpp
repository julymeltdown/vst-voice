#pragma once

#include "seam/core/result.hpp"
#include "seam/time/tick.hpp"

#include <vector>

namespace seam::time {

struct TempoEvent final {
  Tick tick;
  double bpm{120.0};

  friend bool operator==(const TempoEvent&, const TempoEvent&) = default;
};

class TempoMap final {
public:
  explicit TempoMap(Ppq ppq = kDefaultPpq);

  [[nodiscard]] Ppq ppq() const noexcept { return ppq_; }
  [[nodiscard]] const std::vector<TempoEvent>& events() const noexcept { return events_; }

  [[nodiscard]] core::Result<void> addOrReplace(Tick tick, double bpm);
  [[nodiscard]] core::Result<void> remove(Tick tick);

  [[nodiscard]] double secondsAt(Tick tick) const noexcept;
  [[nodiscard]] Tick tickAtSeconds(double seconds) const noexcept;
  [[nodiscard]] SampleFrame sampleFrameAt(Tick tick, double sampleRate) const noexcept;
  [[nodiscard]] Tick tickAtSampleFrame(SampleFrame frame, double sampleRate) const noexcept;
  [[nodiscard]] double bpmAt(Tick tick) const noexcept;

  friend bool operator==(const TempoMap&, const TempoMap&) = default;

private:
  [[nodiscard]] std::size_t eventIndexAt(Tick tick) const noexcept;
  [[nodiscard]] double secondsPerTick(double bpm) const noexcept;

  Ppq ppq_;
  std::vector<TempoEvent> events_;
};

}  // namespace seam::time
