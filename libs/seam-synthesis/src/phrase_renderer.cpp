#include "seam/synthesis/phrase_renderer.hpp"

#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <map>

namespace seam::synthesis {

core::Result<PhraseRenderResult> RawPhraseRenderer::render(
    const voicebank::Manifest& manifest,
    const std::filesystem::path& bankRoot,
    const TimingPlan& timing,
    std::uint32_t outputSampleRate,
    RawRenderParameters renderParameters,
    SeamSettings seamSettings) const {
  if (timing.placements.empty()) {
    return core::failure<PhraseRenderResult>(core::ErrorCode::InvalidArgument,
                                             "Phrase renderer requires timing placements");
  }
  RawLoopRenderer renderer;
  std::map<std::filesystem::path, voicebank::AudioBuffer> audioCache;
  std::vector<PlacedRenderedUnit> rendered;
  PhraseRenderResult result;
  rendered.reserve(timing.placements.size());
  result.placements.reserve(timing.placements.size());

  for (const auto& placement : timing.placements) {
    const auto* unit = manifest.findUnit(placement.unitId);
    if (unit == nullptr) {
      return core::failure<PhraseRenderResult>(core::ErrorCode::NotFound,
                                               "Timing plan references a missing unit",
                                               placement.unitId);
    }
    const auto path = bankRoot / unit->audioPath;
    auto iterator = audioCache.find(path);
    if (iterator == audioCache.end()) {
      auto loaded = voicebank::readWav(path);
      if (!loaded) return core::Result<PhraseRenderResult>{loaded.error()};
      iterator = audioCache.emplace(path, std::move(loaded).value()).first;
    }
    const auto requestedFrames = std::max<time::SampleFrame>(
        1, placement.destinationEnd - placement.destinationStart);
    auto renderedUnit = renderer.render(*unit, iterator->second, outputSampleRate,
                                        requestedFrames, placement.targetMidi,
                                        renderParameters);
    if (!renderedUnit) {
      return core::Result<PhraseRenderResult>{renderedUnit.error()};
    }
    const auto alignedStart = placement.desiredVowelOnset -
                              renderedUnit.value().vowelOnsetOffset;
    result.placements.push_back(RenderedPlacementInfo{
        .unitId = placement.unitId,
        .requestedStart = placement.destinationStart,
        .alignedStart = alignedStart,
        .frameCount = static_cast<time::SampleFrame>(renderedUnit.value().samples.size()),
        .vowelOnset = alignedStart + renderedUnit.value().vowelOnsetOffset,
    });
    rendered.push_back(PlacedRenderedUnit{
        .destinationStart = alignedStart,
        .unit = std::move(renderedUnit).value(),
    });
  }
  seamSettings.sampleRate = outputSampleRate;
  SeamComposer composer;
  auto audio = composer.compose(rendered, seamSettings);
  if (!audio) return core::Result<PhraseRenderResult>{audio.error()};
  result.audio = std::move(audio).value();
  return result;
}

}  // namespace seam::synthesis
