#pragma once

#include <algorithm>
#include <chrono>

namespace seam::native_ui::design {

// A clock-driven, finite animation. The caller owns the clock and requests another frame only
// while running() is true; reduced motion resolves the tween immediately to its final value.
struct Tween final {
  using Clock = std::chrono::steady_clock;
  Clock::time_point started{};
  std::chrono::milliseconds duration{0};
  bool active{false};

  void start(Clock::time_point now, std::chrono::milliseconds length, bool reduceMotion) noexcept {
    started = now;
    duration = length;
    active = !reduceMotion && length.count() > 0;
  }
  void reset() noexcept { active = false; }
  [[nodiscard]] double progress(Clock::time_point now, bool reduceMotion) const noexcept {
    if (!active || reduceMotion || duration.count() <= 0) return 1.0;
    const auto elapsed = std::chrono::duration<double>(now - started).count();
    return std::clamp(elapsed / std::chrono::duration<double>(duration).count(), 0.0, 1.0);
  }
  [[nodiscard]] bool running(Clock::time_point now, bool reduceMotion) const noexcept {
    return active && !reduceMotion && now < started + duration;
  }
  [[nodiscard]] double eased(Clock::time_point now, bool reduceMotion) const noexcept {
    const auto p = progress(now, reduceMotion);
    return 1.0 - (1.0 - p) * (1.0 - p) * (1.0 - p);
  }
};

}  // namespace seam::native_ui::design
