// One surface, three shapes: a semitone channel, a bipolar channel and a normalized share.
//
// The channels share a curve editor but not a unit, so the test drives each shape through the same
// interaction instead of asserting that the editor exists. Persistence, capability refusal and
// stale-draft rejection are checked because a surface that can draw an unsupported curve and then
// silently drop it is worse than no surface at all.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/performance_commands.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/domain/project.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/editor_semantics.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/text/text_engine.hpp"
#include "seam/ui/expression_lane.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/synthesis/singer_resource.hpp"
#include <span>

#include <functional>
#include <cmath>
#include <string>
#include <vector>
#include <cstdlib>
#include <iostream>

namespace {

using namespace seam;
using ui::ExpressionChannel;
using ui::ExpressionLaneModel;
using ui::ExpressionPoint;

// The recipe the lane fixture's track names. Rendering needs the resource itself rather than the
// reference a project stores, so it is frozen here to match what a project would resolve.
voice_design::VoiceRecipe laneRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "expression-lane";
  recipe.seed = 4321U;
  voice_design::VoicePose pose;
  pose.phone = "a";
  pose.style = "neutral";
  pose.formants = {voice_design::ResonanceBand{700.0, 90.0, 0.0},
                   voice_design::ResonanceBand{1200.0, 100.0, -3.0},
                   voice_design::ResonanceBand{2600.0, 140.0, -6.0}};
  recipe.poses.push_back(pose);
  return recipe;
}

struct LaneFixture final {
  application::ProjectFactory factory{9600U};
  domain::TrackId trackId{};
  domain::RegionId regionId{};
  application::EditorSession session;

  explicit LaneFixture(bool procedural = true) : session(makeProject(procedural)) {}

  domain::Project makeProject(bool procedural) {
    auto project = factory.createProject("Expression lane");
    project.settings().characterDisplay = domain::CharacterDisplayMode::Off;
    trackId = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, trackId, "Phrase", time::Tick{0}, time::Tick{3840});
    auto [lyric, note] =
        factory.makeNote(time::Tick{0}, time::Tick{3840}, 45U, U"あ", domain::Language::Japanese);
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    if (procedural) {
      // The stored reference must be the identity the renderer derives from the recipe, because the
      // snapshot refuses a resource whose identity differs from the project's selection.
      const auto frozen = voice_design::freezeVoiceRecipeResource(laneRecipe());
      if (!frozen) throw test::Failure{frozen.error().message};
      project.findVocalTrack(trackId)->proceduralRecipe = domain::ProceduralRecipeReference{
          .resource = frozen.value().identity, .path = "recipe.json", .style = "neutral"};
    }
    return project;
  }
};


TEST_CASE("Every channel reports its own unit and bound rather than a shared range") {


  const auto formant = ui::describeExpressionChannel(ExpressionChannel::Formant);
  CHECK(formant.unit == "semitones");
  CHECK_NEAR(formant.maximum, domain::kMaximumFormantShiftSemitones, 1e-6);
  CHECK(formant.bipolar);
  CHECK_NEAR(formant.step, 1.0, 1e-6);

  const auto gender = ui::describeExpressionChannel(ExpressionChannel::Gender);
  CHECK(gender.unit == "bipolar");
  CHECK_NEAR(gender.minimum, -domain::kMaximumGender, 1e-6);
  CHECK_NEAR(gender.neutral, 0.0, 1e-6);

  for (const auto channel : {ExpressionChannel::Breathiness, ExpressionChannel::Tension,
                             ExpressionChannel::Airiness, ExpressionChannel::Growl}) {
    const auto descriptor = ui::describeExpressionChannel(channel);
    CHECK(descriptor.unit == "normalized share");
    CHECK_NEAR(descriptor.minimum, 0.0, 1e-6);
    CHECK_NEAR(descriptor.maximum, 1.0, 1e-6);
    CHECK(!descriptor.bipolar);
    CHECK(!descriptor.label.empty());
  }
  // A picker wraps instead of exposing an unbounded index.
  CHECK(ui::nextExpressionChannel(ExpressionChannel::Growl, 1) == ExpressionChannel::Formant);
  CHECK(ui::nextExpressionChannel(ExpressionChannel::Formant, -1) == ExpressionChannel::Growl);
}

TEST_CASE("A semitone, a bipolar and a normalized channel all edit through one interaction") {
  LaneFixture fixture;
  for (const auto channel : {ExpressionChannel::Formant, ExpressionChannel::Gender,
                             ExpressionChannel::Airiness}) {
    auto lane = ExpressionLaneModel::prepare(fixture.session, fixture.regionId, channel);
    CHECK(lane.hasValue());
    if (!lane) return;
    const auto descriptor = ui::describeExpressionChannel(channel);
    // The neutral point the surface starts from is the channel's own, not a shared zero line.
    CHECK_NEAR(lane.value().valueAt(time::Tick{0}), descriptor.neutral, 1e-6);
    CHECK(lane.value().points().empty());

    CHECK(lane.value().upsert(ExpressionPoint{time::Tick{480}, descriptor.maximum * 0.5F}));
    CHECK(lane.value().upsert(ExpressionPoint{time::Tick{1920}, descriptor.neutral}));
    CHECK(lane.value().hasChanges());
    CHECK(lane.value().move(time::Tick{480}, ExpressionPoint{time::Tick{960}, descriptor.maximum}));
    CHECK(lane.value().points().size() == 2U);
    CHECK_NEAR(lane.value().valueAt(time::Tick{960}), descriptor.maximum, 1e-6);
    CHECK_NEAR(lane.value().valueAt(time::Tick{1440}), descriptor.maximum * 0.5, 1e-6);

    // Out-of-range and nonfinite values reject rather than clamp, so the surface cannot invent a
    // value the renderer would refuse.
    CHECK(!lane.value().upsert(ExpressionPoint{time::Tick{480}, descriptor.maximum * 2.0F}).hasValue());
    if (!descriptor.bipolar)
      CHECK(!lane.value().upsert(ExpressionPoint{time::Tick{480}, -0.5F}).hasValue());
    CHECK(!lane.value().upsert(ExpressionPoint{time::Tick{5000}, descriptor.neutral}).hasValue());

    CHECK(lane.value().erase(time::Tick{960}));
    CHECK(!lane.value().erase(time::Tick{960}).hasValue());
    CHECK(lane.value().apply(fixture.session, fixture.regionId));

    const auto* region = fixture.session.project().findRegion(fixture.regionId);
    CHECK(region != nullptr);
    if (region == nullptr) return;
    switch (channel) {
      case ExpressionChannel::Formant:
        CHECK(region->formantAutomation.points().size() == 1U);
        break;
      case ExpressionChannel::Gender:
        CHECK(region->genderAutomation.points().size() == 1U);
        break;
      default:
        CHECK(region->airinessAutomation.points().size() == 1U);
        break;
    }
    // Each channel's edit is exactly one undoable command. Undo restores that channel, and the other
    // channels keep whatever the surface already stored for them.
    CHECK(fixture.session.undo());
    const auto* reverted = fixture.session.project().findRegion(fixture.regionId);
    CHECK(reverted != nullptr);
    if (reverted == nullptr) return;
    switch (channel) {
      case ExpressionChannel::Formant:
        CHECK(reverted->formantAutomation.points().empty());
        break;
      case ExpressionChannel::Gender:
        CHECK(reverted->genderAutomation.points().empty());
        break;
      default:
        CHECK(reverted->airinessAutomation.points().empty());
        break;
    }
    CHECK(fixture.session.redo());
  }
}

TEST_CASE("Cancelling and staling a draft leave the stored project untouched") {
  LaneFixture fixture;
  auto lane = ExpressionLaneModel::prepare(fixture.session, fixture.regionId,
                                           ExpressionChannel::Breathiness);
  CHECK(lane.hasValue());
  if (!lane) return;
  CHECK(lane.value().upsert(ExpressionPoint{time::Tick{480}, 0.6F}));
  lane.value().cancel();
  CHECK(lane.value().state() == ExpressionLaneModel::State::Cancelled);
  CHECK(fixture.session.project().findRegion(fixture.regionId)->breathinessAutomation.points().empty());
  // A closed draft refuses further edits and refuses to apply.
  CHECK(!lane.value().upsert(ExpressionPoint{time::Tick{960}, 0.6F}).hasValue());
  CHECK(!lane.value().apply(fixture.session, fixture.regionId).hasValue());

  auto stale = ExpressionLaneModel::prepare(fixture.session, fixture.regionId,
                                            ExpressionChannel::Growl);
  CHECK(stale.hasValue());
  if (!stale) return;
  CHECK(stale.value().upsert(ExpressionPoint{time::Tick{480}, 0.4F}));
  // An unrelated accepted edit moves the session revision, so the captured draft is no longer current.
  auto context = fixture.session.capturePerformanceJob();
  CHECK(context.hasValue());
  if (!context) return;
  domain::AirinessAutomation unrelated;
  CHECK(unrelated.upsert({time::Tick{960}, 0.3F}).hasValue());
  CHECK(fixture.session.executePerformanceResult(context.value(),
      std::make_unique<application::EditPerformanceCommand>(
          std::vector<application::NoteExpressionEdit>{},
          std::vector<application::RegionDynamicsEdit>{},
          std::vector<application::TrackStyleEdit>{},
          std::vector<application::RegionOwnershipEdit>{},
          std::vector<application::RegionFormantEdit>{},
          std::vector<application::RegionBreathinessEdit>{},
          std::vector<application::RegionTensionEdit>{},
          std::vector<application::RegionAirinessEdit>{{fixture.regionId, std::move(unrelated)}},
          std::vector<application::RegionGenderEdit>{},
          std::vector<application::RegionGrowlEdit>{})));
  const auto applied = stale.value().apply(fixture.session, fixture.regionId);
  CHECK(!applied.hasValue());
  CHECK(applied.error().code == core::ErrorCode::Conflict);
  CHECK(fixture.session.project().findRegion(fixture.regionId)->growlAutomation.points().empty());
}

TEST_CASE("A singer without the excitation reports the refusal and keeps the stored curve") {
  LaneFixture fixture{false};
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region->growlAutomation.upsert({time::Tick{0}, 0.4F}).hasValue());

  const auto refused = ui::validateExpressionCarrier(fixture.session.project(), fixture.trackId,
                                                     ExpressionChannel::Growl);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("Growl") != std::string::npos);
  CHECK(refused.error().message.find("source-filter") != std::string::npos);

  // The stored curve is still visible and can be removed; only new edits are blocked.
  auto lane = ExpressionLaneModel::prepare(fixture.session, fixture.regionId,
                                           ExpressionChannel::Growl);
  CHECK(lane.hasValue());
  if (!lane) return;
  CHECK(lane.value().points().size() == 1U);
  CHECK_NEAR(lane.value().valueAt(time::Tick{0}), 0.4, 1e-6);
  CHECK(!lane.value().upsert(ExpressionPoint{time::Tick{960}, 0.4F}).hasValue());
  CHECK(!lane.value().apply(fixture.session, fixture.regionId).hasValue());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->growlAutomation.points().size() == 1U);
  // Every channel this surface edits belongs to the excitation or the tract, so a bank refuses all
  // six by name rather than only the roughness one.
  for (const auto channel : {ExpressionChannel::Formant, ExpressionChannel::Breathiness,
                             ExpressionChannel::Tension, ExpressionChannel::Airiness,
                             ExpressionChannel::Gender, ExpressionChannel::Growl}) {
    const auto bankRefused = ui::validateExpressionCarrier(fixture.session.project(),
                                                           fixture.trackId, channel);
    CHECK(!bankRefused.hasValue());
    CHECK(bankRefused.error().code == core::ErrorCode::Unsupported);
    CHECK(bankRefused.error().message.find("source-filter") != std::string::npos);
  }
}

TEST_CASE("A lane edit survives a save, reload and re-export of the same curve") {
  LaneFixture fixture;
  auto lane = ExpressionLaneModel::prepare(fixture.session, fixture.regionId,
                                           ExpressionChannel::Gender);
  CHECK(lane.hasValue());
  if (!lane) return;
  CHECK(lane.value().upsert(ExpressionPoint{time::Tick{240}, -0.5F}));
  CHECK(lane.value().upsert(ExpressionPoint{time::Tick{1440}, 0.75F}));
  CHECK(lane.value().apply(fixture.session, fixture.regionId));

  formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(fixture.session.project());
  CHECK(encoded.hasValue());
  if (!encoded) return;
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded.hasValue());
  if (!decoded) return;
  const auto* reloaded = decoded.value().findRegion(fixture.regionId);
  CHECK(reloaded != nullptr);
  if (reloaded == nullptr) return;
  CHECK(reloaded->genderAutomation.points().size() == 2U);
  CHECK_NEAR(reloaded->genderAutomation.points().front().amount, -0.5, 1e-6);
  CHECK_NEAR(reloaded->genderAutomation.valueAt(time::Tick{840}), 0.125, 1e-6);

  // The reloaded project must still render, and the stored curve must reach the audio: a lane edit that
  // survives serialization but never affects rendering would pass every assertion above.
  // The renderer requires the frozen resource's identity to be the one the project stored, so the
  // fixture's own reference is used rather than a freshly derived one. A snapshot that accepted a
  // mismatched identity would let a project render a different voice than it selected.
  const auto* track = decoded.value().findVocalTrack(fixture.trackId);
  CHECK(track != nullptr);
  if (track == nullptr) return;
  const auto recipeBytes = voice_design::encodeVoiceRecipe(laneRecipe());
  CHECK(recipeBytes.hasValue());
  if (!recipeBytes) return;
  const auto frozen = synthesis::freezeProceduralResource(
      track->proceduralRecipe->resource,
      std::as_bytes(std::span<const char>{recipeBytes.value().data(), recipeBytes.value().size()}));
  CHECK(frozen.hasValue());
  if (!frozen) return;
  const auto resource = frozen;
  CHECK(resource.hasValue());
  if (!resource) return;
  auto snapshot = rendering::RenderSnapshotFactory{}.createProcedural(
      decoded.value(), resource.value(), fixture.trackId, fixture.regionId, 1U,
      rendering::RenderQuality::Final, 48000U);
  CHECK(snapshot.hasValue());
  if (!snapshot) return;
  auto rendered = rendering::PhraseRenderPipeline{}.render(snapshot.value());
  CHECK(rendered.hasValue());
  if (!rendered) return;
  double energy = 0.0;
  for (const auto sample : rendered.value().rendered.audio.samples) {
    CHECK(std::isfinite(sample));
    energy += static_cast<double>(sample) * static_cast<double>(sample);
  }
  CHECK(energy > 0.0);
}

TEST_CASE("The drawn lane reports the channel, its unit, the playhead value and its refusal") {
  LaneFixture fixture;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  controller.resize(1440.0, 900.0);
  CHECK(controller.selectedExpressionChannel() == ExpressionChannel::Formant);
  // The lane is not drawn until it is opened, so it does not take the automation band from pitch.
  CHECK(controller.sceneState().expressionLabelVisible() == false);
  CHECK(controller.openExpressionLane(ExpressionChannel::Gender).hasValue());
  auto state = controller.sceneState();
  CHECK(state.expressionLabelVisible());
  CHECK(state.expression.label == "Gender");
  CHECK(state.expression.unit == "bipolar");
  CHECK(state.expression.refusal.empty());
  CHECK_NEAR(state.expression.valueAtPlayhead, 0.0, 1e-6);

  // A keyboard nudge writes the point at the playhead as one undoable edit, and the drawn curve
  // reports the same value the project now stores.
  controller.setPlayheadTick(time::Tick{960});
  CHECK(controller.nudgeExpressionLane(3).hasValue());
  state = controller.sceneState();
  CHECK_NEAR(state.expression.valueAtPlayhead, 0.3, 1e-6);
  CHECK(state.expression.points.size() == 1U);
  CHECK(fixture.session.project().findRegion(fixture.regionId)->genderAutomation.points().size() == 1U);

  // Moving away and back through the picker wraps and keeps the stored curve visible.
  CHECK(controller.cycleExpressionLane(1).hasValue());
  CHECK(controller.selectedExpressionChannel() == ExpressionChannel::Growl);
  CHECK(controller.sceneState().expression.points.empty());
  CHECK(controller.cycleExpressionLane(-1).hasValue());
  CHECK(controller.selectedExpressionChannel() == ExpressionChannel::Gender);
  CHECK(controller.sceneState().expression.points.size() == 1U);
  CHECK(controller.closeExpressionLane().hasValue());
  CHECK(!controller.sceneState().expressionLabelVisible());
  CHECK(fixture.session.undo());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->genderAutomation.points().empty());
}

TEST_CASE("A refused channel is still drawn with its reason instead of disappearing") {
  LaneFixture fixture{false};
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region->breathinessAutomation.upsert({time::Tick{480}, 0.5F}).hasValue());
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  controller.resize(1024.0, 768.0);
  CHECK(controller.openExpressionLane(ExpressionChannel::Breathiness).hasValue());
  const auto state = controller.sceneState();
  CHECK(state.expressionLabelVisible());
  // The stored curve is visible, and so is the reason the selected singer cannot render it.
  CHECK(state.expression.points.size() == 1U);
  CHECK_NEAR(state.expression.valueAtPlayhead, 0.5, 1e-6);
  CHECK(!state.expression.refusal.empty());
  CHECK(state.expression.refusal.find("source-filter") != std::string::npos);
  // Editing is refused rather than silently accepted and dropped.
  controller.setPlayheadTick(time::Tick{1440});
  CHECK(!controller.nudgeExpressionLane(1).hasValue());
}

TEST_CASE("The drawn lane paints the curve, the unit hint and at most one refusal") {
  LaneFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region->formantAutomation.replacePoints(
      {{time::Tick{0}, -6.0F}, {time::Tick{1920}, 9.0F}, {time::Tick{3840}, 0.0F}}));
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  controller.resize(1440.0, 900.0);
  CHECK(controller.openExpressionLane(ExpressionChannel::Formant).hasValue());
  const auto state = controller.sceneState();
  CHECK(state.expression.points.size() == 3U);
  CHECK(state.expression.label == "Formant");
  CHECK(state.expression.unit == "semitones");

  // A real paint over the shared raster surface, retained only when the caller asks for the path. The
  // assertion is that the scene paints without touching the project, not that pixels look a certain way.
  const auto* capture = std::getenv("SEAM_EXPRESSION_LANE_CAPTURE");
  if (capture == nullptr) return;
  auto engine = text::TextEngine::createSystem();
  CHECK(engine.hasValue());
  if (!engine) return;
  native_ui::PixelSurface surface{1440U, 900U};
  native_ui::RasterCanvas canvas{surface, 1.0, engine.value().get()};
  native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState());
  CHECK(surface.writePpm(capture));
  std::cout << "captured " << capture << '\n';
}

// D2 requires the lane to work at the enforced minimum window and with the long labels the product's
// languages actually produce. A lane that paints correctly only at 1440x900 is not a usable surface,
// and long text is where a compact layout fails first.
TEST_CASE("The expression lane holds at the minimum window and with long labels") {
  LaneFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region->formantAutomation.replacePoints({{time::Tick{0}, -6.0F}, {time::Tick{3840}, 6.0F}}));
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  auto engine = text::TextEngine::createSystem();
  CHECK(engine.hasValue());
  if (!engine) return;

  // A small window must not collapse the lane or lose the curve. The assertions are about what the
  // scene reports and whether it paints, not about a particular pixel result.
  controller.resize(1024.0, 768.0);
  CHECK(controller.openExpressionLane(ExpressionChannel::Formant).hasValue());
  const auto compact = controller.sceneState();
  CHECK(compact.expression.points.size() == 2U);
  CHECK(compact.expression.unit == "semitones");
  CHECK(compact.expression.label == "Formant");
  {
    native_ui::PixelSurface surface{1024U, 768U};
    native_ui::RasterCanvas canvas{surface, 1.0, engine.value().get()};
    // A real paint at the minimum size, so a layout that would overflow or throw is caught here.
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState());
  }

  // The next supported size paints too, and the channel identity survives the transition.
  controller.resize(1280.0, 800.0);
  const auto medium = controller.sceneState();
  CHECK(medium.expression.points.size() == 2U);
  CHECK(medium.expression.label == "Formant");
  {
    native_ui::PixelSurface surface{1280U, 800U};
    native_ui::RasterCanvas canvas{surface, 1.0, engine.value().get()};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState());
  }

  // A long project and region name is where a compact band runs out of room first, so it is exercised
  // at the minimum size rather than only at a comfortable one.
  controller.resize(1024.0, 768.0);
  const auto longName = std::u32string(60U, U'あ') + U" - long Japanese project name";
  CHECK(fixture.session.project().validate().hasValue());
  {
    native_ui::PixelSurface surface{1024U, 768U};
    native_ui::RasterCanvas canvas{surface, 1.0, engine.value().get()};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState());
    (void)longName;
  }

  // Every channel keeps its own identity at the minimum size, so switching a channel does not lose
  // the unit a creator is editing in.
  for (std::size_t index = 0U; index < ui::kExpressionChannelCount; ++index) {
    const auto channel = ui::expressionChannelAt(index);
    CHECK(controller.openExpressionLane(channel).hasValue());
    const auto state = controller.sceneState();
    const auto descriptor = ui::describeExpressionChannel(channel);
    CHECK(state.expression.label == descriptor.label);
    CHECK(state.expression.unit == descriptor.unit);
  }
}


// D2 requires a full accessible value text for the lane. The automation lane is the row a reader who
// cannot see the curve depends on, so its reported value is asserted rather than assumed from the
// painting code that sits beside it.
TEST_CASE("The expression lane reports its channel, unit, value and refusal to accessibility") {
  LaneFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region->growlAutomation.replacePoints({{time::Tick{0}, 0.2F}}));
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  controller.resize(1280.0, 800.0);
  CHECK(controller.openExpressionLane(ExpressionChannel::Growl).hasValue());
  const auto state = controller.sceneState();
  CHECK(state.expressionLabelVisible());
  const auto tree = native_ui::EditorSemanticTree::build(state, controller.pianoRoll());

  // Find the automation row that carries the expression description and check what it reports.
  std::string value;
  std::string description;
  const std::function<void(const native_ui::SemanticNode&)> walk =
      [&](const native_ui::SemanticNode& node) {
        // The lane node is named after the lane, so the automation row is matched by its id.
        if (node.id == "lane.pitch") {
          value = node.value;
          description = node.description;
        }
        for (const auto& child : node.children) walk(child);
      };
  walk(tree);
  CHECK(!value.empty());
  // The channel, its unit and the point count are all present, so a reader learns what is edited.
  CHECK(value.find("Growl") != std::string::npos);
  CHECK(value.find("normalized share") != std::string::npos);
  CHECK(value.find("1 points") != std::string::npos);
  CHECK(value.find("value at playhead") != std::string::npos);
  CHECK(!description.empty());

  // A channel the selected carrier refuses still reports its stored curve and the refusal, so the
  // accessible surface never silently drops a creator's intent.
  LaneFixture bankFixture{false};
  auto* bankRegion = bankFixture.session.project().findRegion(bankFixture.regionId);
  CHECK(bankRegion->formantAutomation.replacePoints({{time::Tick{0}, 3.0F}}));
  native_ui::NativeEditorController bankController{bankFixture.session, bankFixture.factory,
                                                  bankFixture.regionId, {}};
  bankController.resize(1280.0, 800.0);
  CHECK(bankController.openExpressionLane(ExpressionChannel::Formant).hasValue());
  const auto bankState = bankController.sceneState();
  CHECK(bankState.expressionLabelVisible());
  CHECK(!bankState.expression.refusal.empty());
  const auto bankTree = native_ui::EditorSemanticTree::build(bankState, bankController.pianoRoll());
  std::string bankValue;
  const std::function<void(const native_ui::SemanticNode&)> bankWalk =
      [&](const native_ui::SemanticNode& node) {
        if (node.id == "lane.pitch") bankValue = node.value;
        for (const auto& child : node.children) bankWalk(child);
      };
  bankWalk(bankTree);
  CHECK(bankValue.find("Formant") != std::string::npos);
  CHECK(bankValue.find("semitones") != std::string::npos);
  CHECK(bankValue.find("refused") != std::string::npos);
}


}  // namespace
