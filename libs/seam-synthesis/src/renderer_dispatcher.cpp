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
    case RenderPolicy::RespectVoicebank: return unit.renderer;
  }
  return voicebank::RendererHint::Raw;
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
  if (requested == voicebank::RendererHint::ClassicPsola) {
    ClassicPsolaRenderer renderer;
    auto result = renderer.render(unit, source, outputSampleRate, outputFrames,
                                  targetMidi, parameters.psola, stopToken);
    if (result) {
      return DispatchedRenderedUnit{
          .unit = std::move(result).value(),
          .requested = requested,
          .actual = voicebank::RendererHint::ClassicPsola,
          .usedFallback = false,
          .diagnostic = {},
      };
    }
    if (!parameters.allowRawFallback || stopToken.stop_requested()) {
      return core::Result<DispatchedRenderedUnit>{result.error()};
    }
    RawLoopRenderer fallback;
    auto raw = fallback.render(unit, source, outputSampleRate, outputFrames,
                               targetMidi, parameters.raw);
    if (!raw) return core::Result<DispatchedRenderedUnit>{raw.error()};
    return DispatchedRenderedUnit{
        .unit = std::move(raw).value(),
        .requested = requested,
        .actual = voicebank::RendererHint::Raw,
        .usedFallback = true,
        .diagnostic = result.error().message,
    };
  }

  if (requested == voicebank::RendererHint::SpectralClassic ||
      requested == voicebank::RendererHint::Stretch) {
    if (!parameters.allowRawFallback) {
      return core::failure<DispatchedRenderedUnit>(
          core::ErrorCode::Unsupported,
          "Requested renderer is not implemented in Phase 3",
          unit.id);
    }
    RawLoopRenderer fallback;
    auto raw = fallback.render(unit, source, outputSampleRate, outputFrames,
                               targetMidi, parameters.raw);
    if (!raw) return core::Result<DispatchedRenderedUnit>{raw.error()};
    return DispatchedRenderedUnit{
        .unit = std::move(raw).value(),
        .requested = requested,
        .actual = voicebank::RendererHint::Raw,
        .usedFallback = true,
        .diagnostic = "Requested renderer is not implemented; RawLoopRenderer was used",
    };
  }

  RawLoopRenderer renderer;
  auto raw = renderer.render(unit, source, outputSampleRate, outputFrames,
                             targetMidi, parameters.raw);
  if (!raw) return core::Result<DispatchedRenderedUnit>{raw.error()};
  return DispatchedRenderedUnit{
      .unit = std::move(raw).value(),
      .requested = requested,
      .actual = voicebank::RendererHint::Raw,
      .usedFallback = false,
      .diagnostic = {},
  };
}

}  // namespace seam::synthesis
