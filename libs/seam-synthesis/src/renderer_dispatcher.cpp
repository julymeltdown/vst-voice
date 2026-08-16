#include "seam/synthesis/renderer_dispatcher.hpp"

namespace seam::synthesis {
namespace {

voicebank::RendererHint requestedRenderer(
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

core::Result<RenderedUnit> rawFallback(
    const voicebank::Unit& unit,
    const voicebank::AudioBuffer& source,
    std::uint32_t outputSampleRate,
    time::SampleFrame outputFrames,
    std::int32_t targetMidi,
    const RawRenderParameters& parameters) {
  RawLoopRenderer fallback;
  return fallback.render(unit, source, outputSampleRate, outputFrames,
                         targetMidi, parameters);
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
  const auto requested = requestedRenderer(
      unit, parameters.policy, parameters.rendererOverride);

  core::Result<RenderedUnit> rendered = core::failure<RenderedUnit>(
      core::ErrorCode::Internal, "Renderer dispatch did not select a backend",
      unit.id);
  switch (requested) {
    case voicebank::RendererHint::Raw: {
      RawLoopRenderer renderer;
      rendered = renderer.render(unit, source, outputSampleRate, outputFrames,
                                 targetMidi, parameters.raw);
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
    return DispatchedRenderedUnit{
        .unit = std::move(rendered).value(),
        .requested = requested,
        .actual = requested,
        .usedFallback = false,
        .diagnostic = {},
    };
  }
  if (!parameters.allowRawFallback || requested == voicebank::RendererHint::Raw ||
      stopToken.stop_requested()) {
    return core::Result<DispatchedRenderedUnit>{rendered.error()};
  }

  const auto diagnostic = rendered.error().message;
  auto raw = rawFallback(unit, source, outputSampleRate, outputFrames,
                         targetMidi, parameters.raw);
  if (!raw) return core::Result<DispatchedRenderedUnit>{raw.error()};
  return fallbackResult(std::move(raw).value(), requested, diagnostic);
}

}  // namespace seam::synthesis
