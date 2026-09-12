#include "test_framework.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/text/text_engine.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/synthesis/performance_compiler.hpp"
#include <cstdlib>
#include <string>
#include <limits>
#include <chrono>
#include <iostream>
#include <unordered_set>

namespace {
struct DynamicsUiFixture final {
  seam::application::ProjectFactory factory{7000U};
  seam::domain::RegionId regionId{};
  seam::domain::NoteId noteId{};
  seam::application::EditorSession session;
  DynamicsUiFixture() : session(makeProject()) {}
  seam::domain::Project makeProject() {
    auto project = factory.createProject("Dynamics UI");
    project.settings().characterDisplay = seam::domain::CharacterDisplayMode::Off;
    const auto track = factory.addVocalTrack(project, "Voice");
    regionId = factory.addRegion(project, track, "Region", seam::time::Tick{0}, seam::time::Tick{7680});
    auto [lyric, note] = factory.makeNote(seam::time::Tick{960}, seam::time::Tick{960}, 64U, U"edge", seam::domain::Language::English);
    noteId = note.id;
    project.findRegion(regionId)->lyrics.push_back(std::move(lyric));
    project.findRegion(regionId)->notes.push_back(std::move(note));
    return project;
  }
};
}

TEST_CASE("dynamics viewport handles extreme extents and local zoom pan fit without overflow") {
  using View = seam::ui::DynamicsPlotViewport; View view;
  const auto end = std::numeric_limits<std::int64_t>::max();
  view.navigate(View::Action::ZoomIn, end, 0.0); CHECK(view.resolve(end).start == 0); CHECK(view.resolve(end).end == end / 2);
  view.navigate(View::Action::Right, end); CHECK(view.resolve(end).start > 0);
  view.navigate(View::Action::ZoomOut, end); CHECK(view.resolve(end).end <= end);
  const auto before = view.resolve(end); view.navigate(View::Action::ZoomIn, end, std::numeric_limits<double>::quiet_NaN());
  CHECK(view.resolve(end).start == before.start); CHECK(view.resolve(end).end == before.end);
  CHECK(view.resolve(5).end <= 5); CHECK(view.resolve(5).end > view.resolve(5).start);
  for (int i = 0; i < 70; ++i) view.navigate(View::Action::ZoomIn, end, 1.0);
  CHECK(view.resolve(end).end - view.resolve(end).start == 1);
  view.navigate(View::Action::Fit, end); CHECK(view.resolve(end).start == 0); CHECK(view.resolve(end).end == end);
}

TEST_CASE("native dynamics navigation clips curves and keeps score and main timeline unchanged") {
  using namespace seam; DynamicsUiFixture fixture;
  CHECK(fixture.session.project().findRegion(fixture.regionId)->dynamicsAutomation.replacePoints(
      {{time::Tick{0}, 0.0F}, {time::Tick{7680}, domain::kMaximumDynamicsGain}}));
  const auto before = fixture.session.project();
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId};
  controller.resize(480.0, 320.0); CHECK(controller.openDynamicsInspector());
  auto plot = controller.sceneState().replacementReview.dynamicsPlot; CHECK(plot);
  const auto zoom = plot->navigation[0];
  const auto mainOrigin = controller.pianoRoll().timeline().originTick();
  const auto mainScale = controller.pianoRoll().timeline().pixelsPerQuarter();
  const auto overviewSamples = plot->target.size();
  CHECK(controller.pointerDown({.position = {zoom.x + 5.0, zoom.y + 5.0}, .button = native_ui::PointerButton::Left}));
  plot = controller.sceneState().replacementReview.dynamicsPlot;
  CHECK(plot->startTick == 1920); CHECK(plot->endTick == 5760); CHECK(plot->score.size() == 2U);
  CHECK_NEAR(plot->score.front().y, plot->bounds.y + plot->bounds.height * 0.75, 0.00001);
  CHECK_NEAR(plot->score.back().y, plot->bounds.y + plot->bounds.height * 0.25, 0.00001);
  CHECK(plot->handles.empty());
  if (const auto* capture = std::getenv("SEAM_DYNAMICS_ZOOM_CAPTURE")) {
    auto engine = text::TextEngine::createSystem(); CHECK(engine);
    native_ui::PixelSurface surface{480U,320U}; native_ui::RasterCanvas canvas{surface,1.0,engine.value().get()};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState()); CHECK(surface.writePpm(capture));
  }
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Right}));
  CHECK(controller.sceneState().replacementReview.dynamicsPlot->startTick == 2880);
  CHECK(controller.keyDown({.key = native_ui::NativeKey::R}));
  plot = controller.sceneState().replacementReview.dynamicsPlot;
  controller.scroll(0.0, -40.0, {plot->bounds.x, plot->bounds.y + 5.0}, {.command = true});
  CHECK(controller.sceneState().replacementReview.dynamicsPlot->startTick == 0);
  CHECK(controller.sceneState().replacementReview.dynamicsPlot->endTick == 3840);
  CHECK(controller.sceneState().replacementReview.dynamicsPlot->target.size() > overviewSamples);
  controller.rebuildAccessibilityTree(); std::string fit;
  for (const auto& child : controller.accessibilityTree().root().children) if (child.id.ends_with("zoom.2")) fit = child.id;
  CHECK(!fit.empty()); CHECK(controller.dispatchAccessibility(fit, native_ui::SemanticAction::Activate));
  CHECK(controller.sceneState().replacementReview.dynamicsPlot->endTick == 7680);
  CHECK(!controller.dispatchAccessibility(fit, native_ui::SemanticAction::Activate));
  CHECK(fixture.session.project() == before); CHECK(!fixture.session.canUndo());
  CHECK(controller.pianoRoll().timeline().originTick() == mainOrigin);
  CHECK(controller.pianoRoll().timeline().pixelsPerQuarter() == mainScale);
  CHECK(fixture.session.replaceProject(before)); CHECK(!controller.keyDown({.key = native_ui::NativeKey::Plus}));
}

TEST_CASE("shift dragging dynamics time preserves gain and rejects occupied destinations before region Apply") {
  using namespace seam; DynamicsUiFixture fixture;
  CHECK(fixture.session.project().findRegion(fixture.regionId)->dynamicsAutomation.replacePoints(
      {{time::Tick{0}, 0.25F}, {time::Tick{480}, 0.75F}}));
  const auto before = fixture.session.project();
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId};
  controller.resize(480.0, 320.0); CHECK(controller.openDynamicsInspector());
  auto plot = controller.sceneState().replacementReview.dynamicsPlot; CHECK(plot);
  const auto atTick = [&](int tick) { return plot->bounds.x + plot->bounds.width * static_cast<double>(tick) / 7680.0; };
  CHECK(controller.pointerDown({.position = plot->handles[0].position, .button = native_ui::PointerButton::Left, .modifiers = {.shift = true}}));
  // Releasing Shift mid-gesture must not switch from time to gain editing.
  CHECK(controller.pointerMove({.position = {atTick(480), plot->bounds.y}, .button = native_ui::PointerButton::Left}));
  CHECK(controller.pointerUp({.position = {atTick(480), plot->bounds.y}, .button = native_ui::PointerButton::Left}));
  CHECK(controller.sceneState().replacementReview.rows[0] == "Region tick: 480");
  CHECK(controller.sceneState().replacementReview.rows[1] == "Linear gain: 0.25");
  CHECK(!controller.replacementReviewAction(3U)); CHECK(fixture.session.project() == before);
  plot = controller.sceneState().replacementReview.dynamicsPlot; CHECK(plot->candidate);
  CHECK(controller.pointerDown({.position = *plot->candidate, .button = native_ui::PointerButton::Left, .modifiers = {.shift = true}}));
  CHECK(controller.pointerUp({.position = {atTick(960), plot->bounds.bottom()}, .button = native_ui::PointerButton::Left}));
  CHECK(controller.sceneState().replacementReview.rows[0] == "Region tick: 960");
  CHECK(controller.sceneState().replacementReview.rows[1] == "Linear gain: 0.25");
  CHECK(controller.replacementReviewAction(3U)); CHECK(fixture.session.project() == before);
  CHECK(controller.replacementReviewAction(3U));
  auto expected = before;
  CHECK(expected.findRegion(fixture.regionId)->dynamicsAutomation.replacePoints({{time::Tick{480}, 0.75F}, {time::Tick{960}, 0.25F}}));
  CHECK(fixture.session.project() == expected); CHECK(fixture.session.undo()); CHECK(fixture.session.project() == before);
  CHECK(fixture.session.redo()); CHECK(fixture.session.project() == expected);
}

TEST_CASE("dynamics time dragging uses zoomed bounds and rejects nonfinite input") {
  using namespace seam; DynamicsUiFixture fixture;
  CHECK(fixture.session.project().findRegion(fixture.regionId)->dynamicsAutomation.upsert({time::Tick{3840}, 0.5F}));
  const auto before = fixture.session.project();
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId};
  controller.resize(480.0, 320.0); CHECK(controller.openDynamicsInspector()); CHECK(controller.keyDown({.key = native_ui::NativeKey::Plus}));
  auto plot = controller.sceneState().replacementReview.dynamicsPlot; CHECK(plot->startTick == 1920); CHECK(plot->endTick == 5760);
  CHECK(controller.pointerDown({.position = plot->handles[0].position, .button = native_ui::PointerButton::Left, .modifiers = {.shift = true}}));
  CHECK(controller.pointerUp({.position = {plot->bounds.right() + 100.0, plot->bounds.y}, .button = native_ui::PointerButton::Left}));
  CHECK(controller.sceneState().replacementReview.rows[0] == "Region tick: 5760");
  CHECK(controller.sceneState().replacementReview.rows[1] == "Linear gain: 0.5");
  plot = controller.sceneState().replacementReview.dynamicsPlot;
  CHECK(controller.pointerDown({.position = *plot->candidate, .button = native_ui::PointerButton::Left, .modifiers = {.shift = true}}));
  CHECK(!controller.pointerMove({.position = {std::numeric_limits<double>::infinity(), plot->bounds.y}, .button = native_ui::PointerButton::Left}));
  CHECK(controller.sceneState().replacementReview.rows[0] == "Region tick: 5760");
  CHECK(controller.replacementReviewAction(4U)); CHECK(fixture.session.project() == before); CHECK(!fixture.session.canUndo());
}

TEST_CASE("dynamics dragging maps extreme integers while draft points obey the region duration") {
  using namespace seam; DynamicsUiFixture fixture;
  const auto maximum = std::numeric_limits<std::int64_t>::max();
  CHECK(ui::DynamicsPlotViewport::tickAtFraction({0, maximum}, 1.0) == maximum);
  CHECK(ui::DynamicsPlotViewport::tickAtFraction({maximum - 10, maximum}, 0.5) == maximum - 5);
  CHECK(!ui::DynamicsPlotViewport::tickAtFraction({0, maximum}, std::numeric_limits<double>::infinity()));
  auto model = ui::DynamicsLaneModel::prepare(fixture.session, fixture.regionId); CHECK(model);
  CHECK(model.value().upsert({time::Tick{0}, 0.25F})); const auto curve = model.value().curve();
  CHECK(!model.value().upsert({time::Tick{maximum}, 0.75F}));
  CHECK(!model.value().move(time::Tick{0}, {time::Tick{7681}, 0.25F})); CHECK(model.value().curve() == curve);
  CHECK(model.value().validatePoint({time::Tick{7680}, 0.25F}));
  const auto before = fixture.session.project();
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {.beginTextInput = [](const auto&) {}}};
  controller.resize(480.0, 320.0); CHECK(controller.openDynamicsInspector());
  CHECK(controller.replacementReviewAction(2U)); CHECK(controller.openReplacementRow(0U));
  CHECK(!controller.commitTextComposition(U"7681")); CHECK(!controller.sceneState().replacementReview.enabled[3]);
  CHECK(controller.replacementReviewAction(4U)); CHECK(fixture.session.project() == before);
}

TEST_CASE("native dynamics inspector edits point drafts and publishes the region once") {
  using namespace seam;
  DynamicsUiFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region->dynamicsAutomation.replacePoints({{time::Tick{0}, 0.5F}, {time::Tick{480}, 1.0F}}));
  const auto source = fixture.session.project(); unsigned changes = 0U; std::u32string initial;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [&](const native_ui::TextInputRequest& request) { initial = request.currentText; }, .documentChanged = [&] { ++changes; }}};
  controller.resize(960.0, 640.0); controller.rebuildAccessibilityTree();
  CHECK(controller.dispatchAccessibility("inspector.dynamics", native_ui::SemanticAction::Activate));
  controller.resize(480.0, 320.0);
  CHECK(controller.sceneState().replacementReview.dockedInspector); CHECK(!controller.sceneState().replacementReview.enabled[3]);
  CHECK(controller.openReplacementRow(0U)); CHECK(controller.sceneState().replacementReview.rows.size() == 2U);
  const native_ui::EditorSceneLayout layout;
  const auto gain = layout.reviewRowBounds(480.0, 320.0, 1U, true);
  CHECK(controller.pointerDown({.position = {gain.x + 5.0, gain.y + 5.0}, .button = native_ui::PointerButton::Left}));
  CHECK(initial == U"0.5"); CHECK(controller.sceneState().boundedInputLabel.starts_with("DYNAMICS:"));
  CHECK(!controller.commitTextComposition(U"nan")); CHECK(!controller.sceneState().replacementReview.enabled[3]);
  CHECK(controller.openReplacementRow(1U)); CHECK(controller.commitTextComposition(U"0.25"));
  CHECK(controller.replacementReviewAction(3U)); CHECK(changes == 0U); CHECK(fixture.session.project() == source);
  std::string oldApply; controller.rebuildAccessibilityTree();
  for (const auto& child : controller.accessibilityTree().root().children) if (child.id.ends_with("action.3")) oldApply = child.id;
  CHECK(!oldApply.empty());
  CHECK(controller.replacementReviewAction(2U)); // New point, not a score mutation.
  CHECK(!controller.replacementReviewAction(3U)); // Default tick zero is occupied; do not replace it.
  controller.rebuildAccessibilityTree(); const auto tickField = controller.accessibilityTree().root().children.at(0U).id;
  CHECK(controller.dispatchAccessibility(tickField, native_ui::SemanticAction::SetFocus));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Enter})); CHECK(controller.commitTextComposition(U"960"));
  CHECK(controller.openReplacementRow(1U)); controller.cancelTextComposition(); CHECK(controller.replacementReviewOpen());
  CHECK(controller.replacementReviewAction(3U));
  CHECK(controller.openReplacementRow(1U)); CHECK(controller.replacementReviewAction(2U)); // Delete tick 480 from draft.
  CHECK(!controller.dispatchAccessibility(oldApply, native_ui::SemanticAction::Activate));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Delete})); CHECK(fixture.session.project() == source);
  controller.rebuildAccessibilityTree(); const auto panel = controller.accessibilityTree().root().bounds;
  for (const auto& child : controller.accessibilityTree().root().children) {
    CHECK(child.bounds.x >= panel.x); CHECK(child.bounds.y >= panel.y);
    CHECK(child.bounds.x + child.bounds.width <= panel.x + panel.width);
    CHECK(child.bounds.y + child.bounds.height <= panel.y + panel.height);
  }
  if (const auto* capture = std::getenv("SEAM_DYNAMICS_INSPECTOR_CAPTURE")) {
    auto engine = text::TextEngine::createSystem(); CHECK(engine);
    native_ui::PixelSurface surface{480U,320U}; native_ui::RasterCanvas canvas{surface,1.0,engine.value().get()};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState()); CHECK(surface.writePpm(capture));
  }
  auto expected = source;
  CHECK(expected.findRegion(fixture.regionId)->dynamicsAutomation.replacePoints({{time::Tick{0}, 0.25F}, {time::Tick{960}, 1.0F}}));
  CHECK(controller.replacementReviewAction(3U)); CHECK(changes == 1U); CHECK(fixture.session.project() == expected);
  CHECK(fixture.session.undo()); CHECK(fixture.session.project() == source);
  CHECK(fixture.session.redo()); CHECK(fixture.session.project() == expected);
  CHECK(controller.openDynamicsInspector()); CHECK(controller.replacementReviewAction(4U));
  fixture.session.selection().selectOnly(fixture.noteId);
  CHECK(controller.openVibratoInspector()); CHECK(controller.sceneState().replacementReview.status.find("Vibrato") != std::string::npos);
}

TEST_CASE("native dynamics point fields reject replaced documents and refresh safely") {
  using namespace seam; DynamicsUiFixture fixture;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {.beginTextInput = [](const auto&) {}}};
  controller.resize(960.0, 640.0); controller.rebuildAccessibilityTree();
  CHECK(controller.dispatchAccessibility("inspector.dynamics", native_ui::SemanticAction::SetFocus));
  const auto* entry = controller.accessibilityTree().focusedNode(); CHECK(entry); CHECK(entry->id == "inspector.dynamics");
  const auto entryBounds = entry->bounds;
  CHECK(controller.pointerDown({.position = {entryBounds.x + 5.0, entryBounds.y + 5.0}, .button = native_ui::PointerButton::Left}));
  CHECK(controller.replacementReviewOpen());
  CHECK(controller.replacementReviewAction(2U)); CHECK(controller.openReplacementRow(0U));
  const auto source = fixture.session.project(); CHECK(fixture.session.replaceProject(source));
  CHECK(!controller.commitTextComposition(U"120")); CHECK(controller.replacementReviewOpen());
  CHECK(!controller.sceneState().replacementReview.enabled[3]); CHECK(!controller.openReplacementRow(0U));
  CHECK(controller.replacementReviewAction(5U)); CHECK(controller.replacementReviewAction(5U)); // Back, then refresh.
  CHECK(controller.replacementReviewAction(2U)); CHECK(controller.openReplacementRow(1U));
  CHECK(!controller.commitTextComposition(std::u32string(65U, U'1')));
  CHECK(controller.openReplacementRow(1U)); CHECK(controller.commitTextComposition(U"0"));
  CHECK(controller.replacementReviewAction(3U)); CHECK(controller.sceneState().replacementReview.enabled[3]);
  CHECK(controller.replacementReviewAction(5U)); CHECK(!controller.sceneState().replacementReview.enabled[3]);
  CHECK(fixture.session.project() == source); CHECK(!fixture.session.canUndo());
  CHECK(controller.replacementReviewAction(4U));
}

TEST_CASE("native dynamics point paging survives deleting the final page without changing the score") {
  using namespace seam; DynamicsUiFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  for (int i = 0; i < 3; ++i) CHECK(region->dynamicsAutomation.upsert({time::Tick{i * 120}, 1.0F}));
  const auto before = fixture.session.project();
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId};
  controller.resize(480.0, 320.0); CHECK(controller.openDynamicsInspector());
  CHECK(controller.sceneState().replacementReview.rows.size() == 2U);
  CHECK(controller.replacementReviewAction(1U)); CHECK(controller.sceneState().replacementReview.rows.size() == 1U);
  CHECK(controller.openReplacementRow(0U)); CHECK(controller.replacementReviewAction(2U));
  CHECK(controller.sceneState().replacementReview.rows.size() == 2U);
  CHECK(!controller.sceneState().replacementReview.enabled[0]); CHECK(!controller.sceneState().replacementReview.enabled[1]);
  CHECK(controller.replacementReviewAction(4U)); CHECK(fixture.session.project() == before); CHECK(!fixture.session.canUndo());
}

TEST_CASE("dynamics curve handles draft gain without altering saved score and share exact field editing") {
  using namespace seam; DynamicsUiFixture fixture;
  CHECK(fixture.session.project().findRegion(fixture.regionId)->dynamicsAutomation.replacePoints(
      {{time::Tick{0}, 0.5F}, {time::Tick{480}, 1.0F}}));
  const auto before = fixture.session.project(); unsigned changes = 0U;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [](const auto&) {}, .documentChanged = [&] { ++changes; }}};
  controller.resize(480.0, 320.0); CHECK(controller.openDynamicsInspector());
  const auto plot = controller.sceneState().replacementReview.dynamicsPlot; CHECK(plot);
  CHECK(plot->handles.size() == 2U); CHECK(plot->endTick == 7680);
  CHECK(plot->score.size() == 4U); CHECK(plot->draft.size() == 4U);
  CHECK(plot->bounds.y > native_ui::EditorSceneLayout{}.reviewRowBounds(480.0, 320.0, 2U, true).bottom());
  CHECK(plot->bounds.bottom() < native_ui::EditorSceneLayout{}.reviewButtonBounds(480.0, 320.0, 0U, true).y);
  CHECK(controller.pointerDown({.position = plot->handles[1].position, .button = native_ui::PointerButton::Left}));
  CHECK(controller.pointerMove({.position = {plot->bounds.x, plot->bounds.bottom() + 100.0}, .button = native_ui::PointerButton::Left}));
  CHECK(controller.pointerUp({.position = {plot->bounds.x, plot->bounds.bottom() + 100.0}, .button = native_ui::PointerButton::Left}));
  auto view = controller.sceneState().replacementReview; CHECK(view.rows[1] == "Linear gain: 0");
  CHECK(view.dynamicsPlot->candidate); CHECK(view.dynamicsPlot->candidate->y == plot->bounds.bottom());
  CHECK(view.dynamicsPlot->score[2].y == plot->score[2].y); CHECK(view.dynamicsPlot->draft[2].y == plot->draft[2].y);
  CHECK(fixture.session.project() == before); CHECK(changes == 0U);
  CHECK(controller.replacementReviewAction(3U)); // Commit the candidate to draft, not to score.
  view = controller.sceneState().replacementReview;
  CHECK(view.dynamicsPlot->score[2].y != view.dynamicsPlot->draft[2].y);
  controller.rebuildAccessibilityTree(); std::string handle;
  for (const auto& child : controller.accessibilityTree().root().children) if (child.id.ends_with("point.1")) handle = child.id;
  CHECK(!handle.empty()); CHECK(controller.dispatchAccessibility(handle, native_ui::SemanticAction::SetFocus));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Enter})); CHECK(controller.openReplacementRow(1U));
  CHECK(controller.commitTextComposition(U"0.25")); CHECK(controller.replacementReviewAction(3U));
  CHECK(!controller.dispatchAccessibility(handle, native_ui::SemanticAction::Activate));
  CHECK(controller.replacementReviewAction(3U)); CHECK(changes == 1U);
  auto expected = before; CHECK(expected.findRegion(fixture.regionId)->dynamicsAutomation.upsert({time::Tick{480}, 0.25F}));
  CHECK(fixture.session.project() == expected); CHECK(fixture.session.undo()); CHECK(fixture.session.project() == before);
  CHECK(fixture.session.redo()); CHECK(fixture.session.project() == expected);
}

TEST_CASE("dynamics graphical dragging rejects changed documents and nonfinite coordinates") {
  using namespace seam; DynamicsUiFixture fixture;
  CHECK(fixture.session.project().findRegion(fixture.regionId)->dynamicsAutomation.upsert({time::Tick{0}, 1.0F}));
  const auto before = fixture.session.project();
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId};
  controller.resize(960.0, 640.0); CHECK(controller.openDynamicsInspector());
  auto plot = controller.sceneState().replacementReview.dynamicsPlot; CHECK(plot);
  CHECK(controller.pointerDown({.position = plot->handles[0].position, .button = native_ui::PointerButton::Left}));
  CHECK(!controller.pointerMove({.position = {plot->bounds.x, std::numeric_limits<double>::quiet_NaN()}, .button = native_ui::PointerButton::Left}));
  CHECK(controller.sceneState().replacementReview.rows[1] == "Linear gain: 1");
  CHECK(controller.replacementReviewAction(5U));
  plot = controller.sceneState().replacementReview.dynamicsPlot;
  CHECK(controller.pointerDown({.position = plot->handles[0].position, .button = native_ui::PointerButton::Left}));
  CHECK(fixture.session.replaceProject(before));
  CHECK(!controller.pointerMove({.position = {plot->bounds.x, plot->bounds.y}, .button = native_ui::PointerButton::Left}));
  CHECK(!controller.sceneState().replacementReview.enabled[3]);
  CHECK(controller.replacementReviewAction(4U)); CHECK(fixture.session.project() == before); CHECK(!fixture.session.canUndo());
}

TEST_CASE("dynamics handles choose the nearest dense point and cancel dragging after resize") {
  using namespace seam; DynamicsUiFixture fixture;
  CHECK(fixture.session.project().findRegion(fixture.regionId)->dynamicsAutomation.replacePoints(
      {{time::Tick{0}, 1.0F}, {time::Tick{1}, 1.0F}}));
  const auto before = fixture.session.project();
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId};
  controller.resize(480.0, 320.0); CHECK(controller.openDynamicsInspector());
  const auto plot = controller.sceneState().replacementReview.dynamicsPlot; CHECK(plot);
  CHECK(controller.pointerDown({.position = plot->handles[1].position, .button = native_ui::PointerButton::Left}));
  CHECK(controller.sceneState().replacementReview.rows[0] == "Region tick: 1");
  controller.resize(960.0, 640.0);
  CHECK(!controller.pointerMove({.position = {plot->bounds.x, plot->bounds.y}, .button = native_ui::PointerButton::Left}));
  CHECK(controller.sceneState().replacementReview.rows[1] == "Linear gain: 1");
  CHECK(controller.replacementReviewAction(5U));
  const auto freshPlot = controller.sceneState().replacementReview.dynamicsPlot; CHECK(freshPlot);
  CHECK(controller.pointerDown({.position = freshPlot->handles[1].position, .button = native_ui::PointerButton::Left}));
  CHECK(controller.pointerMove({.position = {freshPlot->bounds.x, freshPlot->bounds.y}})); // No held button retires a lost drag.
  CHECK(controller.pointerUp({.position = {freshPlot->bounds.x, freshPlot->bounds.y}, .button = native_ui::PointerButton::Left}));
  CHECK(controller.sceneState().replacementReview.rows[1] == "Linear gain: 1");
  CHECK(controller.replacementReviewAction(4U)); CHECK(fixture.session.project() == before);
}

TEST_CASE("dynamics inspector explains generated overrides and preserves manual replacement scope semantics") {
  using namespace seam; DynamicsUiFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  region->lyrics.front().surface = U"あ"; region->lyrics.front().language = domain::Language::Japanese;
  CHECK(region->dynamicsAutomation.upsert({time::Tick{0}, 1.0F}));
  const auto pronunciation = phonemizer::resolveJapanesePronunciation(*region); CHECK(pronunciation);
  region->performance.pronunciation = pronunciation.value().identity;
  region->performance.takes = {{.id = "generated-dynamics", .sourceRegionId = fixture.regionId,
      .capturedRevision = region->performance.revision,
      .resource = {domain::SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
      .pronunciation = pronunciation.value().identity, .generatorId = "fixture", .generatorVersion = "1",
      .range = {time::Tick{0}, time::Tick{7680}},
      .lanes = {{domain::PerformanceChannel::Dynamics, {{time::Tick{0}, 0.8}}}}}};
  region->performance.accepted = {{"generated-dynamics", domain::PerformanceChannel::Dynamics, fixture.noteId, time::Tick{0}}};
  region->performance.ownership = {
      {domain::PerformanceChannel::Dynamics, domain::PerformanceTimeRange{time::Tick{1440}, time::Tick{1920}}, domain::ManualPerformanceMode::Replace, {}},
      {domain::PerformanceChannel::Pitch, fixture.noteId, domain::ManualPerformanceMode::Replace, {}}};
  const auto before = fixture.session.project();
  auto model = ui::DynamicsLaneModel::prepare(fixture.session, fixture.regionId); CHECK(model);
  CHECK(model.value().influence().generatedSelections == 1U); CHECK(model.value().influence().manualReplacementScopes == 1U);
  const auto compiledBefore = synthesis::compileScorePerformance(before, *before.findRegion(fixture.regionId), 48000U); CHECK(compiledBefore);
  model.value().refreshTargetPreview(); CHECK(model.value().targetReady()); CHECK(model.value().targetError().empty());
  CHECK(!model.value().targetSamples().empty());
  for (const auto& sample : model.value().targetSamples()) {
    CHECK(sample.voice == 0U); CHECK(sample.note == fixture.noteId);
    CHECK(sample.linearGain == compiledBefore.value().at(before.tempoMap().sampleFrameAt(sample.tick, 48000U)).dynamicsGain);
  }
  CHECK(model.value().upsert({time::Tick{0}, 0.25F})); CHECK(!model.value().targetReady()); CHECK(model.value().targetSamples().empty());
  model.value().refreshTargetPreview(); CHECK(model.value().targetReady());
  bool generatedSeen = false, nativeSeen = false;
  for (const auto& sample : model.value().targetSamples()) {
    if (sample.tick < time::Tick{1440}) { CHECK(sample.linearGain == 0.8F); generatedSeen = true; }
    else { CHECK(sample.linearGain == 0.25F); nativeSeen = true; }
    CHECK(sample.selectedGeneratedGain == 0.8F);
  }
  CHECK(generatedSeen); CHECK(nativeSeen); CHECK(fixture.session.project() == before);
  model.value().refreshTargetPreview(ui::DynamicsLaneModel::TargetWindow{time::Tick{1438}, time::Tick{1442}});
  CHECK(model.value().targetReady()); CHECK(model.value().targetSamples().size() == 5U);
  for (std::size_t i = 0U; i < 5U; ++i) {
    const auto& sample = model.value().targetSamples()[i];
    CHECK(sample.tick == time::Tick{1438 + static_cast<std::int64_t>(i)});
    CHECK(sample.linearGain == (i < 2U ? 0.8F : 0.25F)); CHECK(sample.selectedGeneratedGain == 0.8F);
  }
  model.value().refreshTargetPreview(ui::DynamicsLaneModel::TargetWindow{time::Tick{1}, time::Tick{1}});
  CHECK(!model.value().targetReady()); CHECK(model.value().targetSamples().empty()); CHECK(!model.value().targetError().empty());
  model.value().refreshTargetPreview(ui::DynamicsLaneModel::TargetWindow{time::Tick{8000}, time::Tick{std::numeric_limits<std::int64_t>::max()}});
  CHECK(model.value().targetReady()); CHECK(model.value().targetSamples().empty());
  model.value().refreshTargetPreview(ui::DynamicsLaneModel::TargetWindow{time::Tick{1440}, time::Tick{1441}});
  CHECK(model.value().targetReady()); CHECK(model.value().targetSamples().size() == 2U);
  CHECK(model.value().targetSamples().front().linearGain == 0.25F);
  const auto generatedFrame = before.tempoMap().sampleFrameAt(time::Tick{1200}, 48000U);
  const auto ownedFrame = before.tempoMap().sampleFrameAt(time::Tick{1680}, 48000U);
  CHECK(compiledBefore.value().at(generatedFrame).dynamicsGain == 0.8F);
  CHECK(compiledBefore.value().at(ownedFrame).dynamicsGain == 1.0F);
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {.beginTextInput = [](const auto&) {}}};
  controller.resize(480.0, 320.0); CHECK(controller.openDynamicsInspector());
  CHECK(controller.sceneState().replacementReview.summary == "Generated dynamics may override this curve");
  CHECK(controller.sceneState().replacementReview.status == "Region: 1 generated / 1 native Replace scopes");
  CHECK(controller.sceneState().replacementReview.dynamicsPlot->influenceDescription.starts_with("1 accepted dynamics selections; 1 manual dynamics replacement scopes."));
  controller.rebuildAccessibilityTree(); bool found = false;
  for (const auto& child : controller.accessibilityTree().root().children) if (child.id.ends_with("curve")) {
    found = true; CHECK(child.description.find("does not claim ownership") != std::string::npos);
    CHECK(child.value.find("Unselected proposals and measured curves are not shown") != std::string::npos);
  }
  CHECK(found);
  if (const auto* capture = std::getenv("SEAM_DYNAMICS_INFLUENCE_CAPTURE")) {
    auto engine = text::TextEngine::createSystem(); CHECK(engine);
    native_ui::PixelSurface surface{480U,320U}; native_ui::RasterCanvas canvas{surface,1.0,engine.value().get()};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState()); CHECK(surface.writePpm(capture));
  }
  CHECK(controller.openReplacementRow(0U)); CHECK(controller.openReplacementRow(1U));
  CHECK(controller.commitTextComposition(U"0.25")); CHECK(controller.replacementReviewAction(3U));
  CHECK(fixture.session.project() == before); CHECK(controller.replacementReviewAction(3U));
  auto expected = before; CHECK(expected.findRegion(fixture.regionId)->dynamicsAutomation.upsert({time::Tick{0}, 0.25F}));
  CHECK(fixture.session.project() == expected);
  const auto compiledAfter = synthesis::compileScorePerformance(expected, *expected.findRegion(fixture.regionId), 48000U); CHECK(compiledAfter);
  CHECK(compiledAfter.value().at(generatedFrame).dynamicsGain == 0.8F); // Native edit is intentionally masked here.
  CHECK(compiledAfter.value().at(ownedFrame).dynamicsGain == 0.25F); // Existing manual ownership exposes the edit here.
  CHECK(fixture.session.undo()); CHECK(fixture.session.project() == before);
  CHECK(fixture.session.redo()); CHECK(fixture.session.project() == expected);
}

TEST_CASE("dynamics target samples retain polyphonic identities and expose compilation limits without blocking edits") {
  using namespace seam; DynamicsUiFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  auto [lyric, note] = fixture.factory.makeNote(time::Tick{960}, time::Tick{960}, 67U, U"la", domain::Language::English);
  region->lyrics.push_back(lyric); region->notes.push_back(note);
  auto model = ui::DynamicsLaneModel::prepare(fixture.session, fixture.regionId); CHECK(model);
  model.value().refreshTargetPreview(); CHECK(model.value().targetReady());
  bool first = false, second = false;
  for (const auto& sample : model.value().targetSamples()) {
    CHECK(sample.linearGain == 1.0F); CHECK(sample.tick >= time::Tick{960}); CHECK(sample.tick < time::Tick{1920});
    if (sample.note == fixture.noteId) { first = true; CHECK(sample.voice == 0U); }
    if (sample.note == note.id) { second = true; CHECK(sample.voice == 1U); }
  }
  CHECK(first); CHECK(second);
  for (int i = 0; i < 15; ++i) {
    auto [nextLyric, nextNote] = fixture.factory.makeNote(time::Tick{960}, time::Tick{960}, 60U, U"la", domain::Language::English);
    region->lyrics.push_back(nextLyric); region->notes.push_back(nextNote);
  }
  auto limited = ui::DynamicsLaneModel::prepare(fixture.session, fixture.regionId); CHECK(limited);
  limited.value().refreshTargetPreview(); CHECK(!limited.value().targetReady()); CHECK(!limited.value().targetError().empty());
  CHECK(limited.value().targetSamples().empty());
  CHECK(limited.value().upsert({time::Tick{0}, 0.5F})); CHECK(limited.value().apply(fixture.session, fixture.regionId));
  CHECK(fixture.session.project().findRegion(fixture.regionId)->dynamicsAutomation.valueAt(time::Tick{1000}) == 0.5F);
}

TEST_CASE("dynamics preview measures 4096 notes in 16 voices and explicitly rejects a 10000 note target") {
  using namespace seam;
  application::ProjectFactory factory{351000U}; auto project = factory.createProject("Dynamics capacity");
  const auto track = factory.addVocalTrack(project, "Singer");
  const auto id = factory.addRegion(project, track, "Region", time::Tick{0}, time::Tick{100000});
  const auto add = [&](std::size_t i) {
    auto [lyric, note] = factory.makeNote(time::Tick{static_cast<std::int64_t>(i / 16U) * 32}, time::Tick{16},
        60U, U"la", domain::Language::English);
    project.findRegion(id)->lyrics.push_back(lyric); project.findRegion(id)->notes.push_back(note);
  };
  for (std::size_t i = 0U; i < synthesis::kMaximumScoreVoiceAllocationNotes; ++i) add(i);
  application::EditorSession session{project};
  const auto begin = std::chrono::steady_clock::now();
  auto preview = ui::DynamicsLaneModel::prepare(session, id); CHECK(preview);
  const auto captured = std::chrono::steady_clock::now();
  preview.value().refreshTargetPreview(); const auto compiled = std::chrono::steady_clock::now();
  CHECK(preview.value().targetReady()); CHECK(preview.value().targetError().empty());
  CHECK(preview.value().targetSamples().size() <= 16U * 257U + 3U * synthesis::kMaximumScoreVoiceAllocationNotes);
  std::unordered_set<domain::NoteId> notes; std::unordered_set<std::size_t> voices;
  for (const auto& sample : preview.value().targetSamples()) {
    notes.insert(sample.note); voices.insert(sample.voice); CHECK(sample.linearGain == 1.0F); CHECK(!sample.selectedGeneratedGain);
  }
  CHECK(notes.size() == synthesis::kMaximumScoreVoiceAllocationNotes); CHECK(voices.size() == 16U);
  CHECK(session.project() == project); CHECK(!session.canUndo());
  std::cout << "dynamics-capacity-ms capture=" << std::chrono::duration<double, std::milli>(captured - begin).count()
      << " compile=" << std::chrono::duration<double, std::milli>(compiled - captured).count()
      << " samples=" << preview.value().targetSamples().size() << '\n';
  const auto resampleStart = std::chrono::steady_clock::now();
  preview.value().refreshTargetPreview(ui::DynamicsLaneModel::TargetWindow{time::Tick{500}, time::Tick{550}});
  const auto resampled = std::chrono::steady_clock::now();
  CHECK(preview.value().targetReady()); CHECK(!preview.value().targetSamples().empty());
  for (const auto& sample : preview.value().targetSamples()) CHECK(sample.tick >= time::Tick{500} && sample.tick <= time::Tick{550});
  std::cout << "dynamics-cached-window-ms=" << std::chrono::duration<double, std::milli>(resampled - resampleStart).count()
      << " samples=" << preview.value().targetSamples().size() << '\n';
  for (std::size_t i = synthesis::kMaximumScoreVoiceAllocationNotes; i < 10000U; ++i) add(i);
  CHECK(session.replaceProject(project));
  auto large = ui::DynamicsLaneModel::prepare(session, id); CHECK(large);
  large.value().refreshTargetPreview(); CHECK(!large.value().targetReady()); CHECK(large.value().targetSamples().empty());
  CHECK(large.value().targetError().find("4096") != std::string::npos);
  CHECK(large.value().upsert({time::Tick{0}, 0.5F})); CHECK(large.value().apply(session, id));
  auto expected = project; CHECK(expected.findRegion(id)->dynamicsAutomation.upsert({time::Tick{0}, 0.5F}));
  CHECK(session.project() == expected); CHECK(session.undo()); CHECK(session.project() == project);
  native_ui::NativeEditorController controller{session, factory, id, {.beginTextInput = [](const auto&) {}}};
  controller.resize(480.0, 320.0); CHECK(controller.openDynamicsInspector());
  CHECK(controller.sceneState().replacementReview.summary.find("4096") != std::string::npos);
  CHECK(controller.sceneState().replacementReview.dynamicsPlot->target.empty());
  CHECK(controller.replacementReviewAction(2U)); CHECK(controller.openReplacementRow(1U));
  CHECK(controller.commitTextComposition(U"0.5")); CHECK(controller.replacementReviewAction(3U));
  CHECK(controller.sceneState().replacementReview.enabled[3]); CHECK(controller.replacementReviewAction(3U));
  CHECK(session.project() == expected); CHECK(session.undo()); CHECK(session.project() == project);
}
