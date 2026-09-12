#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/text/text_engine.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include <chrono>
#include <cstdlib>
#include <thread>

TEST_CASE("native measured dynamics follows rendered frame windows channels and document freshness") {
  using namespace seam;
  application::ProjectFactory factory{371000U}; auto project = factory.createProject("Measured dynamics");
  const auto track = factory.addVocalTrack(project, "Singer");
  const auto region = factory.addRegion(project, track, "Phrase", time::Tick{960}, time::Tick{1920});
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{1920}, 69U, U"あ", domain::Language::Japanese);
  project.findRegion(region)->lyrics.push_back(lyric); project.findRegion(region)->notes.push_back(note);
  CHECK(project.tempoMap().addOrReplace(time::Tick{1920}, 90.0));
  voice_design::VoiceRecipe recipe; recipe.id = "measurement-fixture";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  const auto resource = voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  application::EditorSession session{project};
  authoring::AuthoringRenderCoordinator coordinator{test::support::temporaryDirectory("measured-native")};
  coordinator.submitWithSources(project, {rendering::TrackProceduralSource{track, resource.value(), "neutral"}},
      track, region, 0U, 48000U, rendering::RenderQuality::Final, true);
  const auto renderDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
  while (!coordinator.acquireCurrent() && std::chrono::steady_clock::now() < renderDeadline)
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  const auto audio = coordinator.latest(); CHECK(coordinator.matchesCurrent(*audio));
  native_ui::NativeEditorController controller{session, factory, region};
  controller.setMeasurementCoordinator(coordinator); controller.resize(480.0, 320.0); CHECK(controller.openDynamicsInspector());
  const auto cycle = [&] {
    controller.rebuildAccessibilityTree(); std::string id;
    for (const auto& child : controller.accessibilityTree().root().children) if (child.id.ends_with("zoom.3")) id = child.id;
    CHECK(!id.empty()); CHECK(controller.dispatchAccessibility(id, native_ui::SemanticAction::Activate));
  };
  const auto wait = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
    while (std::chrono::steady_clock::now() < deadline) {
      controller.pollReplacementReview();
      const auto plot = controller.sceneState().replacementReview.dynamicsPlot;
      if (plot && !plot->measured.empty()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return false;
  };
  cycle(); CHECK(wait());
  auto plot = controller.sceneState().replacementReview.dynamicsPlot; CHECK(plot); CHECK(plot->measuredMode);
  CHECK(plot->measuredChannel == 1U); CHECK(plot->measurementLabel.find("Final / rev 0") != std::string::npos);
  CHECK(controller.sceneState().replacementReview.summary.starts_with("RMS "));
  controller.rebuildAccessibilityTree(); bool measuredSemantics = false;
  for (const auto& child : controller.accessibilityTree().root().children) if (child.id.ends_with("curve")) {
    measuredSemantics = true; CHECK(child.value.find("dBFS / FS") != std::string::npos);
    CHECK(child.description.find("not the unsaved point") != std::string::npos);
  }
  CHECK(measuredSemantics);
  CHECK(plot->handles.empty()); CHECK(!plot->candidate);
  const auto first = static_cast<std::size_t>(project.tempoMap().sampleFrameAt(time::Tick{960}, 48000U));
  const auto last = static_cast<std::size_t>(project.tempoMap().sampleFrameAt(time::Tick{2880}, 48000U));
  CHECK(audio->result.interleaved.size() / audio->result.channelCount >= last);
  for (std::size_t i = 0U; i < first * audio->result.channelCount; ++i) CHECK(audio->result.interleaved[i] == 0.0F);
  const auto end = std::min(last, audio->result.interleaved.size() / audio->result.channelCount);
  const auto expected = rendering::measureAudioLevels({audio->result.interleaved.data(), audio->result.interleaved.size()},
      audio->result.channelCount, first, end - first, 256U); CHECK(expected);
  CHECK(plot->measured.size() == expected.value().bins.size());
  for (std::size_t i = 0; i < plot->measured.size(); ++i) {
    const auto& bin = expected.value().bins[i]; const auto db = bin.channels[0].rmsDbfs().value_or(-96.0);
    const auto tick = project.tempoMap().tickAtSampleFrame(static_cast<time::SampleFrame>(bin.firstFrame + (bin.frameCount - 1U) / 2U), 48000U) - time::Tick{960};
    CHECK_NEAR(plot->measured[i].x, plot->bounds.x + plot->bounds.width * static_cast<double>(tick.value()) / 1920.0, 0.00001);
    CHECK_NEAR(plot->measured[i].y, plot->bounds.y + plot->bounds.height * (1.0 - (std::clamp(db, -96.0, plot->measuredCeilingDb) + 96.0) / (96.0 + plot->measuredCeilingDb)), 0.00001);
  }
  if (const auto* path = std::getenv("SEAM_MEASURED_DYNAMICS_CAPTURE")) {
    auto engine = text::TextEngine::createSystem(); CHECK(engine);
    native_ui::PixelSurface surface{480U,320U}; native_ui::RasterCanvas canvas{surface,1.0,engine.value().get()};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState()); CHECK(surface.writePpm(path));
  }
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Plus}));
  CHECK(controller.sceneState().replacementReview.dynamicsPlot->measured.empty()); CHECK(wait());
  controller.rebuildAccessibilityTree(); std::string channelButton;
  for (const auto& child : controller.accessibilityTree().root().children) if (child.id.ends_with("zoom.3")) channelButton = child.id;
  CHECK(controller.dispatchAccessibility(channelButton, native_ui::SemanticAction::SetFocus));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Enter})); CHECK(controller.sceneState().replacementReview.dynamicsPlot->measuredChannel == 2U);
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Enter})); CHECK(!controller.sceneState().replacementReview.dynamicsPlot->measuredMode);
  cycle(); CHECK(wait());
  CHECK(session.project() == project); CHECK(!session.canUndo());
  coordinator.submitWithSources(project, {rendering::TrackProceduralSource{track, resource.value(), "neutral"}},
      track, region, 0U, 96000U, rendering::RenderQuality::Final, true);
  CHECK(controller.sceneState().replacementReview.dynamicsPlot->measured.empty());
  CHECK(wait()); CHECK(coordinator.latest()->result.sampleRate == 96000U);
  CHECK(coordinator.latest()->requestId != audio->requestId); // Same score revision, different measured source and frame window.
  CHECK(session.replaceProject(project)); controller.pollReplacementReview();
  CHECK(controller.sceneState().replacementReview.dynamicsPlot->measured.empty());
  CHECK(controller.openDynamicsInspector()); cycle(); controller.pollReplacementReview();
  plot = controller.sceneState().replacementReview.dynamicsPlot;
  CHECK(plot->measured.empty()); CHECK(plot->measurementLabel.find("Render the current document") != std::string::npos);
}
