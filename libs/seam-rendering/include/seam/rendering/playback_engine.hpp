#pragma once

#include "seam/core/result.hpp"
#include "seam/rendering/audio_ring_buffer.hpp"
#include "seam/rendering/pcm_cache.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace seam::rendering {

struct PlaybackClip final {
  std::string id;
  std::shared_ptr<const CachedPcm> pcm;
  float gain{1.0F};
  time::SampleFrame fadeInFrames{0};
  time::SampleFrame fadeOutFrames{0};
  bool enabled{true};
};

class PlaybackTimeline final {
public:
  explicit PlaybackTimeline(std::uint32_t sampleRate = 48000U)
      : sampleRate_(sampleRate) {}

  [[nodiscard]] core::Result<void> setClips(std::vector<PlaybackClip> clips);
  [[nodiscard]] core::Result<void> addClip(PlaybackClip clip);
  [[nodiscard]] const std::vector<PlaybackClip>& clips() const noexcept {
    return clips_;
  }
  [[nodiscard]] std::uint32_t sampleRate() const noexcept { return sampleRate_; }
  [[nodiscard]] time::SampleFrame startFrame() const noexcept;
  [[nodiscard]] time::SampleFrame endFrame() const noexcept;
  void mix(time::SampleFrame startFrame, std::span<float> output) const noexcept;

private:
  [[nodiscard]] core::Result<void> validateClip(const PlaybackClip& clip) const;

  std::uint32_t sampleRate_{48000U};
  std::vector<PlaybackClip> clips_;
};

struct PlaybackLoop final {
  bool enabled{false};
  time::SampleFrame startFrame{0};
  time::SampleFrame endFrame{0};
};

struct PlaybackFeederStats final {
  std::uint64_t feedCalls{0};
  std::uint64_t framesMixed{0};
  std::uint64_t framesWritten{0};
  std::uint64_t ringFullEvents{0};
  std::uint64_t loopWraps{0};
  std::uint64_t seeks{0};
  std::uint64_t controlCommands{0};
  std::uint64_t resetRequests{0};
  std::uint64_t resetWaits{0};
  std::uint64_t rejectedCommands{0};
};

// UI methods enqueue immutable control commands. Only the feeder thread mutates
// timeline, loop, playhead, and playing state. This preserves SPSC ownership
// when a dedicated feeder thread is attached in the native platform phase.
class PlaybackFeeder final {
public:
  PlaybackFeeder(SpscAudioRingBuffer& ring,
                 std::uint32_t sampleRate,
                 std::size_t blockFrames = 1024U,
                 std::size_t controlQueueCapacity = 64U);

  [[nodiscard]] core::Result<void> setTimeline(
      std::shared_ptr<const PlaybackTimeline> timeline);
  [[nodiscard]] core::Result<void> setLoop(PlaybackLoop loop);
  [[nodiscard]] core::Result<void> setPlaying(bool playing);
  [[nodiscard]] bool playing() const noexcept {
    return publishedPlaying_.load(std::memory_order_acquire);
  }
  [[nodiscard]] core::Result<void> seek(time::SampleFrame frame);
  [[nodiscard]] time::SampleFrame playhead() const noexcept {
    return publishedPlayhead_.load(std::memory_order_acquire);
  }
  [[nodiscard]] std::size_t feedOnce() noexcept;
  [[nodiscard]] std::size_t feedToWatermark(std::size_t targetFrames) noexcept;
  [[nodiscard]] PlaybackFeederStats stats() const noexcept;

private:
  enum class ControlKind : std::uint8_t { Timeline, Loop, Playing, Seek };

  struct ControlCommand final {
    ControlKind kind{ControlKind::Playing};
    std::shared_ptr<const PlaybackTimeline> timeline;
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
    alignas(64) std::atomic<std::size_t> readIndex_{0};
    alignas(64) std::atomic<std::size_t> writeIndex_{0};
  };

  struct AtomicStats final {
    std::atomic<std::uint64_t> feedCalls{0};
    std::atomic<std::uint64_t> framesMixed{0};
    std::atomic<std::uint64_t> framesWritten{0};
    std::atomic<std::uint64_t> ringFullEvents{0};
    std::atomic<std::uint64_t> loopWraps{0};
    std::atomic<std::uint64_t> seeks{0};
    std::atomic<std::uint64_t> controlCommands{0};
    std::atomic<std::uint64_t> resetRequests{0};
    std::atomic<std::uint64_t> resetWaits{0};
    std::atomic<std::uint64_t> rejectedCommands{0};
  };

  [[nodiscard]] core::Result<void> enqueue(ControlCommand command);
  [[nodiscard]] bool processControls() noexcept;
  void publishState() noexcept;
  void mixWithLoop(std::span<float> output) noexcept;

  SpscAudioRingBuffer& ring_;
  std::uint32_t sampleRate_{48000U};
  std::vector<float> scratch_;
  ControlQueue controls_;

  // Feeder-thread-owned state.
  std::shared_ptr<const PlaybackTimeline> timeline_;
  PlaybackLoop loop_;
  time::SampleFrame playhead_{0};
  bool playing_{false};
  std::uint64_t pendingResetEpoch_{0};

  // Cross-thread observation only.
  std::atomic<time::SampleFrame> publishedPlayhead_{0};
  std::atomic<bool> publishedPlaying_{false};
  AtomicStats stats_;
};

}  // namespace seam::rendering
