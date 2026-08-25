#include "seam/authoring/transport_controller.hpp"

#include "seam/domain/routing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
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
  const bool wasPlaying = feeder_->playing();
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
  const auto remappedPlayhead = scaleFrame(feeder_->playhead());
  service_->stop();
  started_ = false;

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
  {
    std::lock_guard lock(stateMutex_);
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
  domain::ProjectRouting routing{
      .deviceOutputChannels = config_.outputChannels,
      .masterBus = masterId,
      .buses = {domain::AudioBus{.id = masterId,
                                 .name = "Authoring Master",
                                 .channelCount = config_.outputChannels}},
      .sends = {},
      .deviceRoutes = {domain::DeviceOutputRoute{
          .sourceBus = masterId,
          .matrix = domain::RoutingMatrix::identity(config_.outputChannels)}},
  };
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
    remappedPlayhead = std::clamp<time::SampleFrame>(
        pendingPlayheadValid_ ? pendingPlayhead_ : feeder_->playhead(), 0,
        timeline.value()->endFrame());
    resumeAfterReconfigure = resumeAfterReconfigure_;
  }
  const auto result = feeder_->setTimeline(timeline.value());
  if (!result) return result;
  const auto loopResult = feeder_->setLoop(remappedLoop);
  if (!loopResult) return loopResult;
  const auto seekResult = feeder_->seek(remappedPlayhead);
  if (!seekResult) return seekResult;
  if (resumeAfterReconfigure) {
    const auto resumeResult = feeder_->setPlaying(true);
    if (!resumeResult) return resumeResult;
  }
  {
    std::lock_guard lock(stateMutex_);
    loop_ = remappedLoop;
    publishedRevision_ = audio->projectRevision;
    timelineEnd_ = timeline.value()->endFrame();
    pendingPlayheadValid_ = false;
    resumeAfterReconfigure_ = false;
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
  {
    std::lock_guard lock(stateMutex_);
    resumeAfterReconfigure_ = true;
  }
  return feeder_->setPlaying(true);
}

core::Result<void> TransportController::pause() {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  {
    std::lock_guard lock(stateMutex_);
    resumeAfterReconfigure_ = false;
  }
  return feeder_->setPlaying(false);
}

core::Result<void> TransportController::stop() {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  {
    std::lock_guard lock(stateMutex_);
    resumeAfterReconfigure_ = false;
  }
  auto result = feeder_->setPlaying(false);
  if (!result) return result;
  return feeder_->seek(0);
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
  return feeder_->seek(frame);
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
  const auto result = feeder_->setLoop(range);
  if (result) {
    std::lock_guard lock(stateMutex_);
    loop_ = range;
  }
  return result;
}

TransportState TransportController::state() const noexcept {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  std::lock_guard lock(stateMutex_);
  return TransportState{
      .playing = feeder_->playing(),
      .available = timelineEnd_ > time::SampleFrame{0},
      .availabilityDiagnostic = timelineEnd_ == time::SampleFrame{0}
                                    ? "Render audio before starting transport"
                                    : std::string{},
      .playhead = feeder_->playhead(),
      .loop = loop_,
      .publishedRevision = publishedRevision_,
      .timelineEnd = timelineEnd_,
  };
}

}  // namespace seam::authoring
