#include "test_framework.hpp"

#include "seam/application/note_commands.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/ui/piano_roll_model.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/synthesis/performance_compiler.hpp"
#include <limits>

TEST_CASE("lyric distribution preserves languages and assigns shared melisma tokens once") {
  using namespace seam;
  application::ProjectFactory factory{12500U}; auto project = factory.createProject("Distribution groups");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{7680});
  auto [firstLyric, first] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U, U"la", domain::Language::English);
  auto [unused, continuation] = factory.makeNote(time::Tick{480}, time::Tick{480}, 62U, U"unused", domain::Language::English);
  static_cast<void>(unused); continuation.lyricTokenId = firstLyric.id;
  auto [lastLyric, last] = factory.makeNote(time::Tick{960}, time::Tick{480}, 64U, U"あ", domain::Language::Japanese);
  project.findRegion(regionId)->lyrics = {firstLyric, lastLyric};
  project.findRegion(regionId)->notes = {first, continuation, last};
  application::EditorSession session{std::move(project)}; ui::PianoRollModel model{session, factory, regionId};
  const auto before = session.project();
  session.selection().replace({first.id, last.id});
  CHECK(!model.distributeSelectedLyrics(U"hello い")); CHECK(session.project() == before);
  session.selection().replace({last.id, continuation.id, first.id});
  const auto tooMany = model.distributeSelectedLyrics(U"one two three"); CHECK(tooMany);
  CHECK(!tooMany.value().committed); CHECK(tooMany.value().targetNotes == 3U);
  CHECK(tooMany.value().targetLyrics == 2U); CHECK(tooMany.value().leftoverSyllables == 1U);
  CHECK(session.project() == before);
  const auto result = model.distributeSelectedLyrics(U"hello\u3000い"); CHECK(result);
  CHECK(result.value().committed); CHECK(result.value().appliedSyllables == 2U); CHECK(result.value().changedLyrics == 2U);
  const auto* region = session.project().findRegion(regionId);
  CHECK(region->findLyric(firstLyric.id)->surface == U"hello");
  CHECK(region->findLyric(firstLyric.id)->language == domain::Language::English);
  CHECK(region->findLyric(lastLyric.id)->surface == U"い");
  CHECK(region->findLyric(lastLyric.id)->language == domain::Language::Japanese);
  CHECK(region->findNote(continuation.id)->lyricTokenId == firstLyric.id);
  const auto after = session.project(); CHECK(session.undo()); CHECK(session.project() == before);
  CHECK(session.redo()); CHECK(session.project() == after);
  const auto revision = session.revision();
  const auto noop = model.distributeSelectedLyrics(U"hello い"); CHECK(noop); CHECK(noop.value().committed);
  CHECK(noop.value().changedLyrics == 0U); CHECK(session.revision() == revision);
  CHECK(model.distributeSelectedLyrics(U"hello い", domain::Language::Unspecified));
  CHECK(session.project().findRegion(regionId)->findLyric(firstLyric.id)->language == domain::Language::Unspecified);
  CHECK(session.undo()); CHECK(session.project() == after);
}

TEST_CASE("lyric distribution rejects malformed over-limit and foreign targets without mutation") {
  using namespace seam;
  application::ProjectFactory factory{12400U}; auto project = factory.createProject("Distribution bounds");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{960});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U, U"la", domain::Language::English);
  project.findRegion(regionId)->lyrics = {lyric}; project.findRegion(regionId)->notes = {note};
  application::EditorSession session{std::move(project)}; ui::PianoRollModel model{session, factory, regionId};
  session.selection().selectOnly(note.id); const auto before = session.project();
  CHECK(!model.distributeSelectedLyrics(std::u32string{static_cast<char32_t>(0xd800U)}));
  CHECK(!model.distributeSelectedLyrics(std::u32string(4U * 1024U * 1024U + 1U, U'a')));
  std::u32string many; for (int i = 0; i < 10001; ++i) many += U"a ";
  CHECK(!model.distributeSelectedLyrics(many));
  session.selection().add(domain::NoteId{99999U}); CHECK(!model.distributeSelectedLyrics(U"hi"));
  CHECK(session.project() == before); CHECK(!session.canUndo());
}

TEST_CASE("duplicated melisma retains shared syllable and independent slur identities") {
  using namespace seam;
  application::ProjectFactory factory{12600U};
  auto project = factory.createProject("Independent melisma copy");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  auto [lyric, first] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U, U"か", domain::Language::Japanese);
  auto [unused, second] = factory.makeNote(time::Tick{480}, time::Tick{960}, 64U, U"か", domain::Language::Japanese);
  second.lyricTokenId = lyric.id;
  first.articulation = second.articulation = domain::NoteArticulation::Legato;
  first.slurGroup = second.slurGroup = 17U;
  auto* region = project.findRegion(regionId);
  region->lyrics = {lyric}; region->notes = {first, second};
  CHECK(region->validate());
  application::EditorSession session{std::move(project)};
  ui::PianoRollModel model{session, factory, regionId};
  session.selection().replace({first.id, second.id});
  CHECK(model.duplicateSelection());
  const auto copied = session.selection().noteIds();
  const auto* a = session.project().findNote(copied[0]);
  const auto* b = session.project().findNote(copied[1]);
  if (a->startTick > b->startTick) std::swap(a, b);
  CHECK(a->lyricTokenId == b->lyricTokenId); CHECK(a->lyricTokenId != lyric.id);
  CHECK(a->slurGroup == b->slurGroup); CHECK(a->slurGroup != first.slurGroup);
  CHECK(domain::continuesSharedLyric(*a, *b));
  const auto copiedLyric = a->lyricTokenId;
  CHECK(session.project().findRegion(regionId)->lyrics.size() == 2U);
  const auto resolved = phonemizer::resolveJapanesePronunciation(*session.project().findRegion(regionId));
  CHECK(resolved); CHECK(resolved.value().pronunciation.tokens.size() == 6U);
  CHECK(session.undo());
  CHECK(session.project().findRegion(regionId)->lyrics.size() == 1U);
  CHECK(session.project().findNote(first.id)->slurGroup == 17U);
  CHECK(session.redo());
  CHECK(session.project().findRegion(regionId)->findLyric(copiedLyric) != nullptr);
  CHECK(session.project().findRegion(regionId)->lyrics.size() == 2U);
  session.project().findNote(first.id)->slurGroup = std::numeric_limits<std::uint64_t>::max();
  session.selection().replace({first.id, second.id});
  CHECK(!model.duplicateSelection()); CHECK(session.project().noteCount() == 4U);
}

TEST_CASE("shared lyric note insertion requires exact existing token and preserves its owner on undo") {
  using namespace seam;
  application::ProjectFactory factory{12700U};
  auto project = factory.createProject("Shared lyric admission");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U, U"la", domain::Language::English);
  application::AddNoteCommand shared{regionId, lyric, note, application::AddNoteCommand::LyricMode::ReuseExact};
  CHECK(!shared.apply(project));
  project.findRegion(regionId)->lyrics.push_back(lyric);
  project.findRegion(regionId)->lyrics.front().surface = U"changed";
  CHECK(!shared.apply(project)); CHECK(project.noteCount() == 0U);
  project.findRegion(regionId)->lyrics.front() = lyric;
  application::AddNoteCommand normal{regionId, lyric, note};
  CHECK(!normal.apply(project));
  CHECK(shared.apply(project)); CHECK(shared.revert(project));
  CHECK(project.findRegion(regionId)->findLyric(lyric.id) != nullptr);
  CHECK(shared.apply(project)); CHECK(project.noteCount() == 1U);
}

TEST_CASE("mixed slur disable preserves lyrics and unrelated articulation while exhaustion is atomic") {
  using namespace seam;
  application::ProjectFactory factory{12800U};
  auto project = factory.createProject("Slur boundaries");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{9600});
  auto* region = project.findRegion(regionId);
  for (int index = 0; index < 3; ++index) {
    auto [lyric, note] = factory.makeNote(time::Tick{index * 480}, time::Tick{480}, 60U, U"あ", domain::Language::Japanese);
    if (index == 0) { note.articulation = domain::NoteArticulation::Legato; note.slurGroup = 5U; }
    if (index == 1) note.articulation = domain::NoteArticulation::Staccato;
    if (index == 2) note.slurGroup = std::numeric_limits<std::uint64_t>::max();
    region->lyrics.push_back(lyric); region->notes.push_back(note);
  }
  const auto first = region->notes[0]; const auto second = region->notes[1]; const auto outside = region->notes[2];
  application::EditorSession session{project};
  ui::PianoRollModel model{session, factory, regionId};
  session.selection().replace({first.id, second.id});
  CHECK(model.setSelectionSlur(false));
  CHECK(session.project().findNote(first.id)->articulation == domain::NoteArticulation::Normal);
  CHECK(!session.project().findNote(first.id)->slurGroup);
  CHECK(session.project().findNote(second.id)->articulation == domain::NoteArticulation::Staccato);
  CHECK(session.project().findNote(first.id)->lyricTokenId == first.lyricTokenId);
  CHECK(*session.project().findNote(outside.id) == outside);
  const auto disabled = session.project(); const auto revision = session.revision();
  CHECK(!model.setSelectionSlur(true)); // No selected group: allocating beyond UINT64_MAX must not wrap to zero.
  CHECK(session.project() == disabled); CHECK(session.revision() == revision);
  CHECK(session.undo()); CHECK(session.project() == project);
  CHECK(session.redo()); CHECK(session.project() == disabled);
  CHECK(session.undo());
  CHECK(model.setSelectionSlur(true)); // Extending the selected existing group does not allocate another ID.
  CHECK(session.project().findNote(first.id)->slurGroup == 5U);
  CHECK(session.project().findNote(second.id)->slurGroup == 5U);
  CHECK(*session.project().findNote(outside.id) == outside);
}

TEST_CASE("phrase duplication preserves unequal durations rests and rejects end overflow atomically") {
  using namespace seam;
  application::ProjectFactory factory{12500U};
  auto project = factory.createProject("Phrase translation");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto region = factory.addRegion(project, track, "Verse", time::Tick{0}, time::Tick{9600});
  application::EditorSession session{std::move(project)};
  auto add = [&](std::int64_t start, std::int64_t duration) {
    auto [token, note] = factory.makeNote(time::Tick{start}, time::Tick{duration}, 60U,
                                         U"la", domain::Language::English);
    const auto id = note.id;
    CHECK(session.execute(std::make_unique<application::AddNoteCommand>(region, std::move(token), std::move(note))));
    return id;
  };
  const auto first = add(120, 240);
  const auto second = add(960, 720);
  ui::PianoRollModel model{session, factory, region};
  session.selection().replace({second, first});
  CHECK(model.duplicateSelection());
  const auto copied = session.selection().noteIds();
  CHECK(copied.size() == 2U);
  const auto* a = session.project().findNote(copied[0]);
  const auto* b = session.project().findNote(copied[1]);
  if (a->startTick > b->startTick) std::swap(a, b);
  const auto shift = time::Tick{1560} + session.project().settings().snapGrid;
  CHECK(a->startTick == time::Tick{120} + shift);
  CHECK(b->startTick == time::Tick{960} + shift);
  CHECK(a->durationTick == time::Tick{240});
  CHECK(b->durationTick == time::Tick{720});
  CHECK(b->startTick - a->endTick() == time::Tick{600});
  CHECK(session.undo()); CHECK(session.project().noteCount() == 2U);
  CHECK(session.redo()); CHECK(session.project().noteCount() == 4U);
  CHECK(session.project().findNote(copied[0]) != nullptr);
  const auto last = add(2400, 480);
  // Isolate duplication's arithmetic admission from AddNoteCommand's separate
  // region-growth limits. This note's own start/end remain valid int64 ticks.
  session.project().findNote(last)->startTick = time::Tick{std::numeric_limits<std::int64_t>::max() - 480};
  session.selection().replace({first, last});
  const auto before = session.project().noteCount();
  CHECK(!model.duplicateSelection());
  CHECK(session.project().noteCount() == before);
  CHECK(session.selection().contains(first)); CHECK(session.selection().contains(last));
  CHECK(session.undo()); // Rejection did not add an undo command.
  CHECK(session.project().findNote(last) == nullptr);
}

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

TEST_CASE("editor melisma continues the vowel and undo restores repeated consonants") {
  seam::application::ProjectFactory factory{14000U};
  auto project = factory.createProject("Melisma pronunciation");
  const auto track = factory.addVocalTrack(project, "Singer");
  const auto regionId = factory.addRegion(project, track, "Phrase", seam::time::Tick{0}, seam::time::Tick{3840});
  auto* region = project.findRegion(regionId);
  for (const auto start : {0, 960}) {
    auto [lyric, note] = factory.makeNote(seam::time::Tick{start}, seam::time::Tick{960},
        60U, U"か", seam::domain::Language::Japanese);
    region->lyrics.push_back(lyric); region->notes.push_back(note);
  }
  const auto first = region->notes[0].id;
  const auto second = region->notes[1].id;
  seam::application::EditorSession session{std::move(project)};
  seam::ui::PianoRollModel model{session, factory, regionId};
  session.selection().replace({first, second});
  const auto tokens = [&] {
    const auto resolved = seam::phonemizer::resolveJapanesePronunciation(*session.project().findRegion(regionId));
    CHECK(resolved); return resolved.value().pronunciation.tokens;
  };
  CHECK(tokens().size() == 4U);
  const auto before = session.project();
  CHECK(model.setSelectionMelisma());
  const auto linked = tokens();
  CHECK(linked.size() == 3U);
  CHECK(linked[0].symbol == "k"); CHECK(linked[1].symbol == "a"); CHECK(linked[2].symbol == "a");
  const auto score = seam::synthesis::compileScorePerformance(session.project(),
      *session.project().findRegion(regionId), 48000U, linked);
  CHECK(score); CHECK(!score.value().notes()[1].reattack);
  CHECK(session.undo());
  CHECK(session.project() == before);
  CHECK(tokens().size() == 4U);
  CHECK(session.redo());
  CHECK(tokens().size() == 3U);
  const auto linkedRevision = session.project().findRegion(regionId)->performance.revision.pronunciation;
  CHECK(model.setSelectionSlur(false));
  CHECK(session.project().findRegion(regionId)->performance.revision.pronunciation == linkedRevision + 1U);
  CHECK(session.project().findNote(first)->articulation == seam::domain::NoteArticulation::Normal);
  CHECK(session.project().findNote(second)->articulation == seam::domain::NoteArticulation::Normal);
  CHECK(tokens().size() == 4U);
  CHECK(session.undo());
  CHECK(tokens().size() == 3U);
  CHECK(session.project().findRegion(regionId)->performance.revision.pronunciation == linkedRevision);
}
