// The singing route is what a creator is shown before they write a song: which singer will render,
// what it supports, and why it will not play. These cases pin the answer for every carrier so a
// surface and the renderer cannot drift apart, and so an unreviewed-but-usable singer is never
// confused with an unusable one.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/rendering/singer_route.hpp"

#include <string>

namespace {

using namespace seam;

domain::SingerResourceIdentity identity(domain::SingerResourceKind kind, std::string id) {
  return domain::SingerResourceIdentity{.kind = kind, .id = std::move(id), .version = "1.0.0",
                                         .contentHash = std::string(64U, 'a')};
}

struct Song final {
  domain::Project project;
  domain::TrackId track;
};

Song makeSong() {
  application::ProjectFactory factory{9901U};
  auto project = factory.createProject("Singer route");
  const auto track = factory.addVocalTrack(project, "Pilot");
  return Song{std::move(project), track};
}

}  // namespace

TEST_CASE("A track with no original singer resolves the sample bank and no timbral channel") {
  auto song = makeSong();
  const auto route = rendering::resolveSingerRoute(song.project, song.track);
  CHECK(route.hasValue());
  if (!route) return;
  CHECK(route.value().carrier == synthesis::RendererCarrier::SampleBank);
  CHECK(route.value().renderable());
  CHECK(route.value().resource.kind == domain::SingerResourceKind::Sample);
  CHECK(!route.value().supportsControl(synthesis::RendererControl::Formant));
  CHECK(route.value().supportsControl(synthesis::RendererControl::Pitch));
  const auto refused = rendering::validateRouteControl(route.value(), synthesis::RendererControl::Growl);
  CHECK(!refused.hasValue());
  CHECK(refused.error().message.find("growl") != std::string::npos);
}

TEST_CASE("A procedural singer resolves the source-filter carrier and its six channels") {
  auto song = makeSong();
  auto* track = song.project.findVocalTrack(song.track);
  CHECK(track != nullptr);
  if (track == nullptr) return;
  track->proceduralRecipe = domain::ProceduralRecipeReference{
      .resource = identity(domain::SingerResourceKind::Procedural, "preset"),
      .path = "recipe.json", .style = "neutral"};
  rendering::SingerRouteEnvironment environment{};
  environment.renderableEngineId = "seam.source-filter.v1";
  environment.renderableEngineRevision = 14U;
  environment.declaration = rendering::SingerRouteDeclaration{.engineId = "seam.source-filter.v1",
                                                               .engineRevision = 14U,
                                                               .language = "ja",
                                                               .styles = {"neutral"},
                                                               .phones = {"a"},
                                                               .declared = true};
  const auto route = rendering::resolveSingerRoute(song.project, song.track, environment);
  CHECK(route.hasValue());
  if (!route) return;
  CHECK(route.value().carrier == synthesis::RendererCarrier::SourceFilter);
  CHECK(route.value().renderable());
  CHECK(route.value().declaredLanguage());
  for (const auto control : {synthesis::RendererControl::Formant, synthesis::RendererControl::Breathiness,
                             synthesis::RendererControl::Tension, synthesis::RendererControl::Airiness,
                             synthesis::RendererControl::Gender, synthesis::RendererControl::Growl})
    CHECK(route.value().supportsControl(control));
  CHECK(rendering::validateRouteControl(route.value(), synthesis::RendererControl::Growl).hasValue());
}

TEST_CASE("A singer built for another engine revision is reported, not silently played") {
  auto song = makeSong();
  auto* track = song.project.findVocalTrack(song.track);
  CHECK(track != nullptr);
  if (track == nullptr) return;
  track->proceduralRecipe = domain::ProceduralRecipeReference{
      .resource = identity(domain::SingerResourceKind::Procedural, "preset"),
      .path = "recipe.json", .style = "neutral"};
  rendering::SingerRouteEnvironment environment{};
  environment.renderableEngineId = "seam.source-filter.v1";
  environment.renderableEngineRevision = 14U;
  environment.declaration = rendering::SingerRouteDeclaration{.engineId = "seam.source-filter.v1",
                                                               .engineRevision = 13U, .declared = true};
  const auto route = rendering::resolveSingerRoute(song.project, song.track, environment);
  CHECK(route.hasValue());
  if (!route) return;
  CHECK(route.value().status == rendering::SingerRouteStatus::IncompatibleEngine);
  CHECK(!route.value().renderable());
  CHECK(route.value().diagnostic.find("13") != std::string::npos);
  // An incompatible route refuses every control rather than appearing editable.
  CHECK(!route.value().supportsControl(synthesis::RendererControl::Pitch));
}

TEST_CASE("A neural singer is its own carrier and does not inherit the six channels") {
  auto song = makeSong();
  auto* track = song.project.findVocalTrack(song.track);
  CHECK(track != nullptr);
  if (track == nullptr) return;
  track->neuralResource = domain::NeuralResourceReference{
      .resource = identity(domain::SingerResourceKind::Neural, "model")};
  const auto route = rendering::resolveSingerRoute(song.project, song.track);
  CHECK(route.hasValue());
  if (!route) return;
  CHECK(route.value().carrier == synthesis::RendererCarrier::Neural);
  CHECK(route.value().renderable());
  CHECK(route.value().supportsControl(synthesis::RendererControl::Pitch));
  CHECK(!route.value().supportsControl(synthesis::RendererControl::Formant));
  const auto refused = rendering::validateRouteControl(route.value(), synthesis::RendererControl::Formant);
  CHECK(!refused.hasValue());
}

TEST_CASE("An admitted neural control extends only that resolved route") {
  auto song = makeSong();
  auto* track = song.project.findVocalTrack(song.track);
  CHECK(track != nullptr);
  if (track == nullptr) return;
  track->neuralResource = domain::NeuralResourceReference{
      .resource = identity(domain::SingerResourceKind::Neural, "conditioned")};
  rendering::SingerRouteEnvironment environment{};
  environment.neuralConditioningControls = {synthesis::RendererControl::Breathiness};
  const auto route = rendering::resolveSingerRoute(song.project, song.track, environment);
  CHECK(route.hasValue());
  if (!route) return;
  CHECK(route.value().supportsControl(synthesis::RendererControl::Breathiness));
  CHECK(rendering::validateRouteControl(route.value(), synthesis::RendererControl::Breathiness));
  CHECK(!route.value().supportsControl(synthesis::RendererControl::Formant));
}

TEST_CASE("An unavailable singer is reported with its reason and stays unreviewed") {
  auto song = makeSong();
  auto* track = song.project.findVocalTrack(song.track);
  CHECK(track != nullptr);
  if (track == nullptr) return;
  track->proceduralRecipe = domain::ProceduralRecipeReference{
      .resource = identity(domain::SingerResourceKind::Procedural, "preset"),
      .path = "recipe.json", .style = "neutral"};
  rendering::SingerRouteEnvironment environment{};
  environment.available = false;
  environment.unavailableReason = "The installed singer is missing from this machine";
  const auto route = rendering::resolveSingerRoute(song.project, song.track, environment);
  CHECK(route.hasValue());
  if (!route) return;
  CHECK(route.value().status == rendering::SingerRouteStatus::Unavailable);
  CHECK(!route.value().renderable());
  CHECK(!route.value().reviewed);
  CHECK(route.value().diagnostic.find("missing") != std::string::npos);
}

TEST_CASE("A track that records two singer families is refused rather than resolved") {
  auto song = makeSong();
  auto* track = song.project.findVocalTrack(song.track);
  CHECK(track != nullptr);
  if (track == nullptr) return;
  track->proceduralRecipe = domain::ProceduralRecipeReference{
      .resource = identity(domain::SingerResourceKind::Procedural, "preset"),
      .path = "recipe.json", .style = "neutral"};
  track->neuralResource = domain::NeuralResourceReference{
      .resource = identity(domain::SingerResourceKind::Neural, "model")};
  const auto route = rendering::resolveSingerRoute(song.project, song.track);
  CHECK(!route.hasValue());
  if (!route) CHECK(route.error().code == core::ErrorCode::InvalidArgument);
}

TEST_CASE("Resolving a track that does not exist is a not-found, not a sample carrier") {
  auto song = makeSong();
  const auto route = rendering::resolveSingerRoute(song.project, domain::TrackId{});
  CHECK(!route.hasValue());
  if (!route) CHECK(route.error().code == core::ErrorCode::NotFound);
}

// The creator must be able to see what a singer supports before choosing it. This checks the summary
// names the carrier and the controls it will actually apply, and that an unusable singer reports its
// reason instead of an empty or misleading capability list.
TEST_CASE("A route summary names the supported controls and an unusable reason") {
  auto song = makeSong();
  auto* track = song.project.findVocalTrack(song.track);
  CHECK(track != nullptr);
  if (track == nullptr) return;
  track->proceduralRecipe = domain::ProceduralRecipeReference{
      .resource = identity(domain::SingerResourceKind::Procedural, "preset"),
      .path = "recipe.json", .style = "neutral"};
  rendering::SingerRouteEnvironment environment{};
  environment.renderableEngineId = "seam.source-filter.v1";
  environment.renderableEngineRevision = 14U;
  environment.declaration = rendering::SingerRouteDeclaration{.engineId = "seam.source-filter.v1",
                                                               .engineRevision = 14U, .language = "ja",
                                                               .declared = true};
  const auto resolved = rendering::resolveSingerRoute(song.project, song.track, environment);
  CHECK(resolved.hasValue());
  if (!resolved) return;
  const auto summary = rendering::singerRouteCapabilitySummary(resolved.value());
  CHECK(summary.find("voice designer") != std::string::npos);
  CHECK(summary.find("formant") != std::string::npos);
  CHECK(summary.find("growl") != std::string::npos);
  CHECK(summary.find("language ja") != std::string::npos);
  CHECK(summary.find("unreviewed") != std::string::npos);

  // A neural route must not advertise the timbral channels in its own summary either.
  auto* neuralTrack = song.project.findVocalTrack(song.track);
  CHECK(neuralTrack != nullptr);
  if (neuralTrack == nullptr) return;
  neuralTrack->proceduralRecipe.reset();
  neuralTrack->neuralResource = domain::NeuralResourceReference{
      .resource = identity(domain::SingerResourceKind::Neural, "model")};
  const auto neuralRoute = rendering::resolveSingerRoute(song.project, song.track);
  CHECK(neuralRoute.hasValue());
  if (!neuralRoute) return;
  const auto neuralSummary = rendering::singerRouteCapabilitySummary(neuralRoute.value());
  CHECK(neuralSummary.find("neural singer") != std::string::npos);
  CHECK(neuralSummary.find("formant") == std::string::npos);
  CHECK(neuralSummary.find("pitch") != std::string::npos);

  // An unavailable singer reports why, not a capability list.
  rendering::SingerRouteEnvironment missing{};
  missing.available = false;
  missing.unavailableReason = "the installed singer is missing";
  const auto missingRoute = rendering::resolveSingerRoute(song.project, song.track, missing);
  CHECK(missingRoute.hasValue());
  if (!missingRoute) return;
  const auto missingSummary = rendering::singerRouteCapabilitySummary(missingRoute.value());
  CHECK(missingSummary.find("missing") != std::string::npos);
}
