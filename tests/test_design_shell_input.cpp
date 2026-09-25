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
#include "seam/native_ui/editor_semantics.hpp"
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
  std::optional<native_ui::TextInputRequest> lastTextInput;

  explicit ShellFixture(time::Tick regionStart = time::Tick{0})
      : session(makeProject(regionStart)),
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

  domain::Project makeProject(time::Tick regionStart) {
    auto project = factory.createProject("Design shell");
    project.settings().characterDisplay = domain::CharacterDisplayMode::Off;
    trackId = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, trackId, "Phrase", regionStart, time::Tick{7680});
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
    CHECK(f.frame());
    f.controller.setPlayheadTick(kRegionStart + time::Tick{1920});
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
    // A playhead before the region edits the value the knob shows: the region's first tick, never
    // the absolute playhead tick reinterpreted as a region-local one.
    ShellFixture f{kRegionStart};
    CHECK(f.frame());
    f.controller.setPlayheadTick(time::Tick{960});
    CHECK(f.controller.nudgeGender(3).hasValue());
    const auto& points = f.session.project().findRegion(f.regionId)->genderAutomation.points();
    CHECK(points.size() == 1U);
    if (!points.empty()) CHECK(points.front().tick == time::Tick{0});
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
  const auto* voice = find("shell.workspace.voice");
  CHECK(voice && !voice->enabled);
  CHECK(find("shell.lane-tab.gender") != nullptr);
  // Notes keep controller ids and sit exactly on their painted rectangles in the grid.
  const auto visuals = f.controller.pianoRoll().visibleNotes();
  CHECK(!visuals.empty());
  if (!visuals.empty()) {
    const auto* note = find("note." + visuals.front().noteId.toString());
    CHECK(note != nullptr);
    if (note) {
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
