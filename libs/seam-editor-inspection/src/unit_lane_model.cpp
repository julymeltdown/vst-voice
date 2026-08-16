#include "seam/ui/unit_lane_model.hpp"

#include <algorithm>

namespace seam::ui {

void UnitLaneModel::rebuild(const domain::Project& project,
                            const domain::VocalRegion& region,
                            const phonemizer::Result& phonemes,
                            const synthesis::UnitPlan& plan,
                            const synthesis::TimingPlan& timing,
                            const synthesis::PhraseRenderResult* rendered,
                            const TimelineTransform& timeline,
                            double contentLeft,
                            double laneTop,
                            double laneHeight,
                            std::uint32_t sampleRate) {
  static_cast<void>(region);
  visuals_.clear();
  if (sampleRate < 8000U || timing.placements.empty()) return;
  visuals_.reserve(timing.placements.size());

  for (std::size_t index = 0U; index < timing.placements.size(); ++index) {
    const auto& placement = timing.placements[index];
    const auto planIterator = std::find_if(
        plan.entries.begin(), plan.entries.end(),
        [&placement](const synthesis::UnitPlanEntry& candidate) {
          return candidate.tokenStart == placement.tokenStart &&
                 candidate.unitId == placement.unitId;
        });
    if (planIterator == plan.entries.end() ||
        placement.tokenStart >= phonemes.tokens.size()) {
      continue;
    }
    const auto startTick = project.tempoMap().tickAtSampleFrame(
        placement.destinationStart, static_cast<double>(sampleRate));
    const auto endTick = project.tempoMap().tickAtSampleFrame(
        placement.destinationEnd, static_cast<double>(sampleRate));
    const auto x = contentLeft + timeline.tickToPixel(startTick);
    const auto width = std::max(3.0, timeline.durationToPixels(endTick - startTick));

    UnitLaneVisual visual{
        .startKey = phonemes.tokens[placement.tokenStart].key,
        .unitId = placement.unitId,
        .alternatives = planIterator->alternatives,
        .bounds = Rect{x, laneTop, width, laneHeight},
        .targetMidi = placement.targetMidi,
        .forced = planIterator->forced,
        .requestedRenderer = voicebank::RendererHint::Raw,
        .actualRenderer = voicebank::RendererHint::Raw,
        .usedFallback = false,
        .seamAmount = 0.7F,
        .seamCurve = domain::SeamCurve::HardCharacter,
        .diagnostic = {},
    };
    if (rendered != nullptr && index < rendered->placements.size()) {
      const auto& renderInfo = rendered->placements[index];
      visual.requestedRenderer = renderInfo.requestedRenderer;
      visual.actualRenderer = renderInfo.actualRenderer;
      visual.usedFallback = renderInfo.usedFallback;
      visual.seamAmount = renderInfo.seamAmount;
      visual.seamCurve = renderInfo.seamCurve;
      visual.diagnostic = renderInfo.diagnostic;
    }
    visuals_.push_back(std::move(visual));
  }
}

std::optional<domain::PhonemeKey> UnitLaneModel::hitTest(Point point) const {
  for (auto iterator = visuals_.rbegin(); iterator != visuals_.rend(); ++iterator) {
    if (iterator->bounds.contains(point)) return iterator->startKey;
  }
  return std::nullopt;
}

}  // namespace seam::ui
