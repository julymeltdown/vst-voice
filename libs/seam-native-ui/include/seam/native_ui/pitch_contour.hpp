#pragma once
#include "seam/voicebank/pitch.hpp"
#include <cmath>

namespace seam::native_ui {
struct PitchContourPoint final {
  double sourceFrame{0.0}, centsFromTarget{0.0};
  bool connectedToPrevious{false};
};
// Window-center estimates, not sample-accurate acoustic boundaries. Invalid or
// unvoiced windows break the line; missing windows must never be bridged.
inline std::vector<PitchContourPoint> buildPitchContour(
    std::span<const voicebank::PitchFrame> frames, const voicebank::PitchConfig& config,
    std::int32_t targetMidi) {
  std::vector<PitchContourPoint> points;
  if (targetMidi < 0 || targetMidi > 127 || config.frameSize == 0U || config.hopSize == 0U) return points;
  points.reserve(frames.size());
  bool previousVoiced = false;
  std::size_t previousFrame = 0U;
  const auto target = 440.0 * std::pow(2.0, (static_cast<double>(targetMidi) - 69.0) / 12.0);
  for (const auto& frame : frames) {
    if (!frame.voiced || !std::isfinite(frame.f0Hz) || frame.f0Hz <= 0.0) { previousVoiced = false; continue; }
    const auto connected = previousVoiced && frame.sourceFrame > previousFrame && frame.sourceFrame - previousFrame == config.hopSize;
    const auto cents = 1200.0 * std::log2(frame.f0Hz / target);
    if (!std::isfinite(cents)) { previousVoiced = false; continue; }
    points.push_back({static_cast<double>(frame.sourceFrame) + static_cast<double>(config.frameSize) / 2.0, cents, connected});
    previousFrame = frame.sourceFrame;
    previousVoiced = true;
  }
  return points;
}
}
