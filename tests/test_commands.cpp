#include "test_framework.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/application/project_factory.hpp"

#include <memory>

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
