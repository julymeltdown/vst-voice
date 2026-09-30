#pragma once

#include "seam/authoring/render_coordinator.hpp"
#include "seam/core/result.hpp"
#include "seam/rendering/interleaved_audio_ring_buffer.hpp"
#include "seam/rendering/multichannel_playback.hpp"
#include "seam/rendering/multichannel_routing.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace seam::authoring {

struct TransportConfig final {
  std::uint32_t sampleRate{48000U};
  std::uint8_t outputChannels{2U};
  std::size_t ringCapacityFrames{32768U};
  std::size_t blockFrames{1024U};
  std::size_t watermarkFrames{8192U};
};

struct TransportState final {
  bool playing{false};
  bool available{false};
  std::string availabilityDiagnostic;
  time::SampleFrame playhead{0};
  rendering::PlaybackLoop loop;
  std::uint64_t publishedRevision{0U};
  time::SampleFrame timelineEnd{0};
};

class TransportController final {
public:
  explicit TransportController(TransportConfig config = {});
  ~TransportController();

  TransportController(const TransportController&) = delete;
  TransportController& operator=(const TransportController&) = delete;

  [[nodiscard]] core::Result<void> start();
  void shutdown() noexcept;

  [[nodiscard]] core::Result<void> publishAudio(
      RealtimeProjectAudioPublication::ReadHandle audio);
  // Returns the transport to "nothing to play": stops playback, empties the timeline and forgets
  // the published revision, and also forgets what a reconfigure was carrying for the audio it
  // dropped (the loop, whether it was playing, the playhead), so the next publication starts
  // silent at the beginning. A transport that holds no audio and has no dropped audio to forget
  // is left alone and sent nothing. In particular a play asked for while it held none stays
  // armed, whenever it was asked: it belongs to no audio, so there is nothing for a clear to
  // drop, and it plays the first audio the transport is given.
  // The feeder applies its commands on its own thread. When this returns they are queued, not
  // acknowledged: state() reports available, loop, publishedRevision and timelineEnd at once, but
  // playing, playhead and what is already in the ring follow a moment later.
  [[nodiscard]] core::Result<void> clearAudio();
  [[nodiscard]] core::Result<void> play();
  [[nodiscard]] core::Result<void> pause();
  // Pauses and rewinds. It also supersedes what a reconfigure was carrying for the audio to come
  // (the play, and the playhead): the creator asked for the beginning last. A pause or a play
  // leaves the carried position alone.
  [[nodiscard]] core::Result<void> stop();
  [[nodiscard]] core::Result<void> seek(time::SampleFrame frame);
  [[nodiscard]] core::Result<void> setLoop(rendering::PlaybackLoop range);
  [[nodiscard]] core::Result<void> reconfigure(TransportConfig config);

  [[nodiscard]] TransportState state() const noexcept;
  [[nodiscard]] TransportConfig config() const noexcept {
    std::lock_guard lock(lifecycleMutex_);
    return config_;
  }
  [[nodiscard]] rendering::SpscInterleavedAudioRingBuffer& ringBuffer()
      noexcept {
    return *ring_;
  }
  [[nodiscard]] const rendering::SpscInterleavedAudioRingBuffer& ringBuffer()
      const noexcept {
    return *ring_;
  }
  [[nodiscard]] std::uint8_t outputChannels() const noexcept {
    return config_.outputChannels;
  }
  [[nodiscard]] std::uint32_t sampleRate() const noexcept {
    return config_.sampleRate;
  }
  [[nodiscard]] rendering::MultichannelFeederStats feederStats() const noexcept {
    return feeder_->stats();
  }

private:
  [[nodiscard]] core::Result<std::shared_ptr<const rendering::RoutedPlaybackTimeline>>
  makeTimeline(const PublishedProjectAudio& audio, bool crossfade) const;

  TransportConfig config_;
  std::unique_ptr<rendering::SpscInterleavedAudioRingBuffer> ring_;
  std::unique_ptr<rendering::MultichannelPlaybackFeeder> feeder_;
  std::unique_ptr<rendering::MultichannelPlaybackFeederService> service_;
  mutable std::mutex lifecycleMutex_;
  mutable std::mutex stateMutex_;
  rendering::PlaybackLoop loop_;
  std::uint64_t publishedRevision_{0U};
  time::SampleFrame timelineEnd_{0};
  time::SampleFrame pendingPlayhead_{0};
  bool pendingPlayheadValid_{false};
  bool resumeAfterReconfigure_{false};
  // True from a reconfigure that dropped published audio until audio is published again or
  // cleared: loop_, pendingPlayhead_ and resumeAfterReconfigure_ then describe that audio.
  bool audioDroppedByReconfigure_{false};
  bool started_{false};
};

}  // namespace seam::authoring
