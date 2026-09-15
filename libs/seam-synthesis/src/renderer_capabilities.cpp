#include "seam/synthesis/renderer_capabilities.hpp"

#include "seam/domain/project.hpp"

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

namespace {

// The source-filter engine compiles the score once and owns its own excitation and tract, so it
// consumes every control the compiled performance carries, including the formant channel that moves
// its own resonances, the breathiness channel that rebalances its periodic and aperiodic energy, and
// the tension channel that tilts the spectrum of the harmonic source it generates.
RendererCapabilityView sourceFilterCapabilities() noexcept {
  RendererCapabilityView result{.renderer = voicebank::RendererHint::Raw};
  result.supported[static_cast<std::size_t>(RendererControl::Pitch)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Timing)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Dynamics)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Vibrato)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Attack)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Release)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Formant)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Breathiness)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Tension)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Airiness)] = true;
  // Gender is the coupled channel: it moves the tract's resonances and the source's spectrum together,
  // and only a carrier that owns both halves can apply it.
  result.supported[static_cast<std::size_t>(RendererControl::Gender)] = true;
  // Growl is a roughness of the source the carrier generates for itself.
  result.supported[static_cast<std::size_t>(RendererControl::Growl)] = true;
  result.pitchPreservingTransient = true;
  return result;
}

// The shipped neural worker renders from transferred audio and phonetic conditioning. It consumes the
// shared compiled pitch, timing, dynamics, vibrato, attack and release path, and it does not generate a
// designer-owned excitation or tract, so it refuses the six timbral channels exactly as the sample bank
// does. Its own conditioning controls are not claimed here until an admitted execution contract is
// shown to consume them.
RendererCapabilityView neuralCapabilities() noexcept {
  RendererCapabilityView result{.renderer = voicebank::RendererHint::Raw};
  result.supported[static_cast<std::size_t>(RendererControl::Pitch)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Timing)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Dynamics)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Vibrato)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Attack)] = true;
  result.supported[static_cast<std::size_t>(RendererControl::Release)] = true;
  result.pitchPreservingTransient = true;
  return result;
}

}  // namespace

RendererCapabilityView rendererCapabilities(RendererCarrier carrier) noexcept {
  // Exhaustive on purpose: a new carrier must state its own controls rather than inheriting the
  // source-filter set by falling through an else branch.
  switch (carrier) {
    case RendererCarrier::SampleBank:
      return makeCapabilities(voicebank::RendererHint::ClassicPsola);
    case RendererCarrier::SourceFilter:
      return sourceFilterCapabilities();
    case RendererCarrier::Neural:
      return neuralCapabilities();
  }
  return makeCapabilities(voicebank::RendererHint::ClassicPsola);
}

RendererCarrier rendererCarrierFor(const domain::VocalTrack& track) noexcept {
  if (track.neuralResource) return RendererCarrier::Neural;
  if (track.proceduralRecipe) return RendererCarrier::SourceFilter;
  return RendererCarrier::SampleBank;
}

RendererCapabilityView rendererCapabilities(
    voicebank::RendererHint renderer) noexcept {
  return makeCapabilities(renderer);
}

core::Result<RendererCapabilityDecision> validateRendererCapabilities(
    RendererCarrier carrier, const RendererControlRequest& request, bool allowRawFallback) {
  const auto selected = rendererCapabilities(carrier);
  RendererCapabilityDecision decision{
      .requested = selected,
      .fallback = selected,
      .canFallbackToRaw = false,
      .diagnostic = {},
  };
  std::string missing;
  for (std::size_t index = 0U; index < kRendererControlCount; ++index) {
    if (!request.required[index] || selected.supported[index]) continue;
    if (!missing.empty()) missing += ", ";
    missing += std::string{rendererControlName(static_cast<RendererControl>(index))};
  }
  if (missing.empty() &&
      (!request.requiresPitchPreservingTransient || selected.pitchPreservingTransient))
    return decision;
  // A carrier is not a hint: there is no second backend behind it to fall back to, and a control it
  // does not implement has to fail by name rather than disappear.
  static_cast<void>(allowRawFallback);
  decision.diagnostic = missing.empty()
                            ? "The selected carrier cannot preserve the required transient timing"
                            : "The selected carrier lacks required controls: " + missing;
  return core::failure<RendererCapabilityDecision>(core::ErrorCode::Unsupported, decision.diagnostic);
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
