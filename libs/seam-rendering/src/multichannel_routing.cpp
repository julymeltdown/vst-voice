#include "seam/rendering/multichannel_routing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace seam::rendering {
namespace {

float clipEdgeGain(const RoutedPlaybackClip& clip,
                   time::SampleFrame relativeFrame,
                   time::SampleFrame totalFrames) noexcept {
  float value = 1.0F;
  if (clip.fadeInFrames > 0 && relativeFrame < clip.fadeInFrames) {
    value *= std::clamp(static_cast<float>(relativeFrame + 1) /
                            static_cast<float>(clip.fadeInFrames + 1),
                        0.0F, 1.0F);
  }
  if (clip.fadeOutFrames > 0) {
    const auto remaining = totalFrames - relativeFrame;
    if (remaining <= clip.fadeOutFrames) {
      value *= std::clamp(static_cast<float>(remaining) /
                              static_cast<float>(clip.fadeOutFrames + 1),
                          0.0F, 1.0F);
    }
  }
  return value;
}

void applyMatrix(std::span<const float> source,
                 std::uint8_t sourceChannels,
                 std::span<float> destination,
                 std::uint8_t destinationChannels,
                 std::size_t frames,
                 const domain::RoutingMatrix& matrix,
                 float gain) noexcept {
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const auto sourceOffset = frame * sourceChannels;
    const auto destinationOffset = frame * destinationChannels;
    for (std::uint8_t destinationChannel = 0U;
         destinationChannel < destinationChannels; ++destinationChannel) {
      float mixed = 0.0F;
      for (std::uint8_t sourceChannel = 0U; sourceChannel < sourceChannels;
           ++sourceChannel) {
        mixed += source[sourceOffset + sourceChannel] *
                 matrix.gain(destinationChannel, sourceChannel);
      }
      destination[destinationOffset + destinationChannel] += mixed * gain;
    }
  }
}

std::unordered_set<domain::BusId> audibleBuses(
    const domain::ProjectRouting& routing) {
  std::unordered_set<domain::BusId> soloSeeds;
  for (const auto& bus : routing.buses) {
    if (bus.solo) soloSeeds.insert(bus.id);
  }
  if (soloSeeds.empty()) {
    std::unordered_set<domain::BusId> all;
    for (const auto& bus : routing.buses) all.insert(bus.id);
    return all;
  }

  // Solo keeps the selected bus, every upstream dependency that feeds it,
  // and every downstream bus needed to reach a device route. Sibling buses
  // that merely share a downstream master are intentionally not included.
  auto upstream = soloSeeds;
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& send : routing.sends) {
      if (!send.enabled || !upstream.contains(send.destinationBus) ||
          upstream.contains(send.sourceBus)) {
        continue;
      }
      upstream.insert(send.sourceBus);
      changed = true;
    }
  }

  auto downstream = soloSeeds;
  changed = true;
  while (changed) {
    changed = false;
    for (const auto& send : routing.sends) {
      if (!send.enabled || !downstream.contains(send.sourceBus) ||
          downstream.contains(send.destinationBus)) {
        continue;
      }
      downstream.insert(send.destinationBus);
      changed = true;
    }
  }

  upstream.insert(downstream.begin(), downstream.end());
  return upstream;
}

}  // namespace

core::Result<void> RoutedPcm::validate() const {
  if (sampleRate < 8000U || sampleRate > 384000U || channelCount == 0U ||
      channelCount > domain::kMaximumAudioChannels || startFrame < 0 ||
      interleavedSamples.empty() ||
      interleavedSamples.size() % channelCount != 0U) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Routed PCM metadata is invalid");
  }
  for (const auto sample : interleavedSamples) {
    if (!std::isfinite(sample)) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Routed PCM contains a non-finite sample");
    }
  }
  return core::success();
}

core::Result<RoutedPcm> RoutedPcm::fromMono(const CachedPcm& pcm) {
  RoutedPcm result{
      .sampleRate = pcm.sampleRate,
      .startFrame = pcm.startFrame,
      .channelCount = 1U,
      .interleavedSamples = pcm.samples,
  };
  const auto validation = result.validate();
  if (!validation) return core::Result<RoutedPcm>{validation.error()};
  return result;
}

core::Result<void> RoutingWorkspace::prepare(
    const domain::ProjectRouting& routing, std::size_t maximumFrames) {
  const auto validation = routing.validate();
  if (!validation) return validation;
  if (maximumFrames == 0U || maximumFrames > 1'048'576U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Routing workspace frame capacity is invalid");
  }
  buses_.clear();
  for (const auto& bus : routing.buses) {
    buses_.emplace(bus.id, BusBuffer{
        .channels = bus.channelCount,
        .samples = std::vector<float>(maximumFrames * bus.channelCount, 0.0F),
    });
  }
  maximumFrames_ = maximumFrames;
  return core::success();
}

void RoutingWorkspace::clear(std::size_t frames) noexcept {
  const auto clamped = std::min(frames, maximumFrames_);
  for (auto& [id, buffer] : buses_) {
    static_cast<void>(id);
    const auto count = clamped * buffer.channels;
    std::fill_n(buffer.samples.begin(),
                static_cast<std::ptrdiff_t>(count), 0.0F);
  }
}

std::span<float> RoutingWorkspace::busBuffer(domain::BusId bus,
                                              std::size_t frames) noexcept {
  const auto iterator = buses_.find(bus);
  if (iterator == buses_.end()) return {};
  const auto count = std::min(frames, maximumFrames_) * iterator->second.channels;
  return std::span<float>{iterator->second.samples}.first(count);
}

std::span<const float> RoutingWorkspace::busBuffer(
    domain::BusId bus, std::size_t frames) const noexcept {
  const auto iterator = buses_.find(bus);
  if (iterator == buses_.end()) return {};
  const auto count = std::min(frames, maximumFrames_) * iterator->second.channels;
  return std::span<const float>{iterator->second.samples}.first(count);
}

core::Result<void> RoutedPlaybackTimeline::validateClip(
    const RoutedPlaybackClip& clip) const {
  if (clip.id.empty() || clip.pcm == nullptr || !std::isfinite(clip.gain) ||
      std::abs(clip.gain) > 16.0F || clip.fadeInFrames < 0 ||
      clip.fadeOutFrames < 0) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Routed playback clip is invalid", clip.id);
  }
  const auto pcmValidation = clip.pcm->validate();
  if (!pcmValidation) return pcmValidation;
  if (clip.pcm->sampleRate != sampleRate_) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Routed playback clip sample rate does not match timeline",
                         clip.id);
  }
  const auto routeValidation = clip.outputRoute.validate();
  if (!routeValidation) return routeValidation;
  const auto* bus = routing_.findBus(clip.outputRoute.bus);
  if (bus == nullptr ||
      clip.outputRoute.matrix.sourceChannels != clip.pcm->channelCount ||
      clip.outputRoute.matrix.destinationChannels != bus->channelCount) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Routed playback clip matrix does not match source and destination channels",
                         clip.id);
  }
  return core::success();
}

core::Result<void> RoutedPlaybackTimeline::configure(
    domain::ProjectRouting routing, std::vector<RoutedPlaybackClip> clips) {
  if (sampleRate_ < 8000U || sampleRate_ > 384000U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Routed playback timeline sample rate is invalid");
  }
  const auto routingValidation = routing.validate();
  if (!routingValidation) return routingValidation;
  routing_ = std::move(routing);
  auto order = routing_.topologicalOrder();
  if (!order) return core::Result<void>{order.error()};
  topologicalOrder_ = std::move(order.value());
  for (const auto& clip : clips) {
    const auto validation = validateClip(clip);
    if (!validation) return validation;
  }
  std::stable_sort(clips.begin(), clips.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.pcm->startFrame == rhs.pcm->startFrame) return lhs.id < rhs.id;
    return lhs.pcm->startFrame < rhs.pcm->startFrame;
  });
  clips_ = std::move(clips);
  return core::success();
}

time::SampleFrame RoutedPlaybackTimeline::startFrame() const noexcept {
  auto result = std::numeric_limits<time::SampleFrame>::max();
  for (const auto& clip : clips_) {
    if (clip.enabled && clip.pcm != nullptr) {
      result = std::min(result, clip.pcm->startFrame);
    }
  }
  return result == std::numeric_limits<time::SampleFrame>::max() ? 0 : result;
}

time::SampleFrame RoutedPlaybackTimeline::endFrame() const noexcept {
  time::SampleFrame result = 0;
  for (const auto& clip : clips_) {
    if (!clip.enabled || clip.pcm == nullptr) continue;
    result = std::max(result, clip.pcm->startFrame + clip.pcm->frameCount());
  }
  return result;
}

core::Result<void> RoutedPlaybackTimeline::mix(
    time::SampleFrame startFrame, std::size_t frameCount,
    std::span<float> interleavedOutput,
    RoutingWorkspace& workspace) const noexcept {
  const auto outputChannels = routing_.deviceOutputChannels;
  if (frameCount == 0U || frameCount > workspace.maximumFrames() ||
      interleavedOutput.size() < frameCount * outputChannels) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Routed playback output buffer is too small");
  }
  workspace.clear(frameCount);
  std::fill_n(interleavedOutput.begin(),
              static_cast<std::ptrdiff_t>(frameCount * outputChannels), 0.0F);
  const auto outputEnd = startFrame + static_cast<time::SampleFrame>(frameCount);
  const auto audible = audibleBuses(routing_);
  const auto anyClipSolo = std::any_of(
      clips_.begin(), clips_.end(),
      [](const RoutedPlaybackClip& clip) { return clip.enabled && clip.solo; });

  for (const auto& clip : clips_) {
    if (!clip.enabled || clip.pcm == nullptr ||
        (anyClipSolo && !clip.solo) ||
        !audible.contains(clip.outputRoute.bus)) {
      continue;
    }
    const auto clipFrames = clip.pcm->frameCount();
    const auto clipStart = clip.pcm->startFrame;
    const auto clipEnd = clipStart + clipFrames;
    const auto overlapStart = std::max(startFrame, clipStart);
    const auto overlapEnd = std::min(outputEnd, clipEnd);
    if (overlapStart >= overlapEnd) continue;
    const auto* destinationBus = routing_.findBus(clip.outputRoute.bus);
    if (destinationBus == nullptr) continue;
    auto destination = workspace.busBuffer(destinationBus->id, frameCount);
    const auto sourceChannels = clip.pcm->channelCount;
    const auto destinationChannels = destinationBus->channelCount;
    for (auto frame = overlapStart; frame < overlapEnd; ++frame) {
      const auto outputFrame = static_cast<std::size_t>(frame - startFrame);
      const auto sourceFrame = frame - clipStart;
      const auto sourceOffset = static_cast<std::size_t>(sourceFrame) * sourceChannels;
      const auto destinationOffset = outputFrame * destinationChannels;
      const auto edge = clipEdgeGain(clip, sourceFrame, clipFrames);
      for (std::uint8_t destinationChannel = 0U;
           destinationChannel < destinationChannels; ++destinationChannel) {
        float mixed = 0.0F;
        for (std::uint8_t sourceChannel = 0U; sourceChannel < sourceChannels;
             ++sourceChannel) {
          mixed += clip.pcm->interleavedSamples[sourceOffset + sourceChannel] *
                   clip.outputRoute.matrix.gain(destinationChannel, sourceChannel);
        }
        destination[destinationOffset + destinationChannel] +=
            mixed * clip.gain * edge;
      }
    }
  }

  for (const auto busId : topologicalOrder_) {
    const auto* bus = routing_.findBus(busId);
    if (bus == nullptr) continue;
    auto source = workspace.busBuffer(busId, frameCount);
    const auto busGain = bus->muted || !audible.contains(busId)
                             ? 0.0F
                             : domain::decibelsToLinear(bus->gainDb);
    for (auto& sample : source) sample *= busGain;

    for (const auto& send : routing_.sends) {
      if (!send.enabled || send.sourceBus != busId ||
          !audible.contains(send.destinationBus)) {
        continue;
      }
      const auto* destinationBus = routing_.findBus(send.destinationBus);
      if (destinationBus == nullptr) continue;
      auto destination = workspace.busBuffer(send.destinationBus, frameCount);
      applyMatrix(source, bus->channelCount, destination,
                  destinationBus->channelCount, frameCount, send.matrix,
                  domain::decibelsToLinear(send.gainDb));
    }
  }

  auto output = interleavedOutput.first(frameCount * outputChannels);
  for (const auto& route : routing_.deviceRoutes) {
    if (!route.enabled || !audible.contains(route.sourceBus)) continue;
    const auto* sourceBus = routing_.findBus(route.sourceBus);
    if (sourceBus == nullptr) continue;
    const auto source = workspace.busBuffer(route.sourceBus, frameCount);
    applyMatrix(source, sourceBus->channelCount, output, outputChannels,
                frameCount, route.matrix,
                domain::decibelsToLinear(route.gainDb));
  }
  for (auto& sample : output) {
    sample = std::isfinite(sample) ? std::clamp(sample, -4.0F, 4.0F) : 0.0F;
  }
  return core::success();
}

}  // namespace seam::rendering
