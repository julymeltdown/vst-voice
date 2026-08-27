#include "test_framework.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/ui/note_spatial_index.hpp"
#include "seam/ui/note_visual_layout.hpp"
#include "seam/ui/phoneme_lane_model.hpp"
#include "seam/ui/text_composition_model.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/ui/piano_roll_model.hpp"
#include "seam/ui/timeline_transform.hpp"

#include <algorithm>

TEST_CASE("timeline zoom keeps the anchor tick stable") {
  seam::ui::TimelineTransform transform{960, 100.0, seam::time::Tick{480}};
  const auto before = transform.pixelToTick(320.0);
  transform.zoomAround(320.0, 2.0);
  const auto after = transform.pixelToTick(320.0);
  CHECK(before == after);
}

TEST_CASE("note visual layout preserves timeline truth while exposing overlaps") {
  std::vector<seam::ui::NoteVisualLayoutItem> items;
  for (std::size_t index = 0U; index < 5U; ++index) {
    items.push_back(seam::ui::NoteVisualLayoutItem{
        .noteId = seam::domain::NoteId{100U + index},
        .midiKey = 64U,
        .start = seam::time::Tick{960},
        .end = seam::time::Tick{1920},
        .timelineBounds = seam::ui::Rect{100.0, 40.0, 120.0, 18.0},
    });
  }
  const auto first = seam::ui::layoutNoteVisuals(items);
  const auto second = seam::ui::layoutNoteVisuals(items);
  CHECK(first.size() == 5U);
  CHECK(first[0].paintBounds.y == second[0].paintBounds.y);
  CHECK(first[4].bandIndex == second[4].bandIndex);
  CHECK(first[0].groupMemberCount == 5U);
  CHECK(first[0].visibleBandCount == 3U);
  CHECK(first[0].hiddenMemberCount == 2U);
  CHECK(first[0].drawsOverflowIndicator);
  CHECK(first[3].hiddenByDensity);
  CHECK(first[4].hiddenByDensity);
  CHECK(first[0].paintBounds.y != first[1].paintBounds.y);
  CHECK(first[1].paintBounds.y != first[2].paintBounds.y);
  for (const auto& layout : first) {
    CHECK(layout.paintBounds.x == 100.0);
    CHECK(layout.paintBounds.width == 120.0);
    CHECK(layout.hitBounds.width >= layout.paintBounds.width);
  }
}

TEST_CASE("piano roll returns every overlapping note in stable cycle order") {
  seam::application::ProjectFactory factory{220U};
  auto project = factory.createProject("Overlaps");
  const auto trackId = factory.addVocalTrack(project, "Track");
  const auto regionId = factory.addRegion(
      project, trackId, "Region", seam::time::Tick{0}, seam::time::Tick{3840});
  auto* region = project.findRegion(regionId);
  std::vector<seam::domain::NoteId> expected;
  for (std::size_t index = 0U; index < 5U; ++index) {
    auto [lyric, note] = factory.makeNote(seam::time::Tick{960},
                                          seam::time::Tick{480}, 64U, U"あ");
    expected.push_back(note.id);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  }
  region->sortNotes();
  std::sort(expected.begin(), expected.end());
  seam::application::EditorSession session{std::move(project)};
  seam::ui::PianoRollModel model{session, factory, regionId};
  model.setViewport({{0.0, 0.0, 1280.0, 720.0}, 72.0});
  model.pitch().setTopMidiKey(84U);
  const auto visuals = model.visibleNotes();
  CHECK(visuals.size() == 5U);
  const auto candidates = model.overlapCandidatesAt(
      {visuals.front().bounds.x + 2.0, visuals.front().bounds.y + 2.0});
  CHECK(candidates == expected);
}

TEST_CASE("spatial index virtualizes a ten-thousand-note project") {
  seam::application::ProjectFactory factory{1000};
  auto project = factory.createProject("Virtualization");
  const auto trackId = factory.addVocalTrack(project, "Track");
  const auto regionId = factory.addRegion(
      project, trackId, "Region", seam::time::Tick{0}, seam::time::Tick{3000000});
  auto* region = project.findRegion(regionId);
  region->lyrics.reserve(10000);
  region->notes.reserve(10000);
  for (std::size_t index = 0; index < 10000; ++index) {
    auto [lyric, note] = factory.makeNote(
        seam::time::Tick{static_cast<std::int64_t>(index) * 240},
        seam::time::Tick{180},
        static_cast<std::uint8_t>(48 + index % 24),
        U"a");
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  }
  region->sortNotes();

  seam::ui::NoteSpatialIndex index;
  index.rebuild(project);
  CHECK(index.size() == 10000);
  const auto visible = index.query(seam::time::Tick{0}, seam::time::Tick{15360}, 48, 72);
  CHECK(!visible.empty());
  CHECK(visible.size() < 100);
}

TEST_CASE("spatial index prunes non-overlapping prefixes without dropping long notes") {
  seam::application::ProjectFactory factory{1000};
  auto project = factory.createProject("Spatial overlap bounds");
  const auto trackId = factory.addVocalTrack(project, "Track");
  const auto regionId = factory.addRegion(
      project, trackId, "Region", seam::time::Tick{0}, seam::time::Tick{200000});
  auto* region = project.findRegion(regionId);

  auto [longLyric, longNote] = factory.makeNote(
      seam::time::Tick{5000}, seam::time::Tick{15000}, 60U, U"long");
  const auto longNoteId = longNote.id;
  region->lyrics.push_back(std::move(longLyric));
  region->notes.push_back(std::move(longNote));

  auto [earlyLyric, earlyNote] = factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{240}, 60U, U"early");
  const auto earlyNoteId = earlyNote.id;
  region->lyrics.push_back(std::move(earlyLyric));
  region->notes.push_back(std::move(earlyNote));

  auto [targetLyric, targetNote] = factory.makeNote(
      seam::time::Tick{20000}, seam::time::Tick{240}, 64U, U"target");
  const auto targetNoteId = targetNote.id;
  region->lyrics.push_back(std::move(targetLyric));
  region->notes.push_back(std::move(targetNote));
  region->sortNotes();

  seam::ui::NoteSpatialIndex index;
  index.rebuild(project);
  const auto visible = index.query(seam::time::Tick{15000}, seam::time::Tick{21000}, 60, 64);

  CHECK(visible.size() == 2U);
  CHECK(std::find_if(visible.begin(), visible.end(), [longNoteId](const auto& note) {
    return note.noteId == longNoteId;
  }) != visible.end());
  CHECK(std::find_if(visible.begin(), visible.end(), [targetNoteId](const auto& note) {
    return note.noteId == targetNoteId;
  }) != visible.end());
  CHECK(std::find_if(visible.begin(), visible.end(), [earlyNoteId](const auto& note) {
    return note.noteId == earlyNoteId;
  }) == visible.end());
}

TEST_CASE("piano roll draws, selects, moves, and hit-tests notes") {
  seam::application::ProjectFactory factory{10};
  auto project = factory.createProject("Piano roll");
  project.settings().snapGrid = seam::time::Tick{240};
  const auto trackId = factory.addVocalTrack(project, "Track");
  const auto regionId = factory.addRegion(
      project, trackId, "Region", seam::time::Tick{0}, seam::time::Tick{15360});
  seam::application::EditorSession session{std::move(project)};
  seam::ui::PianoRollModel model{session, factory, regionId};
  model.setViewport({{0, 0, 1280, 720}, 72});
  model.pitch().setTopMidiKey(84);

  const auto added = model.drawNote({72.0 + 10.0, model.pitch().midiToPixel(60) + 4.0},
                                    seam::time::Tick{480}, U"a");
  CHECK(added);
  CHECK(session.selection().contains(added.value()));
  const auto visuals = model.visibleNotes();
  CHECK(visuals.size() == 1);
  CHECK(model.hitTest({visuals.front().bounds.x + 2.0, visuals.front().bounds.y + 2.0}) ==
        added.value());
  CHECK(model.overlapCandidatesAt(
            {visuals.front().bounds.x + 2.0, visuals.front().bounds.y + 2.0}) ==
        std::vector<seam::domain::NoteId>{added.value()});
  CHECK(model.moveSelection(seam::time::Tick{240}, 2));
  CHECK(session.project().findNote(added.value())->midiKey == 62);
  CHECK(session.undo());
  CHECK(session.project().findNote(added.value())->midiKey == 60);

  const auto selectedBounds = model.visibleNotes().front().bounds;
  model.selectInBox(selectedBounds);
  CHECK(session.selection().contains(added.value()));
  CHECK(model.deleteSelection());
  CHECK(session.project().findNote(added.value()) == nullptr);
  CHECK(session.undo());
  CHECK(session.project().findNote(added.value()) != nullptr);
}

TEST_CASE("piano roll defaults newly drawn notes to Japanese Hiragana") {
  seam::application::ProjectFactory factory{10};
  auto project = factory.createProject("Japanese piano roll");
  const auto trackId = factory.addVocalTrack(project, "Track");
  const auto regionId = factory.addRegion(
      project, trackId, "Region", seam::time::Tick{0}, seam::time::Tick{15360});
  seam::application::EditorSession session{std::move(project)};
  seam::ui::PianoRollModel model{session, factory, regionId};
  model.setViewport({{0, 0, 1280, 720}, 72});
  model.pitch().setTopMidiKey(84);

  const auto added = model.drawNote(
      {72.0 + 10.0, model.pitch().midiToPixel(60) + 4.0},
      seam::time::Tick{480});
  CHECK(added);
  const auto* note = session.project().findNote(added.value());
  CHECK(note != nullptr);
  const auto* region = session.project().findRegion(regionId);
  CHECK(region != nullptr);
  const auto* lyric = region->findLyric(note->lyricTokenId);
  CHECK(lyric != nullptr);
  CHECK(lyric->surface == U"あ");
}

TEST_CASE("piano roll visuals stay within the selected region") {
  seam::application::ProjectFactory factory{100U};
  auto project = factory.createProject("Region isolation");
  const auto trackId = factory.addVocalTrack(project, "Track");
  const auto firstRegionId = factory.addRegion(
      project, trackId, "First", seam::time::Tick{0}, seam::time::Tick{1920});
  const auto secondRegionId = factory.addRegion(
      project, trackId, "Second", seam::time::Tick{0}, seam::time::Tick{1920});
  auto* firstRegion = project.findRegion(firstRegionId);
  auto* secondRegion = project.findRegion(secondRegionId);
  auto [firstLyric, firstNote] = factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{480}, 60U, U"a");
  auto [secondLyric, secondNote] = factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{480}, 64U, U"i");
  const auto firstNoteId = firstNote.id;
  const auto secondNoteId = secondNote.id;
  firstRegion->lyrics.push_back(std::move(firstLyric));
  firstRegion->notes.push_back(std::move(firstNote));
  secondRegion->lyrics.push_back(std::move(secondLyric));
  secondRegion->notes.push_back(std::move(secondNote));
  seam::application::EditorSession session{std::move(project)};
  seam::ui::PianoRollModel model{session, factory, firstRegionId};
  model.setViewport({{0.0, 0.0, 1280.0, 720.0}, 72.0});

  const auto firstVisuals = model.visibleNotes();
  CHECK(firstVisuals.size() == 1U);
  CHECK(firstVisuals.front().noteId == firstNoteId);

  model.setRegionId(secondRegionId);
  const auto secondVisuals = model.visibleNotes();
  CHECK(secondVisuals.size() == 1U);
  CHECK(secondVisuals.front().noteId == secondNoteId);
}

TEST_CASE("piano roll keeps subpixel-duration notes addressable") {
  seam::application::ProjectFactory factory{40U};
  auto project = factory.createProject("Subpixel notes");
  const auto trackId = factory.addVocalTrack(project, "Track");
  const auto regionId = factory.addRegion(
      project, trackId, "Region", seam::time::Tick{0}, seam::time::Tick{960});
  auto [lyric, note] = factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{12}, 60U, U"a");
  const auto noteId = note.id;
  auto* region = project.findRegion(regionId);
  region->lyrics.push_back(std::move(lyric));
  region->notes.push_back(std::move(note));
  seam::application::EditorSession session{std::move(project)};
  seam::ui::PianoRollModel model{session, factory, regionId};
  model.setViewport({{0.0, 0.0, 1280.0, 720.0}, 0.0});
  model.pitch().setTopMidiKey(84);

  const auto visuals = model.visibleNotes();
  CHECK(visuals.size() == 1U);
  CHECK(visuals.front().noteId == noteId);
  CHECK(visuals.front().bounds.width > 0.0);
  CHECK(visuals.front().bounds.height > 0.0);
  CHECK(model.hitTest(seam::ui::Point{visuals.front().bounds.x + 0.5,
                                      visuals.front().bounds.y + 0.5}) == noteId);
}

TEST_CASE("text composition models native IME begin update commit and cancel") {
  seam::ui::TextCompositionModel composition;
  const seam::domain::LyricTokenId lyricId{55};
  CHECK(composition.begin(lyricId, U"き"));
  CHECK(composition.active());
  CHECK(composition.update(U"きゃ", seam::ui::CompositionSelection{1, 1}));
  CHECK(!composition.update(U"きゃ", seam::ui::CompositionSelection{4, 0}));
  const auto committed = composition.commit();
  CHECK(committed);
  CHECK(committed.value().lyricId == lyricId);
  CHECK(committed.value().text == U"きゃ");
  CHECK(!composition.active());

  CHECK(composition.begin(lyricId, U"あ"));
  composition.cancel();
  CHECK(!composition.active());
  CHECK(!composition.commit());
}

TEST_CASE("phoneme lane exposes generated and manually timed phonemes") {
  seam::application::ProjectFactory factory{200};
  auto project = factory.createProject("Phoneme lane");
  const auto trackId = factory.addVocalTrack(project, "Track");
  const auto regionId = factory.addRegion(
      project, trackId, "Region", seam::time::Tick{0}, seam::time::Tick{15360});
  auto [lyric, note] = factory.makeNote(
      seam::time::Tick{960}, seam::time::Tick{960}, 60, U"き");
  const auto noteId = note.id;
  auto* region = project.findRegion(regionId);
  region->lyrics.push_back(lyric);
  region->notes.push_back(note);
  region->phonemeOverrides.push_back(seam::domain::PhonemeOverride{
      .key = seam::domain::PhonemeKey{noteId, 0},
      .symbol = std::nullopt,
      .timing = seam::domain::PhonemeTiming{
          .startOffset = seam::time::Microseconds{-60000},
          .endOffset = seam::time::Microseconds{0},
      },
      .locked = true,
  });

  seam::phonemizer::JapaneseKanaPhonemizer phonemizer;
  const auto phonemes = phonemizer.phonemize(*region);
  seam::application::EditorSession session{std::move(project)};
  seam::ui::PianoRollModel pianoRoll{session, factory, regionId};
  pianoRoll.setViewport({{0, 0, 1280, 720}, 72});
  pianoRoll.timeline().setPixelsPerQuarter(96.0);
  pianoRoll.pitch().setTopMidiKey(84);

  seam::ui::PhonemeLaneModel lane;
  lane.rebuild(pianoRoll, phonemes, 600.0, 32.0);
  CHECK(lane.visuals().size() == 2);
  CHECK(lane.visuals().front().symbol == "k");
  CHECK(lane.visuals().front().locked);
  CHECK(lane.visuals().front().timingOverridden);
  const auto center = seam::ui::Point{
      lane.visuals().front().bounds.x + 1.0,
      lane.visuals().front().bounds.y + 1.0};
  const seam::domain::PhonemeKey expectedKey{noteId, 0};
  CHECK(lane.hitTest(center) == expectedKey);
  const auto leftBoundary = lane.hitTestBoundary(
      seam::ui::Point{lane.visuals().front().bounds.x,
                      lane.visuals().front().bounds.y + 4.0});
  CHECK(leftBoundary.has_value());
  CHECK(leftBoundary->first == expectedKey);
  CHECK(leftBoundary->second);
}
