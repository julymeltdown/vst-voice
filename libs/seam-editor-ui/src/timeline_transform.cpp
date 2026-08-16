#include "seam/ui/timeline_transform.hpp"

#include <cmath>

namespace seam::ui {

TimelineTransform::TimelineTransform(time::Ppq ppq,
                                     double pixelsPerQuarter,
                                     time::Tick originTick) noexcept
    : ppq_(ppq > 0 ? ppq : time::kDefaultPpq),
      pixelsPerQuarter_(std::clamp(pixelsPerQuarter, 8.0, 4096.0)),
      originTick_(originTick) {}

double TimelineTransform::tickToPixel(time::Tick tick) const noexcept {
  return static_cast<double>((tick - originTick_).value()) * pixelsPerQuarter_ /
         static_cast<double>(ppq_);
}

time::Tick TimelineTransform::pixelToTick(double pixel) const noexcept {
  const auto offset = static_cast<std::int64_t>(std::llround(
      pixel * static_cast<double>(ppq_) / pixelsPerQuarter_));
  return originTick_ + time::Tick{offset};
}

double TimelineTransform::durationToPixels(time::Tick duration) const noexcept {
  return static_cast<double>(duration.value()) * pixelsPerQuarter_ /
         static_cast<double>(ppq_);
}

void TimelineTransform::setPixelsPerQuarter(double value) noexcept {
  pixelsPerQuarter_ = std::clamp(value, 8.0, 4096.0);
}

void TimelineTransform::panPixels(double deltaPixels) noexcept {
  originTick_ = pixelToTick(-deltaPixels);
}

void TimelineTransform::zoomAround(double anchorPixel, double factor) noexcept {
  if (!std::isfinite(factor) || factor <= 0.0) {
    return;
  }
  const auto anchorTick = pixelToTick(anchorPixel);
  setPixelsPerQuarter(pixelsPerQuarter_ * factor);
  const auto newAnchorTick = pixelToTick(anchorPixel);
  originTick_ += anchorTick - newAnchorTick;
}

}  // namespace seam::ui
