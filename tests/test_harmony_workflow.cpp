#include "test_framework.hpp"

#include "seam/application/harmony_commands.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/formats/project_json.hpp"

#include <algorithm>
#include <memory>
#include <limits>

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

TEST_CASE("diatonic harmony creates an independent editable track and survives undo and reload") {
  Fixture fixture;
  auto* lead=fixture.project.findVocalTrack(fixture.track);
  lead->voicebank={"singer","1",std::string(64U,'a')};
  const auto before=fixture.project;
  const auto request=seam::application::HarmonyRequest{
      .regionId=fixture.region,.intervalSemitones=0,
      .diatonic=seam::application::DiatonicHarmony{
          .tonicPitchClass=0U,.scaleIntervals={0U,2U,4U,5U,7U,9U,11U},.degreeOffset=2}};
  const auto draft=seam::application::prepareHarmonyTrack(fixture.project,fixture.factory,request);
  CHECK(draft);
  CHECK(fixture.project==before);
  CHECK(draft.value().sourceTrackId==fixture.track);
  CHECK(draft.value().harmonyTrack.id!=fixture.track);
  CHECK(draft.value().harmonyTrack.voicebank==lead->voicebank);
  CHECK(draft.value().harmonyTrack.regions.size()==1U);
  const auto& harmony=draft.value().harmonyTrack.regions.front();
  CHECK(harmony.id!=fixture.region);
  CHECK(harmony.startTick==lead->regions.front().startTick);
  CHECK(harmony.notes.size()==2U);
  CHECK(harmony.notes[0].midiKey==64U);
  CHECK(harmony.notes[1].midiKey==67U);
  CHECK(harmony.pitchAutomation.points().empty());
  seam::application::EditorSession session{fixture.project};
  CHECK(session.execute(std::make_unique<seam::application::AddHarmonyTrackCommand>(draft.value())));
  CHECK(session.project().vocalTracks().size()==2U);
  CHECK(*session.project().findVocalTrack(fixture.track)==draft.value().expectedSourceTrack);
  const auto accepted=session.project();
  const auto encoded=seam::formats::ProjectJsonCodec{}.encode(accepted); CHECK(encoded);
  const auto reloaded=seam::formats::ProjectJsonCodec{}.decode(encoded.value()); CHECK(reloaded);
  CHECK(reloaded.value()==accepted);
  CHECK(session.undo()); CHECK(session.project()==before);
  CHECK(session.redo()); CHECK(session.project()==accepted);
  auto another=seam::application::prepareHarmonyTrack(session.project(),fixture.factory,request);
  CHECK(another);
  CHECK(another.value().harmonyTrack.name=="Lead Harmony 2");
}

TEST_CASE("diatonic harmony handles a non-C tonic and descending octave boundary") {
  Fixture fixture;
  const auto draft=seam::application::prepareHarmonyTrack(fixture.project,fixture.factory,
      seam::application::HarmonyRequest{.regionId=fixture.region,
          .diatonic=seam::application::DiatonicHarmony{
              .tonicPitchClass=5U,.scaleIntervals={0U,2U,4U,5U,7U,9U,11U},
              .degreeOffset=-2}});
  CHECK(draft);
  if (!draft) return;
  const auto& notes=draft.value().harmonyTrack.regions.front().notes;
  CHECK(notes.size()==2U);
  CHECK(notes[0].midiKey==57U); // C4 down two F-major degrees -> A3
  CHECK(notes[1].midiKey==60U); // E4 -> C4
}

TEST_CASE("harmony keeps shared-lyric melisma and phonetic intent without sharing lead IDs") {
  Fixture fixture;
  auto* source=fixture.project.findRegion(fixture.region);
  const auto leadLyric=source->notes.front().lyricTokenId;
  source->notes.back().lyricTokenId=leadLyric;
  source->lyrics.pop_back();
  source->notes.front().slurGroup=42U;
  source->notes.back().slurGroup=42U;
  source->notes.front().phoneticHint="l a";
  CHECK(fixture.project.validate());
  auto draft=seam::application::prepareHarmonyTrack(fixture.project,fixture.factory,
      seam::application::HarmonyRequest{.regionId=fixture.region,.intervalSemitones=7});
  CHECK(draft);
  const auto& region=draft.value().harmonyTrack.regions.front();
  CHECK(region.lyrics.size()==1U);
  CHECK(region.notes.size()==2U);
  CHECK(region.notes.front().lyricTokenId==region.notes.back().lyricTokenId);
  CHECK(region.notes.front().lyricTokenId!=leadLyric);
  CHECK(region.notes.front().slurGroup==42U);
  CHECK(region.notes.back().slurGroup==42U);
  CHECK(region.notes.front().phoneticHint==source->notes.front().phoneticHint);
  seam::application::EditorSession session{fixture.project};
  CHECK(session.execute(std::make_unique<seam::application::AddHarmonyTrackCommand>(draft.value())));
  CHECK(session.project().validate());
  const auto inRegion=seam::application::prepareHarmony(fixture.project,fixture.factory,
      seam::application::HarmonyRequest{.regionId=fixture.region,.intervalSemitones=-5});
  CHECK(inRegion);
  CHECK(inRegion.value().lyrics.size()==1U);
  CHECK(session.execute(std::make_unique<seam::application::AddHarmonyCommand>(
      fixture.region,inRegion.value().expectedNotes,inRegion.value().lyrics,inRegion.value().notes)));
  CHECK(session.project().validate());
}

TEST_CASE("diatonic harmony rejects invalid scale and stale source without publication") {
  Fixture fixture;
  using seam::application::DiatonicHarmony;
  using seam::application::HarmonyRequest;
  auto invalid=HarmonyRequest{.regionId=fixture.region,
      .diatonic=DiatonicHarmony{.scaleIntervals={0U,4U,2U}}};
  CHECK(!seam::application::prepareHarmonyTrack(fixture.project,fixture.factory,invalid));
  invalid.diatonic=DiatonicHarmony{.degreeOffset=0};
  CHECK(!seam::application::prepareHarmonyTrack(fixture.project,fixture.factory,invalid));
  invalid.diatonic=DiatonicHarmony{.degreeOffset=48};
  CHECK(!seam::application::prepareHarmonyTrack(fixture.project,fixture.factory,invalid));
  auto outside=fixture.project;
  outside.findRegion(fixture.region)->notes.front().midiKey=61U;
  invalid.diatonic=DiatonicHarmony{};
  CHECK(!seam::application::prepareHarmonyTrack(outside,fixture.factory,invalid));
  auto prepared=seam::application::prepareHarmonyTrack(fixture.project,fixture.factory,
      HarmonyRequest{.regionId=fixture.region,.intervalSemitones=-5});
  CHECK(prepared);
  fixture.project.findRegion(fixture.region)->notes.front().midiKey=61U;
  seam::application::EditorSession session{fixture.project};
  const auto before=session.project();
  CHECK(!session.execute(std::make_unique<seam::application::AddHarmonyTrackCommand>(prepared.value())));
  CHECK(session.project()==before);
}

TEST_CASE("harmony refuses an exhausted project identity space before allocating") {
  Fixture fixture;
  fixture.project.findRegion(fixture.region)->notes.front().id =
      seam::domain::NoteId{std::numeric_limits<std::uint64_t>::max()};
  CHECK(fixture.project.validate());
  const auto original = fixture.project;
  const auto prepared = seam::application::prepareHarmonyTrack(
      fixture.project, fixture.factory,
      seam::application::HarmonyRequest{.regionId=fixture.region,
          .intervalSemitones=7});
  CHECK(!prepared);
  CHECK(fixture.project == original);
}
