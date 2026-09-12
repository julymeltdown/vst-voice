#include "seam/rendering/region_renderer.hpp"

#include "seam/rendering/phrase_segmenter.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/synthesis/performance_compiler.hpp"

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
    const auto clipped = std::uint64_t{0} - static_cast<std::uint64_t>(startFrame);
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

std::string rendererIdentity(
    std::span<const synthesis::RenderedPlacementInfo> placements) {
  if (placements.empty()) return "unknown";
  const auto first = voicebank::rendererHintName(placements.front().actualRenderer);
  const auto same = std::all_of(
      placements.begin(), placements.end(), [first](const auto& placement) {
        return voicebank::rendererHintName(placement.actualRenderer) == first;
      });
  if (!same) return "mixed";
  return std::string{first};
}

std::string fallbackDiagnostic(
    std::span<const synthesis::RenderedPlacementInfo> placements) {
  for (const auto& placement : placements) {
    if (placement.usedFallback && !placement.diagnostic.empty()) {
      return placement.diagnostic;
    }
  }
  return {};
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
    std::stop_token stopToken,
    bool continueOnPhraseFailure) const {
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
  if (stopToken.stop_requested()) return core::failure<RegionRenderResult>(
      core::ErrorCode::Conflict, "Region render was cancelled");
  const auto allocation = synthesis::allocateScoreVoices(*region);
  if (!allocation) return core::Result<RegionRenderResult>{allocation.error()};
  if (allocation.value().voices.size() > 1U) {
    const auto valid = project.validate();
    if (!valid) return core::Result<RegionRenderResult>{valid.error()};
    const auto pronunciation = phonemizer::resolvePronunciationForLanguage(*region, manifest.language);
    if (!pronunciation) return core::Result<RegionRenderResult>{pronunciation.error()};
    const auto voices = synthesis::projectScoreVoices(project, *region, sampleRate,
        pronunciation.value().pronunciation.tokens);
    if (!voices) return core::Result<RegionRenderResult>{voices.error()};
    RegionRenderResult mixed;
    mixed.sampleRate = sampleRate;
    // Render sequentially: retain one child PCM at a time, not one full-song
    // buffer per voice. Clamp only after all independent voices are summed.
    for (const auto& voice : voices.value()) {
      if (stopToken.stop_requested()) return core::failure<RegionRenderResult>(
          core::ErrorCode::Conflict, "Region render was cancelled");
      auto projected = project;
      *projected.findRegion(regionId) = voice;
      // Each child contains nonoverlapping score notes, so recursion stops
      // here. Its snapshot resolves pronunciation in its own voice context.
      auto child = render(projected, manifest, bankRoot, trackId, regionId,
          revision, sampleRate, quality, style, options, cache, stopToken, continueOnPhraseFailure);
      if (!child) return core::Result<RegionRenderResult>{child.error()};
      auto& audio = child.value();
      if (mixed.mono.size() < audio.mono.size()) mixed.mono.resize(audio.mono.size(), 0.0F);
      for (std::size_t i = 0; i < audio.mono.size(); ++i) {
        if (i % 4096U == 0U && stopToken.stop_requested()) return core::failure<RegionRenderResult>(
            core::ErrorCode::Conflict, "Region render was cancelled");
        mixed.mono[i] += audio.mono[i];
      }
      mixed.phrases.insert(mixed.phrases.end(), audio.phrases.begin(), audio.phrases.end());
      mixed.unitPlan.insert(mixed.unitPlan.end(), audio.unitPlan.begin(), audio.unitPlan.end());
      mixed.failures.insert(mixed.failures.end(), audio.failures.begin(), audio.failures.end());
      mixed.unitCount += audio.unitCount;
      mixed.fallbackCount += audio.fallbackCount;
      mixed.cacheHits += audio.cacheHits;
    }
    for (std::size_t i = 0; i < mixed.mono.size(); ++i) {
      if (i % 4096U == 0U && stopToken.stop_requested()) return core::failure<RegionRenderResult>(
          core::ErrorCode::Conflict, "Region render was cancelled");
      mixed.mono[i] = std::clamp(mixed.mono[i], -1.0F, 1.0F);
    }
    return mixed;
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
    if (!snapshot) {
      if (!continueOnPhraseFailure ||
          (snapshot.error().code != core::ErrorCode::NotFound &&
           snapshot.error().code != core::ErrorCode::Conflict)) {
        return core::Result<RegionRenderResult>{snapshot.error()};
      }
      output.failures.push_back(RegionRenderPhraseFailure{
          .phraseId = segment.id,
          .code = snapshot.error().code,
          .message = snapshot.error().message,
          .context = snapshot.error().context,
      });
      continue;
    }

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
        .unitCount = snapshot.value().sample().unitPlan->entries.size(),
        .fallbackCount = cached != nullptr ? cached->fallbackCount : 0U,
        .cacheHit = cached != nullptr,
        .rendererIdentity = cached != nullptr ? cached->rendererIdentity : "unknown",
        .fallbackDiagnostic = cached != nullptr ? cached->fallbackDiagnostic : "",
    };
    output.unitCount += info.unitCount;
    output.unitPlan.insert(output.unitPlan.end(),
                           snapshot.value().sample().unitPlan->entries.begin(),
                           snapshot.value().sample().unitPlan->entries.end());
    if (cached != nullptr) {
      if (cached->sampleRate != sampleRate) {
        return core::failure<RegionRenderResult>(
            core::ErrorCode::Conflict,
            "PCM cache entry sample rate differs from render request");
      }
      const auto mixed = mixPhrase(output, cached->startFrame, cached->samples);
      if (!mixed) return core::Result<RegionRenderResult>{mixed.error()};
      output.fallbackCount += info.fallbackCount;
      ++output.cacheHits;
      output.phrases.push_back(std::move(info));
      continue;
    }

    auto rendered = pipeline.render(snapshot.value(), stopToken);
    if (!rendered) {
      if (!continueOnPhraseFailure ||
          (rendered.error().code != core::ErrorCode::NotFound &&
           rendered.error().code != core::ErrorCode::Conflict)) {
        return core::Result<RegionRenderResult>{rendered.error()};
      }
      output.failures.push_back(RegionRenderPhraseFailure{
          .phraseId = segment.id,
          .code = rendered.error().code,
          .message = rendered.error().message,
          .context = rendered.error().context,
      });
      continue;
    }
    info.fallbackCount = static_cast<std::size_t>(std::count_if(
        rendered.value().rendered.placements.begin(),
        rendered.value().rendered.placements.end(),
        [](const auto& placement) { return placement.usedFallback; }));
    info.rendererIdentity = rendererIdentity(rendered.value().rendered.placements);
    info.fallbackDiagnostic = fallbackDiagnostic(rendered.value().rendered.placements);
    output.fallbackCount += info.fallbackCount;
    const auto& audio = rendered.value().rendered.audio;
    const auto mixed = mixPhrase(output, audio.startFrame, audio.samples);
    if (!mixed) return core::Result<RegionRenderResult>{mixed.error()};
    if (cache != nullptr) {
      const auto stored = cache->store(snapshot.value().contentHash,
                                       CachedPcm{.sampleRate = sampleRate,
                                                 .startFrame = audio.startFrame,
                                                 .samples = audio.samples,
                                                 .rendererIdentity = info.rendererIdentity,
                                                 .fallbackCount = info.fallbackCount,
                                                 .fallbackDiagnostic = info.fallbackDiagnostic});
      if (!stored) return core::Result<RegionRenderResult>{stored.error()};
    }
    output.phrases.push_back(std::move(info));
  }
  if (output.mono.empty()) {
    if (continueOnPhraseFailure && !output.failures.empty()) return output;
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
