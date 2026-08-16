#pragma once

#include "seam/core/result.hpp"
#include "seam/rendering/playback_engine.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <thread>

namespace seam::rendering {

struct PlaybackFeederServiceConfig final {
  std::size_t targetBufferedFrames{8192U};
  std::chrono::microseconds activePollInterval{500};
  std::chrono::milliseconds idlePollInterval{4};
};

struct PlaybackFeederServiceStats final {
  std::uint64_t loopIterations{0};
  std::uint64_t wakeSignals{0};
  std::uint64_t framesFed{0};
  std::uint64_t idleWaits{0};
};

// Owns the only thread that may call PlaybackFeeder::feedOnce/feedToWatermark.
// UI callers use the proxy control methods below, which enqueue immutable
// commands into PlaybackFeeder and wake this service. The audio callback remains
// the sole consumer of the SPSC ring buffer.
class PlaybackFeederService final {
public:
  PlaybackFeederService(PlaybackFeeder& feeder,
                        PlaybackFeederServiceConfig config = {});
  ~PlaybackFeederService();

  PlaybackFeederService(const PlaybackFeederService&) = delete;
  PlaybackFeederService& operator=(const PlaybackFeederService&) = delete;

  [[nodiscard]] core::Result<void> start();
  void stop() noexcept;
  void notify() noexcept;

  [[nodiscard]] bool running() const noexcept {
    return running_.load(std::memory_order_acquire);
  }

  [[nodiscard]] core::Result<void> setTimeline(
      std::shared_ptr<const PlaybackTimeline> timeline);
  [[nodiscard]] core::Result<void> setLoop(PlaybackLoop loop);
  [[nodiscard]] core::Result<void> setPlaying(bool playing);
  [[nodiscard]] core::Result<void> seek(time::SampleFrame frame);

  [[nodiscard]] PlaybackFeederServiceStats stats() const noexcept;

private:
  void run(std::stop_token stopToken) noexcept;

  PlaybackFeeder& feeder_;
  PlaybackFeederServiceConfig config_;
  std::jthread worker_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> wakeGeneration_{0};
  std::atomic<std::uint64_t> loopIterations_{0};
  std::atomic<std::uint64_t> wakeSignals_{0};
  std::atomic<std::uint64_t> framesFed_{0};
  std::atomic<std::uint64_t> idleWaits_{0};
};

}  // namespace seam::rendering
