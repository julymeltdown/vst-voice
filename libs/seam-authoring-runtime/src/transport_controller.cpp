#include "seam/authoring/transport_controller.hpp"

#include "seam/domain/routing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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

}  // namespace

TransportController::TransportController(TransportConfig config)
    : config_(config),
      ring_(config.ringCapacityFrames, config.outputChannels),
      feeder_(ring_, config.sampleRate, config.outputChannels,
              config.blockFrames),
      service_(feeder_, config.watermarkFrames) {}

TransportController::~TransportController() { shutdown(); }

core::Result<void> TransportController::start() {
  if (started_) return core::success();
  const auto result = service_.start();
  if (result) started_ = true;
  return result;
}

void TransportController::shutdown() noexcept {
  service_.stop();
  started_ = false;
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
  if (!audio) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Published authoring audio handle is empty");
  }
  {
    std::lock_guard lock(stateMutex_);
    if (audio->projectRevision < publishedRevision_) {
      return core::failure(core::ErrorCode::Conflict,
                           "An older render revision cannot replace current audio");
    }
  }
  bool crossfade = false;
  {
    std::lock_guard lock(stateMutex_);
    crossfade = publishedRevision_ != 0U;
  }
  auto timeline = makeTimeline(*audio, crossfade);
  if (!timeline) return core::Result<void>{timeline.error()};
  const auto result = feeder_.setTimeline(timeline.value());
  if (!result) return result;
  {
    std::lock_guard lock(stateMutex_);
    publishedRevision_ = audio->projectRevision;
    timelineEnd_ = timeline.value()->endFrame();
  }
  return core::success();
}

core::Result<void> TransportController::play() {
  if (!started_) {
    const auto started = start();
    if (!started) return started;
  }
  return feeder_.setPlaying(true);
}

core::Result<void> TransportController::pause() {
  return feeder_.setPlaying(false);
}

core::Result<void> TransportController::stop() {
  auto result = feeder_.setPlaying(false);
  if (!result) return result;
  return feeder_.seek(0);
}

core::Result<void> TransportController::seek(time::SampleFrame frame) {
  if (frame < 0) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Transport seek frame cannot be negative");
  }
  return feeder_.seek(frame);
}

core::Result<void> TransportController::setLoop(
    rendering::PlaybackLoop range) {
  if (range.enabled &&
      (range.startFrame < 0 || range.endFrame <= range.startFrame)) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Loop range must have a non-negative start and positive length");
  }
  const auto result = feeder_.setLoop(range);
  if (result) {
    std::lock_guard lock(stateMutex_);
    loop_ = range;
  }
  return result;
}

TransportState TransportController::state() const noexcept {
  std::lock_guard lock(stateMutex_);
  return TransportState{
      .playing = feeder_.playing(),
      .playhead = feeder_.playhead(),
      .loop = loop_,
      .publishedRevision = publishedRevision_,
      .timelineEnd = timelineEnd_,
  };
}

}  // namespace seam::authoring
