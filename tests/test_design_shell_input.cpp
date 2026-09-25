// Active SING shell input: gesture cancellation, surface/geometry ownership, IME placement and the
// hosted expression lane, driven through a real NativeEditorController and EditorSession.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/domain/project.hpp"
#include "seam/native_ui/design/sing_layout.hpp"
#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/ui/expression_lane.hpp"
#include "seam/voice_design/recipe_resource.hpp"

#include <limits>

namespace {

using namespace seam;
using native_ui::KeyEvent;
using native_ui::NativeKey;
using native_ui::PointerButton;
using native_ui::PointerEvent;
using native_ui::design::DesignMode;
using native_ui::design::DesignPreferences;
using native_ui::design::SingShell;

voice_design::VoiceRecipe shellRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "design-shell";
  recipe.seed = 1234U;
  voice_design::VoicePose pose;
  pose.phone = "a";
  pose.style = "neutral";
  pose.formants = {voice_design::ResonanceBand{700.0, 90.0, 0.0},
                   voice_design::ResonanceBand{1200.0, 100.0, -3.0},
                   voice_design::ResonanceBand{2600.0, 140.0, -6.0}};
  recipe.poses.push_back(pose);
  return recipe;
}

struct ShellFixture final {
  application::ProjectFactory factory{9600U};
  domain::TrackId trackId{};
  domain::RegionId regionId{};
  application::EditorSession session;
  native_ui::NativeEditorController controller;
  SingShell shell;

  ShellFixture() : session(makeProject()), controller{session, factory, regionId, {}} {
    controller.resize(1600.0, 900.0);
    shell.activate({}, DesignPreferences{.mode = DesignMode::Emo});
  }

  domain::Project makeProject() {
    auto project = factory.createProject("Design shell");
    project.settings().characterDisplay = domain::CharacterDisplayMode::Off;
    trackId = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, trackId, "Phrase", time::Tick{0}, time::Tick{7680});
    auto [lyric, note] =
        factory.makeNote(time::Tick{960}, time::Tick{960}, 72U, U"\u3042", domain::Language::Japanese);
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    const auto frozen = voice_design::freezeVoiceRecipeResource(shellRecipe());
    if (!frozen) throw test::Failure{frozen.error().message};
    project.findVocalTrack(trackId)->proceduralRecipe = domain::ProceduralRecipeReference{
        .resource = frozen.value().identity, .path = "recipe.json", .style = "neutral"};
    return project;
  }

  const domain::Note& note() const {
    return session.project().findRegion(regionId)->notes.front();
  }

  // Centre of the note in shell window coordinates, from the shell-owned viewport.
  ui::Point noteCenter() const {
    const auto visuals = controller.pianoRoll().visibleNotes();
    if (visuals.empty()) throw test::Failure{"no visible note"};
    const auto b = visuals.front().bounds;
    return {b.x + std::min(8.0, b.width * 0.25), shell.layout().grid.y + b.y + b.height * 0.5};
  }

  // Paints one real frame so the shell records what the scene state allows (lane editability).
  bool frame() {
    if (!shell.prepareFrame(controller, 1600.0, 900.0)) return false;
    native_ui::PixelSurface surface{1600U, 900U};
    native_ui::RasterCanvas canvas{surface, 1.0, nullptr};
    return shell.paint(canvas, controller, controller.sceneState(), controller.playheadTick());
  }
};

PointerEvent press(ui::Point p) { return {.position = p, .button = PointerButton::Left}; }

KeyEvent toggleKey() {
  return {.key = NativeKey::Space, .modifiers = {.shift = true, .command = true}};
}

}  // namespace

TEST_CASE("the active shell owns the input geometry and returns it when a classic surface opens") {
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  const auto hosted = f.controller.hostedGrid();
  CHECK(hosted.has_value());
  CHECK_NEAR(hosted->pianoBottom,
             native_ui::EditorSceneLayout{}.contentTop() + f.shell.layout().grid.height, 1e-9);
  CHECK_NEAR(hosted->laneHeight, f.shell.layout().laneTimePlot.height, 1e-9);
  CHECK_NEAR(f.controller.pianoRoll().viewport().keyboardWidth, f.shell.layout().grid.x, 1e-9);

  // A classic surface opened by any path returns the classic geometry before any further input.
  f.controller.showVoicebankBrowser();
  CHECK(!f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  CHECK(!f.controller.hostedGrid().has_value());
  CHECK(!f.shell.presentedLastFrame());

  // The shell's own "Change voice" hands over synchronously, before any repaint.
  ShellFixture g;
  CHECK(g.shell.prepareFrame(g.controller, 1600.0, 900.0));
  const auto change = g.shell.layout().singerChange;
  CHECK(g.shell.pointerDown(g.controller, press({change.x + 4.0, change.y + 4.0})).hasValue());
  CHECK(!g.controller.hostedGrid().has_value());
  CHECK(!g.shell.presentedLastFrame());
}

TEST_CASE("a forwarded note drag moves the note, and Escape or a shell toggle abandons it") {
  {
    ShellFixture f;
    if (!native_ui::paint::vectorBackendAvailable()) return;
    CHECK(f.frame());
    const auto start = f.note().startTick;
    const auto p = f.noteCenter();
    CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
    CHECK(f.shell.pointerMove(f.controller, press({p.x + 120.0, p.y})).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press({p.x + 120.0, p.y})).hasValue());
    CHECK(f.note().startTick > start);  // positive control: forwarding really edits
  }
  {
    ShellFixture f;
    CHECK(f.frame());
    const auto revision = f.controller.documentRevision();
    const auto p = f.noteCenter();
    CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
    CHECK(f.shell.pointerMove(f.controller, press({p.x + 120.0, p.y})).hasValue());
    CHECK(f.controller.pointerGestureActive());
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
    CHECK(!f.controller.pointerGestureActive());
    CHECK(f.shell.pointerUp(f.controller, press({p.x + 120.0, p.y})).hasValue());
    CHECK(f.controller.documentRevision() == revision);
  }
  {
    ShellFixture f;
    CHECK(f.frame());
    const auto revision = f.controller.documentRevision();
    const auto p = f.noteCenter();
    CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
    CHECK(f.shell.pointerMove(f.controller, press({p.x + 120.0, p.y})).hasValue());
    // Switching to the classic editor mid-drag cancels the drag; the raw mouse-up then finds none.
    CHECK(f.shell.handleShellKey(f.controller, toggleKey()));
    CHECK(!f.shell.enabled());
    CHECK(!f.controller.hostedGrid().has_value());
    CHECK(f.shell.pointerUp(f.controller, press({p.x + 120.0, p.y})).hasValue());
    CHECK(!f.controller.pointerGestureActive());
    CHECK(f.controller.documentRevision() == revision);
  }
}

TEST_CASE("a knob gesture commits once, and never after Escape or a target change") {
  const auto knobDrag = [](ShellFixture& f, std::size_t index, auto&& middle) {
    const auto cell = f.shell.layout().knob[index];
    const ui::Point c{cell.x + cell.width * 0.5, cell.y + cell.height * 0.5};
    CHECK(f.shell.pointerDown(f.controller, press(c)).hasValue());
    CHECK(f.shell.pointerMove(f.controller, press({c.x, c.y - 48.0})).hasValue());
    middle();
    return f.shell.pointerUp(f.controller, press({c.x, c.y - 48.0}));
  };
  constexpr std::size_t kGender = 4U;
  CHECK(ui::expressionChannelAt(kGender) == ui::ExpressionChannel::Gender);
  {
    ShellFixture f;
    if (!native_ui::paint::vectorBackendAvailable()) return;
    CHECK(f.frame());
    const auto revision = f.controller.documentRevision();
    CHECK(knobDrag(f, kGender, [] {}).hasValue());
    CHECK(f.controller.documentRevision() != revision);  // positive control
    CHECK(f.session.project().findRegion(f.regionId)->genderAutomation.points().size() == 1U);
  }
  {
    ShellFixture f;
    CHECK(f.frame());
    const auto revision = f.controller.documentRevision();
    CHECK(knobDrag(f, kGender, [&] {
      CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
    }).hasValue());
    CHECK(f.controller.documentRevision() == revision);
  }
  {
    ShellFixture f;
    CHECK(f.frame());
    const auto revision = f.controller.documentRevision();
    // The playhead (the nudge target) moves during the gesture: the release must not write there.
    CHECK(knobDrag(f, kGender, [&] { f.controller.setPlayheadTick(time::Tick{1920}); }).hasValue());
    CHECK(f.controller.documentRevision() == revision);
  }
}

TEST_CASE("lyric input moves into shell space and classic-surface input keeps its coordinates") {
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  const auto offset = f.shell.layout().grid.y - native_ui::EditorSceneLayout{}.contentTop();
  const ui::Rect bounds{200.0, 300.0, 80.0, 20.0};
  const auto lyric = f.shell.translateTextInput({domain::LyricTokenId{7U}, bounds, U""});
  CHECK_NEAR(lyric.logicalBounds.y, bounds.y + offset, 1e-9);
  const auto external = f.shell.translateTextInput(
      {domain::LyricTokenId{std::numeric_limits<std::uint64_t>::max()}, bounds, U""});
  CHECK_NEAR(external.logicalBounds.y, bounds.y, 1e-9);
}

TEST_CASE("the hosted expression lane edits the curve it draws, and Escape abandons a drag") {
  {
    ShellFixture f;
    if (!native_ui::paint::vectorBackendAvailable()) return;
    CHECK(f.controller.openExpressionLane(ui::ExpressionChannel::Gender).hasValue());
    CHECK(f.frame());
    const auto plot = f.shell.layout().laneTimePlot;
    const auto& viewport = f.controller.pianoRoll().viewport();
    const auto x = viewport.bounds.x + viewport.keyboardWidth +
                   f.controller.pianoRoll().timeline().tickToPixel(time::Tick{1920});
    // Above the lane centre is above neutral for a bipolar channel.
    const ui::Point p{x, plot.y + plot.height * 0.25};
    CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press(p)).hasValue());
    const auto& points = f.session.project().findRegion(f.regionId)->genderAutomation.points();
    CHECK(points.size() == 1U);
    if (!points.empty()) CHECK(points.front().amount > 0.0F);
  }
  {
    ShellFixture f;
    CHECK(f.controller.openExpressionLane(ui::ExpressionChannel::Gender).hasValue());
    CHECK(f.frame());
    const auto plot = f.shell.layout().laneTimePlot;
    const ui::Point p{plot.x + plot.width * 0.5, plot.y + plot.height * 0.25};
    const auto revision = f.controller.documentRevision();
    CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
    CHECK(f.shell.pointerMove(f.controller, press({p.x + 30.0, p.y + 10.0})).hasValue());
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
    CHECK(f.shell.pointerUp(f.controller, press({p.x + 30.0, p.y + 10.0})).hasValue());
    CHECK(f.controller.documentRevision() == revision);
    CHECK(f.session.project().findRegion(f.regionId)->genderAutomation.points().empty());
    CHECK(f.controller.sceneState().expression.points.empty());
  }
}

TEST_CASE("a lane click is not forwarded while no expression channel is open") {
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  const auto plot = f.shell.layout().laneTimePlot;
  const auto revision = f.controller.documentRevision();
  const ui::Point p{plot.x + plot.width * 0.5, plot.y + plot.height * 0.25};
  CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(p)).hasValue());
  CHECK(f.controller.documentRevision() == revision);
}
