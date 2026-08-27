#include "test_framework.hpp"
#include "native_ui_design_fixture.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_frame_layout.hpp"
#include "seam/native_ui/editor_semantics.hpp"

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

TEST_CASE("semantic selection is distinct from keyboard focus") {
  const auto node = seam::native_ui::EditorSemanticTree::noteNode(
      seam::ui::NoteVisual{
          .noteId = seam::domain::NoteId{42U},
          .bounds = seam::ui::Rect{10.0, 20.0, 30.0, 12.0},
          .midiKey = 64U,
          .absoluteStart = seam::time::Tick{0},
          .duration = seam::time::Tick{480},
          .selected = true,
          .lyric = "あ",
      });
  CHECK(node.selected);
  CHECK(!node.focused);
  CHECK(node.editableValue == "あ");
}

TEST_CASE("editor semantic tree exposes stable accessible controls") {
  const auto findNode = [](const seam::native_ui::SemanticNode& root,
                           std::string_view id,
                           const auto& self) -> const seam::native_ui::SemanticNode* {
    if (root.id == id) return &root;
    for (const auto& child : root.children) {
      if (const auto* match = self(child, id, self); match != nullptr) {
        return match;
      }
    }
    return nullptr;
  };
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
  const auto status = std::find_if(
      tree.children.begin(), tree.children.end(), [](const auto& child) {
        return child.id == "status.render";
      });
  CHECK(status != tree.children.end());
  CHECK(status->bounds.y == 720.0 - 28.0);
  CHECK(status->bounds.height == 28.0);
  CHECK(tree.bounds.height == 720.0);

  auto technicalState = state;
  technicalState.phonemes.tokens.resize(3U);
  technicalState.phonemes.warnings.resize(1U);
  technicalState.unitOverrides.resize(2U);
  technicalState.seamOverrides.resize(1U);
  technicalState.selectedSeam = seam::domain::PhonemeKey{
      .noteId = seam::domain::NoteId{1U}, .ordinal = 0U};
  technicalState.pitchAutomation.resize(4U);
  const auto technicalTree = seam::native_ui::EditorSemanticTree::build(
      technicalState, controller.pianoRoll());
  const auto findChild = [&findNode](const auto& root, std::string_view id) {
    return findNode(root, id, findNode);
  };
  CHECK(findChild(technicalTree, "lane.phoneme")->value ==
        "3 tokens / 1 warnings");
  CHECK(findChild(technicalTree, "lane.unit")->value == "2 overrides");
  CHECK(findChild(technicalTree, "lane.seam")->value ==
        "1 overrides / boundary selected");
  CHECK(findChild(technicalTree, "lane.pitch")->value ==
        "4 automation points");
  CHECK(findChild(technicalTree, "lane.pitch")->description ==
        "Pitch automation points and interpolation");

  auto exportState = state;
  exportState.exportProgress.state = seam::authoring::ExportState::Staging;
  exportState.exportProgress.completedFiles = 1U;
  exportState.exportProgress.totalFiles = 2U;
  const auto exportTree = seam::native_ui::EditorSemanticTree::build(
      exportState, controller.pianoRoll());
  const auto exportProgress = std::find_if(
      exportTree.children.begin(), exportTree.children.end(), [](const auto& child) {
        return child.id == "export.progress";
      });
  CHECK(exportProgress != exportTree.children.end());
  CHECK(exportProgress->bounds.y == 720.0 - 28.0 - 28.0);
  CHECK(exportProgress->bounds.height == 28.0);
  CHECK(exportProgress->bounds.bottom() == status->bounds.y);

  exportState.lastExport = seam::authoring::ExportResult{
      .state = seam::authoring::ExportState::Committed,
      .receiptPath = "/tmp/song-export/receipt.json",
      .setPath = "/tmp/song-export",
  };
  const auto committedExportTree = seam::native_ui::EditorSemanticTree::build(
      exportState, controller.pianoRoll());
  const auto committedExport = std::find_if(
      committedExportTree.children.begin(), committedExportTree.children.end(),
      [](const auto& child) { return child.id == "export.progress"; });
  CHECK(committedExport != committedExportTree.children.end());
  CHECK(committedExport->value.find("/tmp/song-export") != std::string::npos);
  CHECK(committedExport->value.find("receipt.json") != std::string::npos);

  controller.resize(480.0, 320.0);
  const auto narrowState = controller.sceneState();
  const auto narrowTree = seam::native_ui::EditorSemanticTree::build(
      narrowState, controller.pianoRoll());
  const auto narrowStatus = std::find_if(
      narrowTree.children.begin(), narrowTree.children.end(), [](const auto& child) {
        return child.id == "status.render";
      });
  CHECK(narrowStatus != narrowTree.children.end());
  CHECK(narrowTree.bounds.height == 320.0);
  CHECK(narrowStatus->bounds.y == 292.0);
  CHECK(narrowStatus->bounds.bottom() == 320.0);
  const auto* narrowStop = findNode(narrowTree, "toolbar.stop", findNode);
  CHECK(narrowStop != nullptr);
  CHECK(narrowStop->bounds.right() <= narrowTree.bounds.right());
  const auto* narrowTempo = findNode(narrowTree, "toolbar.tempo", findNode);
  CHECK(narrowTempo != nullptr);
  CHECK(narrowTempo->bounds.right() <= narrowTree.bounds.right());
  const auto narrowPhonemeLane = std::find_if(
      narrowTree.children.begin(), narrowTree.children.end(), [](const auto& child) {
        return child.id == "lane.phoneme";
      });
  CHECK(narrowPhonemeLane != narrowTree.children.end());
  const seam::native_ui::EditorSceneLayout narrowLayout;
  const auto narrowTechnical = seam::native_ui::resolveTechnicalLaneHeights(
      seam::native_ui::TechnicalLaneLayoutInput{
          .presentation = narrowState.technicalLanes,
          .populated = {false, false, false, false},
          .previewHeights = {narrowLayout.phonemeLaneHeight,
                             narrowLayout.unitLaneHeight,
                             narrowLayout.seamLaneHeight,
                             narrowLayout.automationLaneHeight},
          .contentTop = narrowLayout.contentTop(),
          .contentBottom = 320.0 - narrowLayout.statusHeight,
      });
  CHECK_NEAR(narrowPhonemeLane->bounds.y,
             narrowTechnical.pianoBottom, 1e-9);
  const auto narrowSeamLane = std::find_if(
      narrowTree.children.begin(), narrowTree.children.end(), [](const auto& child) {
        return child.id == "lane.seam";
      });
  CHECK(narrowSeamLane != narrowTree.children.end());
  const auto narrowPitchLane = std::find_if(
      narrowTree.children.begin(), narrowTree.children.end(), [](const auto& child) {
        return child.id == "lane.pitch";
      });
  CHECK(narrowPitchLane != narrowTree.children.end());
  const auto treeBoundsAreContained =
      [](const seam::native_ui::SemanticNode& node,
         const seam::ui::Rect& rootBounds, const auto& self) -> bool {
    const auto contained =
        node.bounds.x >= rootBounds.x && node.bounds.y >= rootBounds.y &&
        node.bounds.right() <= rootBounds.right() &&
        node.bounds.bottom() <= rootBounds.bottom() && node.bounds.width > 0.0 &&
        node.bounds.height > 0.0;
    if (node.id != "editor" && !contained) return false;
    return std::all_of(node.children.begin(), node.children.end(),
                       [&rootBounds, &self](const auto& child) {
                         return self(child, rootBounds, self);
                       });
  };
  CHECK(treeBoundsAreContained(narrowTree, narrowTree.bounds,
                               treeBoundsAreContained));

  auto narrowOverlayState = narrowState;
  narrowOverlayState.diagnostics.push_back(seam::authoring::Diagnostic{
      .code = "RENDER_FAILED",
      .severity = seam::authoring::DiagnosticSeverity::Error,
  });
  narrowOverlayState.exportProgress.totalFiles = 1U;
  const auto narrowOverlayTree = seam::native_ui::EditorSemanticTree::build(
      narrowOverlayState, controller.pianoRoll());
  CHECK(treeBoundsAreContained(narrowOverlayTree, narrowOverlayTree.bounds,
                               treeBoundsAreContained));

  controller.resize(1280.0, 720.0);
  seam::native_ui::PixelSurface portrait{1U, 1U};
  auto dockState = controller.sceneState();
  dockState.characterMode = seam::domain::CharacterDisplayMode::Full;
  dockState.characterPortrait = &portrait;
  const auto dockTree = seam::native_ui::EditorSemanticTree::build(
      dockState, controller.pianoRoll());
  const auto dockTimeline = std::find_if(
      dockTree.children.begin(), dockTree.children.end(), [](const auto& child) {
        return child.id == "timeline";
      });
  CHECK(dockTimeline != dockTree.children.end());
  CHECK(dockTimeline->bounds.width == 1280.0);
  const auto dock = std::find_if(
      dockTree.children.begin(), dockTree.children.end(), [](const auto& child) {
        return child.id == "character.dock";
      });
  CHECK(dock == dockTree.children.end());

  auto minimalPortraitState = controller.sceneState();
  minimalPortraitState.characterMode = seam::domain::CharacterDisplayMode::Minimal;
  minimalPortraitState.characterPortrait = &portrait;
  const auto minimalPortraitTree = seam::native_ui::EditorSemanticTree::build(
      minimalPortraitState, controller.pianoRoll());
  CHECK(!seam::native_ui::EditorSemanticTree::containsId(
      minimalPortraitTree, "character.portrait"));

  seam::application::ProjectFactory detailFactory{740U};
  auto detailProject = detailFactory.createProject("Detail");
  const auto detailTrack = detailFactory.addVocalTrack(detailProject, "Voice");
  const auto detailRegion = detailFactory.addRegion(
      detailProject, detailTrack, "Region", seam::time::Tick{0},
      seam::time::Tick{3840});
  auto [detailLyric, detailNote] = detailFactory.makeNote(
      seam::time::Tick{480}, seam::time::Tick{960}, 64U, U"こんにちは");
  const auto detailNoteId = detailNote.id;
  auto* detailRegionModel = detailProject.findRegion(detailRegion);
  CHECK(detailRegionModel != nullptr);
  detailRegionModel->lyrics.push_back(std::move(detailLyric));
  detailRegionModel->notes.push_back(std::move(detailNote));
  detailRegionModel->sortNotes();
  seam::application::EditorSession detailSession{std::move(detailProject)};
  seam::native_ui::NativeEditorController detailController{
      detailSession, detailFactory, detailRegion};
  detailController.resize(1280.0, 720.0);
  auto detailState = detailController.sceneState();
  detailState.hoveredNote = detailNoteId;
  detailState.detail = seam::native_ui::EditorDetail{
      .kind = seam::native_ui::EditorDetailKind::Note,
      .stableId = detailState.hoveredNote->toString(),
      .value = "こんにちは 안녕 你好",
  };
  const auto detailTree = seam::native_ui::EditorSemanticTree::build(
      detailState, detailController.pianoRoll());
  const auto detailId = "detail.note." + detailState.detail->stableId;
  const auto* detailNode = findNode(detailTree, detailId, findNode);
  CHECK(detailNode != nullptr);
  if (detailNode != nullptr) {
    CHECK(detailNode->value == detailState.detail->value);
    CHECK(detailNode->bounds.width > 0.0);
    CHECK(detailNode->bounds.height > 0.0);
  }

  auto unavailableState = controller.sceneState();
  unavailableState.renderStatus.hasAudibleAudio = false;
  const auto unavailableTree = seam::native_ui::EditorSemanticTree::build(
      unavailableState, controller.pianoRoll());
  const auto* transport =
      findNode(unavailableTree, "toolbar.transport", findNode);
  CHECK(transport != nullptr);
  CHECK(!transport->enabled);
  CHECK(std::find(transport->actions.begin(), transport->actions.end(),
                  seam::native_ui::SemanticAction::Activate) ==
        transport->actions.end());

  auto arrangementState = controller.sceneState();
  arrangementState.characterMode = seam::domain::CharacterDisplayMode::Off;
  const auto arrangementTree = seam::native_ui::EditorSemanticTree::build(
      arrangementState, controller.pianoRoll());
  const auto arrangement = std::find_if(
      arrangementTree.children.begin(), arrangementTree.children.end(),
      [](const auto& child) { return child.id == "arrangement.panel"; });
  CHECK(arrangement != arrangementTree.children.end());
  CHECK(seam::native_ui::EditorSemanticTree::containsId(
      arrangementTree, "inspector.route"));
  CHECK(std::all_of(
      arrangement->children.begin(), arrangement->children.end(),
      [&arrangementTree](const auto& child) {
        return child.bounds.width > 0.0 && child.bounds.height > 0.0 &&
               child.bounds.x >= 0.0 && child.bounds.y >= 0.0 &&
               child.bounds.right() <= arrangementTree.bounds.right() &&
               child.bounds.bottom() <= arrangementTree.bounds.bottom();
      }));
  const auto trackNode = std::find_if(
      arrangement->children.begin(), arrangement->children.end(),
      [](const auto& child) {
        return child.id.rfind("arrangement.track.", 0U) == 0U;
      });
  CHECK(trackNode != arrangement->children.end());
  CHECK(!trackNode->children.empty());
  CHECK(trackNode->children.front().bounds.width > 0.0);

  arrangementState.diagnostics = {seam::authoring::Diagnostic{
      .code = "RENDER_STALE",
      .severity = seam::authoring::DiagnosticSeverity::Warning,
      .messageKey = "render.stale",
      .affectedIds = {},
      .actions = seam::authoring::DiagnosticRegistry::actions("RENDER_STALE"),
      .occurrenceCount = 1U}};
  const auto diagnosticTree = seam::native_ui::EditorSemanticTree::build(
      arrangementState, controller.pianoRoll());
  const auto diagnostics = std::find_if(
      diagnosticTree.children.begin(), diagnosticTree.children.end(),
      [](const auto& child) { return child.id == "diagnostics.panel"; });
  CHECK(diagnostics != diagnosticTree.children.end());
  CHECK(diagnostics->children.size() == 2U);
  CHECK(diagnostics->children.front().bounds.width > 0.0);
  CHECK(diagnostics->children.front().bounds.height > 0.0);
  CHECK(diagnostics->children.front().value.find("render.stale") !=
        std::string::npos);
  CHECK(seam::native_ui::EditorSemanticTree::containsId(
      *diagnostics, "diagnostic-action.0.RETRY"));

  bool diagnosticActionInvoked = false;
  seam::native_ui::NativeEditorController actionController{
      session, factory, region,
      seam::native_ui::EditorHostCallbacks{
          .diagnosticAction =
              [&diagnosticActionInvoked](
                  const seam::authoring::Diagnostic&,
                  seam::authoring::DiagnosticAction action) {
                diagnosticActionInvoked =
                    action == seam::authoring::DiagnosticAction::Retry;
                return seam::core::success();
              },
      }};
  actionController.setDiagnostics(arrangementState.diagnostics);
  actionController.rebuildAccessibilityTree();
  CHECK(actionController.dispatchAccessibility(
      "diagnostic-action.0.RETRY",
      seam::native_ui::SemanticAction::Activate));
  CHECK(diagnosticActionInvoked);

  seam::authoring::VoicebankCard card;
  card.id = "demo.voice";
  card.version = "1.0.0";
  card.displayName = "Demo Voice";
  card.language = "Japanese";
  card.contentHash = "0123456789abcdef";
  card.contentHashAbbreviation = "0123456789ab";
  card.trust = seam::voicebank::VoicebankTrust::TrustedInstalled;
  card.trustLabel = "TRUSTED";
  card.selectable = true;
  card.enabledUnitCount = 12U;
  card.hasSustain = true;
  auto browserState = controller.sceneState();
  browserState.voicebankBrowserVisible = true;
  browserState.voicebankCards = {card};
  const auto browserTree = seam::native_ui::EditorSemanticTree::build(
      browserState, controller.pianoRoll());
  const auto browser = std::find_if(
      browserTree.children.begin(), browserTree.children.end(),
      [](const auto& child) { return child.id == "voicebank.panel"; });
  CHECK(browser != browserTree.children.end());
  CHECK(browser->children.size() == 1U);
  CHECK(browser->children.front().id == "voicebank.card.0");
  CHECK(browser->children.front().bounds.width > 0.0);
  CHECK(browser->children.front().bounds.height > 0.0);

  std::string selectedVoicebank;
  seam::native_ui::NativeEditorController browserController{
      session, factory, region,
      seam::native_ui::EditorHostCallbacks{
          .selectVoicebank =
              [&selectedVoicebank](std::string_view id, std::string_view,
                                   std::string_view) {
                selectedVoicebank = std::string{id};
                return seam::core::success();
              },
      }};
  browserController.setVoicebankCards({card});
  CHECK(browserController.keyDown(
      seam::native_ui::KeyEvent{.key = seam::native_ui::NativeKey::V}));
  browserController.rebuildAccessibilityTree();
  CHECK(browserController.dispatchAccessibility(
      "voicebank.card.0", seam::native_ui::SemanticAction::Activate));
  CHECK(selectedVoicebank == "demo.voice");
  CHECK(!browserController.voicebankBrowserVisible());
}

TEST_CASE("native export progress exposes an accessible cancellation action") {
  seam::application::ProjectFactory factory{760U};
  auto project = factory.createProject("Export cancellation");
  const auto track = factory.addVocalTrack(project, "Voice");
  const auto region = factory.addRegion(project, track, "Region",
                                        seam::time::Tick{0},
                                        seam::time::Tick{3840});
  auto session = seam::application::EditorSession{std::move(project)};
  bool cancelled = false;
  seam::native_ui::NativeEditorController controller{
      session, factory, region,
      seam::native_ui::EditorHostCallbacks{
          .cancelExport = [&cancelled] { cancelled = true; },
      }};
  controller.resize(1280.0, 720.0);
  controller.setExportProgress(seam::authoring::ExportProgress{
      .state = seam::authoring::ExportState::Staging,
      .currentOutput = "master.wav",
      .completedFiles = 1U,
      .totalFiles = 2U,
  });
  controller.rebuildAccessibilityTree();

  const auto& root = controller.accessibilityTree().root();
  const auto progress = std::find_if(
      root.children.begin(), root.children.end(), [](const auto& node) {
        return node.id == "export.progress";
      });
  CHECK(progress != root.children.end());
  const auto cancel = std::find_if(
      progress->children.begin(), progress->children.end(), [](const auto& node) {
        return node.id == "export.cancel";
      });
  CHECK(cancel != progress->children.end());
  CHECK(cancel->enabled);
  CHECK(controller.dispatchAccessibility(
      "export.cancel", seam::native_ui::SemanticAction::Activate));
  CHECK(cancelled);

  cancelled = false;
  const seam::native_ui::EditorSceneLayout layout;
  const auto cancelBounds = layout.exportCancelBounds(1280.0, 720.0);
  CHECK(cancelBounds.width > 0.0);
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{cancelBounds.x + cancelBounds.width * 0.5,
                                  cancelBounds.y + cancelBounds.height * 0.5},
      .button = seam::native_ui::PointerButton::Left,
  }));
  CHECK(cancelled);
}

TEST_CASE("native accessibility accepts focus on every semantic node") {
  seam::application::ProjectFactory factory{900U};
  auto project = factory.createProject("Semantic focus");
  const auto track = factory.addVocalTrack(project, "Voice");
  const auto region = factory.addRegion(project, track, "Region",
                                        seam::time::Tick{0},
                                        seam::time::Tick{3840});
  auto session = seam::application::EditorSession{std::move(project)};
  seam::native_ui::NativeEditorController controller{session, factory, region};
  controller.resize(1280.0, 720.0);
  controller.rebuildAccessibilityTree();

  const auto findNode = [](const seam::native_ui::SemanticNode& root,
                           std::string_view id,
                           const auto& self)
      -> const seam::native_ui::SemanticNode* {
    if (root.id == id) return &root;
    for (const auto& child : root.children) {
      if (const auto* match = self(child, id, self); match != nullptr) {
        return match;
      }
    }
    return nullptr;
  };
  const auto countFocused = [](const seam::native_ui::SemanticNode& root,
                               const auto& self) -> std::size_t {
    const auto own = root.focused ? std::size_t{1U} : std::size_t{0U};
    return own + std::accumulate(
                     root.children.begin(), root.children.end(),
                     std::size_t{0U},
                     [&self](std::size_t count,
                             const seam::native_ui::SemanticNode& child) {
                       return count + self(child, self);
                     });
  };
  std::vector<std::string> focusable;
  const auto collect = [&focusable](const seam::native_ui::SemanticNode& node,
                                    const auto& self) -> void {
    if (std::find(node.actions.begin(), node.actions.end(),
                  seam::native_ui::SemanticAction::SetFocus) !=
        node.actions.end()) {
      focusable.push_back(node.id);
    }
    for (const auto& child : node.children) self(child, self);
  };
  collect(controller.accessibilityTree().root(), collect);
  CHECK(!focusable.empty());
  for (std::size_t index = 0U; index < focusable.size(); ++index) {
    const auto& id = focusable[index];
    CHECK(controller.dispatchAccessibility(
        id, seam::native_ui::SemanticAction::SetFocus));
    if (index == 0U) controller.rebuildAccessibilityTree();
    const auto* focused = findNode(controller.accessibilityTree().root(), id,
                                   findNode);
    CHECK(focused != nullptr);
    CHECK(focused->focused);
    CHECK(countFocused(controller.accessibilityTree().root(), countFocused) ==
          1U);
  }

  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::Tab,
      .modifiers = {},
      .repeat = false,
  }));
  const auto* firstTabStop = controller.accessibilityTree().focusedNode();
  CHECK(firstTabStop != nullptr);
  const auto firstTabStopId = firstTabStop->id;
  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::Tab,
      .modifiers = {},
      .repeat = false,
  }));
  const auto* secondTabStop = controller.accessibilityTree().focusedNode();
  CHECK(secondTabStop != nullptr);
  CHECK(secondTabStop->id != firstTabStopId);
  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::Tab,
      .modifiers = {.shift = true},
      .repeat = false,
  }));
  CHECK(controller.accessibilityTree().focusedNode() != nullptr);
  CHECK(controller.accessibilityTree().focusedNode()->id == firstTabStopId);
}

TEST_CASE("design fixture semantic bounds remain contained at every target viewport") {
  seam::test::native_ui_design::Fixture fixture;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  const auto contained = [](const seam::native_ui::SemanticNode& node,
                            const seam::ui::Rect& root,
                            const auto& self) -> bool {
    const auto valid = node.bounds.x >= root.x && node.bounds.y >= root.y &&
                       node.bounds.right() <= root.right() &&
                       node.bounds.bottom() <= root.bottom() &&
                       node.bounds.width > 0.0 && node.bounds.height > 0.0;
    if (node.id != "editor" && !valid) return false;
    return std::all_of(node.children.begin(), node.children.end(),
                       [&root, &self](const auto& child) {
                         return self(child, root, self);
                       });
  };

  for (const auto& viewport : seam::test::native_ui_design::kTargetViewports) {
    controller.resize(static_cast<double>(viewport.width),
                      static_cast<double>(viewport.height));
    const auto state = controller.sceneState();
    const auto tree = seam::native_ui::EditorSemanticTree::build(
        state, controller.pianoRoll());
    CHECK(tree.bounds.width == static_cast<double>(viewport.width));
    CHECK(tree.bounds.height == static_cast<double>(viewport.height));
    CHECK(contained(tree, tree.bounds, contained));
  }
}
