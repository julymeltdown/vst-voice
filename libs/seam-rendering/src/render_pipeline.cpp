#include "seam/rendering/render_pipeline.hpp"
#include "seam/synthesis/phrase_backend.hpp"
#include "seam/voice_design/procedural_renderer.hpp"
#include "seam/voice_design/vocal_tract.hpp"
#include <limits>
#include <algorithm>

namespace seam::rendering {

core::Result<std::vector<voice_design::ProceduralPhoneMarker>> projectProceduralMarkers(const RenderSnapshot& snapshot) {
  using Output = std::vector<voice_design::ProceduralPhoneMarker>;
  const auto valid = validateProceduralSnapshot(snapshot);
  if (!valid) return core::Result<Output>{valid.error()};
  const auto anchors = snapshot.compiledPerformance->phonemeTiming();
  if (anchors.size() != snapshot.phonemes->tokens.size()) return core::failure<Output>(core::ErrorCode::Conflict,
      "Procedural marker timing differs from snapshot pronunciation");
  const auto notes = snapshot.compiledPerformance->notes();
  const auto owned = snapshot.ownedFrames.value_or(synthesis::PhraseFrameRange{notes.front().startFrame, notes.back().endFrame});
  Output result;
  if (voice_design::requiresArticulation(snapshot.phonemes->tokens)) {
    const auto plan = voice_design::ArticulationPlan::compileRecipe(
        std::get<synthesis::ProceduralSingerResource>(snapshot.resource), *snapshot.compiledPerformance,
        snapshot.phonemes->tokens, snapshot.style);
    if (!plan) return core::Result<Output>{plan.error()};
    for (const auto& gesture : plan.value().gestures()) {
      const auto start = std::max(owned.start, gesture.span.start), end = std::min(owned.end, gesture.span.end);
      if (start < end) result.push_back({gesture.key, gesture.phone, {start, end}, start != gesture.span.start, end != gesture.span.end,
          gesture.kind});
    }
    return result;
  }
  for (std::size_t i = 0U; i < anchors.size(); ++i) {
    const auto& token = snapshot.phonemes->tokens[i];
    const auto& anchor = anchors[i];
    if (token.key != anchor.key) return core::failure<Output>(core::ErrorCode::Conflict, "Procedural marker key differs from snapshot pronunciation");
    const auto start = std::max(owned.start, anchor.nucleusFrame);
    const auto end = std::min(owned.end, anchor.endFrame);
    if (start < end) result.push_back({token.key, token.symbol, {start, end}, start != anchor.nucleusFrame, end != anchor.endFrame});
  }
  std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.ownedSpan.start < b.ownedSpan.start; });
  return result;
}

core::Result<std::string> validateProceduralSnapshot(const RenderSnapshot& snapshot) {
  const auto* resource = std::get_if<synthesis::ProceduralSingerResource>(&snapshot.resource);
  if (!resource || !resource->patch) return core::failure<std::string>(core::ErrorCode::Unsupported,
      "Procedural snapshot has no frozen recipe payload");
  if (!snapshot.project || !snapshot.phonemes || !snapshot.compiledPerformance ||
      snapshot.compiledPerformance->notes().empty() || snapshot.contentHash.empty() || snapshot.renderAbiId.empty() ||
      snapshot.sampleRate != snapshot.compiledPerformance->sampleRate()) return core::failure<std::string>(
          core::ErrorCode::InvalidArgument, "Procedural snapshot is incomplete or has an incompatible sample rate");
  const auto* track = snapshot.project->findVocalTrack(snapshot.trackId);
  const auto* region = track ? track->findRegion(snapshot.segment.regionId) : nullptr;
  if (!region) return core::failure<std::string>(core::ErrorCode::NotFound, "Procedural snapshot region is missing");
  const auto phrase = voice_design::validateProceduralPhrase(*region, snapshot.phonemes->tokens);
  if (!phrase) return core::Result<std::string>{phrase.error()};
  const synthesis::PhraseFrameRange context{snapshot.compiledPerformance->notes().front().startFrame,
      snapshot.compiledPerformance->notes().back().endFrame};
  const auto valid = synthesis::PhraseOutputContract{snapshot.sampleRate, context, snapshot.ownedFrames.value_or(context)}.validate();
  if (!valid) return core::Result<std::string>{valid.error()};
  if (voice_design::requiresArticulation(snapshot.phonemes->tokens)) {
    const auto plan = voice_design::ArticulationPlan::compileRecipe(*resource, *snapshot.compiledPerformance,
        snapshot.phonemes->tokens, snapshot.style);
    if (!plan) return core::Result<std::string>{plan.error()};
    for (const auto& gesture : plan.value().gestures()) if (voice_design::isVoicedGesture(gesture.kind))
      return gesture.phone;
    return core::failure<std::string>(core::ErrorCode::Unsupported, "Articulated snapshot has no voiced pose");
  }
  const auto vowel = voice_design::validateSustainedVowelPhrase(*region, snapshot.phonemes->tokens);
  if (!vowel) return vowel;
  const auto recipe = voice_design::decodeVoiceRecipeResource(*resource);
  if (!recipe) return core::Result<std::string>{recipe.error()};
  const auto tract = voice_design::validateVowelRecipePoses(recipe.value(), snapshot.phonemes->tokens, snapshot.style, snapshot.sampleRate);
  if (!tract) return core::Result<std::string>{tract.error()};
  const auto timing = voice_design::validateVowelTiming(*snapshot.compiledPerformance);
  if (!timing) return core::Result<std::string>{timing.error()};
  return vowel;
}

core::Result<ProceduralSnapshotStream> ProceduralSnapshotStream::create(
    RenderSnapshot snapshot, std::stop_token stopToken) {
  if (stopToken.stop_requested()) return core::failure<ProceduralSnapshotStream>(core::ErrorCode::Conflict, "Procedural preparation cancelled");
  const auto vowel = validateProceduralSnapshot(snapshot);
  if (!vowel) return core::Result<ProceduralSnapshotStream>{vowel.error()};
  const auto& performance = *snapshot.compiledPerformance;
  if (voice_design::requiresArticulation(snapshot.phonemes->tokens)) {
    auto stream = voice_design::ArticulatedStream::createFromRecipe(
        std::get<synthesis::ProceduralSingerResource>(snapshot.resource), performance,
        snapshot.phonemes->tokens, snapshot.style, 512U, stopToken);
    if (!stream) return core::Result<ProceduralSnapshotStream>{stream.error()};
    return ProceduralSnapshotStream{std::move(snapshot), std::move(stream.value())};
  }
  auto stream = voice_design::SustainedPoseStream::create(
      std::get<synthesis::ProceduralSingerResource>(snapshot.resource), performance,
      vowel.value(), snapshot.style, {performance.notes().front().startFrame, performance.notes().back().endFrame},
      512U, stopToken);
  if (!stream) return core::Result<ProceduralSnapshotStream>{stream.error()};
  const auto scheduled = stream.value().configureVowels(*snapshot.project->findRegion(snapshot.segment.regionId), snapshot.phonemes->tokens);
  if (!scheduled) return core::Result<ProceduralSnapshotStream>{scheduled.error()};
  return ProceduralSnapshotStream{std::move(snapshot), std::move(stream).value()};
}

bool ProceduralSnapshotStream::matchesContext(const RenderSnapshot& snapshot) const noexcept {
  const auto* resource = std::get_if<synthesis::ProceduralSingerResource>(&snapshot.resource);
  const auto& original = std::get<synthesis::ProceduralSingerResource>(snapshot_.resource);
  // Pointer identity is intentionally conservative: independently compiled
  // equivalent inputs may render afresh, but cannot borrow this checkpoint.
  if (!resource || resource->patch != original.patch || resource->identity != original.identity ||
      snapshot.project != snapshot_.project || snapshot.phonemes != snapshot_.phonemes ||
      snapshot.compiledPerformance != snapshot_.compiledPerformance ||
      snapshot.sourceProjectId != snapshot_.sourceProjectId || snapshot.trackId != snapshot_.trackId ||
      snapshot.segment.regionId != snapshot_.segment.regionId || snapshot.segment.id != snapshot_.segment.id ||
      snapshot.revision != snapshot_.revision || snapshot.quality != snapshot_.quality ||
      snapshot.renderAbiId != snapshot_.renderAbiId || snapshot.sampleRate != snapshot_.sampleRate ||
      snapshot.style != snapshot_.style || snapshot.pronunciationIdentity != snapshot_.pronunciationIdentity) {
    return false;
  }
  return true;
}

core::Result<voice_design::SustainedPoseResult> ProceduralSnapshotStream::render(
    const RenderSnapshot& snapshot, std::stop_token stopToken) {
  if (stopToken.stop_requested()) return core::failure<voice_design::SustainedPoseResult>(core::ErrorCode::Conflict, "Procedural rendering cancelled");
  if (!matchesContext(snapshot)) return core::failure<voice_design::SustainedPoseResult>(core::ErrorCode::Conflict,
      "Procedural checkpoint belongs to another immutable snapshot context");
  const auto valid = validateProceduralSnapshot(snapshot);
  if (!valid) return core::Result<voice_design::SustainedPoseResult>{valid.error()};
  const auto& notes = snapshot.compiledPerformance->notes();
  const auto owned = snapshot.ownedFrames.value_or(synthesis::PhraseFrameRange{notes.front().startFrame, notes.back().endFrame});
  const auto allowed = snapshot_.ownedFrames.value_or(synthesis::PhraseFrameRange{notes.front().startFrame, notes.back().endFrame});
  if (owned.start < allowed.start || owned.end > allowed.end) return core::failure<voice_design::SustainedPoseResult>(
      core::ErrorCode::Conflict, "Procedural checkpoint cannot expand its source output ownership");
  if (auto* vowel = std::get_if<voice_design::SustainedPoseStream>(&stream_)) return vowel->renderOwned(owned, stopToken);
  auto markers = projectProceduralMarkers(snapshot);
  if (!markers) return core::Result<voice_design::SustainedPoseResult>{markers.error()};
  const auto before = position();
  auto audio = std::get<voice_design::ArticulatedStream>(stream_).renderOwned(owned, stopToken);
  if (!audio) return core::Result<voice_design::SustainedPoseResult>{audio.error()};
  return voice_design::SustainedPoseResult{.audio = std::move(audio.value()),
      .resource = std::get<synthesis::ProceduralSingerResource>(snapshot.resource).identity,
      .posePhone = valid.value(), .style = snapshot.style,
      .algorithmRevision = voice_design::ArticulatedStream::algorithmRevision,
      .processedFrames = static_cast<std::size_t>(owned.end - before), .markers = std::move(markers.value())};
}

core::Result<PhrasePipelineResult> PhraseRenderPipeline::render(
    const RenderSnapshot& snapshot,
    std::stop_token stopToken) const {
  if (std::holds_alternative<synthesis::ProceduralSingerResource>(snapshot.resource)) {
    auto stream = ProceduralSnapshotStream::create(snapshot, stopToken);
    if (!stream) return core::Result<PhrasePipelineResult>{stream.error()};
    auto rendered = stream.value().render(snapshot, stopToken);
    if (!rendered) return core::Result<PhrasePipelineResult>{rendered.error()};
    return PhrasePipelineResult{.phonemes = *snapshot.phonemes, .unitPlan = {}, .timing = {},
        .rendered = {std::move(rendered.value().audio), {}}, .resourceKind = domain::SingerResourceKind::Procedural,
        .proceduralMarkers = std::move(rendered.value().markers)};
  }
  if (!std::holds_alternative<synthesis::SampleSingerResource>(snapshot.resource)) {
    return core::failure<PhrasePipelineResult>(core::ErrorCode::Unsupported,
        "Phrase pipeline has no backend for this resource");
  }
  if (snapshot.project == nullptr || snapshot.sample().voicebank == nullptr ||
      snapshot.phonemes == nullptr || snapshot.sample().unitPlan == nullptr ||
      snapshot.contentHash.empty() || snapshot.renderAbiId.empty() ||
      snapshot.sample().frozenAudio.size() != snapshot.sample().unitPlan->entries.size() ||
      snapshot.sampleRate < 8000U || snapshot.sampleRate > 384000U) {
    return core::failure<PhrasePipelineResult>(
        core::ErrorCode::InvalidArgument,
        "Render pipeline snapshot is incomplete");
  }
  if (stopToken.stop_requested()) {
    return core::failure<PhrasePipelineResult>(
        core::ErrorCode::Conflict, "Phrase render was cancelled");
  }
  const auto* track = snapshot.project->findVocalTrack(snapshot.trackId);
  const auto* region = track == nullptr
      ? nullptr
      : track->findRegion(snapshot.segment.regionId);
  if (track == nullptr || region == nullptr) {
    return core::failure<PhrasePipelineResult>(
        core::ErrorCode::NotFound,
        "Render pipeline phrase track or region is missing");
  }

  synthesis::TimingSolver timingSolver;
  std::vector<synthesis::SourceAlignmentEvidence> alignments;
  for (const auto& frozen : snapshot.sample().frozenAudio) {
    if (frozen.sourceAlignment && frozen.audio && frozen.audio->frameCount() <= 32ULL * 1024ULL * 1024ULL) {
      alignments.push_back({&*frozen.sourceAlignment, frozen.verifiedAudioSha256,
          static_cast<time::SampleFrame>(frozen.audio->frameCount())});
    }
  }
  auto timing = timingSolver.solve(*snapshot.project, *region,
                                   snapshot.phonemes->tokens,
                                   *snapshot.sample().unitPlan, *snapshot.sample().voicebank,
                                   snapshot.sampleRate, alignments, true);
  if (!timing) return core::Result<PhrasePipelineResult>{timing.error()};
  for (const auto& placement : timing.value().placements) {
    const auto covered = std::span<const domain::PhonemeToken>{snapshot.phonemes->tokens}.subspan(
        placement.tokenStart, placement.tokenCount);
    const auto* unit = snapshot.sample().voicebank->findUnit(placement.unitId);
    if (synthesis::hasMultipleNuclei(covered) &&
        (!unit || !synthesis::supportsAlignedPhonemeTiming(*unit, covered, alignments))) {
      return core::failure<PhrasePipelineResult>(core::ErrorCode::Conflict,
          "Multi-nucleus rendering requires source alignment; author landmarks or select smaller units", placement.unitId);
    }
  }
  if (stopToken.stop_requested()) {
    return core::failure<PhrasePipelineResult>(
        core::ErrorCode::Conflict, "Phrase render was cancelled");
  }

  synthesis::ConcatenativePhraseRenderer renderer;
  auto renderOptions = snapshot.sample().renderOptions;
  renderOptions.renderer.psola.performance = snapshot.compiledPerformance;
  renderOptions.renderer.raw.performance = snapshot.compiledPerformance;
  renderOptions.renderer.spectral.performance = snapshot.compiledPerformance;
  renderOptions.renderer.stretch.performance = snapshot.compiledPerformance;
  auto rendered = renderer.render(*snapshot.sample().voicebank,
                                  *snapshot.project, *region,
                                  *snapshot.sample().unitPlan, timing.value(),
                                  snapshot.sampleRate, renderOptions,
                                  snapshot.sample().frozenAudio, stopToken);
  if (!rendered) return core::Result<PhrasePipelineResult>{rendered.error()};
  auto& audio = rendered.value().audio;
  constexpr auto maximumFrames = std::size_t{32U * 1024U * 1024U};
  if (audio.samples.size() > maximumFrames ||
      audio.startFrame > std::numeric_limits<time::SampleFrame>::max() - static_cast<time::SampleFrame>(audio.samples.size())) {
    return core::failure<PhrasePipelineResult>(core::ErrorCode::InvalidArgument, "Sample phrase output exceeds bounds");
  }
  const synthesis::PhraseFrameRange extent{audio.startFrame,
      audio.startFrame + static_cast<time::SampleFrame>(audio.samples.size())};
  auto finalized = synthesis::finalizePhraseBackendAudio(
      {snapshot.sampleRate, extent, snapshot.ownedFrames.value_or(extent)}, std::move(audio), stopToken);
  if (!finalized) return core::Result<PhrasePipelineResult>{finalized.error()};
  audio = std::move(finalized).value();
  return PhrasePipelineResult{
      .phonemes = *snapshot.phonemes,
      .unitPlan = *snapshot.sample().unitPlan,
      .timing = std::move(timing).value(),
      .rendered = std::move(rendered).value(),
  };
}

}  // namespace seam::rendering
