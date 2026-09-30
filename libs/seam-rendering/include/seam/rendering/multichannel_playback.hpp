#pragma once

#include "seam/core/result.hpp"
#include "seam/rendering/interleaved_audio_ring_buffer.hpp"
#include "seam/rendering/multichannel_routing.hpp"
#include "seam/rendering/playback_engine.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>
#include <vector>

namespace seam::rendering {

// Whether a feeder is playing and where its playhead is: the two things a control command can
// change, and the two things playing() and playhead() report.
struct PlaybackPoint final {
  bool playing{false};
  time::SampleFrame playhead{0};
};

struct MultichannelFeederStats final {
  std::uint64_t feedCalls{0U};
  std::uint64_t framesMixed{0U};
  std::uint64_t framesWritten{0U};
  std::uint64_t ringFullEvents{0U};
  std::uint64_t loopWraps{0U};
  std::uint64_t seeks{0U};
  std::uint64_t controlCommands{0U};
  std::uint64_t resetRequests{0U};
  std::uint64_t resetWaits{0U};
  std::uint64_t rejectedCommands{0U};
  std::uint64_t mixFailures{0U};
};

class MultichannelPlaybackFeeder final {
  enum class ControlKind : std::uint8_t { Timeline, Loop, Playing, Seek };
  struct ControlCommand final {
    ControlKind kind{ControlKind::Playing};
    std::shared_ptr<const RoutedPlaybackTimeline> timeline;
    PlaybackLoop loop;
    time::SampleFrame frame{0};
    bool playing{false};
  };

public:
  // A group of control commands that reach the feeder together, in the order they were added, or
  // not at all. The consumer sees the whole group in one step: it can never mix audio with a
  // timeline that has arrived without the loop and the playhead that go with it, and a group that
  // does not fit leaves the queue exactly as it was.
  class ControlScript final {
  public:
    ControlScript& timeline(std::shared_ptr<const RoutedPlaybackTimeline> value);
    ControlScript& loop(PlaybackLoop value);
    ControlScript& playing(bool value);
    ControlScript& seek(time::SampleFrame frame);
    [[nodiscard]] std::size_t size() const noexcept { return commands_.size(); }
    // Whether the feeder is playing and where it is once it has consumed the script, if it is at
    // `from` when it starts. Playback that goes on meanwhile is not part of it. The rules are the
    // ones the feeder applies to each command when it consumes it; the two are kept in step by the
    // multichannel tests, which compare them on the same sequences of commands.
    [[nodiscard]] PlaybackPoint projectedFrom(PlaybackPoint from) const noexcept;

  private:
    friend class MultichannelPlaybackFeeder;
    std::vector<ControlCommand> commands_;
  };

  MultichannelPlaybackFeeder(SpscInterleavedAudioRingBuffer& ring,
                             std::uint32_t sampleRate,
                             std::uint8_t outputChannels,
                             std::size_t blockFrames = 1024U,
                             std::size_t controlQueueCapacity = 64U);

  [[nodiscard]] core::Result<void> setTimeline(
      std::shared_ptr<const RoutedPlaybackTimeline> timeline);
  [[nodiscard]] core::Result<void> setLoop(PlaybackLoop loop);
  [[nodiscard]] core::Result<void> setPlaying(bool playing);
  [[nodiscard]] core::Result<void> seek(time::SampleFrame frame);
  // Validates every command in the script the way setTimeline, setLoop, setPlaying and seek do,
  // and queues them all, or none. A script that is invalid, or larger than the queue could ever
  // hold, is refused as InvalidArgument and counted nowhere; one that does not fit in what is left
  // of the queue is refused as Conflict, and each of its commands is counted in rejectedCommands,
  // as a single command that finds the queue full is. Either way the queue is left as it was.
  [[nodiscard]] core::Result<void> apply(ControlScript script);
  // The number of control commands the feeder has consumed and whose effect playing() and
  // playhead() already show. Commands are consumed in the order they were queued, so a caller that
  // counts what it queued knows, from this, which of its commands the published state includes.
  [[nodiscard]] std::uint64_t acknowledgedCommands() const noexcept;
  [[nodiscard]] bool playing() const noexcept;
  [[nodiscard]] time::SampleFrame playhead() const noexcept;
  [[nodiscard]] std::size_t feedOnce() noexcept;
  [[nodiscard]] std::size_t feedToWatermark(std::size_t targetFrames) noexcept;
  [[nodiscard]] MultichannelFeederStats stats() const noexcept;

private:
  class ControlQueue final {
  public:
    explicit ControlQueue(std::size_t capacity);
    [[nodiscard]] bool push(ControlCommand command) noexcept;
    // All of the commands or none: the consumer sees them with a single store.
    [[nodiscard]] bool pushAll(std::span<ControlCommand> commands) noexcept;
    [[nodiscard]] std::optional<ControlCommand> pop() noexcept;
    // The most commands the queue can hold at once: one slot always stays empty.
    [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size() - 1U; }
  private:
    std::vector<std::optional<ControlCommand>> slots_;
    alignas(64) std::atomic<std::size_t> readIndex_{0U};
    alignas(64) std::atomic<std::size_t> writeIndex_{0U};
  };
  struct AtomicStats final {
    std::atomic<std::uint64_t> feedCalls{0U};
    std::atomic<std::uint64_t> framesMixed{0U};
    std::atomic<std::uint64_t> framesWritten{0U};
    std::atomic<std::uint64_t> ringFullEvents{0U};
    std::atomic<std::uint64_t> loopWraps{0U};
    std::atomic<std::uint64_t> seeks{0U};
    std::atomic<std::uint64_t> controlCommands{0U};
    std::atomic<std::uint64_t> resetRequests{0U};
    std::atomic<std::uint64_t> resetWaits{0U};
    std::atomic<std::uint64_t> rejectedCommands{0U};
    std::atomic<std::uint64_t> mixFailures{0U};
  };

  [[nodiscard]] core::Result<void> enqueue(ControlCommand command);
  [[nodiscard]] core::Result<void> validate(const ControlCommand& command) const;
  [[nodiscard]] bool processControls() noexcept;
  void publishState() noexcept;
  [[nodiscard]] bool mixWithLoop(std::span<float> output,
                                 std::size_t frameCount) noexcept;

  SpscInterleavedAudioRingBuffer& ring_;
  std::uint32_t sampleRate_{48000U};
  std::uint8_t outputChannels_{2U};
  std::size_t blockFrames_{1024U};
  std::vector<float> scratch_;
  RoutingWorkspace workspace_;
  ControlQueue controls_;
  std::shared_ptr<const RoutedPlaybackTimeline> timeline_;
  PlaybackLoop loop_;
  time::SampleFrame playhead_{0};
  bool playing_{false};
  std::uint64_t pendingResetEpoch_{0U};
  // Commands consumed so far, counted by the consumer alone, and the count that playing() and
  // playhead() are known to include: stored after the state is published.
  std::uint64_t consumedCommands_{0U};
  std::atomic<std::uint64_t> acknowledgedCommands_{0U};
  std::atomic<time::SampleFrame> publishedPlayhead_{0};
  std::atomic<bool> publishedPlaying_{false};
  AtomicStats stats_;
};

class MultichannelPlaybackFeederService final {
public:
  explicit MultichannelPlaybackFeederService(
      MultichannelPlaybackFeeder& feeder,
      std::size_t watermarkFrames = 4096U);
  ~MultichannelPlaybackFeederService();
  [[nodiscard]] core::Result<void> start();
  void stop() noexcept;
  [[nodiscard]] bool running() const noexcept;

private:
  MultichannelPlaybackFeeder& feeder_;
  std::size_t watermarkFrames_{4096U};
  std::jthread worker_;
  std::atomic<bool> running_{false};
};

}  // namespace seam::rendering
