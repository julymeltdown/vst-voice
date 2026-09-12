#include "test_framework.hpp"
#include "seam/native_ui/accessibility_tree.hpp"

TEST_CASE("custom accessibility surfaces retain numeric actions without virtual notes") {
  using namespace seam::native_ui;
  AccessibilityTree tree;
  SemanticNode root{.id = "designer", .role = SemanticRole::Panel, .name = "Voice Designer"};
  root.children.push_back({.id = "parameter", .role = SemanticRole::TextField, .name = "Open quotient", .value = "0.62",
      .actions = {SemanticAction::SetFocus, SemanticAction::EditText}, .editableValue = "0.62"});
  root.children.push_back({.id = "save", .role = SemanticRole::Button, .name = "Save voice",
      .actions = {SemanticAction::SetFocus, SemanticAction::Activate}});
  tree.rebuildCustom(std::move(root), "parameter");
  CHECK(tree.virtualizedNoteCount() == 0U); CHECK(tree.materializeNotes(0U, 10U).empty());
  CHECK(tree.focusedNode()); CHECK(tree.focusedNode()->id == "parameter");
  CHECK(tree.focusNext(false)); CHECK(tree.focusedNode()->id == "save");
  bool invoked = false;
  CHECK(tree.dispatch("parameter", SemanticAction::EditText, [&](std::string_view id, SemanticAction) {
    invoked = id == "parameter"; return seam::core::success();
  }));
  CHECK(invoked);
  CHECK(!tree.dispatch("missing", SemanticAction::Activate, [](std::string_view, SemanticAction) { return seam::core::success(); }));
  tree.rebuildCustom({.id = "replacement", .role = SemanticRole::Panel, .name = "Empty Designer"});
  CHECK(!tree.focusedNode()); CHECK(!tree.setFocus("parameter"));
}

#include "seam/application/project_factory.hpp"

TEST_CASE("accessibility tree virtualizes large note collections") {
  seam::application::ProjectFactory factory{30000U};
  auto project = factory.createProject("Accessibility");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, trackId, "Verse",
                                           seam::time::Tick{0},
                                           seam::time::Tick{1920});
  auto* region = project.findRegion(regionId);
  for (std::size_t index = 0U; index < 1000U; ++index) {
    auto [lyric, note] = factory.makeNote(
        seam::time::Tick{0}, seam::time::Tick{240},
        static_cast<std::uint8_t>(index % 80U + 24U), U"a");
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  }
  CHECK(project.validate());
  seam::application::EditorSession session{std::move(project)};
  seam::ui::PianoRollModel model{session, factory, regionId};
  model.pitch().setTopMidiKey(127);
  model.pitch().setRowHeight(4.0);
  model.setViewport(seam::ui::PianoRollViewport{
      .bounds = seam::ui::Rect{0.0, 0.0, 10000.0, 20000.0},
      .keyboardWidth = 0.0,
  });
  seam::native_ui::EditorSceneState state;
  state.logicalWidth = 1440.0;
  state.logicalHeight = 900.0;
  state.projectName = "Accessibility";
  seam::native_ui::AccessibilityTree tree;
  tree.rebuild(state, model,
               seam::native_ui::AccessibilityTreeConfig{
                   .maximumMaterializedNotes = 64U});
  CHECK(tree.virtualizedNoteCount() == 1000U);
  CHECK(tree.materializedNoteCount() == 64U);
  CHECK(tree.materializeNotes(0U, 64U).size() == 64U);
  CHECK(tree.materializeNotes(960U, 64U).size() == 40U);
  CHECK(tree.root().virtualizedChildCount == 1000U);
  bool dispatched = false;
  const auto first = tree.materializeNotes(0U, 1U).front().id;
  CHECK(tree.dispatch(
      first, seam::native_ui::SemanticAction::SetFocus,
      [&dispatched](std::string_view, seam::native_ui::SemanticAction) {
        dispatched = true;
        return seam::core::success();
      }));
  CHECK(dispatched);

  CHECK(tree.focusNext(false));
  const auto* firstTabStop = tree.focusedNode();
  CHECK(firstTabStop != nullptr);
  const auto firstTabStopId = firstTabStop->id;
  CHECK(tree.focusNext(false));
  const auto* secondTabStop = tree.focusedNode();
  CHECK(secondTabStop != nullptr);
  CHECK(secondTabStop->id != firstTabStopId);
  CHECK(tree.focusNext(true));
  CHECK(tree.focusedNode() != nullptr);
  CHECK(tree.focusedNode()->id == firstTabStopId);
  CHECK(tree.focusNext(true));
  const auto* lastTabStop = tree.focusedNode();
  CHECK(lastTabStop != nullptr);
  CHECK(lastTabStop->id != firstTabStopId);
  CHECK(tree.focusNext(false));
  CHECK(tree.focusedNode() != nullptr);
  CHECK(tree.focusedNode()->id == firstTabStopId);
}

TEST_CASE("accessibility tree includes notes outside the piano viewport") {
  seam::application::ProjectFactory factory{30000U};
  auto project = factory.createProject("Offscreen accessibility");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(
      project, trackId, "Verse", seam::time::Tick{0},
      seam::time::Tick{3000000});
  auto* region = project.findRegion(regionId);
  for (std::size_t index = 0U; index < 1000U; ++index) {
    auto [lyric, note] = factory.makeNote(
        seam::time::Tick{static_cast<std::int64_t>(index) * 240},
        seam::time::Tick{180}, static_cast<std::uint8_t>(60U + index % 12U),
        U"あ");
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  }
  seam::application::EditorSession session{std::move(project)};
  seam::ui::PianoRollModel model{session, factory, regionId};
  model.pitch().setTopMidiKey(127);
  model.pitch().setRowHeight(4.0);
  model.setViewport(seam::ui::PianoRollViewport{
      .bounds = seam::ui::Rect{0.0, 0.0, 320.0, 800.0},
      .keyboardWidth = 0.0,
  });
  seam::native_ui::EditorSceneState state;
  state.projectName = "Offscreen accessibility";
  state.logicalWidth = 320.0;
  state.logicalHeight = 800.0;
  seam::native_ui::AccessibilityTree tree;
  tree.rebuild(state, model,
               seam::native_ui::AccessibilityTreeConfig{
                   .maximumMaterializedNotes = 64U});

  CHECK(tree.virtualizedNoteCount() == 1000U);
  CHECK(tree.materializeNotes(960U, 64U).size() == 40U);
}
