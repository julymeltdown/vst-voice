#include "seam/rendering/project_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace seam::rendering {
namespace {
constexpr std::uint64_t kMaximumOutputBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMixBlockFrames = 4096U;

const TrackVoicebankSource* sourceFor(
    std::span<const TrackVoicebankSource> sources,
    domain::TrackId trackId) noexcept {
  const auto iterator = std::find_if(
      sources.begin(), sources.end(), [trackId](const auto& value) {
        return value.trackId == trackId;
      });
  return iterator == sources.end() ? nullptr : &*iterator;
}

}  // namespace

core::Result<ProjectRenderResult> ProductionProjectRenderer::render(
    const domain::Project& project,
    std::span<const TrackVoicebankSource> voicebanks,
    domain::TrackId activeTrack,
    domain::RegionId activeRegion,
    std::uint64_t revision,
    std::uint32_t sampleRate,
    RenderQuality quality,
    const synthesis::PhraseRenderOptions& options,
    PcmCache* cache,
    std::stop_token stopToken) const {
  if (sampleRate < 8000U || sampleRate > 192000U) {
    return core::failure<ProjectRenderResult>(
        core::ErrorCode::InvalidArgument,
        "Project render sample rate is unsupported");
  }
  const auto projectValidation = project.validate();
  if (!projectValidation) {
    return core::Result<ProjectRenderResult>{projectValidation.error()};
  }

  const auto anySolo = std::any_of(
      project.vocalTracks().begin(), project.vocalTracks().end(),
      [](const domain::VocalTrack& track) { return track.solo && !track.muted; });
  std::vector<RoutedPlaybackClip> clips;
  ProjectRenderResult output;
  output.sampleRate = sampleRate;
  output.channelCount = project.routing().deviceOutputChannels;
  ProductionRegionRenderer renderer;

  for (const auto& track : project.vocalTracks()) {
    if (stopToken.stop_requested()) {
      return core::failure<ProjectRenderResult>(core::ErrorCode::Conflict,
                                                "Project render was cancelled");
    }
    if (track.muted || (anySolo && !track.solo)) continue;
    const auto* source = sourceFor(voicebanks, track.id);
    if (source == nullptr) {
      return core::failure<ProjectRenderResult>(
          core::ErrorCode::NotFound,
          "No resolved Voicebank is available for an audible vocal track",
          track.id.toString());
    }
    ++output.trackCount;
    for (const auto& region : track.regions) {
      if (region.notes.empty()) continue;
      auto rendered = renderer.render(
          project, source->manifest, source->bankRoot, track.id, region.id,
          revision, sampleRate, quality,
          source->manifest.styles.empty() ? std::string{}
                                          : source->manifest.styles.front(),
          options, cache, stopToken);
      if (!rendered) return core::Result<ProjectRenderResult>{rendered.error()};

      auto pcm = std::make_shared<RoutedPcm>();
      pcm->sampleRate = sampleRate;
      pcm->startFrame = 0;
      pcm->channelCount = 1U;
      pcm->interleavedSamples = rendered.value().mono;
      const auto pcmValidation = pcm->validate();
      if (!pcmValidation) {
        return core::Result<ProjectRenderResult>{pcmValidation.error()};
      }
      clips.push_back(RoutedPlaybackClip{
          .id = track.id.toString() + ":" + region.id.toString(),
          .pcm = std::move(pcm),
          .outputRoute = track.outputRoute,
          .gain = domain::decibelsToLinear(track.gainDb),
          .fadeInFrames = 0,
          .fadeOutFrames = 0,
          .enabled = true,
          .solo = track.solo,
      });
      ++output.regionCount;
      output.phraseCount += rendered.value().phrases.size();
      output.unitCount += rendered.value().unitCount;
      output.fallbackCount += rendered.value().fallbackCount;
      output.cacheHits += rendered.value().cacheHits;
      for (const auto& phrase : rendered.value().phrases) {
        output.phraseContentHashes.push_back(phrase.contentHash);
      }
      if (track.id == activeTrack && region.id == activeRegion) {
        output.activeUnitPlan = rendered.value().unitPlan;
      }
    }
  }
  if (clips.empty()) {
    return core::failure<ProjectRenderResult>(core::ErrorCode::NotFound,
                                              "Project has no audible rendered vocal region");
  }

  RoutedPlaybackTimeline timeline{sampleRate};
  auto configured = timeline.configure(project.routing(), std::move(clips));
  if (!configured) return core::Result<ProjectRenderResult>{configured.error()};
  const auto endFrame = timeline.endFrame();
  if (endFrame <= 0) {
    return core::failure<ProjectRenderResult>(core::ErrorCode::NotFound,
                                              "Project routing produced no output frames");
  }
  const auto frames = static_cast<std::uint64_t>(endFrame);
  const auto channels = static_cast<std::uint64_t>(output.channelCount);
  if (frames > std::numeric_limits<std::size_t>::max() / channels ||
      frames * channels > kMaximumOutputBytes / sizeof(float)) {
    return core::failure<ProjectRenderResult>(
        core::ErrorCode::Unsupported,
        "Project preview exceeds the bounded 512 MiB PCM publication limit");
  }
  output.interleaved.assign(static_cast<std::size_t>(frames * channels), 0.0F);
  RoutingWorkspace workspace;
  const auto prepared = workspace.prepare(project.routing(), kMixBlockFrames);
  if (!prepared) return core::Result<ProjectRenderResult>{prepared.error()};
  for (std::uint64_t offset = 0U; offset < frames;) {
    if (stopToken.stop_requested()) {
      return core::failure<ProjectRenderResult>(core::ErrorCode::Conflict,
                                                "Project render was cancelled");
    }
    const auto count = static_cast<std::size_t>(
        std::min<std::uint64_t>(kMixBlockFrames, frames - offset));
    auto destination = std::span<float>{output.interleaved}.subspan(
        static_cast<std::size_t>(offset * channels), count * output.channelCount);
    const auto mixed = timeline.mix(static_cast<time::SampleFrame>(offset), count,
                                    destination, workspace);
    if (!mixed) return core::Result<ProjectRenderResult>{mixed.error()};
    offset += count;
  }
  if (std::any_of(output.interleaved.begin(), output.interleaved.end(),
                  [](float value) { return !std::isfinite(value); })) {
    return core::failure<ProjectRenderResult>(
        core::ErrorCode::InvariantViolation,
        "Project render contains non-finite multichannel PCM");
  }
  return output;
}

}  // namespace seam::rendering
