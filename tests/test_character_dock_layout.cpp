// The character dock is the last thing that takes room from the piano roll and the first to give it
// back.
//
// A dock that is squeezed until its portrait is a strip and its name runs off the window is worse than
// no dock: the singer's face stops being recognisable and the identity stops being readable, which is
// exactly what the dock exists to show. These cases fix the collapse order -- artwork first, then the
// dock -- and prove it on pixels: at a full width the portrait is drawn, at a narrow width it is
// replaced by the dock's own background while the identity, state and performance strip stay, and at a
// width that cannot carry those the dock is not painted at all.
//
// The artwork here is a flat colour, not a reviewed asset, and no redesign is claimed: this is the
// collapse policy and one dock, measured on a painted surface.
#include "test_framework.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/character/performance.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/ui/piano_roll_model.hpp"

#include <cstdint>
#include <limits>
#include <utility>

namespace {

using namespace seam;

struct LayoutFixture final {
  application::ProjectFactory factory{7200U};
  domain::RegionId regionId{};
  application::EditorSession session;

  LayoutFixture() : session(makeProject()) {}

  domain::Project makeProject() {
    auto project = factory.createProject("Dock layout");
    const auto trackId = factory.addVocalTrack(project, "Voice");
    regionId = factory.addRegion(project, trackId, "Phrase", time::Tick{0}, time::Tick{1920});
    auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{1920}, 69U, U"a",
                                          domain::Language::Japanese);
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    return project;
  }
};

constexpr native_ui::Color kPortraitColor{40U, 20U, 60U, 255U};
constexpr native_ui::Color kDockBackground{18U, 16U, 21U, 255U};
constexpr native_ui::Color kWindowBackground{16U, 15U, 19U, 255U};

std::uint32_t pixelAt(const native_ui::PixelSurface& surface, std::uint32_t x, std::uint32_t y) {
  CHECK(x < surface.width());
  CHECK(y < surface.height());
  return surface.pixels()[static_cast<std::size_t>(y) * surface.width() + x];
}

TEST_CASE("The dock collapses in one order and hides rather than cropping or overflowing") {
  using native_ui::CharacterDockPresentation;
  CHECK(native_ui::resolveCharacterDockPresentation(238.0) == CharacterDockPresentation::Full);
  CHECK(native_ui::resolveCharacterDockPresentation(native_ui::kCharacterDockPortraitMinimumWidth) ==
        CharacterDockPresentation::Full);
  CHECK(native_ui::resolveCharacterDockPresentation(
            native_ui::kCharacterDockPortraitMinimumWidth - 1.0) ==
        CharacterDockPresentation::Compact);
  CHECK(native_ui::resolveCharacterDockPresentation(native_ui::kCharacterDockCompactMinimumWidth) ==
        CharacterDockPresentation::Compact);
  CHECK(native_ui::resolveCharacterDockPresentation(
            native_ui::kCharacterDockCompactMinimumWidth - 1.0) ==
        CharacterDockPresentation::Hidden);
  CHECK(native_ui::resolveCharacterDockPresentation(0.0) == CharacterDockPresentation::Hidden);
  // A width the layout could not compute hides the dock instead of drawing a fragment of it.
  CHECK(native_ui::resolveCharacterDockPresentation(
            std::numeric_limits<double>::quiet_NaN()) == CharacterDockPresentation::Hidden);
}

TEST_CASE("A narrow window drops the portrait, then the dock, and never the musical workspace") {
  LayoutFixture fixture;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
                                               {}};
  auto state = controller.sceneState();
  state.logicalHeight = 640.0;
  state.characterMode = domain::CharacterDisplayMode::Full;
  state.voiceIdentity.characterActive = true;
  state.characterName = "A name far longer than the dock can show";
  native_ui::PixelSurface portrait{220U, 200U};
  portrait.clear(kPortraitColor);
  state.characterPortrait = &portrait;
  // The package reserves the dock. Dock presence is a package question rather than a
  // frame question, so a caller building scene state by hand has to answer it too.
  state.characterDockReserved = true;
  state.characterPerformance = native_ui::EditorSceneState::CharacterPerformanceView{
      .mouth = character::MouthShape::Open,
      .energy = 0.8F,
      .expression = 0.0F,
      .performing = true,
      .reducedMotion = false,
  };

  const auto painterLayout = native_ui::EditorScenePainter{}.layout();
  native_ui::EditorScenePainter painter;
  const auto paint = [&](double logicalWidth) {
    state.logicalWidth = logicalWidth;
    native_ui::PixelSurface surface{static_cast<std::uint32_t>(logicalWidth),
                                    static_cast<std::uint32_t>(state.logicalHeight)};
    native_ui::RasterCanvas canvas{surface, 1.0};
    painter.paint(canvas, controller.pianoRoll(), state);
    return surface;
  };
  const auto presentation = [&](double logicalWidth) {
    const auto frame = native_ui::buildEditorFrameLayout(native_ui::EditorFrameLayoutInput{
        .logicalWidth = logicalWidth,
        .logicalHeight = state.logicalHeight,
        .toolbarHeight = painterLayout.toolbarHeight,
        .rulerHeight = painterLayout.rulerHeight,
        .statusHeight = painterLayout.statusHeight,
        .keyboardWidth = painterLayout.keyboardWidth,
        .minimumTimelineWidth = painterLayout.minimumTimelineWidth,
        .dockWidth = painterLayout.characterDockWidth,
        .dockVisible = true,
    });
    // The workspace promise: however narrow the window gets, the dock gives up its room first.
    CHECK(frame.timeline.width >= painterLayout.minimumTimelineWidth ||
          frame.editorRight >= logicalWidth);
    return std::pair{frame.editorRight, frame.dock.width};
  };

  const auto wide = presentation(560.0);
  const auto narrow = presentation(380.0);
  const auto squeezed = presentation(330.0);
  CHECK(native_ui::resolveCharacterDockPresentation(wide.second) ==
        native_ui::CharacterDockPresentation::Full);
  CHECK(native_ui::resolveCharacterDockPresentation(narrow.second) ==
        native_ui::CharacterDockPresentation::Compact);
  CHECK(native_ui::resolveCharacterDockPresentation(squeezed.second) ==
        native_ui::CharacterDockPresentation::Hidden);

  const auto wideSurface = paint(560.0);
  const auto narrowSurface = paint(380.0);
  const auto squeezedSurface = paint(330.0);
  CHECK(wideSurface.checksum() != narrowSurface.checksum());
  CHECK(narrowSurface.checksum() != squeezedSurface.checksum());
  CHECK(wideSurface.checksum() != squeezedSurface.checksum());

  // The same sample point inside the artwork's rectangle tells the whole story: the portrait, then the
  // dock's own background, then the window's.
  const auto portraitBounds = painterLayout.characterDockPortraitBounds(
      wide.first, state.logicalHeight - painterLayout.statusHeight, 560.0);
  const auto sampleY = static_cast<std::uint32_t>(portraitBounds.y + portraitBounds.height * 0.5);
  const auto sampleX = [&](double editorRight) {
    return static_cast<std::uint32_t>(editorRight) +
           static_cast<std::uint32_t>(painterLayout.characterDockPadding) + 4U;
  };
  CHECK(pixelAt(wideSurface, sampleX(wide.first), sampleY) == kPortraitColor.bgra());
  CHECK(pixelAt(narrowSurface, sampleX(narrow.first), sampleY) == kDockBackground.bgra());
  CHECK(pixelAt(squeezedSurface, sampleX(squeezed.first), sampleY) == kWindowBackground.bgra());

  // The identity and the performance survive the artwork's collapse.
  const auto dockLeft = static_cast<std::uint32_t>(narrow.first);
  bool sawDockInk = false;
  for (std::uint32_t y = static_cast<std::uint32_t>(painterLayout.toolbarHeight) + 1U;
       y < 320U && !sawDockInk; ++y) {
    for (std::uint32_t x = dockLeft + 1U; x + 1U < narrowSurface.width(); ++x) {
      const auto pixel = pixelAt(narrowSurface, x, y);
      if (pixel != kDockBackground.bgra() && pixel != kWindowBackground.bgra()) {
        sawDockInk = true;
        break;
      }
    }
  }
  CHECK(sawDockInk);
}

}  // namespace
