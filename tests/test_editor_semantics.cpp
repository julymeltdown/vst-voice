#include "test_framework.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_semantics.hpp"

TEST_CASE("editor semantic tree exposes stable accessible controls") {
  seam::application::ProjectFactory factory{700U};
  auto project = factory.createProject("Semantic");
  const auto track = factory.addVocalTrack(project, "Voice");
  const auto region = factory.addRegion(project, track, "Region",
                                        seam::time::Tick{0},
                                        seam::time::Tick{3840});
  auto session = seam::application::EditorSession{std::move(project)};
  seam::native_ui::NativeEditorController controller{session, factory, region};
  controller.resize(1280.0, 720.0);
  const auto state = controller.sceneState();
  const auto tree = seam::native_ui::EditorSemanticTree::build(
      state, controller.pianoRoll());
  CHECK(tree.id == "editor");
  CHECK(seam::native_ui::EditorSemanticTree::containsId(tree, "toolbar.transport"));
  CHECK(seam::native_ui::EditorSemanticTree::containsId(tree, "timeline"));
  CHECK(seam::native_ui::EditorSemanticTree::containsId(tree, "lane.phoneme"));
  CHECK(seam::native_ui::EditorSemanticTree::containsId(tree, "status.render"));
}
