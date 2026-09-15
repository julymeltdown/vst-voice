#include "seam/rendering/singer_route.hpp"

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
  synthesis::RendererControlRequest request;
  request.require(control);
  const auto decision = synthesis::validateRendererCapabilities(route.carrier, request);
  if (!decision)
    return core::failure(core::ErrorCode::Unsupported, decision.error().message);
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
