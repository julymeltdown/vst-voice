// Active SING shell input: gesture cancellation, surface/geometry ownership, IME placement and the
// hosted expression lane, driven through a real NativeEditorController and EditorSession.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/domain/project.hpp"
#include "seam/native_ui/design/sing_layout.hpp"
#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/native_ui/design/shell_evidence.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_semantics.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/ui/expression_lane.hpp"
#include "seam/voice_design/recipe_resource.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>

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
  std::optional<native_ui::TextInputRequest> lastTextInput;

  explicit ShellFixture(time::Tick regionStart = time::Tick{0}, bool wide = false)
      : session(makeProject(regionStart, wide)),
        controller{session, factory, regionId,
                   native_ui::EditorHostCallbacks{
                       .beginTextInput =
                           [this](const native_ui::TextInputRequest& request) {
                             lastTextInput = shell.translateTextInput(request);
                           },
                       .endTextInput = [this] { shell.textInputEnded(); },
                   }} {
    controller.resize(1600.0, 900.0);
    shell.activate({}, DesignPreferences{.mode = DesignMode::Emo});
  }

  // A wide project adds notes far to the right, far above and below, and a dense overlap, so
  // scrolling moves notes across every grid edge.
  domain::Project makeProject(time::Tick regionStart, bool wide) {
    auto project = factory.createProject("Design shell");
    project.settings().characterDisplay = domain::CharacterDisplayMode::Off;
    trackId = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, trackId, "Phrase", regionStart,
                                 wide ? time::Tick{96000} : time::Tick{7680});
    auto [lyric, note] =
        factory.makeNote(time::Tick{960}, time::Tick{960}, 72U, U"\u3042", domain::Language::Japanese);
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    if (wide) {
      const auto addNote = [&](std::int64_t start, std::uint8_t key) {
        auto [extraLyric, extraNote] = factory.makeNote(time::Tick{start}, time::Tick{960}, key,
                                                        U"\u3044", domain::Language::Japanese);
        region->lyrics.push_back(std::move(extraLyric));
        region->notes.push_back(std::move(extraNote));
      };
      addNote(48000, 72U);
      addNote(960, 30U);
      addNote(960, 110U);
      addNote(4800, 57U);  // the bottom row at 1600x890, which that height shows only in part
      for (int i = 0; i < 3; ++i) addNote(2880, 74U);
    }
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

TEST_CASE("text fields move into shell space only when anchored to the note grid") {
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  const auto offset = f.shell.layout().grid.y - native_ui::EditorSceneLayout{}.contentTop();
  const ui::Rect bounds{200.0, 300.0, 80.0, 20.0};
  const auto grid = f.shell.translateTextInput(
      {domain::LyricTokenId{7U}, bounds, U"", native_ui::TextInputAnchor::NoteGrid});
  CHECK_NEAR(grid.logicalBounds.y, bounds.y + offset, 1e-9);
  // The anchor, not the target id, decides: a classic panel keeps classic coordinates.
  const auto classic = f.shell.translateTextInput(
      {domain::LyricTokenId{7U}, bounds, U"", native_ui::TextInputAnchor::ClassicSurface});
  CHECK_NEAR(classic.logicalBounds.y, bounds.y, 1e-9);
}

TEST_CASE("batch lyric input anchors on the selected note in the shell grid") {
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  const auto p = f.noteCenter();
  CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(p)).hasValue());
  // Shift-L distributes lyrics over the selected notes.
  CHECK(f.controller.keyDown(KeyEvent{.key = NativeKey::L, .modifiers = {.shift = true}}).hasValue());
  CHECK(f.lastTextInput.has_value());
  if (!f.lastTextInput) return;
  // The batch field uses the external target id but sits over the note, in shell space.
  CHECK(f.lastTextInput->anchor == native_ui::TextInputAnchor::NoteGrid);
  const auto grid = f.shell.layout().grid;
  CHECK(f.lastTextInput->logicalBounds.y >= grid.y - 1.0);
  CHECK(f.lastTextInput->logicalBounds.y < grid.bottom());
  CHECK(std::abs(f.lastTextInput->logicalBounds.y - p.y) < 40.0);
  // The shell keeps presenting, and a resize cancels the open field instead of misplacing it.
  CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  CHECK(f.controller.sceneState().lyricEditor.has_value());
  CHECK(f.shell.prepareFrame(f.controller, 1500.0, 900.0));
  CHECK(!f.controller.sceneState().lyricEditor.has_value());
}

TEST_CASE("a track rename field hands the frame to the classic arrangement surface") {
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  CHECK(f.controller.selectTrack(f.trackId).hasValue());
  CHECK(f.controller.beginSelectedTrackRename().hasValue());
  CHECK(f.controller.legacyModalSurfaceActive());
  CHECK(!f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  CHECK(!f.controller.hostedGrid().has_value());
  CHECK(f.lastTextInput.has_value());
  if (f.lastTextInput) CHECK(f.lastTextInput->anchor == native_ui::TextInputAnchor::ClassicSurface);
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
    // A knob step after the abandoned drag is shown, and the next lane drag commits on release.
    CHECK(f.shell.dispatchSemantic(f.controller, "shell.knob.gender",
                                   native_ui::SemanticAction::Increment)
              .hasValue());
    CHECK(f.session.project().findRegion(f.regionId)->genderAutomation.points().size() == 1U);
    CHECK(f.controller.sceneState().expression.points.size() == 1U);
    CHECK(!f.controller.sceneState().expression.draftOpen);
    CHECK(f.frame());
    CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
    CHECK(f.shell.pointerMove(f.controller, press({p.x + 30.0, p.y + 10.0})).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press({p.x + 30.0, p.y + 10.0})).hasValue());
    CHECK(f.session.project().findRegion(f.regionId)->genderAutomation.points().size() == 2U);
    CHECK(f.controller.sceneState().expression.points.size() == 2U);
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

TEST_CASE("a region placed later in the song keeps notes, lane points and playhead edits aligned") {
  constexpr time::Tick kRegionStart{3840};
  {
    // A lane click under the note's start stores the note's region-local tick.
    ShellFixture f{kRegionStart};
    if (!native_ui::paint::vectorBackendAvailable()) return;
    CHECK(f.controller.openExpressionLane(ui::ExpressionChannel::Gender).hasValue());
    CHECK(f.frame());
    const auto visuals = f.controller.pianoRoll().visibleNotes();
    CHECK(!visuals.empty());
    if (visuals.empty()) return;
    const auto noteX = visuals.front().bounds.x;
    const auto plot = f.shell.layout().laneTimePlot;
    const ui::Point p{noteX, plot.y + plot.height * 0.25};
    CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press(p)).hasValue());
    const auto& points = f.session.project().findRegion(f.regionId)->genderAutomation.points();
    CHECK(points.size() == 1U);
    if (!points.empty()) CHECK(points.front().tick == f.note().startTick);
    // The scene publishes the region origin that painters add to region-local points.
    CHECK(f.controller.sceneState().automationOriginTick == kRegionStart);
  }
  {
    // A knob nudge writes at the playhead's position inside the region.
    ShellFixture f{kRegionStart};
    f.controller.setPlayheadTick(kRegionStart + time::Tick{1920});
    // The painted knobs reflect the playhead: paint after moving it.
    CHECK(f.frame());
    const auto cell = f.shell.layout().knob[4U];
    const ui::Point c{cell.x + cell.width * 0.5, cell.y + cell.height * 0.5};
    CHECK(f.shell.pointerDown(f.controller, press(c)).hasValue());
    CHECK(f.shell.pointerMove(f.controller, press({c.x, c.y - 48.0})).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press({c.x, c.y - 48.0})).hasValue());
    const auto& points = f.session.project().findRegion(f.regionId)->genderAutomation.points();
    CHECK(points.size() == 1U);
    if (!points.empty()) CHECK(points.front().tick == time::Tick{1920});
  }
  {
    // "At the playhead" edits need the playhead inside the region, both ends included. Outside
    // it, the edit is refused with the document and undo history untouched; it is never moved to
    // the region edge.
    const time::Tick kRegionEnd{kRegionStart.value() + 7680};
    const auto attempt = [&](time::Tick playhead, std::optional<time::Tick> expected) {
      ShellFixture f{kRegionStart};
      f.controller.setPlayheadTick(playhead);
      const auto revision = f.controller.documentRevision();
      const auto undoable = f.session.canUndo();
      const auto result = f.controller.nudgeGender(3);
      const auto& points = f.session.project().findRegion(f.regionId)->genderAutomation.points();
      CHECK(f.controller.sceneState().playheadInsideRegion == expected.has_value());
      if (!expected) {
        CHECK(!result.hasValue());
        CHECK(f.controller.documentRevision() == revision);
        CHECK(f.session.canUndo() == undoable);
        CHECK(points.empty());
        return;
      }
      CHECK(result.hasValue());
      CHECK(points.size() == 1U);
      if (!points.empty()) CHECK(points.front().tick == *expected);
    };
    attempt(time::Tick{960}, std::nullopt);
    attempt(time::Tick{kRegionStart.value() - 1}, std::nullopt);
    attempt(kRegionStart, time::Tick{0});
    attempt(kRegionEnd, time::Tick{7680});
    attempt(time::Tick{kRegionEnd.value() + 1}, std::nullopt);
  }
  {
    // The knobs read the edge value but show the refusal, and assistive input is refused too.
    ShellFixture f{kRegionStart};
    if (!native_ui::paint::vectorBackendAvailable()) return;
    f.controller.setPlayheadTick(time::Tick{960});
    CHECK(f.frame());
    const auto revision = f.controller.documentRevision();
    CHECK(!f.shell.dispatchSemantic(f.controller, "shell.knob.gender",
                                    native_ui::SemanticAction::Increment).hasValue());
    CHECK(f.controller.documentRevision() == revision);
    const auto cell = f.shell.layout().knob[4U];
    const ui::Point c{cell.x + cell.width * 0.5, cell.y + cell.height * 0.5};
    CHECK(f.shell.pointerDown(f.controller, press(c)).hasValue());
    CHECK(f.shell.pointerMove(f.controller, press({c.x, c.y - 48.0})).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press({c.x, c.y - 48.0})).hasValue());
    CHECK(f.controller.documentRevision() == revision);
  }
}

TEST_CASE("notes panned off horizontally are pointed at earlier or later, never above") {
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  CHECK(!f.shell.lastOffscreenHint().has_value());  // the note is visible
  const auto grid = f.shell.layout().grid;
  const ui::Point anchor{grid.x + grid.width * 0.5, grid.y + grid.height * 0.5};
  // Pan the timeline horizontally until the note leaves the visible time range.
  for (int i = 0; i < 200 && !f.controller.pianoRoll().visibleNotes().empty(); ++i) {
    CHECK(f.shell.scroll(f.controller, 400.0, 0.0, anchor, {}));
    CHECK(f.frame());
  }
  CHECK(f.controller.pianoRoll().visibleNotes().empty());
  CHECK(f.frame());
  const auto hint = f.shell.lastOffscreenHint();
  CHECK(hint.has_value());
  if (hint) CHECK(*hint == 2U || *hint == 3U);
}

TEST_CASE("the shell accessibility tree exposes its controls with real roles in shell geometry") {
  using native_ui::SemanticAction;
  using native_ui::SemanticNode;
  using native_ui::SemanticRole;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  const auto& root = f.shell.accessibilityTree().root();
  const auto find = [&](std::string_view id) -> const SemanticNode* {
    for (const auto& child : root.children)
      if (child.id == id) return &child;
    return nullptr;
  };
  const auto& l = f.shell.layout();
  const ui::Rect client{0.0, 0.0, l.width, l.height};
  for (const auto& child : root.children) {
    CHECK(child.bounds.x >= client.x - 0.5 && child.bounds.right() <= client.right() + 0.5);
    CHECK(child.bounds.y >= client.y - 0.5 && child.bounds.bottom() <= client.bottom() + 0.5);
  }
  // Knobs are sliders in display units with a range, step and increment/decrement.
  const auto* gender = find("shell.knob.gender");
  CHECK(gender != nullptr);
  if (gender) {
    CHECK(gender->role == SemanticRole::Slider);
    CHECK(gender->numericMinimum.has_value() && *gender->numericMinimum < 0.0);
    CHECK(gender->numericMaximum.has_value() && *gender->numericMaximum > 0.0);
    CHECK(std::find(gender->actions.begin(), gender->actions.end(), SemanticAction::Increment) !=
          gender->actions.end());
    CHECK(gender->bounds.x == l.knob[4U].x && gender->bounds.y == l.knob[4U].y);
  }
  // EMO / SCENE are a radio pair reflecting the current look; workspaces and lanes are tabs.
  const auto* emo = find("shell.mode.emo");
  const auto* scene = find("shell.mode.scene");
  CHECK(emo && scene && emo->role == SemanticRole::RadioButton && emo->selected && !scene->selected);
  const auto* sing = find("shell.workspace.sing");
  CHECK(sing && sing->role == SemanticRole::Tab && sing->selected);
  const auto* tune = find("shell.workspace.tune");
  CHECK(tune && tune->enabled && !tune->selected);
  CHECK(find("shell.lane-tab.gender") != nullptr);
  // Controller controls nested in the classic toolbar group are re-homed onto the shell's own
  // header and singer card, keeping their ids, roles and actions.
  const auto* transport = find("toolbar.transport");
  const auto* tempo = find("toolbar.tempo");
  const auto* meter = find("toolbar.meter");
  const auto* identity = find("voice.identity");
  CHECK(transport && transport->role == SemanticRole::Button &&
        transport->bounds.x == l.playButton.x && transport->bounds.y == l.playButton.y);
  CHECK(tempo && tempo->role == SemanticRole::TextField && tempo->bounds.x == l.tempoReadout.x);
  CHECK(tempo && std::find(tempo->actions.begin(), tempo->actions.end(), SemanticAction::EditText) !=
                     tempo->actions.end());
  CHECK(meter && meter->bounds.x == l.meterReadout.x && meter->bounds.y == l.meterReadout.y);
  CHECK(identity && identity->role == SemanticRole::Status && identity->bounds.x == l.singer.x);
  // Notes stay virtualized: they keep controller ids and sit exactly on their painted rectangles.
  const auto& tree = f.shell.accessibilityTree();
  CHECK(tree.virtualizedNoteCount() == 1U);
  CHECK(tree.materializedNoteCount() == 0U);
  const auto visuals = f.controller.pianoRoll().visibleNotes();
  CHECK(!visuals.empty());
  if (!visuals.empty()) {
    const auto notes = tree.materializeNotes(0U, 8U);
    CHECK(notes.size() == 1U);
    if (!notes.empty()) {
      const auto* note = &notes.front();
      CHECK(note->id == "note." + visuals.front().noteId.toString());
      CHECK_NEAR(note->bounds.y, visuals.front().bounds.y + l.grid.y, 1e-9);
      CHECK(note->bounds.y >= l.grid.y && note->bounds.bottom() <= l.grid.bottom());
    }
  }
  // Classic-only chrome without a visual in the shell is not exposed.
  CHECK(find("toolbar.loop") == nullptr);
  CHECK(find("arrangement.panel") == nullptr);
}

TEST_CASE("shell accessibility actions edit through the same commands as pointer input") {
  using native_ui::SemanticAction;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  const auto revision = f.controller.documentRevision();
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.knob.gender", SemanticAction::Increment).hasValue());
  CHECK(f.controller.documentRevision() != revision);
  CHECK(f.session.project().findRegion(f.regionId)->genderAutomation.points().size() == 1U);
  CHECK(f.session.undo());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.mode.scene", SemanticAction::Activate).hasValue());
  CHECK(f.shell.mode() == DesignMode::Scene);
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.lane-tab.breath", SemanticAction::Activate).hasValue());
  CHECK(f.controller.sceneState().expressionLabelVisible());
  CHECK(!f.shell.dispatchSemantic(f.controller, "note.1", SemanticAction::Activate).hasValue());
  // Change voice opens a classic surface and hands the frame over at once.
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.change-voice", SemanticAction::Activate).hasValue());
  CHECK(!f.shell.presentedLastFrame());
  CHECK(!f.controller.hostedGrid().has_value());
}

TEST_CASE("shell note semantics are clipped to the grid at every edge and stay virtualized") {
  using native_ui::SemanticNode;
  ShellFixture f{time::Tick{0}, true};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  auto grid = f.shell.layout().grid;
  const ui::Point anchor{grid.x + grid.width * 0.5, grid.y + grid.height * 0.5};
  // left, right, top, bottom partial clips, plus notes fully outside the grid.
  std::array<bool, 4U> clipped{};
  bool sawOutside = false;
  std::size_t checks = 0U;
  const auto verify = [&] {
    f.controller.rebuildAccessibilityTree();
    f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
    const auto& tree = f.shell.accessibilityTree();
    const auto& source = f.controller.accessibilityTree();
    CHECK(tree.virtualizedNoteCount() == source.virtualizedNoteCount());
    CHECK(tree.materializedNoteCount() == 0U);
    const auto shellNotes = tree.materializeNotes(0U, 64U);
    const auto legacyNotes = source.materializeNotes(0U, 64U);
    CHECK(shellNotes.size() == legacyNotes.size());
    // The oracle is the painted layout (overlap bands included) for visible notes, and the logical
    // rectangle for the rest.
    std::map<std::string, ui::Rect> painted;
    for (const auto& visual : f.controller.pianoRoll().visibleNotes()) {
      auto bounds = visual.bounds;
      bounds.y += grid.y;
      painted["note." + visual.noteId.toString()] = bounds;
    }
    for (std::size_t i = 0U; i < std::min(shellNotes.size(), legacyNotes.size()); ++i) {
      ++checks;
      const auto& note = shellNotes[i];
      const auto paintedNote = painted.find(legacyNotes[i].id);
      const auto full = paintedNote != painted.end() ? paintedNote->second
                                                     : f.shell.fromLegacy(legacyNotes[i].bounds);
      CHECK(note.id == legacyNotes[i].id);
      const auto left = std::max(full.x, grid.x);
      const auto right = std::min(full.right(), grid.right());
      const auto top = std::max(full.y, grid.y);
      const auto bottom = std::min(full.bottom(), grid.bottom());
      if (right > left && bottom > top) {
        // Exactly the visible part of the painted note; never a hit rectangle outside the grid.
        CHECK_NEAR(note.bounds.x, left, 1e-9);
        CHECK_NEAR(note.bounds.right(), right, 1e-9);
        CHECK_NEAR(note.bounds.y, top, 1e-9);
        CHECK_NEAR(note.bounds.bottom(), bottom, 1e-9);
        if (full.x < grid.x) clipped[0] = true;
        if (full.right() > grid.right()) clipped[1] = true;
        // Vertical scrolling moves whole pitch rows (PitchTransform keeps an integer top key), so
        // the grid's top edge is always a row boundary and no note can straddle it.
        CHECK(full.y >= grid.y - 1e-9);
        if (full.y < grid.y) clipped[2] = true;
        if (full.bottom() > grid.bottom()) clipped[3] = true;
      } else {
        sawOutside = true;
        CHECK(note.bounds.width == 0.0 && note.bounds.height == 0.0);
        CHECK(note.bounds.x >= grid.x && note.bounds.x <= grid.right());
        CHECK(note.bounds.y >= grid.y && note.bounds.y <= grid.bottom());
        CHECK(note.description.find("Outside the visible grid") != std::string::npos);
      }
    }
  };
  verify();
  // Sweep the timeline both ways and the pitch axis both ways without repainting.
  for (int i = 0; i < 400 && !(clipped[0] && clipped[1]); ++i) {
    CHECK(f.shell.scroll(f.controller, 60.0, 0.0, anchor, {}));
    verify();
  }
  for (int i = 0; i < 800 && !(clipped[0] && clipped[1]); ++i) {
    CHECK(f.shell.scroll(f.controller, -60.0, 0.0, anchor, {}));
    verify();
  }
  // Pitch rows are whole (18 pt) and the canonical grid is exactly 28 rows, so a vertical partial
  // note needs a height that is not a whole number of rows: 1600x890 cuts the bottom row.
  CHECK(f.shell.prepareFrame(f.controller, 1600.0, 890.0));
  grid = f.shell.layout().grid;
  CHECK(std::fmod(grid.height, f.controller.pianoRoll().pitch().rowHeight()) > 0.0);
  for (int i = 0; i < 800 && !clipped[3]; ++i) {
    CHECK(f.shell.scroll(f.controller, -60.0, 0.0, anchor, {}));
    verify();
  }
  for (int i = 0; i < 800 && !clipped[3]; ++i) {
    CHECK(f.shell.scroll(f.controller, 60.0, 0.0, anchor, {}));
    verify();
  }
  CHECK(checks > 0U);
  CHECK(sawOutside);
  CHECK(clipped[0]);
  CHECK(clipped[1]);
  CHECK(!clipped[2]);
  CHECK(clipped[3]);
}

TEST_CASE("overlap groups and their detail rows move into shell space with every child") {
  using native_ui::SemanticAction;
  using native_ui::SemanticNode;
  ShellFixture f{time::Tick{0}, true};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  f.controller.rebuildAccessibilityTree();
  std::string groupId;
  for (const auto& child : f.controller.accessibilityTree().root().children)
    if (child.id.starts_with("overlap-group.")) groupId = child.id;
  CHECK(!groupId.empty());
  if (groupId.empty()) return;
  const auto opened = f.controller.dispatchAccessibility(groupId, SemanticAction::Activate);
  CHECK(opened.hasValue());
  CHECK(f.frame());
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  const auto grid = f.shell.layout().grid;
  bool sawGroup = false;
  bool sawDetail = false;
  for (const auto& child : f.shell.accessibilityTree().root().children) {
    if (child.id.starts_with("overlap-group.")) {
      sawGroup = true;
      CHECK(child.bounds.x >= grid.x - 1e-9 && child.bounds.right() <= grid.right() + 1e-9);
      CHECK(child.bounds.y >= grid.y - 1e-9 && child.bounds.bottom() <= grid.bottom() + 1e-9);
    }
    if (child.id.starts_with("detail.overlap-group.")) {
      sawDetail = true;
      CHECK(!child.children.empty());
      // Rows are transformed with the popover, so each lies inside it.
      for (const auto& row : child.children) {
        CHECK(row.bounds.y >= child.bounds.y - 1e-9);
        CHECK(row.bounds.bottom() <= child.bounds.bottom() + 1e-9);
      }
    }
  }
  CHECK(sawGroup);
  CHECK(sawDetail);
}

TEST_CASE("keyboard focus has one owner: Tab walks the shell tree and focused controls own plain keys") {
  using native_ui::SemanticAction;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  const auto focusedId = [&]() -> std::string {
    f.controller.rebuildAccessibilityTree();
    f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
    const auto* node = f.shell.accessibilityTree().focusedNode();
    return node == nullptr ? std::string{} : node->id;
  };
  // Tab starts at the first shell control, Shift-Tab from there reaches the last note, which the
  // controller then owns (the delegated note is focusable through the shell tree).
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Tab}));
  CHECK(focusedId() == "shell.workspace.sing");
  CHECK(f.shell.handleShellKey(f.controller,
                               KeyEvent{.key = NativeKey::Tab, .modifiers = {.shift = true}}));
  const auto noteId = "note." + f.note().id.toString();
  CHECK(focusedId() == noteId);
  const auto* controllerFocus = f.controller.accessibilityTree().focusedNode();
  CHECK(controllerFocus != nullptr && controllerFocus->id == noteId);
  // With the note (controller) focused, plain keys are not the shell's.
  CHECK(!f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Up}));

  // A focused knob owns plain keys: Delete does not delete the selected note, arrows adjust it.
  const auto p = f.noteCenter();
  CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(p)).hasValue());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.knob.gender", SemanticAction::SetFocus).hasValue());
  CHECK(focusedId() == "shell.knob.gender");
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Delete}));
  CHECK(f.session.project().findRegion(f.regionId)->notes.size() == 1U);
  const auto revision = f.controller.documentRevision();
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Up}));
  CHECK(f.controller.documentRevision() != revision);
  CHECK(f.session.project().findRegion(f.regionId)->genderAutomation.points().size() == 1U);
  // Command shortcuts still reach the editor.
  CHECK(!f.shell.handleShellKey(f.controller,
                                KeyEvent{.key = NativeKey::Z, .modifiers = {.command = true}}));
  // A failed action does not move focus.
  CHECK(!f.shell.dispatchSemantic(f.controller, "shell.mode.emo", SemanticAction::Increment).hasValue());
  CHECK(focusedId() == "shell.knob.gender");
  // Enter activates the focused control: the knob opens its curve in the lane.
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Enter}));
  CHECK(f.controller.sceneState().expressionLabelVisible());
  // A press in the grid hands focus back to the editor, even onto the same note as before.
  CHECK(f.frame());
  CHECK(f.shell.pointerDown(f.controller, press(f.noteCenter())).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(f.noteCenter())).hasValue());
  CHECK(focusedId() != "shell.knob.gender");
  CHECK(!f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Up}));
}

TEST_CASE("shell accessibility actions are validated against the current layout and state") {
  using native_ui::SemanticAction;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  const auto revision = f.controller.documentRevision();
  CHECK(!f.shell.dispatchSemantic(f.controller, "shell.knob.nonexistent", SemanticAction::Increment).hasValue());
  CHECK(!f.shell.dispatchSemantic(f.controller, "shell.mode.emo", SemanticAction::Increment).hasValue());
  // EXPORT's run button is not published while SING shows, so it cannot be activated from here.
  CHECK(!f.shell.dispatchSemantic(f.controller, "shell.export.run", SemanticAction::Activate).hasValue());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.workspace.tune", SemanticAction::SetFocus).hasValue());
  // After the rack collapses to a rail, a retained knob element no longer exists.
  CHECK(f.shell.prepareFrame(f.controller, 1000.0, 700.0));
  CHECK(f.shell.layout().rack == native_ui::design::RackPresentation::Rail);
  CHECK(!f.shell.dispatchSemantic(f.controller, "shell.knob.gender", SemanticAction::Increment).hasValue());
  CHECK(f.controller.documentRevision() == revision);
  CHECK(!f.session.canUndo());
  // While a classic surface is up, shell elements do nothing.
  CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  f.controller.showAudioSettings();
  CHECK(!f.shell.dispatchSemantic(f.controller, "shell.mode.scene", SemanticAction::Activate).hasValue());
  CHECK(f.shell.mode() == DesignMode::Emo);
}

TEST_CASE("a rename started over an open lyric field keeps the rename and edits the right target") {
  {
    // Ordinary lyric -> track rename -> commit renames the track, not the lyric.
    ShellFixture f;
    if (!native_ui::paint::vectorBackendAvailable()) return;
    CHECK(f.frame());
    CHECK(f.controller.selectTrack(f.trackId).hasValue());
    CHECK(f.frame());
    CHECK(f.controller.beginLyricEdit(f.note().id).hasValue());
    CHECK(f.lastTextInput && f.lastTextInput->anchor == native_ui::TextInputAnchor::NoteGrid);
    CHECK(f.controller.beginSelectedTrackRename().hasValue());
    CHECK(f.lastTextInput && f.lastTextInput->anchor == native_ui::TextInputAnchor::ClassicSurface);
    CHECK(!f.shell.prepareFrame(f.controller, 1600.0, 900.0));
    CHECK(f.controller.textInputActive());
    CHECK(f.controller.commitTextComposition(U"Lead").hasValue());
    CHECK(f.session.project().findVocalTrack(f.trackId)->name == "Lead");
    const auto* region = f.session.project().findRegion(f.regionId);
    const auto* lyric = region->findLyric(f.note().lyricTokenId);
    CHECK(lyric != nullptr && lyric->surface == U"\u3042");
  }
  {
    // Batch lyric -> region rename -> cancel leaves both the region name and the lyrics alone.
    ShellFixture f;
    if (!native_ui::paint::vectorBackendAvailable()) return;
    CHECK(f.frame());
    const auto p = f.noteCenter();
    CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press(p)).hasValue());
    CHECK(f.controller.keyDown(KeyEvent{.key = NativeKey::L, .modifiers = {.shift = true}}).hasValue());
    CHECK(f.lastTextInput && f.lastTextInput->anchor == native_ui::TextInputAnchor::NoteGrid);
    CHECK(f.controller.beginSelectedRegionRename().hasValue());
    CHECK(!f.shell.prepareFrame(f.controller, 1600.0, 900.0));
    CHECK(f.controller.textInputActive());
    f.controller.cancelTextComposition();
    CHECK(!f.controller.textInputActive());
    CHECK(f.session.project().findRegion(f.regionId)->name == "Phrase");
  }
  {
    // Batch lyric -> region rename -> commit renames the region.
    ShellFixture f;
    if (!native_ui::paint::vectorBackendAvailable()) return;
    CHECK(f.frame());
    const auto p = f.noteCenter();
    CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press(p)).hasValue());
    CHECK(f.controller.keyDown(KeyEvent{.key = NativeKey::L, .modifiers = {.shift = true}}).hasValue());
    CHECK(f.controller.beginSelectedRegionRename().hasValue());
    CHECK(!f.shell.prepareFrame(f.controller, 1600.0, 900.0));
    CHECK(f.controller.commitTextComposition(U"Verse").hasValue());
    CHECK(f.session.project().findRegion(f.regionId)->name == "Verse");
  }
  {
    // Ordinary lyric -> region rename keeps the rename field open through the next frame.
    ShellFixture f;
    if (!native_ui::paint::vectorBackendAvailable()) return;
    CHECK(f.frame());
    CHECK(f.controller.beginLyricEdit(f.note().id).hasValue());
    CHECK(f.controller.beginSelectedRegionRename().hasValue());
    CHECK(!f.shell.prepareFrame(f.controller, 1600.0, 900.0));
    CHECK(f.controller.textInputActive());
  }
}

TEST_CASE("a shell control that leaves the layout gives up focus and the keys") {
  using native_ui::SemanticAction;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.knob.gender", SemanticAction::SetFocus).hasValue());
  CHECK(f.shell.prepareFrame(f.controller, 1000.0, 700.0));
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  const auto* focused = f.shell.accessibilityTree().focusedNode();
  CHECK(focused == nullptr || focused->id != "shell.knob.gender");
  // No phantom owner: Space reaches the editor (transport) instead of a removed knob.
  CHECK(!f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Space}));
  CHECK(!f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Up}));
}

TEST_CASE("re-homed editor controls own Enter and plain keys, notes keep theirs") {
  using native_ui::SemanticAction;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  CHECK(f.shell.pointerDown(f.controller, press(f.noteCenter())).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(f.noteCenter())).hasValue());
  const auto tabTo = [&](std::string_view target) {
    CHECK(f.shell.dispatchSemantic(f.controller, "shell.mode.scene", SemanticAction::SetFocus).hasValue());
    for (int i = 0; i < 60; ++i) {
      CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Tab}));
      const auto* node = f.shell.accessibilityTree().focusedNode();
      if (node != nullptr && node->id == target) return true;
    }
    return false;
  };
  // Tempo: Enter opens the tempo field on its classic surface, never the selected note's lyric.
  CHECK(tabTo("toolbar.tempo"));
  f.lastTextInput.reset();
  const KeyEvent enter{.key = NativeKey::Enter};
  if (!f.shell.handleShellKey(f.controller, enter)) CHECK(f.controller.keyDown(enter).hasValue());
  CHECK(f.lastTextInput.has_value());
  if (f.lastTextInput) CHECK(f.lastTextInput->anchor != native_ui::TextInputAnchor::NoteGrid);
  CHECK(f.controller.textInputActive());
  f.controller.cancelTextComposition();
  CHECK(f.frame());
  // Meter likewise.
  CHECK(tabTo("toolbar.meter"));
  f.lastTextInput.reset();
  CHECK(f.shell.handleShellKey(f.controller, enter));
  CHECK(f.lastTextInput.has_value());
  if (f.lastTextInput) CHECK(f.lastTextInput->anchor != native_ui::TextInputAnchor::NoteGrid);
  f.controller.cancelTextComposition();
  CHECK(f.frame());
  // Destructive score shortcuts do not reach the selected note while a control is focused.
  CHECK(tabTo("toolbar.transport"));
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Delete}));
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Up}));
  CHECK(f.session.project().findRegion(f.regionId)->notes.size() == 1U);
  CHECK(static_cast<int>(f.note().midiKey) == 72);
  // Once the note owns focus again, its keys are the score editor's.
  CHECK(f.shell.pointerDown(f.controller, press(f.noteCenter())).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(f.noteCenter())).hasValue());
  CHECK(f.controller.dispatchAccessibility("note." + f.note().id.toString(), SemanticAction::SetFocus).hasValue());
  const KeyEvent up{.key = NativeKey::Up};
  if (!f.shell.handleShellKey(f.controller, up)) CHECK(f.controller.keyDown(up).hasValue());
  CHECK(static_cast<int>(f.note().midiKey) == 73);
}

TEST_CASE("delegated note semantics use the painted overlap layout") {
  ShellFixture f{time::Tick{0}, true};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  // A fourth simultaneous note makes the dense layout hide a member.
  auto [lyric, note] = f.factory.makeNote(time::Tick{2880}, time::Tick{960}, 74U, U"x",
                                          domain::Language::Japanese);
  auto* region = f.session.project().findRegion(f.regionId);
  region->lyrics.push_back(std::move(lyric));
  region->notes.push_back(std::move(note));
  f.controller.pianoRoll().rebuildIndex();
  CHECK(f.frame());
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  const auto ax = f.shell.accessibilityTree().materializeNotes(0U, 64U);
  const auto grid = f.shell.layout().grid;
  bool sawBand = false;
  bool sawHidden = false;
  for (const auto& visual : f.controller.pianoRoll().visibleNotes()) {
    const auto id = "note." + visual.noteId.toString();
    const auto node = std::find_if(ax.begin(), ax.end(), [&](const auto& n) { return n.id == id; });
    CHECK(node != ax.end());
    if (node == ax.end()) continue;
    if (visual.hiddenByOverlapDensity) {
      sawHidden = true;
      CHECK(node->description.find("dense overlap group") != std::string::npos);
      continue;
    }
    if (visual.overlapMemberCount > 1U) sawBand = true;
    CHECK_NEAR(node->bounds.x, std::max(visual.bounds.x, grid.x), 1e-9);
    CHECK_NEAR(node->bounds.y, visual.bounds.y + grid.y, 1e-9);
    CHECK_NEAR(node->bounds.height, visual.bounds.height, 1e-9);
  }
  CHECK(sawBand);
  CHECK(sawHidden);
}

TEST_CASE("leaving a vibrato handle through shell Tab returns arrows to the note") {
  using native_ui::SemanticAction;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.session.project().findRegion(f.regionId)->notes.front().vibrato.enabled = true;
  CHECK(f.frame());
  CHECK(f.shell.pointerDown(f.controller, press(f.noteCenter())).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(f.noteCenter())).hasValue());
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  std::string handle;
  for (const auto& child : f.shell.accessibilityTree().root().children)
    if (child.id.starts_with("editor.vibrato.handle.")) handle = child.id;
  CHECK(!handle.empty());
  if (handle.empty()) return;
  // While the handle owns focus, arrows adjust the vibrato.
  CHECK(f.controller.dispatchAccessibility(handle, SemanticAction::SetFocus).hasValue());
  CHECK(f.controller.sceneState().vibratoKeyboardFocus.has_value());
  for (const auto shift : {false, true}) {
    CHECK(f.controller.dispatchAccessibility(handle, SemanticAction::SetFocus).hasValue());
    bool reachedNote = false;
    for (int i = 0; i < 80; ++i) {
      CHECK(f.shell.handleShellKey(f.controller,
                                   KeyEvent{.key = NativeKey::Tab, .modifiers = {.shift = shift}}));
      const auto* node = f.shell.accessibilityTree().focusedNode();
      if (node != nullptr && node->id.starts_with("note.")) {
        reachedNote = true;
        break;
      }
    }
    CHECK(reachedNote);
    CHECK(!f.controller.sceneState().vibratoKeyboardFocus.has_value());
  }
  const auto beforeKey = static_cast<int>(f.note().midiKey);
  const auto beforeOnset = f.note().vibrato.startFraction;
  const KeyEvent up{.key = NativeKey::Up};
  if (!f.shell.handleShellKey(f.controller, up)) CHECK(f.controller.keyDown(up).hasValue());
  CHECK(static_cast<int>(f.note().midiKey) == beforeKey + 1);
  CHECK(f.note().vibrato.startFraction == beforeOnset);
  // Handle -> shell control also ends the subfocus.
  CHECK(f.controller.dispatchAccessibility(handle, SemanticAction::SetFocus).hasValue());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.knob.gender", SemanticAction::SetFocus).hasValue());
  CHECK(!f.controller.sceneState().vibratoKeyboardFocus.has_value());
}

TEST_CASE("releasing shell focus returns to the note, not to a stale vibrato handle") {
  using native_ui::SemanticAction;
  for (const auto resize : {false, true}) {
    ShellFixture f;
    if (!native_ui::paint::vectorBackendAvailable()) return;
    f.session.project().findRegion(f.regionId)->notes.front().vibrato.enabled = true;
    CHECK(f.frame());
    CHECK(f.shell.pointerDown(f.controller, press(f.noteCenter())).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press(f.noteCenter())).hasValue());
    f.controller.rebuildAccessibilityTree();
    f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
    std::string handle;
    for (const auto& child : f.shell.accessibilityTree().root().children)
      if (child.name == "Vibrato onset") handle = child.id;
    CHECK(!handle.empty());
    if (handle.empty()) return;
    CHECK(f.controller.dispatchAccessibility(handle, SemanticAction::SetFocus).hasValue());
    CHECK(f.shell.dispatchSemantic(f.controller, "shell.knob.gender", SemanticAction::SetFocus).hasValue());
    CHECK(!f.controller.sceneState().vibratoKeyboardFocus.has_value());
    if (resize)
      CHECK(f.shell.prepareFrame(f.controller, 1000.0, 700.0));
    else
      CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
    f.controller.rebuildAccessibilityTree();
    f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
    // The published focus and the keyboard target agree: the note owns both.
    const auto noteId = "note." + f.note().id.toString();
    const auto* published = f.shell.accessibilityTree().focusedNode();
    CHECK(published != nullptr && published->id == noteId);
    const auto* controllerFocus = f.controller.accessibilityTree().focusedNode();
    CHECK(controllerFocus != nullptr && controllerFocus->id == noteId);
    CHECK(!f.controller.sceneState().vibratoKeyboardFocus.has_value());
    const auto beforeKey = static_cast<int>(f.note().midiKey);
    const auto beforeOnset = f.note().vibrato.startFraction;
    const KeyEvent up{.key = NativeKey::Up};
    if (!f.shell.handleShellKey(f.controller, up)) CHECK(f.controller.keyDown(up).hasValue());
    CHECK(static_cast<int>(f.note().midiKey) == beforeKey + 1);
    CHECK(f.note().vibrato.startFraction == beforeOnset);
  }
}

TEST_CASE("the EXPORT workspace covers the score and runs only the host's real export command") {
  using native_ui::EditorSemanticTree;
  using native_ui::SemanticAction;
  using native_ui::SemanticNode;
  using native_ui::design::ShellExportPlan;
  using native_ui::design::ShellHostActions;
  using native_ui::design::Workspace;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  int runs = 0;
  f.shell.setHostActions(ShellHostActions{
      .exportSet = [&runs]() -> core::Result<void> {
        ++runs;
        return core::success();
      },
      .exportPlan = [] {
        return std::optional<ShellExportPlan>{ShellExportPlan{.sampleRate = 44100U,
                                                              .channels = 2U,
                                                              .format = "24-bit WAV",
                                                              .master = true,
                                                              .stems = false}};
      },
      .exportUnavailable = {},
  });
  CHECK(f.frame());
  // Select the note, then switch workspaces with the EXPORT tab.
  CHECK(f.shell.pointerDown(f.controller, press(f.noteCenter())).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(f.noteCenter())).hasValue());
  const auto tab = f.shell.layout().workspaceTab[4U];
  CHECK(f.shell.pointerDown(f.controller, press({tab.x + tab.width * 0.5, tab.y + tab.height * 0.5}))
            .hasValue());
  CHECK(f.shell.workspace() == Workspace::Export);
  CHECK(f.frame());
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  const auto& tree = f.shell.accessibilityTree();
  CHECK(tree.virtualizedNoteCount() == 0U);
  CHECK(!EditorSemanticTree::containsId(tree.root(), "timeline"));
  CHECK(!EditorSemanticTree::containsId(tree.root(), "shell.lane"));
  const auto* exportTab = [&]() -> const SemanticNode* {
    for (const auto& child : tree.root().children)
      if (child.id == "shell.workspace.export") return &child;
    return nullptr;
  }();
  CHECK(exportTab != nullptr && exportTab->selected);
  const auto* panel = [&]() -> const SemanticNode* {
    for (const auto& child : tree.root().children)
      if (child.id == "shell.export.panel") return &child;
    return nullptr;
  }();
  CHECK(panel != nullptr && panel->value.find("44.1 kHz") != std::string::npos);
  // The grid under the panel is unreachable by pointer, and plain keys do not edit hidden notes.
  const auto grid = f.shell.layout().grid;
  const auto revision = f.controller.documentRevision();
  CHECK(f.shell.pointerDown(f.controller,
                            PointerEvent{.position = {grid.x + 300.0, grid.y + 100.0},
                                         .button = PointerButton::Left, .clickCount = 2})
            .hasValue());
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Delete}));
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Up}));
  CHECK(f.controller.documentRevision() == revision);
  CHECK(f.session.project().findRegion(f.regionId)->notes.size() == 1U);
  // The run button and its accessible twin both reach the host command.
  const auto run = f.shell.exportRunButton();
  CHECK(run.width > 0.0);
  CHECK(f.shell.pointerDown(f.controller, press({run.x + 10.0, run.y + 10.0})).hasValue());
  CHECK(runs == 1);
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.export.run", SemanticAction::Activate).hasValue());
  CHECK(runs == 2);
  // Escape returns to SING and the score is published again.
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(f.shell.workspace() == Workspace::Sing);
  CHECK(f.frame());
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  CHECK(f.shell.accessibilityTree().virtualizedNoteCount() == 1U);
}

TEST_CASE("a host that cannot export says why and never pretends to run") {
  using native_ui::SemanticAction;
  using native_ui::SemanticNode;
  using native_ui::design::ShellHostActions;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.shell.setHostActions(ShellHostActions{
      .exportSet = {}, .exportPlan = {}, .exportUnavailable = "Export from your DAW"});
  CHECK(f.frame());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.workspace.export", SemanticAction::Activate)
            .hasValue());
  CHECK(f.frame());
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  const SemanticNode* runNode = nullptr;
  for (const auto& child : f.shell.accessibilityTree().root().children)
    if (child.id == "shell.export.run") runNode = &child;
  CHECK(runNode != nullptr);
  if (runNode) {
    CHECK(!runNode->enabled);
    CHECK(runNode->description == "Export from your DAW");
  }
  const auto refused = f.shell.dispatchSemantic(f.controller, "shell.export.run", SemanticAction::Activate);
  CHECK(!refused.hasValue());
  const auto run = f.shell.exportRunButton();
  const auto clicked = f.shell.pointerDown(f.controller, press({run.x + 10.0, run.y + 10.0}));
  CHECK(!clicked.hasValue());
  if (!clicked) CHECK(clicked.error().message == "Export from your DAW");
  // VOICE opens the real voice browser (a classic surface for now).
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.workspace.voice", SemanticAction::Activate)
            .hasValue());
  CHECK(f.controller.voicebankBrowserVisible());
}

namespace {

const native_ui::SemanticNode* findShellNode(const native_ui::SemanticNode& node, std::string_view id) {
  if (node.id == id) return &node;
  for (const auto& child : node.children)
    if (const auto* found = findShellNode(child, id); found != nullptr) return found;
  return nullptr;
}

const native_ui::SemanticNode* publishedNode(ShellFixture& f, std::string_view id) {
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  return findShellNode(f.shell.accessibilityTree().root(), id);
}

bool frameAt(ShellFixture& f, double width, double height) {
  if (!f.shell.prepareFrame(f.controller, width, height)) return false;
  native_ui::PixelSurface surface{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
  native_ui::RasterCanvas canvas{surface, 1.0, nullptr};
  return f.shell.paint(canvas, f.controller, f.controller.sceneState(), f.controller.playheadTick());
}

}  // namespace

TEST_CASE("EXPORT keeps modified and plain editing keys away from the hidden score") {
  using native_ui::design::Workspace;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  CHECK(f.shell.pointerDown(f.controller, press(f.noteCenter())).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(f.noteCenter())).hasValue());
  CHECK(!f.session.selection().empty());
  f.shell.setWorkspace(f.controller, Workspace::Export);
  CHECK(f.frame());
  const auto revision = f.controller.documentRevision();
  const std::vector<KeyEvent> editing{
      {.key = NativeKey::Delete, .modifiers = {.alt = true}},
      {.key = NativeKey::Backspace, .modifiers = {.alt = true}},
      {.key = NativeKey::Delete, .modifiers = {.command = true}},
      {.key = NativeKey::X, .modifiers = {.command = true}},
      {.key = NativeKey::D, .modifiers = {.command = true}},
      {.key = NativeKey::A, .modifiers = {.alt = true}},
      {.key = NativeKey::Up, .modifiers = {.alt = true}},
      {.key = NativeKey::Delete},
      {.key = NativeKey::Backspace},
      {.key = NativeKey::D},
      {.key = NativeKey::Q},
      {.key = NativeKey::Up},
  };
  for (const auto& event : editing) {
    // A key the shell passes on reaches the editor exactly as a host would send it.
    if (!f.shell.handleShellKey(f.controller, event)) static_cast<void>(f.controller.keyDown(event));
    CHECK(f.controller.documentRevision() == revision);
    CHECK(f.shell.workspace() == Workspace::Export);
  }
  CHECK(f.session.project().findRegion(f.regionId)->notes.size() == 1U);
  // With no host declaration every modified key stops at the shell, Command-Q included (the
  // editor's plain Q quantizes the selection).
  for (const auto key : {NativeKey::N, NativeKey::O, NativeKey::S, NativeKey::E, NativeKey::Q,
                         NativeKey::Z, NativeKey::Y}) {
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = key, .modifiers = {.command = true}}));
  }
  // Only the shortcuts the host implements itself pass on to it.
  f.shell.setHostActions(native_ui::design::ShellHostActions{
      .exportSet = {}, .exportPlan = {}, .exportUnavailable = {}, .exportBusy = {},
      .regionWaveform = {},
      .applicationShortcut = [](const KeyEvent& event) {
        return event.modifiers.command && !event.modifiers.alt &&
               (event.key == NativeKey::S || event.key == NativeKey::Z);
      }});
  CHECK(!f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::S, .modifiers = {.command = true}}));
  CHECK(!f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Z, .modifiers = {.command = true}}));
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Q, .modifiers = {.command = true}}));
  CHECK(f.shell.handleShellKey(f.controller,
                               KeyEvent{.key = NativeKey::S, .modifiers = {.alt = true, .command = true}}));
  CHECK(f.controller.documentRevision() == revision);
}

TEST_CASE("Escape leaves EXPORT whatever holds focus, at every supported size") {
  using native_ui::SemanticAction;
  using native_ui::design::Workspace;
  const std::vector<std::pair<double, double>> sizes{{480.0, 320.0}, {720.0, 480.0},
                                                     {1280.0, 800.0}, {1600.0, 900.0}};
  for (const auto& [width, height] : sizes) {
    for (int focus = 0; focus < 3; ++focus) {
      ShellFixture f;
      if (!native_ui::paint::vectorBackendAvailable()) return;
      f.controller.resize(width, height);
      CHECK(frameAt(f, width, height));
      f.shell.setWorkspace(f.controller, Workspace::Export);
      CHECK(frameAt(f, width, height));
      if (focus == 0) {
        CHECK(f.shell.dispatchSemantic(f.controller, "shell.export.run", SemanticAction::SetFocus)
                  .hasValue());
      } else if (focus == 1) {
        // A re-homed editor control the EXPORT workspace still publishes (transport, tempo...).
        f.controller.rebuildAccessibilityTree();
        f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
        std::string rehomed;
        for (const auto& child : f.shell.accessibilityTree().root().children) {
          if (child.id.starts_with("toolbar.") && child.enabled) {
            rehomed = child.id;
            break;
          }
        }
        if (rehomed.empty()) std::cerr << "no re-homed control at " << width << "x" << height << '\n';
        CHECK(!rehomed.empty());
        CHECK(f.shell.dispatchController(f.controller, rehomed, SemanticAction::SetFocus).hasValue());
        CHECK(f.shell.accessibilityTree().focusedNode() == nullptr ||
              !SingShell::ownsSemantic(f.shell.accessibilityTree().focusedNode()->id));
      }
      CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
      if (f.shell.workspace() != Workspace::Sing)
        std::cerr << "EXPORT kept at " << width << "x" << height << " focus case " << focus << '\n';
      CHECK(f.shell.workspace() == Workspace::Sing);
    }
  }
}

TEST_CASE("EXPORT refuses a run from live busy state, never from the last paint") {
  using native_ui::SemanticAction;
  using native_ui::design::ShellHostActions;
  using native_ui::design::Workspace;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  int calls = 0;
  bool hostBusy = false;
  f.shell.setHostActions(ShellHostActions{
      .exportSet = [&calls]() -> core::Result<void> {
        ++calls;
        return core::success();
      },
      .exportPlan = {},
      .exportUnavailable = {},
      .exportBusy = [&hostBusy] { return hostBusy; }});
  CHECK(f.frame());
  f.shell.setWorkspace(f.controller, Workspace::Export);
  CHECK(f.frame());
  // The host's worker is in a zero-file preflight: the editor still reports its idle progress.
  hostBusy = true;
  CHECK(f.shell.exportBusy(f.controller));
  CHECK(!f.shell.dispatchSemantic(f.controller, "shell.export.run", SemanticAction::Activate));
  const auto run = f.shell.exportRunButton();
  CHECK(!f.shell.pointerDown(f.controller, press({run.x + 10.0, run.y + 10.0})));
  const auto* busyNode = publishedNode(f, "shell.export.run");
  CHECK(busyNode != nullptr && !busyNode->enabled);
  CHECK(calls == 0);
  hostBusy = false;
  // Staging reported by the editor after the last paint, without a repaint in between.
  f.controller.setExportProgress({.state = authoring::ExportState::Staging,
                                  .currentOutput = "master.wav",
                                  .completedFiles = 0U,
                                  .totalFiles = 2U});
  CHECK(!f.shell.dispatchSemantic(f.controller, "shell.export.run", SemanticAction::Activate));
  CHECK(calls == 0);
  const auto* progress = publishedNode(f, "shell.export.progress");
  CHECK(progress != nullptr);
  // The run ends: the next attempt is accepted, again without a repaint.
  f.controller.setExportProgress({.state = authoring::ExportState::Committed,
                                  .currentOutput = {},
                                  .completedFiles = 2U,
                                  .totalFiles = 2U});
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.export.run", SemanticAction::Activate).hasValue());
  CHECK(calls == 1);
}

TEST_CASE("a failed export attempt publishes its reason even with no files counted") {
  using native_ui::design::Workspace;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.shell.setHostActions({.exportSet = [] { return core::success(); }});
  CHECK(f.frame());
  f.shell.setWorkspace(f.controller, Workspace::Export);
  const std::string reason = "The voicebank cannot cover the phoneme sequence r a";
  f.controller.setExportProgress({.state = authoring::ExportState::Failed,
                                  .currentOutput = reason,
                                  .completedFiles = 0U,
                                  .totalFiles = 0U});
  CHECK(f.frame());
  const auto* attempt = publishedNode(f, "shell.export.attempt");
  CHECK(attempt != nullptr);
  if (attempt != nullptr) {
    CHECK(attempt->value.find("Failed") != std::string::npos ||
          attempt->value.find("failed") != std::string::npos);
    CHECK(attempt->value.find(reason) != std::string::npos);
  }
  // A cancelled attempt is reported the same way.
  f.controller.setExportProgress({.state = authoring::ExportState::Cancelled,
                                  .currentOutput = {},
                                  .completedFiles = 0U,
                                  .totalFiles = 0U});
  CHECK(publishedNode(f, "shell.export.attempt") != nullptr);
}

TEST_CASE("the EXPORT run button, its hit area and its accessible bounds are one layout at every size") {
  using native_ui::SemanticAction;
  using native_ui::design::ShellExportPlan;
  using native_ui::design::ShellHostActions;
  using native_ui::design::Workspace;
  const std::vector<std::pair<double, double>> sizes{{480.0, 320.0}, {720.0, 480.0},
                                                     {1280.0, 800.0}, {1600.0, 900.0},
                                                     {1920.0, 1080.0}};
  const std::string longText(420U, 'w');
  for (const auto& [width, height] : sizes) {
    for (const bool failed : {false, true}) {
      ShellFixture f;
      if (!native_ui::paint::vectorBackendAvailable()) return;
      int calls = 0;
      f.shell.setHostActions(ShellHostActions{
          .exportSet = [&calls]() -> core::Result<void> {
            ++calls;
            return core::success();
          },
          .exportPlan = [&longText] {
            return std::optional<ShellExportPlan>{ShellExportPlan{
                .sampleRate = 48000U, .channels = 2U, .format = longText, .master = true,
                .stems = true, .asksAboutPackaging = true}};
          },
          .exportUnavailable = {}});
      f.controller.resize(width, height);
      CHECK(frameAt(f, width, height));
      f.shell.setWorkspace(f.controller, Workspace::Export);
      if (failed) {
        f.controller.setExportProgress({.state = authoring::ExportState::Failed,
                                        .currentOutput = "/Volumes/" + longText + "/set",
                                        .completedFiles = 0U,
                                        .totalFiles = 0U});
      }
      CHECK(frameAt(f, width, height));
      const auto& layout = f.shell.layout();
      const auto run = f.shell.exportRunButton();
      if (run.bottom() > height || run.right() > width || run.y < layout.editor.y)
        std::cerr << width << "x" << height << " run " << run.x << "," << run.y << " "
                  << run.width << "x" << run.height << '\n';
      CHECK(run.width > 0.0 && run.height > 0.0);
      CHECK(run.x >= 0.0 && run.right() <= width);
      CHECK(run.y >= layout.editor.y && run.bottom() <= layout.lane.bottom());
      CHECK(run.bottom() <= height);
      const auto* node = publishedNode(f, "shell.export.run");
      CHECK(node != nullptr);
      if (node != nullptr) {
        CHECK_NEAR(node->bounds.x, run.x, 1e-9);
        CHECK_NEAR(node->bounds.y, run.y, 1e-9);
        CHECK_NEAR(node->bounds.width, run.width, 1e-9);
        CHECK_NEAR(node->bounds.height, run.height, 1e-9);
      }
      for (const auto* id : {"shell.export.last", "shell.export.attempt"}) {
        if (const auto* status = publishedNode(f, id); status != nullptr) {
          CHECK(status->bounds.bottom() <= height && status->bounds.right() <= width);
        }
      }
      CHECK(f.shell.pointerDown(f.controller,
                                press({run.x + run.width * 0.5, run.y + run.height * 0.5}))
                .hasValue());
      CHECK(calls == 1);
    }
  }
}

TEST_CASE("retained score ids are refused while EXPORT covers the score") {
  using native_ui::SemanticAction;
  using native_ui::design::Workspace;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  const auto id = "note." + f.note().id.toString();
  // On screen, the note is published and takes focus through the shell's host boundary.
  CHECK(f.shell.dispatchController(f.controller, id, SemanticAction::SetFocus).hasValue());
  f.shell.setWorkspace(f.controller, Workspace::Export);
  CHECK(f.frame());
  const auto revision = f.controller.documentRevision();
  CHECK(!f.shell.dispatchController(f.controller, id, SemanticAction::EditText));
  CHECK(!f.controller.textInputActive());
  CHECK(!f.shell.dispatchController(f.controller, id, SemanticAction::Activate));
  CHECK(!f.shell.dispatchController(f.controller, "timeline", SemanticAction::SetFocus));
  CHECK(!f.shell.setControllerValue(f.controller, id, "HIDDEN"));
  CHECK(f.controller.documentRevision() == revision);
  // Back in SING the same id acts again.
  f.shell.setWorkspace(f.controller, Workspace::Sing);
  CHECK(f.frame());
  CHECK(f.shell.dispatchController(f.controller, id, SemanticAction::SetFocus).hasValue());
}

TEST_CASE("every EXPORT size keeps the run button and a failure line on screen") {
  using native_ui::design::ShellHostActions;
  using native_ui::design::Workspace;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  ShellFixture f;
  f.shell.setHostActions(ShellHostActions{.exportSet = [] { return core::success(); }});
  f.shell.setWorkspace(f.controller, Workspace::Export);
  f.controller.setExportProgress({.state = authoring::ExportState::Failed,
                                  .currentOutput = "The voicebank cannot cover r a",
                                  .completedFiles = 0U,
                                  .totalFiles = 0U});
  std::size_t checked = 0U;
  for (double width = 480.0; width <= 1920.0; width += 80.0) {
    for (double height = 320.0; height <= 1080.0; height += 40.0) {
      f.controller.resize(width, height);
      if (!frameAt(f, width, height)) continue;
      ++checked;
      const auto run = f.shell.exportRunButton();
      const auto* attempt = publishedNode(f, "shell.export.attempt");
      const bool runOk = run.height > 0.0 && run.bottom() <= height && run.right() <= width;
      const bool attemptOk = attempt != nullptr && attempt->bounds.height >= 16.0 &&
                             attempt->bounds.bottom() <= height + 1e-9 &&
                             !(attempt->bounds.x == run.x && attempt->bounds.y == run.y) &&
                             attempt->value.find("cannot cover") != std::string::npos;
      if (!runOk || !attemptOk)
        std::cerr << width << "x" << height << " run " << run.y << ".." << run.bottom()
                  << " status " << (attempt ? attempt->bounds.y : -1.0) << "+"
                  << (attempt ? attempt->bounds.height : -1.0) << '\n';
      CHECK(runOk);
      CHECK(attemptOk);
    }
  }
  CHECK(checked > 100U);
}

TEST_CASE("the notes carry their region's rendered waveform only while it matches, and say why not") {
  using native_ui::design::ShellHostActions;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  ShellFixture f;
  std::vector<float> mono(48000U * 4U);
  for (std::size_t i = 0U; i < mono.size(); ++i)
    mono[i] = 0.8F * static_cast<float>(std::sin(static_cast<double>(i) * 0.03)) *
              static_cast<float>(0.5 + 0.5 * std::sin(static_cast<double>(i) * 0.0004));
  const auto envelope = native_ui::RegionEnvelope::build(mono);
  auto view = std::make_shared<const native_ui::RegionEnvelopeView>(native_ui::RegionEnvelopeView{
      native_ui::RegionEnvelopeKey{.region = f.regionId, .pcmSamples = mono.size(),
                                   .sampleRate = 48000U, .originFrame = 0},
      envelope});
  native_ui::RegionWaveform supplied{view, "Waveform", {}};
  f.shell.setHostActions(ShellHostActions{
      .exportSet = {}, .exportPlan = {}, .exportUnavailable = {}, .exportBusy = {},
      .regionWaveform = [&supplied] { return supplied; }});
  const auto paintInto = [&f](native_ui::PixelSurface& surface) {
    CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
    native_ui::RasterCanvas canvas{surface, 1.0, nullptr};
    CHECK(f.shell.paint(canvas, f.controller, f.controller.sceneState(), f.controller.playheadTick()));
  };
  native_ui::PixelSurface withWave{1600U, 900U};
  paintInto(withWave);
  const auto* status = publishedNode(f, "shell.waveform");
  CHECK(status != nullptr && status->value == "Showing the current render");
  supplied = native_ui::RegionWaveform{nullptr, "Out of date", "The score changed after the last render."};
  native_ui::PixelSurface without{1600U, 900U};
  paintInto(without);
  status = publishedNode(f, "shell.waveform");
  CHECK(status != nullptr && status->value == "Out of date" &&
        status->description.find("score changed") != std::string::npos);
  // Only pixels inside the note capsule differ: the waveform never paints outside the note.
  const auto visuals = f.controller.pianoRoll().visibleNotes();
  CHECK(!visuals.empty());
  if (visuals.empty()) return;
  auto capsule = visuals.front().bounds;
  capsule.y += f.shell.layout().grid.y;
  std::size_t inside = 0U;
  std::size_t outside = 0U;
  for (std::uint32_t y = 0U; y < 900U; ++y) {
    for (std::uint32_t x = 0U; x < 1600U; ++x) {
      const auto index = static_cast<std::size_t>(y) * 1600U + x;
      if (withWave.pixels()[index] == without.pixels()[index]) continue;
      const auto px = static_cast<double>(x) + 0.5;
      const auto py = static_cast<double>(y) + 0.5;
      const bool within = px >= capsule.x - 1.0 && px <= capsule.right() + 1.0 &&
                          py >= capsule.y - 1.0 && py <= capsule.bottom() + 1.0;
      const bool caption = px >= f.shell.layout().gridLabel.x && px <= f.shell.layout().gridLabel.right() &&
                           py >= f.shell.layout().gridLabel.y && py <= f.shell.layout().gridLabel.bottom();
      if (within) ++inside;
      else if (!caption) ++outside;
    }
  }
  if (inside < 20U || outside != 0U) std::cerr << "waveform pixels inside=" << inside << " outside=" << outside << '\n';
  CHECK(inside >= 20U);
  CHECK(outside == 0U);
  // A view for another region is refused at paint, whatever the host said.
  auto foreign = std::make_shared<const native_ui::RegionEnvelopeView>(native_ui::RegionEnvelopeView{
      native_ui::RegionEnvelopeKey{.region = domain::RegionId{f.regionId.value() + 1000U},
                                   .pcmSamples = mono.size(), .sampleRate = 48000U},
      envelope});
  supplied = native_ui::RegionWaveform{foreign, "Waveform", {}};
  native_ui::PixelSurface foreignFrame{1600U, 900U};
  paintInto(foreignFrame);
  status = publishedNode(f, "shell.waveform");
  CHECK(status != nullptr && status->value == "Other region");
}

TEST_CASE("a long note at maximum zoom walks only its visible waveform columns") {
  using native_ui::design::ShellHostActions;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  // The column walk itself: a note far wider than the grid, starting far to its left.
  std::size_t walked = 0U;
  bool phased = true;
  const ui::Rect huge{-1'000'001.0, 300.0, 3'000'000.0, 12.0};
  const auto count = native_ui::design::noteWaveformColumns(
      huge, 80.0, 1120.0, [&](double x0, double x1) {
        ++walked;
        const auto phase = std::fmod(x0 - huge.x, native_ui::design::kNoteWaveformColumn);
        phased = phased && std::abs(phase) < 1e-6 && x1 > x0 && x1 > 80.0 && x0 < 1120.0;
      });
  CHECK(count == walked);
  CHECK(count <= 522U && count >= 519U);
  CHECK(phased);
  CHECK(native_ui::design::noteWaveformColumns({2000.0, 0.0, 50.0, 10.0}, 80.0, 1120.0,
                                               [](double, double) {}) == 0U);

  // Painted: one 90-quarter note at the maximum zoom, scrolled so the grid shows its middle.
  ShellFixture f{time::Tick{0}, true};
  auto* region = f.session.project().findRegion(f.regionId);
  region->notes.front().durationTick = time::Tick{960 * 90};
  f.controller.pianoRoll().rebuildIndex();
  std::vector<float> mono(48000U * 50U);
  for (std::size_t i = 0U; i < mono.size(); ++i)
    mono[i] = 0.6F * static_cast<float>(std::sin(static_cast<double>(i) * 0.02));
  const auto envelope = native_ui::RegionEnvelope::build(mono);
  auto view = std::make_shared<const native_ui::RegionEnvelopeView>(native_ui::RegionEnvelopeView{
      native_ui::RegionEnvelopeKey{.region = f.regionId, .pcmSamples = mono.size(),
                                   .sampleRate = 48000U, .originFrame = 0},
      envelope});
  f.shell.setHostActions(ShellHostActions{
      .exportSet = {}, .exportPlan = {}, .exportUnavailable = {}, .exportBusy = {},
      .regionWaveform = [view] { return native_ui::RegionWaveform{view, "Waveform", {}}; }});
  CHECK(f.frame());
  auto& timeline = f.controller.pianoRoll().timeline();
  timeline.setPixelsPerQuarter(4096.0);
  timeline.setOriginTick(time::Tick{960 + 960 * 45});
  f.controller.pianoRoll().rebuildIndex();
  const auto before = envelope->queries();
  CHECK(f.frame());
  const auto queried = envelope->queries() - before;
  const auto gridWidth = f.shell.layout().grid.width;
  const auto bound = static_cast<std::uint64_t>(std::ceil(gridWidth / native_ui::design::kNoteWaveformColumn)) + 2U;
  const auto visuals = f.controller.pianoRoll().visibleNotes();
  double longest = 0.0;
  for (const auto& visual : visuals) longest = std::max(longest, visual.bounds.width);
  if (queried == 0U || queried > bound)
    std::cerr << "queried " << queried << " columns, bound " << bound << ", note width " << longest << '\n';
  CHECK(longest > 100000.0);  // the note really is far wider than the grid
  CHECK(queried > 0U);
  CHECK(queried <= bound);
  // And the frame budget: the waveform adds little to a frame, however long the note. The bound
  // is coarse (medians of five frames, 25 ms of slack) so a loaded test machine does not flake;
  // the per-column stroke this replaced cost about 65 ms here.
  native_ui::RegionWaveform supplied{view, "Waveform", {}};
  f.shell.setHostActions(ShellHostActions{
      .exportSet = {}, .exportPlan = {}, .exportUnavailable = {}, .exportBusy = {},
      .regionWaveform = [&supplied] { return supplied; }});
  const auto medianFrame = [&f] {
    std::vector<double> times;
    for (int i = 0; i < 5; ++i) {
      const auto started = std::chrono::steady_clock::now();
      CHECK(f.frame());
      times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    }
    std::sort(times.begin(), times.end());
    return times[2];
  };
  const auto withWave = medianFrame();
  supplied = native_ui::RegionWaveform{nullptr, "Out of date", "test"};
  const auto withoutWave = medianFrame();
  if (withWave > withoutWave + 25.0)
    std::cerr << "waveform frame " << withWave << " ms vs " << withoutWave << " ms without\n";
  CHECK(withWave <= withoutWave + 25.0);
}

TEST_CASE("UI evidence is the presented layout and the published tree, not a copy of them") {
  using formats::JsonValue;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  ShellFixture f{time::Tick{0}, true};
  CHECK(f.frame());
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  const auto& l = f.shell.layout();
  const auto sameRect = [](const JsonValue* value, ui::Rect r) {
    if (value == nullptr) return false;
    const auto& a = value->asArray();
    return a.size() == 4U && a[0].asNumber() == r.x && a[1].asNumber() == r.y &&
           a[2].asNumber() == r.width && a[3].asNumber() == r.height;
  };

  const auto geometry = native_ui::design::singLayoutEvidence(f.shell, 2.0);
  CHECK(geometry.find("presented")->asBool());
  CHECK(geometry.find("mode")->asString() == "emo");
  CHECK(geometry.find("workspace")->asString() == "sing");
  CHECK(geometry.find("deviceScale")->asNumber() == 2.0);
  CHECK(geometry.find("rack")->asString() == "full");
  const auto* regions = geometry.find("regions");
  CHECK(regions != nullptr);
  // Every canonical region of docs/design/ui-fidelity-contract-v1.json is emitted.
  std::set<std::string, std::less<>> names;
  for (const auto& [name, value] : regions->asObject()) names.insert(name);
  for (const auto* id : {"header", "wordmark", "workspaceTabs", "modeSwitch", "transport",
                         "outputMeter", "settings", "editor", "tools", "ruler", "keyboard", "grid",
                         "lane", "laneTabs", "lanePlot", "laneTimePlot", "rack", "singer",
                         "portraitRing", "expression", "style", "status"})
    CHECK(names.contains(id));
  CHECK(sameRect(regions->find("grid"), l.grid));
  CHECK(sameRect(regions->find("rack"), l.rackArea));
  CHECK(sameRect(regions->find("laneTimePlot"), l.laneTimePlot));
  CHECK(sameRect(regions->find("status"), l.status));
  const auto* controls = geometry.find("controls");
  CHECK(sameRect(controls->find("knob0"), l.knob[0]));
  CHECK(sameRect(controls->find("playButton"), l.playButton));

  // Semantic bounds are the published nodes, and the shell controls sit on their layout rects.
  const auto semantic = native_ui::design::semanticEvidence(f.shell.accessibilityTree(), 4U);
  std::map<std::string, const JsonValue*, std::less<>> byId;
  for (const auto& node : semantic.find("nodes")->asArray()) {
    const auto& id = node.find("id")->asString();
    CHECK(!byId.contains(id));  // ids are unique in the flattened tree
    byId.emplace(id, &node);
  }
  const auto bounds = [&byId](std::string_view id) {
    const auto it = byId.find(id);
    return it == byId.end() ? nullptr : it->second->find("bounds");
  };
  CHECK(sameRect(bounds("shell.status"), l.status));
  CHECK(sameRect(bounds("shell.lane"), l.laneTimePlot));
  CHECK(sameRect(bounds("shell.settings"), l.settings));
  CHECK(sameRect(bounds("shell.change-voice"), l.singerChange));
  CHECK(sameRect(bounds("shell.style"), l.style));
  CHECK(sameRect(bounds("shell.workspace.sing"), l.workspaceTab[0]));
  CHECK(sameRect(bounds("shell.knob.gender"), l.knob[4]));
  CHECK(byId.at("shell.knob.gender")->find("role")->asString() == "slider");
  // The note list is bounded by the limit and never exceeds what the tree virtualizes.
  const auto noteCount = semantic.find("notes")->asArray().size();
  const auto virtualized = static_cast<std::size_t>(semantic.find("virtualizedNoteCount")->asNumber());
  CHECK(virtualized >= 4U);
  CHECK(noteCount == 4U);
}

TEST_CASE("a failed render's status line names its reason, not only that it failed") {
  using native_ui::RenderStatusState;
  using native_ui::design::StatusTone;
  native_ui::EditorSceneState state;
  state.audioDeviceOnline = true;
  state.audioBackend = "CoreAudio";
  CHECK(native_ui::design::singStatusMessage(state).text == "Audio CoreAudio");
  CHECK(native_ui::design::singStatusMessage(state).tone == StatusTone::Normal);

  // A render note alone is status, not a warning.
  state.renderStatus.state = RenderStatusState::Ready;
  state.renderStatus.diagnostic = "Production multi-track routing render completed";
  CHECK(native_ui::design::singStatusMessage(state).text == state.renderStatus.diagnostic);
  CHECK(native_ui::design::singStatusMessage(state).tone == StatusTone::Normal);

  // Failed with a RENDER_FAILED diagnostic: the title and the reason, as a warning.
  const std::string reason =
      "Project has no audible rendered tracks: Voicebank cannot cover the phoneme sequence";
  state.renderStatus.state = RenderStatusState::Failed;
  state.renderStatus.diagnostic = reason;
  state.diagnostics.push_back(authoring::Diagnostic{.code = "RENDER_FAILED"});
  const auto failed = native_ui::design::singStatusMessage(state);
  CHECK(failed.text.starts_with("Render did not complete"));
  CHECK(failed.text.ends_with(reason));
  CHECK(failed.tone == StatusTone::Warning);

  // Any other diagnostic keeps its title; a render that did not fail adds no reason to it.
  state.renderStatus.state = RenderStatusState::Ready;
  state.diagnostics.front().code = "BANK_MISSING";
  CHECK(native_ui::design::singStatusMessage(state).text == "Voicebank needs attention");
}

TEST_CASE("the compact inspector opens from its drawer button and keeps every rack control usable") {
  using native_ui::SemanticAction;
  using native_ui::design::RackPresentation;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  ShellFixture f;
  const auto frameAt = [&f](double width, double height) {
    if (!f.shell.prepareFrame(f.controller, width, height)) return false;
    native_ui::PixelSurface surface{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    native_ui::RasterCanvas canvas{surface, 1.0, nullptr};
    return f.shell.paint(canvas, f.controller, f.controller.sceneState(), f.controller.playheadTick());
  };
  const auto published = [&f](std::string_view id) {
    f.controller.rebuildAccessibilityTree();
    f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
    for (const auto& node : f.shell.accessibilityTree().root().children)
      if (node.id == id) return true;
    return false;
  };

  // Full rack: the Stage stands behind the notes; no inspector exists.
  CHECK(frameAt(1600.0, 900.0));
  CHECK(f.shell.lastFrameShowedStage() == f.shell.assetsLoaded(DesignMode::Emo));
  CHECK(!published("shell.inspector"));

  // 720x480: a 44-point drawer button, no Stage, and no knob until the inspector opens.
  CHECK(frameAt(720.0, 480.0));
  const auto closed = f.shell.layout();
  CHECK(closed.rack == RackPresentation::Drawer);
  CHECK(!f.shell.lastFrameShowedStage());
  CHECK(published("shell.inspector"));
  CHECK(!published("shell.knob.gender"));
  CHECK(!published("shell.change-voice"));
  const auto revision = f.controller.documentRevision();
  CHECK(!f.shell.dispatchSemantic(f.controller, "shell.knob.gender", SemanticAction::Increment).hasValue());

  // The button opens it; its knobs, Change voice and style are published where they are painted.
  const auto button = closed.inspectorButton;
  const ui::Point buttonCenter{button.x + button.width * 0.5, button.y + button.height * 0.5};
  CHECK(f.shell.pointerDown(f.controller, press(buttonCenter)));
  CHECK(f.shell.pointerUp(f.controller, press(buttonCenter)));
  CHECK(f.shell.inspectorOpen());
  CHECK(frameAt(720.0, 480.0));
  CHECK(f.shell.inspectorOpen());  // a frame at the same size keeps it open
  const auto open = f.shell.layout();
  CHECK(published("shell.knob.gender"));
  CHECK(published("shell.change-voice"));
  CHECK(published("shell.style"));

  // A drag on a knob inside the inspector is one command and one undo step.
  const auto knob = open.knob[4];  // gender
  const ui::Point dial{knob.x + knob.width * 0.5, knob.y + 46.0};
  CHECK(f.shell.pointerDown(f.controller, press(dial)));
  CHECK(f.shell.pointerMove(f.controller, press({dial.x, dial.y - 36.0})));
  CHECK(f.shell.pointerUp(f.controller, press({dial.x, dial.y - 36.0})));
  CHECK(f.controller.documentRevision() == revision + 1U);
  CHECK(f.session.canUndo());

  // A press in the grid outside the inspector only closes it: no note is created or moved.
  const auto notesBefore = f.session.project().findRegion(f.regionId)->notes.size();
  const ui::Point gridPoint{open.grid.x + 20.0, open.grid.y + open.grid.height * 0.5};
  CHECK(gridPoint.x < open.inspector.x);  // left of the inspector, inside the grid
  CHECK(f.shell.pointerDown(f.controller, press(gridPoint)));
  CHECK(f.shell.pointerUp(f.controller, press(gridPoint)));
  CHECK(!f.shell.inspectorOpen());
  CHECK(f.session.project().findRegion(f.regionId)->notes.size() == notesBefore);
  CHECK(f.controller.documentRevision() == revision + 1U);

  // Keyboard: Activate on the button opens it with focus kept there; Tab walks into the knobs;
  // Escape closes it and returns focus to the button.
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.inspector", SemanticAction::Activate));
  CHECK(f.shell.inspectorOpen());
  CHECK(f.shell.accessibilityTree().focusedNode() != nullptr &&
        f.shell.accessibilityTree().focusedNode()->id == "shell.inspector");
  std::string reached;
  for (int i = 0; i < 12 && !reached.starts_with("shell.knob."); ++i) {
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Tab}));
    const auto* focused = f.shell.accessibilityTree().focusedNode();
    reached = focused == nullptr ? std::string{} : focused->id;
  }
  CHECK(reached.starts_with("shell.knob."));
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(!f.shell.inspectorOpen());
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  CHECK(f.shell.accessibilityTree().focusedNode() != nullptr &&
        f.shell.accessibilityTree().focusedNode()->id == "shell.inspector");

  // Growing back to the full rack forgets the inspector; shrinking again starts closed.
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.inspector", SemanticAction::Activate));
  CHECK(frameAt(1600.0, 900.0));
  CHECK(!f.shell.inspectorOpen());
  CHECK(frameAt(720.0, 480.0));
  CHECK(!f.shell.inspectorOpen());
}

TEST_CASE("an inspector opened by pointer owns the keys, and nothing reaches the score it covers") {
  using native_ui::SemanticAction;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  ShellFixture f;
  const auto frameAt = [&f](double width, double height) {
    if (!f.shell.prepareFrame(f.controller, width, height)) return false;
    native_ui::PixelSurface surface{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    native_ui::RasterCanvas canvas{surface, 1.0, nullptr};
    return f.shell.paint(canvas, f.controller, f.controller.sceneState(), f.controller.playheadTick());
  };
  // Developer 2's reproduction: select a note, move it under where the panel will open, open the
  // inspector with the pointer, then send editing keys as a host would.
  CHECK(frameAt(860.0, 640.0));
  const auto start = f.noteCenter();
  CHECK(f.shell.pointerDown(f.controller, press(start)));
  CHECK(f.shell.pointerMove(f.controller, press({start.x + 350.0, start.y})));
  CHECK(f.shell.pointerUp(f.controller, press({start.x + 350.0, start.y})));
  CHECK(!f.session.selection().empty());
  CHECK(frameAt(860.0, 640.0));
  const auto selected = f.noteCenter();
  const auto button = f.shell.layout().inspectorButton;
  const ui::Point center{button.x + 22.0, button.y + 22.0};
  CHECK(f.shell.pointerDown(f.controller, press(center)));
  CHECK(f.shell.pointerUp(f.controller, press(center)));
  CHECK(frameAt(860.0, 640.0));
  CHECK(f.shell.inspectorOpen());
  const auto panel = f.shell.layout().inspector;
  CHECK(selected.x >= panel.x && selected.x < panel.right() && selected.y >= panel.y &&
        selected.y < panel.bottom());  // the selected note really is covered

  // The pointer opening moved focus to the inspector, like the keyboard opening does.
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  const auto* focused = f.shell.accessibilityTree().focusedNode();
  CHECK(focused != nullptr && focused->id == "shell.inspector");

  const auto revision = f.controller.documentRevision();
  const auto notes = f.session.project().findRegion(f.regionId)->notes.size();
  const std::vector<KeyEvent> editing{
      {.key = NativeKey::Delete},
      {.key = NativeKey::Backspace},
      {.key = NativeKey::D},
      {.key = NativeKey::Q},
      {.key = NativeKey::Up},
      {.key = NativeKey::Delete, .modifiers = {.alt = true}},
      {.key = NativeKey::Delete, .modifiers = {.command = true}},
      {.key = NativeKey::X, .modifiers = {.command = true}},
      {.key = NativeKey::D, .modifiers = {.command = true}},
      {.key = NativeKey::Q, .modifiers = {.command = true}},
      {.key = NativeKey::Up, .modifiers = {.alt = true}},
  };
  for (const auto& event : editing) {
    if (!f.shell.handleShellKey(f.controller, event)) static_cast<void>(f.controller.keyDown(event));
    CHECK(f.controller.documentRevision() == revision);
    CHECK(f.shell.inspectorOpen());
  }
  CHECK(f.session.project().findRegion(f.regionId)->notes.size() == notes);

  // Even with focus moved off the button (a knob, then cleared), the score stays covered.
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Tab}));
  f.shell.controllerFocusTaken();
  if (!f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Delete}))
    static_cast<void>(f.controller.keyDown(KeyEvent{.key = NativeKey::Delete}));
  CHECK(f.session.project().findRegion(f.regionId)->notes.size() == notes);

  // The covered score is not published, so an accessibility action on a note or the timeline is
  // refused; the inspector's own controls still act.
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  CHECK(f.shell.accessibilityTree().virtualizedNoteCount() == 0U);
  const auto& region = *f.session.project().findRegion(f.regionId);
  const auto noteId = "note." + region.notes.front().id.toString();
  CHECK(!f.shell.dispatchController(f.controller, noteId, SemanticAction::SetFocus).hasValue());
  CHECK(!f.shell.dispatchController(f.controller, "timeline", SemanticAction::SetFocus).hasValue());
  CHECK(!f.shell.dispatchSemantic(f.controller, "shell.classic", SemanticAction::Activate).hasValue());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.knob.gender", SemanticAction::SetFocus).hasValue());

  // Only shortcuts the host declares as its own commands pass on.
  f.shell.setHostActions(native_ui::design::ShellHostActions{
      .exportSet = {}, .exportPlan = {}, .exportUnavailable = {}, .exportBusy = {},
      .regionWaveform = {},
      .applicationShortcut = [](const KeyEvent& event) {
        return event.modifiers.command && !event.modifiers.alt && event.key == NativeKey::S;
      }});
  CHECK(!f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::S, .modifiers = {.command = true}}));
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::D, .modifiers = {.command = true}}));

  // Closed again, the score is published and editable as before.
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(!f.shell.inspectorOpen());
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  CHECK(f.shell.accessibilityTree().virtualizedNoteCount() == notes);
  // The same ids act again once the score is uncovered (so the refusals above were real).
  CHECK(f.shell.dispatchController(f.controller, noteId, SemanticAction::SetFocus).hasValue());
  CHECK(f.shell.dispatchController(f.controller, "timeline", SemanticAction::SetFocus).hasValue());
}

TEST_CASE("compact workspaces and appearance remain reachable without hidden score edits") {
  using native_ui::SemanticAction;
  using native_ui::design::Workspace;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.shell.prepareFrame(f.controller, 480.0, 320.0));
  const auto l = f.shell.layout();
  CHECK(l.workspaceTabs.width == 0.0);
  CHECK(l.workspaceMenuButton.width >= 44.0);
  CHECK(l.modeSwitch.width == 0.0);
  CHECK(l.workspaceMenu.bottom() < l.status.y);
  const auto revision = f.controller.documentRevision();
  CHECK(f.shell.pointerDown(f.controller, press({l.workspaceMenuButton.x + 12.0,
                                                l.workspaceMenuButton.y + 12.0})).hasValue());
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  CHECK(findShellNode(f.shell.accessibilityTree().root(), "shell.workspace.export") != nullptr);
  CHECK(findShellNode(f.shell.accessibilityTree().root(), "shell.mode.scene") != nullptr);
  CHECK(f.shell.accessibilityTree().virtualizedNoteCount() == 0U);
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Delete}));
  CHECK(f.controller.documentRevision() == revision);
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.mode.scene", SemanticAction::Activate).hasValue());
  CHECK(f.shell.mode() == DesignMode::Scene);
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  CHECK(f.shell.accessibilityTree().focusedNode() != nullptr &&
        f.shell.accessibilityTree().focusedNode()->id == "shell.workspace-menu");
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Delete}));
  CHECK(f.controller.documentRevision() == revision);
  CHECK(f.shell.pointerDown(f.controller, press({l.workspaceMenuButton.x + 12.0,
                                                l.workspaceMenuButton.y + 12.0})).hasValue());
  CHECK(f.shell.pointerDown(f.controller, press({l.workspaceMenuRow[4].x + 12.0,
                                                l.workspaceMenuRow[4].y + 12.0})).hasValue());
  CHECK(f.shell.workspace() == Workspace::Export);
  CHECK(f.controller.documentRevision() == revision);
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.workspace-menu", SemanticAction::Activate)
            .hasValue());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.workspace.sing", SemanticAction::Activate)
            .hasValue());
  CHECK(f.shell.workspace() == Workspace::Sing);
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.workspace.export", SemanticAction::Activate)
            .hasValue() == false);  // menu items disappear when it closes
}

TEST_CASE("TUNE and MIX cover the score with their own body, and Escape returns to SING") {
  using native_ui::SemanticAction;
  using native_ui::design::Workspace;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  const auto revision = f.controller.documentRevision();
  const auto notes = f.session.project().findRegion(f.regionId)->notes.size();
  for (const auto& [tab, workspace, prefix] :
       {std::tuple{"shell.workspace.tune", Workspace::Tune, "shell.tune."},
        std::tuple{"shell.workspace.mix", Workspace::Mix, "shell.mix."}}) {
    CHECK(f.shell.dispatchSemantic(f.controller, tab, SemanticAction::Activate).hasValue());
    CHECK(f.shell.workspace() == workspace);
    CHECK(f.shell.bodyWorkspace() != nullptr);
    CHECK(f.frame());
    f.controller.rebuildAccessibilityTree();
    f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
    // The score is covered: no note or timeline node, and the body publishes its own nodes.
    CHECK(f.shell.accessibilityTree().virtualizedNoteCount() == 0U);
    CHECK(findShellNode(f.shell.accessibilityTree().root(), "timeline") == nullptr);
    bool bodyNode = false;
    for (const auto& node : f.shell.accessibilityTree().root().children)
      bodyNode = bodyNode || node.id.starts_with(prefix);
    CHECK(bodyNode);
    const auto* selected = findShellNode(f.shell.accessibilityTree().root(), tab);
    CHECK(selected != nullptr && selected->selected);
    // Editing keys never reach the selected note behind the body.
    for (const auto key : {NativeKey::Delete, NativeKey::Backspace, NativeKey::Up}) {
      if (!f.shell.handleShellKey(f.controller, KeyEvent{.key = key}))
        static_cast<void>(f.controller.keyDown(KeyEvent{.key = key}));
    }
    // A modified editing shortcut stops at the shell too.
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::D,
                                                         .modifiers = {.command = true}}));
    CHECK(f.controller.documentRevision() == revision);
    CHECK(f.session.project().findRegion(f.regionId)->notes.size() == notes);
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
    CHECK(f.shell.workspace() == Workspace::Sing);
    CHECK(f.shell.bodyWorkspace() == nullptr);
  }
  // The compact menu lists all five workspaces and both looks above the status bar.
  CHECK(f.shell.prepareFrame(f.controller, 480.0, 320.0));
  const auto l = f.shell.layout();
  CHECK(l.workspaceMenu.bottom() < l.status.y);
  CHECK(l.modeMenuRow[1].bottom() <= l.workspaceMenu.bottom());
  CHECK(l.modeMenuRow[0].y >= l.workspaceMenuRow[4].bottom());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.workspace-menu", SemanticAction::Activate).hasValue());
  CHECK(f.shell.pointerDown(f.controller, press({l.workspaceMenuRow[3].x + 12.0,
                                                l.workspaceMenuRow[3].y + 12.0})).hasValue());
  CHECK(f.shell.workspace() == Workspace::Mix);
}

TEST_CASE("a TUNE or MIX drag owns the input until it ends, and a resize abandons it") {
  using native_ui::SemanticAction;
  using native_ui::design::Workspace;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.workspace.mix", SemanticAction::Activate).hasValue());
  CHECK(f.frame());
  const auto fader = [&f] {
    f.controller.rebuildAccessibilityTree();
    f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
    const native_ui::SemanticNode* best = nullptr;
    for (const auto& node : f.shell.accessibilityTree().root().children)
      if (node.id.starts_with("shell.mix.track.") && node.role == native_ui::SemanticRole::Slider &&
          (best == nullptr || node.bounds.height > best->bounds.height))
        best = &node;
    CHECK(best != nullptr);
    return best == nullptr ? native_ui::SemanticNode{} : *best;
  };
  const auto drag = [&f](const native_ui::SemanticNode& node) {
    const ui::Point start{node.bounds.x + node.bounds.width * 0.5,
                          node.bounds.y + node.bounds.height * 0.5};
    CHECK(f.shell.pointerDown(f.controller, press(start)).hasValue());
    CHECK(f.shell.pointerMove(f.controller, press({start.x, start.y - 60.0})).hasValue());
    return ui::Point{start.x, start.y - 60.0};
  };
  const auto revision = f.controller.documentRevision();

  // Mid-drag, keys and accessibility actions do nothing; the drag then commits one step.
  auto node = fader();
  auto end = drag(node);
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Up}));
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Z, .modifiers = {.command = true}}));
  CHECK(!f.shell.dispatchSemantic(f.controller, node.id, SemanticAction::Increment).hasValue());
  CHECK(!f.shell.dispatchSemantic(f.controller, "shell.workspace.sing", SemanticAction::Activate).hasValue());
  CHECK(f.shell.scroll(f.controller, 0.0, 40.0, end, {}));
  CHECK(f.controller.documentRevision() == revision);
  CHECK(f.shell.pointerUp(f.controller, press(end)).hasValue());
  CHECK(f.controller.documentRevision() == revision + 1U);
  CHECK(f.session.undo());

  // A resize mid-drag abandons it: the release commits nothing against the new geometry.
  node = fader();
  end = drag(node);
  CHECK(f.shell.prepareFrame(f.controller, 720.0, 480.0));
  CHECK(f.shell.pointerUp(f.controller, press(end)).hasValue());
  CHECK(f.controller.documentRevision() == revision + 2U);  // the undo above counts as one
  CHECK(f.shell.workspace() == Workspace::Mix);

  // A press after a lost release abandons the open drag; the new press never commits it.
  node = fader();
  end = drag(node);
  const auto before = f.controller.documentRevision();
  const auto header = f.shell.layout().header;
  const ui::Point outside{header.x + 3.0, header.y + 3.0};  // the header's corner, no control
  CHECK(f.shell.pointerDown(f.controller, press(outside)).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press({outside.x, outside.y + 50.0})).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press({outside.x, outside.y + 50.0})).hasValue());
  CHECK(f.controller.documentRevision() == before);
}

TEST_CASE("the compact inspector is modal over TUNE and MIX as well as SING") {
  using native_ui::SemanticAction;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  for (const auto* tab : {"shell.workspace.tune", "shell.workspace.mix", "shell.workspace.export"}) {
    // A rail layout: header tabs are shown and the singer inspector exists.
    CHECK(f.shell.prepareFrame(f.controller, 1000.0, 700.0));
    CHECK(f.shell.dispatchSemantic(f.controller, tab, SemanticAction::Activate).hasValue());
    CHECK(f.shell.prepareFrame(f.controller, 1000.0, 700.0));
    f.controller.rebuildAccessibilityTree();
    f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
    std::vector<std::string> body;
    for (const auto& n : f.shell.accessibilityTree().root().children)
      if (n.id.starts_with("shell.tune.") || n.id.starts_with("shell.mix.") ||
          n.id.starts_with("shell.export."))
        body.push_back(n.id);
    CHECK(!body.empty());
    CHECK(f.shell.dispatchSemantic(f.controller, "shell.inspector", SemanticAction::Activate).hasValue());
    CHECK(f.shell.inspectorOpen());
    f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
    const auto revision = f.controller.documentRevision();
    for (const auto& id : body) {
      CHECK(findShellNode(f.shell.accessibilityTree().root(), id) == nullptr);
      CHECK(!f.shell.dispatchSemantic(f.controller, id, SemanticAction::Increment).hasValue());
      CHECK(!f.shell.dispatchSemantic(f.controller, id, SemanticAction::Activate).hasValue());
    }
    // Tab stays inside the inspector.
    for (int i = 0; i < 24; ++i) {
      CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Tab}));
      const auto* focused = f.shell.accessibilityTree().focusedNode();
      CHECK((focused == nullptr || !(focused->id.starts_with("shell.tune.") ||
                                     focused->id.starts_with("shell.mix.") ||
                                     focused->id.starts_with("shell.export."))));
    }
    CHECK(f.controller.documentRevision() == revision);
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
    CHECK(!f.shell.inspectorOpen());
    CHECK(f.shell.dispatchSemantic(f.controller, "shell.workspace.sing", SemanticAction::Activate).hasValue());
  }
}

TEST_CASE("a compact first frame reveals the phrase but later user pitch scrolling is respected") {
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  CHECK(f.controller.pianoRoll().pitch().topMidiKey() == 84);
  CHECK(f.shell.prepareFrame(f.controller, 720.0, 480.0));
  const auto& pitch = f.controller.pianoRoll().pitch();
  CHECK(pitch.topMidiKey() < 84);
  const auto noteY = pitch.midiToPixel(f.note().midiKey);
  CHECK(noteY >= 0.0 && noteY + pitch.rowHeight() <= f.shell.layout().grid.height);
  f.controller.pianoRoll().pitch().setTopMidiKey(100);
  CHECK(f.shell.prepareFrame(f.controller, 720.0, 440.0));
  CHECK(f.controller.pianoRoll().pitch().topMidiKey() == 100);
}

TEST_CASE("notes loaded after an empty first frame are framed when they arrive") {
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  auto* region = f.session.project().findRegion(f.regionId);
  const auto notes = region->notes;
  region->notes.clear();
  f.controller.pianoRoll().rebuildIndex();
  CHECK(f.shell.prepareFrame(f.controller, 720.0, 480.0));
  CHECK(f.controller.pianoRoll().pitch().topMidiKey() == 84);
  region->notes = notes;
  f.controller.pianoRoll().rebuildIndex();
  CHECK(f.shell.prepareFrame(f.controller, 720.0, 480.0));
  const auto& pitch = f.controller.pianoRoll().pitch();
  CHECK(pitch.topMidiKey() < 84);
  CHECK(pitch.midiToPixel(notes.front().midiKey) + pitch.rowHeight() <= f.shell.layout().grid.height);
}

TEST_CASE("a replacement editor controller with the same region gets its own compact framing") {
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.shell.prepareFrame(f.controller, 720.0, 480.0));
  CHECK(f.controller.pianoRoll().pitch().topMidiKey() < 84);
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.inspector", native_ui::SemanticAction::Activate));
  CHECK(f.shell.inspectorOpen());
  native_ui::NativeEditorController replacement{f.session, f.factory, f.regionId};
  CHECK(replacement.instanceSerial() != f.controller.instanceSerial());
  CHECK(replacement.pianoRoll().pitch().topMidiKey() == 84);
  CHECK(f.shell.prepareFrame(replacement, 720.0, 480.0));
  CHECK(f.shell.inspectorOpen());  // Presentation choice survives a project controller swap.
  CHECK(replacement.pianoRoll().pitch().topMidiKey() < 84);
  const auto y = replacement.pianoRoll().pitch().midiToPixel(f.note().midiKey);
  CHECK(y >= 0.0 && y + replacement.pianoRoll().pitch().rowHeight() <= f.shell.layout().grid.height);
}
