#include "seam/rendering/multichannel_playback.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace seam::rendering {

MultichannelPlaybackFeeder::ControlQueue::ControlQueue(std::size_t capacity)
    : slots_(std::max<std::size_t>(2U, capacity) + 1U) {}

bool MultichannelPlaybackFeeder::ControlQueue::push(
    ControlCommand command) noexcept {
  const auto write = writeIndex_.load(std::memory_order_relaxed);
  const auto next = (write + 1U) % slots_.size();
  if (next == readIndex_.load(std::memory_order_acquire)) return false;
  slots_[write].emplace(std::move(command));
  writeIndex_.store(next, std::memory_order_release);
  return true;
}

std::optional<MultichannelPlaybackFeeder::ControlCommand>
MultichannelPlaybackFeeder::ControlQueue::pop() noexcept {
  const auto read = readIndex_.load(std::memory_order_relaxed);
  if (read == writeIndex_.load(std::memory_order_acquire)) return std::nullopt;
  auto command = std::move(slots_[read]);
  slots_[read].reset();
  readIndex_.store((read + 1U) % slots_.size(), std::memory_order_release);
  return command;
}

MultichannelPlaybackFeeder::MultichannelPlaybackFeeder(
    SpscInterleavedAudioRingBuffer& ring, std::uint32_t sampleRate,
    std::uint8_t outputChannels, std::size_t blockFrames,
    std::size_t controlQueueCapacity)
    : ring_(ring),
      sampleRate_(sampleRate),
      outputChannels_(outputChannels),
      blockFrames_(std::max<std::size_t>(1U, blockFrames)),
      scratch_(blockFrames_ * outputChannels_, 0.0F),
      controls_(controlQueueCapacity) {}

core::Result<void> MultichannelPlaybackFeeder::enqueue(
    ControlCommand command) {
  if (!controls_.push(std::move(command))) {
    stats_.rejectedCommands.fetch_add(1U, std::memory_order_relaxed);
    return core::failure(core::ErrorCode::Conflict,
                         "Multichannel playback control queue is full");
  }
  return core::success();
}

core::Result<void> MultichannelPlaybackFeeder::setTimeline(
    std::shared_ptr<const RoutedPlaybackTimeline> timeline) {
  if (timeline == nullptr || timeline->sampleRate() != sampleRate_ ||
      timeline->outputChannels() != outputChannels_) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Routed timeline does not match feeder format");
  }
  return enqueue(ControlCommand{.kind = ControlKind::Timeline,
                                .timeline = std::move(timeline),
                                .loop = {},
                                .frame = 0,
                                .playing = false});
}

core::Result<void> MultichannelPlaybackFeeder::setLoop(PlaybackLoop loop) {
  if (loop.enabled && loop.endFrame <= loop.startFrame) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Playback loop end must be after loop start");
  }
  return enqueue(ControlCommand{.kind = ControlKind::Loop,
                                .timeline = {},
                                .loop = loop,
                                .frame = 0,
                                .playing = false});
}

core::Result<void> MultichannelPlaybackFeeder::setPlaying(bool playing) {
  return enqueue(ControlCommand{.kind = ControlKind::Playing,
                                .timeline = {},
                                .loop = {},
                                .frame = 0,
                                .playing = playing});
}

core::Result<void> MultichannelPlaybackFeeder::seek(time::SampleFrame frame) {
  return enqueue(ControlCommand{.kind = ControlKind::Seek,
                                .timeline = {},
                                .loop = {},
                                .frame = frame,
                                .playing = false});
}

bool MultichannelPlaybackFeeder::playing() const noexcept {
  return publishedPlaying_.load(std::memory_order_acquire);
}

time::SampleFrame MultichannelPlaybackFeeder::playhead() const noexcept {
  return publishedPlayhead_.load(std::memory_order_acquire);
}

void MultichannelPlaybackFeeder::publishState() noexcept {
  publishedPlayhead_.store(playhead_, std::memory_order_release);
  publishedPlaying_.store(playing_, std::memory_order_release);
}

bool MultichannelPlaybackFeeder::processControls() noexcept {
  bool resetNeeded = false;
  while (auto command = controls_.pop()) {
    stats_.controlCommands.fetch_add(1U, std::memory_order_relaxed);
    switch (command->kind) {
      case ControlKind::Timeline: {
        timeline_ = std::move(command->timeline);
        const auto prepared = workspace_.prepare(timeline_->routing(), blockFrames_);
        if (!prepared) {
          timeline_.reset();
          stats_.mixFailures.fetch_add(1U, std::memory_order_relaxed);
        }
        resetNeeded = true;
        break;
      }
      case ControlKind::Loop:
        loop_ = command->loop;
        if (loop_.enabled && playhead_ >= loop_.endFrame) {
          playhead_ = loop_.startFrame;
        }
        resetNeeded = true;
        break;
      case ControlKind::Playing:
        if (playing_ != command->playing) resetNeeded = true;
        playing_ = command->playing;
        break;
      case ControlKind::Seek:
        playhead_ = command->frame;
        stats_.seeks.fetch_add(1U, std::memory_order_relaxed);
        resetNeeded = true;
        break;
    }
  }
  if (resetNeeded && ring_.availableReadFrames() > 0U) {
    pendingResetEpoch_ = ring_.requestConsumerReset();
    stats_.resetRequests.fetch_add(1U, std::memory_order_relaxed);
  }
  publishState();
  if (pendingResetEpoch_ != 0U) {
    if (!ring_.resetAcknowledged(pendingResetEpoch_)) {
      stats_.resetWaits.fetch_add(1U, std::memory_order_relaxed);
      return false;
    }
    pendingResetEpoch_ = 0U;
  }
  return true;
}

bool MultichannelPlaybackFeeder::mixWithLoop(
    std::span<float> output, std::size_t frameCount) noexcept {
  std::fill(output.begin(), output.end(), 0.0F);
  if (timeline_ == nullptr) return true;
  std::size_t mixedFrames = 0U;
  while (mixedFrames < frameCount) {
    if (loop_.enabled && playhead_ >= loop_.endFrame) {
      playhead_ = loop_.startFrame;
      stats_.loopWraps.fetch_add(1U, std::memory_order_relaxed);
    }
    if (!loop_.enabled && playhead_ >= timeline_->endFrame()) {
      playing_ = false;
      break;
    }

    auto chunk = frameCount - mixedFrames;
    if (loop_.enabled) {
      const auto remaining = loop_.endFrame - playhead_;
      if (remaining <= 0) continue;
      chunk = std::min(chunk, static_cast<std::size_t>(remaining));
    } else {
      const auto remaining = timeline_->endFrame() - playhead_;
      if (remaining <= 0) {
        playing_ = false;
        break;
      }
      chunk = std::min(chunk, static_cast<std::size_t>(remaining));
    }

    auto destination = output.subspan(mixedFrames * outputChannels_,
                                      chunk * outputChannels_);
    const auto mixed = timeline_->mix(playhead_, chunk, destination, workspace_);
    if (!mixed) return false;
    playhead_ += static_cast<time::SampleFrame>(chunk);
    mixedFrames += chunk;
  }
  return true;
}

std::size_t MultichannelPlaybackFeeder::feedOnce() noexcept {
  stats_.feedCalls.fetch_add(1U, std::memory_order_relaxed);
  if (!processControls() || pendingResetEpoch_ != 0U || !playing_ ||
      timeline_ == nullptr) {
    return 0U;
  }
  const auto writable = ring_.availableWriteFrames();
  if (writable == 0U) {
    stats_.ringFullEvents.fetch_add(1U, std::memory_order_relaxed);
    return 0U;
  }
  const auto frames = std::min(blockFrames_, writable);
  auto output = std::span<float>{scratch_}.first(frames * outputChannels_);
  if (!mixWithLoop(output, frames)) {
    stats_.mixFailures.fetch_add(1U, std::memory_order_relaxed);
    return 0U;
  }
  stats_.framesMixed.fetch_add(frames, std::memory_order_relaxed);
  const auto written = ring_.writeFrames(output);
  stats_.framesWritten.fetch_add(written, std::memory_order_relaxed);
  publishState();
  return written;
}

std::size_t MultichannelPlaybackFeeder::feedToWatermark(
    std::size_t targetFrames) noexcept {
  std::size_t total = 0U;
  const auto clamped = std::min(targetFrames, ring_.capacityFrames());
  while (ring_.availableReadFrames() < clamped) {
    const auto written = feedOnce();
    if (written == 0U) break;
    total += written;
  }
  return total;
}

MultichannelFeederStats MultichannelPlaybackFeeder::stats() const noexcept {
  return MultichannelFeederStats{
      .feedCalls = stats_.feedCalls.load(std::memory_order_relaxed),
      .framesMixed = stats_.framesMixed.load(std::memory_order_relaxed),
      .framesWritten = stats_.framesWritten.load(std::memory_order_relaxed),
      .ringFullEvents = stats_.ringFullEvents.load(std::memory_order_relaxed),
      .loopWraps = stats_.loopWraps.load(std::memory_order_relaxed),
      .seeks = stats_.seeks.load(std::memory_order_relaxed),
      .controlCommands = stats_.controlCommands.load(std::memory_order_relaxed),
      .resetRequests = stats_.resetRequests.load(std::memory_order_relaxed),
      .resetWaits = stats_.resetWaits.load(std::memory_order_relaxed),
      .rejectedCommands = stats_.rejectedCommands.load(std::memory_order_relaxed),
      .mixFailures = stats_.mixFailures.load(std::memory_order_relaxed),
  };
}

MultichannelPlaybackFeederService::MultichannelPlaybackFeederService(
    MultichannelPlaybackFeeder& feeder, std::size_t watermarkFrames)
    : feeder_(feeder), watermarkFrames_(std::max<std::size_t>(1U, watermarkFrames)) {}

MultichannelPlaybackFeederService::~MultichannelPlaybackFeederService() {
  stop();
}

core::Result<void> MultichannelPlaybackFeederService::start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true,
                                        std::memory_order_acq_rel)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Multichannel feeder service is already running");
  }
  try {
    worker_ = std::jthread([this](std::stop_token stopToken) {
      while (!stopToken.stop_requested()) {
        const auto fed = feeder_.feedToWatermark(watermarkFrames_);
        // Control commands must still be consumed while the ring is already
        // above the watermark. Otherwise pause/seek/stop can remain queued
        // indefinitely until the audio callback drains more data.
        if (fed == 0U) {
          static_cast<void>(feeder_.feedOnce());
          std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
      }
      running_.store(false, std::memory_order_release);
    });
  } catch (...) {
    running_.store(false, std::memory_order_release);
    return core::failure(core::ErrorCode::Internal,
                         "Unable to start multichannel feeder thread");
  }
  return core::success();
}

void MultichannelPlaybackFeederService::stop() noexcept {
  if (worker_.joinable()) {
    worker_.request_stop();
    worker_.join();
  }
  running_.store(false, std::memory_order_release);
}

bool MultichannelPlaybackFeederService::running() const noexcept {
  return running_.load(std::memory_order_acquire);
}

}  // namespace seam::rendering
