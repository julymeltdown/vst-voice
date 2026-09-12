#include "seam/rendering/project_renderer.hpp"

#include "seam/rendering/sample_rate_converter.hpp"
#include "seam/rendering/streaming_pcm_source.hpp"
#include "seam/rendering/render_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <span>

namespace seam::rendering {
namespace {
constexpr std::uint64_t kMaximumOutputBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMixBlockFrames = 4096U;

const TrackSingerSource* sourceFor(
    std::span<const TrackSingerSource> sources,
    domain::TrackId trackId) noexcept {
  const auto iterator = std::find_if(
      sources.begin(), sources.end(), [trackId](const auto& value) {
        return std::visit([trackId](const auto& source) { return source.trackId == trackId; }, value);
      });
  return iterator == sources.end() ? nullptr : &*iterator;
}

domain::TrackOutputRoute routeForTrack(const domain::TrackOutputRoute& route,
                                       float pan) {
  auto result = route;
  if (result.matrix.sourceChannels == 1U &&
      result.matrix.destinationChannels == 2U) {
    result.matrix = domain::RoutingMatrix::monoToStereo(pan);
  }
  return result;
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
  std::vector<TrackSingerSource> sources;
  sources.reserve(voicebanks.size());
  for (const auto& source : voicebanks) sources.emplace_back(source);
  return renderWithSources(project, sources, activeTrack, activeRegion, revision,
      sampleRate, quality, options, cache, stopToken);
}

core::Result<ProjectRenderResult> ProductionProjectRenderer::renderWithSources(
    const domain::Project& project, std::span<const TrackSingerSource> sources,
    domain::TrackId activeTrack, domain::RegionId activeRegion, std::uint64_t revision,
    std::uint32_t sampleRate, RenderQuality quality,
    const synthesis::PhraseRenderOptions& options, PcmCache* cache, std::stop_token stopToken) const {
  if (sources.size() > project.vocalTracks().size()) return core::failure<ProjectRenderResult>(
      core::ErrorCode::Conflict, "Singer source count exceeds project vocal tracks");
  if (std::any_of(sources.begin(), sources.end(), [](const auto& source) {
        return std::holds_alternative<TrackRecipeFileSource>(source);
      })) {
    std::vector<TrackSingerSource> resolved;
    resolved.reserve(sources.size());
    for (const auto& source : sources) {
      if (stopToken.stop_requested()) return core::failure<ProjectRenderResult>(core::ErrorCode::Conflict, "Recipe resolution cancelled");
      const auto* file = std::get_if<TrackRecipeFileSource>(&source);
      if (!file) { resolved.push_back(source); continue; }
      const auto* track = project.findVocalTrack(file->trackId);
      if (!track) return core::failure<ProjectRenderResult>(core::ErrorCode::NotFound, "Recipe source track is missing");
      const auto solo = std::any_of(project.vocalTracks().begin(), project.vocalTracks().end(),
          [](const auto& value) { return value.solo && !value.muted; }) ||
          std::any_of(project.audioTracks().begin(), project.audioTracks().end(),
          [](const auto& value) { return value.solo && !value.muted; });
      if (track->muted || (solo && !track->solo)) continue;
      const auto valid = file->reference.validate();
      if (!valid) return core::Result<ProjectRenderResult>{valid.error()};
      auto path = std::filesystem::path{file->reference.path};
      if (path.is_relative()) {
        if (!file->projectDirectory || !file->projectDirectory->is_absolute()) return core::failure<ProjectRenderResult>(
            core::ErrorCode::NotFound, "Relative recipe reference requires a saved project directory");
        path = *file->projectDirectory / path;
      }
      const auto resource = voice_design::loadVoiceRecipeResource(path.lexically_normal(), file->reference.resource, stopToken);
      if (!resource) return core::Result<ProjectRenderResult>{resource.error()};
      resolved.emplace_back(TrackProceduralSource{file->trackId, resource.value(), file->reference.style});
    }
    return renderWithSources(project, resolved, activeTrack, activeRegion, revision, sampleRate, quality, options, cache, stopToken);
  }
  std::vector<domain::TrackId> sourceTracks;
  for (const auto& source : sources) {
    const auto id = std::visit([](const auto& value) { return value.trackId; }, source);
    if (!project.findVocalTrack(id) || std::find(sourceTracks.begin(), sourceTracks.end(), id) != sourceTracks.end()) {
      return core::failure<ProjectRenderResult>(core::ErrorCode::Conflict, "Singer sources contain an unknown or duplicate track");
    }
    sourceTracks.push_back(id);
  }
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
      [](const domain::VocalTrack& track) { return track.solo && !track.muted; }) ||
      std::any_of(project.audioTracks().begin(), project.audioTracks().end(),
                  [](const domain::AudioTrack& track) {
                    return track.solo && !track.muted;
                  });
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
    const auto* source = sourceFor(sources, track.id);
    if (source == nullptr) {
      return core::failure<ProjectRenderResult>(
          core::ErrorCode::NotFound,
          "No resolved singer resource is available for an audible vocal track",
          track.id.toString());
    }
    ++output.trackCount;
    if (track.proceduralRecipe) {
      const auto* procedural = std::get_if<TrackProceduralSource>(source);
      if (!procedural || procedural->resource.identity != track.proceduralRecipe->resource ||
          procedural->style != track.proceduralRecipe->style) return core::failure<ProjectRenderResult>(
          core::ErrorCode::Conflict, "Resolved singer differs from the saved procedural recipe selection");
    }
    for (const auto& region : track.regions) {
      if (region.notes.empty()) continue;
      if (const auto* procedural = std::get_if<TrackProceduralSource>(source)) {
        const auto snapshot = RenderSnapshotFactory{}.createProcedural(project, procedural->resource,
            track.id, region.id, revision, quality, sampleRate, procedural->style);
        if (!snapshot) return core::Result<ProjectRenderResult>{snapshot.error()};
        auto rendered = PhraseRenderPipeline{}.render(snapshot.value(), stopToken);
        if (!rendered) return core::Result<ProjectRenderResult>{rendered.error()};
        auto pcm = std::make_shared<RoutedPcm>();
        pcm->sampleRate = sampleRate;
        pcm->startFrame = rendered.value().rendered.audio.startFrame;
        pcm->channelCount = 1U;
        pcm->interleavedSamples = std::move(rendered.value().rendered.audio.samples);
        const auto valid = pcm->validate();
        if (!valid) return core::Result<ProjectRenderResult>{valid.error()};
        clips.push_back(RoutedPlaybackClip{
            .id = track.id.toString() + ":" + region.id.toString(), .pcm = std::move(pcm),
            .outputRoute = routeForTrack(track.outputRoute, track.pan),
            .gain = domain::decibelsToLinear(track.gainDb), .fadeInFrames = 0, .fadeOutFrames = 0,
            .enabled = true, .solo = track.solo});
        ++output.regionCount; ++output.phraseCount;
        output.phraseContentHashes.push_back(snapshot.value().contentHash);
        continue;
      }
      const auto& sample = std::get<TrackVoicebankSource>(*source);
      auto rendered = renderer.render(
          project, sample.manifest, sample.bankRoot, track.id, region.id,
          revision, sampleRate, quality,
          std::string{},
          options, cache, stopToken, true);
      if (!rendered) return core::Result<ProjectRenderResult>{rendered.error()};
      for (const auto& failure : rendered.value().failures) {
        output.diagnostics.push_back(ProjectRenderDiagnostic{
            .trackId = track.id,
            .regionId = region.id,
            .phraseId = failure.phraseId,
            .code = failure.code,
            .message = failure.message,
            .context = failure.context,
        });
      }
      if (rendered.value().mono.empty()) {
        continue;
      }

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
          .outputRoute = routeForTrack(track.outputRoute, track.pan),
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

  for (const auto& track : project.audioTracks()) {
    if (stopToken.stop_requested()) {
      return core::failure<ProjectRenderResult>(core::ErrorCode::Conflict,
                                                "Project render was cancelled");
    }
    if (track.muted || (anySolo && !track.solo)) continue;
    if (track.mediaPath.empty()) {
      output.diagnostics.push_back(ProjectRenderDiagnostic{
          .trackId = track.id,
          .regionId = domain::RegionId{},
          .phraseId = {},
          .code = core::ErrorCode::NotFound,
          .message = "Audio track has no backing media",
          .context = track.id.toString(),
      });
      continue;
    }
    auto source = StreamingPcmSource::open(
        std::filesystem::path{track.mediaPath}, kMixBlockFrames);
    if (!source) {
      output.diagnostics.push_back(ProjectRenderDiagnostic{
          .trackId = track.id,
          .regionId = domain::RegionId{},
          .phraseId = {},
          .code = source.error().code,
          .message = source.error().message,
          .context = source.error().context,
      });
      continue;
    }
    if (!track.mediaHash.empty() &&
        source.value()->info().contentHash != track.mediaHash) {
      output.diagnostics.push_back(ProjectRenderDiagnostic{
          .trackId = track.id,
          .regionId = domain::RegionId{},
          .phraseId = {},
          .code = core::ErrorCode::Conflict,
          .message = "Backing media content hash does not match the project identity",
          .context = track.mediaHash,
      });
      continue;
    }
    const auto sourceFrameCount = source.value()->info().frameCount;
    const auto trimStart = track.trimStartFrame;
    const auto trimEnd = track.trimEndFrame.value_or(sourceFrameCount);
    if (trimStart >= trimEnd || trimEnd > sourceFrameCount ||
        (track.sourceFrameCount != 0U &&
         track.sourceFrameCount != sourceFrameCount)) {
      output.diagnostics.push_back(ProjectRenderDiagnostic{
          .trackId = track.id,
          .regionId = domain::RegionId{},
          .phraseId = {},
          .code = core::ErrorCode::Conflict,
          .message = "Backing media trim or source identity is invalid",
          .context = track.id.toString(),
      });
      continue;
    }
    if (source.value()->info().frameCount >
            std::numeric_limits<std::size_t>::max() ||
        source.value()->info().frameCount > kMaximumOutputBytes / sizeof(float)) {
      output.diagnostics.push_back(ProjectRenderDiagnostic{
          .trackId = track.id,
          .regionId = domain::RegionId{},
          .phraseId = {},
          .code = core::ErrorCode::Unsupported,
          .message = "Backing media exceeds the bounded preview PCM limit",
          .context = track.id.toString(),
      });
      continue;
    }
    const auto qualityMode = quality == RenderQuality::Final
                                 ? SampleRateQuality::Final
                                 : SampleRateQuality::Preview;
    StreamingSampleRateConverter converter(source.value()->info().sampleRate,
                                           sampleRate, qualityMode);
    std::vector<float> samples;
    const auto estimatedFrames = static_cast<std::uint64_t>(std::llround(
        static_cast<long double>(trimEnd - trimStart) * sampleRate /
        source.value()->info().sampleRate));
    if (estimatedFrames > kMaximumOutputBytes / sizeof(float)) {
      output.diagnostics.push_back(ProjectRenderDiagnostic{
          .trackId = track.id,
          .regionId = domain::RegionId{},
          .phraseId = {},
          .code = core::ErrorCode::Unsupported,
          .message = "Resampled backing media exceeds the bounded preview limit",
          .context = track.id.toString(),
      });
      continue;
    }
    samples.reserve(static_cast<std::size_t>(estimatedFrames));
    bool readFailed = false;
    const auto firstChunk = static_cast<std::size_t>(
        trimStart / source.value()->chunkFrames());
    const auto lastChunk = static_cast<std::size_t>(
        (trimEnd - 1U) / source.value()->chunkFrames());
    for (std::size_t chunkIndex = firstChunk; chunkIndex <= lastChunk;
         ++chunkIndex) {
      if (stopToken.stop_requested()) {
        return core::failure<ProjectRenderResult>(core::ErrorCode::Conflict,
                                                  "Project render was cancelled");
      }
      auto chunk = source.value()->readChunk(chunkIndex);
      if (!chunk) {
        output.diagnostics.push_back(ProjectRenderDiagnostic{
            .trackId = track.id,
            .regionId = domain::RegionId{},
            .phraseId = {},
            .code = chunk.error().code,
            .message = chunk.error().message,
            .context = chunk.error().context,
        });
        samples.clear();
        readFailed = true;
        break;
      }
      const auto channels = static_cast<std::size_t>(source.value()->info().channels);
      const auto chunkFirstFrame = static_cast<std::uint64_t>(chunkIndex) *
                                   source.value()->chunkFrames();
      const auto chunkFrameCount = chunk.value().size() / channels;
      const auto selectedStart = std::max(trimStart, chunkFirstFrame);
      const auto selectedEnd = std::min(
          trimEnd, chunkFirstFrame + static_cast<std::uint64_t>(chunkFrameCount));
      if (selectedStart >= selectedEnd) continue;
      std::vector<float> mono;
      const auto localStart = static_cast<std::size_t>(selectedStart - chunkFirstFrame);
      const auto localEnd = static_cast<std::size_t>(selectedEnd - chunkFirstFrame);
      mono.reserve(localEnd - localStart);
      for (std::size_t frame = localStart; frame < localEnd; ++frame) {
        double sum = 0.0;
        for (std::size_t channel = 0U; channel < channels; ++channel) {
          sum += chunk.value()[frame * channels + channel];
        }
        mono.push_back(static_cast<float>(sum / static_cast<double>(channels)));
      }
      auto convertedChunk = converter.append(mono);
      if (!convertedChunk) {
        output.diagnostics.push_back(ProjectRenderDiagnostic{
            .trackId = track.id,
            .regionId = domain::RegionId{},
            .phraseId = {},
            .code = convertedChunk.error().code,
            .message = convertedChunk.error().message,
            .context = convertedChunk.error().context,
        });
        samples.clear();
        readFailed = true;
        break;
      }
      samples.insert(samples.end(), convertedChunk.value().begin(),
                     convertedChunk.value().end());
    }
    if (readFailed) continue;
    auto tail = converter.finish();
    if (!tail) {
      output.diagnostics.push_back(ProjectRenderDiagnostic{
          .trackId = track.id,
          .regionId = domain::RegionId{},
          .phraseId = {},
          .code = tail.error().code,
          .message = tail.error().message,
          .context = tail.error().context,
      });
      continue;
    }
    samples.insert(samples.end(), tail.value().begin(), tail.value().end());
    if (samples.empty()) continue;
    auto pcm = std::make_shared<RoutedPcm>();
    pcm->sampleRate = sampleRate;
    pcm->startFrame = project.tempoMap().sampleFrameAt(track.startTick,
                                                       sampleRate);
    pcm->channelCount = 1U;
    pcm->interleavedSamples = std::move(samples);
    const auto pcmValidation = pcm->validate();
    if (!pcmValidation) return core::Result<ProjectRenderResult>{pcmValidation.error()};
    clips.push_back(RoutedPlaybackClip{
        .id = track.id.toString() + ":media",
        .pcm = std::move(pcm),
        .outputRoute = routeForTrack(track.outputRoute, track.pan),
        .gain = domain::decibelsToLinear(track.gainDb),
        .fadeInFrames = 0,
        .fadeOutFrames = 0,
        .enabled = true,
        .solo = track.solo,
    });
    ++output.trackCount;
  }
  if (clips.empty()) {
    if (!output.diagnostics.empty()) {
      const auto& first = output.diagnostics.front();
      return core::failure<ProjectRenderResult>(
          first.code, "Project has no audible rendered tracks",
          first.context.empty() ? first.message
                                : first.context + ": " + first.message);
    }
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
