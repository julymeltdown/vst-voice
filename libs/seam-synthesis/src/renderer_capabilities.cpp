#include "seam/synthesis/renderer_capabilities.hpp"

#include <algorithm>
#include <array>

namespace seam::synthesis {
namespace {

RendererCapabilityView makeCapabilities(voicebank::RendererHint renderer) noexcept {
  RendererCapabilityView result{.renderer = renderer};
  // Every current backend consumes the shared compiled pitch/dynamics/
  // articulation path. Timing is only pitch-preserving when source alignment
  // is available; Raw can remap a transient but cannot guarantee its pitch.
  result.supported[static_cast<std::size_t>(RendererControl::Pitch)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Timing)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Dynamics)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Vibrato)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Attack)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Release)] = true;
  result.pitchPreservingTransient = renderer != voicebank::RendererHint::Raw;
  return result;
}

}  // namespace

RendererCapabilityView rendererCapabilities(
    voicebank::RendererHint renderer) noexcept {
  return makeCapabilities(renderer);
}

core::Result<RendererCapabilityDecision> validateRendererCapabilities(
    voicebank::RendererHint renderer, const RendererControlRequest& request,
    bool allowRawFallback) {
  const auto selected = makeCapabilities(renderer);
  const auto raw = makeCapabilities(voicebank::RendererHint::Raw);
  RendererCapabilityDecision decision{
      .requested = selected,
      .fallback = raw,
      .canFallbackToRaw = false,
      .diagnostic = {},
  };
  std::string missing;
  for (std::size_t index = 0U; index < kRendererControlCount; ++index) {
    if (!request.required[index] || selected.supported[index]) continue;
    if (!missing.empty()) missing += ", ";
    missing += std::string{rendererControlName(
        static_cast<RendererControl>(index))};
  }
  if (missing.empty() &&
      (!request.requiresPitchPreservingTransient ||
       selected.pitchPreservingTransient)) {
    return decision;
  }
  bool rawCanApplyControls = true;
  for (std::size_t index = 0U; index < kRendererControlCount; ++index) {
    if (request.required[index] && !raw.supported[index]) {
      rawCanApplyControls = false;
      break;
    }
  }
  const bool rawCanPreserve = !request.requiresPitchPreservingTransient ||
                              raw.pitchPreservingTransient;
  decision.canFallbackToRaw = allowRawFallback &&
                              renderer != voicebank::RendererHint::Raw &&
                              rawCanApplyControls && rawCanPreserve;
  if (decision.canFallbackToRaw) {
    decision.diagnostic = missing.empty()
                              ? "Selected renderer cannot preserve the required transient timing"
                              : "Selected renderer lacks required controls: " + missing;
    return decision;
  }
  decision.diagnostic = missing.empty()
                           ? "Selected renderer cannot preserve the required transient timing"
                           : "Selected renderer lacks required controls: " + missing;
  return core::failure<RendererCapabilityDecision>(
      core::ErrorCode::Unsupported, decision.diagnostic);
}

std::string_view rendererControlName(RendererControl control) noexcept {
  switch (control) {
    case RendererControl::Pitch: return "pitch";
    case RendererControl::Timing: return "timing";
    case RendererControl::Dynamics: return "dynamics";
    case RendererControl::Vibrato: return "vibrato";
    case RendererControl::Attack: return "attack";
    case RendererControl::Release: return "release";
    case RendererControl::Formant: return "formant";
    case RendererControl::Breathiness: return "breathiness";
    case RendererControl::Tension: return "tension";
    case RendererControl::Airiness: return "airiness";
    case RendererControl::Gender: return "gender";
    case RendererControl::StyleBlend: return "style-blend";
    case RendererControl::Growl: return "growl";
  }
  return "unknown";
}

}  // namespace seam::synthesis
