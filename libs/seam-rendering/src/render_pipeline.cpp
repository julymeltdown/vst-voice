#include "seam/rendering/render_pipeline.hpp"

namespace seam::rendering {

core::Result<PhrasePipelineResult> PhraseRenderPipeline::render(
    const RenderSnapshot& snapshot,
    std::stop_token stopToken) const {
  if (snapshot.project == nullptr || snapshot.voicebank == nullptr ||
      snapshot.phonemes == nullptr || snapshot.unitPlan == nullptr ||
      snapshot.contentHash.empty() || snapshot.renderAbiId.empty() ||
      snapshot.frozenAudio.size() != snapshot.unitPlan->entries.size() ||
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
  auto timing = timingSolver.solve(*snapshot.project, *region,
                                   snapshot.phonemes->tokens,
                                   *snapshot.unitPlan, *snapshot.voicebank,
                                   snapshot.sampleRate);
  if (!timing) return core::Result<PhrasePipelineResult>{timing.error()};
  if (stopToken.stop_requested()) {
    return core::failure<PhrasePipelineResult>(
        core::ErrorCode::Conflict, "Phrase render was cancelled");
  }

  synthesis::ConcatenativePhraseRenderer renderer;
  auto rendered = renderer.render(*snapshot.voicebank,
                                  *snapshot.project, *region,
                                  *snapshot.unitPlan, timing.value(),
                                  snapshot.sampleRate, snapshot.renderOptions,
                                  snapshot.frozenAudio, stopToken);
  if (!rendered) return core::Result<PhrasePipelineResult>{rendered.error()};
  return PhrasePipelineResult{
      .phonemes = *snapshot.phonemes,
      .unitPlan = *snapshot.unitPlan,
      .timing = std::move(timing).value(),
      .rendered = std::move(rendered).value(),
  };
}

}  // namespace seam::rendering
