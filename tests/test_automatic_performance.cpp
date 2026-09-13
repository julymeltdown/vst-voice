#include "test_framework.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/performance_commands.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/authoring/automatic_performance_capture.hpp"
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

namespace {

// Four notes in one phrase, with a consonant onset on every note, so the
// phrase-aware backend has arch, scoop and resolution positions to shape.
struct PhraseFixture final {
  seam::application::ProjectFactory factory{4000U};
  seam::domain::Project project{factory.createProject("Phrase proposal")};
  seam::domain::TrackId track{};
  seam::domain::RegionId regionId{};

  PhraseFixture() {
    track = factory.addVocalTrack(project, "Lead");
    regionId = factory.addRegion(project, track, "Verse", seam::time::Tick{0},
                                 seam::time::Tick{1920});
    auto* region = project.findRegion(regionId);
    const std::array<std::pair<char32_t, std::uint8_t>, 4U> notes{{
        {U'か', 60U}, {U'き', 64U}, {U'く', 67U}, {U'け', 62U}}};
    for (std::size_t index = 0U; index < notes.size(); ++index) {
      auto [lyric, note] = factory.makeNote(
          seam::time::Tick{static_cast<std::int64_t>(index) * 480}, seam::time::Tick{480},
          notes[index].second, std::u32string(1U, notes[index].first),
          seam::domain::Language::Japanese);
      region->lyrics.push_back(std::move(lyric));
      region->notes.push_back(std::move(note));
    }
    region->sortNotes();
  }

  seam::phonemizer::ResolvedPronunciation pronunciation() const {
    return seam::phonemizer::resolvePronunciation(*project.findRegion(regionId)).value();
  }

  seam::synthesis::AutomaticPerformanceRequest request(
      const seam::phonemizer::ResolvedPronunciation& pronunciation,
      std::uint64_t seed = 7U,
      std::string generatorId = std::string{seam::synthesis::kPhraseAwareGeneratorId},
      std::string generatorVersion =
          std::string{seam::synthesis::kPhraseAwareGeneratorVersion}) const {
    return seam::synthesis::AutomaticPerformanceRequest{
        .takeId = "proposal-01",
        .regionId = regionId,
        .capturedRevision = project.findRegion(regionId)->performance.revision,
        .resource = {seam::domain::SingerResourceKind::Neural, "fixture-neural", "1.0.0",
                     std::string(64U, 'b')},
        .pronunciation = pronunciation.identity,
        .generatorId = std::move(generatorId),
        .generatorVersion = std::move(generatorVersion),
        .seed = seed,
        .range = {seam::time::Tick{0}, seam::time::Tick{1920}},
    };
  }
};

const seam::domain::PerformanceLane& laneOf(const seam::domain::PerformanceTake& take,
    seam::domain::PerformanceChannel channel) {
  const auto found = std::find_if(take.lanes.begin(), take.lanes.end(),
      [channel](const auto& lane) { return lane.channel == channel; });
  if (found == take.lanes.end()) throw seam::test::Failure{"expected performance lane is missing"};
  return *found;
}

double valueAt(const seam::domain::PerformanceLane& lane, seam::time::Tick tick) {
  const auto found = std::find_if(lane.points.begin(), lane.points.end(),
      [tick](const auto& point) { return point.tick == tick; });
  if (found == lane.points.end() || !found->value.has_value())
    throw seam::test::Failure{"expected performance point is missing"};
  return *found->value;
}

}  // namespace

TEST_CASE("phrase proposal shapes the score and records its own identity") {
  PhraseFixture fixture;
  const auto before = fixture.project;
  const auto pronunciation = fixture.pronunciation();
  const auto request = fixture.request(pronunciation);
  const auto proposal = seam::synthesis::generatePhraseAwarePerformance(
      fixture.project, *fixture.project.findRegion(fixture.regionId), pronunciation, request);
  CHECK(proposal);
  if (!proposal) return;
  CHECK(fixture.project == before);
  CHECK(proposal.value().state == seam::domain::PerformanceProposalState::Proposed);
  CHECK(proposal.value().generatorId == seam::synthesis::kPhraseAwareGeneratorId);
  CHECK(proposal.value().generatorVersion == seam::synthesis::kPhraseAwareGeneratorVersion);
  CHECK(proposal.value().seed == request.seed);
  CHECK(proposal.value().resource == request.resource);
  CHECK(proposal.value().pronunciation == request.pronunciation);
  CHECK(proposal.value().lanes.size() == 4U);
  CHECK(proposal.value().validate());

  const auto& region = *fixture.project.findRegion(fixture.regionId);
  const auto& pitch = laneOf(proposal.value(), seam::domain::PerformanceChannel::Pitch);
  // The phrase opens low (onset scoop) and arches above the written pitch toward the
  // middle, which is the structural shaping this backend exists to provide.
  const double firstWritten = static_cast<double>(region.notes[0].midiKey) * 100.0;
  CHECK(valueAt(pitch, region.notes[0].startTick) < firstWritten - 20.0);
  const double secondWritten = static_cast<double>(region.notes[1].midiKey) * 100.0;
  CHECK(valueAt(pitch, region.notes[1].startTick) > secondWritten + 10.0);
  // The last note settles exactly on its written pitch.
  const double lastWritten = static_cast<double>(region.notes[3].midiKey) * 100.0;
  CHECK(valueAt(pitch, region.notes[3].endTick()) == lastWritten);

  const auto& attack = laneOf(proposal.value(), seam::domain::PerformanceChannel::Attack);
  const auto& release = laneOf(proposal.value(), seam::domain::PerformanceChannel::Release);
  CHECK(valueAt(attack, region.notes[0].startTick) == 25.0);
  CHECK(valueAt(attack, region.notes[1].startTick) == 35.0);
  CHECK(valueAt(release, region.notes[0].endTick()) == 40.0);
  CHECK(valueAt(release, region.notes[3].endTick()) == 120.0);
}

TEST_CASE("phrase proposal is deterministic per seed and refuses foreign identity") {
  PhraseFixture fixture;
  const auto pronunciation = fixture.pronunciation();
  const auto& region = *fixture.project.findRegion(fixture.regionId);
  const auto first = seam::synthesis::generatePhraseAwarePerformance(
      fixture.project, region, pronunciation, fixture.request(pronunciation, 7U));
  const auto repeat = seam::synthesis::generatePhraseAwarePerformance(
      fixture.project, region, pronunciation, fixture.request(pronunciation, 7U));
  const auto other = seam::synthesis::generatePhraseAwarePerformance(
      fixture.project, region, pronunciation, fixture.request(pronunciation, 8U));
  CHECK(first && repeat && other);
  if (!first || !repeat || !other) return;
  CHECK(first.value() == repeat.value());
  // A different seed humanizes the pitch lane but cannot change the structural
  // articulation decisions, because those come from the score.
  CHECK(laneOf(first.value(), seam::domain::PerformanceChannel::Pitch) !=
        laneOf(other.value(), seam::domain::PerformanceChannel::Pitch));
  CHECK(laneOf(first.value(), seam::domain::PerformanceChannel::Attack) ==
        laneOf(other.value(), seam::domain::PerformanceChannel::Attack));
  CHECK(laneOf(first.value(), seam::domain::PerformanceChannel::Release) ==
        laneOf(other.value(), seam::domain::PerformanceChannel::Release));

  const auto foreign = seam::synthesis::generatePhraseAwarePerformance(fixture.project,
      region, pronunciation, fixture.request(pronunciation, 7U, "seam-deterministic-performance"));
  CHECK(!foreign);
  if (foreign) return;
  CHECK(foreign.error().code == seam::core::ErrorCode::Unsupported);
  const auto unknownVersion = seam::synthesis::generatePhraseAwarePerformance(
      fixture.project, region, pronunciation, fixture.request(pronunciation, 7U,
          std::string{seam::synthesis::kPhraseAwareGeneratorId}, "99"));
  CHECK(!unknownVersion);
  CHECK(unknownVersion.error().code == seam::core::ErrorCode::Unsupported);
}

TEST_CASE("automatic performance capture adopts a proposal and refuses a stale session") {
  PhraseFixture fixture;
  seam::application::EditorSession session{fixture.project};
  const seam::domain::SingerResourceIdentity resource{
      seam::domain::SingerResourceKind::Neural, "fixture-neural", "1.0.0",
      std::string(64U, 'b')};
  const std::vector<seam::domain::PerformanceChannel> channels{
      seam::domain::PerformanceChannel::Pitch, seam::domain::PerformanceChannel::Dynamics,
      seam::domain::PerformanceChannel::Attack, seam::domain::PerformanceChannel::Release};
  auto prepared = seam::authoring::AutomaticPerformanceCapture::prepare(session,
      fixture.regionId, {seam::time::Tick{0}, seam::time::Tick{1920}}, channels, resource, 5U,
      "proposal-capture-01");
  CHECK(prepared);
  if (!prepared) return;
  CHECK(prepared.value().matches(session));
  const auto generated = prepared.value().generate();
  CHECK(generated);
  if (!generated) return;
  CHECK(generated.value().resource == resource);
  CHECK(generated.value().range == prepared.value().range());
  CHECK(session.project().findRegion(fixture.regionId)->performance.takes.empty());

  CHECK(prepared.value().apply(generated.value(), session));
  const auto& state = session.project().findRegion(fixture.regionId)->performance;
  CHECK(state.takes.size() == 1U);
  CHECK(state.takes.front().id == "proposal-capture-01");
  CHECK(state.takes.front().state == seam::domain::PerformanceProposalState::Proposed);
  // Adopting a proposal is not accepting it: the proposal adds a take and leaves
  // every accepted selection untouched for the user to decide later.
  CHECK(state.accepted.empty());
  CHECK(session.undo());
  CHECK(session.project().findRegion(fixture.regionId)->performance.takes.empty());
  CHECK(session.redo());
  CHECK(session.project().findRegion(fixture.regionId)->performance.takes.size() == 1U);

  // A capture is consumed by the adoption, and a session that moved on cannot be
  // published into at all.
  CHECK(!prepared.value().apply(generated.value(), session));
  auto moved = seam::authoring::AutomaticPerformanceCapture::prepare(session,
      fixture.regionId, {seam::time::Tick{0}, seam::time::Tick{1920}}, channels, resource, 6U,
      "proposal-capture-02");
  CHECK(moved);
  if (!moved) return;
  auto changed = session.project();
  changed.findRegion(fixture.regionId)->notes.front().midiKey = 61U;
  CHECK(session.replaceProject(std::move(changed)));
  CHECK(!moved.value().matches(session));
  auto stale = moved.value().generate();
  CHECK(stale);
  if (!stale) return;
  CHECK(!moved.value().apply(stale.value(), session));
}

TEST_CASE("phrase proposal honours articulation, ranges and cancellation") {
  PhraseFixture fixture;
  auto& region = *fixture.project.findRegion(fixture.regionId);
  region.notes[1].articulation = seam::domain::NoteArticulation::Staccato;
  region.notes[2].articulation = seam::domain::NoteArticulation::Legato;
  const auto pronunciation = fixture.pronunciation();
  const auto proposal = seam::synthesis::generatePhraseAwarePerformance(
      fixture.project, region, pronunciation, fixture.request(pronunciation));
  CHECK(proposal);
  if (!proposal) return;
  const auto& attack = laneOf(proposal.value(), seam::domain::PerformanceChannel::Attack);
  CHECK(valueAt(attack, region.notes[1].startTick) == 15.0);
  CHECK(valueAt(attack, region.notes[2].startTick) == 55.0);

  // A partial range only shapes what it covers, and every point stays inside it.
  auto partialRequest = fixture.request(pronunciation);
  partialRequest.range = {seam::time::Tick{480}, seam::time::Tick{1440}};
  const auto partial = seam::synthesis::generatePhraseAwarePerformance(
      fixture.project, region, pronunciation, partialRequest);
  CHECK(partial);
  if (!partial) return;
  for (const auto& lane : partial.value().lanes) {
    CHECK(!lane.points.empty());
    for (const auto& point : lane.points) {
      CHECK(point.tick >= partialRequest.range.startTick);
      CHECK(point.tick <= partialRequest.range.endTick);
    }
  }

  auto staleRequest = fixture.request(pronunciation);
  ++staleRequest.capturedRevision.musical;
  CHECK(!seam::synthesis::generatePhraseAwarePerformance(fixture.project, region,
      pronunciation, staleRequest));
  auto unsupported = fixture.request(pronunciation);
  unsupported.channels.push_back(seam::domain::PerformanceChannel::Timing);
  CHECK(!seam::synthesis::generatePhraseAwarePerformance(fixture.project, region,
      pronunciation, unsupported));
  std::stop_source cancelled;
  cancelled.request_stop();
  CHECK(!seam::synthesis::generatePhraseAwarePerformance(fixture.project, region,
      pronunciation, fixture.request(pronunciation), cancelled.get_token()));
}

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
