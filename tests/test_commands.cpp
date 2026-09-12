#include "test_framework.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/application/view_commands.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"

#include <memory>
#include <limits>

namespace {

struct Fixture final {
  seam::application::ProjectFactory factory{10};
  seam::domain::Project project{factory.createProject("Command fixture")};
  seam::domain::TrackId trackId{factory.addVocalTrack(project, "Track")};
  seam::domain::RegionId regionId{
      factory.addRegion(project, trackId, "Region", seam::time::Tick{0}, seam::time::Tick{15360})};
};

}  // namespace

TEST_CASE("add note command is undoable and redoable") {
  Fixture fixture;
  seam::application::EditorSession session{std::move(fixture.project)};
  auto [lyric, note] = fixture.factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{960}, 60, U"a");
  const auto noteId = note.id;

  CHECK(session.execute(std::make_unique<seam::application::AddNoteCommand>(
      fixture.regionId, std::move(lyric), std::move(note))));
  CHECK(session.project().findNote(noteId) != nullptr);
  CHECK(session.revision() == 1);
  CHECK(session.undo());
  CHECK(session.project().findNote(noteId) == nullptr);
  CHECK(session.revision() == 2);
  CHECK(session.redo());
  CHECK(session.project().findNote(noteId) != nullptr);
  CHECK(session.revision() == 3);
}

TEST_CASE("adding notes refreshes pronunciation identity with exact undo and bounded revision") {
  Fixture fixture;
  auto* region = fixture.project.findRegion(fixture.regionId);
  const auto initial = seam::phonemizer::resolveJapanesePronunciation(*region);
  CHECK(initial);
  region->performance.pronunciation = initial.value().identity;
  region->performance.revision.pronunciation = 7U;
  const auto before = fixture.project;
  seam::application::EditorSession session{fixture.project};
  auto [lyric, note] = fixture.factory.makeNote(seam::time::Tick{0}, seam::time::Tick{960}, 60, U"こ");
  CHECK(session.execute(std::make_unique<seam::application::AddNoteCommand>(fixture.regionId, lyric, note)));
  region = session.project().findRegion(fixture.regionId);
  const auto resolved = seam::phonemizer::resolveJapanesePronunciation(*region);
  CHECK(resolved);
  CHECK(region->performance.pronunciation == resolved.value().identity);
  CHECK(region->performance.pronunciation != initial.value().identity);
  CHECK(region->performance.revision.pronunciation == 8U);
  const auto after = session.project();
  CHECK(session.undo());
  CHECK(session.project() == before);
  CHECK(session.redo());
  CHECK(session.project() == after);
  CHECK(session.undo());
  lyric.language = seam::domain::Language::English;
  CHECK(session.execute(std::make_unique<seam::application::AddNoteCommand>(fixture.regionId, lyric, note)));
  CHECK(!session.project().findRegion(fixture.regionId)->performance.pronunciation);
  CHECK(session.undo());
  CHECK(session.project() == before);
  session.project().findRegion(fixture.regionId)->performance.revision.pronunciation = std::numeric_limits<std::uint64_t>::max();
  const auto exhausted = session.project();
  CHECK(!session.execute(std::make_unique<seam::application::AddNoteCommand>(fixture.regionId, lyric, note)));
  CHECK(session.project() == exhausted);
}

TEST_CASE("note geometry maintains pronunciation identity only for changed resolver inputs") {
  for (unsigned scenario = 0U; scenario < 4U; ++scenario) {
    Fixture fixture;
    auto [lyric, note] = fixture.factory.makeNote(seam::time::Tick{960}, seam::time::Tick{480}, 60, U"こ");
    auto* region = fixture.project.findRegion(fixture.regionId);
    region->lyrics.push_back(lyric);
    region->notes.push_back(note);
    const auto initial = seam::phonemizer::resolveJapanesePronunciation(*region);
    CHECK(initial);
    region->performance.pronunciation = initial.value().identity;
    region->performance.revision.pronunciation = 9U;
    const auto before = fixture.project;
    seam::application::EditorSession session{fixture.project};
    auto command = [&]() -> std::unique_ptr<seam::application::ICommand> {
      if (scenario % 2U == 0U) {
        return std::make_unique<seam::application::MoveNotesCommand>(std::vector<seam::application::NoteMove>{{
            .noteId = note.id, .before = note.startTick,
            .after = seam::time::Tick{scenario == 0U ? 1200 : 960}, .beforeKey = 60U, .afterKey = 62U}});
      }
      return std::make_unique<seam::application::ResizeNotesCommand>(std::vector<seam::application::NoteResize>{{
          .noteId = note.id, .beforeStart = note.startTick, .beforeDuration = note.durationTick,
          .afterStart = seam::time::Tick{scenario == 1U ? 720 : 960}, .afterDuration = seam::time::Tick{720}}});
    };
    CHECK(session.execute(command()));
    region = session.project().findRegion(fixture.regionId);
    const auto resolved = seam::phonemizer::resolveJapanesePronunciation(*region);
    CHECK(resolved);
    CHECK(region->performance.pronunciation == resolved.value().identity);
    CHECK(region->performance.revision.pronunciation == (scenario != 2U ? 10U : 9U));
    const auto after = session.project();
    CHECK(session.undo());
    CHECK(session.project() == before);
    CHECK(session.redo());
    CHECK(session.project() == after);
    CHECK(session.undo());
    session.project().findRegion(fixture.regionId)->performance.revision.pronunciation = std::numeric_limits<std::uint64_t>::max();
    const auto exhausted = session.project();
    if (scenario != 2U) {
      CHECK(!session.execute(command()));
      CHECK(session.project() == exhausted);
    } else {
      CHECK(session.execute(command()));
      CHECK(session.project().findRegion(fixture.regionId)->performance.revision.pronunciation == std::numeric_limits<std::uint64_t>::max());
    }
  }
}

TEST_CASE("note reordering retains broken unit spans and seam joins unresolved with exact undo") {
  for (bool reorder : {false, true}) {
    Fixture fixture;
    auto* region = fixture.project.findRegion(fixture.regionId);
    for (unsigned i = 0U; i < 3U; ++i) {
      auto [lyric, note] = fixture.factory.makeNote(seam::time::Tick{static_cast<std::int64_t>(i) * 960},
          seam::time::Tick{480}, 60U, i == 0U ? U"あ" : i == 1U ? U"い" : U"う");
      region->lyrics.push_back(lyric);
      region->notes.push_back(note);
    }
    const auto first = region->notes[0].id;
    const auto middle = region->notes[1].id;
    const auto last = region->notes[2].id;
    region->unitSelectionOverrides = {{.startKey = {first, 0U}, .tokenCount = 2U, .unitId = "cross-note"},
        {.startKey = {last, 0U}, .unitId = "unchanged"}};
    region->seamOverrides = {{.incomingStartKey = {middle, 0U}, .seamAmount = 0.6F},
        {.incomingStartKey = {first, 0U}, .seamAmount = 0.2F}};
    seam::application::EditorSession session{fixture.project};
    CHECK(session.execute(std::make_unique<seam::application::UpsertPhonemeOverrideCommand>(fixture.regionId,
        seam::domain::PhonemeOverride{.key = {middle, 0U}, .timing = {.startOffset = -1200}, .locked = true})));
    const auto before = session.project();
    CHECK(session.execute(std::make_unique<seam::application::MoveNotesCommand>(std::vector<seam::application::NoteMove>{{
        .noteId = middle, .before = seam::time::Tick{960}, .after = seam::time::Tick{reorder ? 2880 : 1200},
        .beforeKey = 60U, .afterKey = 60U}})));
    region = session.project().findRegion(fixture.regionId);
    CHECK(region->unitSelectionOverrides[0].unresolved == reorder);
    CHECK(region->unitSelectionOverrides[0].unitId == "cross-note");
    CHECK(!region->unitSelectionOverrides[1].unresolved);
    CHECK(region->seamOverrides[0].unresolved == reorder);
    CHECK(region->seamOverrides[0].seamAmount == 0.6F);
    CHECK(!region->seamOverrides[1].unresolved);
    CHECK(!region->phonemeOverrides[0].unresolved);
    const auto resolved = seam::phonemizer::resolveJapanesePronunciation(*region);
    CHECK(resolved);
    CHECK(region->performance.pronunciation == resolved.value().identity);
    const auto after = session.project();
    CHECK(session.undo());
    CHECK(session.project() == before);
    CHECK(session.redo());
    CHECK(session.project() == after);
  }
}

TEST_CASE("note insertion reconciles existing spans joins and continuation locks") {
  for (unsigned scenario = 0U; scenario < 3U; ++scenario) {
    Fixture fixture;
    auto* region = fixture.project.findRegion(fixture.regionId);
    auto [lyricA, noteA] = fixture.factory.makeNote(seam::time::Tick{0}, seam::time::Tick{480}, 60U, U"あ");
    auto [lyricB, noteB] = fixture.factory.makeNote(seam::time::Tick{1920}, seam::time::Tick{480}, 60U, scenario == 2U ? U"ー" : U"う");
    region->lyrics = {lyricA, lyricB};
    region->notes = {noteA, noteB};
    region->unitSelectionOverrides = {{.startKey = {noteA.id, 0U}, .tokenCount = 2U, .unitId = "span"}};
    region->seamOverrides = {{.incomingStartKey = {noteB.id, 0U}, .seamAmount = 0.4F},
        {.incomingStartKey = {noteA.id, 0U}, .seamAmount = 0.2F}};
    seam::application::EditorSession session{fixture.project};
    CHECK(session.execute(std::make_unique<seam::application::UpsertPhonemeOverrideCommand>(fixture.regionId,
        seam::domain::PhonemeOverride{.key = {noteB.id, 0U}, .timing = {.startOffset = -1300}, .locked = true})));
    const auto before = session.project();
    auto [lyric, note] = fixture.factory.makeNote(seam::time::Tick{scenario == 0U ? 2880 : 960},
        seam::time::Tick{480}, 60U, U"い");
    CHECK(session.execute(std::make_unique<seam::application::AddNoteCommand>(fixture.regionId, lyric, note)));
    region = session.project().findRegion(fixture.regionId);
    CHECK(region->unitSelectionOverrides[0].unresolved == (scenario != 0U));
    CHECK(region->unitSelectionOverrides[0].unitId == "span");
    CHECK(region->seamOverrides[0].unresolved == (scenario != 0U));
    CHECK(!region->seamOverrides[1].unresolved);
    CHECK(region->phonemeOverrides[0].unresolved == (scenario == 2U));
    CHECK(region->phonemeOverrides[0].timing.startOffset == -1300);
    const auto resolved = seam::phonemizer::resolveJapanesePronunciation(*region);
    CHECK(resolved);
    CHECK(region->performance.pronunciation == resolved.value().identity);
    const auto after = session.project();
    CHECK(session.undo());
    CHECK(session.project() == before);
    CHECK(session.redo());
    CHECK(session.project() == after);
  }
}

TEST_CASE("note deletion reconciles surviving spans joins and continuation locks") {
  for (bool continuation : {false, true}) {
    Fixture fixture;
    auto* region = fixture.project.findRegion(fixture.regionId);
    for (unsigned i = 0U; i < 3U; ++i) {
      auto [lyric, note] = fixture.factory.makeNote(seam::time::Tick{static_cast<std::int64_t>(i) * 960},
          seam::time::Tick{480}, 60U, i == 0U ? U"あ" : i == 1U ? U"い" : continuation ? U"ー" : U"う");
      region->lyrics.push_back(lyric);
      region->notes.push_back(note);
    }
    const auto first = region->notes[0].id;
    const auto middle = region->notes[1].id;
    const auto last = region->notes[2].id;
    region->unitSelectionOverrides = {{.startKey = {first, 0U}, .tokenCount = 2U, .unitId = "broken"},
        {.startKey = {last, 0U}, .unitId = "surviving"}};
    region->seamOverrides = {{.incomingStartKey = {last, 0U}, .seamAmount = 0.7F},
        {.incomingStartKey = {first, 0U}, .seamAmount = 0.2F}};
    seam::application::EditorSession session{fixture.project};
    CHECK(session.execute(std::make_unique<seam::application::UpsertPhonemeOverrideCommand>(fixture.regionId,
        seam::domain::PhonemeOverride{.key = {last, 0U}, .timing = {.startOffset = -1400}, .locked = true})));
    const auto before = session.project();
    CHECK(session.execute(std::make_unique<seam::application::RemoveNotesCommand>(std::vector{middle})));
    region = session.project().findRegion(fixture.regionId);
    CHECK(region->notes.size() == 2U);
    CHECK(region->unitSelectionOverrides[0].unresolved);
    CHECK(region->unitSelectionOverrides[0].unitId == "broken");
    CHECK(region->unitSelectionOverrides[1].unresolved == continuation);
    CHECK(region->seamOverrides[0].unresolved);
    CHECK(!region->seamOverrides[1].unresolved);
    CHECK(region->phonemeOverrides[0].unresolved == continuation);
    CHECK(region->phonemeOverrides[0].timing.startOffset == -1400);
    const auto resolved = seam::phonemizer::resolveJapanesePronunciation(*region);
    CHECK(resolved);
    CHECK(region->performance.pronunciation == resolved.value().identity);
    const auto after = session.project();
    CHECK(session.undo());
    CHECK(session.project() == before);
    CHECK(session.redo());
    CHECK(session.project() == after);
    CHECK(session.undo());
    session.project().findRegion(fixture.regionId)->performance.revision.pronunciation = std::numeric_limits<std::uint64_t>::max();
    const auto exhausted = session.project();
    CHECK(!session.execute(std::make_unique<seam::application::RemoveNotesCommand>(std::vector{middle})));
    CHECK(session.project() == exhausted);
  }
}

TEST_CASE("note lyric reassignment reconciles bindings while articulation-only edits preserve pronunciation") {
  for (unsigned scenario = 0U; scenario < 3U; ++scenario) {
    Fixture fixture;
    auto [lyric, note] = fixture.factory.makeNote(seam::time::Tick{0}, seam::time::Tick{480}, 60U, U"あ");
    auto [other, unused] = fixture.factory.makeNote(seam::time::Tick{960}, seam::time::Tick{480}, 60U, U"い");
    static_cast<void>(unused);
    if (scenario == 2U) other.language = seam::domain::Language::English;
    auto* region = fixture.project.findRegion(fixture.regionId);
    region->lyrics = {lyric, other};
    region->notes = {note};
    region->unitSelectionOverrides = {{.startKey = {note.id, 0U}, .unitId = "saved"}};
    seam::application::EditorSession session{fixture.project};
    CHECK(session.execute(std::make_unique<seam::application::UpsertPhonemeOverrideCommand>(fixture.regionId,
        seam::domain::PhonemeOverride{.key = {note.id, 0U}, .timing = {.startOffset = -1500}, .locked = true})));
    const auto before = session.project();
    const auto revision = before.findRegion(fixture.regionId)->performance.revision.pronunciation;
    auto command = [&] {
      return std::make_unique<seam::application::SetNotePerformanceCommand>(std::vector<seam::application::NotePerformanceEdit>{{
          .noteId = note.id, .beforeArticulation = note.articulation, .afterArticulation = note.articulation,
          .beforeSlurGroup = {}, .afterSlurGroup = 7U,
          .beforeLyricTokenId = lyric.id, .afterLyricTokenId = scenario == 0U ? lyric.id : other.id}});
    };
    CHECK(session.execute(command()));
    region = session.project().findRegion(fixture.regionId);
    CHECK(region->phonemeOverrides[0].unresolved == (scenario != 0U));
    CHECK(region->unitSelectionOverrides[0].unresolved == (scenario != 0U));
    CHECK(region->performance.revision.pronunciation == revision + (scenario != 0U ? 1U : 0U));
    if (scenario == 2U) CHECK(!region->performance.pronunciation);
    else {
      const auto resolved = seam::phonemizer::resolveJapanesePronunciation(*region);
      CHECK(resolved);
      CHECK(region->performance.pronunciation == resolved.value().identity);
    }
    const auto after = session.project();
    CHECK(session.undo());
    CHECK(session.project() == before);
    CHECK(session.redo());
    CHECK(session.project() == after);
    CHECK(session.undo());
    session.project().findRegion(fixture.regionId)->performance.revision.pronunciation = std::numeric_limits<std::uint64_t>::max();
    const auto exhausted = session.project();
    if (scenario != 0U) {
      CHECK(!session.execute(command()));
      CHECK(session.project() == exhausted);
    } else CHECK(session.execute(command()));
  }
}

TEST_CASE("technical lane presentation is undoable without audio impact") {
  Fixture fixture;
  seam::application::EditorSession session{std::move(fixture.project)};
  const auto before = session.project().settings().technicalLanes[3U];
  CHECK(session.execute(std::make_unique<
                        seam::application::SetTechnicalLanePresentationCommand>(
      seam::domain::TechnicalLane::Pitch,
      seam::domain::TechnicalLanePresentation{
          .mode = seam::domain::TechnicalLaneMode::Expanded,
          .expandedHeight = 144.0})));
  CHECK(session.lastImpact().scope == seam::application::CommandAudioImpact::ViewOnly);
  CHECK(session.project().settings().technicalLanes[3U].expandedHeight == 144.0);
  CHECK(session.undo());
  CHECK(session.project().settings().technicalLanes[3U] == before);
}

TEST_CASE("moving notes changes time and pitch as one transaction") {
  Fixture fixture;
  auto [lyric, note] = fixture.factory.makeNote(
      seam::time::Tick{960}, seam::time::Tick{480}, 60, U"i");
  const auto noteId = note.id;
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->lyrics.push_back(lyric);
  region->notes.push_back(note);

  seam::application::EditorSession session{std::move(fixture.project)};
  std::vector<seam::application::NoteMove> moves{{
      .noteId = noteId,
      .before = seam::time::Tick{960},
      .after = seam::time::Tick{1440},
      .beforeKey = 60,
      .afterKey = 63,
  }};
  CHECK(session.execute(std::make_unique<seam::application::MoveNotesCommand>(moves)));
  CHECK(session.project().findNote(noteId)->startTick == seam::time::Tick{1440});
  CHECK(session.project().findNote(noteId)->midiKey == 63);
  CHECK(session.undo());
  CHECK(session.project().findNote(noteId)->startTick == seam::time::Tick{960});
  CHECK(session.project().findNote(noteId)->midiKey == 60);
}

TEST_CASE("resizing notes rejects non-positive duration") {
  Fixture fixture;
  auto [lyric, note] = fixture.factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{480}, 60, U"u");
  const auto noteId = note.id;
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->lyrics.push_back(lyric);
  region->notes.push_back(note);
  seam::application::EditorSession session{std::move(fixture.project)};

  std::vector<seam::application::NoteResize> invalid{{
      .noteId = noteId,
      .beforeStart = seam::time::Tick{0},
      .beforeDuration = seam::time::Tick{480},
      .afterStart = seam::time::Tick{0},
      .afterDuration = seam::time::Tick{0},
  }};
  CHECK(!session.execute(std::make_unique<seam::application::ResizeNotesCommand>(invalid)));
  CHECK(session.project().findNote(noteId)->durationTick == seam::time::Tick{480});
}

TEST_CASE("deleting notes removes unreferenced lyrics and is reversible") {
  Fixture fixture;
  auto [lyricA, noteA] = fixture.factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{480}, 60, U"a");
  auto [lyricB, noteB] = fixture.factory.makeNote(
      seam::time::Tick{480}, seam::time::Tick{480}, 62, U"i");
  const auto noteAId = noteA.id;
  const auto noteBId = noteB.id;
  const auto lyricAId = lyricA.id;
  const auto lyricBId = lyricB.id;
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->lyrics.push_back(lyricA);
  region->lyrics.push_back(lyricB);
  region->notes.push_back(noteA);
  region->notes.push_back(noteB);

  const auto original = fixture.project;
  seam::application::EditorSession session{std::move(fixture.project)};
  CHECK(session.execute(std::make_unique<seam::application::RemoveNotesCommand>(
      std::vector<seam::domain::NoteId>{noteAId, noteBId})));
  CHECK(session.project().findNote(noteAId) == nullptr);
  CHECK(session.project().findNote(noteBId) == nullptr);
  CHECK(session.project().findRegion(fixture.regionId)->findLyric(lyricAId) == nullptr);
  CHECK(session.project().findRegion(fixture.regionId)->findLyric(lyricBId) == nullptr);
  CHECK(session.undo());
  CHECK(session.project() == original);
  CHECK(session.redo());
  CHECK(session.project().findNote(noteAId) == nullptr);
}

TEST_CASE("project factory reserves IDs after loading an existing project") {
  Fixture fixture;
  auto [lyric, note] = fixture.factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{480}, 60, U"a");
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->lyrics.push_back(lyric);
  region->notes.push_back(note);

  seam::application::ProjectFactory loadedFactory{1};
  loadedFactory.synchronizeWith(fixture.project);
  const auto previousMaximum = note.id.value();
  auto [newLyric, newNote] = loadedFactory.makeNote(
      seam::time::Tick{480}, seam::time::Tick{480}, 62, U"i");
  CHECK(newLyric.id.value() > previousMaximum);
  CHECK(newNote.id.value() > newLyric.id.value());
}

TEST_CASE("lyric and phoneme overrides are reversible editor commands") {
  Fixture fixture;
  auto [lyric, note] = fixture.factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{960}, 60, U"き");
  const auto lyricId = lyric.id;
  const auto noteId = note.id;
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->lyrics.push_back(lyric);
  region->notes.push_back(note);

  seam::application::EditorSession session{std::move(fixture.project)};
  CHECK(session.execute(std::make_unique<seam::application::SetLyricCommand>(
      lyricId, U"ぎ", seam::domain::Language::Japanese)));
  CHECK(session.project().findRegion(fixture.regionId)->findLyric(lyricId)->surface == U"ぎ");

  seam::domain::PhonemeOverride overrideValue{
      .key = seam::domain::PhonemeKey{noteId, 0},
      .symbol = std::string{"g"},
      .timing = seam::domain::PhonemeTiming{
          .startOffset = seam::time::Microseconds{-45000},
          .endOffset = seam::time::Microseconds{0},
      },
      .locked = true,
  };
  CHECK(session.execute(
      std::make_unique<seam::application::UpsertPhonemeOverrideCommand>(
          fixture.regionId, overrideValue)));
  CHECK(session.project().findRegion(fixture.regionId)
            ->findPhonemeOverride(overrideValue.key) != nullptr);
  CHECK(session.undo());
  CHECK(session.project().findRegion(fixture.regionId)
            ->findPhonemeOverride(overrideValue.key) == nullptr);
  CHECK(session.undo());
  CHECK(session.project().findRegion(fixture.regionId)->findLyric(lyricId)->surface == U"き");
  CHECK(session.redo());
  CHECK(session.redo());
  CHECK(session.project().findRegion(fixture.regionId)
            ->findPhonemeOverride(overrideValue.key)->locked);
}

TEST_CASE("deleting a note removes and restores its phoneme overrides") {
  Fixture fixture;
  auto [lyric, note] = fixture.factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{960}, 60, U"き");
  const auto noteId = note.id;
  const seam::domain::PhonemeKey key{noteId, 0};
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->lyrics.push_back(lyric);
  region->notes.push_back(note);
  region->phonemeOverrides.push_back(seam::domain::PhonemeOverride{
      .key = key,
      .symbol = std::string{"g"},
      .timing = {},
      .locked = true,
  });

  seam::application::EditorSession session{std::move(fixture.project)};
  CHECK(session.execute(std::make_unique<seam::application::RemoveNotesCommand>(
      std::vector<seam::domain::NoteId>{noteId})));
  CHECK(session.project().findRegion(fixture.regionId)->findPhonemeOverride(key) == nullptr);
  CHECK(session.undo());
  CHECK(session.project().findRegion(fixture.regionId)->findPhonemeOverride(key) != nullptr);
  CHECK(session.redo());
  CHECK(session.project().findRegion(fixture.regionId)->findPhonemeOverride(key) == nullptr);
}


TEST_CASE("unit seam and pitch controls are persistent undoable commands") {
  Fixture fixture;
  auto [lyric, note] = fixture.factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{960}, 60, U"き");
  const auto noteId = note.id;
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->lyrics.push_back(lyric);
  region->notes.push_back(note);
  seam::application::EditorSession session{std::move(fixture.project)};

  const seam::domain::PhonemeKey key{noteId, 0};
  CHECK(session.execute(std::make_unique<
      seam::application::UpsertUnitSelectionOverrideCommand>(
      fixture.regionId, seam::domain::UnitSelectionOverride{
          .startKey = key,
          .tokenCount = 2,
          .unitId = "ja.original.c4.k-i.02",
          .renderer = seam::domain::UnitRendererKind::ClassicPsola,
          .locked = true,
      })));
  CHECK(session.execute(std::make_unique<
      seam::application::UpsertSeamOverrideCommand>(
      fixture.regionId, seam::domain::SeamOverride{
          .incomingStartKey = key,
          .seamAmount = 0.9F,
          .overlap = seam::time::Microseconds{7000},
          .phaseReset = 0.8F,
          .envelopeBlend = 0.1F,
          .curve = seam::domain::SeamCurve::HardCharacter,
          .locked = true,
      })));
  CHECK(session.execute(std::make_unique<
      seam::application::UpsertPitchAutomationPointCommand>(
      fixture.regionId, seam::domain::PitchAutomationPoint{
          .tick = seam::time::Tick{240},
          .cents = -18.0F,
          .interpolation = seam::domain::CurveInterpolation::Smooth,
      })));

  const auto* edited = session.project().findRegion(fixture.regionId);
  CHECK(edited->unitSelectionOverrides.size() == 1);
  CHECK(edited->seamOverrides.size() == 1);
  CHECK(edited->pitchAutomation.points().size() == 1);
  CHECK(session.undo());
  CHECK(session.project().findRegion(fixture.regionId)->pitchAutomation.points().empty());
  CHECK(session.undo());
  CHECK(session.project().findRegion(fixture.regionId)->seamOverrides.empty());
  CHECK(session.undo());
  CHECK(session.project().findRegion(fixture.regionId)->unitSelectionOverrides.empty());
  CHECK(session.redo());
  CHECK(session.redo());
  CHECK(session.redo());
  CHECK(session.project().findRegion(fixture.regionId)->validate());
}

TEST_CASE("deleting a note removes and restores render overrides") {
  Fixture fixture;
  auto [lyric, note] = fixture.factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{960}, 60, U"き");
  const auto noteId = note.id;
  const seam::domain::PhonemeKey key{noteId, 0};
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->lyrics.push_back(lyric);
  region->notes.push_back(note);
  region->unitSelectionOverrides.push_back(seam::domain::UnitSelectionOverride{
      .startKey = key,
      .tokenCount = 2,
      .unitId = "unit.02",
      .renderer = seam::domain::UnitRendererKind::Raw,
      .locked = true,
  });
  region->seamOverrides.push_back(seam::domain::SeamOverride{
      .incomingStartKey = key,
      .seamAmount = 0.7F,
      .overlap = std::nullopt,
      .phaseReset = std::nullopt,
      .envelopeBlend = std::nullopt,
      .curve = seam::domain::SeamCurve::Linear,
      .locked = true,
  });
  seam::application::EditorSession session{std::move(fixture.project)};
  CHECK(session.execute(std::make_unique<seam::application::RemoveNotesCommand>(
      std::vector<seam::domain::NoteId>{noteId})));
  CHECK(session.project().findRegion(fixture.regionId)->unitSelectionOverrides.empty());
  CHECK(session.project().findRegion(fixture.regionId)->seamOverrides.empty());
  CHECK(session.undo());
  CHECK(session.project().findRegion(fixture.regionId)->unitSelectionOverrides.size() == 1);
  CHECK(session.project().findRegion(fixture.regionId)->seamOverrides.size() == 1);
}
