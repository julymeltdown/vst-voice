#include "seam/rendering/singer_route.hpp"
#include "seam/synthesis/renderer_dispatcher.hpp"

#include <algorithm>
#include <array>

namespace seam::rendering {
namespace {

std::string engineDiagnostic(const SingerRouteDeclaration& declaration,
                             const SingerRouteEnvironment& environment) {
  std::string message = "The installed singer was built for engine '" + declaration.engineId +
                        "' revision " + std::to_string(declaration.engineRevision) +
                        ", which this build does not render";
  if (!environment.renderableEngineId.empty()) {
    message += " (this build renders '" + environment.renderableEngineId + "' revision " +
               std::to_string(environment.renderableEngineRevision) + ")";
  }
  return message;
}

}  // namespace

SingerRouteEnvironment sampleSingerRouteEnvironment(
    const domain::VocalTrack& track, const voicebank::Manifest& manifest) {
  SingerRouteEnvironment result;
  auto style = track.styleSelection.styleId;
  if (style.empty()) {
    if (manifest.styles.size() != 1U ||
        track.styleSelection.origin == domain::VoiceStyleOrigin::LegacyNeedsExactBankResolution) {
      result.available = false; result.unavailableReason = "Select an exact sample style before editing formants";
      return result;
    }
    style = manifest.styles.front();
  }
  bool primaryPresent = false, secondaryPresent = !track.styleSelection.blend;
  const auto append = [&](voicebank::RendererHint renderer) {
    if (std::find(result.sampleRenderers.begin(), result.sampleRenderers.end(), renderer) == result.sampleRenderers.end())
      result.sampleRenderers.push_back(renderer);
  };
  for (const auto& unit : manifest.units) {
    if (!unit.enabled) continue;
    if (unit.style != style &&
        (!track.styleSelection.blend || unit.style != track.styleSelection.blend->targetStyleId)) continue;
    primaryPresent |= unit.style == style;
    secondaryPresent |= track.styleSelection.blend && unit.style == track.styleSelection.blend->targetStyleId;
    append(unit.renderer);
  }
  for (const auto& region : track.regions) {
    for (const auto& overrideValue : region.unitSelectionOverrides) {
      if (overrideValue.unresolved) continue;
      const auto* unit = manifest.findUnit(overrideValue.unitId);
      if (!unit || !unit->enabled) { result.available = false; result.unavailableReason = "Selected sample unit is absent or disabled"; continue; }
      append(synthesis::resolveRequestedRenderer(*unit, synthesis::RenderPolicy::RespectVoicebank, overrideValue.renderer));
    }
  }
  if (!primaryPresent || !secondaryPresent) {
    result.available = false;
    result.unavailableReason = "No enabled sample units in a selected style";
  }
  return result;
}

core::Result<ResolvedSingerRoute> resolveSingerRoute(
    const domain::Project& project, domain::TrackId trackId,
    const SingerRouteEnvironment& environment) {
  const auto* track = project.findVocalTrack(trackId);
  if (track == nullptr)
    return core::failure<ResolvedSingerRoute>(core::ErrorCode::NotFound,
                                              "No vocal track has that identity");
  if (track->proceduralRecipe && track->neuralResource)
    return core::failure<ResolvedSingerRoute>(core::ErrorCode::InvalidArgument,
        "A track selects one singer family, not a procedural and a neural selection together");

  domain::SingerResourceIdentity resource{};
  if (track->neuralResource) {
    resource = track->neuralResource->resource;
  } else if (track->proceduralRecipe) {
    resource = track->proceduralRecipe->resource;
  } else {
    resource = domain::SingerResourceIdentity{
        .kind = domain::SingerResourceKind::Sample,
        .id = track->voicebank.id,
        .version = track->voicebank.version,
        .contentHash = track->voicebank.contentHash};
  }
  auto route = resolveSingerRouteForResource(resource, synthesis::rendererCarrierFor(*track), environment);
  route.trackId = trackId;
  return route;
}

ResolvedSingerRoute resolveSingerRouteForResource(
    const domain::SingerResourceIdentity& resource, synthesis::RendererCarrier carrier,
    const SingerRouteEnvironment& environment) {
  ResolvedSingerRoute route{};
  route.resource = resource;
  route.carrier = carrier;
  route.capabilities = synthesis::rendererCapabilities(carrier);
  if (carrier == synthesis::RendererCarrier::SampleBank && !environment.sampleRenderers.empty()) {
    route.capabilities = synthesis::rendererCapabilities(environment.sampleRenderers.front());
    for (const auto renderer : environment.sampleRenderers) {
      const auto next = synthesis::rendererCapabilities(renderer);
      for (std::size_t i = 0; i < synthesis::kRendererControlCount; ++i)
        route.capabilities.supported[i] = route.capabilities.supported[i] && next.supported[i];
      route.capabilities.pitchPreservingTransient &= next.pitchPreservingTransient;
    }
  }
  if (carrier == synthesis::RendererCarrier::Neural) {
    for (const auto control : environment.neuralConditioningControls)
      route.capabilities.supported[static_cast<std::size_t>(control)] = true;
  }
  route.declaration = environment.declaration;
  route.reviewed = environment.reviewed;
  route.reviewDetail = environment.reviewDetail;

  if (!environment.available) {
    route.status = SingerRouteStatus::Unavailable;
    route.diagnostic = environment.unavailableReason.empty()
                           ? "The selected singer is not available on this machine"
                           : environment.unavailableReason;
    return route;
  }

  // The engine check applies to a distributed recipe. A source-filter singer whose declared engine or
  // revision is not the one this build renders must be reported as incompatible rather than played by
  // a different renderer that would sound different; a resource that declared nothing is not checked.
  if (carrier == synthesis::RendererCarrier::SourceFilter && environment.declaration.declared &&
      (!environment.declaration.engineId.empty() || environment.declaration.engineRevision != 0U)) {
    const bool engineMatches = environment.renderableEngineId == environment.declaration.engineId &&
                               environment.renderableEngineRevision == environment.declaration.engineRevision;
    if (!engineMatches) {
      route.status = SingerRouteStatus::IncompatibleEngine;
      route.diagnostic = engineDiagnostic(environment.declaration, environment);
      return route;
    }
  }

  route.status = SingerRouteStatus::Resolved;
  return route;
}

core::Result<void> validateRouteControl(const ResolvedSingerRoute& route,
                                        synthesis::RendererControl control) {
  if (!route.renderable())
    return core::failure(core::ErrorCode::Unsupported,
        "The selected singer cannot render this phrase: " + route.diagnostic);
  if (!route.capabilities.supports(control))
    return core::failure(core::ErrorCode::Unsupported,
        "The selected singer lacks required control: " +
        std::string{synthesis::rendererControlName(control)});
  return core::success();
}

std::string_view singerRouteStatusName(SingerRouteStatus status) noexcept {
  switch (status) {
    case SingerRouteStatus::Unavailable: return "unavailable";
    case SingerRouteStatus::Resolved: return "resolved";
    case SingerRouteStatus::IncompatibleEngine: return "incompatible-engine";
  }
  return "unavailable";
}

std::string_view singerRouteCarrierName(synthesis::RendererCarrier carrier) noexcept {
  switch (carrier) {
    case synthesis::RendererCarrier::SampleBank: return "sample bank";
    case synthesis::RendererCarrier::SourceFilter: return "voice designer";
    case synthesis::RendererCarrier::Neural: return "neural singer";
  }
  return "unknown carrier";
}

std::string singerRouteCapabilitySummary(const ResolvedSingerRoute& route) {
  // Every control the route will actually apply is named, and nothing else is. A creator reading this
  // label is being told the same answer the renderer will give, which is the point of one resolver.
  static constexpr std::array<synthesis::RendererControl, 13U> kAllControls{
      synthesis::RendererControl::Pitch,       synthesis::RendererControl::Timing,
      synthesis::RendererControl::Dynamics,    synthesis::RendererControl::Vibrato,
      synthesis::RendererControl::Attack,      synthesis::RendererControl::Release,
      synthesis::RendererControl::Formant,     synthesis::RendererControl::Breathiness,
      synthesis::RendererControl::Tension,     synthesis::RendererControl::Airiness,
      synthesis::RendererControl::Gender,      synthesis::RendererControl::StyleBlend,
      synthesis::RendererControl::Growl};
  std::string summary{singerRouteCarrierName(route.carrier)};
  if (!route.renderable()) {
    summary += " (" + std::string{singerRouteStatusName(route.status)} + ")";
    if (!route.diagnostic.empty()) summary += ": " + route.diagnostic;
    return summary;
  }
  std::string controls;
  for (const auto control : kAllControls) {
    if (!route.supportsControl(control)) continue;
    if (!controls.empty()) controls += ", ";
    controls += std::string{synthesis::rendererControlName(control)};
  }
  summary += ": " + (controls.empty() ? std::string{"no editable controls"} : controls);
  if (route.declaredLanguage()) summary += " | language " + route.declaration.language;
  summary += route.reviewed ? " | reviewed" : " | unreviewed";
  return summary;
}

}  // namespace seam::rendering
