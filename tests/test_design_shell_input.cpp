// Active SING shell input: gesture cancellation, surface/geometry ownership, IME placement and the
// hosted expression lane, driven through a real NativeEditorController and EditorSession.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/domain/project.hpp"
#include "seam/native_ui/design/sing_layout.hpp"
#include "seam/native_ui/design/shell_overlays.hpp"
#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/native_ui/design/shell_evidence.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_semantics.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/ui/expression_lane.hpp"
#include "seam/ui/phoneme_lane_model.hpp"
#include "seam/voice_design/recipe_resource.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
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
    // The frame's own surface is kept, so a test can read the pixels the painters produced.
    surface = native_ui::PixelSurface{1600U, 900U};
    native_ui::RasterCanvas canvas{surface, 1.0, nullptr};
    return shell.paint(canvas, controller, controller.sceneState(), controller.playheadTick());
  }

  native_ui::PixelSurface surface;
};

PointerEvent press(ui::Point p) { return {.position = p, .button = PointerButton::Left}; }

}  // namespace

TEST_CASE("the active shell keeps the input geometry when a sheet opens") {
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  const auto hosted = f.controller.hostedGrid();
  CHECK(hosted.has_value());
  CHECK_NEAR(hosted->pianoBottom,
             native_ui::EditorSceneLayout{}.contentTop() + f.shell.layout().grid.height, 1e-9);
  CHECK_NEAR(hosted->laneHeight, f.shell.layout().laneTimePlot.height, 1e-9);
  CHECK_NEAR(f.controller.pianoRoll().viewport().keyboardWidth, f.shell.layout().grid.x, 1e-9);

  // The voice browser is a shell sheet now: opening it keeps the shell and its geometry.
  f.controller.showVoicebankBrowser();
  CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  CHECK(f.controller.hostedGrid().has_value());
  CHECK(f.shell.overlayKind(f.controller) == native_ui::design::OverlayKind::VoicebankBrowser);
  // Closing the sheet leaves the shell presenting with the same geometry: there is no other
  // editor surface for it to hand the frame to.
  f.controller.closeVoicebankBrowser();
  CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  CHECK(f.controller.hostedGrid() == hosted);
  CHECK(f.shell.presentedLastFrame());

  // The shell's own "Change voice" presents the browser as the shell's sheet.
  ShellFixture g;
  CHECK(g.shell.prepareFrame(g.controller, 1600.0, 900.0));
  const auto change = g.shell.layout().singerChange;
  CHECK(g.shell.pointerDown(g.controller, press({change.x + 4.0, change.y + 4.0})).hasValue());
  CHECK(g.controller.voicebankBrowserVisible());
  CHECK(g.controller.hostedGrid().has_value());
  CHECK(g.shell.presentedLastFrame());
  CHECK(g.shell.overlayKind(g.controller) == native_ui::design::OverlayKind::VoicebankBrowser);
}

TEST_CASE("a forwarded note drag moves the note, and Escape or a capture loss abandons it") {
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
    // A capture loss mid-drag cancels the drag; the late mouse-up then finds none.
    f.shell.cancelGestures(f.controller);
    CHECK(f.controller.hostedGrid().has_value());
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

TEST_CASE("text fields move into shell space: lyrics with the grid, other fields onto their card") {
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  const auto offset = f.shell.layout().grid.y - native_ui::EditorSceneLayout{}.contentTop();
  const ui::Rect bounds{200.0, 300.0, 80.0, 20.0};
  const auto grid = f.shell.translateTextInput(
      {domain::LyricTokenId{7U}, bounds, U"", native_ui::TextInputAnchor::NoteGrid});
  CHECK_NEAR(grid.logicalBounds.y, bounds.y + offset, 1e-9);
  // The anchor, not the target id, decides: any other field sits on the field the shell draws for
  // it, whatever classic rectangle the controller computed.
  using native_ui::TextInputAnchor;
  for (const auto anchor : {TextInputAnchor::ClassicSurface, TextInputAnchor::BoundedField,
                            TextInputAnchor::Transport, TextInputAnchor::ArrangementField}) {
    const auto placed =
        f.shell.translateTextInput({domain::LyricTokenId{7U}, bounds, U"", anchor});
    const auto expected = native_ui::design::textFieldPlacement(
        anchor == TextInputAnchor::ClassicSurface ? TextInputAnchor::BoundedField : anchor,
        f.shell.layout());
    CHECK_NEAR(placed.logicalBounds.x, expected.input.x, 1e-9);
    CHECK_NEAR(placed.logicalBounds.y, expected.input.y, 1e-9);
    CHECK_NEAR(placed.logicalBounds.width, expected.input.width, 1e-9);
  }
  const auto timeMap = f.shell.translateTextInput(
      {domain::LyricTokenId{7U}, bounds, U"", TextInputAnchor::TimeMapPanel});
  const auto inMap = native_ui::design::timeMapFieldPlacement(
      native_ui::design::timeMapPanelBounds(f.shell.layout().overlay));
  CHECK_NEAR(timeMap.logicalBounds.y, inMap.input.y, 1e-9);
  CHECK_NEAR(timeMap.logicalBounds.x, inMap.input.x, 1e-9);
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

TEST_CASE("a track rename field is an inline field card in the shell") {
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  CHECK(f.controller.selectTrack(f.trackId).hasValue());
  CHECK(f.controller.beginSelectedTrackRename().hasValue());
  CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  CHECK(f.controller.hostedGrid().has_value());
  CHECK(f.shell.overlayKind(f.controller) == native_ui::design::OverlayKind::TextField);
  CHECK(f.lastTextInput.has_value());
  if (!f.lastTextInput) return;
  CHECK(f.lastTextInput->anchor == native_ui::TextInputAnchor::ArrangementField);
  const auto field = native_ui::design::textFieldPlacement(native_ui::TextInputAnchor::ArrangementField,
                                                           f.shell.layout());
  CHECK_NEAR(f.lastTextInput->logicalBounds.y, field.input.y, 1e-9);
  CHECK(f.controller.commitTextComposition(U"Lead").hasValue());
  CHECK(f.session.project().findVocalTrack(f.trackId)->name == "Lead");
  CHECK(f.shell.overlayKind(f.controller) == native_ui::design::OverlayKind::None);
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
  // Change voice opens the voice browser as the shell's own sheet.
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.change-voice", SemanticAction::Activate).hasValue());
  CHECK(f.controller.voicebankBrowserVisible());
  CHECK(f.shell.presentedLastFrame());
  CHECK(f.controller.hostedGrid().has_value());
  CHECK(f.shell.overlayKind(f.controller) == native_ui::design::OverlayKind::VoicebankBrowser);
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

TEST_CASE("an overlap group opens the re-homed detail popover, whose rows lie inside it") {
  using native_ui::SemanticAction;
  using native_ui::SemanticNode;
  using native_ui::design::OverlayKind;
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
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::OverlapDetail);
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  const auto grid = f.shell.layout().grid;
  bool sawPanel = false;
  bool sawRow = false;
  for (const auto& child : f.shell.accessibilityTree().root().children) {
    // The popover is presented as one of the shell's own panels, not the classic detail.
    if (child.id.starts_with("shell.overlay.overlap.")) sawPanel = true;
    if (child.id.starts_with("overlap-note-row.")) {
      sawRow = true;
      // Each row is inside the panel it belongs to.
      for (const auto& panel : f.shell.accessibilityTree().root().children) {
        if (panel.id != "shell.overlay.overlap.panel") continue;
        CHECK(child.bounds.y >= panel.bounds.y - 1e-9);
        CHECK(child.bounds.bottom() <= panel.bounds.bottom() + 1e-9);
      }
    }
    // The covered score keeps no overlap node of its own while the popover is up.
    CHECK(!child.id.starts_with("overlap-group."));
    CHECK(!child.id.starts_with("detail.overlap-group."));
  }
  CHECK(sawPanel);
  CHECK(sawRow);
  static_cast<void>(grid);
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
  // While a sheet is up, the shell elements it covers do nothing.
  CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  f.controller.showAudioSettings();
  CHECK(f.shell.overlayKind(f.controller) == native_ui::design::OverlayKind::AudioSettings);
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
    CHECK(f.lastTextInput && f.lastTextInput->anchor == native_ui::TextInputAnchor::ArrangementField);
    CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
    CHECK(f.shell.overlayKind(f.controller) == native_ui::design::OverlayKind::TextField);
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
    CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
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
    CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
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
    CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
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
  // VOICE opens the VOICE workspace. This host has no Voice Designer, so VOICE says voice design
  // runs in the standalone app and offers the browser, which Change voice also still opens.
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.workspace.voice", SemanticAction::Activate)
            .hasValue());
  CHECK(f.shell.workspace() == native_ui::design::Workspace::Voice);
  CHECK(!f.controller.voicebankBrowserVisible());
  CHECK(f.frame());
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  const auto& voiceNodes = f.shell.accessibilityTree().root().children;
  CHECK(std::any_of(voiceNodes.begin(), voiceNodes.end(), [](const SemanticNode& node) {
    return node.id == "shell.voice.unavailable";
  }));
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.voice.browser", SemanticAction::Activate)
            .hasValue());
  CHECK(f.controller.voicebankBrowserVisible());
}

TEST_CASE("VOICE leaves Undo to the song when the Voice Designer cannot use it") {
  using native_ui::SemanticAction;
  using native_ui::design::Workspace;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  auto* region = f.session.project().findRegion(f.regionId);
  CHECK(region != nullptr);
  if (region == nullptr || region->notes.empty()) return;
  const auto notesBefore = region->notes.size();
  // Make one real edit the song can undo: select the first note and delete it through the editor.
  CHECK(f.frame());
  const auto noteCenter = f.noteCenter();
  CHECK(f.shell.pointerDown(f.controller, press(noteCenter)).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(noteCenter)).hasValue());
  CHECK(!f.session.selection().empty());
  CHECK(f.controller.keyDown(KeyEvent{.key = NativeKey::Delete}).hasValue());
  CHECK(f.session.project().findRegion(f.regionId)->notes.size() == notesBefore - 1U);
  CHECK(f.session.canUndo());

  // This host has no Voice Designer at all, so it cannot own Undo; the shell must not swallow it.
  CHECK(!f.shell.routeUndo(false).has_value());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.workspace.voice", SemanticAction::Activate)
            .hasValue());
  CHECK(f.shell.workspace() == Workspace::Voice);
  CHECK(f.frame());
  CHECK(!f.shell.routeUndo(false).has_value());  // no designer: the song keeps its Undo
  CHECK(!f.shell.routeUndo(true).has_value());
  // A real host declares Command-Z as its own command, exactly as the standalone app does.
  native_ui::design::ShellHostActions actions{};
  actions.applicationShortcut = [](const KeyEvent& event) {
    return event.modifiers.primaryShortcut() && !event.modifiers.alt &&
           (event.key == NativeKey::Z || event.key == NativeKey::Y);
  };
  f.shell.setHostActions(std::move(actions));
  CHECK(!f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Z,
                                                       .modifiers = {.command = true}}));
  // The song's own undo still works and restores the note.
  CHECK(f.session.undo().hasValue());
  CHECK(f.session.project().findRegion(f.regionId)->notes.size() == notesBefore);

  // While SING shows, no workspace claims the command either.
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.workspace.sing", SemanticAction::Activate)
            .hasValue());
  CHECK(f.frame());
  CHECK(!f.shell.routeUndo(false).has_value());
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
  CHECK(geometry.find("contrast")->asString() == "standard");
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
  f.shell.setContrast(native_ui::design::Contrast::High, false);
  f.shell.setReduceMotion(true, false);
  CHECK(f.frame());
  const auto high = native_ui::design::singLayoutEvidence(f.shell, 1.0);
  CHECK(high.find("contrast")->asString() == "high");
  CHECK(high.find("reduceMotion")->asBool());
  CHECK(high.find("language")->asString() == f.shell.language());
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

TEST_CASE("the seam hint and the empty-project line are whole sentences from the string table") {
  using native_ui::design::ScopedShellStrings;
  using native_ui::design::ShellStringTable;
  using native_ui::design::Str;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const auto pseudo = ShellStringTable::pseudoLocalized(0.4);
  const ScopedShellStrings scope{pseudo};
  const auto prompt = native_ui::design::emptyProjectPrompt(0U);
  CHECK(prompt.has_value());
  if (prompt) CHECK(*prompt == std::string_view{pseudo.text(Str::DoubleClickTheGridToWrite)});
  ShellFixture f;
  CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.lane-tab.phonemes", native_ui::SemanticAction::Activate)
            .hasValue());
  const auto noteId = f.session.project().findRegion(f.regionId)->notes.front().id;
  for (const auto alternate : {false, true}) {
    auto state = f.controller.sceneState();
    state.selectedSeam = domain::PhonemeKey{.noteId = noteId, .ordinal = 1U};
    state.seamPreviewConnected = true;
    state.seamPreviewAlternate = alternate;
    CHECK(f.shell.prepareFrame(f.controller, 1600.0, 900.0));
    native_ui::PixelSurface surface{1600U, 900U};
    native_ui::RasterCanvas canvas{surface, 1.0, nullptr};
    native_ui::paint::ScopedTextCapture capture;
    CHECK(f.shell.paint(canvas, f.controller, state, f.controller.playheadTick()));
    const std::string expected{
        pseudo.text(alternate ? Str::SeamHintAlternatePreview : Str::SeamHintBasePreview)};
    const auto& lines = capture.records();
    const auto painted = std::any_of(lines.begin(), lines.end(),
                                     [&expected](const auto& line) { return line.text == expected; });
    if (!painted) throw test::Failure{"the seam hint is not the table's sentence: " + expected};
    // No English fragment is glued onto the translated sentence.
    for (const auto& line : lines)
      CHECK(line.text.find("alternate") == std::string::npos && line.text.find(" base") == std::string::npos);
  }
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
  // The application menu's Undo and Redo ask routeUndo first and fall through to the song only when
  // it answers nothing: mid-drag it answers with a refusal, so the song's history is not stepped.
  for (const auto redo : {false, true}) {
    const auto routed = f.shell.routeUndo(redo);
    CHECK(routed.has_value());
    if (routed) CHECK(!routed->hasValue());
  }
  CHECK(!f.shell.dispatchSemantic(f.controller, node.id, SemanticAction::Increment).hasValue());
  CHECK(!f.shell.dispatchSemantic(f.controller, "shell.workspace.sing", SemanticAction::Activate).hasValue());
  CHECK(f.shell.scroll(f.controller, 0.0, 40.0, end, {}));
  CHECK(f.controller.documentRevision() == revision);
  CHECK(f.shell.pointerUp(f.controller, press(end)).hasValue());
  CHECK(f.controller.documentRevision() == revision + 1U);
  // Once the drag has ended MIX claims nothing, and Undo is the song's again.
  CHECK(!f.shell.routeUndo(false).has_value());
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

TEST_CASE("the header output meter lights from a measured level and shows only the empty scale without one") {
  using native_ui::SemanticAction;
  ShellFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  // A controller whose host counts clip resets, shown by the same shell.
  int hostResets = 0;
  native_ui::NativeEditorController controller{
      f.session, f.factory, f.regionId,
      native_ui::EditorHostCallbacks{.resetOutputClip = [&hostResets] { ++hostResets; }}};
  controller.resize(1600.0, 900.0);
  const auto paintInto = [&](native_ui::PixelSurface& surface) {
    CHECK(f.shell.prepareFrame(controller, 1600.0, 900.0));
    native_ui::RasterCanvas canvas{surface, 1.0, nullptr};
    CHECK(f.shell.paint(canvas, controller, controller.sceneState(), controller.playheadTick()));
    controller.rebuildAccessibilityTree();
    f.shell.rebuildSemantics(controller, controller.sceneState());
  };
  const auto node = [&]() { return findShellNode(f.shell.accessibilityTree().root(), "shell.output-meter"); };
  using Level = native_ui::EditorSceneState::OutputLevel;

  native_ui::PixelSurface empty{1600U, 900U};
  paintInto(empty);
  const auto meter = f.shell.layout().outputMeter;
  CHECK(f.shell.layout().outputMeterVisible);
  // No level: the node says nothing is measured and carries no number.
  const auto* published = node();
  CHECK(published != nullptr);
  if (published == nullptr) return;
  CHECK(published->value == "Not measured");
  CHECK(published->value.find("dBFS") == std::string::npos);
  CHECK(published->bounds.x == meter.x && published->bounds.width == meter.width);

  // Counts pixels that differ between two frames inside and outside a rectangle.
  const auto differences = [](const native_ui::PixelSurface& a, const native_ui::PixelSurface& b,
                              ui::Rect r) {
    std::pair<std::size_t, std::size_t> counts{0U, 0U};
    for (std::uint32_t y = 0U; y < 900U; ++y)
      for (std::uint32_t x = 0U; x < 1600U; ++x) {
        const auto index = static_cast<std::size_t>(y) * 1600U + x;
        if (a.pixels()[index] == b.pixels()[index]) continue;
        const auto px = static_cast<double>(x) + 0.5;
        const auto py = static_cast<double>(y) + 0.5;
        const bool within = px >= r.x && px <= r.right() && py >= r.y && py <= r.bottom();
        ++(within ? counts.first : counts.second);
      }
    return counts;
  };

  // A measured stereo level lights segments inside the meter and nothing outside it.
  controller.setOutputLevel(Level{.peak = {0.5F, 0.25F}, .hold = {0.7F, 0.3F}, .bus = "Master"});
  native_ui::PixelSurface lit{1600U, 900U};
  paintInto(lit);
  const auto [litInside, litOutside] = differences(empty, lit, meter);
  if (litInside < 40U || litOutside != 0U)
    std::cerr << "meter pixels inside=" << litInside << " outside=" << litOutside << '\n';
  CHECK(litInside >= 40U);
  CHECK(litOutside == 0U);
  published = node();
  CHECK(published != nullptr && published->name == "Output level, Master");
  CHECK(published != nullptr && published->value == "L -6.0 dBFS, R -12.0 dBFS");
  CHECK(published != nullptr && std::find(published->actions.begin(), published->actions.end(),
                                          SemanticAction::Activate) == published->actions.end());
  // A louder level lights more of the scale.
  controller.setOutputLevel(Level{.peak = {0.95F, 0.9F}, .hold = {0.95F, 0.9F}, .bus = "Master"});
  native_ui::PixelSurface louder{1600U, 900U};
  paintInto(louder);
  CHECK(differences(empty, louder, meter).first > litInside);

  // A latched clip lights the clip light; clicking it resets the shown flag and the host latch.
  controller.setOutputLevel(
      Level{.peak = {0.5F, 0.25F}, .hold = {0.7F, 0.3F}, .bus = "Master", .clipped = true});
  native_ui::PixelSurface clipped{1600U, 900U};
  paintInto(clipped);
  const ui::Rect clipArea{meter.right() - 16.0, meter.y, 16.0, meter.height};
  CHECK(differences(lit, clipped, clipArea).first > 20U);
  published = node();
  CHECK(published != nullptr && published->value.find("clipped") != std::string::npos);
  CHECK(f.shell.pointerDown(controller, press({meter.right() - 7.0, meter.y + 22.0})).hasValue());
  CHECK(f.shell.pointerUp(controller, press({meter.right() - 7.0, meter.y + 22.0})).hasValue());
  CHECK(hostResets == 1);
  CHECK(controller.sceneState().outputLevel.has_value() && !controller.sceneState().outputLevel->clipped);
  // The accessible route: Activate on the node resets a latched clip too.
  controller.setOutputLevel(
      Level{.peak = {0.5F, 0.25F}, .hold = {0.7F, 0.3F}, .bus = "Master", .clipped = true});
  native_ui::PixelSurface clippedAgain{1600U, 900U};
  paintInto(clippedAgain);
  CHECK(f.shell.dispatchSemantic(controller, "shell.output-meter", SemanticAction::Activate).hasValue());
  CHECK(hostResets == 2);
  CHECK(!controller.sceneState().outputLevel->clipped);

  // A mono bus is one row and one reading.
  controller.setOutputLevel(Level{.peak = {0.5F}, .hold = {0.5F}, .bus = "Master"});
  native_ui::PixelSurface mono{1600U, 900U};
  paintInto(mono);
  published = node();
  CHECK(published != nullptr && published->value == "-6.0 dBFS");
  CHECK(differences(empty, mono, meter).second == 0U);

  // Cleared again (device stopped): exactly the empty scale, and no value.
  controller.setOutputLevel(std::nullopt);
  native_ui::PixelSurface cleared{1600U, 900U};
  paintInto(cleared);
  const auto [clearedInside, clearedOutside] = differences(empty, cleared, meter);
  CHECK(clearedInside == 0U && clearedOutside == 0U);
  published = node();
  CHECK(published != nullptr && published->value == "Not measured");
  CHECK(!f.shell.dispatchSemantic(controller, "shell.output-meter", SemanticAction::Activate).hasValue());

  // A narrow header hides the meter, and its node goes with it.
  CHECK(f.shell.prepareFrame(controller, 1000.0, 700.0));
  CHECK(!f.shell.layout().outputMeterVisible);
  controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(controller, controller.sceneState());
  CHECK(node() == nullptr);
}

// The re-homed overlays (docs/design/SEAM_UI_REDESIGN_CODE_PLAN_2026-09-25.md section 7.6). Each
// was a classic surface that took the whole frame; each is now a shell panel whose controls run the
// same controller commands, whose nodes are published at their hit rectangles, and which covers the
// score while it is open.
namespace {

using native_ui::SemanticAction;
using native_ui::SemanticNode;
using native_ui::SemanticRole;
using native_ui::design::OverlayKind;

// A shell fixture whose host answers every overlay's own callbacks, so each surface can be opened
// through its real command and driven to completion.
struct OverlayFixture final {
  application::ProjectFactory factory{4400U};
  domain::TrackId trackId{};
  domain::RegionId regionId{};
  application::EditorSession session;
  std::uint64_t serial{9100U};
  unsigned plays{0U};
  unsigned edits{0U};
  unsigned selectedReports{0U};
  unsigned diagnosticActions{0U};
  // The host commands the voice browser and the audio settings reach.
  unsigned voicebankRefreshes{0U};
  unsigned installerOpens{0U};
  std::vector<std::string> selectedBanks;
  std::vector<authoring::AudioSettings> appliedAudio;
  bool refuseAudio{false};
  native_ui::NativeEditorController controller;
  SingShell shell;
  std::optional<native_ui::TextInputRequest> lastTextInput;

  OverlayFixture()
      : session(makeProject()),
        controller{session, factory, regionId,
                   native_ui::EditorHostCallbacks{
                      .beginTextInput = [this](const native_ui::TextInputRequest& request) {
                        lastTextInput = shell.translateTextInput(request);
                      },
                      .endTextInput = [this] { shell.textInputEnded(); },
                       .loadSampleMicroscope =
                           [](domain::PhonemeKey) -> core::Result<native_ui::SampleMicroscopeData> {
                            return native_ui::SampleMicroscopeData{
                                unit(),
                                voicebank::AudioBuffer{.sampleRate = 48000U,
                                                       .channels = 1U,
                                                       .interleaved = test::support::sineWave(
                                                           48000U, 220.0, 0.05)},
                                "Captured decision 1: source-boundary proxy."};
                          },
                       .microscopeUnitChanged =
                           [this](domain::PhonemeKey, const voicebank::Unit&) {
                             ++edits;
                             return core::success();
                           },
                       .playMicroscopeSample =
                           [this](const voicebank::Unit&, const voicebank::AudioBuffer&) {
                             ++plays;
                             return core::success();
                           },
                       .selectVoicebank =
                           [this](std::string_view id, std::string_view, std::string_view) {
                             selectedBanks.emplace_back(id);
                             return core::success();
                           },
                       .refreshVoicebanks =
                           [this] {
                             ++voicebankRefreshes;
                             return core::success();
                           },
                       .openVoicebankInstaller =
                           [this] {
                             ++installerOpens;
                             return core::success();
                           },
                       .diagnosticAction =
                           [this](const authoring::Diagnostic&, authoring::DiagnosticAction) {
                             ++diagnosticActions;
                             return core::success();
                           },
                       .selectSupportReport = [this](std::size_t) {
                         ++selectedReports;
                         return core::success();
                       },
                       .applyAudioSettings =
                           [this](authoring::AudioSettings requested) -> core::Result<void> {
                             if (refuseAudio)
                               return core::failure(core::ErrorCode::Unsupported,
                                                    "The output device refused this format");
                             appliedAudio.push_back(requested);
                             return core::success();
                           },
                       .reviewPhonemeBindings = [this]() -> core::Result<authoring::PhonemeBindingReview> {
                         return core::success(authoring::PhonemeBindingReview{
                             .regionId = regionId,
                             .warnings = {phonemizer::Warning{.message = "One retained edit"}}});
                       },
                       .rebindPhonemeOverride =
                           [this](const domain::PhonemeOverride&, domain::PhonemeKey, std::string_view) {
                             ++edits;
                             return core::success();
                           },
                   }} {
    controller.resize(1600.0, 900.0);
    shell.activate({}, DesignPreferences{.mode = DesignMode::Emo});
  }

  static voicebank::Unit unit() {
    return test::support::makeUnit("voice-a", {"a"}, "audio/a.wav", 60U, voicebank::UnitKind::Cv,
                                   2400U);
  }
  domain::Project makeProject() {
    auto project = factory.createProject("Overlays");
    trackId = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, trackId, "Phrase", time::Tick{0}, time::Tick{7680});
    auto [lyric, note] =
        factory.makeNote(time::Tick{960}, time::Tick{960}, 72U, U"a", domain::Language::Japanese);
    // An overlap: a second note sharing the first note's row and time, so a group and its badge
    // exist for the overlap detail popover.
    auto [secondLyric, secondNote] =
        factory.makeNote(time::Tick{960}, time::Tick{960}, 72U, U"i", domain::Language::Japanese);
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    region->lyrics.push_back(std::move(secondLyric));
    region->notes.push_back(std::move(secondNote));
    return project;
  }

  bool frame(double width = 1600.0, double height = 900.0) {
    if (!shell.prepareFrame(controller, width, height)) return false;
    // The frame's pixels are kept, so a test can compare what two states painted.
    surface = native_ui::PixelSurface{static_cast<std::uint32_t>(width),
                                      static_cast<std::uint32_t>(height)};
    native_ui::RasterCanvas canvas{surface, 1.0, nullptr};
    return shell.paint(canvas, controller, controller.sceneState(), controller.playheadTick());
  }
  native_ui::PixelSurface surface;

  // The last frame's pixels inside a rectangle, row by row.
  std::vector<std::uint32_t> pixelsIn(ui::Rect r) const {
    std::vector<std::uint32_t> out;
    const auto w = static_cast<std::int64_t>(surface.width());
    for (auto y = static_cast<std::int64_t>(r.y); y < static_cast<std::int64_t>(r.bottom()); ++y)
      for (auto x = static_cast<std::int64_t>(r.x); x < static_cast<std::int64_t>(r.right()); ++x)
        if (x >= 0 && y >= 0 && x < w && y < static_cast<std::int64_t>(surface.height()))
          out.push_back(surface.pixels()[static_cast<std::size_t>(y * w + x)]);
    return out;
  }

  const SemanticNode* node(std::string_view id, double width = 1600.0, double height = 900.0) {
    controller.rebuildAccessibilityTree();
    shell.rebuildSemantics(controller, controller.sceneState());
    static_cast<void>(width);
    static_cast<void>(height);
    return findShellNode(shell.accessibilityTree().root(), id);
  }

  // Every node the shell publishes that belongs to the presented overlay.
  std::vector<const SemanticNode*> overlayNodes(double width = 1600.0, double height = 900.0) {
    controller.rebuildAccessibilityTree();
    shell.rebuildSemantics(controller, controller.sceneState());
    std::vector<const SemanticNode*> out;
    const auto kind = shell.overlayKind(controller);
    if (kind == OverlayKind::None) return out;
    const auto collect = [&](const SemanticNode& parent, const auto& self) -> void {
      for (const auto& child : parent.children) {
        out.push_back(&child);
        self(child, self);
      }
    };
    collect(shell.accessibilityTree().root(), collect);
    static_cast<void>(width);
    static_cast<void>(height);
    return out;
  }

  // The id the shell reports as focused now, or an empty string.
  std::string focusedId() {
    controller.rebuildAccessibilityTree();
    shell.rebuildSemantics(controller, controller.sceneState());
    const auto* focused = shell.accessibilityTree().focusedNode();
    return focused == nullptr ? std::string{} : focused->id;
  }

  // The centre of the shell's own overlap badge for the first overlapping note, which is what the
  // creator clicks and what the detail popover anchors to.
  ui::Point badgeCenter(double width = 1600.0, double height = 900.0) {
    static_cast<void>(width);
    static_cast<void>(height);
    const auto& l = shell.layout();
    for (const auto& note : controller.pianoRoll().visibleNotes()) {
      if (!note.drawsOverlapIndicator) continue;
      const auto painted = ui::Rect{note.bounds.x, note.bounds.y + l.grid.y, note.bounds.width,
                                    note.bounds.height};
      auto badge = ui::Rect{std::min(painted.right() + 3.0, l.grid.right() - 30.0), painted.y - 2.0,
                            28.0, 18.0};
      if (badge.y < l.grid.y) badge.y = l.grid.y;
      return {badge.x + badge.width * 0.5, badge.y + badge.height * 0.5};
    }
    throw test::Failure{"no overlap badge"};
  }
};

// The overlays all share the same contract: the shell presents them, their nodes match their hit
// rectangles, the score under them is neither published nor editable, Escape closes them and focus
// returns, and they stay usable at the 480x320 minimum and at 1600x900.
void checkOverlayContract(OverlayFixture& f, std::string_view panelPrefix,
                          std::vector<std::string> required, std::vector<std::string> optional,
                          std::string_view opener) {
  for (const auto [width, height] : {std::pair{480.0, 320.0}, std::pair{720.0, 480.0},
                                     std::pair{1100.0, 700.0}, std::pair{1100.0, 720.0},
                                     std::pair{1600.0, 900.0}}) {
    CHECK(f.frame(width, height));
    CHECK(f.shell.overlayKind(f.controller) != OverlayKind::None);
    // One tree snapshot for the whole check: the nodes are copies, so a lookup never invalidates an
    // earlier one.
    f.controller.rebuildAccessibilityTree();
    f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
    const auto root = f.shell.accessibilityTree().root();
    const auto lookup = [&root](std::string_view id) -> std::optional<SemanticNode> {
      if (const auto* node = findShellNode(root, id); node != nullptr) return *node;
      return std::nullopt;
    };
    const auto fail = [&](const std::string& id, std::string_view why) {
      throw test::Failure{"overlay control " + id + " at " + std::to_string(width) + "x" +
                          std::to_string(height) + ": " + std::string{why}};
    };
    // The covered score is not published: no note node, no lane node, no timeline.
    if (lookup("timeline").has_value()) fail("timeline", "is published under the overlay");
    if (lookup("shell.lane").has_value()) fail("shell.lane", "is published under the overlay");
    const auto noteId =
        "note." + f.session.project().findRegion(f.regionId)->notes.front().id.toString();
    if (lookup(noteId).has_value()) fail(noteId, "is published under the overlay");
    const auto panel = lookup(std::string{panelPrefix} + "panel");
    if (!panel.has_value()) fail(std::string{panelPrefix} + "panel", "is not published");
    if (!panel.has_value()) continue;
    CHECK(panel->bounds.x >= 0.0 && panel->bounds.y >= 0.0);
    CHECK(panel->bounds.right() <= width + 0.5 && panel->bounds.bottom() <= height + 0.5);
    std::vector<ui::Rect> placed;
    const auto checkControl = [&](const std::string& id, bool mustExist) {
      const auto control = lookup(id);
      if (!control.has_value()) {
        if (mustExist) fail(id, "is not published");
        return;
      }
      if (!(control->bounds.width > 0.0 && control->bounds.height > 0.0))
        fail(id, "has no size");
      if (!(control->bounds.x >= panel->bounds.x - 0.5 &&
            control->bounds.y >= panel->bounds.y - 0.5))
        fail(id, "is above or left of its panel (" + std::to_string(control->bounds.x) + "," +
                     std::to_string(control->bounds.y) + " vs " +
                     std::to_string(panel->bounds.x) + "," + std::to_string(panel->bounds.y) + ")");
      if (!(control->bounds.right() <= panel->bounds.right() + 0.5 &&
            control->bounds.bottom() <= panel->bounds.bottom() + 0.5))
        fail(id, "is below or right of its panel");
      for (const auto& other : placed) {
        const auto disjoint = control->bounds.right() <= other.x + 0.001 ||
                              other.right() <= control->bounds.x + 0.001 ||
                              control->bounds.bottom() <= other.y + 0.001 ||
                              other.bottom() <= control->bounds.y + 0.001;
        if (!disjoint) fail(id, "overlaps another control");
      }
      placed.push_back(control->bounds);
    };
    for (const auto& id : required) checkControl(id, true);
    for (const auto& id : optional) checkControl(id, false);
    // No editing key reaches the covered score: the document and the selection are unchanged after
    // keys that would delete or move notes if they fell through.
    const auto revision = f.controller.documentRevision();
    const auto selected = f.session.selection().noteIds();
    for (const auto& key : {KeyEvent{.key = NativeKey::Delete},
                            KeyEvent{.key = NativeKey::Backspace},
                            KeyEvent{.key = NativeKey::Up}, KeyEvent{.key = NativeKey::D}})
      CHECK(f.shell.handleShellKey(f.controller, key));
    CHECK(f.controller.documentRevision() == revision);
    CHECK(f.session.selection().noteIds() == selected);
    CHECK(f.shell.overlayPresented(f.controller));
  }
  // Escape closes it (a surface with an inner page steps back first) and returns focus to the
  // control that opened it.
  CHECK(f.frame());
  for (int i = 0; i < 3 && f.shell.overlayKind(f.controller) != OverlayKind::None; ++i)
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  const auto* focused = f.shell.accessibilityTree().focusedNode();
  if (!opener.empty()) {
    if (focused == nullptr || focused->id != opener)
      throw test::Failure{std::string{"Escape returned focus to "} +
                          (focused == nullptr ? std::string{"nothing"} : focused->id) +
                          " instead of " + std::string{opener}};
  }
}

}  // namespace

TEST_CASE("the sample microscope is a shell sheet whose plots, pager and close stay usable") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  const auto key = domain::PhonemeKey{f.session.project().findRegion(f.regionId)->notes.front().id, 0U};
  CHECK(f.controller.openSampleMicroscope(key).hasValue());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::SampleMicroscope);
  // The controller's own model backs the surface the shell presents.
  CHECK(f.controller.sampleMicroscopeOpen());
  checkOverlayContract(f, "shell.overlay.microscope.", {"microscope.close", "microscope.details"},
                       {"microscope.previous", "microscope.next"}, "");

  // Details opens the captured lines through the controller's own command, and its pager moves the
  // controller's page; a second press returns to the plots. The contract above closed the
  // microscope, so it is opened again here.
  CHECK(f.controller.openSampleMicroscope(key).hasValue());
  CHECK(f.frame());
  const auto* details = f.node("shell.overlay.microscope.panel");
  CHECK(details != nullptr);
  const auto detailsBounds = f.node("microscope.details")->bounds;
  CHECK(f.shell.pointerDown(f.controller, press({detailsBounds.x + 4.0, detailsBounds.y + 4.0})).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press({detailsBounds.x + 4.0, detailsBounds.y + 4.0})).hasValue());
  CHECK(f.controller.sceneState().sampleMicroscope->detailsVisible);
  CHECK(f.frame());
  CHECK(f.node("microscope.previous") != nullptr);
  CHECK(f.node("microscope.next") != nullptr);
  // Close is the controller's own close: the model is gone and the shell stops presenting.
  CHECK(f.frame());
  const auto closeBounds = f.node("microscope.close")->bounds;
  CHECK(f.shell.pointerDown(f.controller, press({closeBounds.x + 4.0, closeBounds.y + 4.0})).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press({closeBounds.x + 4.0, closeBounds.y + 4.0})).hasValue());
  CHECK(!f.controller.sampleMicroscopeOpen());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
}

TEST_CASE("the microscope's Details button says what it will do, and D and Escape work as before") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  const auto key = domain::PhonemeKey{f.session.project().findRegion(f.regionId)->notes.front().id, 0U};
  CHECK(f.controller.openSampleMicroscope(key).hasValue());
  CHECK(f.frame());
  const auto button = f.node("microscope.details")->bounds;
  CHECK(f.node("microscope.details")->name == "Details");
  const auto waveformPixels = f.pixelsIn(button);
  // D opens the details page, the node says the button now returns to the waveform, and the
  // painted label changes with it.
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::D}));
  CHECK(f.controller.sceneState().sampleMicroscope->detailsVisible);
  CHECK(f.frame());
  CHECK(f.node("microscope.details")->name == "Waveform");
  CHECK(f.node("microscope.details")->bounds.x == button.x);
  CHECK(f.pixelsIn(button) != waveformPixels);
  // D again returns to the waveform.
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::D}));
  CHECK(!f.controller.sceneState().sampleMicroscope->detailsVisible);
  CHECK(f.frame());
  CHECK(f.pixelsIn(button) == waveformPixels);
  // Escape on the details page returns to the waveform first and keeps the microscope open; the
  // next Escape closes it.
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::D}));
  CHECK(f.frame());
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(f.controller.sampleMicroscopeOpen());
  CHECK(!f.controller.sceneState().sampleMicroscope->detailsVisible);
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::SampleMicroscope);
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(!f.controller.sampleMicroscopeOpen());
}

TEST_CASE("the phoneme review popover anchors to the lane and runs the review's own actions") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  CHECK(f.controller.openPhonemeReview().hasValue());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::PhonemeReview);
  checkOverlayContract(f, "shell.overlay.phoneme.",
                       {"phoneme.review.action.0", "phoneme.review.action.1",
                        "phoneme.review.action.2", "phoneme.review.action.3",
                        "phoneme.review.action.4", "phoneme.review.action.5"},
                       {}, "shell.lane.review");
  // The open action through the controller still opens it (the classic path a host keeps).
  CHECK(f.controller.openPhonemeReview().hasValue());
  CHECK(f.frame());
  const auto closeBounds = f.node("phoneme.review.action.2")->bounds;
  CHECK(f.shell.pointerDown(f.controller, press({closeBounds.x + 4.0, closeBounds.y + 4.0})).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press({closeBounds.x + 4.0, closeBounds.y + 4.0})).hasValue());
  CHECK(!f.controller.sceneState().phonemeReview.visible);
}

TEST_CASE("the time map is a shell popover whose rows and eight actions run the map's commands") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  CHECK(f.controller.openTimeMapPanel().hasValue());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::TimeMap);
  checkOverlayContract(f, "shell.overlay.time-map.",
                       {"time-map-action.0", "time-map-action.1", "time-map-action.2",
                        "time-map-action.3", "time-map-action.4", "time-map-action.5",
                        "time-map-action.6", "time-map-action.7"},
                       {"time-map-row.0"}, "shell.ruler.time-map");

  // The ruler's own opener opens it too, and one of its rows selects through the controller.
  CHECK(f.controller.openTimeMapPanel().hasValue());
  CHECK(f.frame());
  const auto* row = f.node("time-map-row.0");
  CHECK(row != nullptr);
  const auto before = f.controller.sceneState().timeMapSelectedRow;
  CHECK(f.controller.selectTimeMapRow(0U).hasValue());
  CHECK(f.controller.sceneState().timeMapSelectedRow == before);
  // Add tempo opens the event field inside the popover itself: the map stays up, its rows and
  // actions are disabled while the field is open, and the host's input client sits on the field.
  const auto added = f.controller.timeMapPanelAction(6U);
  if (!added) throw test::Failure{"add tempo refused: " + added.error().message};
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::TimeMap);
  const auto field = f.controller.textFieldView();
  CHECK(field.kind == native_ui::NativeEditorController::TextFieldView::Kind::TimeMap);
  const auto* input = f.node(field.inputId);
  CHECK(input != nullptr);
  CHECK(f.lastTextInput.has_value());
  if (input == nullptr || !f.lastTextInput) return;
  CHECK(input->role == native_ui::SemanticRole::TextField);
  CHECK(f.lastTextInput->anchor == native_ui::TextInputAnchor::TimeMapPanel);
  CHECK_NEAR(f.lastTextInput->logicalBounds.x, input->bounds.x, 1e-9);
  CHECK_NEAR(f.lastTextInput->logicalBounds.y, input->bounds.y, 1e-9);
  CHECK_NEAR(f.lastTextInput->logicalBounds.width, input->bounds.width, 1e-9);
  CHECK(f.focusedId() == field.inputId);
  const auto* disabledRow = f.node("time-map-row.0");
  CHECK(disabledRow != nullptr && !disabledRow->enabled);
  // Keys are the input client's (the host passes them to the controller's text handling).
  CHECK(!f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Enter}));
  // Escape cancels the field and leaves the map open, as the classic panel did.
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(!f.controller.textInputActive());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::TimeMap);
  // Through the host's value path: the new event's tick, then its tempo, one undoable edit.
  CHECK(f.controller.timeMapPanelAction(6U).hasValue());
  CHECK(f.frame());
  const auto revision = f.controller.documentRevision();
  const auto tickResult =
      f.shell.setControllerValue(f.controller, f.controller.textFieldView().inputId, "1920");
  if (!tickResult) throw test::Failure{"tick refused: " + tickResult.error().message};
  CHECK(f.frame());
  CHECK(f.controller.textFieldView().kind ==
        native_ui::NativeEditorController::TextFieldView::Kind::TimeMap);
  const auto bpmResult =
      f.shell.setControllerValue(f.controller, f.controller.textFieldView().inputId, "90");
  if (!bpmResult) throw test::Failure{"tempo refused: " + bpmResult.error().message};
  CHECK(f.controller.documentRevision() != revision);
  const auto& tempos = f.session.project().tempoMap().events();
  CHECK(std::any_of(tempos.begin(), tempos.end(),
                    [](const auto& event) { return event.tick == time::Tick{1920}; }));
  CHECK(f.session.undo());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::TimeMap);
}

TEST_CASE("an open overlay holds the keyboard: its first control is focused and Tab walks it") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  CHECK(f.controller.openTimeMapPanel().hasValue());
  CHECK(f.frame());
  const auto isControl = [](const std::string& id) {
    return id.starts_with("time-map-row.") || id.starts_with("time-map-action.");
  };
  // Opening the popover gives its first control (the first event row) the keyboard.
  const auto first = f.focusedId();
  if (first != "time-map-row.0") throw test::Failure{"opened time map focuses " + first};
  // Tab moves through the card's own controls, never out of it and never onto the card itself.
  std::set<std::string> visited{first};
  for (int i = 0; i < 3; ++i) {
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Tab}));
    const auto id = f.focusedId();
    if (!isControl(id)) throw test::Failure{"Tab moved focus to " + id};
    visited.insert(id);
  }
  CHECK(visited.size() == 4U);
  // Enter on a focused action runs that action only: Refresh keeps the list and opens no field.
  for (int i = 0; i < 32 && f.focusedId() != "time-map-action.4"; ++i)
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Tab}));
  CHECK(f.focusedId() == "time-map-action.4");
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Enter}));
  CHECK(f.controller.sceneState().timeMapVisible);
  CHECK(!f.controller.sceneState().timeMapInputActive);
  // Shift-Tab walks back; Enter on a focused row selects it and edits the selected event, as the
  // classic panel bound Enter.
  for (int i = 0; i < 32 && f.focusedId() != "time-map-row.0"; ++i)
    CHECK(f.shell.handleShellKey(f.controller,
                                 KeyEvent{.key = NativeKey::Tab, .modifiers = {.shift = true}}));
  CHECK(f.focusedId() == "time-map-row.0");
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Enter}));
  CHECK(f.controller.sceneState().timeMapInputActive);
}

TEST_CASE("every overlay control acts through the host's accessibility path") {
  // A host sends an action for a non-shell id to dispatchController. Each overlay control must
  // take focus there and run its own command, whether or not the controller publishes the id.
  const auto focusEach = [](OverlayFixture& f, const std::vector<std::string>& ids) {
    for (const auto& id : ids) {
      const auto focused = f.shell.dispatchController(f.controller, id, SemanticAction::SetFocus);
      if (!focused) throw test::Failure{"focus " + id + ": " + focused.error().message};
      if (f.focusedId() != id) throw test::Failure{"focus " + id + " reports " + f.focusedId()};
    }
  };
  const auto activate = [](OverlayFixture& f, const std::string& id) {
    const auto result = f.shell.dispatchController(f.controller, id, SemanticAction::Activate);
    if (!result) throw test::Failure{"activate " + id + ": " + result.error().message};
  };
  {
    OverlayFixture f;
    if (!native_ui::paint::vectorBackendAvailable()) return;
    CHECK(f.frame());
    CHECK(f.controller.editTempo(f.controller.documentRevision(), time::Tick{1920}, 90.0).hasValue());
    CHECK(f.controller.openTimeMapPanel().hasValue());
    CHECK(f.frame());
    std::vector<std::string> ids;
    for (std::size_t i = 0U; i < f.controller.sceneState().timeMapRows.size(); ++i)
      ids.push_back("time-map-row." + std::to_string(i));
    for (std::size_t i = 0U; i < 8U; ++i) ids.push_back("time-map-action." + std::to_string(i));
    focusEach(f, ids);
    activate(f, "time-map-row.1");
    CHECK(f.controller.sceneState().timeMapSelectedRow == std::optional<std::size_t>{1U});
    activate(f, "time-map-action.4");
    CHECK(f.controller.sceneState().timeMapVisible);
    // A row has no editable value, and a value sent to it changes nothing.
    CHECK(!f.shell.setControllerValue(f.controller, "time-map-row.0", "120").hasValue());
    activate(f, "time-map-action.6");
    CHECK(f.controller.sceneState().timeMapInputActive);
    f.controller.cancelTextComposition();
    CHECK(f.frame());
    // VoiceOver closes the time map through its own Close.
    activate(f, "time-map-action.5");
    CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
    CHECK(!f.controller.sceneState().timeMapVisible);
  }
  {
    OverlayFixture f;
    CHECK(f.frame());
    const auto key =
        domain::PhonemeKey{f.session.project().findRegion(f.regionId)->notes.front().id, 0U};
    CHECK(f.controller.openSampleMicroscope(key).hasValue());
    CHECK(f.frame());
    focusEach(f, {"microscope.close", "microscope.details"});
    activate(f, "microscope.details");
    CHECK(f.controller.sceneState().sampleMicroscope->detailsVisible);
    CHECK(f.frame());
    focusEach(f, {"microscope.previous", "microscope.next"});
    activate(f, "microscope.close");
    CHECK(!f.controller.sampleMicroscopeOpen());
  }
  {
    OverlayFixture f;
    CHECK(f.frame());
    CHECK(f.controller.openPhonemeReview().hasValue());
    CHECK(f.frame());
    std::vector<std::string> ids;
    for (std::size_t i = 0U; i < 6U; ++i) ids.push_back("phoneme.review.action." + std::to_string(i));
    focusEach(f, ids);
    activate(f, "phoneme.review.action.2");
    CHECK(!f.controller.sceneState().phonemeReview.visible);
  }
  {
    OverlayFixture f;
    CHECK(f.frame());
    f.controller.setRecoverySupportView(native_ui::RecoverySupportView{
        .visible = true,
        .mode = native_ui::RecoverySupportMode::Reports,
        .items = {{.name = "report-a", .detail = "crash marker", .bytes = 4096U},
                  {.name = "report-b", .detail = "first run", .bytes = 8192U}},
        .reportCount = 2U,
        .status = "Two owned reports",
    });
    CHECK(f.frame());
    focusEach(f, {"support.track.previous", "support.track.next", "support.item.0",
                  "support.item.1"});
    activate(f, "support.item.1");
    CHECK(f.selectedReports == 1U);
  }
  {
    OverlayFixture f;
    CHECK(f.frame());
    CHECK(f.controller.openOverlapDetail(0U).hasValue());
    CHECK(f.frame());
    const auto members = f.controller.sceneState().overlapDetail->members.size();
    std::vector<std::string> ids;
    for (std::size_t i = 0U; i < members; ++i) ids.push_back("overlap-note-row." + std::to_string(i));
    focusEach(f, ids);
    activate(f, "overlap-note-row.1");
    const auto& detail = f.controller.sceneState().overlapDetail;
    CHECK(detail.has_value() && detail->members[1].selected);
    if (detail.has_value()) CHECK(f.session.selection().contains(detail->members[1].noteId));
  }
}

TEST_CASE("the painted time-map and review openers open their surfaces and never reach the score") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  for (const auto [width, height] : {std::pair{1600.0, 900.0}, std::pair{1100.0, 720.0}}) {
    CHECK(f.frame(width, height));
    const auto ruler = f.shell.layout().rulerTimeMapButton;
    if (ruler.width <= 0.0) throw test::Failure{"no time-map opener at this size"};
    const auto playhead = f.controller.playheadTick();
    const auto revision = f.controller.documentRevision();
    const ui::Point r{ruler.x + ruler.width * 0.5, ruler.y + ruler.height * 0.5};
    CHECK(f.shell.pointerDown(f.controller, press(r)).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press(r)).hasValue());
    CHECK(f.controller.sceneState().timeMapVisible);
    CHECK(f.controller.playheadTick() == playhead);
    CHECK(f.controller.documentRevision() == revision);
    CHECK(f.frame(width, height));
    CHECK(f.shell.overlayKind(f.controller) == OverlayKind::TimeMap);
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
    CHECK(f.focusedId() == "shell.ruler.time-map");

    CHECK(f.frame(width, height));
    const auto review = f.shell.layout().laneReviewButton;
    if (review.width <= 0.0) throw test::Failure{"no review opener at this size"};
    const ui::Point v{review.x + review.width * 0.5, review.y + review.height * 0.5};
    CHECK(f.shell.pointerDown(f.controller, press(v)).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press(v)).hasValue());
    CHECK(f.controller.sceneState().phonemeReview.visible);
    CHECK(f.frame(width, height));
    CHECK(f.shell.overlayKind(f.controller) == OverlayKind::PhonemeReview);
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
    CHECK(!f.controller.sceneState().phonemeReview.visible);
    CHECK(f.focusedId() == "shell.lane.review");
  }
}

TEST_CASE("modified keys over an overlay are application commands, never the overlay's keys") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  // The standalone host declares Command-N, Command-O, Command-S and friends as its own commands.
  std::vector<KeyEvent> routed;
  f.shell.setHostActions(native_ui::design::ShellHostActions{
      .applicationShortcut = [&routed](const KeyEvent& event) {
        if (!event.modifiers.primaryShortcut() || event.key != NativeKey::N) return false;
        routed.push_back(event);
        return true;
      }});
  CHECK(f.frame());
  // A removable event, selected, so a Command-Delete that fell through would remove it.
  CHECK(f.controller.editTempo(f.controller.documentRevision(), time::Tick{1920}, 90.0).hasValue());
  CHECK(f.controller.openTimeMapPanel().hasValue());
  CHECK(f.frame());
  {
    const auto rows = f.controller.sceneState().timeMapRows;
    for (std::size_t i = 0U; i < rows.size(); ++i)
      if (rows[i].find("1920") != std::string::npos) CHECK(f.controller.selectTimeMapRow(i).hasValue());
  }
  const auto events = f.controller.sceneState().timeMapRows;
  const auto revision = f.controller.documentRevision();
  // Command-N and Command-Shift-N go to the host (New Project), and add no tempo or meter event.
  CHECK(!f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::N, .modifiers = {.command = true}}));
  CHECK(!f.shell.handleShellKey(
      f.controller, KeyEvent{.key = NativeKey::N, .modifiers = {.shift = true, .command = true}}));
  CHECK(routed.size() == 2U);
  CHECK(!f.controller.sceneState().timeMapInputActive);
  // Command-Delete and Option-R are not host commands: they stop at the card and do nothing.
  for (const auto& key : {KeyEvent{.key = NativeKey::Delete, .modifiers = {.command = true}},
                          KeyEvent{.key = NativeKey::Backspace, .modifiers = {.alt = true}},
                          KeyEvent{.key = NativeKey::R, .modifiers = {.alt = true}}})
    CHECK(f.shell.handleShellKey(f.controller, key));
  CHECK(f.controller.documentRevision() == revision);
  CHECK(f.controller.sceneState().timeMapRows == events);
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::TimeMap);
  // The plain keys are still the time map's own: N adds a tempo event through its field.
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::N}));
  CHECK(f.controller.sceneState().timeMapInputActive);
}

TEST_CASE("the recovery support sheet lists the host's reports and selects one through the panel") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  f.controller.setRecoverySupportView(native_ui::RecoverySupportView{
      .visible = true,
      .mode = native_ui::RecoverySupportMode::Reports,
      .items = {{.name = "report-a", .detail = "crash marker", .bytes = 4096U},
                {.name = "report-b", .detail = "first run", .bytes = 8192U}},
      .reportCount = 2U,
      .status = "Two owned reports",
  });
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::RecoverySupport);
  checkOverlayContract(f, "shell.overlay.support.", {"support.track.previous", "support.track.next"},
                       {"support.item.0", "support.item.1"}, "shell.settings");
  // Selecting a report is the controller's own command, reported by the host callback.
  f.controller.setRecoverySupportView(native_ui::RecoverySupportView{
      .visible = true,
      .mode = native_ui::RecoverySupportMode::Reports,
      .items = {{.name = "report-a", .detail = "crash marker", .bytes = 4096U},
                {.name = "report-b", .detail = "first run", .bytes = 8192U}},
      .reportCount = 2U,
      .status = "Two owned reports",
  });
  CHECK(f.frame());
  const auto item = f.node("support.item.1")->bounds;
  CHECK(f.shell.pointerDown(f.controller, press({item.x + 4.0, item.y + 4.0})).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press({item.x + 4.0, item.y + 4.0})).hasValue());
  CHECK(f.selectedReports == 1U);
  CHECK(f.controller.sceneState().recoverySupport.items[1].selected);
}

TEST_CASE("the overlap detail popover anchors to the +N badge and selects a member") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  // The note's +N badge is a shell control when the group hides a member; activating it opens the
  // same controller state the classic overlap group's own action sets.
  CHECK(f.controller.openOverlapDetail(0U).hasValue());
  CHECK(f.controller.sceneState().overlapDetail.has_value());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::OverlapDetail);
  const auto members = f.controller.sceneState().overlapDetail->members.size();
  CHECK(members >= 2U);
  std::vector<std::string> rows;
  for (std::size_t i = 0U; i < members; ++i) rows.push_back("overlap-note-row." + std::to_string(i));
  checkOverlayContract(f, "shell.overlay.overlap.", rows, {}, "");
  // A row selects its member through the controller and marks it in the popover.
  CHECK(f.controller.openOverlapDetail(0U).hasValue());
  CHECK(f.frame());
  const auto row = f.node("overlap-note-row.1")->bounds;
  CHECK(f.shell.pointerDown(f.controller, press({row.x + 4.0, row.y + 4.0})).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press({row.x + 4.0, row.y + 4.0})).hasValue());
  const auto& detail = f.controller.sceneState().overlapDetail;
  CHECK(detail.has_value());
  if (detail.has_value()) {
    CHECK(detail->members[1].selected);
    CHECK(!detail->members[0].selected);
    CHECK(f.session.selection().contains(detail->members[1].noteId));
  }
}

TEST_CASE("overlap popover rows never extend below the card, however large the group") {
  OverlayFixture f;
  const auto overlay = native_ui::design::makeOverlapDetailOverlay();
  native_ui::EditorSceneState state;
  native_ui::EditorSceneState::OverlapDetail detail{.groupIndex = 0U};
  for (int i = 0; i < 12; ++i) detail.members.push_back({.lyric = "a", .midiKey = 60U});
  state.overlapDetail = detail;
  for (const auto [width, height] : {std::pair{480.0, 320.0}, std::pair{720.0, 480.0},
                                     std::pair{1100.0, 720.0}, std::pair{1600.0, 900.0}}) {
    const auto layout = native_ui::design::solveSingLayout(width, height, false);
    const auto panel = overlay->panel(f.controller, state, layout, layout.overlay);
    CHECK(panel.width > 0.0);
    const auto controls = overlay->controls(f.controller, state, layout, panel);
    CHECK(!controls.empty());
    for (const auto& control : controls) {
      if (control.bounds.bottom() > panel.bottom() - 4.0 + 1e-9)
        throw test::Failure{control.id + " ends " +
                            std::to_string(control.bounds.bottom() - panel.bottom()) +
                            " pt below its card at " + std::to_string(width) + "x" +
                            std::to_string(height)};
      CHECK(control.bounds.y >= panel.y);
    }
  }
}

TEST_CASE("the diagnostics toast and popover present the status diagnostics as a shell surface") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  authoring::Diagnostic issue{.code = "MEDIA_MISSING",
                              .severity = authoring::DiagnosticSeverity::Warning,
                              .messageKey = "media.missing",
                              .actions = {authoring::DiagnosticAction::RelinkMedia,
                                          authoring::DiagnosticAction::CopyDiagnostic}};
  f.controller.setDiagnostics({issue});
  CHECK(f.frame());
  // The toast is above the status bar and the DIAGNOSTICS opener sits beside it.
  const auto* toast = f.node("shell.diagnostics.toast");
  const auto* open = f.node("shell.diagnostics.open");
  CHECK(toast != nullptr);
  CHECK(open != nullptr);
  if (open == nullptr) return;
  CHECK(open->bounds.y < f.shell.layout().status.y);
  CHECK(open->role == SemanticRole::Button);
  // Activating it opens the popover, whose action is the controller's own recovery action.
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.diagnostics.open", SemanticAction::Activate)
            .hasValue());
  CHECK(f.shell.diagnosticsOpen());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::Diagnostics);
  const auto* panel = f.node("shell.overlay.diagnostics.panel");
  CHECK(panel != nullptr);
  CHECK(f.node("timeline") == nullptr);
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(!f.shell.diagnosticsOpen());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
  // The recovery action on the panel reaches the host's handler through the controller.
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.diagnostics.open", SemanticAction::Activate)
            .hasValue());
  CHECK(f.frame());
  const auto* action = f.node("diagnostic-action.0.RELINK_MEDIA");
  CHECK(action != nullptr);
  if (action != nullptr) {
    // The controller's own diagnostic node: the host dispatches it through the shell's controller
    // path, as a real assistive client would.
    const auto performed = f.shell.dispatchController(
        f.controller, "diagnostic-action.0.RELINK_MEDIA", SemanticAction::Activate);
    if (!performed) throw test::Failure{"diagnostic action refused: " + performed.error().message};
    CHECK(f.diagnosticActions == 1U);
  }
}

TEST_CASE("the diagnostics popover closes with the last diagnostic and does not reopen by itself") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  authoring::Diagnostic issue{.code = "MEDIA_MISSING",
                              .severity = authoring::DiagnosticSeverity::Warning,
                              .messageKey = "media.missing",
                              .actions = {authoring::DiagnosticAction::RelinkMedia}};
  f.controller.setDiagnostics({issue});
  CHECK(f.frame());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.diagnostics.open", SemanticAction::Activate)
            .hasValue());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::Diagnostics);
  // The last diagnostic is resolved: the popover is gone, and so is the flag that presented it.
  f.controller.setDiagnostics({});
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
  CHECK(!f.shell.diagnosticsOpen());
  // The next failure shows its toast; the popover stays closed until the creator opens it.
  f.controller.setDiagnostics({issue});
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
  CHECK(f.node("shell.diagnostics.toast") != nullptr);
  CHECK(f.node("timeline") != nullptr);
}

TEST_CASE("every diagnostic's recovery actions are reachable from the popover, not only the first") {
  const auto issue = [](std::string code, authoring::DiagnosticSeverity severity,
                        std::vector<authoring::DiagnosticAction> actions) {
    return authoring::Diagnostic{.code = std::move(code), .severity = severity,
                                 .messageKey = "test", .actions = std::move(actions)};
  };
  const std::vector<authoring::Diagnostic> issues{
      issue("MEDIA_MISSING", authoring::DiagnosticSeverity::Warning,
            {authoring::DiagnosticAction::RelinkMedia, authoring::DiagnosticAction::CopyDiagnostic}),
      issue("BANK_MISSING", authoring::DiagnosticSeverity::Critical,
            {authoring::DiagnosticAction::RelinkVoicebank,
             authoring::DiagnosticAction::ChooseVoicebank}),
      issue("RENDER_FAILED", authoring::DiagnosticSeverity::Warning,
            {authoring::DiagnosticAction::Retry, authoring::DiagnosticAction::CopyDiagnostic})};
  const auto actionIds = [&issues](std::size_t i) {
    std::vector<std::string> out;
    for (const auto kind : native_ui::presentDiagnostic(issues[i]).primaryActionKinds)
      out.push_back("diagnostic-action." + std::to_string(i) + "." +
                    std::string{authoring::toString(kind)});
    return out;
  };
  // The popover's own contract, with the first block's row and actions, at every size.
  {
    OverlayFixture f;
    if (!native_ui::paint::vectorBackendAvailable()) return;
    CHECK(f.frame());
    f.controller.setDiagnostics(issues);
    f.shell.setDiagnosticsOpen(true);
    CHECK(f.frame());
    auto required = actionIds(0U);
    required.push_back("diagnostic.0.MEDIA_MISSING");
    checkOverlayContract(f, "shell.overlay.diagnostics.", required,
                         {"diagnostic.1.BANK_MISSING", "shell.overlay.diagnostics.previous",
                          "shell.overlay.diagnostics.next"},
                         "shell.diagnostics.open");
  }
  // At the minimum window only one block fits: the pager reaches every other one, by pointer and
  // through accessibility, and every action of every diagnostic runs through the host path.
  for (const auto [width, height] : {std::pair{480.0, 320.0}, std::pair{1600.0, 900.0}}) {
    OverlayFixture f;
    CHECK(f.frame(width, height));
    f.controller.setDiagnostics(issues);
    f.shell.setDiagnosticsOpen(true);
    unsigned expected = 0U;
    for (std::size_t i = 0U; i < issues.size(); ++i) {
      for (int step = 0; step < 4 && f.node(actionIds(i).front()) == nullptr; ++step) {
        CHECK(f.frame(width, height));
        const auto* next = f.node("shell.overlay.diagnostics.next");
        if (next == nullptr) throw test::Failure{"diagnostic " + std::to_string(i) + " unreachable"};
        const ui::Point p{next->bounds.x + 4.0, next->bounds.y + 4.0};
        CHECK(f.shell.pointerDown(f.controller, press(p)).hasValue());
        CHECK(f.shell.pointerUp(f.controller, press(p)).hasValue());
      }
      CHECK(f.frame(width, height));
      for (const auto& id : actionIds(i)) {
        if (f.node(id) == nullptr) throw test::Failure{id + " is not published"};
        const auto performed = f.shell.dispatchController(f.controller, id, SemanticAction::Activate);
        if (!performed) throw test::Failure{id + " refused: " + performed.error().message};
        CHECK(f.diagnosticActions == ++expected);
      }
    }
    if (width < 500.0) {
      // The pager goes back too, through its accessibility node.
      CHECK(f.shell.dispatchSemantic(f.controller, "shell.overlay.diagnostics.previous",
                                     SemanticAction::Activate)
                .hasValue());
      CHECK(f.frame(width, height));
      CHECK(f.node(actionIds(1U).front()) != nullptr);
    }
  }
}

TEST_CASE("the export progress strip is a status-bar segment with the controller's own cancel") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  // No export has reported files: no segment and no node.
  CHECK(f.shell.exportStatusSegment(f.controller.sceneState()).width == 0.0);
  // Staging is a cancellable state, so the controller publishes its own cancel action too.
  f.controller.setExportProgress({.state = authoring::ExportState::Staging,
                                 .currentOutput = "master.wav",
                                 .completedFiles = 1U,
                                 .totalFiles = 4U});
  CHECK(f.frame());
  const auto segment = f.shell.exportStatusSegment(f.controller.sceneState());
  CHECK(segment.width > 0.0);
  // The segment sits in the status bar and inside the client area at both supported sizes.
  for (const auto [width, height] : {std::pair{480.0, 320.0}, std::pair{1600.0, 900.0}}) {
    CHECK(f.frame(width, height));
    const auto bounds = f.shell.exportStatusSegment(f.controller.sceneState());
    if (bounds.width <= 0.0) continue;
    CHECK(bounds.x >= 0.0 && bounds.right() <= width + 0.5);
    CHECK(bounds.y >= f.shell.layout().status.y - 0.5);
    CHECK(bounds.bottom() <= f.shell.layout().status.bottom() + 0.5);
  }
  CHECK(f.frame());
  const auto* progress = f.node("export.progress");
  CHECK(progress != nullptr);
  // The progress node is the controller's, re-homed to the segment's rectangle.
  if (progress != nullptr) CHECK_NEAR(progress->bounds.x, segment.x, 1e-9);
  // Its cancel action is the controller's own command, reached through the shell's controller path.
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  bool sawCancel = false;
  const auto collect = [&](const SemanticNode& node, const auto& self) -> void {
    for (const auto& child : node.children) {
      if (child.id == "export.cancel") sawCancel = true;
      self(child, self);
    }
  };
  collect(f.shell.accessibilityTree().root(), collect);
  CHECK(sawCancel);
}

TEST_CASE("a rendering frame and a failed frame produce different singer-ring pixels") {
  // The singer ring is the protagonist's state made visible, so a ring around a live render cannot be
  // the pixels of a ring around a failure: one is the look's accent lit to the render fraction, the
  // other is the error tint over the whole ring. The frame is a real one through the shell's paint.
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const auto ringPixels = [](ShellFixture& f) {
    CHECK(f.frame());
    const auto& ring = f.shell.layout().portraitRing;
    std::vector<std::uint32_t> samples;
    constexpr std::size_t kTicks = 64U;
    for (std::size_t i = 0U; i < kTicks; ++i) {
      const auto angle = -std::numbers::pi * 0.5 +
                         static_cast<double>(i) * 2.0 * std::numbers::pi /
                             static_cast<double>(kTicks);
      const auto x = static_cast<std::uint32_t>(
          ring.x + ring.width * 0.5 + std::cos(angle) * (ring.width * 0.5 - 4.0));
      const auto y = static_cast<std::uint32_t>(
          ring.y + ring.height * 0.5 + std::sin(angle) * (ring.height * 0.5 - 4.0));
      if (x >= f.surface.width() || y >= f.surface.height()) continue;
      samples.push_back(f.surface.pixels()[static_cast<std::size_t>(y) * f.surface.width() + x]);
    }
    return samples;
  };
  ShellFixture first;
  native_ui::RenderStatusView rendering;
  rendering.state = native_ui::RenderStatusState::Rendering;
  rendering.fraction = 0.5;
  first.controller.setRenderStatus(rendering);
  const auto rendered = ringPixels(first);

  ShellFixture second;
  native_ui::RenderStatusView failed;
  failed.state = native_ui::RenderStatusState::Failed;
  failed.diagnostic = "Project has no audible rendered tracks";
  second.controller.setRenderStatus(failed);
  const auto broke = ringPixels(second);

  CHECK(!rendered.empty());
  CHECK(rendered.size() == broke.size());
  CHECK(rendered != broke);
  CHECK(first.shell.characterState() != second.shell.characterState());
}

// ---- The last classic surfaces, re-homed as shell sheets and inline fields ----------------------

namespace {

using native_ui::design::Workspace;

// A copy of the node the shell publishes now: every lookup rebuilds the tree, so a pointer from an
// earlier lookup would not survive the next one.
std::optional<SemanticNode> nodeNow(OverlayFixture& f, std::string_view id) {
  if (const auto* found = f.node(id); found != nullptr) return *found;
  return std::nullopt;
}

// The shell's panel node for an overlay.
std::optional<SemanticNode> overlayPanel(OverlayFixture& f, std::string_view panelPrefix) {
  return nodeNow(f, std::string{panelPrefix} + "panel");
}

void succeeds(const core::Result<void>& result, std::string_view what) {
  if (!result) throw test::Failure{std::string{what} + " refused: " + result.error().message};
}

// Every control the presented surface publishes takes focus through the host's accessibility path
// (dispatchController), whatever its id, and is then the node the shell reports as focused.
void focusEveryControl(OverlayFixture& f, std::string_view panelPrefix) {
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  std::vector<std::string> ids;
  for (const auto& child : f.shell.accessibilityTree().root().children)
    if (child.id != "shell.status" && child.id != "shell.render-progress" &&
        child.id != std::string{panelPrefix} + "panel")
      ids.push_back(child.id);
  if (ids.empty()) throw test::Failure{"no controls published for " + std::string{panelPrefix}};
  for (const auto& id : ids) {
    succeeds(f.shell.dispatchController(f.controller, id, native_ui::SemanticAction::SetFocus),
             "focus " + id);
    if (f.focusedId() != id) throw test::Failure{"focus " + id + " reports " + f.focusedId()};
  }
}

bool offers(const SemanticNode& node, native_ui::SemanticAction action) {
  return std::find(node.actions.begin(), node.actions.end(), action) != node.actions.end();
}

ui::Point centre(ui::Rect r) { return {r.x + r.width * 0.5, r.y + r.height * 0.5}; }

}  // namespace

TEST_CASE("a review is a shell sheet whose rows and actions are the controller's own commands") {
  using native_ui::SemanticAction;
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  // A note with vibrato, so Clear vibrato has something to apply.
  f.session.project().findRegion(f.regionId)->notes.front().vibrato.enabled = true;
  f.session.selection().selectOnly(f.session.project().findRegion(f.regionId)->notes.front().id);
  CHECK(f.frame());
  succeeds(f.controller.openClearVibratoReview(), "opening the review");
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::ReplacementReview);
  const auto prefix = f.controller.replacementReviewSemanticPrefix();
  std::vector<std::string> required{prefix + "status"};
  for (std::size_t i = 0U; i < 6U; ++i) required.push_back(prefix + "action." + std::to_string(i));
  std::vector<std::string> optional{"shell.overlay.review.rows-up", "shell.overlay.review.rows-down"};
  for (std::size_t i = 0U; i < 6U; ++i) optional.push_back(prefix + "row." + std::to_string(i));
  checkOverlayContract(f, "shell.overlay.review.", required, optional, "");
  CHECK(!f.controller.sceneState().replacementReview.visible);

  // Reopened: the rows publish their whole text, whatever the painted row elides, and the status
  // node carries status and summary.
  succeeds(f.controller.openClearVibratoReview(), "reopening the review");
  CHECK(f.frame(480.0, 320.0));
  const auto view = f.controller.sceneState().replacementReview;
  const auto open = f.controller.replacementReviewSemanticPrefix();
  focusEveryControl(f, "shell.overlay.review.");
  for (std::size_t i = 0U; i < view.rows.size(); ++i) {
    const auto row = nodeNow(f, open + "row." + std::to_string(i));
    if (row.has_value()) CHECK(row->value == view.rows[i]);
  }
  const auto status = nodeNow(f, open + "status");
  CHECK(status.has_value() && status->value.find(view.status) != std::string::npos);
  // Every action through the host's accessibility path: Cancel (4) closes it.
  const auto cancel = nodeNow(f, open + "action.4");
  CHECK(cancel.has_value() && cancel->role == native_ui::SemanticRole::Button);
  CHECK(f.shell.dispatchController(f.controller, open + "action.4", SemanticAction::Activate).hasValue());
  CHECK(!f.controller.sceneState().replacementReview.visible);
  // An id from that closed review is refused after a new one opens; nothing is applied.
  succeeds(f.controller.openClearVibratoReview(), "opening a new review");
  CHECK(f.frame());
  const auto revision = f.controller.documentRevision();
  CHECK(!f.shell.dispatchController(f.controller, open + "action.3", SemanticAction::Activate).hasValue());
  CHECK(f.controller.documentRevision() == revision);
  // The pointer runs the same command: Apply (3) clears the vibrato as one undoable edit.
  const auto current = f.controller.replacementReviewSemanticPrefix();
  const auto apply = nodeNow(f, current + "action.3");
  CHECK(apply.has_value() && apply->enabled);
  if (!apply.has_value()) return;
  CHECK(f.shell.pointerDown(f.controller, press(centre(apply->bounds))).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(centre(apply->bounds))).hasValue());
  CHECK(f.controller.documentRevision() != revision);
  CHECK(!f.session.project().findRegion(f.regionId)->notes.front().vibrato.enabled);
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
  CHECK(f.session.undo());
  CHECK(f.session.project().findRegion(f.regionId)->notes.front().vibrato.enabled);
}

TEST_CASE("the dynamics inspector is a docked review sheet whose plot keeps the controller's gestures") {
  using native_ui::SemanticAction;
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.session.project().findRegion(f.regionId)->dynamicsAutomation.replacePoints(
      {{time::Tick{0}, 0.25F}, {time::Tick{480}, 0.75F}}));
  CHECK(f.frame());
  CHECK(f.controller.openDynamicsInspector().hasValue());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::ReplacementReview);
  const auto prefix = f.controller.replacementReviewSemanticPrefix();
  const auto panel = overlayPanel(f, "shell.overlay.review.");
  CHECK(panel.has_value());
  if (!panel) return;
  const auto curve = nodeNow(f, prefix + "curve");
  CHECK(curve.has_value());
  if (!curve.has_value()) return;
  CHECK(curve->bounds.x >= panel->bounds.x && curve->bounds.right() <= panel->bounds.right() + 0.5);
  CHECK(curve->bounds.bottom() <= panel->bounds.bottom() + 0.5);
  // Zoom in through the host path, then Fit through the pointer: the controller's own navigation.
  const auto before = f.controller.sceneState().replacementReview.dynamicsPlot->endTick;
  CHECK(f.shell.dispatchController(f.controller, prefix + "zoom.0", SemanticAction::Activate).hasValue());
  CHECK(f.controller.sceneState().replacementReview.dynamicsPlot->endTick < before);
  CHECK(f.frame());
  const auto fit = nodeNow(f, f.controller.replacementReviewSemanticPrefix() + "zoom.2");
  CHECK(fit.has_value());
  if (fit.has_value()) {
    CHECK(f.shell.pointerDown(f.controller, press(centre(fit->bounds))).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press(centre(fit->bounds))).hasValue());
    CHECK(f.controller.sceneState().replacementReview.dynamicsPlot->endTick == before);
  }
  // A scroll over the card is the sheet's own; the score under it never scrolls.
  const auto origin = f.controller.pianoRoll().timeline().originTick();
  CHECK(f.shell.scroll(f.controller, 0.0, -40.0, centre(panel->bounds), {}));
  CHECK(f.controller.pianoRoll().timeline().originTick() == origin);
  // A drag on a point handle reaches the controller's draft, mapped into its plot, and changes no
  // document until Apply.
  CHECK(f.frame());
  const auto open = f.controller.replacementReviewSemanticPrefix();
  const auto point = nodeNow(f, open + "point.0");
  if (point.has_value()) {
    const auto revision = f.controller.documentRevision();
    const auto at = centre(point->bounds);
    CHECK(f.shell.pointerDown(f.controller, press(at)).hasValue());
    // The press opened the point (its tick and gain rows); the drag moves its gain.
    const auto pressed = f.controller.sceneState().replacementReview.rows;
    CHECK(f.controller.pointerGestureActive());
    CHECK(f.shell.pointerMove(f.controller, press({at.x, at.y + 30.0})).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press({at.x, at.y + 30.0})).hasValue());
    CHECK(f.controller.documentRevision() == revision);
    CHECK(f.controller.sceneState().replacementReview.rows != pressed);
    CHECK(!f.controller.pointerGestureActive());
  }
  // Escape leaves the review through the controller's own Escape.
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(!f.controller.sceneState().replacementReview.visible);
}

TEST_CASE("Settings exposes audio, appearance, language and About through one modal sheet") {
  using native_ui::SemanticAction;
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.controller.setAudioSettings(
      authoring::AudioSettings{.deviceId = "built-in", .sampleRate = 48000U,
                               .blockFrames = 256U, .outputChannels = 2U},
      {{.id = "built-in", .name = "Built-in Output", .physical = true, .selected = true}},
      12U, 3U);
  CHECK(f.frame());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.settings", SemanticAction::Activate).hasValue());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::Settings);
  CHECK(nodeNow(f, "audio.sample-rate").has_value());
  CHECK(nodeNow(f, "audio.diagnostics").has_value());
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Tab}));
  CHECK(f.focusedId() == "shell.overlay.settings.section.audio");
  CHECK(f.shell.handleShellKey(f.controller,
      KeyEvent{.key = NativeKey::Tab, .modifiers = {.shift = true}}));
  CHECK(f.focusedId() == "shell.overlay.settings.close");
  CHECK(!f.shell.dispatchSemantic(f.controller, "shell.mode.scene", SemanticAction::Activate).hasValue());
  CHECK(f.shell.dispatchController(f.controller, "shell.overlay.settings.section.appearance",
                                   SemanticAction::Activate).hasValue());
  CHECK(f.frame());
  CHECK(f.shell.dispatchController(f.controller, "shell.overlay.settings.look.scene",
                                   SemanticAction::Activate).hasValue());
  CHECK(f.shell.mode() == DesignMode::Scene);
  CHECK(f.shell.dispatchController(f.controller, "shell.overlay.settings.contrast.high",
                                   SemanticAction::Activate).hasValue());
  CHECK(f.shell.contrast() == native_ui::design::Contrast::High);
  CHECK(f.shell.dispatchController(f.controller, "shell.overlay.settings.motion.on",
                                   SemanticAction::Activate).hasValue());
  CHECK(f.shell.motionReduced());
  CHECK(f.shell.dispatchController(f.controller, "shell.overlay.settings.character.off",
                                   SemanticAction::Activate).hasValue());
  CHECK(f.controller.characterDisplay() == domain::CharacterDisplayMode::Off);
  CHECK(f.shell.dispatchController(f.controller, "shell.overlay.settings.section.language",
                                   SemanticAction::Activate).hasValue());
  CHECK(f.frame());
  CHECK(f.shell.dispatchController(f.controller, "shell.overlay.settings.language.ko",
                                   SemanticAction::Activate).hasValue());
  CHECK(f.shell.language() == "ko");
  CHECK(f.shell.dispatchController(f.controller, "shell.overlay.settings.section.about",
                                   SemanticAction::Activate).hasValue());
  CHECK(f.frame());
  CHECK(f.shell.dispatchController(f.controller, "shell.overlay.settings.about",
                                   SemanticAction::Activate).hasValue());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::About);
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.settings", SemanticAction::Activate).hasValue());
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(!f.shell.settingsOpen());
  CHECK(f.focusedId() == "shell.settings");
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.settings", SemanticAction::Activate).hasValue());
  CHECK(f.frame(1280.0, 800.0));
  CHECK(!f.shell.settingsOpen());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.settings", SemanticAction::Activate).hasValue());
  f.shell.setWorkspace(f.controller, Workspace::Mix);
  CHECK(!f.shell.settingsOpen());
}

TEST_CASE("the audio settings sheet lists the devices and applies every change through the host") {
  using native_ui::SemanticAction;
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const std::string longName =
      "A very long USB audio interface name that no settings row at any window size can show whole";
  f.controller.setAudioSettings(
      authoring::AudioSettings{.deviceId = "built-in", .sampleRate = 48000U, .blockFrames = 256U,
                               .outputChannels = 2U},
      {{.id = "built-in", .name = "Built-in Output", .physical = true, .selected = true},
       {.id = "usb", .name = longName, .physical = true, .selected = false},
       {.id = "null", .name = "Silent fallback", .physical = false, .selected = false}},
      12U, 3U);
  CHECK(f.frame());
  f.controller.showAudioSettings();
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::AudioSettings);
  checkOverlayContract(f, "shell.overlay.audio.",
                       {"shell.overlay.audio.close", "audio.device.0", "audio.sample-rate",
                        "audio.block-frames", "audio.channels", "audio.diagnostics"},
                       {"audio.device.1", "audio.device.2", "shell.overlay.audio.devices-up",
                        "shell.overlay.audio.devices-down"},
                       "shell.settings");
  CHECK(!f.controller.audioSettingsVisible());

  f.controller.showAudioSettings();
  CHECK(f.frame());
  focusEveryControl(f, "shell.overlay.audio.");
  // The device name is whole on the node however the row elides it; the counts are published.
  const auto usb = nodeNow(f, "audio.device.1");
  CHECK(usb.has_value() && usb->name == longName);
  const auto chosen = nodeNow(f, "audio.device.0");
  CHECK(chosen.has_value() && chosen->selected);
  const auto counts = nodeNow(f, "audio.diagnostics");
  CHECK(counts.has_value() && counts->value.find("Underflow 12") != std::string::npos &&
        counts->value.find("XRun 3") != std::string::npos);
  const auto rate = nodeNow(f, "audio.sample-rate");
  CHECK(rate.has_value() && rate->value == "48000 Hz");
  CHECK(rate.has_value() && offers(*rate, SemanticAction::Increment) &&
        offers(*rate, SemanticAction::Decrement));
  // Each change is the controller's own command, applied through the host's applyAudioSettings.
  CHECK(f.shell.dispatchController(f.controller, "audio.sample-rate", SemanticAction::Increment).hasValue());
  CHECK(!f.appliedAudio.empty() && f.appliedAudio.back().sampleRate == 96000U);
  CHECK(f.shell.dispatchController(f.controller, "audio.sample-rate", SemanticAction::Decrement).hasValue());
  CHECK(f.appliedAudio.back().sampleRate == 44100U);
  CHECK(f.shell.dispatchController(f.controller, "audio.block-frames", SemanticAction::Activate).hasValue());
  CHECK(f.appliedAudio.back().blockFrames == 512U);
  CHECK(f.shell.dispatchController(f.controller, "audio.channels", SemanticAction::Increment).hasValue());
  CHECK(f.appliedAudio.back().outputChannels == 4U);
  CHECK(f.shell.dispatchController(f.controller, "audio.device.1", SemanticAction::Activate).hasValue());
  CHECK(f.appliedAudio.back().deviceId == "usb");
  // The pointer and the classic keys run the same commands.
  const auto applied = f.appliedAudio.size();
  if (const auto channels = nodeNow(f, "audio.channels"); channels.has_value()) {
    CHECK(f.shell.pointerDown(f.controller, press(centre(channels->bounds))).hasValue());
    CHECK(f.appliedAudio.size() == applied + 1U);
  }
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Right}));
  CHECK(f.appliedAudio.size() == applied + 2U);
  CHECK(!f.shell.setControllerValue(f.controller, "audio.sample-rate", "96000").hasValue());
  // A refusal is the host's own, recorded as the sheet's diagnostic and published.
  f.refuseAudio = true;
  CHECK(!f.shell.dispatchController(f.controller, "audio.sample-rate", SemanticAction::Increment).hasValue());
  const auto refused = nodeNow(f, "audio.diagnostics");
  CHECK(refused.has_value() && refused->value.find("refused") != std::string::npos);
  CHECK(!f.session.canUndo());
  // Close through the host path.
  CHECK(f.shell.dispatchController(f.controller, "shell.overlay.audio.close", SemanticAction::Activate).hasValue());
  CHECK(!f.controller.audioSettingsVisible());

  // MIX's device card opens the same sheet over MIX; Escape returns focus to that button.
  f.shell.setWorkspace(f.controller, Workspace::Mix);
  CHECK(f.frame());
  const auto mixSettings = nodeNow(f, "shell.mix.audio-settings");
  CHECK(mixSettings.has_value());
  if (!mixSettings.has_value()) return;
  const auto at = centre(mixSettings->bounds);
  CHECK(f.shell.pointerDown(f.controller, press(at)).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(at)).hasValue());
  CHECK(f.controller.audioSettingsVisible());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::AudioSettings);
  CHECK(f.shell.workspace() == Workspace::Mix);
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(!f.controller.audioSettingsVisible());
  CHECK(f.focusedId() == "shell.mix.audio-settings");
  // A workspace switch closes it cleanly.
  f.controller.showAudioSettings();
  CHECK(f.frame());
  f.shell.setWorkspace(f.controller, Workspace::Sing);
  CHECK(!f.controller.audioSettingsVisible());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
}

TEST_CASE("the voice browser is a large sheet that selects, refreshes and installs through the host") {
  using native_ui::SemanticAction;
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  std::vector<authoring::VoicebankCard> cards;
  for (std::size_t i = 0U; i < 14U; ++i) {
    authoring::VoicebankCard card;
    card.id = "bank-" + std::to_string(i);
    card.version = "1.0." + std::to_string(i);
    card.displayName = "Singer " + std::to_string(i);
    card.language = "ja";
    card.contentHash = "hash-" + std::to_string(i);
    card.contentHashAbbreviation = "h" + std::to_string(i);
    card.trustLabel = "Official";
    card.installed = true;
    card.selectable = true;
    card.rootPitchLayers = {48, 72};
    cards.push_back(std::move(card));
  }
  const std::string longName =
      "A singer whose display name is far longer than any card in the voice browser can show";
  cards[1].displayName = longName;
  cards[2].selectable = false;
  cards[2].trustLabel = "Untrusted";
  f.controller.setVoicebankCards(cards);
  CHECK(f.frame());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.change-voice", SemanticAction::Activate).hasValue());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::VoicebankBrowser);
  std::vector<std::string> optional{"shell.overlay.voicebank.previous", "shell.overlay.voicebank.next"};
  for (std::size_t i = 1U; i < cards.size(); ++i) optional.push_back("voicebank.card." + std::to_string(i));
  checkOverlayContract(f, "shell.overlay.voicebank.",
                       {"voicebank.card.0", "shell.overlay.voicebank.refresh",
                        "shell.overlay.voicebank.install", "shell.overlay.voicebank.close"},
                       optional, "shell.change-voice");
  CHECK(!f.controller.voicebankBrowserVisible());

  // At the minimum window the cards page; the pager reaches every card.
  f.controller.showVoicebankBrowser();
  CHECK(f.frame(480.0, 320.0));
  CHECK(f.node("voicebank.card.13", 480.0, 320.0) == nullptr);
  for (int i = 0; i < 16 && f.node("voicebank.card.13", 480.0, 320.0) == nullptr; ++i)
    CHECK(f.shell.dispatchController(f.controller, "shell.overlay.voicebank.next", SemanticAction::Activate).hasValue());
  CHECK(f.node("voicebank.card.13", 480.0, 320.0) != nullptr);
  CHECK(f.node("voicebank.card.0", 480.0, 320.0) == nullptr);
  // A resize keeps the sheet open, and every card is back on one page.
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::VoicebankBrowser);
  focusEveryControl(f, "shell.overlay.voicebank.");
  // The whole name is on the node; the untrusted card says why it cannot be chosen.
  const auto named = nodeNow(f, "voicebank.card.1");
  CHECK(named.has_value() && named->name == longName);
  const auto untrusted = nodeNow(f, "voicebank.card.2");
  CHECK(untrusted.has_value() && !untrusted->enabled &&
        untrusted->description.find("not trusted") != std::string::npos);
  CHECK(!f.shell.dispatchController(f.controller, "voicebank.card.2", SemanticAction::Activate).hasValue());
  CHECK(f.selectedBanks.empty());
  // Refresh by pointer, install by the classic key, refresh by R: the host's own commands.
  if (const auto refresh = nodeNow(f, "shell.overlay.voicebank.refresh"); refresh.has_value()) {
    CHECK(f.shell.pointerDown(f.controller, press(centre(refresh->bounds))).hasValue());
  }
  CHECK(f.voicebankRefreshes == 1U);
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::O}));
  CHECK(f.installerOpens == 1U);
  CHECK(f.shell.dispatchController(f.controller, "shell.overlay.voicebank.install", SemanticAction::Activate).hasValue());
  CHECK(f.installerOpens == 2U);
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::R}));
  CHECK(f.voicebankRefreshes == 2U);
  // Choosing a card asks the host to replace the track's voicebank and closes the browser.
  CHECK(f.shell.dispatchController(f.controller, "voicebank.card.3", SemanticAction::Activate).hasValue());
  CHECK(f.selectedBanks == std::vector<std::string>{"bank-3"});
  CHECK(!f.controller.voicebankBrowserVisible());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);

  // VOICE's own button opens the same sheet; a workspace switch closes it.
  f.shell.setWorkspace(f.controller, Workspace::Voice);
  CHECK(f.frame());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.voice.browser", SemanticAction::Activate).hasValue());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::VoicebankBrowser);
  f.shell.setWorkspace(f.controller, Workspace::Sing);
  CHECK(!f.controller.voicebankBrowserVisible());

  // A replaced controller (a project opened or recovered) starts with no sheet and no stale focus.
  f.controller.showVoicebankBrowser();
  CHECK(f.frame());
  CHECK(f.focusedId().starts_with("voicebank.card."));
  native_ui::NativeEditorController replacement{f.session, f.factory, f.regionId, {}};
  replacement.resize(1600.0, 900.0);
  CHECK(f.shell.prepareFrame(replacement, 1600.0, 900.0));
  CHECK(f.shell.overlayKind(replacement) == OverlayKind::None);
  replacement.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(replacement, replacement.sceneState());
  CHECK(findShellNode(f.shell.accessibilityTree().root(), "shell.overlay.voicebank.panel") == nullptr);
  const auto* focused = f.shell.accessibilityTree().focusedNode();
  CHECK(focused == nullptr || !focused->id.starts_with("voicebank."));
}

TEST_CASE("the hint and transport fields are inline shell fields on the lyric field's input path") {
  using native_ui::SemanticAction;
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const auto noteId = f.session.project().findRegion(f.regionId)->notes.front().id;
  const auto openHint = [&] {
    f.session.selection().selectOnly(noteId);
    const auto begun = f.controller.beginSelectedHintEdit();
    if (!begun) throw test::Failure{"hint edit refused: " + begun.error().message};
  };
  for (const auto [width, height] : {std::pair{480.0, 320.0}, std::pair{720.0, 480.0},
                                     std::pair{1100.0, 720.0}, std::pair{1600.0, 900.0}}) {
    CHECK(f.frame(width, height));
    openHint();
    CHECK(f.frame(width, height));
    CHECK(f.shell.overlayKind(f.controller) == OverlayKind::TextField);
    const auto field = f.controller.textFieldView();
    CHECK(field.kind == native_ui::NativeEditorController::TextFieldView::Kind::Bounded);
    const auto panel = overlayPanel(f, "shell.overlay.field.");
    const auto input = nodeNow(f, field.inputId);
    const auto cancel = nodeNow(f, field.cancelId);
    CHECK(panel.has_value() && input.has_value() && cancel.has_value());
    if (!panel || !input.has_value() || !cancel.has_value()) return;
    // The IME rectangle, the painted field, its hit rectangle and its node are one rectangle.
    CHECK(f.lastTextInput.has_value());
    if (!f.lastTextInput) return;
    CHECK(f.lastTextInput->anchor == native_ui::TextInputAnchor::BoundedField);
    CHECK_NEAR(f.lastTextInput->logicalBounds.x, input->bounds.x, 1e-9);
    CHECK_NEAR(f.lastTextInput->logicalBounds.y, input->bounds.y, 1e-9);
    CHECK_NEAR(f.lastTextInput->logicalBounds.width, input->bounds.width, 1e-9);
    CHECK_NEAR(f.lastTextInput->logicalBounds.height, input->bounds.height, 1e-9);
    CHECK(input->role == native_ui::SemanticRole::TextField);
    CHECK(offers(*input, SemanticAction::EditText));
    CHECK(cancel->role == native_ui::SemanticRole::Button);
    for (const auto& bounds : {input->bounds, cancel->bounds}) {
      CHECK(bounds.x >= panel->bounds.x && bounds.right() <= panel->bounds.right());
      CHECK(bounds.y >= panel->bounds.y && bounds.bottom() <= panel->bounds.bottom());
    }
    CHECK(input->bounds.right() <= cancel->bounds.x);
    CHECK(panel->bounds.x >= 0.0 && panel->bounds.right() <= width &&
          panel->bounds.bottom() <= height);
    // The field has the keyboard, and its keys are the input client's, never the score's.
    CHECK(f.focusedId() == field.inputId);
    CHECK(!f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Tab}));
    CHECK(f.shell.accessibilityTree().virtualizedNoteCount() == 0U);
    // Escape cancels it and leaves nothing behind.
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
    CHECK(!f.controller.textInputActive());
    CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
  }

  // What is typed is the node's value, whole; a commit through the host's value path is the
  // controller's own hint edit, one undo step.
  openHint();
  CHECK(f.frame());
  focusEveryControl(f, "shell.overlay.field.");
  const std::string typed = "k a k a k a k a k a k a k a k a k a k a k a k a k a k a k a k a k a";
  CHECK(f.controller.updateTextComposition(domain::fromUtf8(typed).value(), {}).hasValue());
  const auto inputId = f.controller.textFieldView().inputId;
  const auto typedNode = nodeNow(f, inputId);
  CHECK(typedNode.has_value() && typedNode->value == typed);
  const auto revision = f.controller.documentRevision();
  const auto committed = f.shell.setControllerValue(f.controller, inputId, "k a");
  if (!committed) throw test::Failure{"hint refused: " + committed.error().message};
  CHECK(f.controller.documentRevision() != revision);
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
  CHECK(f.session.undo());
  CHECK(f.controller.documentRevision() != revision || !f.session.canUndo());

  // Cancel by pointer; a resize cancels an open field rather than leave the input client behind.
  openHint();
  CHECK(f.frame());
  if (const auto cancel = nodeNow(f, f.controller.textFieldView().cancelId); cancel.has_value()) {
    CHECK(f.shell.pointerDown(f.controller, press(centre(cancel->bounds))).hasValue());
    CHECK(!f.controller.textInputActive());
  }
  openHint();
  CHECK(f.frame());
  CHECK(f.frame(720.0, 480.0));
  CHECK(!f.controller.textInputActive());
  // A workspace switch cancels it too.
  openHint();
  CHECK(f.frame());
  f.shell.setWorkspace(f.controller, Workspace::Tune);
  CHECK(!f.controller.textInputActive());
  f.shell.setWorkspace(f.controller, Workspace::Sing);

  // The transport's tempo field drops from the transport display and commits through the
  // toolbar readout's own value path.
  CHECK(f.frame());
  CHECK(f.controller.beginTempoEdit().hasValue());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::TextField);
  CHECK(f.lastTextInput && f.lastTextInput->anchor == native_ui::TextInputAnchor::Transport);
  const auto tempo = nodeNow(f, "toolbar.tempo");
  CHECK(tempo.has_value() && tempo->role == native_ui::SemanticRole::TextField);
  if (tempo.has_value() && f.lastTextInput)
    CHECK_NEAR(f.lastTextInput->logicalBounds.y, tempo->bounds.y, 1e-9);
  const auto tempoRevision = f.controller.documentRevision();
  const auto tempoResult = f.shell.setControllerValue(f.controller, "toolbar.tempo", "96");
  if (!tempoResult) throw test::Failure{"tempo refused: " + tempoResult.error().message};
  CHECK(f.controller.documentRevision() != tempoRevision);
  CHECK(f.session.project().tempoMap().events().front().bpm == 96.0);
  CHECK(f.session.undo());
  // An invalid value is the controller's refusal, unchanged.
  CHECK(f.controller.beginTempoEdit().hasValue());
  CHECK(f.frame());
  const auto refused = f.controller.documentRevision();
  CHECK(!f.shell.setControllerValue(f.controller, "toolbar.tempo", "fast").hasValue());
  CHECK(f.controller.documentRevision() == refused);
}

namespace {

// The classic editor's technical lanes, microscope plots and bounce timing now live only in the
// shell. This fixture records which host commands the shell's gestures reached.
struct ParityFixture final {
  application::ProjectFactory factory{9700U};
  domain::TrackId trackId{};
  domain::RegionId regionId{};
  domain::NoteId noteId{};
  application::EditorSession session;
  std::vector<std::string> calls;
  bool followHost{false};
  voicebank::AudioBuffer audio{.sampleRate = 48000U, .channels = 1U,
                               .interleaved = test::support::sineWave(48000U, 220.0, 0.05)};
  voicebank::Unit unit{test::support::makeUnit("parity-a", {"a"}, "audio/a.wav", 60U,
                                               voicebank::UnitKind::Cv, audio.frameCount())};
  native_ui::NativeEditorController controller;
  SingShell shell;

  ParityFixture()
      : session(makeProject()),
        controller{session, factory, regionId,
                   native_ui::EditorHostCallbacks{
                       .setBounceTiming =
                           [this](bool follow) {
                             calls.push_back(follow ? "bounce:host" : "bounce:fixed");
                             followHost = follow;
                             return core::success();
                           },
                       .cycleUnitVariant =
                           [this](domain::PhonemeKey) {
                             calls.emplace_back("variant");
                             return core::success();
                           },
                       .cycleUnitRenderer =
                           [this](domain::PhonemeKey) {
                             calls.emplace_back("renderer");
                             return core::success();
                           },
                       .movePhonemeBoundary =
                           [this](domain::PhonemeKey, bool, time::Microseconds) {
                             calls.emplace_back("boundary");
                             return core::success();
                           },
                       .loadSampleMicroscope =
                           [this](domain::PhonemeKey) -> core::Result<native_ui::SampleMicroscopeData> {
                             calls.emplace_back("microscope");
                             return native_ui::SampleMicroscopeData{unit, audio, "parity"};
                           },
                       .microscopeUnitChanged =
                           [this](domain::PhonemeKey, const voicebank::Unit&) {
                             calls.emplace_back("marker");
                             return core::success();
                           },
                       .playMicroscopeSample =
                           [this](const voicebank::Unit&, const voicebank::AudioBuffer&) {
                             calls.emplace_back("play");
                             return core::success();
                           },
                   }} {
    controller.resize(1600.0, 900.0);
    shell.activate({}, DesignPreferences{.mode = DesignMode::Emo});
  }

  domain::Project makeProject() {
    auto project = factory.createProject("Shell parity");
    project.settings().characterDisplay = domain::CharacterDisplayMode::Off;
    trackId = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, trackId, "Phrase", time::Tick{0}, time::Tick{7680});
    auto [lyric, note] = factory.makeNote(time::Tick{960}, time::Tick{1920}, 72U, U"\u3042",
                                          domain::Language::Japanese);
    noteId = note.id;
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    return project;
  }

  bool frame() {
    if (!shell.prepareFrame(controller, 1600.0, 900.0)) return false;
    native_ui::PixelSurface surface{1600U, 900U};
    native_ui::RasterCanvas canvas{surface, 1.0, nullptr};
    return shell.paint(canvas, controller, controller.sceneState(), controller.playheadTick());
  }

  std::size_t count(std::string_view call) const {
    return static_cast<std::size_t>(std::count(calls.begin(), calls.end(), call));
  }
};

const native_ui::SemanticNode* findNode(const native_ui::AccessibilityTree& tree, std::string_view id) {
  const auto search = [id](const native_ui::SemanticNode& node, const auto& self) -> const native_ui::SemanticNode* {
    for (const auto& child : node.children) {
      if (child.id == id) return &child;
      if (const auto* found = self(child, self); found != nullptr) return found;
    }
    return nullptr;
  };
  return search(tree.root(), search);
}

}  // namespace
TEST_CASE("a surface presented over the score cancels an open lyric, whose keys never commit it") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const auto lyricSurface = [&f] {
    const auto* region = f.session.project().findRegion(f.regionId);
    if (region == nullptr || region->notes.empty()) return std::u32string{U"<no note>"};
    const auto* lyric = region->findLyric(region->notes.front().lyricTokenId);
    return lyric == nullptr ? std::u32string{} : lyric->surface;
  };
  const auto before = lyricSurface();
  // The host's key path: the shell first, then the controller for a key the shell did not take.
  const auto key = [&f](NativeKey k) {
    const KeyEvent event{.key = k};
    if (!f.shell.handleShellKey(f.controller, event))
      static_cast<void>(f.controller.keyDown(event));
  };
  const auto openLyric = [&] {
    CHECK(f.frame());
    const auto* region = f.session.project().findRegion(f.regionId);
    if (region == nullptr || region->notes.empty()) throw test::Failure{"the note is gone"};
    CHECK(f.controller.beginLyricEdit(region->notes.front().id).hasValue());
    CHECK(f.lastTextInput && f.lastTextInput->anchor == native_ui::TextInputAnchor::NoteGrid);
    CHECK(f.controller.updateTextComposition(U"zz", {}).hasValue());
  };
  // Each surface the shell does not open itself: the host's menu (voice browser, Audio
  // Settings...), and the shell's own DIAGNOSTICS popover.
  authoring::Diagnostic issue{.code = "MEDIA_MISSING",
                              .severity = authoring::DiagnosticSeverity::Warning,
                              .messageKey = "media.missing",
                              .actions = {authoring::DiagnosticAction::RelinkMedia}};
  const std::vector<std::pair<std::string, std::function<void()>>> openers{
      {"voice browser", [&f] { f.controller.showVoicebankBrowser(); }},
      {"audio settings", [&f] { f.controller.showAudioSettings(); }},
      {"diagnostics", [&f, &issue] {
         f.controller.setDiagnostics({issue});
         f.shell.setDiagnosticsOpen(true);
       }}};
  for (const auto& [name, open] : openers) {
    for (const auto framed : {false, true}) {
      openLyric();
      open();
      // With or without a frame in between, Enter, Tab and Backspace are the surface's keys: the
      // hidden lyric is cancelled, never committed, and its input client is gone.
      if (framed) CHECK(f.frame());
      if (f.shell.overlayKind(f.controller) == OverlayKind::None)
        throw test::Failure{name + " is not presented"};
      for (const auto k : {NativeKey::Backspace, NativeKey::Tab, NativeKey::Enter}) key(k);
      if (f.controller.textInputActive())
        throw test::Failure{"the lyric stays open under the " + name};
      if (lyricSurface() != before) throw test::Failure{"the " + name + " committed the lyric"};
      CHECK(f.shell.overlayKind(f.controller) != OverlayKind::None);
      // Close it for the next opener.
      for (int i = 0; i < 3 && f.shell.overlayKind(f.controller) != OverlayKind::None; ++i)
        CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
      CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
      f.controller.setDiagnostics({});
    }
  }
  // An inline field the surface owns keeps its keys: the transport's tempo field still commits.
  CHECK(f.frame());
  CHECK(f.controller.beginTempoEdit().hasValue());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::TextField);
  CHECK(!f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Enter}));
  CHECK(f.controller.textInputActive());
  f.controller.cancelTextComposition();
}

TEST_CASE("a disabled overlay control absorbs a press and pagers never count past their end") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  // A second tempo event, so the time map has a row that a press could select.
  CHECK(f.frame());
  CHECK(f.controller.openTimeMapPanel().hasValue());
  CHECK(f.frame());
  CHECK(f.controller.timeMapPanelAction(6U).hasValue());
  CHECK(f.frame());
  CHECK(f.shell.setControllerValue(f.controller, f.controller.textFieldView().inputId, "1920")
            .hasValue());
  CHECK(f.frame());
  CHECK(f.shell.setControllerValue(f.controller, f.controller.textFieldView().inputId, "90")
            .hasValue());
  // The map lists the new event after its own Refresh, as the classic panel required.
  CHECK(f.controller.timeMapPanelAction(4U).hasValue());
  CHECK(f.frame());
  CHECK(f.controller.sceneState().timeMapRows.size() >= 2U);
  CHECK(f.controller.selectTimeMapRow(0U).hasValue());
  // Add tempo opens the event field: the rows are disabled, and a press on one selects nothing and
  // leaves the field open.
  CHECK(f.controller.timeMapPanelAction(6U).hasValue());
  CHECK(f.frame());
  const auto selected = f.controller.sceneState().timeMapSelectedRow;
  const auto row = nodeNow(f, "time-map-row.1");
  CHECK(row.has_value() && !row->enabled);
  if (row.has_value()) {
    CHECK(f.shell.pointerDown(f.controller, press(centre(row->bounds))).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press(centre(row->bounds))).hasValue());
  }
  CHECK(f.controller.sceneState().timeMapSelectedRow == selected);
  CHECK(f.controller.textFieldView().kind ==
        native_ui::NativeEditorController::TextFieldView::Kind::TimeMap);
  f.controller.cancelTextComposition();
  CHECK(f.controller.timeMapPanelAction(5U).hasValue());

  // The audio sheet pages its devices when they outnumber its rows. Pressing a disabled "Later
  // devices" or scrolling past the end never runs the counter on, so one step back moves the list.
  std::vector<native_ui::EditorSceneState::AudioDeviceOption> devices;
  for (std::size_t i = 0U; i < 14U; ++i)
    devices.push_back({.id = "device-" + std::to_string(i),
                       .name = "Device " + std::to_string(i),
                       .physical = true,
                       .selected = i == 0U});
  f.controller.setAudioSettings(authoring::AudioSettings{.deviceId = "device-0",
                                                         .sampleRate = 48000U,
                                                         .blockFrames = 256U,
                                                         .outputChannels = 2U},
                                devices, 0U, 0U);
  f.controller.showAudioSettings();
  CHECK(f.frame(720.0, 480.0));
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::AudioSettings);
  const auto firstShown = [&f] {
    std::size_t first = 99U;
    for (std::size_t i = 0U; i < 14U; ++i)
      if (nodeNow(f, "audio.device." + std::to_string(i)).has_value()) {
        first = i;
        break;
      }
    return first;
  };
  const auto pressNode = [&f](std::string_view id) {
    const auto node = nodeNow(f, id);
    if (!node.has_value()) throw test::Failure{std::string{id} + " is not published"};
    CHECK(f.shell.pointerDown(f.controller, press(centre(node->bounds))).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press(centre(node->bounds))).hasValue());
    CHECK(f.frame(720.0, 480.0));
  };
  CHECK(firstShown() == 0U);
  // More than one row is shown, so a counter past the last full page would be hidden by the layout.
  CHECK(nodeNow(f, "audio.device.1").has_value());
  for (int i = 0; i < 30; ++i) pressNode("shell.overlay.audio.devices-down");
  const auto last = firstShown();
  CHECK(last > 0U && last < 14U);
  const auto down = nodeNow(f, "shell.overlay.audio.devices-down");
  CHECK(down.has_value() && !down->enabled);
  pressNode("shell.overlay.audio.devices-up");
  CHECK(firstShown() + 1U == last);
  // The wheel over the sheet stops at the same end.
  const auto panel = nodeNow(f, "shell.overlay.audio.panel");
  CHECK(panel.has_value());
  if (!panel.has_value()) return;
  for (int i = 0; i < 30; ++i)
    CHECK(f.shell.scroll(f.controller, 0.0, -1.0, centre(panel->bounds), {}));
  CHECK(f.frame(720.0, 480.0));
  CHECK(firstShown() == last);
  CHECK(f.shell.scroll(f.controller, 0.0, 1.0, centre(panel->bounds), {}));
  CHECK(f.frame(720.0, 480.0));
  CHECK(firstShown() + 1U == last);
}

TEST_CASE("Escape returns focus to the opener after a review steps back from its field and detail") {
  using native_ui::SemanticAction;
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.session.project().findRegion(f.regionId)->dynamicsAutomation.replacePoints(
      {{time::Tick{0}, 0.25F}, {time::Tick{480}, 0.75F}}));
  CHECK(f.frame());
  // The lane's DYNAMICS tab opens the inspector from the keyboard, so it is the opener.
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.lane-tab.dynamics", SemanticAction::SetFocus)
            .hasValue());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.lane-tab.dynamics", SemanticAction::Activate)
            .hasValue());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::ReplacementReview);
  static_cast<void>(f.focusedId());
  // A point's detail page, then its tick field, which replaces the review while it is open.
  const auto prefix = f.controller.replacementReviewSemanticPrefix();
  succeeds(f.shell.dispatchController(f.controller, prefix + "point.0", SemanticAction::Activate),
           "opening the point");
  CHECK(f.frame());
  succeeds(f.shell.dispatchController(f.controller,
                                      f.controller.replacementReviewSemanticPrefix() + "row.0",
                                      SemanticAction::Activate),
           "opening the tick field");
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::TextField);
  static_cast<void>(f.focusedId());
  // Each Escape steps back one level (field, then detail, then the review itself); only the last
  // one closes the surface, and it returns focus to the tab that opened it.
  for (int i = 0; i < 4 && f.shell.overlayKind(f.controller) != OverlayKind::None; ++i) {
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
    CHECK(f.frame());
    static_cast<void>(f.focusedId());
  }
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
  const auto focused = f.focusedId();
  if (focused != "shell.lane-tab.dynamics")
    throw test::Failure{"Escape returned focus to " + (focused.empty() ? std::string{"nothing"} : focused)};
}

TEST_CASE("a cleared inline field publishes its empty text, not the committed value under it") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  CHECK(f.controller.beginTempoEdit().hasValue());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::TextField);
  // The creator clears the field: the node reads what is typed, never the toolbar's "120".
  CHECK(f.controller.updateTextComposition(U"", {}).hasValue());
  const auto cleared = nodeNow(f, "toolbar.tempo");
  CHECK(cleared.has_value());
  if (!cleared.has_value()) return;
  CHECK(cleared->role == native_ui::SemanticRole::TextField);
  if (!cleared->value.empty()) throw test::Failure{"the cleared field reads " + cleared->value};
  CHECK(cleared->editableValue.empty());
  CHECK(f.controller.updateTextComposition(U"9", {}).hasValue());
  const auto typed = nodeNow(f, "toolbar.tempo");
  CHECK(typed.has_value() && typed->value == "9");
  f.controller.cancelTextComposition();
}

TEST_CASE("a review's row pager keeps its place across a draft field and restarts on a new page") {
  using native_ui::SemanticAction;
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  // Enough notes with vibrato for the Clear vibrato review to have three pages of six rows.
  auto* region = f.session.project().findRegion(f.regionId);
  for (std::uint32_t i = 0U; i < 14U; ++i) {
    auto [lyric, note] = f.factory.makeNote(time::Tick{2160 + 360 * i}, time::Tick{240},
                                            static_cast<std::uint8_t>(60U + i % 12U), U"a",
                                            domain::Language::Japanese);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  }
  std::vector<domain::NoteId> ids;
  for (auto& note : region->notes) {
    note.vibrato.enabled = true;
    ids.push_back(note.id);
  }
  f.session.selection().selectOnly(ids.front());
  for (const auto& id : ids) f.session.selection().add(id);
  constexpr double kWidth = 480.0;
  constexpr double kHeight = 320.0;
  const auto shown = [&f](std::size_t row) {
    return nodeNow(f, f.controller.replacementReviewSemanticPrefix() + "row." + std::to_string(row))
        .has_value();
  };
  const auto pageDown = [&f] {
    const auto down = nodeNow(f, "shell.overlay.review.rows-down");
    if (!down.has_value() || !down->enabled)
      throw test::Failure{"the card shows every row; no row pager to test"};
    CHECK(f.shell.pointerDown(f.controller, press(centre(down->bounds))).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press(centre(down->bounds))).hasValue());
  };

  // The controller's next page starts from its first row, wherever the card had paged to.
  CHECK(f.frame(kWidth, kHeight));
  succeeds(f.controller.openClearVibratoReview(), "opening the review");
  CHECK(f.frame(kWidth, kHeight));
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::ReplacementReview);
  CHECK(shown(0U));
  pageDown();
  CHECK(f.frame(kWidth, kHeight));
  CHECK(!shown(0U));
  succeeds(f.shell.dispatchController(f.controller,
                                      f.controller.replacementReviewSemanticPrefix() + "action.1",
                                      SemanticAction::Activate),
           "moving to the next page");
  CHECK(f.frame(kWidth, kHeight));
  if (!shown(0U)) throw test::Failure{"the next page opened on a later row"};
  CHECK(f.shell.dispatchController(f.controller,
                                   f.controller.replacementReviewSemanticPrefix() + "action.4",
                                   SemanticAction::Activate)
            .hasValue());
  CHECK(f.frame(kWidth, kHeight));

  // A draft field opened from a paged inspector returns to the same rows.
  succeeds(f.controller.openVibratoInspector(), "opening the vibrato inspector");
  CHECK(f.frame(kWidth, kHeight));
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::ReplacementReview);
  pageDown();
  CHECK(f.frame(kWidth, kHeight));
  CHECK(!shown(0U));
  std::size_t visible = 99U;
  for (std::size_t i = 1U; i < 6U && visible == 99U; ++i)
    if (shown(i)) visible = i;
  CHECK(visible < 6U);
  succeeds(f.shell.dispatchController(f.controller,
                                      f.controller.replacementReviewSemanticPrefix() + "row." +
                                          std::to_string(visible),
                                      SemanticAction::Activate),
           "opening the field");
  CHECK(f.frame(kWidth, kHeight));
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::TextField);
  static_cast<void>(f.focusedId());
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(f.frame(kWidth, kHeight));
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::ReplacementReview);
  if (shown(0U)) throw test::Failure{"the review returned from its field at its first row"};
  CHECK(shown(visible));
}

// ---- The SINGER card's overflow menu ------------------------------------------------------------

namespace {

using native_ui::design::kSingerMenuButtonId;
using native_ui::design::singerMenuItemIds;

const std::string kMenuPrefix{"shell.overlay.singer-menu."};
const std::string kMenuButton{kSingerMenuButtonId};

// Frames the shell at a size with the SINGER card on screen: the full rack shows it, and the compact
// presentations open the inspector that carries it.
void showSingerCard(OverlayFixture& f, double width, double height) {
  CHECK(f.frame(width, height));
  if (f.shell.layout().rack != native_ui::design::RackPresentation::Full &&
      !f.shell.inspectorOpen()) {
    succeeds(f.shell.dispatchSemantic(f.controller, "shell.inspector", SemanticAction::Activate),
             "opening the inspector");
    CHECK(f.frame(width, height));
  }
}

void openSingerMenu(OverlayFixture& f, double width = 1600.0, double height = 900.0) {
  showSingerCard(f, width, height);
  succeeds(f.shell.dispatchSemantic(f.controller, kMenuButton, SemanticAction::Activate),
           "opening the singer menu");
  CHECK(f.frame(width, height));
  if (f.shell.overlayKind(f.controller) != OverlayKind::SingerMenu)
    throw test::Failure{"the singer menu did not open"};
}

// Closes whatever surface is up with Escape, as the creator would.
void escapeAll(OverlayFixture& f) {
  for (int i = 0; i < 4 && f.shell.overlayKind(f.controller) != OverlayKind::None; ++i)
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
}

bool disjoint(ui::Rect a, ui::Rect b) {
  return a.right() <= b.x + 0.001 || b.right() <= a.x + 0.001 || a.bottom() <= b.y + 0.001 ||
         b.bottom() <= a.y + 0.001;
}

bool inside(ui::Rect inner, ui::Rect outer) {
  return inner.x >= outer.x - 0.5 && inner.y >= outer.y - 0.5 &&
         inner.right() <= outer.right() + 0.5 && inner.bottom() <= outer.bottom() + 0.5;
}

void click(OverlayFixture& f, ui::Point at) {
  CHECK(f.shell.pointerDown(f.controller, press(at)).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(at)).hasValue());
}

}  // namespace

TEST_CASE("the Phonemes lane hosts the phoneme, unit and seam lanes with the controller's gestures") {
  ParityFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  const auto& l = f.shell.layout();
  const auto tabWidth = native_ui::design::singLaneTabWidth(l);
  const ui::Rect tab{l.laneTabs.x + 7.0 * (tabWidth + 4.0), l.laneTabs.y, tabWidth, l.laneTabs.height};
  CHECK(f.shell.pointerDown(f.controller, press(centre(tab))).hasValue());
  CHECK(f.shell.technicalLanesShown());
  CHECK(f.frame());
  const auto hosted = f.controller.hostedGrid();
  CHECK(hosted.has_value());
  CHECK(hosted->laneHeight == 0.0);
  const auto bands = f.shell.technicalBands();
  CHECK_NEAR(hosted->phonemeHeight + hosted->unitHeight + hosted->seamHeight,
             l.laneTimePlot.height, 1e-9);
  CHECK_NEAR(bands.band[2U].bottom(), l.laneTimePlot.bottom(), 1e-9);
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  const auto* tabNode = findNode(f.shell.accessibilityTree(), "shell.lane-tab.phonemes");
  CHECK(tabNode != nullptr && tabNode->selected);
  CHECK(findNode(f.shell.accessibilityTree(), "shell.lane.band.unit") != nullptr);

  // The phonemes the shell draws are the ones the controller hit-tests.
  const native_ui::EditorSceneLayout metrics{};
  ui::PhonemeLaneModel lane;
  lane.rebuild(f.controller.pianoRoll(), f.controller.sceneState().phonemes,
               metrics.phonemeContentTop(bands.band[0U].y),
               metrics.phonemeContentHeight(bands.band[0U].height));
  CHECK(!lane.visuals().empty());
  if (lane.visuals().empty()) return;
  const auto phoneme = lane.visuals().front().bounds;

  // A phoneme edge drag moves that boundary through the host.
  const ui::Point edge{phoneme.x + 2.0, phoneme.y + phoneme.height * 0.5};
  CHECK(f.shell.pointerDown(f.controller, press(edge)).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press({edge.x + 24.0, edge.y})).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press({edge.x + 24.0, edge.y})).hasValue());
  CHECK(f.count("boundary") == 1U);

  // A unit click cycles its variant and targets it for S (variant) and R (renderer); a
  // double-click opens the microscope.
  const ui::Point unitPoint{phoneme.x + phoneme.width * 0.5, centre(bands.band[1U]).y};
  CHECK(f.shell.pointerDown(f.controller, press(unitPoint)).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(unitPoint)).hasValue());
  CHECK(f.count("variant") == 1U);
  for (const auto key : {NativeKey::S, NativeKey::R}) {
    CHECK(!f.shell.handleShellKey(f.controller, KeyEvent{.key = key}));
    CHECK(f.controller.keyDown(KeyEvent{.key = key}).hasValue());
  }
  CHECK(f.count("variant") == 2U);
  CHECK(f.count("renderer") == 1U);
  auto twice = press(unitPoint);
  twice.clickCount = 2;
  CHECK(f.shell.pointerDown(f.controller, twice).hasValue());
  CHECK(f.count("microscope") == 1U);
  CHECK(f.controller.sampleMicroscopeOpen());
  f.controller.closeSampleMicroscope();

  // A seam click sets the boundary's amount and selects it for the seam keys.
  const auto seamRevision = f.controller.documentRevision();
  const ui::Point seamPoint{phoneme.x + 1.0, centre(bands.band[2U]).y};
  CHECK(f.shell.pointerDown(f.controller, press(seamPoint)).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(seamPoint)).hasValue());
  CHECK(f.controller.sceneState().selectedSeam.has_value());
  CHECK(f.controller.documentRevision() != seamRevision);

  // Collapsing a band is the project's lane presentation; its strip then only expands it.
  CHECK(f.shell.pointerDown(f.controller, press(centre(bands.toggle[1U]))).hasValue());
  const auto unitMode = [&] {
    return f.session.project().settings().technicalLanes[static_cast<std::size_t>(domain::TechnicalLane::Unit)].mode;
  };
  CHECK(unitMode() == domain::TechnicalLaneMode::Collapsed);
  CHECK(f.frame());
  const auto collapsed = f.shell.technicalBands();
  CHECK(collapsed.collapsed[1U]);
  CHECK(collapsed.band[1U].height < bands.band[1U].height);
  const auto variants = f.count("variant");
  CHECK(f.shell.pointerDown(f.controller, press({unitPoint.x, centre(collapsed.band[1U]).y})).hasValue());
  CHECK(unitMode() == domain::TechnicalLaneMode::Auto);
  CHECK(f.count("variant") == variants);

  // An expression tab takes the band back for its curve.
  const ui::Rect formant{l.laneTabs.x + (tabWidth + 4.0), l.laneTabs.y, tabWidth, l.laneTabs.height};
  CHECK(f.shell.pointerDown(f.controller, press(centre(formant))).hasValue());
  CHECK(f.frame());
  CHECK(!f.shell.technicalLanesShown());
  CHECK(f.controller.hostedGrid()->laneHeight > 0.0);
  CHECK(f.controller.hostedGrid()->phonemeHeight == 0.0);
}

TEST_CASE("the shell microscope sheet drags markers and auditions the unit like the classic plots") {
  ParityFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  CHECK(f.controller.openSampleMicroscope({f.noteId, 0U}).hasValue());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == native_ui::design::OverlayKind::SampleMicroscope);
  CHECK(!f.controller.sampleMicroscope()->markers().empty());
  // Find a marker by pressing across the sheet's middle; a press that hits nothing starts nothing.
  const auto slot = f.shell.overlaySlot(f.controller, f.controller.sceneState());
  bool dragged = false;
  for (const auto fraction : {0.35, 0.5, 0.65}) {
    const auto y = slot.y + slot.height * fraction;
    for (auto x = slot.x + slot.width * 0.06; x < slot.right() - slot.width * 0.06 && !dragged; x += 1.0) {
      CHECK(f.shell.pointerDown(f.controller, press({x, y})).hasValue());
      if (!f.controller.pointerGestureActive()) {
        CHECK(f.shell.pointerUp(f.controller, press({x, y})).hasValue());
        continue;
      }
      CHECK(f.shell.pointerMove(f.controller, press({x + 30.0, y})).hasValue());
      CHECK(f.shell.pointerUp(f.controller, press({x + 30.0, y})).hasValue());
      dragged = true;
    }
    if (dragged) break;
  }
  CHECK(dragged);
  CHECK(f.count("marker") == 1U);
  CHECK(f.controller.sampleMicroscopeOpen());
  // A double-click on the plots auditions the unit.
  auto twice = press({slot.x + slot.width * 0.5, slot.y + slot.height * 0.35});
  twice.clickCount = 2;
  CHECK(f.shell.pointerDown(f.controller, twice).hasValue());
  CHECK(f.count("play") == 1U);
}

TEST_CASE("the EXPORT workspace offers the final bounce's timing choice when the host has one") {
  ParityFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.frame());
  f.shell.setWorkspace(f.controller, native_ui::design::Workspace::Export);
  CHECK(f.frame());
  const auto bounce = f.shell.exportBounceButton();
  CHECK(bounce.width > 0.0);
  CHECK(f.shell.pointerDown(f.controller, press(centre(bounce))).hasValue());
  CHECK(f.count("bounce:host") == 1U);
  CHECK(f.controller.sceneState().bounceFollowHost);
  f.controller.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
  const auto* node = findNode(f.shell.accessibilityTree(), "shell.export.bounce");
  CHECK(node != nullptr && node->value == "Follow Host");
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.export.bounce", native_ui::SemanticAction::Activate).hasValue());
  CHECK(f.count("bounce:fixed") == 1U);
  CHECK(!f.controller.sceneState().bounceFollowHost);
}

TEST_CASE("the SINGER card's overflow button opens a modal menu laid out inside the window") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const auto ids = singerMenuItemIds();
  CHECK(ids.size() == 9U);
  const auto noteId =
      "note." + f.session.project().findRegion(f.regionId)->notes.front().id.toString();
  for (const auto [width, height] : {std::pair{720.0, 480.0}, std::pair{1100.0, 720.0},
                                     std::pair{480.0, 320.0}, std::pair{1600.0, 900.0}}) {
    const auto where = " at " + std::to_string(width) + "x" + std::to_string(height);
    showSingerCard(f, width, height);
    const auto button = nodeNow(f, kMenuButton);
    if (!button.has_value()) throw test::Failure{"no singer menu button" + where};
    CHECK(button->role == SemanticRole::Button);
    CHECK(button->value == "Closed");
    CHECK(offers(*button, SemanticAction::Activate));
    // The published button is the painted one, on the card beside Change voice.
    const auto painted = f.shell.layout().singerMenu;
    CHECK(button->bounds.x == painted.x && button->bounds.y == painted.y &&
          button->bounds.width == painted.width && button->bounds.height == painted.height);
    const auto change = nodeNow(f, "shell.change-voice");
    CHECK(change.has_value() && disjoint(button->bounds, change->bounds));
    CHECK(inside(button->bounds, f.shell.layout().singer));

    // A press on the button opens the menu, and its first item takes the keyboard.
    click(f, centre(button->bounds));
    CHECK(f.shell.singerMenuOpen());
    CHECK(f.frame(width, height));
    CHECK(f.shell.overlayKind(f.controller) == OverlayKind::SingerMenu);
    CHECK(f.focusedId() == ids.front());
    f.controller.rebuildAccessibilityTree();
    f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
    const auto root = f.shell.accessibilityTree().root();
    const auto* panel = findShellNode(root, kMenuPrefix + "panel");
    if (panel == nullptr) throw test::Failure{"no singer menu panel" + where};
    const ui::Rect window{0.0, 0.0, width, height};
    CHECK(inside(panel->bounds, window));
    CHECK(inside(panel->bounds, f.shell.layout().overlay));
    // Modal: nothing of the score, the lane or the card under it is published.
    CHECK(findShellNode(root, "timeline") == nullptr);
    CHECK(findShellNode(root, noteId) == nullptr);
    CHECK(findShellNode(root, "shell.lane") == nullptr);
    CHECK(findShellNode(root, kMenuButton) == nullptr);
    CHECK(findShellNode(root, "shell.change-voice") == nullptr);
    std::vector<ui::Rect> placed;
    std::set<double> columns;
    for (const auto& id : ids) {
      const auto* item = findShellNode(root, id);
      if (item == nullptr) throw test::Failure{id + " is not published" + where};
      CHECK(item->role == SemanticRole::Button);
      CHECK(item->enabled && offers(*item, SemanticAction::Activate));
      CHECK(!item->name.empty() && !item->description.empty());
      CHECK(item->bounds.width >= 120.0 && item->bounds.height >= 24.0);
      if (!inside(item->bounds, panel->bounds)) throw test::Failure{id + " leaves its card" + where};
      for (const auto& other : placed)
        if (!disjoint(item->bounds, other)) throw test::Failure{id + " overlaps an item" + where};
      placed.push_back(item->bounds);
      columns.insert(item->bounds.x);
    }
    // One column where the body is tall enough (720x480 and up); the 480x320 minimum folds the
    // items into columns rather than dropping any.
    if (height >= 480.0) CHECK(columns.size() == 1U);
    else CHECK(columns.size() > 1U);
    // The whole label is on the node, whatever the painted row elides.
    const auto* install = findShellNode(root, kMenuPrefix + "install-voicebank");
    CHECK(install != nullptr && install->name == "Install or relink voicebank");

    // A press outside the card closes it and runs nothing.
    click(f, {2.0, height - 2.0});
    CHECK(!f.shell.singerMenuOpen());
    CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
    CHECK(f.installerOpens == 0U && f.voicebankRefreshes == 0U);
    // Escape closes it and returns the keyboard to the button.
    succeeds(f.shell.dispatchSemantic(f.controller, kMenuButton, SemanticAction::Activate),
             "reopening the singer menu");
    CHECK(f.frame(width, height));
    CHECK(f.shell.overlayKind(f.controller) == OverlayKind::SingerMenu);
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
    CHECK(!f.shell.singerMenuOpen());
    CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
    CHECK(f.focusedId() == kMenuButton);
    const auto closed = nodeNow(f, kMenuButton);
    CHECK(closed.has_value() && closed->value == "Closed");
  }
}

TEST_CASE("the singer menu owns the keyboard: Tab and arrows walk it, chords pass, Escape returns") {
  using native_ui::design::ShellHostActions;
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  // The host handles Command-S itself; every other chord stops at the menu.
  ShellHostActions actions;
  actions.applicationShortcut = [](const KeyEvent& event) { return event.key == NativeKey::S; };
  f.shell.setHostActions(std::move(actions));
  const auto ids = singerMenuItemIds();
  CHECK(f.frame());
  // The button takes focus and Enter opens the menu from the keyboard alone.
  succeeds(f.shell.dispatchSemantic(f.controller, kMenuButton, SemanticAction::SetFocus),
           "focusing the button");
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Enter}));
  CHECK(f.shell.singerMenuOpen());
  CHECK(f.frame());
  CHECK(f.focusedId() == ids[0]);
  const auto tab = [&f](bool shift) {
    CHECK(f.shell.handleShellKey(f.controller,
                                 KeyEvent{.key = NativeKey::Tab, .modifiers = {.shift = shift}}));
    return f.focusedId();
  };
  CHECK(tab(false) == ids[1]);
  CHECK(tab(false) == ids[2]);
  CHECK(tab(true) == ids[1]);
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Down}));
  CHECK(f.focusedId() == ids[2]);
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Up}));
  CHECK(f.focusedId() == ids[1]);
  // Tab never leaves the menu: a full lap lands only on its own items and the display switch.
  auto menuIds = ids;
  for (const auto* mode : {"character.full", "character.minimal", "character.off"})
    menuIds.push_back(kMenuPrefix + mode);
  for (std::size_t i = 0U; i < menuIds.size() + 2U; ++i) {
    const auto id = tab(false);
    CHECK(std::find(menuIds.begin(), menuIds.end(), id) != menuIds.end());
  }
  // Plain editing keys never reach the covered score.
  const auto revision = f.controller.documentRevision();
  const auto selected = f.session.selection().noteIds();
  for (const auto key : {NativeKey::Delete, NativeKey::Backspace, NativeKey::D, NativeKey::Left})
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = key}));
  CHECK(f.controller.documentRevision() == revision);
  CHECK(f.session.selection().noteIds() == selected);
  // Command and Option chords are application commands: the host's own pass through, the rest stop.
  CHECK(!f.shell.handleShellKey(f.controller,
                                KeyEvent{.key = NativeKey::S, .modifiers = {.command = true}}));
  CHECK(f.shell.handleShellKey(f.controller,
                               KeyEvent{.key = NativeKey::D, .modifiers = {.command = true}}));
  CHECK(f.shell.handleShellKey(f.controller,
                               KeyEvent{.key = NativeKey::Delete, .modifiers = {.alt = true}}));
  CHECK(f.controller.documentRevision() == revision);
  CHECK(f.shell.singerMenuOpen());
  CHECK(f.installerOpens == 0U && f.voicebankRefreshes == 0U);
  // Escape closes it and the keyboard is back on the button.
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(!f.shell.singerMenuOpen());
  CHECK(f.focusedId() == kMenuButton);
}

TEST_CASE("every singer menu item runs the controller's own command by pointer, keys and host path") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  // The vibrato inspector edits the selected notes.
  f.session.selection().selectOnly(f.session.project().findRegion(f.regionId)->notes.front().id);
  const auto revision = f.controller.documentRevision();
  const auto ids = singerMenuItemIds();
  const auto item = [](std::string_view key) { return kMenuPrefix + std::string{key}; };

  // Every item takes focus through the host's accessibility path; the button under the menu is
  // not on screen, so it is refused there.
  openSingerMenu(f);
  for (const auto& id : ids) {
    succeeds(f.shell.dispatchController(f.controller, id, SemanticAction::SetFocus), "focus " + id);
    CHECK(f.focusedId() == id);
  }
  CHECK(!f.shell.dispatchController(f.controller, kMenuButton, SemanticAction::SetFocus).hasValue());

  // Replacement review (host path): the controller's find/replace field opens, and the menu is gone.
  succeeds(f.shell.dispatchController(f.controller, item("replacement-review"),
                                      SemanticAction::Activate),
           "replacement review");
  CHECK(!f.shell.singerMenuOpen());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::TextField);
  CHECK(f.controller.textInputActive());
  escapeAll(f);
  CHECK(!f.controller.textInputActive());
  CHECK(f.focusedId() == kMenuButton);

  // Dynamics inspector (pointer): the review sheet with the dynamics plot.
  openSingerMenu(f);
  const auto dynamics = nodeNow(f, item("dynamics"));
  CHECK(dynamics.has_value());
  if (dynamics.has_value()) click(f, centre(dynamics->bounds));
  CHECK(!f.shell.singerMenuOpen());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::ReplacementReview);
  CHECK(f.controller.sceneState().replacementReview.dynamicsPlot.has_value());
  escapeAll(f);
  CHECK(f.focusedId() == kMenuButton);

  // Vibrato inspector (Enter on the focused item).
  openSingerMenu(f);
  succeeds(f.shell.dispatchController(f.controller, item("vibrato"), SemanticAction::SetFocus),
           "focus vibrato");
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Enter}));
  CHECK(!f.shell.singerMenuOpen());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::ReplacementReview);
  CHECK(f.controller.sceneState().replacementReview.visible);
  escapeAll(f);

  // Phoneme review (Space on the focused item).
  openSingerMenu(f);
  succeeds(f.shell.dispatchController(f.controller, item("phoneme-review"),
                                      SemanticAction::SetFocus),
           "focus phoneme review");
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Space}));
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::PhonemeReview);
  escapeAll(f);
  CHECK(f.focusedId() == kMenuButton);

  // Change voice (the shell's own dispatch): the voice browser, whose Escape returns to the button
  // the creator opened it from.
  openSingerMenu(f);
  succeeds(f.shell.dispatchSemantic(f.controller, item("change-voice"), SemanticAction::Activate),
           "change voice");
  CHECK(f.frame());
  CHECK(f.controller.voicebankBrowserVisible());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::VoicebankBrowser);
  escapeAll(f);
  CHECK(!f.controller.voicebankBrowserVisible());
  CHECK(f.focusedId() == kMenuButton);

  // Install or relink (host path) and rescan (pointer) reach the host's own commands once each; no
  // surface follows, so the keyboard is back on the button.
  openSingerMenu(f);
  succeeds(f.shell.dispatchController(f.controller, item("install-voicebank"),
                                      SemanticAction::Activate),
           "install voicebank");
  CHECK(f.installerOpens == 1U);
  CHECK(!f.shell.singerMenuOpen());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
  CHECK(f.focusedId() == kMenuButton);
  openSingerMenu(f);
  const auto rescan = nodeNow(f, item("refresh-voicebanks"));
  CHECK(rescan.has_value());
  if (rescan.has_value()) click(f, centre(rescan->bounds));
  CHECK(f.voicebankRefreshes == 1U);
  CHECK(!f.shell.singerMenuOpen());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
  // Opening reviews and browsers edits nothing.
  CHECK(f.controller.documentRevision() == revision);
}

TEST_CASE("the singer menu's Full, Minimal and Off switch sets the character display, not the score") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const std::array<std::pair<const char*, domain::CharacterDisplayMode>, 3U> modes{{
      {"character.full", domain::CharacterDisplayMode::Full},
      {"character.minimal", domain::CharacterDisplayMode::Minimal},
      {"character.off", domain::CharacterDisplayMode::Off},
  }};
  const auto revision = f.controller.documentRevision();
  // Each size, including the minimum window, keeps the switch inside the menu under its commands.
  for (const auto [width, height] : {std::pair{480.0, 320.0}, std::pair{1100.0, 720.0},
                                     std::pair{1600.0, 900.0}}) {
    const auto where = " at " + std::to_string(width) + "x" + std::to_string(height);
    openSingerMenu(f, width, height);
    f.controller.rebuildAccessibilityTree();
    f.shell.rebuildSemantics(f.controller, f.controller.sceneState());
    const auto root = f.shell.accessibilityTree().root();
    const auto* panel = findShellNode(root, kMenuPrefix + "panel");
    if (panel == nullptr) throw test::Failure{"no singer menu panel" + where};
    double lowestCommand = 0.0;
    for (const auto& id : singerMenuItemIds())
      if (const auto* item = findShellNode(root, id); item != nullptr)
        lowestCommand = std::max(lowestCommand, item->bounds.bottom());
    for (const auto& [key, mode] : modes) {
      const auto* segment = findShellNode(root, kMenuPrefix + key);
      if (segment == nullptr) throw test::Failure{std::string{key} + " is not published" + where};
      CHECK(segment->role == SemanticRole::Button && offers(*segment, SemanticAction::Activate));
      CHECK(segment->selected == (mode == f.controller.characterDisplay()));
      CHECK(!segment->description.empty());
      if (!inside(segment->bounds, panel->bounds))
        throw test::Failure{std::string{key} + " leaves its card" + where};
      CHECK(segment->bounds.y >= lowestCommand);
    }
    CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  }

  // Full by pointer: the menu closes, the keyboard is back on the button, the Stage is up.
  openSingerMenu(f);
  const auto full = nodeNow(f, kMenuPrefix + "character.full");
  CHECK(full.has_value());
  if (full.has_value()) click(f, centre(full->bounds));
  CHECK(f.controller.characterDisplay() == domain::CharacterDisplayMode::Full);
  CHECK(!f.shell.singerMenuOpen());
  CHECK(f.focusedId() == kMenuButton);
  CHECK(f.controller.sceneState().characterMode == domain::CharacterDisplayMode::Full);
  openSingerMenu(f);
  const auto lit = nodeNow(f, kMenuPrefix + "character.full");
  CHECK(lit.has_value() && lit->selected);

  // Minimal by Enter on the focused segment.
  succeeds(f.shell.dispatchController(f.controller, kMenuPrefix + "character.minimal",
                                      SemanticAction::SetFocus),
           "focus minimal");
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Enter}));
  CHECK(f.controller.characterDisplay() == domain::CharacterDisplayMode::Minimal);
  CHECK(!f.shell.singerMenuOpen());

  // Off through the host's accessibility path.
  openSingerMenu(f);
  succeeds(f.shell.dispatchController(f.controller, kMenuPrefix + "character.off",
                                      SemanticAction::Activate),
           "character off");
  CHECK(f.controller.characterDisplay() == domain::CharacterDisplayMode::Off);
  CHECK(f.controller.sceneState().characterMode == domain::CharacterDisplayMode::Off);
  // A view choice: the score is untouched and nothing reached the host's commands.
  CHECK(f.controller.documentRevision() == revision);
  CHECK(f.installerOpens == 0U && f.voicebankRefreshes == 0U);
}


TEST_CASE("a singer command the controller refuses is disabled with its reason until the menu reopens") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  const auto revision = f.controller.documentRevision();
  const auto style = kMenuPrefix + "style";
  const auto reading = kMenuPrefix + "japanese-reading";
  openSingerMenu(f);
  // This host connects no style bank: the controller refuses, and says why.
  const auto refused = f.shell.dispatchController(f.controller, style, SemanticAction::Activate);
  CHECK(!refused.hasValue());
  if (refused.hasValue()) return;
  const auto reason = refused.error().message;
  CHECK(reason.find("not connected") != std::string::npos);
  // The menu stays up with the item disabled and the controller's reason on it.
  CHECK(f.shell.singerMenuOpen());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::SingerMenu);
  const auto disabled = nodeNow(f, style);
  CHECK(disabled.has_value());
  if (!disabled.has_value()) return;
  CHECK(!disabled->enabled);
  CHECK(disabled->name == "Style coverage");
  CHECK(disabled->description == reason);
  CHECK(!offers(*disabled, SemanticAction::Activate));
  CHECK(offers(*disabled, SemanticAction::SetFocus));
  // Neither the pointer, Enter nor the host path runs it again; the menu stays open.
  click(f, centre(disabled->bounds));
  CHECK(f.shell.singerMenuOpen());
  succeeds(f.shell.dispatchController(f.controller, style, SemanticAction::SetFocus), "focus style");
  CHECK(f.focusedId() == style);
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Enter}));
  CHECK(f.shell.singerMenuOpen());
  CHECK(!f.shell.dispatchController(f.controller, style, SemanticAction::Activate).hasValue());
  // Japanese reading is refused through Space, for this host's own reason.
  succeeds(f.shell.dispatchController(f.controller, reading, SemanticAction::SetFocus),
           "focus Japanese reading");
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Space}));
  CHECK(f.shell.singerMenuOpen());
  const auto readingNode = nodeNow(f, reading);
  CHECK(readingNode.has_value() && !readingNode->enabled &&
        readingNode->description.find("not connected") != std::string::npos);
  // Every other item is still enabled.
  for (const auto& id : singerMenuItemIds()) {
    if (id == style || id == reading) continue;
    const auto other = nodeNow(f, id);
    CHECK(other.has_value() && other->enabled);
  }
  CHECK(f.frame());
  // A new opening asks the controller again.
  escapeAll(f);
  openSingerMenu(f);
  const auto reopened = nodeNow(f, style);
  CHECK(reopened.has_value() && reopened->enabled && reopened->description != reason);
  CHECK(!f.shell.dispatchController(f.controller, style, SemanticAction::Activate).hasValue());
  CHECK(f.controller.documentRevision() == revision);
}

TEST_CASE("the singer menu closes on a workspace switch, a resize and a replaced controller") {
  OverlayFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  // A workspace switch (a host command while the menu is up) closes it.
  openSingerMenu(f);
  f.shell.setWorkspace(f.controller, Workspace::Tune);
  CHECK(!f.shell.singerMenuOpen());
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
  // The card, and its menu, stay usable beside TUNE; returning to SING closes it again.
  succeeds(f.shell.dispatchSemantic(f.controller, kMenuButton, SemanticAction::Activate),
           "opening the menu over TUNE");
  CHECK(f.frame());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::SingerMenu);
  f.shell.setWorkspace(f.controller, Workspace::Sing);
  CHECK(!f.shell.singerMenuOpen());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);

  // A resize closes it; the button, still on the card, keeps the keyboard.
  openSingerMenu(f);
  CHECK(f.frame(1100.0, 720.0));
  CHECK(!f.shell.singerMenuOpen());
  CHECK(f.shell.overlayKind(f.controller) == OverlayKind::None);
  CHECK(f.focusedId() == kMenuButton);

  // A replaced controller (a project opened or recovered) starts with no menu.
  openSingerMenu(f, 1100.0, 720.0);
  native_ui::NativeEditorController replacement{f.session, f.factory, f.regionId, {}};
  replacement.resize(1100.0, 720.0);
  CHECK(f.shell.prepareFrame(replacement, 1100.0, 720.0));
  CHECK(!f.shell.singerMenuOpen());
  CHECK(f.shell.overlayKind(replacement) == OverlayKind::None);
  replacement.rebuildAccessibilityTree();
  f.shell.rebuildSemantics(replacement, replacement.sceneState());
  CHECK(findShellNode(f.shell.accessibilityTree().root(), kMenuPrefix + "panel") == nullptr);
}
