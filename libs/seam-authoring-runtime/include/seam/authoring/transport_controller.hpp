#pragma once

#include "seam/authoring/render_coordinator.hpp"
#include "seam/core/result.hpp"
#include "seam/rendering/interleaved_audio_ring_buffer.hpp"
#include "seam/rendering/multichannel_playback.hpp"
#include "seam/rendering/multichannel_routing.hpp"

#include <atomic>
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
  // not yet applied it, it is there. When the ring cannot say where the device is (the device moved
  // under every attempt to read it: an adversarial schedule, and how often a real device meets it
  // has not been measured), it is the last place that was confirmed, so that the playhead on screen
  // stays where it was; nothing that carries the creator's place to the audio that follows goes by
  // that value, and they refuse instead.
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
  // so the creator can ask again and nothing half-done has to be undone first. A pause, a loop
  // change and a publication carry the creator's place to the audio that follows, and are refused
  // with Conflict when that place cannot be read from the ring at that moment (see
  // SpscInterleavedAudioRingBuffer::nextFrame): a place that is guessed would be the feeder's, which
  // is ahead of the creator by what the ring holds, and would skip that audio. So is reconfigure(),
  // which leaves the transport as it was.
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
  // For the owner of the audio consumer, when it takes the consumer away to change it (another
  // device, another sample rate or block size) and puts one back. Call it once the consumer is
  // stopped; when it says playback is to go on, call play() once the new consumer is back.
  //
  // It pauses the transport at the place the creator is at. That includes the end of the audio: a
  // feeder that has handed over the last of it leaves the consumer a tail to play out, the consumer
  // is gone and the tail goes with it, so that a play from here starts at the place the creator was
  // and not, as a play at the end of the audio does, at its beginning.
  //
  // And it says whether the creator was playing, which the feeder's own report does not: that
  // follows the creator's commands by a moment. A Pause that is queued is not playing, and a Play
  // that is queued is, and so is a Play that waits for audio to be rendered.
  //
  // The end of the audio is told from the ring and from the consumer, and not from the feeder's
  // playing flag. The feeder reports that it has stopped when it has handed over the last of the
  // audio, which is as far ahead of the consumer as the ring is deep, and it reports that it is
  // still playing until its next turn when the last block it handed over ended exactly with the
  // audio. So at the end of the audio the creator is playing when the ring still holds audio that
  // nobody has played and nobody has been asked to drop, and the creator's last request was a Play
  // (not a Pause, a Stop or a clear), and a consumer is playing it out (consumerWasRunning) or the
  // Play has not been taken up by any consumer yet (see setConsumerRunning). A consumer that ran
  // and has stopped has taken its Play up, and what its ring still holds is not played by it: with
  // the audio all played, or with nobody to play the ring out, the song is over whatever the
  // feeder's flag says, and the answer does not depend on whether the feeder has taken the turn
  // that lowers that flag. The feeder has to have applied everything the creator sent for the ring
  // to say anything: until then what was sent decides, and a seek or a loop change that follows
  // the end makes it drop the ring.
  //
  // An error is Conflict, and nothing was queued or recorded: the place the creator is at cannot be
  // read from the ring at this moment (see SpscInterleavedAudioRingBuffer::nextFrame), or the
  // feeder's queue is full. The transport is as it was.
  [[nodiscard]] core::Result<bool> suspend(bool consumerWasRunning);
  // For the owner of the consumer, to say whether the consumer runs: when it has started it, when
  // it has stopped it, and once per frame for a device that can stop on its own. The transport
  // cannot see the device: a ring that nobody reads looks like one that is read until the feeder
  // asks the consumer to drop something. Two decisions need it, both at the end of the audio,
  // where the feeder has handed over the last of it and the ring holds the rest:
  //  - A publication carries the Play to the replacement while a consumer runs, or while a Play
  //    stands that no consumer has taken up. A consumer that stopped on its own (a device that was
  //    unplugged) is not started again by a render.
  //  - suspend() takes the same two for a consumer that was playing the end out, next to
  //    consumerWasRunning, which says what the owner saw as it stopped the consumer.
  // A Play that no consumer has taken up is what play() leaves while the consumer is not running,
  // and it lasts until the consumer runs. It counts for as long as the Play stands: a Pause, a
  // Stop, a suspension or a clear takes the Play away, and a Play that is asked for after that is
  // taken up or not by the consumer as it is then. An owner that never says anything has a
  // consumer that never runs and a Play that is never taken up: whatever Play is asked for stands.
  // Thread-safe and lock-free: the owner calls it from the thread that paints, and a publication
  // reads it on the render thread.
  void setConsumerRunning(bool running) noexcept;
  // What the owner last said: false until it says that the consumer runs. It is not the device, and
  // for a device that stops on its own it is as old as the owner's last statement.
  [[nodiscard]] bool consumerRunning() const noexcept;
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
  // after pause(), setLoop(), suspend() and publishAudio() have chosen what their script carries
  // and before they send it, so that a test can have the feeder mix on in between. It is empty in
  // the product. It must not throw and must not call into this controller. Set it only while no
  // thread calls state() and the feeder's service is stopped (shutdown() stops it): the probe is
  // then the only thing that moves the feeder.
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
  // The feeder's playhead stands in for the device only when the ring held no frame at the moment
  // it was asked (the device has played everything the feeder mixed, so the feeder's next frame is
  // the device's), and it was taken before the ring was asked: whatever the feeder mixed in between
  // the device has played too, so it is at worst played again. That goes by the order the audio is
  // played in and not by the number of the frame: a loop that wrapped in between puts the feeder at
  // a lower number, which is still later in the audio that is played.
  // Empty when the ring has frames and cannot say where the device is among them (Busy or
  // Unplaced): the feeder is then ahead of the device by what the ring holds, and its playhead must
  // not stand in. Needs lifecycleMutex_ and stateMutex_.
  [[nodiscard]] std::optional<time::SampleFrame> audiblePlayhead() const noexcept;
  // Adds to a script that makes the feeder drop the audio it has mixed ahead of the device the
  // seek that keeps playback where the creator hears it. A playing feeder that has audio keeps
  // mixing until it applies the script, so that its playhead and the audible position being equal
  // when they are sampled says nothing about the moment it applies it: for such a feeder the seek
  // is always there. A feeder that has no audio mixes none, and has nothing to keep. A feeder that
  // is not playing has a tail only when the ring still holds audio, and then only a script that
  // drops the ring needs the seek (`alsoWhenNotPlaying`). A Conflict, with the script as it was, when
  // the position is needed and audiblePlayhead() has none. Needs lifecycleMutex_ and stateMutex_.
  [[nodiscard]] core::Result<void> carryAudiblePosition(
      rendering::MultichannelPlaybackFeeder::ControlScript& script, bool alsoWhenNotPlaying) const;
  // What there is still to hear of the audio the transport holds, going by the feeder (or by the
  // commands that are queued for it, which are what it is about to be) and by the ring. Neither
  // member is set when the transport holds no audio.
  struct HeldAudio final {
    // The feeder is mixing audio that it has not handed over yet: it is playing, and not at the
    // end of audio that does not loop.
    bool mixing{false};
    // The feeder has handed over the last of the audio, whether or not it has taken the turn that
    // says so, and the ring holds audio that nobody has played and nobody has been asked to drop.
    // Only a feeder that has applied everything it was sent can say this: the ring is not the
    // answer to a command that has not been applied.
    bool endInRing{false};
  };
  // Needs lifecycleMutex_ and stateMutex_.
  [[nodiscard]] HeldAudio heldAudio(const rendering::PlaybackPoint& point) const noexcept;

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
  // The creator's last request of playback was a Play: set by play(), cleared by pause(), stop(),
  // clearAudio() and suspend(), and set to what the transport was doing by a reconfigure, for the
  // audio that follows it. It is not the feeder's playing flag, which also goes false when the
  // audio ends on its own, and it is not consumed by a publication. A publication starts the
  // feeder when the creator is playing the audio it replaces and the replacement has to carry on:
  // when the transport held no audio (the Play was waiting for it), while the feeder is mixing
  // (it may finish before it applies the replacement), and while the creator still hears the end
  // of the audio (the feeder has stopped, the ring holds the rest, and the replacement drops it).
  // A song that has ended stays ended however the audio is replaced after it: nothing is left to
  // hear when it was all played, and nothing the creator paused, stopped or put back at the start
  // starts again. A feeder that still reports that it is playing at the end of audio that was all
  // played is told to stop, so that a replacement that is longer does not play its new end.
  bool playRequested_{false};
  // What the owner of the consumer last said (see setConsumerRunning), and whether no consumer has
  // run since the last Play that was asked for while none did: two bits of one word (see
  // transport_controller.cpp), so that a publication on the render thread reads both together and
  // play() cannot record a Play as waiting for a consumer that has just started. The second is read
  // only with playRequested_. A consumer that runs has taken every Play up, so the two are never
  // set together. Atomic, and not guarded: the owner says it from its own thread.
  std::atomic<std::uint8_t> consumer_{0U};
  // True from a reconfigure that dropped published audio until audio is published again or
  // cleared: loop_, pendingPlayhead_ and playRequested_ then describe that audio.
  bool audioDroppedByReconfigure_{false};
  bool started_{false};
  // Guarded by lifecycleMutex_. The feeder counts the commands it consumes from its own start, so
  // these start again from nothing whenever a reconfigure builds a new feeder.
  std::uint64_t queuedCommands_{0U};
  std::optional<QueuedIntent> queuedIntent_;
  // Guarded by lifecycleMutex_, which state() holds while it calls it.
  StateSampleProbe stateSampleProbe_;
  // The last place audiblePlayhead() confirmed: what state() reports when it cannot. Guarded by
  // lifecycleMutex_ and stateMutex_, as audiblePlayhead() is.
  mutable time::SampleFrame lastAudiblePlayhead_{0};
};

}  // namespace seam::authoring
