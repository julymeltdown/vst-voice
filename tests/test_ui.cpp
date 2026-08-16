#include "test_framework.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/ui/note_spatial_index.hpp"
#include "seam/ui/phoneme_lane_model.hpp"
#include "seam/ui/text_composition_model.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/ui/piano_roll_model.hpp"
#include "seam/ui/timeline_transform.hpp"

TEST_CASE("timeline zoom keeps the anchor tick stable") {
  seam::ui::TimelineTransform transform{960, 100.0, seam::time::Tick{480}};
  const auto before = transform.pixelToTick(320.0);
  transform.zoomAround(320.0, 2.0);
  const auto after = transform.pixelToTick(320.0);
  CHECK(before == after);
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
}
