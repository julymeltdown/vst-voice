#include "seam/synthesis/phrase_renderer.hpp"
#include "seam/synthesis/source_target_map.hpp"

#include "seam/voicebank/asset_path.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
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
    if (placement.compressShortTransition) return core::failure<PhraseRenderResult>(core::ErrorCode::Unsupported,
        "Legacy raw phrase rendering cannot consume short-transition maps", placement.unitId);
    const auto* unit = manifest.findUnit(placement.unitId);
    if (unit == nullptr) {
      return core::failure<PhraseRenderResult>(core::ErrorCode::NotFound,
                                               "Timing plan references a missing unit",
                                               placement.unitId);
    }
    auto resolved = voicebank::resolveBankAsset(bankRoot, unit->audioPath);
    if (!resolved) return core::Result<PhraseRenderResult>{resolved.error()};
    const auto& path = resolved.value();
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
    if (placement.explicitOnsetStart) {
      const auto retimed = retimeRenderedOnset(renderedUnit.value(), placement.desiredVowelOnset - placement.destinationStart);
      if (!retimed) return core::Result<PhraseRenderResult>{retimed.error()};
    }
    const auto alignedStart = placement.desiredVowelOnset -
                              renderedUnit.value().vowelOnsetOffset;
    result.placements.push_back(RenderedPlacementInfo{
        .unitId = placement.unitId,
        .requestedStart = placement.destinationStart,
        .alignedStart = alignedStart,
        .frameCount = static_cast<time::SampleFrame>(renderedUnit.value().samples.size()),
        .vowelOnset = alignedStart + renderedUnit.value().vowelOnsetOffset,
        .requestedRenderer = voicebank::RendererHint::Raw,
        .actualRenderer = voicebank::RendererHint::Raw,
        .usedFallback = false,
        .forcedSelection = false,
        .seamAmount = seamSettings.seamAmount,
        .seamCurve = seamSettings.curve,
        .diagnostic = {},
    });
    rendered.push_back(PlacedRenderedUnit{
        .destinationStart = alignedStart,
        .unit = std::move(renderedUnit).value(),
        .incomingBoundary = std::nullopt,
    });
  }
  seamSettings.sampleRate = outputSampleRate;
  SeamComposer composer;
  auto audio = composer.compose(rendered, seamSettings);
  if (!audio) return core::Result<PhraseRenderResult>{audio.error()};
  result.audio = std::move(audio).value();
  return result;
}

core::Result<PhraseRenderResult> ConcatenativePhraseRenderer::render(
    const voicebank::Manifest& manifest,
    const std::filesystem::path& bankRoot,
    const domain::Project& project,
    const domain::VocalRegion& region,
    const UnitPlan& unitPlan,
    const TimingPlan& timing,
    std::uint32_t outputSampleRate,
    const PhraseRenderOptions& options,
    std::stop_token stopToken) const {
  std::map<std::filesystem::path, std::shared_ptr<const voicebank::AudioBuffer>>
      audioByPath;
  std::vector<FrozenUnitAudio> frozenAudio;
  frozenAudio.reserve(unitPlan.entries.size());
  for (const auto& entry : unitPlan.entries) {
    const auto* unit = manifest.findUnit(entry.unitId);
    if (unit == nullptr) {
      return core::failure<PhraseRenderResult>(
          core::ErrorCode::NotFound,
          "Unit plan references a missing voicebank Unit",
          entry.unitId);
    }
    auto resolved = voicebank::resolveBankAsset(bankRoot, unit->audioPath);
    if (!resolved) return core::Result<PhraseRenderResult>{resolved.error()};
    auto iterator = audioByPath.find(resolved.value());
    if (iterator == audioByPath.end()) {
      auto loaded = voicebank::readWav(resolved.value());
      if (!loaded) return core::Result<PhraseRenderResult>{loaded.error()};
      iterator = audioByPath.emplace(
          resolved.value(),
          std::make_shared<const voicebank::AudioBuffer>(
              std::move(loaded).value())).first;
    }
    frozenAudio.push_back(FrozenUnitAudio{
        .unitId = entry.unitId,
        .audio = iterator->second,
    });
  }
  return render(manifest, project, region, unitPlan, timing, outputSampleRate,
                options, frozenAudio, stopToken);
}

core::Result<PhraseRenderResult> ConcatenativePhraseRenderer::render(
    const voicebank::Manifest& manifest,
    const domain::Project& project,
    const domain::VocalRegion& region,
    const UnitPlan& unitPlan,
    const TimingPlan& timing,
    std::uint32_t outputSampleRate,
    const PhraseRenderOptions& options,
    std::span<const FrozenUnitAudio> frozenAudio,
    std::stop_token stopToken) const {
  if (timing.placements.empty() || unitPlan.entries.size() != timing.placements.size()) {
    return core::failure<PhraseRenderResult>(
        core::ErrorCode::InvalidArgument,
        "Production phrase renderer requires aligned unit and timing plans");
  }
  std::vector<PlacedRenderedUnit> rendered;
  PhraseRenderResult result;
  rendered.reserve(timing.placements.size());
  result.placements.reserve(timing.placements.size());
  UnitRendererDispatcher dispatcher;

  for (std::size_t index = 0; index < timing.placements.size(); ++index) {
    if (stopToken.stop_requested()) {
      return core::failure<PhraseRenderResult>(core::ErrorCode::Conflict,
                                               "Phrase render was cancelled");
    }
    const auto& placement = timing.placements[index];
    const auto planIterator = std::find_if(
        unitPlan.entries.begin(), unitPlan.entries.end(),
        [&placement](const UnitPlanEntry& entry) {
          return entry.tokenStart == placement.tokenStart &&
                 entry.unitId == placement.unitId;
        });
    if (planIterator == unitPlan.entries.end()) {
      return core::failure<PhraseRenderResult>(
          core::ErrorCode::InvariantViolation,
          "Unit and timing plans are not aligned",
          placement.unitId);
    }
    const auto& planEntry = *planIterator;
    const auto* unit = manifest.findUnit(placement.unitId);
    if (unit == nullptr) {
      return core::failure<PhraseRenderResult>(core::ErrorCode::NotFound,
                                               "Timing plan references a missing unit",
                                               placement.unitId);
    }
    const auto frozen = std::find_if(
        frozenAudio.begin(), frozenAudio.end(),
        [&placement](const FrozenUnitAudio& candidate) {
          return candidate.unitId == placement.unitId;
        });
    if (frozen == frozenAudio.end() || frozen->audio == nullptr) {
      return core::failure<PhraseRenderResult>(
          core::ErrorCode::NotFound,
          "Frozen render snapshot is missing selected Unit audio",
          placement.unitId);
    }
    const auto requestedFrames = std::max<time::SampleFrame>(
        1, placement.destinationEnd - placement.destinationStart);
    auto dispatchParameters = options.renderer;
    dispatchParameters.rendererOverride = planEntry.renderer;
    if (dispatchParameters.raw.performance || dispatchParameters.psola.performance ||
        dispatchParameters.spectral.performance || dispatchParameters.stretch.performance) {
      dispatchParameters.controls.require(RendererControl::Pitch);
      dispatchParameters.controls.require(RendererControl::Dynamics);
      dispatchParameters.controls.require(RendererControl::Attack);
      dispatchParameters.controls.require(RendererControl::Release);
      dispatchParameters.controls.require(RendererControl::Vibrato);
    }
    for (const auto* performance : {dispatchParameters.raw.performance.get(), dispatchParameters.psola.performance.get(),
                                    dispatchParameters.spectral.performance.get(), dispatchParameters.stretch.performance.get()}) {
      if (performance && performance->requiresFormantControl()) dispatchParameters.controls.require(RendererControl::Formant);
    }
    if (dispatchParameters.controls.requiresControl(RendererControl::Formant)) {
      const auto allowed = validateRendererCapabilities(
          resolveRequestedRenderer(*unit, dispatchParameters.policy, planEntry.renderer), dispatchParameters.controls, false);
      if (!allowed) return core::Result<PhraseRenderResult>{allowed.error()};
    }
    dispatchParameters.raw.performanceVowelFrame = placement.desiredVowelOnset;
    dispatchParameters.psola.performanceStartFrame = placement.destinationStart;
    dispatchParameters.spectral.performanceStartFrame = placement.destinationStart;
    dispatchParameters.stretch.performanceStartFrame = placement.destinationStart;
    if (dispatchParameters.stretch.performance && placement.explicitOnsetStart && !frozen->sourceAlignment &&
        resolveRequestedRenderer(*unit, dispatchParameters.policy, planEntry.renderer) == voicebank::RendererHint::Stretch) {
      return core::failure<PhraseRenderResult>(core::ErrorCode::Unsupported,
          "Compiled stretch performance requires pitch-preserving explicit onset mapping", unit->id);
    }
    if (dispatchParameters.spectral.performance && placement.explicitOnsetStart && !frozen->sourceAlignment &&
        resolveRequestedRenderer(*unit, dispatchParameters.policy, planEntry.renderer) == voicebank::RendererHint::SpectralClassic) {
      return core::failure<PhraseRenderResult>(core::ErrorCode::Unsupported,
          "Compiled spectral performance requires pitch-preserving explicit onset mapping", unit->id);
    }
    if (dispatchParameters.psola.performance && placement.explicitOnsetStart && !frozen->sourceAlignment &&
        resolveRequestedRenderer(*unit, dispatchParameters.policy, planEntry.renderer) == voicebank::RendererHint::ClassicPsola) {
      return core::failure<PhraseRenderResult>(core::ErrorCode::Unsupported,
          "Compiled PSOLA performance requires pitch-preserving explicit onset mapping", unit->id);
    }
    if (const auto* overrideValue =
            region.findUnitSelectionOverride(placement.startKey);
        overrideValue != nullptr && !overrideValue->unresolved) {
      if (overrideValue->loopPrint.has_value()) {
        dispatchParameters.raw.loopPrint = *overrideValue->loopPrint;
      }
      if (overrideValue->sourcePitchResidual.has_value()) {
        dispatchParameters.psola.sourcePitchResidual =
            *overrideValue->sourcePitchResidual;
      }
    }

    std::vector<PitchPoint> pitchPoints;
    if (!region.pitchAutomation.points().empty()) {
      const auto addPoint = [&](time::Tick tick,
                                float cents,
                                domain::CurveInterpolation interpolation) {
        const auto absoluteTick = region.startTick + tick;
        const auto absoluteFrame = project.tempoMap().sampleFrameAt(
            absoluteTick, static_cast<double>(outputSampleRate));
        const auto relativeFrame = std::clamp<time::SampleFrame>(
            absoluteFrame - placement.destinationStart, 0, requestedFrames - 1);
        pitchPoints.push_back(PitchPoint{
            .frame = relativeFrame,
            .cents = cents,
            .interpolation = interpolation,
        });
      };
      addPoint(placement.sourceStartTick,
               region.pitchAutomation.valueAt(placement.sourceStartTick),
               domain::CurveInterpolation::Linear);
      for (const auto& point : region.pitchAutomation.points()) {
        if (point.tick > placement.sourceStartTick &&
            point.tick < placement.sourceEndTick) {
          addPoint(point.tick, point.cents, point.interpolation);
        }
      }
      addPoint(placement.sourceEndTick,
               region.pitchAutomation.valueAt(placement.sourceEndTick),
               domain::CurveInterpolation::Linear);
      std::stable_sort(pitchPoints.begin(), pitchPoints.end(),
          [](const auto& lhs, const auto& rhs) { return lhs.frame < rhs.frame; });
      std::vector<PitchPoint> uniquePoints;
      uniquePoints.reserve(pitchPoints.size());
      for (const auto& point : pitchPoints) {
        if (!uniquePoints.empty() && uniquePoints.back().frame == point.frame) {
          uniquePoints.back() = point;
        } else {
          uniquePoints.push_back(point);
        }
      }
      auto curve = PitchCurve{std::move(uniquePoints)};
      if (!dispatchParameters.raw.performance) dispatchParameters.raw.pitchCurve = curve;
      if (!dispatchParameters.psola.performance) dispatchParameters.psola.pitchCurve = curve;
      if (!dispatchParameters.spectral.performance) dispatchParameters.spectral.pitchCurve = curve;
      if (!dispatchParameters.stretch.performance) dispatchParameters.stretch.pitchCurve = std::move(curve);
    }

    auto renderedUnit = [&]() -> core::Result<DispatchedRenderedUnit> {
      if (placement.compressShortTransition && !frozen->sourceAlignment) {
        const auto requested = resolveRequestedRenderer(*unit, dispatchParameters.policy, dispatchParameters.rendererOverride);
        const auto map = compileShortUnitMarkerMap(*unit, placement.destinationStart,
            placement.desiredVowelOnset, placement.destinationEnd, frozen->audio->sampleRate,
            outputSampleRate, static_cast<time::SampleFrame>(frozen->audio->frameCount()));
        if (!map) return core::Result<DispatchedRenderedUnit>{map.error()};
        // The marker map carries no voicing, and a renderer that asks
        // voicedAtSource(...) == false reads "no answer" as "not unvoiced". Without
        // this, an unvoiced consonant in a short CV transition renders as though it
        // were voiced. Measured voicing is applied when a stored analysis exists;
        // when none does, the previous behaviour is kept rather than guessed at.
        auto voicedMap = map.value();
        if (frozen->acousticAnalysis) {
          // A decline means the analysis covers no part of this unit's range, so
          // the map keeps no voicing and the renderer keeps its previous reading.
          // That is the honest outcome: the alternative is inventing a state for
          // samples nothing measured.
          const bool applied = applyMeasuredVoicing(
              voicedMap, *frozen->acousticAnalysis,
              unit->markers.audioOffset, unit->markers.audioEnd);
          if (!applied) {
            voicedMap.voicing.clear();
          }
        }
        dispatchParameters.allowRawFallback = false;
        if (requested == voicebank::RendererHint::Raw) {
          dispatchParameters.raw.sourceMap = voicedMap; dispatchParameters.raw.performanceStartFrame = placement.destinationStart;
        } else if (requested == voicebank::RendererHint::ClassicPsola) {
          dispatchParameters.psola.sourceMap = voicedMap; dispatchParameters.psola.performanceStartFrame = placement.destinationStart;
        } else if (requested == voicebank::RendererHint::SpectralClassic) {
          dispatchParameters.spectral.sourceMap = voicedMap; dispatchParameters.spectral.performanceStartFrame = placement.destinationStart;
        } else {
          dispatchParameters.stretch.sourceMap = voicedMap; dispatchParameters.stretch.performanceStartFrame = placement.destinationStart;
        }
        auto output = dispatcher.render(*unit, *frozen->audio, outputSampleRate, requestedFrames,
            placement.targetMidi, dispatchParameters, stopToken);
        if (output) {
          output.value().unit.vowelOnsetOffset = placement.desiredVowelOnset - placement.destinationStart;
          output.value().diagnostic = requested == voicebank::RendererHint::Raw
              ? "Short transition compressed from unit markers; Raw sustain pitch loop retained; transient pitch is resampled"
              : "Short transition compressed from unit markers; acoustic qualification pending";
        }
        return output;
      }
      if (!frozen->sourceAlignment) return dispatcher.render(*unit, *frozen->audio, outputSampleRate,
          requestedFrames, placement.targetMidi, dispatchParameters, stopToken);
      const auto requested = resolveRequestedRenderer(
          *unit, dispatchParameters.policy, dispatchParameters.rendererOverride);
      if ((requested == voicebank::RendererHint::ClassicPsola && dispatchParameters.psola.performance) ||
          (requested == voicebank::RendererHint::SpectralClassic && dispatchParameters.spectral.performance) ||
          (requested == voicebank::RendererHint::Stretch && dispatchParameters.stretch.performance)) {
        if (frozen->audio->sampleRate != manifest.expectedSampleRate) {
          return core::failure<DispatchedRenderedUnit>(core::ErrorCode::Conflict, "Aligned classical source rate differs from its bank", unit->id);
        }
        const auto map = compileSourceTargetMap(*frozen->sourceAlignment, *unit, placement,
            frozen->verifiedAudioSha256, static_cast<time::SampleFrame>(frozen->audio->frameCount()));
        if (!map) return core::Result<DispatchedRenderedUnit>{map.error()};
        if (requested == voicebank::RendererHint::ClassicPsola) dispatchParameters.psola.sourceMap = map.value();
        else if (requested == voicebank::RendererHint::SpectralClassic) dispatchParameters.spectral.sourceMap = map.value();
        else dispatchParameters.stretch.sourceMap = map.value();
        auto output = dispatcher.render(*unit, *frozen->audio, outputSampleRate,
            requestedFrames, placement.targetMidi, dispatchParameters, stopToken);
        if (output) {
          output.value().unit.vowelOnsetOffset = placement.desiredVowelOnset - placement.destinationStart;
          output.value().diagnostic = requested == voicebank::RendererHint::ClassicPsola ?
              "Source-aligned PSOLA sustain with compiled performance; transient qualification pending" :
              (requested == voicebank::RendererHint::SpectralClassic ?
              "Source-aligned spectral sustain with compiled performance; transient qualification pending" :
              "Source-aligned granular sustain with compiled performance; transient qualification pending");
        }
        return output;
      }
      if (requested != voicebank::RendererHint::Raw ||
          !region.pitchAutomation.points().empty() || dispatchParameters.raw.loopPrint != 1.0F ||
          dispatchParameters.raw.additionalGainDb != 0.0F ||
          std::any_of(region.performance.accepted.begin(), region.performance.accepted.end(),
              [](const auto& selection) { return selection.channel == domain::PerformanceChannel::Pitch; })) {
        return core::failure<DispatchedRenderedUnit>(core::ErrorCode::Unsupported,
            "Source-aligned rendering currently requires raw selection without pitch automation or raw overrides", unit->id);
      }
      for (const auto& target : placement.phonemeTargets) {
        const auto* note = region.findNote(target.key.noteId);
        if (!note || note->midiKey != placement.targetMidi || note->vibrato.enabled) {
          return core::failure<DispatchedRenderedUnit>(core::ErrorCode::Unsupported,
              "Source-aligned raw rendering cannot follow pitch changes within a unit", unit->id);
        }
      }
      const auto& audio = *frozen->audio;
      if ((audio.channels != 1U && audio.channels != 2U) || audio.interleaved.size() % audio.channels != 0U ||
          audio.frameCount() > 32ULL * 1024ULL * 1024ULL || audio.sampleRate != manifest.expectedSampleRate) {
        return core::failure<DispatchedRenderedUnit>(core::ErrorCode::InvalidArgument, "Aligned source audio format is invalid", unit->id);
      }
      const auto mono = audio.monoMix();
      auto mapped = renderAlignedRawUnit(mono, *frozen->sourceAlignment, *unit, placement, frozen->verifiedAudioSha256, stopToken);
      if (!mapped) return core::Result<DispatchedRenderedUnit>{mapped.error()};
      if (dispatchParameters.raw.performance) {
        const auto gain = applyCompiledPerformanceGain(mapped.value().samples, *dispatchParameters.raw.performance, mapped.value().startFrame, stopToken);
        if (!gain) return core::Result<DispatchedRenderedUnit>{gain.error()};
      }
      return DispatchedRenderedUnit{
          .unit = {.unitId = unit->id, .samples = std::move(mapped.value().samples),
              .vowelOnsetOffset = placement.desiredVowelOnset - mapped.value().startFrame},
          .requested = voicebank::RendererHint::Raw, .actual = voicebank::RendererHint::Raw,
          .usedFallback = false, .diagnostic = "Source-aligned raw resampling; not pitch-preserving"};
    }();
    if (!renderedUnit) {
      return core::Result<PhraseRenderResult>{renderedUnit.error()};
    }
    if (placement.explicitOnsetStart && !frozen->sourceAlignment) {
      const auto retimed = retimeRenderedOnset(renderedUnit.value().unit, placement.desiredVowelOnset - placement.destinationStart);
      if (!retimed) return core::Result<PhraseRenderResult>{retimed.error()};
    }
    const auto alignedStart = placement.desiredVowelOnset -
                              renderedUnit.value().unit.vowelOnsetOffset;

    BoundarySeamSettings boundary{
        .seamAmount = options.defaultSeam.seamAmount,
        .curve = options.defaultSeam.curve,
        .maxOverlapFrames = std::nullopt,
        .phaseReset = 0.0F,
        .envelopeBlend = 0.0F,
    };
    if (const auto* overrideValue = region.findSeamOverride(placement.startKey);
        overrideValue != nullptr && !overrideValue->unresolved) {
      boundary.seamAmount = overrideValue->seamAmount.value_or(boundary.seamAmount);
      boundary.curve = overrideValue->curve;
      if (overrideValue->overlap.has_value()) {
        boundary.maxOverlapFrames = static_cast<time::SampleFrame>(std::llround(
            static_cast<double>(*overrideValue->overlap) *
            static_cast<double>(outputSampleRate) / 1'000'000.0));
      }
      boundary.phaseReset = overrideValue->phaseReset.value_or(0.0F);
      boundary.envelopeBlend = overrideValue->envelopeBlend.value_or(0.0F);
    }
    result.placements.push_back(RenderedPlacementInfo{
        .unitId = placement.unitId,
        .requestedStart = placement.destinationStart,
        .alignedStart = alignedStart,
        .frameCount = static_cast<time::SampleFrame>(
            renderedUnit.value().unit.samples.size()),
        .vowelOnset = alignedStart + renderedUnit.value().unit.vowelOnsetOffset,
        .requestedRenderer = renderedUnit.value().requested,
        .actualRenderer = renderedUnit.value().actual,
        .usedFallback = renderedUnit.value().usedFallback,
        .forcedSelection = planEntry.forced,
        .seamAmount = boundary.seamAmount,
        .seamCurve = boundary.curve,
        .diagnostic = renderedUnit.value().diagnostic,
    });
    rendered.push_back(PlacedRenderedUnit{
        .destinationStart = alignedStart,
        .unit = std::move(renderedUnit).value().unit,
        .incomingBoundary = index == 0U
            ? std::optional<BoundarySeamSettings>{}
            : std::optional<BoundarySeamSettings>{boundary},
    });
  }

  auto seamSettings = options.defaultSeam;
  seamSettings.sampleRate = outputSampleRate;
  SeamComposer composer;
  auto audio = composer.compose(rendered, seamSettings);
  if (!audio) return core::Result<PhraseRenderResult>{audio.error()};
  result.audio = std::move(audio).value();
  return result;
}

}  // namespace seam::synthesis
