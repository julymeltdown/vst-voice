#include "seam/rendering/region_renderer.hpp"

#include "seam/rendering/phrase_segmenter.hpp"
#include "seam/rendering/render_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace seam::rendering {
namespace {

constexpr std::uint64_t kMaximumRegionFrames = 384000ULL * 600ULL;

core::Result<void> mixPhrase(RegionRenderResult& output,
                             time::SampleFrame startFrame,
                             const std::vector<float>& samples) {
  if (samples.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Production region renderer received empty phrase PCM");
  }
  std::size_t sourceOffset = 0U;
  std::uint64_t destination = 0U;
  if (startFrame < 0) {
    const auto clipped = static_cast<std::uint64_t>(-startFrame);
    if (clipped >= samples.size()) return core::success();
    sourceOffset = static_cast<std::size_t>(clipped);
  } else {
    destination = static_cast<std::uint64_t>(startFrame);
  }
  const auto remaining = static_cast<std::uint64_t>(samples.size() - sourceOffset);
  if (destination > kMaximumRegionFrames ||
      remaining > kMaximumRegionFrames - destination) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Production region render exceeds ten minutes");
  }
  const auto required = static_cast<std::size_t>(destination + remaining);
  if (output.mono.size() < required) output.mono.resize(required, 0.0F);
  for (std::size_t index = sourceOffset; index < samples.size(); ++index) {
    const auto target = static_cast<std::size_t>(destination) + index - sourceOffset;
    output.mono[target] = std::clamp(output.mono[target] + samples[index],
                                     -1.0F, 1.0F);
  }
  return core::success();
}

}  // namespace

core::Result<RegionRenderResult> ProductionRegionRenderer::render(
    const domain::Project& project,
    const voicebank::Manifest& manifest,
    const std::filesystem::path& bankRoot,
    domain::TrackId trackId,
    domain::RegionId regionId,
    std::uint64_t revision,
    std::uint32_t sampleRate,
    RenderQuality quality,
    std::string style,
    const synthesis::PhraseRenderOptions& options,
    PcmCache* cache,
    std::stop_token stopToken) const {
  if (sampleRate < 8000U || sampleRate > 192000U) {
    return core::failure<RegionRenderResult>(core::ErrorCode::InvalidArgument,
                                             "Region render sample rate is unsupported");
  }
  const auto* track = project.findVocalTrack(trackId);
  const auto* region = track == nullptr ? nullptr : track->findRegion(regionId);
  if (track == nullptr || region == nullptr) {
    return core::failure<RegionRenderResult>(core::ErrorCode::NotFound,
                                             "Region renderer track or region is missing");
  }
  PhraseSegmenter segmenter;
  auto segments = segmenter.segment(*region);
  if (!segments) return core::Result<RegionRenderResult>{segments.error()};
  if (segments.value().empty()) {
    return core::failure<RegionRenderResult>(core::ErrorCode::NotFound,
                                             "Region has no renderable phrase");
  }

  RegionRenderResult output;
  output.sampleRate = sampleRate;
  RenderSnapshotFactory snapshots;
  PhraseRenderPipeline pipeline;
  for (const auto& segment : segments.value()) {
    if (stopToken.stop_requested()) {
      return core::failure<RegionRenderResult>(core::ErrorCode::Conflict,
                                               "Region render was cancelled");
    }
    auto snapshot = snapshots.create(project, manifest, trackId, segment,
                                     revision, quality, bankRoot, sampleRate,
                                     style, options);
    if (!snapshot) return core::Result<RegionRenderResult>{snapshot.error()};

    std::shared_ptr<const CachedPcm> cached;
    if (cache != nullptr) {
      auto loaded = cache->load(snapshot.value().contentHash);
      if (loaded) {
        cached = std::move(loaded).value();
      } else if (loaded.error().code != core::ErrorCode::NotFound) {
        return core::Result<RegionRenderResult>{loaded.error()};
      }
    }

    RegionRenderPhraseInfo info{
        .phraseId = segment.id,
        .contentHash = snapshot.value().contentHash,
        .unitCount = snapshot.value().unitPlan->entries.size(),
        .fallbackCount = 0U,
        .cacheHit = cached != nullptr,
    };
    output.unitCount += info.unitCount;
    output.unitPlan.insert(output.unitPlan.end(),
                           snapshot.value().unitPlan->entries.begin(),
                           snapshot.value().unitPlan->entries.end());
    if (cached != nullptr) {
      if (cached->sampleRate != sampleRate) {
        return core::failure<RegionRenderResult>(
            core::ErrorCode::Conflict,
            "PCM cache entry sample rate differs from render request");
      }
      const auto mixed = mixPhrase(output, cached->startFrame, cached->samples);
      if (!mixed) return core::Result<RegionRenderResult>{mixed.error()};
      ++output.cacheHits;
      output.phrases.push_back(std::move(info));
      continue;
    }

    auto rendered = pipeline.render(snapshot.value(), stopToken);
    if (!rendered) return core::Result<RegionRenderResult>{rendered.error()};
    info.fallbackCount = static_cast<std::size_t>(std::count_if(
        rendered.value().rendered.placements.begin(),
        rendered.value().rendered.placements.end(),
        [](const auto& placement) { return placement.usedFallback; }));
    output.fallbackCount += info.fallbackCount;
    const auto& audio = rendered.value().rendered.audio;
    const auto mixed = mixPhrase(output, audio.startFrame, audio.samples);
    if (!mixed) return core::Result<RegionRenderResult>{mixed.error()};
    if (cache != nullptr) {
      const auto stored = cache->store(snapshot.value().contentHash,
                                       CachedPcm{.sampleRate = sampleRate,
                                                 .startFrame = audio.startFrame,
                                                 .samples = audio.samples});
      if (!stored) return core::Result<RegionRenderResult>{stored.error()};
    }
    output.phrases.push_back(std::move(info));
  }
  if (output.mono.empty()) {
    return core::failure<RegionRenderResult>(core::ErrorCode::NotFound,
                                             "Production region render produced no PCM");
  }
  if (std::any_of(output.mono.begin(), output.mono.end(),
                  [](float value) { return !std::isfinite(value); })) {
    return core::failure<RegionRenderResult>(core::ErrorCode::InvariantViolation,
                                             "Production region render contains non-finite PCM");
  }
  return output;
}

}  // namespace seam::rendering
