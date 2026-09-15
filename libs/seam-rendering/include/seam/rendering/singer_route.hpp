#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/domain/performance_intent.hpp"
#include "seam/synthesis/renderer_capabilities.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace seam::rendering {

// What an installed resource declares about itself. These fields come from the singer's own signed
// metadata, not from this build, so they are supplied by the layer that already scanned the resource
// rather than re-derived here. An empty declaration means the resource's metadata was not read, which
// is reported as undeclared rather than as a supported capability.
struct SingerRouteDeclaration final {
  std::string engineId;
  std::uint32_t engineRevision{0U};
  std::string language;
  std::vector<std::string> styles;
  std::vector<std::string> phones;
  // True only when the fields above were actually read from the installed resource. A default-made
  // declaration must not be mistaken for a resource that declares an empty language or no styles.
  bool declared{false};
};

// Whether the recorded selection can actually produce sound in this build. This is separate from
// review and from declared coverage: a resource may be perfectly renderable and still unreviewed, and
// a reviewed resource may still be built for an engine revision this build cannot render.
enum class SingerRouteStatus {
  // No resource is available for the recorded selection here.
  Unavailable,
  // The resource is available and this build renders it.
  Resolved,
  // The resource is available and trusted, but declares an engine or engine revision this build does
  // not implement, so it is present and not playable. Reported instead of shown as silence.
  IncompatibleEngine,
};

// The single answer to "what will render this track's sound, and what does that route support" used by
// the picker, the expression lane, preview and export. It is derived on demand and never persisted:
// capability is a function of the selected singer and the region, so it does not belong in a project
// schema. The six timbral channels differ by carrier here, which is what stops a surface from
// advertising a control the renderer refuses.
struct ResolvedSingerRoute final {
  domain::TrackId trackId{};
  domain::SingerResourceIdentity resource{};
  synthesis::RendererCarrier carrier{synthesis::RendererCarrier::SampleBank};
  synthesis::RendererCapabilityView capabilities{};
  SingerRouteStatus status{SingerRouteStatus::Resolved};
  // Human-readable reason a route is unavailable or incompatible. Empty when the route is resolved.
  std::string diagnostic;
  SingerRouteDeclaration declaration{};
  // Whether a recorded review decision still covers this exact resource. Unreviewed is not unusable:
  // the two are reported separately so a creator knows which of them they are looking at.
  bool reviewed{false};
  std::string reviewDetail;

  [[nodiscard]] bool renderable() const noexcept {
    return status == SingerRouteStatus::Resolved;
  }
  [[nodiscard]] bool supportsControl(synthesis::RendererControl control) const noexcept {
    return renderable() && capabilities.supports(control);
  }
  [[nodiscard]] bool declaredLanguage() const noexcept {
    return declaration.declared && !declaration.language.empty() && declaration.language != "und";
  }
};

// Build-time and resource facts the resolver cannot read from the project. The engine identity is this
// build's declaration of what it can render; availability and review come from whatever install scan
// the caller already performed. Defaults describe a build that declares no engine and a resource that
// is available, which keeps a sample-bank route resolvable without extra inputs.
struct SingerRouteEnvironment final {
  std::string renderableEngineId;
  std::uint32_t renderableEngineRevision{0U};
  SingerRouteDeclaration declaration{};
  bool available{true};
  bool reviewed{false};
  std::string reviewDetail;
  // Why the resource is unavailable, when the caller knows. Used verbatim in the diagnostic.
  std::string unavailableReason;
};

// Resolve one track's singing route. A track with neither a procedural nor a neural selection is the
// sample bank; a procedural selection is checked against the engine this build renders; a neural
// selection is its own carrier. A track that records both is invalid and is refused rather than
// silently resolved.
[[nodiscard]] core::Result<ResolvedSingerRoute> resolveSingerRoute(
    const domain::Project& project, domain::TrackId trackId,
    const SingerRouteEnvironment& environment = {});

// Resolve a route whose singer identity and carrier are already known, for a surface that is offering
// an installed resource rather than editing a track. It applies exactly the same availability and
// engine-revision rules as the track path, so an offer and a track cannot disagree about what a
// singer supports. A source-filter singer is the carrier an installed procedural recipe renders on.
[[nodiscard]] ResolvedSingerRoute resolveSingerRouteForResource(
    const domain::SingerResourceIdentity& resource, synthesis::RendererCarrier carrier,
    const SingerRouteEnvironment& environment);

// The same check a surface performs before offering an edit, phrased once so the picker, the lane and
// the export preflight agree. A resolved route that does not support the control fails by name.
[[nodiscard]] core::Result<void> validateRouteControl(
    const ResolvedSingerRoute& route, synthesis::RendererControl control);

[[nodiscard]] std::string_view singerRouteStatusName(SingerRouteStatus status) noexcept;

// A short, human-readable statement of what a resolved route supports, for the surface that offers a
// singer. It names the timbral controls a creator would otherwise only discover by being refused, so
// the choice is informed before a song is written. An unresolvable or incompatible route reports its
// reason instead of a capability list. The wording is presentation, not a claim about musical quality.
[[nodiscard]] std::string singerRouteCapabilitySummary(const ResolvedSingerRoute& route);

// The carrier's own name, for the same surface. Kept beside the capability summary so a label cannot
// name a carrier the resolver did not choose.
[[nodiscard]] std::string_view singerRouteCarrierName(synthesis::RendererCarrier carrier) noexcept;

}  // namespace seam::rendering
