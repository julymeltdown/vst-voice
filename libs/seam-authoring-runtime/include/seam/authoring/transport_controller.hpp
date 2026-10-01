#pragma once

#include "seam/authoring/render_coordinator.hpp"
#include "seam/core/result.hpp"
#include "seam/rendering/interleaved_audio_ring_buffer.hpp"
#include "seam/rendering/multichannel_playback.hpp"
#include "seam/rendering/multichannel_routing.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
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
  // Where the creator hears the transport, which is behind `playhead` while it plays: the feeder
  // mixes ahead of the audio device, and `playhead` is where it has mixed to. This is the place in
  // the audio of the frame the device plays next, and it is what a playhead on screen shows. It
  // goes by the frames themselves, which carry their place, and not by how far ahead the feeder is,
  // so it is right for audio that loops, that began inside a loop and that the feeder has finished
  // while the device still plays its tail. Where a command has put the playhead and the feeder has
  // not yet applied it, it is there.
  time::SampleFrame audiblePlayhead{0};
  rendering::PlaybackLoop loop;
  std::uint64_t publishedRevision{0U};
  time::SampleFrame timelineEnd{0};
  // Whether the feeder has applied every command it was sent. playing and playhead are the
  // feeder's own report, so until then they show what the creator asked for before the last
  // command, and a decision about the audio device that goes by them can undo that command.
  // Once it is true they include every command sent: the feeder moves on its own thread, and
  // state() reads the count of the commands it has applied before the state that count covers, so
  // a report never pairs the newest count with a playing flag from before the last command.
  bool settled{true};
};

class TransportController final {
public:
  explicit TransportController(TransportConfig config = {});
  ~TransportController();

  TransportController(const TransportController&) = delete;
  TransportController& operator=(const TransportController&) = delete;

  [[nodiscard]] core::Result<void> start();
  void shutdown() noexcept;

  // Every call below that sends the feeder commands (publishAudio, clearAudio, play, pause, stop,
  // seek, setLoop) sends them as one script: the feeder gets all of them or none, and a call that
  // returns an error has queued nothing and left what this controller records exactly as it was,
  // so the creator can ask again and nothing half-done has to be undone first.
  //
  // The feeder applies its commands on its own thread, so what it reports (state().playing and
  // state().playhead) follows a moment after a call returns. The decisions this controller makes
  // about audio to come (what a publication resumes, where a reconfigure carries the playhead) do
  // not wait for that: they go by what the creator asked for last until the feeder says it has
  // applied it, and by what the feeder reports after that.
  [[nodiscard]] core::Result<void> publishAudio(
      RealtimeProjectAudioPublication::ReadHandle audio);
  // Returns the transport to "nothing to play": stops playback, empties the timeline and forgets
  // the published revision, and also forgets what a reconfigure was carrying for the audio it
  // dropped (the loop, whether it was playing, the playhead), so the next publication starts
  // silent at the beginning. A transport that holds no audio and has no dropped audio to forget
  // is left alone and sent nothing. In particular a play asked for while it held none stays
  // armed, whenever it was asked: it belongs to no audio, so there is nothing for a clear to
  // drop, and it plays the first audio the transport is given.
  // When this returns the commands are queued, not acknowledged: state() reports available, loop,
  // publishedRevision and timelineEnd at once, but playing, playhead and what is already in the
  // ring follow a moment later.
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
  // For the owner of a consumer that is not running (the audio device is stopped), after the
  // creator asked for playback and before the consumer is started. Starting the consumer first
  // would play whatever the ring still holds of audio from before, and waiting for the ring to
  // fill while the consumer is stopped never ends: for a seek, a play, a loop or a timeline the
  // feeder asks the consumer to drop what the ring holds and writes nothing until it has, and a
  // stopped consumer does not answer. So this waits for the feeder to apply every command it was
  // sent, answers the request that the commands made in the consumer's place, and waits for the
  // ring to hold the start buffer, or all of the audio when that is less or the feeder has no more
  // to give. An error is Conflict: the audio went away, or did not arrive within the timeout.
  // Call it only while no thread reads ringBuffer().
  [[nodiscard]] core::Result<void> awaitStartBuffer(std::chrono::milliseconds timeout);

  [[nodiscard]] TransportState state() const noexcept;
  // For tests. Called with the feeder before each read state() makes of what the feeder reports,
  // so that a test can have the feeder apply a command at any point of one sample, and once more
  // after pause() and setLoop() have chosen the position their script carries and before they send
  // it, so that a test can have the feeder mix on in between. It is empty in the product. It must
  // not throw and must not call into this controller. Set it only while no thread calls state()
  // and the feeder's service is stopped (shutdown() stops it): the probe is then the only thing
  // that moves the feeder.
  using StateSampleProbe = std::function<void(rendering::MultichannelPlaybackFeeder&)>;
  void setStateSampleProbe(StateSampleProbe probe);
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
  // What the creator's last commands make of the feeder, until the feeder has consumed them: the
  // state that the commands sent up to and including number `acknowledgedAt` lead to. The feeder's
  // own report lags those commands, and a decision made from it would undo them.
  struct QueuedIntent final {
    std::uint64_t acknowledgedAt{0U};
    rendering::PlaybackPoint point;
    // Whether those commands put the playhead somewhere themselves, with a seek. When they do not,
    // the playhead is wherever the feeder has mixed to by the time it applies them.
    bool positionIsExplicit{false};
  };
  // Sends a script to the feeder and records what it leads to. An error queues nothing and records
  // nothing. Needs lifecycleMutex_.
  [[nodiscard]] core::Result<void> send(
      rendering::MultichannelPlaybackFeeder::ControlScript script);
  // Whether the feeder is playing and where it is, going by the last commands sent for as long as
  // the feeder has not consumed them and by the feeder's own report once it has. Needs
  // lifecycleMutex_.
  [[nodiscard]] rendering::PlaybackPoint currentPoint() const noexcept;
  // Where the creator hears the transport: the place in the audio of the frame the device plays
  // next (the ring keeps it with every frame), and the feeder's playhead when the ring holds none.
  // This is the position that playback goes on from when the feeder drops the audio it has mixed
  // ahead of the device and carries on or comes back, as a pause, a loop change, a publication and
  // a change of settings make it do. The feeder's own playhead is ahead of the creator by what the
  // ring holds, and a position taken from it skips that much of the audio.
  // Where a command has put the playhead and the feeder has not yet applied it, that is the answer,
  // and so it is when the feeder has asked the device to drop what the ring holds. The place a
  // command carries is never later than where the device is when the feeder applies it, because
  // the device only moves forward, so what the device has played is at worst played again.
  // Needs lifecycleMutex_ and stateMutex_.
  [[nodiscard]] time::SampleFrame audiblePlayhead() const noexcept;
  // Adds to a script that makes the feeder drop the audio it has mixed ahead of the device the
  // seek that keeps playback where the creator hears it. A playing feeder that has audio keeps
  // mixing until it applies the script, so that its playhead and the audible position being equal
  // when they are sampled says nothing about the moment it applies it: for such a feeder the seek
  // is always there. A feeder that has no audio mixes none, and has nothing to keep. A feeder that
  // is not playing has a tail only when the ring still holds audio, and then only a script that
  // drops the ring needs the seek (`alsoWhenNotPlaying`). Needs lifecycleMutex_ and stateMutex_.
  void carryAudiblePosition(rendering::MultichannelPlaybackFeeder::ControlScript& script,
                            bool alsoWhenNotPlaying) const;

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
  // Guarded by lifecycleMutex_. The feeder counts the commands it consumes from its own start, so
  // these start again from nothing whenever a reconfigure builds a new feeder.
  std::uint64_t queuedCommands_{0U};
  std::optional<QueuedIntent> queuedIntent_;
  // Guarded by lifecycleMutex_, which state() holds while it calls it.
  StateSampleProbe stateSampleProbe_;
};

}  // namespace seam::authoring
