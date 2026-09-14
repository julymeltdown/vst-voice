// The dock draws the phrase that is playing, and a published render is what binds it.
//
// The character layer and the renderer now describe a performance, but a description that never
// reaches a window is not a product: these cases check that the dock's read model survives the trip
// into the scene, that painting a phrase really changes what is drawn, that reduced motion removes the
// movement without removing the state, and that a render completing in the standalone session binds
// the dock to the phrase it published -- including the playhead mapping a seek depends on.
//
// The musical material here is one procedural note. Nothing is listened to, the mouths are the dock's
// own drawing vocabulary rather than a phonetic claim, and no roadmap unit is accepted.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/character/performance.hpp"
#include "seam/native_ui/character_presentation.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/editor_semantics.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/standalone/authoring_session.hpp"
#include "seam/text/text_engine.hpp"
#include "seam/ui/piano_roll_model.hpp"
#include "seam/voice_design/recipe_resource.hpp"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace seam;

struct DockFixture final {
  application::ProjectFactory factory{7100U};
  domain::RegionId regionId{};
  application::EditorSession session;

  DockFixture() : session(makeProject()) {}

  domain::Project makeProject() {
    auto project = factory.createProject("Dock");
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

voice_design::VoiceRecipe dockRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "dock-performance";
  recipe.poses = {
      {"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  return recipe;
}

native_ui::EditorSceneState::CharacterPerformanceView view(character::MouthShape mouth,
                                                           float energy, bool performing,
                                                           bool reducedMotion = false) {
  return native_ui::EditorSceneState::CharacterPerformanceView{
      .mouth = mouth,
      .energy = energy,
      .expression = 0.0F,
      .performing = performing,
      .reducedMotion = reducedMotion,
  };
}

// Paints the given state and returns what the dock's own pixels say about it.
std::uint64_t paintDock(native_ui::EditorScenePainter& painter, ui::PianoRollModel& model,
                        const native_ui::EditorSceneState& state, text::TextEngine* engine) {
  native_ui::PixelSurface surface{900U, 640U};
  native_ui::RasterCanvas canvas{surface, 1.0, engine};
  painter.paint(canvas, model, state);
  return surface.checksum();
}

TEST_CASE("The dock carries a performance into the scene and honours reduced motion") {
  DockFixture fixture;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory,
                                               fixture.regionId, {}};
  CHECK(!controller.sceneState().characterPerformance.has_value());
  controller.setCharacterPerformance(view(character::MouthShape::Open, 0.75F, true));
  const auto scene = controller.sceneState();
  CHECK(scene.characterPerformance.has_value());
  CHECK(scene.characterPerformance->mouth == character::MouthShape::Open);
  CHECK_NEAR(scene.characterPerformance->energy, 0.75, 1e-6);
  CHECK(scene.characterPerformance->performing);
  // The host's accessibility setting is applied by the presentation, not stored in the phrase.
  CHECK(!scene.characterPerformance->reducedMotion);
  controller.clearCharacterPerformance();
  CHECK(!controller.sceneState().characterPerformance.has_value());

  native_ui::NativeEditorController reduced{fixture.session, fixture.factory, fixture.regionId,
      native_ui::EditorHostCallbacks{.reduceMotionEnabled = [] { return true; }}};
  reduced.setCharacterPerformance(view(character::MouthShape::Round, 0.25F, true));
  CHECK(reduced.sceneState().characterPerformance.has_value());
  CHECK(reduced.sceneState().characterPerformance->reducedMotion);
}

TEST_CASE("Painting a phrase changes the dock, and reduced motion removes only the movement") {
  DockFixture fixture;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory,
                                               fixture.regionId, {}};
  auto state = controller.sceneState();
  state.logicalWidth = 900.0;
  state.logicalHeight = 640.0;
  // The dock only draws in Full mode with a verified character and a portrait, which is what the
  // product requires before it shows a singer's face at all.
  state.characterMode = domain::CharacterDisplayMode::Full;
  state.voiceIdentity.characterActive = true;
  native_ui::PixelSurface portrait{220U, 200U};
  portrait.clear(native_ui::Color{40U, 20U, 60U, 255U});
  state.characterPortrait = &portrait;
  state.characterName = "Pilot";

  native_ui::EditorScenePainter painter;
  std::unique_ptr<text::TextEngine> engine;
  if (auto created = text::TextEngine::createSystem(); created) engine = std::move(created).value();
  auto* textEngine = engine.get();
  const auto withoutPerformance = paintDock(painter, controller.pianoRoll(), state, textEngine);
  state.characterPerformance = view(character::MouthShape::Open, 0.8F, true);
  const auto withPerformance = paintDock(painter, controller.pianoRoll(), state, textEngine);
  CHECK(withPerformance != withoutPerformance);
  state.characterPerformance = view(character::MouthShape::Open, 0.8F, true, true);
  const auto withoutMovement = paintDock(painter, controller.pianoRoll(), state, textEngine);
  // Reduced motion is a different drawing, not no drawing: the label and the measured level stay.
  CHECK(withoutMovement != withPerformance);
  CHECK(withoutMovement != withoutPerformance);
  state.characterPerformance.reset();
  const auto cleared = paintDock(painter, controller.pianoRoll(), state, textEngine);
  CHECK(cleared == withoutPerformance);
}

// A render is submitted asynchronously; a published phrase is what this suite waits for.
authoring::RenderState waitForRender(standalone::AuthoringSession& session,
                                     std::uint64_t revision) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
  while (std::chrono::steady_clock::now() < deadline) {
    const auto progress = session.runtime().renderer().progress();
    if (progress.requestedRevision == revision &&
        (progress.state == authoring::RenderState::Ready ||
         progress.state == authoring::RenderState::Failed ||
         progress.state == authoring::RenderState::Cancelled))
      return progress.state;
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  return authoring::RenderState::Idle;
}

TEST_CASE("The dock says what is singing and whether that phrase has fallen behind") {
  DockFixture fixture;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory,
                                               fixture.regionId, {}};
  auto state = controller.sceneState();
  state.logicalWidth = 900.0;
  state.logicalHeight = 640.0;
  state.characterMode = domain::CharacterDisplayMode::Full;
  state.voiceIdentity.characterActive = true;
  state.characterName = "Pilot";
  native_ui::PixelSurface portrait{220U, 200U};
  portrait.clear(native_ui::Color{30U, 10U, 40U, 255U});
  state.characterPortrait = &portrait;
  state.characterPerformance = view(character::MouthShape::Open, 0.8F, true);

  const auto dockValue = [&](const native_ui::EditorSceneState& candidate) {
    const auto tree = native_ui::EditorSemanticTree::build(candidate, controller.pianoRoll());
    const auto dock = std::find_if(tree.children.begin(), tree.children.end(),
                                   [](const auto& child) { return child.id == "character.dock"; });
    CHECK(dock != tree.children.end());
    return dock->value;
  };
  const auto fresh = dockValue(state);
  // A reader that cannot see the mouth still learns what is being sung and how loud it is.
  CHECK(fresh.find("singing") != std::string::npos);
  CHECK(fresh.find("mouth open") != std::string::npos);
  CHECK(fresh.find("level 80 percent") != std::string::npos);
  CHECK(fresh.find("changed after this render") == std::string::npos);

  native_ui::EditorScenePainter painter;
  std::unique_ptr<text::TextEngine> engine;
  if (auto created = text::TextEngine::createSystem(); created) engine = std::move(created).value();
  const auto freshPixels = paintDock(painter, controller.pianoRoll(), state, engine.get());

  state.characterPerformance->audibleStale = true;
  const auto stale = dockValue(state);
  CHECK(stale.find("changed after this render") != std::string::npos);
  const auto stalePixels = paintDock(painter, controller.pianoRoll(), state, engine.get());
  // The phrase keeps playing and the dock says that the project has moved on since it was rendered.
  CHECK(stalePixels != freshPixels);

  // A dock with no phrase at all says that too, instead of describing an empty performance.
  state.characterPerformance.reset();
  const auto idle = dockValue(state);
  CHECK(idle.find("singing") == std::string::npos);
  CHECK(idle.find("mouth") == std::string::npos);
}

TEST_CASE("A completed render binds the dock to the phrase it published") {
  const auto scratch = test::support::temporaryDirectory("dock-performance");
  std::filesystem::create_directories(scratch / "banks");
  auto session = standalone::AuthoringSession::create(standalone::AuthoringSessionConfig{
      .cacheRoot = scratch / "cache",
      .voicebankRoots = {voicebank::VoicebankSearchRoot{
          .path = scratch / "banks", .kind = voicebank::VoicebankRootKind::Development}},
      .sampleRate = 48000U,
      .outputChannels = 2U,
      .bindFirstAvailableVoicebank = false,
      .allowDevelopmentVoicebanks = true,
  });
  CHECK(session.hasValue());
  if (!session) return;
  auto& created = *session.value();
  // No render has published anything yet, so the dock has no phrase to draw.
  CHECK(created.characterPerformanceGeneration() == 0U);
  CHECK(created.characterPerformance() == nullptr);
  const auto beforeRender = created.characterPerformanceGeneration();

  auto& document = created.runtime().document();
  auto& project = document.session().project();
  auto* region = project.findRegion(created.regionId());
  CHECK(region != nullptr);
  if (region == nullptr) return;
  // The untitled project arrives with its own placeholder material, and this suite renders exactly one
  // declared vowel, so the region is replaced rather than extended.
  region->lyrics.clear();
  region->notes.clear();
  // The rendered phrase has to end with its own note: a region that continues past the note asks the
  // recipe for a trailing pause source, which this one-vowel fixture does not declare.
  region->durationTick = time::Tick{1920};
  // A Japanese inventory reads its own kana; an ASCII letter is not a kana and resolves to a pause,
  // which this recipe declares no source for.
  auto [lyric, note] = document.factory().makeNote(time::Tick{0}, time::Tick{1920}, 69U, U"あ",
                                                  domain::Language::Japanese);
  region->lyrics.push_back(std::move(lyric));
  region->notes.push_back(std::move(note));
  const auto recipe = voice_design::freezeVoiceRecipeResource(dockRecipe());
  CHECK(recipe.hasValue());
  if (!recipe) return;
  const std::vector<rendering::TrackSingerSource> sources{
      rendering::TrackProceduralSource{created.trackId(), recipe.value(), "neutral"}};

  // A render that publishes a complete identity binds the dock to the phrase it rendered.
  created.runtime().renderer().submitWithSources(project, sources, created.trackId(),
                                                created.regionId(), 1U, 48000U,
                                                rendering::RenderQuality::Preview, true);
  if (waitForRender(created, 1U) != authoring::RenderState::Ready)
    throw test::Failure{"the first render did not become ready: " +
                        created.runtime().renderer().progress().diagnostic};
  const auto* performance = created.characterPerformance();
  CHECK(performance != nullptr);
  if (performance == nullptr) return;
  CHECK(performance->resourceId == recipe.value().identity.id);
  CHECK(performance->resourceContentHash == recipe.value().identity.contentHash);
  CHECK(performance->style == "neutral");
  CHECK(performance->renderRevision == 1U);
  CHECK(performance->sampleRate == 48000U);
  CHECK(!performance->cues.empty());
  CHECK(performance->validate().hasValue());
  const auto generation = created.characterPerformanceGeneration();
  CHECK(generation == beforeRender + 1U);
  // Evaluating again without a new render is not a new phrase.
  CHECK(created.characterPerformance() == performance);
  CHECK(created.characterPerformanceGeneration() == generation);

  // The playhead maps through the project's own tempo map, so a seek lands where the audio is.
  const auto inside = created.characterPerformanceFrameAt(time::Tick{0});
  CHECK(inside.has_value());
  CHECK(inside->performing);
  CHECK(inside->energy > 0.0F);
  const auto past = created.characterPerformanceFrameAt(time::Tick{1920 * 4});
  CHECK(past.has_value());
  CHECK(!past->performing);
  CHECK(past->mouth == character::MouthShape::Closed);
  CHECK(!created.characterPerformanceStale());

  // A second render is a second phrase: the dock rebinds, and the previous snapshot is not reused.
  created.runtime().renderer().submitWithSources(project, sources, created.trackId(),
                                                created.regionId(), 2U, 48000U,
                                                rendering::RenderQuality::Preview, true);
  if (waitForRender(created, 2U) != authoring::RenderState::Ready)
    throw test::Failure{"the second render did not become ready: " +
                        created.runtime().renderer().progress().diagnostic};
  const auto* rebound = created.characterPerformance();
  CHECK(rebound != nullptr);
  if (rebound == nullptr) return;
  CHECK(rebound->renderRevision == 2U);
  CHECK(created.characterPerformanceGeneration() == generation + 1U);

  // A render that fails leaves the audible phrase exactly where it was, and the dock says that the
  // project has moved on instead of drawing the old phrase as if it were current.
  auto broken = sources;
  std::get<rendering::TrackProceduralSource>(broken.front()).resource.identity.contentHash =
      std::string(64U, 'f');
  created.runtime().renderer().submitWithSources(project, broken, created.trackId(),
                                                created.regionId(), 3U, 48000U,
                                                rendering::RenderQuality::Preview, true);
  CHECK(waitForRender(created, 3U) == authoring::RenderState::Failed);
  CHECK(created.characterPerformanceStale());
  const auto* audible = created.characterPerformance();
  CHECK(audible != nullptr);
  if (audible != nullptr) CHECK(audible->renderRevision == 2U);
}

}  // namespace
