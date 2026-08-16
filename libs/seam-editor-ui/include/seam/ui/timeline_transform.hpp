#pragma once

#include "seam/time/tick.hpp"

#include <algorithm>
#include <cstdint>

namespace seam::ui {

class TimelineTransform final {
public:
  TimelineTransform(time::Ppq ppq = time::kDefaultPpq,
                    double pixelsPerQuarter = 96.0,
                    time::Tick originTick = time::Tick{0}) noexcept;

  [[nodiscard]] double tickToPixel(time::Tick tick) const noexcept;
  [[nodiscard]] time::Tick pixelToTick(double pixel) const noexcept;
  [[nodiscard]] double durationToPixels(time::Tick duration) const noexcept;

  [[nodiscard]] double pixelsPerQuarter() const noexcept { return pixelsPerQuarter_; }
  [[nodiscard]] time::Tick originTick() const noexcept { return originTick_; }
  [[nodiscard]] time::Ppq ppq() const noexcept { return ppq_; }

  void setPixelsPerQuarter(double value) noexcept;
  void setOriginTick(time::Tick value) noexcept { originTick_ = value; }
  void panPixels(double deltaPixels) noexcept;
  void zoomAround(double anchorPixel, double factor) noexcept;

private:
  time::Ppq ppq_;
  double pixelsPerQuarter_;
  time::Tick originTick_;
};

class PitchTransform final {
public:
  PitchTransform(double rowHeight = 16.0, std::int32_t topMidiKey = 127) noexcept
      : rowHeight_(std::max(4.0, rowHeight)), topMidiKey_(std::clamp(topMidiKey, 0, 127)) {}

  [[nodiscard]] double midiToPixel(std::int32_t midiKey) const noexcept {
    return static_cast<double>(topMidiKey_ - midiKey) * rowHeight_;
  }
  [[nodiscard]] std::int32_t pixelToMidi(double pixel) const noexcept {
    return std::clamp(topMidiKey_ - static_cast<std::int32_t>(pixel / rowHeight_), 0, 127);
  }
  [[nodiscard]] double rowHeight() const noexcept { return rowHeight_; }
  [[nodiscard]] std::int32_t topMidiKey() const noexcept { return topMidiKey_; }
  void setRowHeight(double value) noexcept { rowHeight_ = std::max(4.0, value); }
  void setTopMidiKey(std::int32_t value) noexcept { topMidiKey_ = std::clamp(value, 0, 127); }

private:
  double rowHeight_;
  std::int32_t topMidiKey_;
};

}  // namespace seam::ui
