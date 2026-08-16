#pragma once

#include "seam/core/result.hpp"
#include "seam/rendering/audio_ring_buffer.hpp"
#include "seam/rendering/pcm_cache.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
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
};

class PlaybackFeeder final {
public:
  PlaybackFeeder(SpscAudioRingBuffer& ring,
                 std::uint32_t sampleRate,
                 std::size_t blockFrames = 1024U);

  [[nodiscard]] core::Result<void> setTimeline(
      std::shared_ptr<const PlaybackTimeline> timeline);
  [[nodiscard]] core::Result<void> setLoop(PlaybackLoop loop);
  void setPlaying(bool playing) noexcept { playing_ = playing; }
  [[nodiscard]] bool playing() const noexcept { return playing_; }
  void seek(time::SampleFrame frame) noexcept;
  [[nodiscard]] time::SampleFrame playhead() const noexcept { return playhead_; }
  [[nodiscard]] std::size_t feedOnce() noexcept;
  [[nodiscard]] std::size_t feedToWatermark(std::size_t targetFrames) noexcept;
  [[nodiscard]] const PlaybackFeederStats& stats() const noexcept { return stats_; }

private:
  void mixWithLoop(std::span<float> output) noexcept;

  SpscAudioRingBuffer& ring_;
  std::uint32_t sampleRate_{48000U};
  std::vector<float> scratch_;
  std::shared_ptr<const PlaybackTimeline> timeline_;
  PlaybackLoop loop_;
  time::SampleFrame playhead_{0};
  bool playing_{false};
  PlaybackFeederStats stats_;
};

}  // namespace seam::rendering
