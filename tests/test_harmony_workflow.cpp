#include "test_framework.hpp"

#include "seam/application/harmony_commands.hpp"
#include "seam/application/editor_session.hpp"

#include <algorithm>
#include <memory>

namespace {

struct Fixture final {
  seam::application::ProjectFactory factory{960000U};
  seam::domain::Project project{factory.createProject("Harmony")};
  seam::domain::TrackId track{};
  seam::domain::RegionId region{};

  Fixture() {
    track = factory.addVocalTrack(project, "Lead");
    region = factory.addRegion(project, track, "Verse", seam::time::Tick{0}, seam::time::Tick{1920});
    auto first = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{960}, 60U, U"la", seam::domain::Language::English);
    auto second = factory.makeNote(seam::time::Tick{960}, seam::time::Tick{960}, 64U, U"na", seam::domain::Language::English);
    auto* target = project.findRegion(region);
    target->lyrics.push_back(std::move(first.first)); target->notes.push_back(std::move(first.second));
    target->lyrics.push_back(std::move(second.first)); target->notes.push_back(std::move(second.second));
  }
};

}  // namespace

TEST_CASE("harmony preparation is side-effect free and copies lyric language") {
  Fixture fixture;
  const auto before = fixture.project;
  auto draft = seam::application::prepareHarmony(
      fixture.project, fixture.factory,
      seam::application::HarmonyRequest{.regionId = fixture.region,
                                        .sourceNotes = {},
                                        .intervalSemitones = 7,
                                        .minimumMidi = 0U,
                                        .maximumMidi = 127U});
  CHECK(draft);
  CHECK(fixture.project == before);
  CHECK(draft.value().notes.size() == 2U);
  CHECK(draft.value().lyrics.size() == 2U);
  CHECK(draft.value().notes[0].midiKey == 67U);
  CHECK(draft.value().notes[1].midiKey == 71U);
  CHECK(draft.value().lyrics[0].surface == U"la");
  CHECK(draft.value().lyrics[0].language == seam::domain::Language::English);
  CHECK(draft.value().lyrics[0].id != fixture.project.findRegion(fixture.region)->lyrics[0].id);
}

TEST_CASE("accepted harmony is one undoable atomic edit and stale source is rejected") {
  Fixture fixture;
  auto draft = seam::application::prepareHarmony(
      fixture.project, fixture.factory,
      seam::application::HarmonyRequest{.regionId = fixture.region,
                                        .sourceNotes = {},
                                        .intervalSemitones = -5});
  CHECK(draft);
  seam::application::EditorSession session{fixture.project};
  auto command = std::make_unique<seam::application::AddHarmonyCommand>(
      fixture.region, draft.value().expectedNotes, draft.value().lyrics,
      draft.value().notes);
  CHECK(session.execute(std::move(command)));
  CHECK(session.project().findRegion(fixture.region)->notes.size() == 4U);
  CHECK(session.canUndo());
  const auto after = session.project();
  CHECK(session.undo());
  CHECK(session.project().findRegion(fixture.region)->notes.size() == 2U);
  CHECK(session.redo());
  CHECK(session.project() == after);

  Fixture stale;
  auto staleDraft = seam::application::prepareHarmony(
      stale.project, stale.factory,
      seam::application::HarmonyRequest{.regionId = stale.region,
                                        .sourceNotes = {stale.project.findRegion(stale.region)->notes.front().id},
                                        .intervalSemitones = 3});
  CHECK(staleDraft);
  stale.project.findRegion(stale.region)->notes.front().midiKey = 61U;
  seam::application::EditorSession staleSession{stale.project};
  auto staleCommand = std::make_unique<seam::application::AddHarmonyCommand>(
      stale.region, staleDraft.value().expectedNotes, staleDraft.value().lyrics,
      staleDraft.value().notes);
  CHECK(!staleSession.execute(std::move(staleCommand)));
}

TEST_CASE("harmony preparation rejects unison, missing notes and out-of-range targets") {
  Fixture fixture;
  CHECK(!seam::application::prepareHarmony(
      fixture.project, fixture.factory,
      seam::application::HarmonyRequest{.regionId = fixture.region,
                                        .intervalSemitones = 0}));
  CHECK(!seam::application::prepareHarmony(
      fixture.project, fixture.factory,
      seam::application::HarmonyRequest{.regionId = fixture.region,
                                        .sourceNotes = {seam::domain::NoteId{999999999U}},
                                        .intervalSemitones = 7}));
  CHECK(!seam::application::prepareHarmony(
      fixture.project, fixture.factory,
      seam::application::HarmonyRequest{.regionId = fixture.region,
                                        .intervalSemitones = 70,
                                        .minimumMidi = 0U,
                                        .maximumMidi = 127U}));
  CHECK(!seam::application::prepareHarmony(
      fixture.project, fixture.factory,
      seam::application::HarmonyRequest{.regionId = fixture.region,
                                        .intervalSemitones = 60,
                                        .minimumMidi = 0U,
                                        .maximumMidi = 127U}));
}
