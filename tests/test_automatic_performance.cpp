#include "test_framework.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/performance_commands.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/synthesis/automatic_performance.hpp"

#include <algorithm>
#include <memory>
#include <stop_token>
#include <string>

namespace {

struct Fixture final {
  seam::application::ProjectFactory factory{3000U};
  seam::domain::Project project{factory.createProject("Automatic performance")};
  seam::domain::TrackId track{};
  seam::domain::RegionId regionId{};

  Fixture() {
    track = factory.addVocalTrack(project, "Lead");
    regionId = factory.addRegion(project, track, "Verse", seam::time::Tick{0},
                                  seam::time::Tick{1920});
    auto* region = project.findRegion(regionId);
    auto first = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{960},
                                  60U, U"か", seam::domain::Language::Japanese);
    auto second = factory.makeNote(seam::time::Tick{960}, seam::time::Tick{960},
                                   64U, U"き", seam::domain::Language::Japanese);
    region->lyrics.push_back(std::move(first.first));
    region->notes.push_back(std::move(first.second));
    region->lyrics.push_back(std::move(second.first));
    region->notes.push_back(std::move(second.second));
    region->sortNotes();
  }

  seam::phonemizer::ResolvedPronunciation pronunciation() const {
    return seam::phonemizer::resolvePronunciation(
               *project.findRegion(regionId))
        .value();
  }

  seam::synthesis::AutomaticPerformanceRequest request(
      const seam::phonemizer::ResolvedPronunciation& pronunciation) const {
    return seam::synthesis::AutomaticPerformanceRequest{
        .takeId = "take-automatic-01",
        .regionId = regionId,
        .capturedRevision = project.findRegion(regionId)->performance.revision,
        .resource = {seam::domain::SingerResourceKind::Neural,
                     "fixture-neural", "1.0.0", std::string(64U, 'a')},
        .pronunciation = pronunciation.identity,
        .generatorId = "fixture-generator",
        .generatorVersion = "1.0.0",
        .seed = 42U,
        .range = {seam::time::Tick{0}, seam::time::Tick{1920}},
    };
  }
};

}  // namespace

TEST_CASE("automatic performance generation is deterministic and side-effect free") {
  Fixture fixture;
  const auto before = fixture.project;
  const auto pronunciation = fixture.pronunciation();
  const auto first = seam::synthesis::generateAutomaticPerformance(
      fixture.project, *fixture.project.findRegion(fixture.regionId),
      pronunciation, fixture.request(pronunciation));
  const auto second = seam::synthesis::generateAutomaticPerformance(
      fixture.project, *fixture.project.findRegion(fixture.regionId),
      pronunciation, fixture.request(pronunciation));
  CHECK(first);
  CHECK(second);
  CHECK(first.value() == second.value());
  CHECK(first.value().state == seam::domain::PerformanceProposalState::Proposed);
  CHECK(first.value().lanes.size() == 4U);
  CHECK(fixture.project == before);
  CHECK(first.value().validate());
  for (const auto& lane : first.value().lanes) {
    CHECK(!lane.points.empty());
    CHECK(std::is_sorted(lane.points.begin(), lane.points.end(),
                         [](const auto& lhs, const auto& rhs) {
                           return lhs.tick < rhs.tick;
                         }));
  }
}

TEST_CASE("automatic performance proposal flows through stale-safe acceptance commands") {
  Fixture fixture;
  const auto pronunciation = fixture.pronunciation();
  auto proposal = seam::synthesis::generateAutomaticPerformance(
      fixture.project, *fixture.project.findRegion(fixture.regionId),
      pronunciation, fixture.request(pronunciation));
  CHECK(proposal);
  const auto before = fixture.project.findRegion(fixture.regionId)->performance;
  seam::application::EditorSession session{fixture.project};
  CHECK(session.execute(std::make_unique<seam::application::AddPerformanceProposalCommand>(
      fixture.regionId, before, proposal.value())));
  const auto& state = session.project().findRegion(fixture.regionId)->performance;
  CHECK(state.takes.size() == 1U);
  const std::vector<seam::domain::AcceptedPerformanceSelection> accepted{
      {proposal.value().id, seam::domain::PerformanceChannel::Pitch,
       seam::domain::PerformanceTimeRange{seam::time::Tick{0}, seam::time::Tick{1920}},
       seam::time::Tick{0}}};
  CHECK(session.execute(std::make_unique<seam::application::SetAcceptedPerformanceCommand>(
      fixture.regionId, state, accepted)));
  CHECK(session.project().findRegion(fixture.regionId)->performance.accepted ==
        accepted);
  CHECK(session.undo());
  CHECK(session.project().findRegion(fixture.regionId)->performance.accepted.empty());
}

TEST_CASE("automatic performance rejects stale, cancelled and advanced requests") {
  Fixture fixture;
  const auto pronunciation = fixture.pronunciation();
  auto stale = fixture.request(pronunciation);
  ++stale.capturedRevision.musical;
  CHECK(!seam::synthesis::generateAutomaticPerformance(
      fixture.project, *fixture.project.findRegion(fixture.regionId),
      pronunciation, stale));

  auto advanced = fixture.request(pronunciation);
  advanced.channels = {seam::domain::PerformanceChannel::Growl};
  const auto unsupported = seam::synthesis::generateAutomaticPerformance(
      fixture.project, *fixture.project.findRegion(fixture.regionId),
      pronunciation, advanced);
  CHECK(!unsupported);
  CHECK(unsupported.error().code == seam::core::ErrorCode::Unsupported);

  std::stop_source stop;
  stop.request_stop();
  CHECK(!seam::synthesis::generateAutomaticPerformance(
      fixture.project, *fixture.project.findRegion(fixture.regionId),
      pronunciation, fixture.request(pronunciation), stop.get_token()));
}
