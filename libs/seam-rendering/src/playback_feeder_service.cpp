#include "seam/rendering/playback_feeder_service.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace seam::rendering {

PlaybackFeederService::PlaybackFeederService(
    PlaybackFeeder& feeder, PlaybackFeederServiceConfig config)
    : feeder_(feeder), config_(config) {
  config_.targetBufferedFrames =
      std::max<std::size_t>(1U, config_.targetBufferedFrames);
  config_.activePollInterval =
      std::max(std::chrono::microseconds{50}, config_.activePollInterval);
  config_.idlePollInterval =
      std::max(std::chrono::milliseconds{1}, config_.idlePollInterval);
}

PlaybackFeederService::~PlaybackFeederService() { stop(); }

core::Result<void> PlaybackFeederService::start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true,
                                        std::memory_order_acq_rel)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Playback feeder service is already running");
  }
  try {
    worker_ = std::jthread(
        [this](std::stop_token stopToken) { run(stopToken); });
  } catch (...) {
    running_.store(false, std::memory_order_release);
    return core::failure(core::ErrorCode::Internal,
                         "Unable to create playback feeder thread");
  }
  return core::success();
}

void PlaybackFeederService::stop() noexcept {
  if (worker_.joinable()) {
    worker_.request_stop();
    notify();
    worker_.join();
  }
  running_.store(false, std::memory_order_release);
}

void PlaybackFeederService::notify() noexcept {
  wakeGeneration_.fetch_add(1U, std::memory_order_release);
  wakeSignals_.fetch_add(1U, std::memory_order_relaxed);
}

core::Result<void> PlaybackFeederService::setTimeline(
    std::shared_ptr<const PlaybackTimeline> timeline) {
  auto result = feeder_.setTimeline(std::move(timeline));
  if (result) notify();
  return result;
}

core::Result<void> PlaybackFeederService::setLoop(PlaybackLoop loop) {
  auto result = feeder_.setLoop(loop);
  if (result) notify();
  return result;
}

core::Result<void> PlaybackFeederService::setPlaying(bool playing) {
  auto result = feeder_.setPlaying(playing);
  if (result) notify();
  return result;
}

core::Result<void> PlaybackFeederService::seek(time::SampleFrame frame) {
  auto result = feeder_.seek(frame);
  if (result) notify();
  return result;
}

PlaybackFeederServiceStats PlaybackFeederService::stats() const noexcept {
  return PlaybackFeederServiceStats{
      .loopIterations = loopIterations_.load(std::memory_order_relaxed),
      .wakeSignals = wakeSignals_.load(std::memory_order_relaxed),
      .framesFed = framesFed_.load(std::memory_order_relaxed),
      .idleWaits = idleWaits_.load(std::memory_order_relaxed),
  };
}

void PlaybackFeederService::run(std::stop_token stopToken) noexcept {
  auto observedGeneration = wakeGeneration_.load(std::memory_order_acquire);
  while (!stopToken.stop_requested()) {
    loopIterations_.fetch_add(1U, std::memory_order_relaxed);
    const auto fed = feeder_.feedToWatermark(config_.targetBufferedFrames);
    framesFed_.fetch_add(fed, std::memory_order_relaxed);

    const auto nextGeneration =
        wakeGeneration_.load(std::memory_order_acquire);
    if (fed > 0U || nextGeneration != observedGeneration) {
      observedGeneration = nextGeneration;
      std::this_thread::sleep_for(config_.activePollInterval);
      continue;
    }

    idleWaits_.fetch_add(1U, std::memory_order_relaxed);
    const auto deadline = std::chrono::steady_clock::now() +
                          config_.idlePollInterval;
    while (!stopToken.stop_requested() &&
           wakeGeneration_.load(std::memory_order_acquire) ==
               observedGeneration &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::microseconds{200});
    }
    observedGeneration = wakeGeneration_.load(std::memory_order_acquire);
  }
  running_.store(false, std::memory_order_release);
}

}  // namespace seam::rendering
