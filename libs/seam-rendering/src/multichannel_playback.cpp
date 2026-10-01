#include "seam/rendering/multichannel_playback.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <thread>

namespace seam::rendering {

time::SampleFrame rewoundPlayhead(time::SampleFrame playhead, std::size_t frames,
                                  const PlaybackLoop& loop) noexcept {
  constexpr auto kMostFrames = std::numeric_limits<time::SampleFrame>::max();
  const auto back = static_cast<time::SampleFrame>(
      std::min<std::uint64_t>(frames, static_cast<std::uint64_t>(kMostFrames)));
  const bool inLoop = loop.enabled && loop.endFrame > loop.startFrame && playhead >= loop.startFrame &&
                      playhead <= loop.endFrame;
  if (!inLoop) return std::max<time::SampleFrame>(0, playhead - back);
  // The loop plays from its start to its end over and over, so stepping back past its start lands
  // at its end, and a step of whole loops lands where it began.
  const auto length = loop.endFrame - loop.startFrame;
  const auto intoLoop = playhead - loop.startFrame;
  if (back <= intoLoop) return playhead - back;
  const auto beyond = (back - intoLoop) % length;
  return beyond == 0 ? loop.startFrame : loop.endFrame - beyond;
}

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

bool MultichannelPlaybackFeeder::ControlQueue::pushAll(
    std::span<ControlCommand> commands) noexcept {
  const auto size = slots_.size();
  const auto write = writeIndex_.load(std::memory_order_relaxed);
  const auto read = readIndex_.load(std::memory_order_acquire);
  // The consumer only moves the read index forward, so what is free now is at least this much.
  const auto used = (write + size - read) % size;
  const auto free = (size - 1U) - used;
  if (commands.size() > free) return false;
  for (std::size_t index = 0U; index < commands.size(); ++index) {
    slots_[(write + index) % size].emplace(std::move(commands[index]));
  }
  // One store publishes the whole group: the consumer sees all of it or none of it.
  writeIndex_.store((write + commands.size()) % size, std::memory_order_release);
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

MultichannelPlaybackFeeder::ControlScript&
MultichannelPlaybackFeeder::ControlScript::timeline(
    std::shared_ptr<const RoutedPlaybackTimeline> value) {
  commands_.push_back(ControlCommand{.kind = ControlKind::Timeline,
                                     .timeline = std::move(value),
                                     .loop = {},
                                     .frame = 0,
                                     .playing = false});
  return *this;
}

MultichannelPlaybackFeeder::ControlScript&
MultichannelPlaybackFeeder::ControlScript::loop(PlaybackLoop value) {
  commands_.push_back(ControlCommand{.kind = ControlKind::Loop,
                                     .timeline = {},
                                     .loop = value,
                                     .frame = 0,
                                     .playing = false});
  return *this;
}

MultichannelPlaybackFeeder::ControlScript&
MultichannelPlaybackFeeder::ControlScript::playing(bool value) {
  commands_.push_back(ControlCommand{.kind = ControlKind::Playing,
                                     .timeline = {},
                                     .loop = {},
                                     .frame = 0,
                                     .playing = value});
  return *this;
}

MultichannelPlaybackFeeder::ControlScript&
MultichannelPlaybackFeeder::ControlScript::seek(time::SampleFrame frame) {
  commands_.push_back(ControlCommand{.kind = ControlKind::Seek,
                                     .timeline = {},
                                     .loop = {},
                                     .frame = frame,
                                     .playing = false});
  return *this;
}

bool MultichannelPlaybackFeeder::ControlScript::seeks() const noexcept {
  return std::any_of(commands_.begin(), commands_.end(),
                     [](const ControlCommand& command) { return command.kind == ControlKind::Seek; });
}

// What consuming each command does to the playing flag and the playhead. processControls() below
// does the same to the feeder's own state, and the multichannel tests check that the two agree.
PlaybackPoint MultichannelPlaybackFeeder::ControlScript::projectedFrom(
    PlaybackPoint from) const noexcept {
  for (const auto& command : commands_) {
    switch (command.kind) {
      case ControlKind::Timeline:
        if (command.timeline != nullptr &&
            from.playhead > command.timeline->endFrame()) {
          from.playhead = command.timeline->endFrame();
        }
        break;
      case ControlKind::Loop:
        if (command.loop.enabled && from.playhead >= command.loop.endFrame) {
          from.playhead = command.loop.startFrame;
        }
        break;
      case ControlKind::Playing:
        from.playing = command.playing;
        break;
      case ControlKind::Seek:
        from.playhead = command.frame;
        break;
    }
  }
  return from;
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

core::Result<void> MultichannelPlaybackFeeder::validate(
    const ControlCommand& command) const {
  switch (command.kind) {
    case ControlKind::Timeline:
      if (command.timeline == nullptr ||
          command.timeline->sampleRate() != sampleRate_ ||
          command.timeline->outputChannels() != outputChannels_) {
        return core::failure(core::ErrorCode::InvalidArgument,
                             "Routed timeline does not match feeder format");
      }
      break;
    case ControlKind::Loop:
      if (command.loop.enabled && command.loop.endFrame <= command.loop.startFrame) {
        return core::failure(core::ErrorCode::InvalidArgument,
                             "Playback loop end must be after loop start");
      }
      break;
    case ControlKind::Playing:
    case ControlKind::Seek:
      break;
  }
  return core::success();
}

core::Result<void> MultichannelPlaybackFeeder::enqueue(
    ControlCommand command) {
  const auto valid = validate(command);
  if (!valid) return valid;
  if (!controls_.push(std::move(command))) {
    stats_.rejectedCommands.fetch_add(1U, std::memory_order_relaxed);
    return core::failure(core::ErrorCode::Conflict,
                         "Multichannel playback control queue is full");
  }
  return core::success();
}

core::Result<void> MultichannelPlaybackFeeder::setTimeline(
    std::shared_ptr<const RoutedPlaybackTimeline> timeline) {
  return enqueue(ControlCommand{.kind = ControlKind::Timeline,
                                .timeline = std::move(timeline),
                                .loop = {},
                                .frame = 0,
                                .playing = false});
}

core::Result<void> MultichannelPlaybackFeeder::setLoop(PlaybackLoop loop) {
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

core::Result<void> MultichannelPlaybackFeeder::apply(ControlScript script) {
  auto& commands = script.commands_;
  if (commands.empty()) return core::success();
  for (const auto& command : commands) {
    const auto valid = validate(command);
    if (!valid) return valid;
  }
  if (commands.size() > controls_.capacity()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Multichannel playback control script is larger than the control queue");
  }
  if (!controls_.pushAll(commands)) {
    stats_.rejectedCommands.fetch_add(commands.size(), std::memory_order_relaxed);
    return core::failure(core::ErrorCode::Conflict,
                         "Multichannel playback control queue is full");
  }
  return core::success();
}

std::uint64_t MultichannelPlaybackFeeder::acknowledgedCommands() const noexcept {
  return acknowledgedCommands_.load(std::memory_order_acquire);
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
    ++consumedCommands_;
    switch (command->kind) {
      case ControlKind::Timeline: {
        timeline_ = std::move(command->timeline);
        const auto prepared = workspace_.prepare(timeline_->routing(), blockFrames_);
        if (!prepared) {
          timeline_.reset();
          stats_.mixFailures.fetch_add(1U, std::memory_order_relaxed);
        } else if (playhead_ > timeline_->endFrame()) {
          playhead_ = timeline_->endFrame();
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
  // After the state: whoever sees this count sees a playing flag and a playhead that include every
  // command it covers.
  acknowledgedCommands_.store(consumedCommands_, std::memory_order_release);
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
    std::span<float> output, std::size_t frameCount, std::size_t& mixedFrames) noexcept {
  std::fill(output.begin(), output.end(), 0.0F);
  mixedFrames = 0U;
  if (timeline_ == nullptr) return true;
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
  std::size_t mixed = 0U;
  if (!mixWithLoop(output, frames, mixed)) {
    stats_.mixFailures.fetch_add(1U, std::memory_order_relaxed);
    return 0U;
  }
  stats_.framesMixed.fetch_add(mixed, std::memory_order_relaxed);
  // Only what was mixed goes to the ring. A silent tail that padded the last block of the audio to
  // a whole block would put frames in the ring that are not part of the audio, and the ring would
  // then hold more than the playhead accounts for.
  const auto written = ring_.writeFrames(output.first(mixed * outputChannels_));
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
