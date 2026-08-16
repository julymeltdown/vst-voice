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
#include <stop_token>
#include <thread>
#include <vector>

namespace seam::rendering {

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
public:
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
  [[nodiscard]] bool playing() const noexcept;
  [[nodiscard]] time::SampleFrame playhead() const noexcept;
  [[nodiscard]] std::size_t feedOnce() noexcept;
  [[nodiscard]] std::size_t feedToWatermark(std::size_t targetFrames) noexcept;
  [[nodiscard]] MultichannelFeederStats stats() const noexcept;

private:
  enum class ControlKind : std::uint8_t { Timeline, Loop, Playing, Seek };
  struct ControlCommand final {
    ControlKind kind{ControlKind::Playing};
    std::shared_ptr<const RoutedPlaybackTimeline> timeline;
    PlaybackLoop loop;
    time::SampleFrame frame{0};
    bool playing{false};
  };
  class ControlQueue final {
  public:
    explicit ControlQueue(std::size_t capacity);
    [[nodiscard]] bool push(ControlCommand command) noexcept;
    [[nodiscard]] std::optional<ControlCommand> pop() noexcept;
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
