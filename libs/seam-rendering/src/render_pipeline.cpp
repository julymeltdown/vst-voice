#include "seam/rendering/render_pipeline.hpp"

#include "seam/phonemizer/japanese_phonemizer.hpp"

namespace seam::rendering {

core::Result<PhrasePipelineResult> PhraseRenderPipeline::render(
    const RenderSnapshot& snapshot,
    const synthesis::PhraseRenderOptions& options,
    std::stop_token stopToken) const {
  if (snapshot.project == nullptr || snapshot.voicebank == nullptr ||
      snapshot.contentHash.empty() || snapshot.sampleRate < 8000U ||
      snapshot.sampleRate > 384000U) {
    return core::failure<PhrasePipelineResult>(
        core::ErrorCode::InvalidArgument,
        "Render pipeline snapshot is incomplete");
  }
  if (stopToken.stop_requested()) {
    return core::failure<PhrasePipelineResult>(
        core::ErrorCode::Conflict, "Phrase render was cancelled");
  }
  if (snapshot.voicebank->language != domain::Language::Japanese) {
    return core::failure<PhrasePipelineResult>(
        core::ErrorCode::Unsupported,
        "Phase 3 render pipeline currently supports Japanese voicebanks only");
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

  phonemizer::JapaneseKanaPhonemizer phonemizer;
  auto phonemes = phonemizer.phonemize(*region);
  if (phonemes.tokens.empty()) {
    return core::failure<PhrasePipelineResult>(
        core::ErrorCode::NotFound,
        "Phonemizer produced no renderable tokens");
  }
  if (stopToken.stop_requested()) {
    return core::failure<PhrasePipelineResult>(
        core::ErrorCode::Conflict, "Phrase render was cancelled");
  }

  synthesis::DeterministicUnitSelector selector;
  auto unitPlan = selector.select(*snapshot.voicebank, *region,
                                  phonemes.tokens, snapshot.style,
                                  region->unitSelectionOverrides);
  if (!unitPlan) return core::Result<PhrasePipelineResult>{unitPlan.error()};

  synthesis::TimingSolver timingSolver;
  auto timing = timingSolver.solve(*snapshot.project, *region, phonemes.tokens,
                                   unitPlan.value(), *snapshot.voicebank,
                                   snapshot.sampleRate);
  if (!timing) return core::Result<PhrasePipelineResult>{timing.error()};
  if (stopToken.stop_requested()) {
    return core::failure<PhrasePipelineResult>(
        core::ErrorCode::Conflict, "Phrase render was cancelled");
  }

  synthesis::ConcatenativePhraseRenderer renderer;
  auto rendered = renderer.render(*snapshot.voicebank, snapshot.bankRoot,
                                  *snapshot.project, *region,
                                  unitPlan.value(), timing.value(),
                                  snapshot.sampleRate, options, stopToken);
  if (!rendered) return core::Result<PhrasePipelineResult>{rendered.error()};
  return PhrasePipelineResult{
      .phonemes = std::move(phonemes),
      .unitPlan = std::move(unitPlan).value(),
      .timing = std::move(timing).value(),
      .rendered = std::move(rendered).value(),
  };
}

}  // namespace seam::rendering
