#include "seam/authoring/transport_controller.hpp"

#include "seam/domain/routing.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace seam::authoring {
namespace {

core::Result<domain::RoutingMatrix> outputMatrix(std::uint8_t sourceChannels,
                                                  std::uint8_t outputChannels) {
  if (sourceChannels == 0U || outputChannels == 0U ||
      sourceChannels > domain::kMaximumAudioChannels ||
      outputChannels > domain::kMaximumAudioChannels) {
    return core::failure<domain::RoutingMatrix>(
        core::ErrorCode::InvalidArgument,
        "Transport channel count must be between one and eight");
  }
  domain::RoutingMatrix matrix{
      .sourceChannels = sourceChannels,
      .destinationChannels = outputChannels,
      .gains = std::vector<float>(
          static_cast<std::size_t>(sourceChannels) * outputChannels, 0.0F),
  };
  if (sourceChannels == 1U) {
    for (std::uint8_t destination = 0U; destination < outputChannels;
         ++destination) {
      matrix.setGain(destination, 0U, 1.0F);
    }
  } else if (outputChannels == 1U) {
    const auto gain = 1.0F / static_cast<float>(sourceChannels);
    for (std::uint8_t source = 0U; source < sourceChannels; ++source) {
      matrix.setGain(0U, source, gain);
    }
  } else {
    const auto shared = std::min(sourceChannels, outputChannels);
    for (std::uint8_t channel = 0U; channel < shared; ++channel) {
      matrix.setGain(channel, channel, 1.0F);
    }
  }
  const auto valid = matrix.validate();
  if (!valid) return core::Result<domain::RoutingMatrix>{valid.error()};
  return core::success(std::move(matrix));
}

core::Result<void> validateTransportConfig(const TransportConfig& config) {
  if (config.sampleRate < 8000U || config.sampleRate > 192000U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Transport sample rate is outside supported bounds");
  }
  if (config.outputChannels == 0U ||
      config.outputChannels > domain::kMaximumAudioChannels) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Transport output channel count is outside supported bounds");
  }
  if (config.ringCapacityFrames < 2U || config.blockFrames == 0U ||
      config.watermarkFrames == 0U ||
      config.watermarkFrames >= config.ringCapacityFrames) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Transport ring capacity, block, and watermark are invalid");
  }
  return core::success();
}

rendering::PlaybackLoop remapLoop(rendering::PlaybackLoop loop,
                                  time::SampleFrame timelineEnd) noexcept {
  if (!loop.enabled || timelineEnd <= 0) return {};
  loop.startFrame = std::clamp<time::SampleFrame>(
      loop.startFrame, 0, timelineEnd - 1);
  loop.endFrame = std::clamp<time::SampleFrame>(
      loop.endFrame, loop.startFrame + 1, timelineEnd);
  if (loop.endFrame <= loop.startFrame) return {};
  return loop;
}

// The routing of everything the authoring transport plays: one master bus straight to the device.
domain::ProjectRouting authoringMasterRouting(std::uint8_t outputChannels) {
  const domain::BusId masterId{1U};
  return domain::ProjectRouting{
      .deviceOutputChannels = outputChannels,
      .masterBus = masterId,
      .buses = {domain::AudioBus{.id = masterId,
                                 .name = "Authoring Master",
                                 .channelCount = outputChannels}},
      .sends = {},
      .deviceRoutes = {domain::DeviceOutputRoute{
          .sourceBus = masterId,
          .matrix = domain::RoutingMatrix::identity(outputChannels)}},
  };
}

}  // namespace

TransportController::TransportController(TransportConfig config)
    : config_(config),
      ring_(std::make_unique<rendering::SpscInterleavedAudioRingBuffer>(
          config.ringCapacityFrames, config.outputChannels)),
      feeder_(std::make_unique<rendering::MultichannelPlaybackFeeder>(
          *ring_, config.sampleRate, config.outputChannels, config.blockFrames)),
      service_(std::make_unique<rendering::MultichannelPlaybackFeederService>(
          *feeder_, config.watermarkFrames)) {}

TransportController::~TransportController() { shutdown(); }

core::Result<void> TransportController::start() {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  if (started_) return core::success();
  const auto result = service_->start();
  if (result) started_ = true;
  return result;
}

void TransportController::shutdown() noexcept {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  service_->stop();
  started_ = false;
}

core::Result<void> TransportController::reconfigure(TransportConfig config) {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  const auto valid = validateTransportConfig(config);
  if (!valid) return valid;
  if (config_.sampleRate == config.sampleRate &&
      config_.outputChannels == config.outputChannels &&
      config_.ringCapacityFrames == config.ringCapacityFrames &&
      config_.blockFrames == config.blockFrames &&
      config_.watermarkFrames == config.watermarkFrames) {
    return core::success();
  }

  const bool wasStarted = started_;
  const auto previousSampleRate = config_.sampleRate;
  const auto scaleFrame = [previousSampleRate, &config](
                               time::SampleFrame frame) {
    if (frame <= 0) return time::SampleFrame{0};
    return static_cast<time::SampleFrame>(std::llround(
        static_cast<long double>(frame) * config.sampleRate /
        previousSampleRate));
  };
  auto remappedLoop = loop_;
  if (remappedLoop.enabled) {
    remappedLoop.startFrame = scaleFrame(remappedLoop.startFrame);
    remappedLoop.endFrame = scaleFrame(remappedLoop.endFrame);
    if (remappedLoop.endFrame <= remappedLoop.startFrame) remappedLoop = {};
  }
  service_->stop();
  started_ = false;
  // The feeder has stopped, so what it reports is final. The commands still queued to it are not
  // final, and they go with it: what the creator asked for last says what they would have made of
  // it, which is what the audio that follows has to carry. With no audio published the feeder was
  // just rebuilt by an earlier reconfigure (or was never given anything), and what the creator was
  // doing is what the controller recorded then: a second change of settings has no reason to
  // forget a play or a playhead that the first one carried.
  const auto point = currentPoint();
  bool wasPlaying = point.playing;
  auto currentPlayhead = point.playhead;
  {
    std::lock_guard lock(stateMutex_);
    // The audio the feeder had mixed ahead of the device goes with the ring that is replaced, and
    // so does the tail of audio a feeder that has finished leaves the device to play. The position
    // that carries on is the one the creator is at; whether playback carries on is a separate
    // matter, and it is the feeder's playing flag (the creator's last play or pause), which a
    // tail does not change.
    const auto audible = audiblePlayhead();
    if (!audible) {
      // The feeder was stopped for this, and nothing else has changed: it goes on as it was, and
      // the change is refused, because a position that is guessed would skip what the ring holds.
      const auto restored = wasStarted ? service_->start() : core::success();
      started_ = wasStarted && static_cast<bool>(restored);
      return core::failure(core::ErrorCode::Conflict,
                           "The place the creator is at could not be read, so the audio format was not changed",
                           restored ? std::string{} : restored.error().message);
    }
    currentPlayhead = *audible;
    if (timelineEnd_ == time::SampleFrame{0}) {
      wasPlaying = wasPlaying || resumeAfterReconfigure_;
      if (pendingPlayheadValid_) currentPlayhead = pendingPlayhead_;
    }
  }
  const auto remappedPlayhead = scaleFrame(currentPlayhead);

  std::unique_ptr<rendering::SpscInterleavedAudioRingBuffer> nextRing;
  std::unique_ptr<rendering::MultichannelPlaybackFeeder> nextFeeder;
  std::unique_ptr<rendering::MultichannelPlaybackFeederService> nextService;
  try {
    nextRing = std::make_unique<rendering::SpscInterleavedAudioRingBuffer>(
        config.ringCapacityFrames, config.outputChannels);
    nextFeeder = std::make_unique<rendering::MultichannelPlaybackFeeder>(
        *nextRing, config.sampleRate, config.outputChannels, config.blockFrames);
    nextService = std::make_unique<rendering::MultichannelPlaybackFeederService>(
        *nextFeeder, config.watermarkFrames);
  } catch (...) {
    const auto restored = wasStarted ? service_->start() : core::success();
    started_ = wasStarted && static_cast<bool>(restored);
    return core::failure(core::ErrorCode::Internal,
                         "Unable to allocate the requested transport format",
                         restored ? std::string{} : restored.error().message);
  }

  if (wasStarted) {
    const auto started = nextService->start();
    if (!started) {
      const auto restored = service_->start();
      started_ = static_cast<bool>(restored);
      return core::failure(
          core::ErrorCode::IoError,
          "Unable to start the requested transport format",
          started.error().message +
              (restored ? std::string{} : "; previous transport restart failed: " +
                                           restored.error().message));
    }
  }

  ring_ = std::move(nextRing);
  feeder_ = std::move(nextFeeder);
  service_ = std::move(nextService);
  config_ = config;
  // The new feeder has consumed nothing, and nothing has been sent to it.
  queuedCommands_ = 0U;
  queuedIntent_.reset();
  {
    std::lock_guard lock(stateMutex_);
    // The published audio is dropped here, but what the creator was doing with it (playing,
    // looping, where the playhead was) is carried for the audio that follows. Remember that it
    // belongs to audio that is gone, across as many reconfigures as happen before new audio comes.
    audioDroppedByReconfigure_ =
        audioDroppedByReconfigure_ || timelineEnd_ > time::SampleFrame{0};
    loop_ = remappedLoop;
    publishedRevision_ = 0U;
    timelineEnd_ = 0;
    pendingPlayhead_ = remappedPlayhead;
    pendingPlayheadValid_ = true;
    resumeAfterReconfigure_ = wasPlaying;
  }
  started_ = wasStarted;
  return core::success();
}

core::Result<std::shared_ptr<const rendering::RoutedPlaybackTimeline>>
TransportController::makeTimeline(const PublishedProjectAudio& audio,
                                  bool crossfade) const {
  if (audio.state != RenderState::Ready || audio.failure != RenderFailureKind::None) {
    return core::failure<std::shared_ptr<const rendering::RoutedPlaybackTimeline>>(
        core::ErrorCode::Conflict,
        "Only a successful production render can be published to transport",
        audio.diagnostic);
  }
  if (audio.result.sampleRate != config_.sampleRate) {
    return core::failure<std::shared_ptr<const rendering::RoutedPlaybackTimeline>>(
        core::ErrorCode::InvalidArgument,
        "Rendered audio sample rate does not match transport sample rate");
  }
  if (audio.result.channelCount == 0U || audio.result.channelCount > 8U ||
      audio.result.interleaved.empty() ||
      audio.result.interleaved.size() % audio.result.channelCount != 0U) {
    return core::failure<std::shared_ptr<const rendering::RoutedPlaybackTimeline>>(
        core::ErrorCode::InvalidArgument,
        "Rendered audio has an invalid interleaved channel layout");
  }
  if (!std::all_of(audio.result.interleaved.begin(),
                   audio.result.interleaved.end(),
                   [](float value) { return std::isfinite(value); })) {
    return core::failure<std::shared_ptr<const rendering::RoutedPlaybackTimeline>>(
        core::ErrorCode::InvalidArgument,
        "Rendered audio contains a non-finite sample");
  }

  auto route = outputMatrix(audio.result.channelCount, config_.outputChannels);
  if (!route) {
    return core::Result<std::shared_ptr<const rendering::RoutedPlaybackTimeline>>{
        route.error()};
  }

  auto pcm = std::make_shared<rendering::RoutedPcm>();
  pcm->sampleRate = audio.result.sampleRate;
  pcm->startFrame = 0;
  pcm->channelCount = audio.result.channelCount;
  pcm->interleavedSamples = audio.result.interleaved;
  const auto pcmValid = pcm->validate();
  if (!pcmValid) {
    return core::Result<std::shared_ptr<const rendering::RoutedPlaybackTimeline>>{
        pcmValid.error()};
  }

  const domain::BusId masterId{1U};
  auto routing = authoringMasterRouting(config_.outputChannels);
  auto timeline = std::make_shared<rendering::RoutedPlaybackTimeline>(
      config_.sampleRate);
  std::vector<rendering::RoutedPlaybackClip> clips;
  clips.push_back(rendering::RoutedPlaybackClip{
      .id = "authoring-production-revision-" +
            std::to_string(audio.projectRevision),
      .pcm = std::move(pcm),
      .outputRoute = domain::TrackOutputRoute{
          .bus = masterId,
          .matrix = std::move(route.value()),
      },
      .gain = 1.0F,
      .fadeInFrames = crossfade
                          ? static_cast<time::SampleFrame>(
                                std::min<std::uint32_t>(64U, config_.sampleRate / 100U))
                          : 0,
      .fadeOutFrames = crossfade
                           ? static_cast<time::SampleFrame>(
                                 std::min<std::uint32_t>(64U, config_.sampleRate / 100U))
                           : 0,
      .enabled = true,
      .solo = false,
  });
  const auto configured = timeline->configure(std::move(routing),
                                               std::move(clips));
  if (!configured) {
    return core::Result<std::shared_ptr<const rendering::RoutedPlaybackTimeline>>{
        configured.error()};
  }
  return core::success(
      std::shared_ptr<const rendering::RoutedPlaybackTimeline>{timeline});
}

core::Result<void> TransportController::publishAudio(
    RealtimeProjectAudioPublication::ReadHandle audio) {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  if (!audio) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Published authoring audio handle is empty");
  }
  bool crossfade = false;
  {
    std::lock_guard lock(stateMutex_);
    if (audio->projectRevision < publishedRevision_) {
      return core::failure(core::ErrorCode::Conflict,
                           "An older render revision cannot replace current audio");
    }
    crossfade = timelineEnd_ > 0;
  }
  auto timeline = makeTimeline(*audio, crossfade);
  if (!timeline) return core::Result<void>{timeline.error()};
  rendering::PlaybackLoop remappedLoop;
  time::SampleFrame remappedPlayhead{0};
  bool resumeAfterReconfigure = false;
  {
    std::lock_guard lock(stateMutex_);
    remappedLoop = remapLoop(loop_, timeline.value()->endFrame());
    // Where the creator is going to be, not where the feeder last said it was: a Stop that it has
    // not yet applied has already decided where the replacement starts, and the feeder stands a
    // ringful of audio ahead of the device. This publication drops that audio, so the replacement
    // starts where the device is, or the creator would not hear it.
    time::SampleFrame startsAt = pendingPlayhead_;
    if (!pendingPlayheadValid_) {
      const auto audible = audiblePlayhead();
      // Nothing has been sent or recorded: the render stays owed, and is published when it can be.
      if (!audible) {
        return core::failure(core::ErrorCode::Conflict,
                             "The place the creator is at could not be read, so the audio was not published");
      }
      startsAt = *audible;
    }
    remappedPlayhead = std::clamp<time::SampleFrame>(startsAt, 0, timeline.value()->endFrame());
    resumeAfterReconfigure = resumeAfterReconfigure_;
  }
  // The timeline, the loop that belongs to it, the playhead in it and, when a play is waiting for
  // audio, the play reach the feeder together or not at all: a publication that does not fit
  // leaves the feeder without a timeline that the controller never recorded.
  rendering::MultichannelPlaybackFeeder::ControlScript script;
  script.timeline(timeline.value()).loop(remappedLoop).seek(remappedPlayhead);
  if (resumeAfterReconfigure) script.playing(true);
  const auto sent = send(std::move(script));
  if (!sent) return sent;
  {
    std::lock_guard lock(stateMutex_);
    loop_ = remappedLoop;
    publishedRevision_ = audio->projectRevision;
    timelineEnd_ = timeline.value()->endFrame();
    pendingPlayheadValid_ = false;
    resumeAfterReconfigure_ = false;
    audioDroppedByReconfigure_ = false;
  }
  return core::success();
}

core::Result<void> TransportController::clearAudio() {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  {
    std::lock_guard lock(stateMutex_);
    // An empty timeline does not make a clean transport. A reconfigure empties the timeline but
    // keeps what the creator was doing (playing, looping, where the playhead was) for the audio
    // rendered next. That belongs to audio that is about to be declared gone, so it is dropped
    // with it. A play asked for while the transport held no audio, and none had been dropped by a
    // reconfigure, belongs to no audio: it stays armed, so a session that starts playing still
    // plays the first audio it is given, however many changes come before it.
    const bool holdsAudio = timelineEnd_ != time::SampleFrame{0};
    if (!holdsAudio && !audioDroppedByReconfigure_) return core::success();
  }
  auto timeline = std::make_shared<rendering::RoutedPlaybackTimeline>(config_.sampleRate);
  const auto configured =
      timeline->configure(authoringMasterRouting(config_.outputChannels), {});
  if (!configured) return configured;
  // Pause, empty, unloop, rewind: all of it or none. A clear that is refused has cleared nothing,
  // so the transport still holds what it held and the next clear tries the whole of it again.
  rendering::MultichannelPlaybackFeeder::ControlScript script;
  script.playing(false)
      .timeline(std::shared_ptr<const rendering::RoutedPlaybackTimeline>{std::move(timeline)})
      .loop({})
      .seek(0);
  const auto sent = send(std::move(script));
  if (!sent) return sent;
  {
    std::lock_guard lock(stateMutex_);
    loop_ = {};
    publishedRevision_ = 0U;
    timelineEnd_ = time::SampleFrame{0};
    pendingPlayheadValid_ = false;
    resumeAfterReconfigure_ = false;
    audioDroppedByReconfigure_ = false;
  }
  return core::success();
}

core::Result<void> TransportController::play() {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  if (!started_) {
    const auto started = service_->start();
    if (!started) return started;
    started_ = true;
  }
  rendering::MultichannelPlaybackFeeder::ControlScript script;
  {
    std::lock_guard lock(stateMutex_);
    // Play at the end of the audio plays it again from the start, as it does in any editor. Played
    // from where it stands it would end in the same instant: the creator would press Play and
    // nothing would sound. A loop never stands at the end, and with no audio there is nothing to
    // rewind, so a play that waits for audio stays what it was.
    const auto point = currentPoint();
    if (timelineEnd_ > time::SampleFrame{0} && !loop_.enabled && !point.playing &&
        point.playhead >= timelineEnd_) {
      script.seek(0);
    }
  }
  script.playing(true);
  const auto sent = send(std::move(script));
  if (!sent) return sent;
  std::lock_guard lock(stateMutex_);
  resumeAfterReconfigure_ = true;
  return core::success();
}

core::Result<void> TransportController::pause() {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  rendering::MultichannelPlaybackFeeder::ControlScript script;
  script.playing(false);
  {
    // The feeder drops the audio it has mixed ahead of the device when it pauses, and a play that
    // follows goes on from its playhead. From where it stands, that would skip the audio the
    // creator had not heard yet: the pause puts the playhead where they are.
    std::lock_guard lock(stateMutex_);
    const auto carried = carryAudiblePosition(script, false);
    if (!carried) return carried;
  }
  const auto sent = send(std::move(script));
  if (!sent) return sent;
  std::lock_guard lock(stateMutex_);
  resumeAfterReconfigure_ = false;
  return core::success();
}

core::Result<void> TransportController::stop() {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  // Pause and rewind reach the feeder together: a Stop that is refused has neither paused nor
  // rewound anything.
  rendering::MultichannelPlaybackFeeder::ControlScript script;
  script.playing(false).seek(0);
  const auto sent = send(std::move(script));
  if (!sent) return sent;
  std::lock_guard lock(stateMutex_);
  resumeAfterReconfigure_ = false;
  // A reconfigure keeps the playhead for the audio that follows it, and a Stop is what the
  // creator asked for last: that audio starts at the beginning. A Pause or a Play leaves the
  // saved position alone.
  if (pendingPlayheadValid_) pendingPlayhead_ = 0;
  return core::success();
}

core::Result<void> TransportController::seek(time::SampleFrame frame) {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  if (frame < 0) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Transport seek frame cannot be negative");
  }
  {
    std::lock_guard lock(stateMutex_);
    if (timelineEnd_ == 0 || frame > timelineEnd_) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Transport seek is outside the published audio timeline");
    }
  }
  rendering::MultichannelPlaybackFeeder::ControlScript script;
  script.seek(frame);
  return send(std::move(script));
}

core::Result<void> TransportController::setLoop(
    rendering::PlaybackLoop range) {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  if (range.enabled &&
      (range.startFrame < 0 || range.endFrame <= range.startFrame)) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Loop range must have a non-negative start and positive length");
  }
  {
    std::lock_guard lock(stateMutex_);
    if (range.enabled &&
        (timelineEnd_ == 0 || range.endFrame > timelineEnd_)) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Playback loop is outside the published audio timeline");
    }
  }
  rendering::MultichannelPlaybackFeeder::ControlScript script;
  script.loop(range);
  {
    // A loop change drops the audio the feeder has mixed ahead of the device as a pause does, and
    // playback goes on from the feeder's playhead.
    std::lock_guard lock(stateMutex_);
    const auto carried = carryAudiblePosition(script, true);
    if (!carried) return carried;
  }
  const auto result = send(std::move(script));
  if (result) {
    std::lock_guard lock(stateMutex_);
    loop_ = range;
  }
  return result;
}

core::Result<void> TransportController::awaitStartBuffer(std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  for (;;) {
    {
      // Held for one look at a time, never across the sleep: the render thread publishes through
      // the same lock.
      std::lock_guard lifecycleLock(lifecycleMutex_);
      std::size_t target = 0U;
      {
        std::lock_guard lock(stateMutex_);
        if (timelineEnd_ == time::SampleFrame{0}) {
          return core::failure(core::ErrorCode::Conflict,
                               "Playable audio became unavailable while buffering");
        }
        target = std::min({config_.watermarkFrames, ring_->capacityFrames(),
                           static_cast<std::size_t>(timelineEnd_)});
      }
      // Until the feeder has applied every command, what it asked the consumer to drop is not all
      // that it is going to ask, and what it reports is not what the creator asked for.
      if (feeder_->acknowledgedCommands() >= queuedCommands_) {
        static_cast<void>(ring_->serviceResetRequest());
        if (ring_->availableReadFrames() >= target) return core::success();
        // The feeder is not playing: it has finished the audio, or the creator paused. Nothing more
        // is coming, so waiting for the start buffer would only run the timeout out.
        if (!feeder_->playing()) return core::success();
      }
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      return core::failure(core::ErrorCode::Conflict,
                           "Audio playback did not reach its startup buffer before timeout");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
}

rendering::PlaybackPoint TransportController::currentPoint() const noexcept {
  if (queuedIntent_ && feeder_->acknowledgedCommands() < queuedIntent_->acknowledgedAt) {
    return queuedIntent_->point;
  }
  return rendering::PlaybackPoint{.playing = feeder_->playing(),
                                  .playhead = feeder_->playhead()};
}

std::optional<time::SampleFrame> TransportController::audiblePlayhead() const noexcept {
  const auto confirmed = [this](time::SampleFrame place) {
    lastAudiblePlayhead_ = place;
    return std::optional<time::SampleFrame>{place};
  };
  const bool pending = queuedIntent_ && feeder_->acknowledgedCommands() < queuedIntent_->acknowledgedAt;
  // A command that put the playhead somewhere is waiting for the feeder: playback goes on from
  // there, whatever the ring still holds of audio from before it.
  if (pending && queuedIntent_->positionIsExplicit) return confirmed(queuedIntent_->point.playhead);
  // Where playback goes on from when the ring holds nothing that is still to be heard. It is taken
  // before the ring is asked: what the feeder mixes after that, and the device then plays, is at
  // worst played again, so the answer is not later than the device in the order the audio is played
  // in (it can be a higher number, in a loop that wrapped in between).
  const auto playhead = pending ? queuedIntent_->point.playhead : feeder_->playhead();
  // A reset the device has not answered drops what the ring holds: that audio is not going to be
  // heard, and the feeder has put the playhead where playback goes on.
  if (ring_->resetPending()) return confirmed(playhead);
  const auto next = ring_->nextFrame();
  switch (next.kind) {
    case rendering::SpscInterleavedAudioRingBuffer::NextFrame::Kind::Placed:
      return confirmed(next.place);
    case rendering::SpscInterleavedAudioRingBuffer::NextFrame::Kind::Empty:
      return confirmed(playhead);
    // The ring has frames and cannot say where the device is among them. The feeder is ahead of it
    // by what they are, so its playhead is not a stand-in.
    case rendering::SpscInterleavedAudioRingBuffer::NextFrame::Kind::Unplaced:
    case rendering::SpscInterleavedAudioRingBuffer::NextFrame::Kind::Busy:
      break;
  }
  return std::nullopt;
}

core::Result<void> TransportController::carryAudiblePosition(
    rendering::MultichannelPlaybackFeeder::ControlScript& script, bool alsoWhenNotPlaying) const {
  const auto point = currentPoint();
  // A playing feeder that has audio goes on mixing until it applies the script, so that the
  // audible position being the feeder's own playhead now does not make it so when the script is
  // applied: the seek is always there, and the feeder puts its playhead where the creator was when
  // they asked. A feeder with no audio mixes nothing, and its playhead is not a place in audio.
  const bool mixesOn = point.playing && timelineEnd_ > time::SampleFrame{0};
  if (mixesOn || alsoWhenNotPlaying) {
    const auto audible = audiblePlayhead();
    if (!audible) {
      return core::failure(core::ErrorCode::Conflict,
                           "The place the creator is at could not be read, so nothing was changed");
    }
    if (mixesOn || *audible != point.playhead) script.seek(*audible);
  }
  if (stateSampleProbe_) stateSampleProbe_(*feeder_);
  return core::success();
}

core::Result<void> TransportController::send(
    rendering::MultichannelPlaybackFeeder::ControlScript script) {
  const auto count = static_cast<std::uint64_t>(script.size());
  if (count == 0U) return core::success();
  // Worked out before the script is sent: the feeder may consume it the moment it is queued.
  const auto projected = script.projectedFrom(currentPoint());
  // Commands that have not been applied yet and put the playhead somewhere keep doing so, whatever
  // a script that follows them leaves alone.
  const bool positionIsExplicit =
      script.seeks() || (queuedIntent_ && queuedIntent_->positionIsExplicit &&
                         feeder_->acknowledgedCommands() < queuedIntent_->acknowledgedAt);
  auto sent = feeder_->apply(std::move(script));
  if (!sent) return sent;
  queuedCommands_ += count;
  queuedIntent_ = QueuedIntent{.acknowledgedAt = queuedCommands_,
                               .point = projected,
                               .positionIsExplicit = positionIsExplicit};
  return core::success();
}

TransportState TransportController::state() const noexcept {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  std::lock_guard lock(stateMutex_);
  const auto probe = [this] {
    if (stateSampleProbe_) stateSampleProbe_(*feeder_);
  };
  // The feeder publishes its playing flag and its playhead and only then, with release, the count of
  // the commands they include. So the count is read first, with acquire: a sample that finds every
  // command applied then reads a playing flag and a playhead that include all of them (and anything
  // the feeder did since, such as reaching the end of the audio). Read last, the count could be newer
  // than the playing flag it is paired with, and a pause the feeder has just applied would be
  // reported as applied beside a flag that still says playing.
  probe();
  const auto acknowledged = feeder_->acknowledgedCommands();
  probe();
  const auto playing = feeder_->playing();
  probe();
  const auto playhead = feeder_->playhead();
  const auto audible = audiblePlayhead();
  return TransportState{
      .playing = playing,
      .available = timelineEnd_ > time::SampleFrame{0},
      .availabilityDiagnostic = timelineEnd_ == time::SampleFrame{0}
                                    ? "Render audio before starting transport"
                                    : std::string{},
      .playhead = playhead,
      .audiblePlayhead = audible.value_or(lastAudiblePlayhead_),
      .loop = loop_,
      .publishedRevision = publishedRevision_,
      .timelineEnd = timelineEnd_,
      .settled = acknowledged >= queuedCommands_,
  };
}

void TransportController::setStateSampleProbe(StateSampleProbe probe) {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  stateSampleProbe_ = std::move(probe);
}

}  // namespace seam::authoring
