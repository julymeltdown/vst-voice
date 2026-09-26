// The TUNE workspace, driven through a real SingShell, NativeEditorController and EditorSession:
// six channel curves on one graph, channel selection, one-gesture-one-undo edits, Escape, the macro
// knobs against SING's own knobs, the selected note's vibrato, and geometry at every contract size.
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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
  application::EditorSession session;
  native_ui::NativeEditorController controller;
  SingShell shell;
  native_ui::PixelSurface surface{1600U, 900U};
  double width{1600.0};
  double height{900.0};

  TuneFixture()
      : session(makeProject()),
        controller{session, factory, regionId, native_ui::EditorHostCallbacks{}} {
    controller.resize(width, height);
    shell.activate({}, DesignPreferences{.mode = DesignMode::Emo});
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
  CHECK(toggle && toggle->value == "On" && toggle->role == SemanticRole::Button);
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
  CHECK(off && off->value == "Off");
}

TEST_CASE("TUNE pitch is shown read-only with an honest caption") {
  TuneFixture f;
  if (!native_ui::paint::vectorBackendAvailable()) return;
  CHECK(f.session.project().findRegion(f.regionId)->pitchAutomation.upsert(
      domain::PitchAutomationPoint{time::Tick{1920}, 50.0F}).hasValue());
  CHECK(f.openTune());
  const auto pitch = f.node("shell.tune.pitch");
  CHECK(pitch.has_value());
  if (!pitch) return;
  CHECK(pitch->value == "1 point");
  CHECK(pitch->description.find("Read-only") != std::string::npos);
  CHECK(std::find(pitch->actions.begin(), pitch->actions.end(), SemanticAction::Increment) ==
        pitch->actions.end());
  const auto revision = f.controller.documentRevision();
  CHECK(f.shell.pointerDown(f.controller, press(at(pitch->bounds, 0.5, 0.5))).hasValue());
  CHECK(f.shell.pointerUp(f.controller, press(at(pitch->bounds, 0.5, 0.5))).hasValue());
  CHECK(f.controller.documentRevision() == revision);
  CHECK(f.focusedId() == "shell.tune.pitch");
}

TEST_CASE("TUNE controls never overlap or leave the workspace at any contract size") {
  for (const auto& [w, h] : {std::pair{480.0, 320.0}, std::pair{720.0, 480.0},
                             std::pair{1100.0, 720.0}, std::pair{1600.0, 900.0}}) {
    TuneFixture f;
    if (!native_ui::paint::vectorBackendAvailable()) return;
    f.session.selection().selectOnly(f.note().id);
    CHECK(f.openTune());
    CHECK(f.frameAt(w, h));
    const auto area = f.shell.workspaceArea();
    for (const bool vibratoView : {false, true}) {
      if (vibratoView) {
        if (!f.node("shell.tune.view.vibrato")) break;  // side by side: nothing to switch
        CHECK(f.shell.dispatchSemantic(f.controller, "shell.tune.view.vibrato",
                                       SemanticAction::Activate).hasValue());
        CHECK(f.frame());
      }
      std::vector<SemanticNode> tune;
      for (const auto& node : f.nodes())
        if (node.id.starts_with("shell.tune.")) tune.push_back(node);
      // The knobs are always there; the graph or the vibrato controls fill the middle.
      CHECK(std::count_if(tune.begin(), tune.end(), [](const SemanticNode& n) {
              return n.id.starts_with("shell.tune.knob.");
            }) == 6);
      CHECK(std::any_of(tune.begin(), tune.end(), [&](const SemanticNode& n) {
        return n.id == (vibratoView ? "shell.tune.vibrato.depth" : "shell.tune.graph");
      }));
      for (std::size_t i = 0U; i < tune.size(); ++i) {
        const auto& a = tune[i];
        CHECK(a.bounds.width >= 8.0 && a.bounds.height >= 8.0);
        CHECK(inside(a.bounds, area));
        for (std::size_t j = i + 1U; j < tune.size(); ++j) {
          const auto& b = tune[j];
          // The vibrato panel carries its own switch and sliders.
          const auto nested = (a.id == "shell.tune.vibrato" && b.id.starts_with("shell.tune.vibrato.")) ||
                              (b.id == "shell.tune.vibrato" && a.id.starts_with("shell.tune.vibrato."));
          if (nested) continue;
          if (intersects(a.bounds, b.bounds))
            throw test::Failure{a.id + " overlaps " + b.id + " at " + std::to_string(w) + "x" +
                                std::to_string(h)};
        }
      }
    }
  }
}
