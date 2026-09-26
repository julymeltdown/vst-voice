#pragma once

#include "seam/platform/audio_callback.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace seam::platform {

// What a meter shows for one reading: per-channel level and held peak (linear, 1.0 = 0 dBFS) and
// whether any sample reached full scale since the clip indicator was last reset.
struct OutputLevelReading final {
  std::vector<float> peak;
  std::vector<float> hold;
  bool clipped{false};
};

// Measures the buffers an audio callback hands to its device and turns them into meter readings.
//
// Threading: measure() runs on the audio thread and only touches atomics (no locks, no
// allocation). Each block raises a per-channel window peak with a compare-exchange maximum and
// latches the clip flag; read() on the UI thread takes those window peaks (exchange with zero),
// so a transient between two UI reads is never lost. Hold and decay are applied in read(), on
// the UI thread, from UI-only state. resetClip() may be called from any thread.
//
// read() returns nothing when the source is not running, when no block has arrived since it
// started, or when blocks stopped arriving (staleAfter), so a meter never shows an invented level.
class OutputLevelMeter final {
public:
  static constexpr std::size_t kMaxChannels = 8U;
  // A sample at or beyond full scale latches the clip flag.
  static constexpr float kClipThreshold = 1.0F;

  struct Ballistics final {
    std::chrono::milliseconds hold{1500};
    double decayDbPerSecond{24.0};
    std::chrono::milliseconds staleAfter{500};
  };

  OutputLevelMeter() = default;
  explicit OutputLevelMeter(Ballistics ballistics) : ballistics_(ballistics) {}
  OutputLevelMeter(const OutputLevelMeter&) = delete;
  OutputLevelMeter& operator=(const OutputLevelMeter&) = delete;

  // Audio thread.
  void measure(const AudioProcessContext& context) noexcept;
  void measure(std::span<const float* const> channels, std::size_t frames) noexcept;
  void measure(std::span<const double* const> channels, std::size_t frames) noexcept;

  // UI thread.
  [[nodiscard]] std::optional<OutputLevelReading> read(
      bool sourceRunning, std::chrono::steady_clock::time_point now);
  // Any thread.
  void resetClip() noexcept { clipped_.store(false, std::memory_order_relaxed); }
  [[nodiscard]] bool clipped() const noexcept {
    return clipped_.load(std::memory_order_relaxed);
  }

private:
  void raise(std::size_t channel, float peak) noexcept;
  void finishBlock(std::size_t channels, bool clipped) noexcept;
  void forget() noexcept;

  static_assert(std::atomic<float>::is_always_lock_free);
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
  std::array<std::atomic<float>, kMaxChannels> windowPeak_{};
  std::atomic<std::uint32_t> channels_{0U};
  std::atomic<std::uint64_t> blocks_{0U};
  std::atomic<bool> clipped_{false};

  // UI thread only.
  Ballistics ballistics_{};
  bool live_{false};
  std::size_t shownChannels_{0U};
  std::uint64_t seenBlocks_{0U};
  std::chrono::steady_clock::time_point lastBlockAt_{};
  std::chrono::steady_clock::time_point lastReadAt_{};
  std::array<float, kMaxChannels> level_{};
  std::array<float, kMaxChannels> hold_{};
  std::array<std::chrono::steady_clock::time_point, kMaxChannels> holdSince_{};
};

}  // namespace seam::platform
