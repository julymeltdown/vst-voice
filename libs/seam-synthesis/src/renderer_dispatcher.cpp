#include "seam/synthesis/renderer_dispatcher.hpp"
#include "seam/voicebank/pitch_marks.hpp"
#include "voiced_source_marks.hpp"

#include <cmath>

namespace seam::synthesis {

voicebank::RendererHint resolveRequestedRenderer(
    const voicebank::Unit& unit,
    RenderPolicy policy,
    const std::optional<domain::UnitRendererKind>& overrideValue) noexcept {
  if (overrideValue.has_value() &&
      *overrideValue != domain::UnitRendererKind::Inherit) {
    switch (*overrideValue) {
      case domain::UnitRendererKind::Raw: return voicebank::RendererHint::Raw;
      case domain::UnitRendererKind::ClassicPsola:
        return voicebank::RendererHint::ClassicPsola;
      case domain::UnitRendererKind::SpectralClassic:
        return voicebank::RendererHint::SpectralClassic;
      case domain::UnitRendererKind::Stretch:
        return voicebank::RendererHint::Stretch;
      case domain::UnitRendererKind::Inherit: break;
    }
  }
  switch (policy) {
    case RenderPolicy::ForceRaw: return voicebank::RendererHint::Raw;
    case RenderPolicy::ForceClassicPsola:
      return voicebank::RendererHint::ClassicPsola;
    case RenderPolicy::ForceSpectralClassic:
      return voicebank::RendererHint::SpectralClassic;
    case RenderPolicy::ForceStretch:
      return voicebank::RendererHint::Stretch;
    case RenderPolicy::RespectVoicebank: return unit.renderer;
  }
  return voicebank::RendererHint::Raw;
}

namespace {

std::string voicedEdgeRetargetDiagnostic(
    const voicebank::Unit& unit, voicebank::RendererHint renderer) {
  if (renderer != voicebank::RendererHint::SpectralClassic &&
      renderer != voicebank::RendererHint::Stretch) {
    return {};
  }
  if (unit.pitchMarks.empty()) {
    return "Voiced-edge pitch retargeting unavailable: stored pitch marks are missing; attack/release pitch is not verified";
  }
  const auto validation = voicebank::validatePitchMarks(
      unit.pitchMarks, unit.markers.audioOffset, unit.markers.audioEnd);
  if (!validation) {
    return "Voiced-edge pitch retargeting unavailable: stored pitch marks are invalid; attack/release pitch is not verified";
  }
  if (unit.pitchMarks.size() < 3U) {
    return "Voiced-edge pitch retargeting unavailable: fewer than three stored pitch marks are available; attack/release pitch is not verified";
  }
  const auto loopStart = unit.markers.loopStart.value_or(unit.markers.stableStart);
  const auto releaseStart = unit.markers.releaseStart.value_or(unit.markers.audioEnd);
  const auto loopEnd = unit.markers.loopEnd.value_or(releaseStart);
  const auto edgeMarks = detail::sustainMarks(
      unit.pitchMarks, loopStart, loopEnd, unit.markers.stableStart,
      releaseStart);
  const auto medianPeriod = detail::medianMarkPeriod(edgeMarks);
  if (edgeMarks.size() < 3U || !std::isfinite(medianPeriod) ||
      medianPeriod <= 1.0) {
    return "Voiced-edge pitch retargeting unavailable: sustain marks are insufficient; attack/release pitch is not verified";
  }
  return "Voiced-edge pitch retargeting enabled by usable sustain marks; only nearby voiced marks are transformed, while unknown/unvoiced edge samples stay source-faithful";
}

core::Result<RenderedUnit> rawFallback(
    const voicebank::Unit& unit,
    const voicebank::AudioBuffer& source,
    std::uint32_t outputSampleRate,
    time::SampleFrame outputFrames,
    std::int32_t targetMidi,
    const RawRenderParameters& parameters, std::stop_token stopToken) {
  RawLoopRenderer fallback;
  return fallback.render(unit, source, outputSampleRate, outputFrames,
                         targetMidi, parameters, stopToken);
}

DispatchedRenderedUnit fallbackResult(RenderedUnit unit,
                                      voicebank::RendererHint requested,
                                      std::string diagnostic) {
  return DispatchedRenderedUnit{
      .unit = std::move(unit),
      .requested = requested,
      .actual = voicebank::RendererHint::Raw,
      .usedFallback = true,
      .diagnostic = std::move(diagnostic),
  };
}

}  // namespace

core::Result<DispatchedRenderedUnit> UnitRendererDispatcher::render(
    const voicebank::Unit& unit,
    const voicebank::AudioBuffer& source,
    std::uint32_t outputSampleRate,
    time::SampleFrame outputFrames,
    std::int32_t targetMidi,
    const RendererDispatchParameters& parameters,
    std::stop_token stopToken) const {
  if (stopToken.stop_requested()) {
    return core::failure<DispatchedRenderedUnit>(
        core::ErrorCode::Conflict, "Unit render was cancelled", unit.id);
  }
  const auto requested = resolveRequestedRenderer(
      unit, parameters.policy, parameters.rendererOverride);
  auto controls = parameters.controls;
  for (const auto* performance : {parameters.raw.performance.get(), parameters.psola.performance.get(),
                                  parameters.spectral.performance.get(), parameters.stretch.performance.get()}) {
    if (performance && performance->requiresFormantControl()) controls.require(RendererControl::Formant);
  }
  const auto capabilities = validateRendererCapabilities(
      requested, controls, parameters.allowRawFallback);
  if (!capabilities) return core::Result<DispatchedRenderedUnit>{capabilities.error()};
  if (controls.required[static_cast<std::size_t>(RendererControl::Formant)] && !parameters.spectral.performance)
    return core::failure<DispatchedRenderedUnit>(core::ErrorCode::Unsupported,
        "Formant rendering requires snapshot-owned compiled performance", unit.id);
  if (capabilities.value().canFallbackToRaw) {
    auto raw = rawFallback(unit, source, outputSampleRate, outputFrames,
                           targetMidi, parameters.raw, stopToken);
    if (!raw) return core::Result<DispatchedRenderedUnit>{raw.error()};
    return fallbackResult(std::move(raw).value(), requested,
                          capabilities.value().diagnostic);
  }

  core::Result<RenderedUnit> rendered = core::failure<RenderedUnit>(
      core::ErrorCode::Internal, "Renderer dispatch did not select a backend",
      unit.id);
  switch (requested) {
    case voicebank::RendererHint::Raw: {
      RawLoopRenderer renderer;
      rendered = renderer.render(unit, source, outputSampleRate, outputFrames,
                                 targetMidi, parameters.raw, stopToken);
      break;
    }
    case voicebank::RendererHint::ClassicPsola: {
      ClassicPsolaRenderer renderer;
      rendered = renderer.render(unit, source, outputSampleRate, outputFrames,
                                 targetMidi, parameters.psola, stopToken);
      break;
    }
    case voicebank::RendererHint::SpectralClassic: {
      SpectralClassicRenderer renderer;
      rendered = renderer.render(unit, source, outputSampleRate, outputFrames,
                                 targetMidi, parameters.spectral, stopToken);
      break;
    }
    case voicebank::RendererHint::Stretch: {
      StretchUnitRenderer renderer;
      rendered = renderer.render(unit, source, outputSampleRate, outputFrames,
                                 targetMidi, parameters.stretch, stopToken);
      break;
    }
  }

  if (rendered) {
    auto diagnostic = voicedEdgeRetargetDiagnostic(unit, requested);
    return DispatchedRenderedUnit{
        .unit = std::move(rendered).value(),
        .requested = requested,
        .actual = requested,
        .usedFallback = false,
        .diagnostic = std::move(diagnostic),
    };
  }
  if (!parameters.allowRawFallback || requested == voicebank::RendererHint::Raw ||
      controls.required[static_cast<std::size_t>(RendererControl::Formant)] ||
      parameters.controls.requiresPitchPreservingTransient ||
      (requested == voicebank::RendererHint::ClassicPsola && parameters.psola.performance) ||
      (requested == voicebank::RendererHint::SpectralClassic && parameters.spectral.performance) ||
      (requested == voicebank::RendererHint::Stretch && parameters.stretch.performance) ||
      stopToken.stop_requested()) {
    return core::Result<DispatchedRenderedUnit>{rendered.error()};
  }

  const auto diagnostic = rendered.error().message;
  auto raw = rawFallback(unit, source, outputSampleRate, outputFrames,
                         targetMidi, parameters.raw, stopToken);
  if (!raw) return core::Result<DispatchedRenderedUnit>{raw.error()};
  return fallbackResult(std::move(raw).value(), requested, diagnostic);
}

}  // namespace seam::synthesis
