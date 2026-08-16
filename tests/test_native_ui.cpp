#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/pixel_surface.hpp"

#include <filesystem>
#include <string>
#include <utility>

namespace {

struct NativeUiFixture final {
  seam::application::ProjectFactory factory{7000U};
  seam::domain::RegionId regionId{};
  seam::domain::NoteId noteId{};
  seam::domain::LyricTokenId lyricId{};
  seam::application::EditorSession session;

  NativeUiFixture() : session(makeProject()) {}

  seam::domain::Project makeProject() {
    auto project = factory.createProject("Native UI");
    const auto trackId = factory.addVocalTrack(project, "Voice");
    regionId = factory.addRegion(project, trackId, "Region",
                                 seam::time::Tick{0}, seam::time::Tick{7680});
    auto [lyric, note] = factory.makeNote(
        seam::time::Tick{960}, seam::time::Tick{960}, 64U, U"edge",
        seam::domain::Language::English);
    noteId = note.id;
    lyricId = lyric.id;
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    region->sortNotes();
    return project;
  }
};

}  // namespace

TEST_CASE("software raster surface paints deterministically and exports PPM") {
  seam::native_ui::PixelSurface first{320U, 180U};
  seam::native_ui::RasterCanvas canvas{first, 1.0};
  canvas.clear(seam::native_ui::Color{16, 15, 19, 255});
  canvas.fillRect(seam::ui::Rect{10.0, 10.0, 100.0, 40.0},
                  seam::native_ui::Color{139, 76, 105, 255});
  canvas.line(seam::ui::Point{0.0, 0.0}, seam::ui::Point{319.0, 179.0},
              seam::native_ui::Color{98, 192, 190, 255}, 2.0);
  canvas.drawText(seam::ui::Point{20.0, 70.0}, "SEAM 05",
                  seam::native_ui::Color{241, 235, 242, 255}, 12.0);

  seam::native_ui::PixelSurface second{320U, 180U};
  seam::native_ui::RasterCanvas secondCanvas{second, 1.0};
  secondCanvas.clear(seam::native_ui::Color{16, 15, 19, 255});
  secondCanvas.fillRect(seam::ui::Rect{10.0, 10.0, 100.0, 40.0},
                        seam::native_ui::Color{139, 76, 105, 255});
  secondCanvas.line(seam::ui::Point{0.0, 0.0},
                    seam::ui::Point{319.0, 179.0},
                    seam::native_ui::Color{98, 192, 190, 255}, 2.0);
  secondCanvas.drawText(seam::ui::Point{20.0, 70.0}, "SEAM 05",
                        seam::native_ui::Color{241, 235, 242, 255}, 12.0);
  CHECK(first.checksum() == second.checksum());
  CHECK(first.checksum() != 0U);

  const auto directory = seam::test::support::temporaryDirectory("native-ui-ppm");
  const auto path = directory / "surface.ppm";
  CHECK(first.writePpm(path));
  CHECK(std::filesystem::file_size(path) > first.pixels().size() * 3U);
}

TEST_CASE("editor scene remains logically identical at one and two times scale") {
  NativeUiFixture fixture;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  controller.resize(1280.0, 720.0);
  seam::native_ui::EditorScenePainter painter;

  seam::native_ui::PixelSurface one{1280U, 720U};
  seam::native_ui::RasterCanvas oneCanvas{one, 1.0};
  painter.paint(oneCanvas, controller.pianoRoll(), controller.sceneState());

  seam::native_ui::PixelSurface two{2560U, 1440U};
  seam::native_ui::RasterCanvas twoCanvas{two, 2.0};
  painter.paint(twoCanvas, controller.pianoRoll(), controller.sceneState());

  CHECK(one.checksum() != 0U);
  CHECK(two.checksum() != 0U);
  CHECK(one.width() * 2U == two.width());
  CHECK(one.height() * 2U == two.height());
  CHECK(controller.pianoRoll().visibleNotes().size() == 1U);
}

TEST_CASE("native controller commits one move command at pointer release") {
  NativeUiFixture fixture;
  std::size_t repaints = 0U;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .requestRepaint = [&repaints] { ++repaints; },
          .beginTextInput = {},
          .endTextInput = {},
          .setPlaying = {},
      }};
  controller.resize(1280.0, 720.0);
  const auto visual = controller.pianoRoll().visibleNotes().front();
  const seam::ui::Point start{
      visual.bounds.x + 4.0,
      visual.bounds.y + 4.0 + 98.0,
  };
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = start,
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(controller.pointerMove(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{start.x + 112.0, start.y - 18.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(fixture.session.revision() == 0U);
  CHECK(controller.pointerUp(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{start.x + 112.0, start.y - 18.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(fixture.session.revision() == 1U);
  const auto* moved = fixture.session.project().findNote(fixture.noteId);
  CHECK(moved != nullptr);
  CHECK(moved->midiKey == 65U);
  CHECK(moved->startTick == seam::time::Tick{1920});
  CHECK(repaints > 0U);
  CHECK(fixture.session.undo());
  CHECK(fixture.session.project().findNote(fixture.noteId)->midiKey == 64U);
}

TEST_CASE("native text input commits Unicode lyric through undoable command") {
  NativeUiFixture fixture;
  std::size_t begins = 0U;
  std::size_t ends = 0U;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .requestRepaint = {},
          .beginTextInput = [&begins](const auto&) { ++begins; },
          .endTextInput = [&ends] { ++ends; },
          .setPlaying = {},
      }};
  controller.resize(1280.0, 720.0);
  CHECK(controller.beginLyricEdit(fixture.noteId));
  CHECK(controller.textInputActive());
  CHECK(controller.updateTextComposition(
      U"継ぎ目", seam::ui::CompositionSelection{3U, 0U}));
  CHECK(controller.commitTextComposition(U"継ぎ目"));
  CHECK(!controller.textInputActive());
  const auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  CHECK(region->findLyric(fixture.lyricId)->surface == U"継ぎ目");
  CHECK(begins == 1U);
  CHECK(ends == 1U);
  CHECK(fixture.session.undo());
  CHECK(region->findLyric(fixture.lyricId)->surface == U"edge");
}
