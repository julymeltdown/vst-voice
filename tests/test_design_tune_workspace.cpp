// The TUNE workspace, driven through a real SingShell, NativeEditorController and EditorSession:
// six channel curves on one graph, channel selection, one-gesture-one-undo edits, Escape, the macro
// knobs against SING's own knobs, the selected note's vibrato, and geometry at every contract size.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/domain/project.hpp"
#include "seam/native_ui/design/sing_layout.hpp"
#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_semantics.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/ui/expression_lane.hpp"
#include "seam/voice_design/recipe_resource.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

using namespace seam;
using native_ui::KeyEvent;
using native_ui::NativeKey;
using native_ui::PointerButton;
using native_ui::PointerEvent;
using native_ui::SemanticAction;
using native_ui::SemanticNode;
using native_ui::SemanticRole;
using native_ui::design::DesignMode;
using native_ui::design::DesignPreferences;
using native_ui::design::SingShell;
using native_ui::design::Workspace;

constexpr std::array<std::string_view, 6U> kIds{"formant", "breath", "tension",
                                                "air",     "gender", "growl"};

voice_design::VoiceRecipe tuneRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "design-tune";
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

struct TuneFixture final {
  application::ProjectFactory factory{9600U};
  domain::TrackId trackId{};
  domain::RegionId regionId{};
  // Every pitch command the host received, in order ("upsert", "move", "remove", "cycle").
  std::vector<std::string> pitchCalls;
  application::EditorSession session;
  native_ui::NativeEditorController controller;
  SingShell shell;
  native_ui::PixelSurface surface{1600U, 900U};
  double width{1600.0};
  double height{900.0};

  // pitchHost: the host wires the pitch callbacks as the standalone does (TechnicalEditController:
  // normalize, then one undoable command each); otherwise it offers none and cannot edit pitch.
  explicit TuneFixture(bool pitchHost = false)
      : session(makeProject()),
        controller{session, factory, regionId,
                   pitchHost ? pitchCallbacks() : native_ui::EditorHostCallbacks{}} {
    controller.resize(width, height);
    shell.activate({}, DesignPreferences{.mode = DesignMode::Emo});
  }

  domain::PitchAutomationPoint normalized(domain::PitchAutomationPoint point) const {
    point.cents = std::clamp(point.cents, -4800.0F, 4800.0F);
    point.tick = std::clamp(point.tick, time::Tick{0}, region().durationTick);
    return point;
  }

  native_ui::EditorHostCallbacks pitchCallbacks() {
    native_ui::EditorHostCallbacks callbacks;
    callbacks.upsertPitchPoint = [this](domain::PitchAutomationPoint point) {
      pitchCalls.emplace_back("upsert");
      return session.execute(std::make_unique<application::UpsertPitchAutomationPointCommand>(
          regionId, normalized(point)));
    };
    callbacks.movePitchPoint = [this](time::Tick from, domain::PitchAutomationPoint point) {
      pitchCalls.emplace_back("move");
      auto command = std::make_unique<application::CompositeCommand>("Move pitch automation point");
      command->add(std::make_unique<application::RemovePitchAutomationPointCommand>(regionId, from));
      command->add(std::make_unique<application::UpsertPitchAutomationPointCommand>(
          regionId, normalized(point)));
      return session.execute(std::move(command));
    };
    callbacks.removePitchPoint = [this](time::Tick tick) {
      pitchCalls.emplace_back("remove");
      return session.execute(
          std::make_unique<application::RemovePitchAutomationPointCommand>(regionId, tick));
    };
    callbacks.cyclePitchInterpolation = [this](time::Tick tick) -> core::Result<void> {
      pitchCalls.emplace_back("cycle");
      const auto& points = region().pitchAutomation.points();
      const auto found = std::find_if(points.begin(), points.end(),
                                      [tick](const auto& point) { return point.tick == tick; });
      if (found == points.end())
        return core::failure(core::ErrorCode::NotFound, "Pitch automation point is missing");
      auto updated = *found;
      updated.interpolation = updated.interpolation == domain::CurveInterpolation::Step
                                  ? domain::CurveInterpolation::Linear
                              : updated.interpolation == domain::CurveInterpolation::Linear
                                  ? domain::CurveInterpolation::Smooth
                                  : domain::CurveInterpolation::Step;
      return session.execute(
          std::make_unique<application::UpsertPitchAutomationPointCommand>(regionId, updated));
    };
    return callbacks;
  }

  const std::vector<domain::PitchAutomationPoint>& pitch() const {
    return region().pitchAutomation.points();
  }

  void storePitch(time::Tick tick, float cents) {
    if (!session.project().findRegion(regionId)->pitchAutomation.upsert({tick, cents}))
      throw test::Failure{"could not store a pitch point"};
  }

  domain::Project makeProject() {
    auto project = factory.createProject("Design tune");
    project.settings().characterDisplay = domain::CharacterDisplayMode::Off;
    trackId = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, trackId, "Phrase", time::Tick{0}, time::Tick{7680});
    auto [lyric, note] = factory.makeNote(time::Tick{960}, time::Tick{1920}, 72U, U"\u3042",
                                          domain::Language::Japanese);
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    const auto frozen = voice_design::freezeVoiceRecipeResource(tuneRecipe());
    if (!frozen) throw test::Failure{frozen.error().message};
    project.findVocalTrack(trackId)->proceduralRecipe = domain::ProceduralRecipeReference{
        .resource = frozen.value().identity, .path = "recipe.json", .style = "neutral"};
    return project;
  }

  const domain::VocalRegion& region() const { return *session.project().findRegion(regionId); }
  domain::Note& note() { return session.project().findRegion(regionId)->notes.front(); }

  // Paints one real frame at the current size so the shell records its layout.
  bool frame() {
    if (!shell.prepareFrame(controller, width, height)) return false;
    static_cast<void>(surface.resize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)));
    native_ui::RasterCanvas canvas{surface, 1.0, nullptr};
    return shell.paint(canvas, controller, controller.sceneState(), controller.playheadTick());
  }

  bool frameAt(double w, double h) {
    width = w;
    height = h;
    controller.resize(w, h);
    return frame();
  }

  bool openTune() {
    if (!frame()) return false;
    if (!shell.dispatchSemantic(controller, "shell.workspace.tune", SemanticAction::Activate))
      return false;
    return shell.workspace() == Workspace::Tune && frame();
  }

  std::vector<SemanticNode> nodes() {
    controller.rebuildAccessibilityTree();
    shell.rebuildSemantics(controller, controller.sceneState());
    return shell.accessibilityTree().root().children;
  }

  std::optional<SemanticNode> node(std::string_view id) {
    for (const auto& child : nodes()) {
      if (child.id == id) return child;
      for (const auto& grandchild : child.children)
        if (grandchild.id == id) return grandchild;
    }
    return std::nullopt;
  }

  std::string focusedId() {
    static_cast<void>(nodes());
    const auto* focused = shell.accessibilityTree().focusedNode();
    return focused == nullptr ? std::string{} : focused->id;
  }

  ui::Rect bounds(std::string_view id) {
    const auto found = node(id);
    if (!found) throw test::Failure{"missing node " + std::string{id}};
    return found->bounds;
  }
};

PointerEvent press(ui::Point p, bool shift = false) {
  return {.position = p, .button = PointerButton::Left, .modifiers = {.shift = shift}};
}

ui::Point at(ui::Rect r, double fx, double fy) { return {r.x + r.width * fx, r.y + r.height * fy}; }

bool intersects(ui::Rect a, ui::Rect b) {
  return a.x < b.right() - 0.01 && b.x < a.right() - 0.01 && a.y < b.bottom() - 0.01 &&
         b.y < a.bottom() - 0.01;
}

bool inside(ui::Rect inner, ui::Rect outer) {
  return inner.x >= outer.x - 0.01 && inner.y >= outer.y - 0.01 &&
         inner.right() <= outer.right() + 0.01 && inner.bottom() <= outer.bottom() + 0.01;
}

std::size_t differingPixels(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b,
                            std::uint32_t width, ui::Rect r) {
  std::size_t count = 0U;
  for (auto y = static_cast<std::uint32_t>(r.y); y < static_cast<std::uint32_t>(r.bottom()); ++y)
    for (auto x = static_cast<std::uint32_t>(r.x); x < static_cast<std::uint32_t>(r.right()); ++x)
      if (a[y * width + x] != b[y * width + x]) ++count;
  return count;
}

// Stores one point for a channel through the TUNE command path, then closes the lane.
bool storePoint(native_ui::NativeEditorController& controller, std::size_t channel, time::Tick tick,
                float fraction) {
  const auto d = ui::describeExpressionChannel(ui::expressionChannelAt(channel));
  if (!controller.openExpressionLane(ui::expressionChannelAt(channel))) return false;
  const auto amount = d.minimum + fraction * (d.maximum - d.minimum);
  if (!controller.pressExpressionPoint(std::nullopt, tick, amount)) return false;
  return static_cast<bool>(controller.releaseExpressionPoint());
}

}  // namespace

TEST_CASE("TUNE paints and publishes every channel's stored curve on one graph") {
  TuneFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.openTune());
  const auto plot = f.bounds("shell.tune.graph");
  CHECK(plot.width > 200.0 && plot.height > 100.0);
  std::vector<std::uint32_t> previous(f.surface.pixels().begin(), f.surface.pixels().end());
  for (std::size_t i = 0U; i < kIds.size(); ++i) {
    // Each channel at its own height, so each curve lands on pixels no other curve covers.
    CHECK(storePoint(f.controller, i, time::Tick{1920 + static_cast<std::int64_t>(i) * 960},
                     0.08F + 0.15F * static_cast<float>(i)));
    CHECK(f.controller.closeExpressionLane().hasValue());
    CHECK(f.frame());
    const std::vector<std::uint32_t> now(f.surface.pixels().begin(), f.surface.pixels().end());
    CHECK(differingPixels(previous, now, f.surface.width(), plot) > 40U);
    previous = now;
    const auto curve = f.node("shell.tune.curve." + std::string{kIds[i]});
    CHECK(curve.has_value());
    if (curve) CHECK(curve->value == "1 point");
  }
  for (std::size_t i = 0U; i < kIds.size(); ++i) {
    const auto channel = ui::expressionChannelAt(i);
    CHECK(ui::readExpressionPoints(f.region(), channel).size() == 1U);
    const auto curve = f.node("shell.tune.curve." + std::string{kIds[i]});
    CHECK(curve.has_value() && curve->bounds.x == plot.x && curve->bounds.width == plot.width);
  }
}

TEST_CASE("TUNE selects a channel from its chip, its knob or an accessibility action") {
  TuneFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.openTune());
  CHECK(!f.controller.expressionLaneOpen());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.tune.channel.breath", SemanticAction::Activate)
            .hasValue());
  CHECK(f.controller.expressionLaneOpen());
  CHECK(f.controller.selectedExpressionChannel() == ui::ExpressionChannel::Breathiness);
  const auto chip = f.node("shell.tune.channel.breath");
  CHECK(chip && chip->selected && chip->role == SemanticRole::Tab);
  // A chip press selects too, and the press focuses the chip.
  CHECK(f.shell.pointerDown(f.controller, press(at(f.bounds("shell.tune.channel.gender"), 0.5, 0.5)))
            .hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(at(f.bounds("shell.tune.channel.gender"), 0.5, 0.5)))
            .hasValue());
  CHECK(f.controller.selectedExpressionChannel() == ui::ExpressionChannel::Gender);
  CHECK(f.focusedId() == "shell.tune.channel.gender");
  // A knob press without a drag selects its channel, exactly as a SING knob does, and edits nothing.
  const auto revision = f.controller.documentRevision();
  const auto knob = at(f.bounds("shell.tune.knob.tension"), 0.5, 0.5);
  CHECK(f.shell.pointerDown(f.controller, press(knob)).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(knob)).hasValue());
  CHECK(f.controller.selectedExpressionChannel() == ui::ExpressionChannel::Tension);
  CHECK(f.focusedId() == "shell.tune.knob.tension");
  CHECK(f.controller.documentRevision() == revision);
  const auto graph = f.node("shell.tune.graph");
  CHECK(graph && graph->name == "Tension curve");
}

TEST_CASE("one TUNE graph gesture is one undoable edit of the selected channel") {
  TuneFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.openTune());
  CHECK(!f.session.canUndo());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.tune.channel.gender", SemanticAction::Activate)
            .hasValue());
  CHECK(f.frame());
  const auto plot = f.bounds("shell.tune.graph");
  const auto d = ui::describeExpressionChannel(ui::ExpressionChannel::Gender);
  CHECK(f.shell.pointerDown(f.controller, press(at(plot, 0.25, 0.5))).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press(at(plot, 0.375, 0.3))).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press(at(plot, 0.5, 0.2))).hasValue());
  // Nothing is committed while the drag is in progress.
  CHECK(!f.session.canUndo());
  CHECK(f.region().genderAutomation.points().empty());
  CHECK(f.shell.pointerUp(f.controller, press(at(plot, 0.5, 0.2))).hasValue());
  const auto points = ui::readExpressionPoints(f.region(), ui::ExpressionChannel::Gender);
  CHECK(points.size() == 1U);
  if (!points.empty()) {
    // Half-way across the 7680-tick region, 80% of the way up the channel's range.
    CHECK(std::abs(points.front().tick.value() - 3840) <= 240);
    const auto expected = d.minimum + 0.8F * (d.maximum - d.minimum);
    CHECK(std::abs(points.front().amount - expected) < 0.06F * (d.maximum - d.minimum));
  }
  CHECK(f.session.canUndo());
  CHECK(f.session.undo().hasValue());
  CHECK(f.region().genderAutomation.points().empty());
  CHECK(!f.session.canUndo());
  // Shift-press on a stored point removes it, again as one undo step.
  CHECK(f.session.redo().hasValue());
  CHECK(f.frame());
  const auto stored = ui::readExpressionPoints(f.region(), ui::ExpressionChannel::Gender).front();
  const auto x = plot.x + static_cast<double>(stored.tick.value()) / 7680.0 * plot.width;
  const auto fraction = (stored.amount - d.minimum) / (d.maximum - d.minimum);
  const auto y = plot.bottom() - 5.0 - fraction * (plot.height - 10.0);
  CHECK(f.shell.pointerDown(f.controller, press({x + 2.0, y - 2.0}, true)).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press({x + 2.0, y - 2.0}, true)).hasValue());
  CHECK(f.region().genderAutomation.points().empty());
  CHECK(f.session.undo().hasValue());
  CHECK(f.region().genderAutomation.points().size() == 1U);
}

TEST_CASE("Escape in the middle of a TUNE gesture commits nothing and stays in TUNE") {
  TuneFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.openTune());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.tune.channel.gender", SemanticAction::Activate)
            .hasValue());
  CHECK(f.frame());
  const auto revision = f.controller.documentRevision();
  const auto plot = f.bounds("shell.tune.graph");
  CHECK(f.shell.pointerDown(f.controller, press(at(plot, 0.4, 0.3))).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press(at(plot, 0.6, 0.2))).hasValue());
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(f.shell.workspace() == Workspace::Tune);
  CHECK(f.shell.pointerUp(f.controller, press(at(plot, 0.6, 0.2))).hasValue());
  CHECK(f.controller.documentRevision() == revision);
  CHECK(!f.session.canUndo());
  CHECK(f.region().genderAutomation.points().empty());
  CHECK(f.controller.sceneState().expression.points.empty());
  // A knob drag and a vibrato slider drag are abandoned the same way.
  const auto knob = at(f.bounds("shell.tune.knob.gender"), 0.5, 0.5);
  CHECK(f.shell.pointerDown(f.controller, press(knob)).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press({knob.x, knob.y - 48.0})).hasValue());
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(f.shell.pointerUp(f.controller, press({knob.x, knob.y - 48.0})).hasValue());
  f.session.selection().selectOnly(f.note().id);
  CHECK(f.frame());
  const auto depth = f.bounds("shell.tune.vibrato.depth");
  CHECK(f.shell.pointerDown(f.controller, press(at(depth, 0.1, 0.5))).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press(at(depth, 0.9, 0.5))).hasValue());
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(f.shell.pointerUp(f.controller, press(at(depth, 0.9, 0.5))).hasValue());
  CHECK(f.controller.documentRevision() == revision);
  CHECK(!f.session.canUndo());
  CHECK(f.shell.workspace() == Workspace::Tune);
}

TEST_CASE("an Escaped TUNE graph drag leaves no draft to go stale behind a knob edit") {
  TuneFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.openTune());
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.tune.channel.gender", SemanticAction::Activate)
            .hasValue());
  CHECK(f.frame());
  const auto plot = f.bounds("shell.tune.graph");
  // Press and drag on the graph, Escape, then release.
  CHECK(f.shell.pointerDown(f.controller, press(at(plot, 0.4, 0.3))).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press(at(plot, 0.6, 0.2))).hasValue());
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(f.shell.pointerUp(f.controller, press(at(plot, 0.6, 0.2))).hasValue());
  CHECK(!f.controller.sceneState().expression.draftOpen);
  // The knob writes the stored curve directly; the graph and accessibility must show that point.
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.tune.knob.gender", SemanticAction::Increment)
            .hasValue());
  CHECK(ui::readExpressionPoints(f.region(), ui::ExpressionChannel::Gender).size() == 1U);
  CHECK(f.controller.sceneState().expression.points.size() == 1U);
  CHECK(f.frame());
  const auto curve = f.node("shell.tune.curve.gender");
  CHECK(curve && curve->value == "1 point");
  const auto graph = f.node("shell.tune.graph");
  CHECK(graph && graph->value.find(" 1 points") != std::string::npos);
  // The next graph drag edits the current curve and its release commits.
  CHECK(f.shell.pointerDown(f.controller, press(at(plot, 0.25, 0.5))).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press(at(plot, 0.5, 0.2))).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(at(plot, 0.5, 0.2))).hasValue());
  CHECK(ui::readExpressionPoints(f.region(), ui::ExpressionChannel::Gender).size() == 2U);
  CHECK(f.controller.sceneState().expression.points.size() == 2U);
  CHECK(!f.controller.sceneState().expression.draftOpen);
  // Two undoable edits: the knob step and the drag.
  CHECK(f.session.undo().hasValue());
  CHECK(ui::readExpressionPoints(f.region(), ui::ExpressionChannel::Gender).size() == 1U);
  CHECK(f.session.undo().hasValue());
  CHECK(f.region().genderAutomation.points().empty());
  CHECK(!f.session.canUndo());
}

TEST_CASE("an untouched expression draft from an older revision is re-read, never reused") {
  TuneFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.openTune());
  CHECK(f.controller.openExpressionLane(ui::ExpressionChannel::Gender).hasValue());
  // A press opens a draft; Escape restores it to the stored curve.
  CHECK(f.controller.pressExpressionPoint(std::nullopt, time::Tick{1920}, 0.5F).hasValue());
  f.controller.cancelPointerGesture();
  CHECK(f.controller.releaseExpressionPoint().hasValue());
  // Another path edits the stored curve behind the lane.
  CHECK(f.controller.nudgeGender(2).hasValue());
  const auto stored = ui::readExpressionPoints(f.region(), ui::ExpressionChannel::Gender);
  CHECK(stored.size() == 1U);
  CHECK(f.controller.sceneState().expression.points == stored);
  // Grabbing the stored point works, because the draft is prepared from the current curve.
  if (!stored.empty()) {
    CHECK(f.controller.pressExpressionPoint(stored.front().tick, time::Tick{2880}, 0.3F).hasValue());
    CHECK(f.controller.dragExpressionPoint(time::Tick{2880}, 0.3F).hasValue());
    CHECK(f.controller.releaseExpressionPoint().hasValue());
    const auto moved = ui::readExpressionPoints(f.region(), ui::ExpressionChannel::Gender);
    CHECK(moved.size() == 1U);
    if (!moved.empty()) CHECK(moved.front().tick == time::Tick{2880});
  }
}

TEST_CASE("a TUNE macro knob edits exactly as the SING knob does") {
  TuneFixture sing;
  TuneFixture tune;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(sing.frame());
  CHECK(tune.openTune());
  CHECK(sing.shell.dispatchSemantic(sing.controller, "shell.knob.gender", SemanticAction::Increment)
            .hasValue());
  CHECK(tune.shell.dispatchSemantic(tune.controller, "shell.tune.knob.gender",
                                    SemanticAction::Increment)
            .hasValue());
  CHECK(sing.region().genderAutomation.points().size() == 1U);
  CHECK(sing.region().genderAutomation.points() == tune.region().genderAutomation.points());
  const auto knob = tune.node("shell.tune.knob.gender");
  const auto singKnob = sing.node("shell.knob.gender");
  CHECK(knob && singKnob && knob->role == SemanticRole::Slider);
  if (knob && singKnob) {
    CHECK(knob->numericValue == singKnob->numericValue);
    CHECK(knob->numericStep == singKnob->numericStep);
  }
  // A drag is one command of the dragged step count, like the SING rack knob.
  CHECK(sing.controller.nudgeGender(2).hasValue());
  const auto cell = tune.bounds("shell.tune.knob.gender");
  const auto c = at(cell, 0.5, 0.5);
  CHECK(tune.shell.pointerDown(tune.controller, press(c)).hasValue());
  CHECK(tune.shell.pointerMove(tune.controller, press({c.x, c.y - 24.0})).hasValue());
  CHECK(tune.shell.pointerUp(tune.controller, press({c.x, c.y - 24.0})).hasValue());
  CHECK(sing.region().genderAutomation.points() == tune.region().genderAutomation.points());
  CHECK(tune.session.undo().hasValue());
  CHECK(tune.session.undo().hasValue());
  CHECK(!tune.session.canUndo());
  // Graph Increment nudges the selected channel at the playhead through the same command.
  CHECK(tune.shell.dispatchSemantic(tune.controller, "shell.tune.channel.gender",
                                    SemanticAction::Activate).hasValue());
  CHECK(tune.shell.dispatchSemantic(tune.controller, "shell.tune.graph", SemanticAction::Increment)
            .hasValue());
  CHECK(tune.region().genderAutomation.points().size() == 1U);
}

TEST_CASE("the TUNE vibrato card shows and edits the selected note's stored vibrato") {
  TuneFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.note().vibrato = domain::NoteVibrato{.enabled = true, .startFraction = 0.4F,
                                         .fadeInFraction = 0.2F, .fadeOutFraction = 0.15F,
                                         .depthCents = 60.0F, .periodMilliseconds = 150.0F,
                                         .phaseTurns = 0.25F};
  CHECK(f.openTune());
  // With no note selected the card says so and offers no controls.
  CHECK(!f.node("shell.tune.vibrato.depth").has_value());
  const auto empty = f.node("shell.tune.vibrato");
  CHECK(empty && empty->value.find("Select one note") != std::string::npos);
  f.session.selection().selectOnly(f.note().id);
  CHECK(f.frame());
  const auto expect = [&](std::string_view id, double value, std::string_view text) {
    const auto node = f.node(id);
    CHECK(node.has_value());
    if (!node) return;
    CHECK(node->role == SemanticRole::Slider);
    CHECK(node->numericValue.has_value() && std::abs(*node->numericValue - value) < 1e-3);
    CHECK(node->value == text);
  };
  expect("shell.tune.vibrato.start", 40.0, "40%");
  expect("shell.tune.vibrato.fade-in", 20.0, "20%");
  expect("shell.tune.vibrato.fade-out", 15.0, "15%");
  expect("shell.tune.vibrato.depth", 60.0, "60 ct");
  expect("shell.tune.vibrato.period", 150.0, "150 ms");
  expect("shell.tune.vibrato.phase", 90.0, "90\u00B0");
  const auto toggle = f.node("shell.tune.vibrato.enabled");
  CHECK(toggle && toggle->value == "On" && toggle->role == SemanticRole::CheckBox &&
        toggle->selected);
  if (toggle) {
    const auto offers = [&toggle](SemanticAction action) {
      return std::find(toggle->actions.begin(), toggle->actions.end(), action) !=
             toggle->actions.end();
    };
    CHECK(offers(SemanticAction::Toggle));
    CHECK(offers(SemanticAction::Activate));
  }
  // Increment is one undoable step of the stored field.
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.tune.vibrato.depth", SemanticAction::Increment)
            .hasValue());
  CHECK(std::abs(f.note().vibrato.depthCents - 65.0F) < 1e-4F);
  CHECK(f.session.undo().hasValue());
  CHECK(std::abs(f.note().vibrato.depthCents - 60.0F) < 1e-4F);
  CHECK(!f.session.canUndo());
  // A slider drag previews while it moves and commits once on release.
  const auto period = f.bounds("shell.tune.vibrato.period");
  const auto revision = f.controller.documentRevision();
  CHECK(f.shell.pointerDown(f.controller, press(at(period, 0.2, 0.5))).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press(at(period, 0.6, 0.5))).hasValue());
  CHECK(f.controller.documentRevision() == revision);
  const auto preview = f.node("shell.tune.vibrato.period");
  CHECK(preview && preview->numericValue && *preview->numericValue > 250.0);
  CHECK(f.shell.pointerUp(f.controller, press(at(period, 0.6, 0.5))).hasValue());
  CHECK(f.note().vibrato.periodMilliseconds > 250.0F);
  CHECK(f.session.undo().hasValue());
  CHECK(std::abs(f.note().vibrato.periodMilliseconds - 150.0F) < 1e-4F);
  CHECK(!f.session.canUndo());
  // The switch toggles the stored flag; the preview and card still read the stored shape.
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.tune.vibrato.enabled", SemanticAction::Toggle)
            .hasValue());
  CHECK(!f.note().vibrato.enabled);
  CHECK(f.frame());
  const auto off = f.node("shell.tune.vibrato.enabled");
  CHECK(off && off->value == "Off" && off->role == SemanticRole::CheckBox && !off->selected);
  // Activate flips it back, exactly as Toggle does.
  CHECK(f.shell.dispatchSemantic(f.controller, "shell.tune.vibrato.enabled", SemanticAction::Activate)
            .hasValue());
  CHECK(f.note().vibrato.enabled);
  CHECK(f.frame());
  const auto on = f.node("shell.tune.vibrato.enabled");
  CHECK(on && on->value == "On" && on->selected);
}

TEST_CASE("TUNE pitch add, move and remove are one host command and one undo step each") {
  TuneFixture f{true};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.openTune());
  const auto group = f.node("shell.tune.pitch");
  CHECK(group && group->enabled && group->description.find("Read-only") == std::string::npos);
  const auto plot = f.bounds("shell.tune.pitch");
  // Press on empty space starts a new point; the drag shapes it and nothing reaches the host.
  CHECK(f.shell.pointerDown(f.controller, press(at(plot, 0.25, 0.4))).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press(at(plot, 0.5, 0.2))).hasValue());
  CHECK(f.controller.pitchPointGesture().has_value());
  CHECK(f.frame());
  CHECK(f.pitchCalls.empty());
  CHECK(f.pitch().empty());
  CHECK(!f.session.canUndo());
  CHECK(f.shell.pointerUp(f.controller, press(at(plot, 0.5, 0.2))).hasValue());
  CHECK(f.pitchCalls == std::vector<std::string>{"upsert"});
  CHECK(f.pitch().size() == 1U);
  if (f.pitch().size() != 1U) return;
  const auto added = f.pitch().front();
  // Half-way across the 7680-tick region, 60% of the way up the two-semitone half range, snapped.
  CHECK(std::abs(added.tick.value() - 3840) <= 240);
  CHECK(added.cents > 90.0F && added.cents < 150.0F);
  CHECK(std::fmod(added.cents, 5.0F) == 0.0F);
  CHECK(f.session.undo().hasValue());
  CHECK(f.pitch().empty());
  CHECK(!f.session.canUndo());
  CHECK(f.session.redo().hasValue());
  CHECK(f.frame());

  // A press grabs the point within 8 points of it and focuses it; the release is one move.
  const auto id = "shell.tune.pitch.point." + std::to_string(added.tick.value());
  const auto hit = f.bounds(id);
  CHECK(std::abs(hit.width - 16.0) < 0.01 && std::abs(hit.height - 16.0) < 0.01);
  const ui::Point grabAt{hit.x + 12.0, hit.y + 5.0};
  CHECK(f.shell.pointerDown(f.controller, press(grabAt)).hasValue());
  CHECK(f.focusedId() == id);
  CHECK(f.shell.pointerMove(f.controller, press({grabAt.x + plot.width * 0.25, grabAt.y + 20.0}))
            .hasValue());
  CHECK(f.pitchCalls.size() == 1U);
  CHECK(f.shell.pointerUp(f.controller, press({grabAt.x + plot.width * 0.25, grabAt.y + 20.0}))
            .hasValue());
  CHECK((f.pitchCalls == std::vector<std::string>{"upsert", "move"}));
  CHECK(f.pitch().size() == 1U);
  if (f.pitch().size() != 1U) return;
  const auto moved = f.pitch().front();
  CHECK(std::abs(moved.tick.value() - 5760) <= 240);
  CHECK(moved.cents < added.cents - 20.0F);
  CHECK(f.session.undo().hasValue());
  CHECK(f.pitch().size() == 1U && f.pitch().front() == added);
  CHECK(f.session.redo().hasValue());
  CHECK(f.frame());

  // A click on a point that does not move it sends nothing.
  const auto movedId = "shell.tune.pitch.point." + std::to_string(moved.tick.value());
  const auto still = at(f.bounds(movedId), 0.5, 0.5);
  CHECK(f.shell.pointerDown(f.controller, press(still)).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(still)).hasValue());
  CHECK(f.pitchCalls.size() == 2U);

  // Shift-press removes it, as one command and one undo step.
  const auto revision = f.controller.documentRevision();
  CHECK(f.shell.pointerDown(f.controller, press(still, true)).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(still, true)).hasValue());
  CHECK((f.pitchCalls == std::vector<std::string>{"upsert", "move", "remove"}));
  CHECK(f.pitch().empty());
  CHECK(f.controller.documentRevision() == revision + 1U);
  CHECK(f.session.undo().hasValue());
  CHECK(f.pitch().size() == 1U && f.pitch().front() == moved);
}

TEST_CASE("Escape in the middle of a TUNE pitch drag commits nothing") {
  TuneFixture f{true};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.storePitch(time::Tick{1920}, 50.0F);
  CHECK(f.openTune());
  const auto revision = f.controller.documentRevision();
  const auto plot = f.bounds("shell.tune.pitch");
  // A new point abandoned mid-drag.
  CHECK(f.shell.pointerDown(f.controller, press(at(plot, 0.6, 0.3))).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press(at(plot, 0.7, 0.2))).hasValue());
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(f.shell.workspace() == Workspace::Tune);
  CHECK(!f.controller.pitchPointGesture().has_value());
  CHECK(f.shell.pointerUp(f.controller, press(at(plot, 0.7, 0.2))).hasValue());
  // A stored point grabbed and abandoned mid-drag.
  const auto point = at(f.bounds("shell.tune.pitch.point.1920"), 0.5, 0.5);
  CHECK(f.shell.pointerDown(f.controller, press(point)).hasValue());
  CHECK(f.shell.pointerMove(f.controller, press({point.x + 80.0, point.y + 20.0})).hasValue());
  CHECK(f.shell.handleShellKey(f.controller, KeyEvent{.key = NativeKey::Escape}));
  CHECK(f.shell.pointerUp(f.controller, press({point.x + 80.0, point.y + 20.0})).hasValue());
  CHECK(f.pitchCalls.empty());
  CHECK(f.controller.documentRevision() == revision);
  CHECK(!f.session.canUndo());
  CHECK(f.pitch().size() == 1U && f.pitch().front().tick == time::Tick{1920} &&
        f.pitch().front().cents == 50.0F);
  // The controller commands behave the same: cancel drops the gesture and release sends nothing.
  CHECK(f.controller.pressPitchPoint(std::nullopt, time::Tick{3840}, 100.0F).hasValue());
  CHECK(f.controller.dragPitchPoint(time::Tick{4800}, 150.0F).hasValue());
  f.controller.cancelPointerGesture();
  CHECK(f.controller.releasePitchPoint().hasValue());
  CHECK(f.pitchCalls.empty());
  CHECK(f.pitch().size() == 1U);
}

TEST_CASE("TUNE pitch points step and change their curve as one command each") {
  TuneFixture f{true};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.storePitch(time::Tick{1920}, 50.0F);
  CHECK(f.openTune());
  const auto group = f.node("shell.tune.pitch");
  CHECK(group.has_value());
  if (!group) return;
  CHECK(group->value == "1 point");
  CHECK(group->children.size() == 1U);
  const auto point = f.node("shell.tune.pitch.point.1920");
  CHECK(point.has_value());
  if (!point) return;
  CHECK(point->role == SemanticRole::Slider && point->enabled);
  CHECK(point->numericValue && *point->numericValue == 50.0);
  CHECK(point->numericStep && *point->numericStep == 5.0);
  CHECK(point->value.find("+50 ct") != std::string::npos);
  CHECK(point->value.find("bar 1 beat 3") != std::string::npos);
  CHECK(inside(point->bounds, f.shell.workspaceArea()));
  CHECK(f.shell.dispatchSemantic(f.controller, point->id, SemanticAction::Increment).hasValue());
  CHECK(f.pitch().front().cents == 55.0F);
  CHECK(f.shell.dispatchSemantic(f.controller, point->id, SemanticAction::Decrement).hasValue());
  CHECK(f.shell.dispatchSemantic(f.controller, point->id, SemanticAction::Decrement).hasValue());
  CHECK(f.pitch().front().cents == 45.0F);
  CHECK((f.pitchCalls == std::vector<std::string>{"upsert", "upsert", "upsert"}));
  CHECK(f.session.undo().hasValue());
  CHECK(f.pitch().front().cents == 50.0F);
  // Activate cycles the interpolation; so does an Alt-press on the point, which also focuses it.
  CHECK(f.shell.dispatchSemantic(f.controller, point->id, SemanticAction::Activate).hasValue());
  CHECK(f.pitch().front().interpolation == domain::CurveInterpolation::Smooth);
  CHECK(f.frame());
  const auto c = at(f.bounds(point->id), 0.5, 0.5);
  PointerEvent alt = press(c);
  alt.modifiers.alt = true;
  CHECK(f.shell.pointerDown(f.controller, alt).hasValue());
  CHECK(f.shell.pointerUp(f.controller, alt).hasValue());
  CHECK(f.pitch().front().interpolation == domain::CurveInterpolation::Step);
  CHECK(f.focusedId() == point->id);
  CHECK(f.pitchCalls.size() == 5U && f.pitchCalls[3] == "cycle" && f.pitchCalls[4] == "cycle");
  CHECK(!f.controller.pointerGestureActive());
  CHECK(f.pitch().size() == 1U);
}

TEST_CASE("every corner of a pitch point's published bounds grabs that point") {
  TuneFixture f{true};
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.storePitch(time::Tick{1920}, 60.0F);
  CHECK(f.openTune());
  const auto point = f.node("shell.tune.pitch.point.1920");
  CHECK(point.has_value());
  if (!point) return;
  CHECK(f.frame());
  const auto r = f.bounds(point->id);
  // The four corners of the node are inside the press target: a press there grabs the stored point
  // rather than adding a new one at the drawn marker.
  for (const auto corner : {ui::Point{r.x + 1.0, r.y + 1.0}, ui::Point{r.right() - 1.0, r.y + 1.0},
                            ui::Point{r.x + 1.0, r.bottom() - 1.0},
                            ui::Point{r.right() - 1.0, r.bottom() - 1.0}}) {
    f.pitchCalls.clear();
    const auto revision = f.controller.documentRevision();
    CHECK(f.shell.pointerDown(f.controller, press(corner)).hasValue());
    CHECK(f.shell.pointerUp(f.controller, press(corner)).hasValue());
    CHECK(f.pitchCalls.empty());                 // grabbed and released in place: no host call
    CHECK(f.controller.documentRevision() == revision);
    CHECK(f.pitch().size() == 1U);
    CHECK(f.pitch().front().cents == 60.0F);
    CHECK(f.focusedId() == point->id);
  }
}

TEST_CASE("a host without pitch callbacks refuses pitch edits and TUNE shows them read-only") {
  TuneFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  f.storePitch(time::Tick{1920}, 50.0F);
  CHECK(f.openTune());
  CHECK(f.controller.pitchEditRefusal() == "This host cannot edit pitch points");
  const auto pitch = f.node("shell.tune.pitch");
  CHECK(pitch.has_value());
  if (!pitch) return;
  CHECK(pitch->value == "1 point");
  CHECK(!pitch->enabled);
  CHECK(pitch->description == "Read-only: This host cannot edit pitch points");
  const auto point = f.node("shell.tune.pitch.point.1920");
  CHECK(point && !point->enabled &&
        point->actions == std::vector<SemanticAction>{SemanticAction::SetFocus});
  const auto refused = f.shell.dispatchSemantic(f.controller, "shell.tune.pitch.point.1920",
                                                SemanticAction::Increment);
  CHECK(!refused.hasValue());
  CHECK(f.pitch().front().cents == 50.0F);
  // The controller commands name what the host cannot do.
  const auto add = f.controller.pressPitchPoint(std::nullopt, time::Tick{960}, 20.0F);
  CHECK(!add.hasValue() && add.error().message == "This host cannot add pitch points");
  const auto move = f.controller.pressPitchPoint(time::Tick{1920}, time::Tick{960}, 20.0F);
  CHECK(!move.hasValue() && move.error().message == "This host cannot move pitch points");
  const auto remove = f.controller.removePitchPointAt(time::Tick{1920});
  CHECK(!remove.hasValue() && remove.error().message == "This host cannot remove pitch points");
  const auto cycle = f.controller.cyclePitchInterpolationAt(time::Tick{1920});
  CHECK(!cycle.hasValue() &&
        cycle.error().message == "This host cannot change pitch interpolation");
  CHECK(!f.controller.pointerGestureActive());
  // A press on the strip only focuses it.
  const auto revision = f.controller.documentRevision();
  CHECK(f.shell.pointerDown(f.controller, press(at(pitch->bounds, 0.5, 0.5))).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(at(pitch->bounds, 0.5, 0.5))).hasValue());
  CHECK(f.controller.documentRevision() == revision);
  CHECK(f.focusedId() == "shell.tune.pitch");
  CHECK(f.pitch().size() == 1U && f.pitch().front().cents == 50.0F);
}

TEST_CASE("TUNE controls never overlap or leave the workspace at any contract size") {
  for (const auto& [w, h] : {std::pair{480.0, 320.0}, std::pair{720.0, 480.0},
                             std::pair{1100.0, 720.0}, std::pair{1600.0, 900.0}}) {
    TuneFixture f{true};
    if (!native_ui::paint::vectorBackendAvailable()) return;
    f.session.selection().selectOnly(f.note().id);
    // Two pitch points, so their press targets are checked with everything else.
    f.storePitch(time::Tick{0}, 150.0F);
    f.storePitch(time::Tick{5760}, -120.0F);
    CHECK(f.openTune());
    CHECK(f.frameAt(w, h));
    const auto area = f.shell.workspaceArea();
    // Side by side, one pass covers everything; compact, each of the three views is checked.
    const auto compact = f.node("shell.tune.view.curves").has_value();
    for (const std::string_view view : {"curves", "pitch", "vibrato"}) {
      if (compact) {
        CHECK(f.shell.dispatchSemantic(f.controller, "shell.tune.view." + std::string{view},
                                       SemanticAction::Activate).hasValue());
        CHECK(f.frame());
        const auto tab = f.node("shell.tune.view." + std::string{view});
        CHECK(tab && tab->selected);
      } else if (view != "curves") {
        break;
      }
      std::vector<SemanticNode> tune;
      std::size_t pitchPoints = 0U;
      for (const auto& node : f.nodes()) {
        if (!node.id.starts_with("shell.tune.")) continue;
        tune.push_back(node);
        if (node.id != "shell.tune.pitch") continue;
        for (const auto& point : node.children) {
          CHECK(inside(point.bounds, area));
          tune.push_back(point);
          ++pitchPoints;
        }
      }
      // The pitch strip and both its points are reachable at every size: under the graph side by
      // side, in its own view when compact.
      const auto pitchShown = compact ? view == "pitch" : true;
      if (pitchPoints != (pitchShown ? 2U : 0U))
        throw test::Failure{"pitch points " + std::to_string(pitchPoints) + " in the " +
                            std::string{view} + " view at " + std::to_string(w) + "x" +
                            std::to_string(h)};
      // The knobs are always there; the graph, the pitch strip or the vibrato controls fill the
      // middle.
      CHECK(std::count_if(tune.begin(), tune.end(), [](const SemanticNode& n) {
              return n.id.starts_with("shell.tune.knob.");
            }) == 6);
      const std::string_view middle = view == "vibrato" ? "shell.tune.vibrato.depth"
                                      : view == "pitch" ? "shell.tune.pitch"
                                                        : "shell.tune.graph";
      CHECK(std::any_of(tune.begin(), tune.end(), [&](const SemanticNode& n) {
        return n.id == middle;
      }));
      for (std::size_t i = 0U; i < tune.size(); ++i) {
        const auto& a = tune[i];
        CHECK(a.bounds.width >= 8.0 && a.bounds.height >= 8.0);
        CHECK(inside(a.bounds, area));
        for (std::size_t j = i + 1U; j < tune.size(); ++j) {
          const auto& b = tune[j];
          // The vibrato panel carries its own switch and sliders.
          const auto nested = (a.id == "shell.tune.vibrato" && b.id.starts_with("shell.tune.vibrato.")) ||
                              (b.id == "shell.tune.vibrato" && a.id.starts_with("shell.tune.vibrato.")) ||
                              // The pitch strip carries its points.
                              (a.id == "shell.tune.pitch" && b.id.starts_with("shell.tune.pitch.")) ||
                              (b.id == "shell.tune.pitch" && a.id.starts_with("shell.tune.pitch."));
          if (nested) continue;
          if (intersects(a.bounds, b.bounds))
            throw test::Failure{a.id + " overlaps " + b.id + " at " + std::to_string(w) + "x" +
                                std::to_string(h)};
        }
      }
    }
  }
}
