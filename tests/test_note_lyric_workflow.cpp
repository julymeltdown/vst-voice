#include "test_framework.hpp"

#include "seam/application/note_commands.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/ui/piano_roll_model.hpp"

TEST_CASE("piano roll duplicate and lyric distribution remain undoable") {
  seam::application::ProjectFactory factory{12000U};
  auto project = factory.createProject("Note workflow");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, trackId, "Verse",
                                           seam::time::Tick{0},
                                           seam::time::Tick{9600});
  seam::application::EditorSession session{std::move(project)};
  auto add = [&](seam::time::Tick start, std::u32string lyric) {
    auto [token, note] = factory.makeNote(
        start, seam::time::Tick{480}, 60U, std::move(lyric),
        seam::domain::Language::English);
    const auto id = note.id;
    CHECK(session.execute(std::make_unique<seam::application::AddNoteCommand>(
        regionId, std::move(token), std::move(note))));
    return id;
  };
  const auto first = add(seam::time::Tick{0}, U"la");
  const auto second = add(seam::time::Tick{960}, U"li");

  seam::ui::PianoRollModel model{session, factory, regionId};
  session.selection().replace({first, second});
  const auto duplicated = model.duplicateSelection();
  CHECK(duplicated);
  CHECK(session.project().noteCount() == 4U);
  CHECK(duplicated.value() != first);
  CHECK(session.undo());
  CHECK(session.project().noteCount() == 2U);
  CHECK(session.redo());
  CHECK(session.project().noteCount() == 4U);

  session.selection().replace({first, second});
  const auto underflow = model.distributeSelectedLyrics(U"one");
  CHECK(underflow);
  CHECK(!underflow.value().committed);
  CHECK(underflow.value().missingSyllables == 1U);

  const auto committed = model.distributeSelectedLyrics(
      U"one two", seam::domain::Language::Japanese);
  CHECK(committed);
  CHECK(committed.value().committed);
  CHECK(committed.value().appliedSyllables == 2U);
  const auto* firstRegion = session.project().findRegion(regionId);
  CHECK(firstRegion->findLyric(session.project().findNote(first)->lyricTokenId)
            ->surface == U"one");
  CHECK(session.undo());
  firstRegion = session.project().findRegion(regionId);
  CHECK(firstRegion->findLyric(session.project().findNote(first)->lyricTokenId)
            ->surface == U"la");
  CHECK(firstRegion->findLyric(session.project().findNote(first)->lyricTokenId)
            ->language == seam::domain::Language::English);
}

TEST_CASE("piano roll quantize, slur and melisma edits are undoable") {
  seam::application::ProjectFactory factory{13000U};
  auto project = factory.createProject("Note performance");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, trackId, "Verse",
                                           seam::time::Tick{0},
                                           seam::time::Tick{3840});
  auto [firstToken, firstNote] = factory.makeNote(
      seam::time::Tick{115}, seam::time::Tick{420}, 60U, U"la",
      seam::domain::Language::English);
  auto [secondToken, secondNote] = factory.makeNote(
      seam::time::Tick{740}, seam::time::Tick{420}, 62U, U"li",
      seam::domain::Language::English);
  const auto first = firstNote.id;
  const auto second = secondNote.id;
  auto* region = project.findRegion(regionId);
  region->lyrics = {firstToken, secondToken};
  region->notes = {firstNote, secondNote};
  seam::application::EditorSession session{std::move(project)};
  seam::ui::PianoRollModel model{session, factory, regionId};
  session.selection().replace({first, second});
  CHECK(model.quantizeSelection(seam::time::Tick{240}));
  CHECK(session.project().findNote(first)->startTick == seam::time::Tick{0});
  CHECK(session.project().findNote(second)->startTick == seam::time::Tick{720});
  CHECK(model.setSelectionSlur(true));
  CHECK(session.project().findNote(first)->slurGroup.has_value());
  CHECK(session.project().findNote(second)->slurGroup ==
        session.project().findNote(first)->slurGroup);
  CHECK(model.setSelectionMelisma());
  CHECK(session.project().findNote(second)->lyricTokenId ==
        session.project().findNote(first)->lyricTokenId);
  CHECK(session.undo());
  CHECK(session.project().findNote(second)->lyricTokenId !=
        session.project().findNote(first)->lyricTokenId);
}
