#include "test_framework.hpp"
#include "test_support.hpp"
#include "native_ui_design_fixture.hpp"
#include "seam/native_ui/appkit_shortcut_key.hpp"

TEST_CASE("AppKit non-Latin shortcuts preserve controls without overriding ASCII layouts") {
  using namespace seam::native_ui;
  CHECK(appKitNonLatinShortcutKey(0x00U, U'ㅁ') == NativeKey::A);
  CHECK(appKitNonLatinShortcutKey(0x23U, U'ㅖ') == NativeKey::P);
  CHECK(appKitNonLatinShortcutKey(0x22U, U'ㅑ') == NativeKey::I);
  CHECK(appKitNonLatinShortcutKey(0x0BU, U'ㅠ') == NativeKey::B);
  CHECK(appKitNonLatinShortcutKey(0x01U, U'ㄴ') == NativeKey::S);
  CHECK(appKitNonLatinShortcutKey(0x06U, U'ㅋ') == NativeKey::Z);
  CHECK(appKitNonLatinShortcutKey(0x23U, U'p') == NativeKey::Unknown);
  CHECK(appKitNonLatinShortcutKey(0x23U, U'A') == NativeKey::Unknown);
  CHECK(appKitNonLatinShortcutKey(0x23U, U'\0') == NativeKey::Unknown);
  CHECK(appKitNonLatinShortcutKey(0xffffU, U'한') == NativeKey::Unknown);
}

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/native_ui/character_presentation.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_frame_layout.hpp"
#include "seam/native_ui/editor_interaction_state.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/native_window.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/native_ui/voicebank_studio.hpp"
#include "seam/native_ui/voice_identity.hpp"
#include "seam/native_ui/diagnostic_presentation.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/text/text_engine.hpp"
#include "seam/text/unicode.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/repository.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <thread>

namespace {

struct NativeUiFixture final {
  seam::application::ProjectFactory factory{7000U};
  seam::domain::RegionId regionId{};
  seam::domain::NoteId noteId{};
  seam::domain::LyricTokenId lyricId{};
  seam::application::EditorSession session;

  NativeUiFixture() : session(makeProject()) {}

  seam::domain::Project makeProject() {
    auto project = factory.createProject("Native UI");
    const auto trackId = factory.addVocalTrack(project, "Voice");
    regionId = factory.addRegion(project, trackId, "Region",
                                 seam::time::Tick{0}, seam::time::Tick{7680});
    auto [lyric, note] = factory.makeNote(
        seam::time::Tick{960}, seam::time::Tick{960}, 64U, U"edge",
        seam::domain::Language::English);
    noteId = note.id;
    lyricId = lyric.id;
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    region->sortNotes();
    return project;
  }
};

void applyReadyLyricReview(seam::native_ui::NativeEditorController& controller) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (std::chrono::steady_clock::now() < deadline) {
    controller.pollReplacementReview();
    const auto view = controller.sceneState().replacementReview;
    if (view.enabled[3]) { CHECK(controller.replacementReviewAction(3U)); return; }
    CHECK(view.status.starts_with("Preparing"));
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  throw seam::test::Failure{"Lyric review did not become ready"};
}

}  // namespace

TEST_CASE("native window minimum stays logical at scaled surfaces") {
  const seam::native_ui::NativeWindowConfig config{
      .width = 960U,
      .height = 640U,
      .scale = 2.0,
      .minimumWidth = 480U,
      .minimumHeight = 320U,
  };
  CHECK(seam::native_ui::nativeWindowMinimumPhysicalWidth(config) == 960U);
  CHECK(seam::native_ui::nativeWindowMinimumPhysicalHeight(config) == 640U);
  CHECK(seam::native_ui::nativeWindowConfigSizeIsValid(config));

  const auto tooNarrow = seam::native_ui::NativeWindowConfig{
      .width = 959U,
      .height = 640U,
      .scale = 2.0,
      .minimumWidth = 480U,
      .minimumHeight = 320U,
  };
  CHECK(!seam::native_ui::nativeWindowConfigSizeIsValid(tooNarrow));
}

TEST_CASE("native phoneme inspection shares resolver tokens and bounded failures") {
  NativeUiFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  region->lyrics.front().surface = U"か";
  region->lyrics.front().language = seam::domain::Language::Japanese;
  region->phonemeOverrides = {{.key = {fixture.noteId, 1U},
      .timing = {.startOffset = -1100}, .locked = true}};
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId, {}};
  const auto resolved = seam::phonemizer::resolveJapanesePronunciation(*region);
  CHECK(resolved);
  const auto scene = controller.sceneState();
  CHECK(scene.phonemes.tokens == resolved.value().pronunciation.tokens);
  CHECK(scene.phonemes.warnings == resolved.value().pronunciation.warnings);
  if (const auto* output = std::getenv("SEAM_TIMING_LANE_CAPTURE")) {
    controller.resize(960.0, 720.0);
    seam::native_ui::PixelSurface surface{960U, 720U};
    seam::native_ui::RasterCanvas canvas{surface, 1.0};
    seam::native_ui::EditorScenePainter painter;
    painter.paint(canvas, controller.pianoRoll(), controller.sceneState());
    CHECK(surface.writePpm(output));
  }
  region->lyrics.front().surface = std::u32string(4097U, U'あ');
  const auto bounded = controller.sceneState();
  CHECK(bounded.phonemes.tokens.empty());
  CHECK(bounded.phonemes.warnings.size() == 1U);
  CHECK(bounded.phonemes.warnings.front().code == seam::phonemizer::WarningCode::ResolutionFailure);
  CHECK(!bounded.phonemes.warnings.front().message.empty());
}

TEST_CASE("native retained phoneme review selects explicitly applies and closes accessibly") {
  NativeUiFixture fixture;
  auto project = fixture.session.project();
  auto* region = project.findRegion(fixture.regionId);
  region->lyrics.front().surface = U"こ";
  region->lyrics.front().language = seam::domain::Language::Japanese;
  region->phonemeOverrides = {{.key = {fixture.noteId, 0U}, .timing = {.startOffset = -2300},
                               .locked = true, .unresolved = true}};
  seam::authoring::ProjectDocument document{project, seam::application::ProjectFactory{9000U}};
  std::size_t edits = 0U;
  seam::authoring::TechnicalEditController technical{document, fixture.regionId, {}, [&] { ++edits; }};
  seam::native_ui::NativeEditorController controller{document.session(), document.factory(), fixture.regionId,
      {.reviewPhonemeBindings = [&] { return technical.reviewPhonemeBindings(); },
       .rebindPhonemeOverride = [&](const auto& edit, auto target, auto context) {
         return technical.rebindPhonemeOverride(edit, target, context);
       }}};
  controller.resize(480.0, 320.0);
  const seam::native_ui::EditorSceneLayout layout;
  const auto open = layout.phonemeReviewOpenBounds(480.0, 320.0);
  CHECK(controller.pointerDown({.position = {open.x + 5.0, open.y + 5.0}, .button = seam::native_ui::PointerButton::Left}));
  CHECK(controller.sceneState().phonemeReview.visible);
  CHECK(!controller.sceneState().phonemeReview.enabled[5U]);
  CHECK(controller.keyDown({.key = seam::native_ui::NativeKey::Tab}));
  CHECK(controller.accessibilityTree().virtualizedNoteCount() == 0U);
  CHECK(controller.accessibilityTree().materializeNotes(0U, 100U).empty());
  CHECK(controller.accessibilityTree().focusedNode() != nullptr);
  CHECK(controller.accessibilityTree().focusedNode()->enabled);
  CHECK(controller.accessibilityTree().focusedNode()->id == "phoneme.review.action.2");
  CHECK(!controller.dispatchAccessibility("editor", seam::native_ui::SemanticAction::Activate));
  CHECK(edits == 0U);
  const auto panel = layout.phonemeReviewPanelBounds(480.0, 320.0);
  for (std::size_t i = 0U; i < 6U; ++i) {
    const auto button = layout.phonemeReviewButtonBounds(480.0, 320.0, i);
    CHECK(button.x >= panel.x);
    CHECK(button.right() <= panel.right());
    CHECK(button.bottom() <= panel.bottom());
  }
  CHECK(controller.dispatchAccessibility("phoneme.review.action.4", seam::native_ui::SemanticAction::Activate));
  CHECK(controller.dispatchAccessibility("phoneme.review.action.4", seam::native_ui::SemanticAction::Activate));
  CHECK(controller.sceneState().phonemeReview.enabled[5U]);
  seam::native_ui::PixelSurface surface{480U, 320U};
  seam::native_ui::RasterCanvas canvas{surface, 1.0};
  seam::native_ui::EditorScenePainter painter;
  painter.paint(canvas, controller.pianoRoll(), controller.sceneState());
  if (const auto* output = std::getenv("SEAM_PHONEME_REVIEW_CAPTURE")) CHECK(surface.writePpm(output));
  CHECK(controller.dispatchAccessibility("phoneme.review.action.5", seam::native_ui::SemanticAction::SetFocus));
  CHECK(controller.keyDown({.key = seam::native_ui::NativeKey::Enter}));
  CHECK(edits == 1U);
  CHECK(document.session().revision() == 1U);
  CHECK(document.session().project().findRegion(fixture.regionId)->findPhonemeOverride({fixture.noteId, 1U})->locked);
  CHECK(controller.keyDown({.key = seam::native_ui::NativeKey::Escape}));
  CHECK(!controller.sceneState().phonemeReview.visible);
  CHECK(technical.undo());
  CHECK(document.session().project() == project);
  CHECK(controller.openPhonemeReview());
  CHECK(controller.activatePhonemeReview(4U));
  CHECK(controller.activatePhonemeReview(4U));
  document.session().project().findRegion(fixture.regionId)->lyrics.front().surface = U"か";
  const auto staleProject = document.session().project();
  const auto staleRevision = document.session().revision();
  CHECK(!controller.activatePhonemeReview(5U));
  CHECK(controller.sceneState().phonemeReview.visible);
  CHECK(controller.sceneState().phonemeReview.status.find("changed") != std::string::npos);
  CHECK(document.session().project() == staleProject);
  CHECK(document.session().revision() == staleRevision);
  CHECK(edits == 2U);
  CHECK(controller.activatePhonemeReview(2U));
}

TEST_CASE("embedded dynamics inspector receives complete drag gestures without leaking technical edits") {
  using namespace seam; NativeUiFixture fixture;
  auto project = fixture.session.project();
  CHECK(project.findRegion(fixture.regionId)->dynamicsAutomation.upsert({time::Tick{0}, 1.0F}));
  const auto root = test::support::temporaryDirectory("embedded-dynamics-curve");
  clap_editor::EditorRuntime runtime{project, {}, {{root / "absent-bank", voicebank::VoicebankRootKind::Installed}}};
  runtime.controller().resize(480.0, 320.0); CHECK(runtime.controller().openDynamicsInspector());
  const auto before = runtime.projectCopy();
  const auto originalState = clap_editor::encodeEditorState(before); CHECK(originalState);
  const auto plot = runtime.controller().sceneState().replacementReview.dynamicsPlot; CHECK(plot);
  runtime.pointerDown({.position = plot->handles[0].position, .button = native_ui::PointerButton::Left});
  runtime.pointerMove({.position = {plot->bounds.x, plot->bounds.bottom()}, .button = native_ui::PointerButton::Left});
  CHECK(runtime.controller().sceneState().replacementReview.rows[1] == "Linear gain: 0");
  runtime.pointerUp({.position = {plot->bounds.x, plot->bounds.y}, .button = native_ui::PointerButton::Left});
  CHECK(runtime.controller().sceneState().replacementReview.rows[1] != "Linear gain: 0");
  CHECK(runtime.projectCopy() == before);
  CHECK(runtime.controller().replacementReviewAction(3U)); CHECK(runtime.projectCopy() == before);
  const auto stagedState = clap_editor::encodeEditorState(runtime.projectCopy()); CHECK(stagedState);
  CHECK(stagedState.value() == originalState.value());
  CHECK(runtime.controller().replacementReviewAction(3U));
  auto expected = before;
  CHECK(expected.findRegion(fixture.regionId)->dynamicsAutomation.upsert({time::Tick{0}, domain::kMaximumDynamicsGain}));
  CHECK(runtime.projectCopy() == expected); CHECK(runtime.controller().sceneState().dirty);
  const auto appliedState = clap_editor::encodeEditorState(runtime.projectCopy()); CHECK(appliedState);
  CHECK(appliedState.value() != originalState.value());
  const auto decoded = clap_editor::decodeEditorState(appliedState.value()); CHECK(decoded); CHECK(decoded.value() == expected);
  clap_editor::EditorRuntime restored{decoded.value(), {}, {{root / "absent-bank", voicebank::VoicebankRootKind::Installed}}};
  CHECK(restored.projectCopy() == expected); CHECK(restored.controller().openDynamicsInspector());
  CHECK(restored.controller().sceneState().replacementReview.rows[0].find("3.981") != std::string::npos);
}

TEST_CASE("embedded editor review callback commits retained edits and dirty state") {
  NativeUiFixture fixture;
  auto project = fixture.session.project();
  auto* region = project.findRegion(fixture.regionId);
  region->lyrics.front().surface = U"こ";
  region->lyrics.front().language = seam::domain::Language::Japanese;
  region->phonemeOverrides = {{.key = {fixture.noteId, 0U}, .locked = true, .unresolved = true}};
  const auto root = seam::test::support::temporaryDirectory("embedded-phoneme-review");
  seam::clap_editor::EditorRuntime runtime{project, {},
      {{root / "absent-bank", seam::voicebank::VoicebankRootKind::Installed}}};
  CHECK(runtime.dispatchAccessibility("phoneme.review.open", seam::native_ui::SemanticAction::Activate));
  CHECK(runtime.dispatchAccessibility("phoneme.review.action.4", seam::native_ui::SemanticAction::Activate));
  CHECK(runtime.dispatchAccessibility("phoneme.review.action.4", seam::native_ui::SemanticAction::Activate));
  CHECK(runtime.dispatchAccessibility("phoneme.review.action.5", seam::native_ui::SemanticAction::Activate));
  const auto changed = runtime.projectCopy();
  const auto* edit = changed.findRegion(fixture.regionId)->findPhonemeOverride({fixture.noteId, 1U});
  CHECK(edit != nullptr);
  CHECK(edit->sourceContextId.has_value());
  CHECK(!edit->unresolved);
  CHECK(runtime.controller().sceneState().dirty);
}

TEST_CASE("embedded retained review commits unit and seam callbacks") {
  for (bool unit : {true, false}) {
    NativeUiFixture fixture;
    auto project = fixture.session.project();
    auto* region = project.findRegion(fixture.regionId);
    region->lyrics.front().surface = U"こ";
    region->lyrics.front().language = seam::domain::Language::Japanese;
    if (unit) region->unitSelectionOverrides = {{.startKey = {fixture.noteId, 0U}, .unitId = "saved", .unresolved = true}};
    else region->seamOverrides = {{.incomingStartKey = {fixture.noteId, 0U}, .seamAmount = 0.5F, .unresolved = true}};
    const auto root = seam::test::support::temporaryDirectory("embedded-render-review");
    seam::clap_editor::EditorRuntime runtime{project, {},
        {{root / "absent-bank", seam::voicebank::VoicebankRootKind::Installed}}};
    CHECK(runtime.dispatchAccessibility("phoneme.review.open", seam::native_ui::SemanticAction::Activate));
    CHECK(runtime.dispatchAccessibility("phoneme.review.action.4", seam::native_ui::SemanticAction::Activate));
    CHECK(runtime.dispatchAccessibility("phoneme.review.action.4", seam::native_ui::SemanticAction::Activate));
    CHECK(runtime.dispatchAccessibility("phoneme.review.action.5", seam::native_ui::SemanticAction::Activate));
    const auto changed = runtime.projectCopy();
    const auto* updated = changed.findRegion(fixture.regionId);
    if (unit) {
      CHECK(updated->findUnitSelectionOverride({fixture.noteId, 1U}) != nullptr);
      CHECK(!updated->findUnitSelectionOverride({fixture.noteId, 1U})->unresolved);
    } else {
      CHECK(updated->findSeamOverride({fixture.noteId, 1U}) != nullptr);
      CHECK(!updated->findSeamOverride({fixture.noteId, 1U})->unresolved);
    }
    CHECK(runtime.controller().sceneState().dirty);
  }
}

TEST_CASE("native retained review applies unit and seam edits through the shared panel") {
  NativeUiFixture fixture;
  auto project = fixture.session.project();
  auto* region = project.findRegion(fixture.regionId);
  region->lyrics.front().surface = U"こ";
  region->lyrics.front().language = seam::domain::Language::Japanese;
  region->unitSelectionOverrides = {{.startKey = {fixture.noteId, 0U}, .unitId = "saved-unit", .unresolved = true}};
  region->seamOverrides = {{.incomingStartKey = {fixture.noteId, 0U}, .seamAmount = 0.4F, .unresolved = true}};
  seam::authoring::ProjectDocument document{project, seam::application::ProjectFactory{9000U}};
  std::size_t edits = 0U;
  seam::authoring::TechnicalEditController technical{document, fixture.regionId, {}, [&] { ++edits; }};
  seam::native_ui::NativeEditorController controller{document.session(), document.factory(), fixture.regionId,
      {.reviewPhonemeBindings = [&] { return technical.reviewPhonemeBindings(); },
       .rebindPhonemeOverride = [&](const auto& edit, auto target, auto context) { return technical.rebindPhonemeOverride(edit, target, context); },
       .reviewRenderEdits = [&] { return technical.reviewRetainedRenderEdits(); },
       .rebindUnitOverride = [&](const auto& review, const auto& edit, auto target) { return technical.rebindUnitOverride(review, edit, target); },
       .rebindSeamOverride = [&](const auto& review, const auto& edit, auto target) { return technical.rebindSeamOverride(review, edit, target); }}};
  controller.resize(480.0, 320.0);
  CHECK(controller.openPhonemeReview());
  CHECK(controller.sceneState().phonemeReview.source.find("Unit saved-unit") != std::string::npos);
  if (const auto* output = std::getenv("SEAM_RENDER_REVIEW_CAPTURE")) {
    seam::native_ui::PixelSurface surface{480U, 320U};
    seam::native_ui::RasterCanvas canvas{surface, 1.0};
    seam::native_ui::EditorScenePainter painter;
    painter.paint(canvas, controller.pianoRoll(), controller.sceneState());
    CHECK(surface.writePpm(output));
  }
  CHECK(!controller.sceneState().phonemeReview.enabled[5U]);
  CHECK(controller.activatePhonemeReview(1U));
  CHECK(controller.sceneState().phonemeReview.source.starts_with("Seam"));
  CHECK(controller.activatePhonemeReview(4U));
  CHECK(controller.activatePhonemeReview(0U));
  CHECK(!controller.sceneState().phonemeReview.enabled[5U]);
  CHECK(controller.activatePhonemeReview(4U));
  CHECK(controller.activatePhonemeReview(4U));
  CHECK(controller.dispatchAccessibility("phoneme.review.action.5", seam::native_ui::SemanticAction::Activate));
  CHECK(edits == 1U);
  CHECK(controller.sceneState().phonemeReview.source.starts_with("Seam"));
  CHECK(!controller.sceneState().phonemeReview.enabled[5U]);
  CHECK(controller.activatePhonemeReview(4U));
  CHECK(controller.activatePhonemeReview(4U));
  CHECK(controller.activatePhonemeReview(5U));
  CHECK(edits == 2U);
  CHECK(controller.sceneState().phonemeReview.source == "No retained edits");
  CHECK(controller.activatePhonemeReview(2U));
  CHECK(technical.undo());
  CHECK(technical.undo());
  CHECK(document.session().project() == project);
  CHECK(controller.openPhonemeReview());
  CHECK(controller.activatePhonemeReview(4U));
  document.session().project().findRegion(fixture.regionId)->lyrics.front().surface = U"さ";
  const auto changed = document.session().project();
  const auto revision = document.session().revision();
  CHECK(!controller.activatePhonemeReview(5U));
  CHECK(controller.sceneState().phonemeReview.status.find("changed") != std::string::npos);
  CHECK(document.session().project() == changed);
  CHECK(document.session().revision() == revision);
}

TEST_CASE("opening phoneme review preserves pending text composition") {
  NativeUiFixture fixture;
  std::size_t reviews = 0U;
  seam::native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.reviewPhonemeBindings = [&] {
         ++reviews;
         return seam::core::success(seam::authoring::PhonemeBindingReview{.regionId = fixture.regionId});
       },
       .rebindPhonemeOverride = [](const auto&, auto, auto) { return seam::core::success(); }}};
  CHECK(controller.beginLyricEdit(fixture.noteId));
  CHECK(controller.updateTextComposition(U"pending lyric", {}));
  const auto before = fixture.session.project();
  const auto preview = controller.sceneState().compositionPreview;
  CHECK(!controller.openPhonemeReview());
  CHECK(reviews == 0U);
  CHECK(controller.textInputActive());
  CHECK(controller.sceneState().compositionPreview == preview);
  CHECK(fixture.session.project() == before);
  controller.cancelTextComposition();
  CHECK(controller.openPhonemeReview());
  CHECK(reviews == 1U);
  const auto hovered = controller.sceneState().hoveredNote;
  CHECK(controller.pointerMove({.position = {200.0, 160.0}}));
  CHECK(controller.pointerUp({.position = {200.0, 160.0}, .button = seam::native_ui::PointerButton::Left}));
  CHECK(controller.sceneState().hoveredNote == hovered);
  CHECK(fixture.session.project() == before);
  CHECK(fixture.session.revision() == 0U);
}

TEST_CASE("native window screenshots honor the requested capture dimensions") {
  const seam::native_ui::NativeWindowConfig interactive{};
  CHECK(seam::native_ui::nativeWindowShouldRestoreSavedFrame(interactive));

  const seam::native_ui::NativeWindowConfig capture{
      .screenshotPath = std::filesystem::path{"capture.ppm"},
  };
  CHECK(!seam::native_ui::nativeWindowShouldRestoreSavedFrame(capture));
  const seam::native_ui::NativeWindowConfig explicitSize{.restoreSavedFrame=false};
  CHECK(!seam::native_ui::nativeWindowShouldRestoreSavedFrame(explicitSize));
}

TEST_CASE("voicebank production rail hit testing follows painted row spacing") {
  const auto production = seam::native_ui::voicebankStudioUnitRailIndexAt(
      397.0, 0U, 144U, 520.0, true);
  CHECK(production.has_value());
  CHECK(*production == 8U);
  CHECK(!seam::native_ui::voicebankStudioUnitRailIndexAt(
      141.0, 0U, 144U, 520.0, true));
  CHECK(!seam::native_ui::voicebankStudioUnitRailIndexAt(
      108.0 + 11.0 * 36.0, 0U, 144U, 520.0, true));
  CHECK(seam::native_ui::voicebankStudioUnitRailVisibleRows(520.0, true) ==
        11U);

  const auto manifest = seam::native_ui::voicebankStudioUnitRailIndexAt(
      397.0, 0U, 144U, 520.0, false);
  CHECK(manifest.has_value());
  CHECK(*manifest == 9U);
  CHECK(!seam::native_ui::voicebankStudioUnitRailIndexAt(
      108.0 + 13.0 * 32.0, 0U, 144U, 520.0, false));
  CHECK(seam::native_ui::voicebankStudioUnitRailVisibleRows(520.0, false) ==
        13U);
}

TEST_CASE("voicebank recording names stay portable direct children") {
  const auto root = seam::test::support::temporaryDirectory(
      "voicebank-recording-path");
  const auto recordings = root / "recordings";
  CHECK(std::filesystem::create_directories(recordings));
  for (const auto unitId : {std::string_view{"../outside"},
                            std::string_view{"..\\outside"},
                            std::string_view{"C:\\temp\\take"},
                            std::string_view{"CON"},
                            std::string_view{"name. "},
                            std::string_view{"/absolute"}}) {
    const auto path = seam::native_ui::nextVoicebankRecordingPath(
        recordings, unitId);
    CHECK(path);
    CHECK(path.value().parent_path() == recordings);
    CHECK(path.value().filename() == path.value().lexically_relative(recordings));
    const auto filename = path.value().filename().string();
    CHECK(filename.find('/') == std::string::npos);
    CHECK(filename.find('\\') == std::string::npos);
    CHECK(filename.find(':') == std::string::npos);
    CHECK(!filename.ends_with('.'));
    CHECK(!filename.ends_with(' '));
    CHECK(filename != "CON-take-0001.wav");
  }
}

TEST_CASE("voice identity suppresses mismatched character presentation") {
  const seam::domain::VoicebankReference reference{
      .id = "voice.test", .version = "1.0.0", .contentHash = std::string(64U, 'a')};
  seam::authoring::VoicebankCard card{
      .id = reference.id,
      .version = reference.version,
      .displayName = "Test Voice",
      .contentHash = reference.contentHash,
      .selectable = true,
      .characterAvailable = true,
      .characterId = "character.test",
      .characterVersion = "1.0.0",
  };
  seam::native_ui::VoiceIdentityInput::CharacterBinding character{
      .id = "character.test",
      .version = "1.0.0",
      .voicebankId = "voice.test",
      .accentPrimary = "#8B4C69",
      .accentSecondary = "#6E5A86",
  };
  const auto ready = seam::native_ui::resolveVoiceIdentity({
      .reference = reference, .card = &card, .character = &character});
  CHECK(ready.state == seam::native_ui::VoiceIdentityState::Ready);
  CHECK(ready.characterActive);
  card.contentHash = std::string(64U, 'b');
  const auto missing = seam::native_ui::resolveVoiceIdentity({
      .reference = reference, .card = &card, .character = &character});
  CHECK(missing.state == seam::native_ui::VoiceIdentityState::Missing);
  CHECK(!missing.characterActive);
  card.contentHash = reference.contentHash;
  const auto completed = seam::native_ui::resolveVoiceIdentity({
      .reference = reference,
      .card = &card,
      .character = &character,
      .renderStatus = {.state = seam::native_ui::RenderStatusState::Ready},
      .completeDwell = true,
  });
  CHECK(completed.state == seam::native_ui::VoiceIdentityState::Complete);
}

TEST_CASE("diagnostic presentation keeps recovery copy separate from stable codes") {
  const auto presentation = seam::native_ui::presentDiagnostic({
      .code = "BANK_MISSING",
      .severity = seam::authoring::DiagnosticSeverity::Error,
      .messageKey = "voicebank.missing",
      .actions = {seam::authoring::DiagnosticAction::ChooseVoicebank,
                  seam::authoring::DiagnosticAction::RelinkVoicebank,
                  seam::authoring::DiagnosticAction::CopyDiagnostic},
  });
  CHECK(presentation.title == "Voicebank needs attention");
  CHECK(presentation.primaryActions.size() == 2U);
  CHECK(presentation.primaryActionKinds[0U] ==
        seam::authoring::DiagnosticAction::ChooseVoicebank);
  CHECK(presentation.primaryActionKinds[1U] ==
        seam::authoring::DiagnosticAction::RelinkVoicebank);
  CHECK(presentation.technicalDetail == "BANK_MISSING / voicebank.missing");
}

TEST_CASE("diagnostic presentation keeps support reachable when it is the recovery") {
  const auto presentation = seam::native_ui::presentDiagnostic({
      .code = "PROJECT_NOT_FOUND",
      .severity = seam::authoring::DiagnosticSeverity::Error,
      .messageKey = "project.not-found",
      .actions = {seam::authoring::DiagnosticAction::OpenSupport,
                  seam::authoring::DiagnosticAction::CopyDiagnostic},
  });
  CHECK(presentation.primaryActions.size() == 1U);
  CHECK(presentation.primaryActionKinds.front() ==
        seam::authoring::DiagnosticAction::OpenSupport);
  CHECK(presentation.primaryActions.front() == "Get support");

  const auto preview = seam::native_ui::presentDiagnostic({
      .code = "SUPPORT_BUNDLE_PREVIEW_READY",
      .severity = seam::authoring::DiagnosticSeverity::Info,
      .messageKey = "support.preview-ready",
      .actions = {seam::authoring::DiagnosticAction::ExportSupportBundle,
                  seam::authoring::DiagnosticAction::Dismiss},
  });
  CHECK(preview.primaryActionKinds.size() == 2U);
  CHECK(preview.primaryActionKinds[0U] ==
        seam::authoring::DiagnosticAction::ExportSupportBundle);
  CHECK(preview.primaryActionKinds[1U] ==
        seam::authoring::DiagnosticAction::Dismiss);
  CHECK(preview.primaryActions[0U] == "Export");
  CHECK(preview.primaryActions[1U] == "Dismiss");
}

TEST_CASE("editor interaction state owns hovered note detail independently") {
  seam::native_ui::EditorInteractionState interaction;
  const seam::domain::NoteId noteId{42U};
  CHECK(!interaction.hoveredNote().has_value());
  CHECK(!interaction.detail().has_value());
  CHECK(interaction.updateHoveredNote(noteId, "こんにちは 안녕 你好"));
  CHECK(interaction.hoveredNote() == noteId);
  CHECK(interaction.detail().has_value());
  CHECK(interaction.detail()->kind == seam::native_ui::EditorDetailKind::Note);
  CHECK(interaction.detail()->stableId == noteId.toString());
  CHECK(interaction.detail()->value == "こんにちは 안녕 你好");
  CHECK(!interaction.updateHoveredNote(noteId, "こんにちは 안녕 你好"));
  const seam::domain::NoteId focusedNoteId{84U};
  CHECK(interaction.updateFocusedNote(focusedNoteId, "focused lyric"));
  CHECK(interaction.focusedNote() == focusedNoteId);
  CHECK(interaction.detail()->stableId == noteId.toString());
  CHECK(interaction.clearHover());
  CHECK(!interaction.hoveredNote().has_value());
  CHECK(interaction.detail().has_value());
  CHECK(interaction.detail()->stableId == focusedNoteId.toString());
  CHECK(!interaction.clearHover());
  CHECK(interaction.clearFocus());
  CHECK(!interaction.focusedNote().has_value());
  CHECK(!interaction.detail().has_value());
  CHECK(!interaction.clearFocus());
}

TEST_CASE("technical lane geometry scales one shared frame") {
  const seam::native_ui::EditorSceneLayout layout;
  const auto normal = layout.technicalLaneGeometry(720.0);
  CHECK_NEAR(normal.phonemeTop, normal.pianoBottom, 1e-9);
  CHECK_NEAR(normal.unitTop, normal.phonemeTop + normal.phonemeHeight, 1e-9);
  CHECK_NEAR(normal.seamTop, normal.unitTop + normal.unitHeight, 1e-9);
  CHECK_NEAR(normal.pitchTop, normal.seamTop + normal.seamHeight, 1e-9);
  CHECK_NEAR(normal.pitchTop + normal.pitchHeight, normal.bottom, 1e-9);

  const auto compact = layout.technicalLaneGeometry(320.0);
  CHECK(compact.phonemeHeight < layout.phonemeLaneHeight);
  CHECK(compact.unitHeight < layout.unitLaneHeight);
  CHECK(compact.seamHeight < layout.seamLaneHeight);
  CHECK(compact.pitchHeight < layout.automationLaneHeight);
  CHECK_NEAR(compact.pitchTop + compact.pitchHeight, compact.bottom, 1e-9);
  CHECK(layout.unitInstructionVisible(normal.unitHeight));
  CHECK(!layout.unitInstructionVisible(compact.unitHeight));
  CHECK(layout.seamInstructionVisible(normal.seamHeight));
  CHECK(!layout.seamInstructionVisible(compact.seamHeight));
  CHECK(layout.unitCardBottomPaddingForHeight(compact.unitHeight) <
        layout.unitCardBottomPadding);
  CHECK(layout.seamRailBottomPaddingForHeight(compact.seamHeight) <
        layout.seamRailBottomPadding);
}

TEST_CASE("adaptive empty technical lanes preserve piano roll dominance") {
  const auto lanes = seam::native_ui::resolveTechnicalLaneHeights(
      seam::native_ui::TechnicalLaneLayoutInput{
          .presentation = {},
          .populated = {false, false, false, false},
          .previewHeights = {42.0, 60.0, 54.0, 72.0},
          .contentTop = 98.0,
          .contentBottom = 772.0,
      });
  for (const auto height : lanes.values) CHECK(height <= 30.0);
  const auto pianoHeight = lanes.pianoBottom - 98.0;
  CHECK(pianoHeight >= (772.0 - 98.0) * 0.65);
}

TEST_CASE("diagnostic recovery controls share visible layout and hit targets") {
  const seam::native_ui::EditorSceneLayout layout;
  const auto panel = layout.diagnosticBounds(1187.0, 768.0, false);
  CHECK(panel.height >= 56.0);
  const auto first = layout.diagnosticActionBounds(1187.0, 768.0, false, 2U, 0U);
  const auto second = layout.diagnosticActionBounds(1187.0, 768.0, false, 2U, 1U);
  CHECK(first.height >= 24.0);
  CHECK(second.height >= 24.0);
  CHECK(first.right() <= second.x);
  CHECK(second.right() <= panel.right() - layout.diagnosticTextInsetX + 1e-9);
  CHECK(first.y >= panel.y);
  CHECK(first.bottom() <= panel.bottom());
}

TEST_CASE("support panel exposes exact preview and selectable report list") {
  NativeUiFixture fixture;
  std::optional<std::size_t> selectedReport;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .selectSupportReport = [&selectedReport](std::size_t index) {
            selectedReport = index;
            return seam::core::success();
          },
      }};
  controller.resize(960.0, 600.0);
  controller.setDiagnostics({seam::authoring::Diagnostic{
      .code = "SUPPORT_BUNDLE_PREVIEW_READY",
      .severity = seam::authoring::DiagnosticSeverity::Info,
      .messageKey = "support.preview-ready",
      .actions = {seam::authoring::DiagnosticAction::ExportSupportBundle,
                  seam::authoring::DiagnosticAction::Dismiss},
  }});
  controller.setRecoverySupportView(seam::native_ui::RecoverySupportView{
      .visible = true,
      .mode = seam::native_ui::RecoverySupportMode::Preview,
      .candidateId = "build-17",
      .archiveBytes = 2048U,
      .archiveSha256 = std::string(64U, 'a'),
      .items = {
          seam::native_ui::RecoverySupportItemView{
              .name = "진단-診断-诊断-manifest.json",
              .detail = "PUBLIC / INCLUDED",
              .bytes = 512U,
              .sha256 = std::string(64U, 'b'),
              .included = true,
          },
          seam::native_ui::RecoverySupportItemView{
              .name = "attachments/세션-セッション-会话-project.seam",
              .detail = "RESTRICTED / CONSENT NEEDED",
              .bytes = 1536U,
              .sha256 = std::string(64U, 'c'),
              .included = false,
          },
      },
      .status = "Nothing has been written yet",
  });
  auto state = controller.sceneState();
  CHECK(state.recoverySupport.visible);
  CHECK(state.recoverySupport.mode ==
        seam::native_ui::RecoverySupportMode::Preview);
  CHECK(state.recoverySupport.items.size() == 2U);
  CHECK(state.recoverySupport.archiveSha256 == std::string(64U, 'a'));
  CHECK(seam::native_ui::editorDockVisible(state));

  seam::native_ui::EditorScenePainter painter;
  seam::native_ui::PixelSurface surface{960U, 600U};
  auto supportTextEngine = seam::text::TextEngine::createSystem();
  seam::native_ui::RasterCanvas canvas{
      surface, 1.0,
      supportTextEngine ? supportTextEngine.value().get() : nullptr};
  painter.paint(canvas, controller.pianoRoll(), state);
  CHECK(surface.checksum() != 0U);
  const auto* supportCaptureRoot =
      std::getenv("SEAM_NATIVE_UI_SUPPORT_CAPTURE_DIR");
  if (supportCaptureRoot != nullptr && *supportCaptureRoot != '\0') {
    const std::filesystem::path directory{supportCaptureRoot};
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    CHECK(!error);
    CHECK(surface.writePpm(directory / "support-preview-960x600.ppm"));
  }
  controller.rebuildAccessibilityTree();
  CHECK(seam::native_ui::EditorSemanticTree::containsId(
      controller.accessibilityTree().root(), "support.panel"));
  CHECK(seam::native_ui::EditorSemanticTree::containsId(
      controller.accessibilityTree().root(), "support.item.0"));

  controller.setRecoverySupportView(seam::native_ui::RecoverySupportView{
      .visible = true,
      .mode = seam::native_ui::RecoverySupportMode::Reports,
      .items = {
          seam::native_ui::RecoverySupportItemView{
              .name = "project-seam-support-one.zip",
              .detail = "ZIP / 2048 B",
              .bytes = 2048U,
              .sha256 = std::string(64U, 'd'),
              .included = true,
              .selected = true,
          },
          seam::native_ui::RecoverySupportItemView{
              .name = "project-seam-support-two.zip",
              .detail = "ZIP / 2048 B",
              .bytes = 2048U,
              .sha256 = std::string(64U, 'e'),
              .included = true,
          },
      },
      .reportCount = 2U,
      .status = "Select a report to reveal or delete",
  });
  controller.setDiagnostics({seam::authoring::Diagnostic{
      .code = "SUPPORT_BUNDLE_EXPORTED",
      .severity = seam::authoring::DiagnosticSeverity::Info,
      .messageKey = "support.exported-local-only",
      .actions = {seam::authoring::DiagnosticAction::OpenSupportFolder,
                  seam::authoring::DiagnosticAction::DeleteSupportBundle,
                  seam::authoring::DiagnosticAction::Dismiss},
  }});
  if (supportCaptureRoot != nullptr && *supportCaptureRoot != '\0') {
    seam::native_ui::PixelSurface reportsSurface{960U, 600U};
    seam::native_ui::RasterCanvas reportsCanvas{
        reportsSurface, 1.0,
        supportTextEngine ? supportTextEngine.value().get() : nullptr};
    painter.paint(reportsCanvas, controller.pianoRoll(), controller.sceneState());
    CHECK(reportsSurface.writePpm(
        std::filesystem::path{supportCaptureRoot} /
        "support-reports-960x600.ppm"));
  }
  controller.rebuildAccessibilityTree();
  CHECK(controller.dispatchAccessibility(
      "support.item.1", seam::native_ui::SemanticAction::Activate));
  CHECK(selectedReport == 1U);

  const auto supportState = controller.sceneState();
  const auto editorRight = 960.0 - painter.layout().characterDockWidth;
  const auto firstRow = painter.layout().supportItemBounds(
      editorRight, 960.0, 0U);
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{firstRow.x + firstRow.width * 0.5,
                                  firstRow.y + firstRow.height * 0.5},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(selectedReport == 0U);
  CHECK(supportState.recoverySupport.reportCount == 2U);

  auto compactView = controller.recoverySupportPanel().view();
  compactView.mode = seam::native_ui::RecoverySupportMode::Preview;
  compactView.candidateId = "build-17";
  compactView.archiveBytes = 2048U;
  compactView.archiveSha256 = std::string(64U, 'a');
  compactView.reportCount = 0U;
  compactView.status = "Review every entry before confirming export";
  for (auto& item : compactView.items) item.selected = false;
  compactView.items.push_back(seam::native_ui::RecoverySupportItemView{
      .name = "진단-診断-诊断-diagnostics.json",
      .detail = "PUBLIC / INCLUDED",
      .bytes = 512U,
      .sha256 = std::string(64U, 'f'),
      .included = true,
  });
  compactView.items.push_back(seam::native_ui::RecoverySupportItemView{
      .name = "attachments/세션-セッション-会话.log",
      .detail = "RESTRICTED / CONSENT NEEDED",
      .bytes = 512U,
      .sha256 = std::string(64U, '1'),
      .included = false,
  });
  controller.resize(480.0, 320.0);
  controller.setDiagnostics({seam::authoring::Diagnostic{
      .code = "SUPPORT_BUNDLE_PREVIEW_READY",
      .severity = seam::authoring::DiagnosticSeverity::Info,
      .messageKey = "support.preview-ready",
      .actions = {seam::authoring::DiagnosticAction::ExportSupportBundle,
                  seam::authoring::DiagnosticAction::Dismiss},
  }});
  controller.setRecoverySupportView(std::move(compactView));
  controller.rebuildAccessibilityTree();
  CHECK(controller.dispatchAccessibility(
      "support.item.3", seam::native_ui::SemanticAction::SetFocus));
  CHECK(controller.recoverySupportPanel().view().firstVisibleItem == 3U);
  const auto* focusedSupportItem = controller.accessibilityTree().focusedNode();
  CHECK(focusedSupportItem != nullptr);
  if (focusedSupportItem != nullptr) {
    CHECK(focusedSupportItem->id == "support.item.3");
    const auto expectedFocusedBounds = painter.layout().supportItemBounds(
        focusedSupportItem->bounds.x - painter.layout().supportPanelInsetX,
        480.0, 0U);
    CHECK_NEAR(focusedSupportItem->bounds.y, expectedFocusedBounds.y, 0.001);
    CHECK_NEAR(focusedSupportItem->bounds.height,
               expectedFocusedBounds.height, 0.001);
  }
  if (supportCaptureRoot != nullptr && *supportCaptureRoot != '\0') {
    seam::native_ui::PixelSurface compactSurface{480U, 320U};
    seam::native_ui::RasterCanvas compactCanvas{
        compactSurface, 1.0,
        supportTextEngine ? supportTextEngine.value().get() : nullptr};
    painter.paint(compactCanvas, controller.pianoRoll(), controller.sceneState());
    CHECK(compactSurface.writePpm(
        std::filesystem::path{supportCaptureRoot} /
        "support-preview-480x320-scrolled.ppm"));
  }
}

TEST_CASE("empty piano roll paints a bounded next-step cue") {
  NativeUiFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  region->notes.clear();
  region->lyrics.clear();
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  controller.resize(960.0, 600.0);
  seam::native_ui::PixelSurface surface{960U, 600U};
  seam::native_ui::RasterCanvas canvas{surface, 1.0};
  seam::native_ui::EditorScenePainter painter;
  painter.paint(canvas, controller.pianoRoll(), controller.sceneState());
  CHECK(surface.checksum() != 0U);
}

TEST_CASE("character dock portrait geometry follows shared layout tokens") {
  const seam::native_ui::EditorSceneLayout layout;
  const auto logicalWidth = 1280.0;
  const auto contentBottom = 692.0;
  const auto editorRight = std::max(
      layout.keyboardWidth + layout.minimumTimelineWidth,
      logicalWidth - layout.characterDockWidth);
  const auto portrait = layout.characterDockPortraitBounds(
      editorRight, contentBottom, logicalWidth);
  CHECK(portrait.x == editorRight + layout.characterDockPadding);
  CHECK(portrait.y == layout.toolbarHeight +
                           layout.characterDockPortraitTopInset);
  CHECK(portrait.right() <= logicalWidth);
  CHECK(portrait.height >= layout.characterDockPortraitMinimumHeight);
  CHECK(layout.characterDockMetadataTop(portrait) ==
        portrait.bottom() + layout.characterDockTextTopGap);
}

TEST_CASE("technical lane content metrics stay shared across compact heights") {
  const seam::native_ui::EditorSceneLayout layout;
  for (const auto height : {720.0, 320.0}) {
    const auto geometry = layout.technicalLaneGeometry(height);
    CHECK(layout.phonemeContentTop(geometry.phonemeTop) >=
          geometry.phonemeTop);
    CHECK(layout.phonemeContentHeight(geometry.phonemeHeight) > 0.0);
    CHECK(layout.unitContentTop(geometry.unitTop) >= geometry.unitTop);
    CHECK(layout.unitContentHeight(geometry.unitHeight) > 0.0);
  }
  CHECK(layout.automationCenterFraction > 0.0);
  CHECK(layout.automationCenterFraction < 1.0);
  CHECK(layout.pitchAutomationCentsRange > 0.0);
  CHECK(layout.pitchAutomationVerticalScale > 0.0);
  CHECK(layout.automationCurveStrokeWidth >=
        layout.automationGridStrokeWidth);
}

TEST_CASE("piano roll visual metrics stay tokenized") {
  const seam::native_ui::EditorSceneLayout layout;
  CHECK(layout.gridQuartersPerBar > 0U);
  CHECK(layout.gridStrokeWidth(true) > layout.gridStrokeWidth(false));
  const seam::ui::Rect shortNote{10.0, 20.0,
                                 layout.noteMinimumLabelWidth, 12.0};
  CHECK(!layout.noteLabelBounds(shortNote).has_value());
  const seam::ui::Rect labeledNote{
      10.0, 20.0, layout.noteMinimumLabelWidth + 10.0, 12.0};
  const auto label = layout.noteLabelBounds(labeledNote);
  CHECK(label.has_value());
  CHECK(label->x == labeledNote.x + layout.noteLabelInsetX);
  CHECK(label->y == labeledNote.y + layout.noteLabelBaselineOffset);
  CHECK(label->width == labeledNote.width -
                             layout.noteLabelHorizontalPadding);
  CHECK(label->height == layout.noteFontSize);
}

TEST_CASE("arrangement action geometry stays shared and contained") {
  const seam::native_ui::EditorSceneLayout layout;
  for (const auto width : {480.0, 720.0, 1280.0}) {
    std::optional<seam::ui::Rect> previous;
    for (std::size_t index = 0U; index < 5U; ++index) {
      const auto bounds = layout.arrangementActionBoundsForWidth(width, index);
      CHECK(bounds.x >= 0.0);
      CHECK(bounds.right() <= width);
      CHECK(bounds.y == layout.toolbarHeight + layout.arrangementActionTop);
      CHECK(bounds.height == layout.trackRowHeight);
      if (previous.has_value()) CHECK(bounds.x > previous->x);
      previous = bounds;
    }
  }
  CHECK(layout.panelSecondaryInstructionFontSize <
        layout.panelInstructionFontSize);
  CHECK(layout.arrangementActionFontSize >
        layout.panelInstructionFontSize);
}

TEST_CASE("voicebank and audio panel text metrics stay bounded") {
  const seam::native_ui::EditorSceneLayout layout;
  CHECK(layout.secondaryTextCharacterWidth > 0.0);
  CHECK(layout.panelRowTextWidthInset > layout.panelRowTextInsetX);
  CHECK(layout.audioSettingsStatsBottomInset >=
        layout.audioSettingsStatsHeight);
  CHECK(layout.audioSettingsDiagnosticVisibilityInset <=
        layout.audioSettingsStatsBottomInset);
  CHECK(layout.voicebankCardDiagnosticMinimumHeight >=
        layout.voicebankCardTitleBaseline);
  CHECK(layout.supportPanelFontSize >= 9.0);
  CHECK(layout.statusTextCharacterWidth >=
        layout.secondaryTextCharacterWidth);
}

TEST_CASE("editor overlay metrics stay tokenized") {
  const seam::native_ui::EditorSceneLayout layout;
  CHECK(layout.boxSelectionStrokeWidth > 0.0);
  CHECK(layout.playheadStrokeWidth > 0.0);
  CHECK(layout.lyricEditorBorderWidth >= layout.playheadStrokeWidth);
  CHECK(layout.lyricEditorFontSize > layout.panelInstructionFontSize);
  CHECK(layout.focusRingInset > 0.0);
  CHECK(layout.focusRingStrokeWidth >= layout.boxSelectionStrokeWidth);
}

TEST_CASE("embedded runtime overlays stay below the toolbar") {
  const seam::native_ui::EditorSceneLayout layout;
  CHECK(layout.runtimeOverlayTopPosition() >= layout.toolbarHeight);
  CHECK(layout.phase12BOverlayTopPosition() >= layout.toolbarHeight);
  CHECK(layout.runtimeOverlayTopPosition() + layout.runtimeOverlayHeight <=
        layout.contentTop());
  CHECK(layout.phase12BOverlayTopPosition() + layout.phase12BOverlayHeight <=
        layout.contentTop());
  for (const auto width : {320.0, 480.0, 800.0, 1100.0}) {
    const auto runtime = layout.runtimeOverlayBoundsForWidth(width);
    const auto meter = layout.runtimeOverlayMeterBoundsForWidth(width);
    const auto phase12B = layout.phase12BOverlayBoundsForWidth(width);
    CHECK(runtime.x >= 0.0);
    CHECK(runtime.right() <= width);
    CHECK(meter.x >= runtime.x);
    CHECK(meter.right() <= runtime.right());
    CHECK(phase12B.x >= 0.0);
    CHECK(phase12B.right() <= width);
    CHECK(layout.phase12BOverlayScaleForWidth(width) >= 0.0);
    CHECK(layout.phase12BOverlayScaleForWidth(width) <= 1.0);
  }
}

TEST_CASE("software raster surface paints deterministically and exports PPM") {
  seam::native_ui::PixelSurface first{320U, 180U};
  seam::native_ui::RasterCanvas canvas{first, 1.0};
  canvas.clear(seam::native_ui::Color{16, 15, 19, 255});
  canvas.fillRect(seam::ui::Rect{10.0, 10.0, 100.0, 40.0},
                  seam::native_ui::Color{139, 76, 105, 255});
  canvas.line(seam::ui::Point{0.0, 0.0}, seam::ui::Point{319.0, 179.0},
              seam::native_ui::Color{98, 192, 190, 255}, 2.0);
  canvas.drawText(seam::ui::Point{20.0, 70.0}, "SEAM 05",
                  seam::native_ui::Color{241, 235, 242, 255}, 12.0);

  seam::native_ui::PixelSurface second{320U, 180U};
  seam::native_ui::RasterCanvas secondCanvas{second, 1.0};
  secondCanvas.clear(seam::native_ui::Color{16, 15, 19, 255});
  secondCanvas.fillRect(seam::ui::Rect{10.0, 10.0, 100.0, 40.0},
                        seam::native_ui::Color{139, 76, 105, 255});
  secondCanvas.line(seam::ui::Point{0.0, 0.0},
                    seam::ui::Point{319.0, 179.0},
                    seam::native_ui::Color{98, 192, 190, 255}, 2.0);
  secondCanvas.drawText(seam::ui::Point{20.0, 70.0}, "SEAM 05",
                        seam::native_ui::Color{241, 235, 242, 255}, 12.0);
  CHECK(first.checksum() == second.checksum());
  CHECK(first.checksum() != 0U);

  const auto directory = seam::test::support::temporaryDirectory("native-ui-ppm");
  const auto path = directory / "surface.ppm";
  CHECK(first.writePpm(path));
  CHECK(std::filesystem::file_size(path) > first.pixels().size() * 3U);
}

TEST_CASE("editor scene remains logically identical at one and two times scale") {
  NativeUiFixture fixture;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  controller.resize(1280.0, 720.0);
  seam::native_ui::EditorScenePainter painter;

  seam::native_ui::PixelSurface one{1280U, 720U};
  seam::native_ui::RasterCanvas oneCanvas{one, 1.0};
  painter.paint(oneCanvas, controller.pianoRoll(), controller.sceneState());

  seam::native_ui::PixelSurface two{2560U, 1440U};
  seam::native_ui::RasterCanvas twoCanvas{two, 2.0};
  painter.paint(twoCanvas, controller.pianoRoll(), controller.sceneState());

  CHECK(one.checksum() != 0U);
  CHECK(two.checksum() != 0U);
  CHECK(one.width() * 2U == two.width());
  CHECK(one.height() * 2U == two.height());
  CHECK(controller.pianoRoll().visibleNotes().size() == 1U);
}

TEST_CASE("native scene paints a visible keyboard focus ring") {
  NativeUiFixture fixture;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  controller.resize(1280.0, 720.0);
  controller.rebuildAccessibilityTree();
  CHECK(!controller.sceneState().focusedElementBounds.has_value());
  CHECK(controller.dispatchAccessibility(
      "toolbar.transport", seam::native_ui::SemanticAction::SetFocus));
  const auto state = controller.sceneState();
  CHECK(state.focusedElementBounds.has_value());

  seam::native_ui::EditorScenePainter painter;
  seam::native_ui::PixelSurface focusedSurface{1280U, 720U};
  seam::native_ui::RasterCanvas focusedCanvas{focusedSurface, 1.0};
  painter.paint(focusedCanvas, controller.pianoRoll(), state);
  CHECK(focusedSurface.checksum() != 0U);

  auto unfocusedState = state;
  unfocusedState.focusedElementBounds.reset();
  seam::native_ui::PixelSurface unfocusedSurface{1280U, 720U};
  seam::native_ui::RasterCanvas unfocusedCanvas{unfocusedSurface, 1.0};
  painter.paint(unfocusedCanvas, controller.pianoRoll(), unfocusedState);
  CHECK(focusedSurface.checksum() != unfocusedSurface.checksum());
}

TEST_CASE("native design fixture matrix is deterministic across target viewports") {
  seam::test::native_ui_design::Fixture fixture;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  controller.setDiagnostics({seam::authoring::Diagnostic{
      .code = "BANK_MISSING",
      .severity = seam::authoring::DiagnosticSeverity::Error,
      .messageKey = "voicebank.missing",
      .actions = {seam::authoring::DiagnosticAction::ChooseVoicebank,
                  seam::authoring::DiagnosticAction::RelinkVoicebank},
  }});
  seam::native_ui::EditorScenePainter painter;
  controller.pianoRoll().pitch().setTopMidiKey(72);
  const auto textEngine = seam::text::TextEngine::createSystem();
  CHECK(textEngine);
  const auto* captureRoot = std::getenv("SEAM_NATIVE_UI_DESIGN_CAPTURE_DIR");
  std::error_code error;
  if (captureRoot != nullptr && *captureRoot != '\0') {
    std::filesystem::create_directories(captureRoot, error);
    CHECK(!error);
  }

  for (const auto& viewport : seam::test::native_ui_design::kTargetViewports) {
    for (const auto zoom : seam::test::native_ui_design::kTimelineZooms) {
      controller.resize(static_cast<double>(viewport.width),
                        static_cast<double>(viewport.height));
      controller.pianoRoll().timeline().setPixelsPerQuarter(zoom);
      controller.pianoRoll().rebuildIndex();
      CHECK(controller.pianoRoll().allNotes().size() == fixture.noteIds.size());
      for (const auto scale : seam::test::native_ui_design::kBackingScales) {
        const auto physicalWidth = static_cast<std::uint32_t>(
            static_cast<double>(viewport.width) * scale);
        const auto physicalHeight = static_cast<std::uint32_t>(
            static_cast<double>(viewport.height) * scale);
        seam::native_ui::PixelSurface first{physicalWidth, physicalHeight};
        seam::native_ui::RasterCanvas firstCanvas{first, scale,
                                                   textEngine.value().get()};
        painter.paint(firstCanvas, controller.pianoRoll(), controller.sceneState());
        seam::native_ui::PixelSurface second{physicalWidth, physicalHeight};
        seam::native_ui::RasterCanvas secondCanvas{second, scale,
                                                    textEngine.value().get()};
        painter.paint(secondCanvas, controller.pianoRoll(), controller.sceneState());
        CHECK(first.checksum() != 0U);
        CHECK(first.checksum() == second.checksum());

        if (captureRoot != nullptr && *captureRoot != '\0') {
          const auto filename = std::string{"dense-"} +
                                std::string{viewport.id} + "-scale" +
                                std::to_string(static_cast<int>(scale)) +
                                "-zoom" +
                                std::to_string(static_cast<int>(zoom)) + ".ppm";
          CHECK(first.writePpm(std::filesystem::path{captureRoot} / filename));
        }
      }
    }
  }
}

TEST_CASE("native design journey fixtures cover detail identity and motion states") {
  const auto* captureRoot =
      std::getenv("SEAM_NATIVE_UI_JOURNEY_CAPTURE_DIR");
  const auto captureDirectory = captureRoot == nullptr
                                    ? std::filesystem::path{}
                                    : std::filesystem::path{captureRoot};
  if (!captureDirectory.empty()) {
    std::error_code error;
    std::filesystem::create_directories(captureDirectory, error);
    CHECK(!error);
  }
  const auto textEngine = seam::text::TextEngine::createSystem();
  CHECK(textEngine);
  seam::native_ui::EditorScenePainter painter;
  const auto capture = [&](seam::native_ui::NativeEditorController& controller,
                           std::string_view name) {
    seam::native_ui::PixelSurface surface{960U, 600U};
    seam::native_ui::RasterCanvas canvas{surface, 1.0,
                                          textEngine.value().get()};
    painter.paint(canvas, controller.pianoRoll(), controller.sceneState());
    CHECK(surface.checksum() != 0U);
    if (!captureDirectory.empty()) {
      CHECK(surface.writePpm(captureDirectory / std::string{name}));
    }
  };

  NativeUiFixture detailFixture;
  auto* detailRegion =
      detailFixture.session.project().findRegion(detailFixture.regionId);
  CHECK(detailRegion != nullptr);
  detailRegion->findLyric(detailFixture.lyricId)->surface =
      U"가나다라마바사 こんにちは世界 中文歌词";
  detailRegion->findNote(detailFixture.noteId)->durationTick =
      seam::time::Tick{240};
  seam::native_ui::NativeEditorController detailController{
      detailFixture.session, detailFixture.factory, detailFixture.regionId};
  detailController.resize(960.0, 600.0);
  detailController.pianoRoll().pitch().setTopMidiKey(72);
  detailController.rebuildAccessibilityTree();
  CHECK(detailController.dispatchAccessibility(
      "note." + detailFixture.noteId.toString(),
      seam::native_ui::SemanticAction::SetFocus));
  CHECK(detailController.sceneState().detail.has_value());
  capture(detailController, "note-detail-focus.ppm");

  seam::test::native_ui_design::Fixture overlapFixture;
  seam::native_ui::NativeEditorController overlapController{
      overlapFixture.session, overlapFixture.factory, overlapFixture.regionId};
  overlapController.resize(960.0, 600.0);
  overlapController.pianoRoll().pitch().setTopMidiKey(72);
  overlapController.rebuildAccessibilityTree();
  const auto& overlapRoot = overlapController.accessibilityTree().root();
  const auto overlapGroup = std::find_if(
      overlapRoot.children.begin(), overlapRoot.children.end(),
      [](const auto& node) { return node.id.starts_with("overlap-group."); });
  CHECK(overlapGroup != overlapRoot.children.end());
  capture(overlapController, "overlap-cycle-before.ppm");
  if (overlapGroup != overlapRoot.children.end()) {
    CHECK(overlapController.dispatchAccessibility(
        overlapGroup->id, seam::native_ui::SemanticAction::Activate));
  }
  const auto firstOverlap = overlapController.sceneState().overlapDetail;
  CHECK(firstOverlap.has_value());
  if (firstOverlap.has_value()) {
    CHECK(firstOverlap->members.size() == 5U);
    CHECK(firstOverlap->members[0U].selected);
  }
  capture(overlapController, "overlap-cycle-1.ppm");
  if (overlapGroup != overlapRoot.children.end()) {
    CHECK(overlapController.dispatchAccessibility(
        overlapGroup->id, seam::native_ui::SemanticAction::Activate));
  }
  const auto secondOverlap = overlapController.sceneState().overlapDetail;
  CHECK(secondOverlap.has_value());
  if (secondOverlap.has_value()) {
    CHECK(secondOverlap->members.size() == 5U);
    CHECK(secondOverlap->members[1U].selected);
  }
  capture(overlapController, "overlap-cycle-2.ppm");

  NativeUiFixture characterFixture;
  auto& characterTrack =
      characterFixture.session.project().vocalTracks().front();
  characterTrack.voicebank = seam::domain::VoicebankReference{
      .id = "voice.journey",
      .version = "1.0.0",
      .contentHash = std::string(64U, 'a'),
  };
  characterFixture.session.project().settings().characterDisplay =
      seam::domain::CharacterDisplayMode::Full;
  auto now = std::chrono::steady_clock::time_point{};
  seam::native_ui::NativeEditorController characterController{
      characterFixture.session, characterFixture.factory,
      characterFixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .uiClock = [&now] { return now; },
          .reduceMotionEnabled = [] { return false; },
      }};
  characterController.resize(960.0, 600.0);
  auto portraitResult = seam::native_ui::PixelSurface::loadPpm(
      "assets/character-01/runtime/neutral.ppm");
  CHECK(portraitResult);
  if (!portraitResult) return;
  auto portrait = std::move(portraitResult).value();
  const seam::authoring::VoicebankCard characterCard{
      .id = characterTrack.voicebank.id,
      .version = characterTrack.voicebank.version,
      .displayName = "Journey Voice",
      .contentHash = characterTrack.voicebank.contentHash,
      .selectable = true,
      .characterAvailable = true,
      .characterId = "character.journey",
      .characterVersion = "1.0.0",
  };
  characterController.setVoicebankCards({characterCard});
  characterController.setCharacterBinding({
      .id = "character.journey",
      .version = "1.0.0",
      .voicebankId = characterTrack.voicebank.id,
  });
  characterController.setCharacterMetadata("Character 01", "emo-low-poly");
  characterController.setCharacterPortrait(&portrait);
  CHECK(characterController.sceneState().voiceIdentity.characterActive);
  characterController.setRenderStatus({.state = seam::native_ui::RenderStatusState::Rendering});
  CHECK(characterController.sceneState().characterState == seam::character::State::Rendering);
  characterController.setPlaying(true);
  characterController.setRenderStatus({.state = seam::native_ui::RenderStatusState::Ready});
  CHECK(characterController.sceneState().characterState == seam::character::State::Focused);
  capture(characterController, "character-ready-matched.ppm");
  CHECK(characterController.keyDown(
      seam::native_ui::KeyEvent{.key = seam::native_ui::NativeKey::C}));
  capture(characterController, "identity-transition-start.ppm");
  now += std::chrono::milliseconds{75};
  capture(characterController, "identity-transition-mid.ppm");
  now += std::chrono::milliseconds{75};
  capture(characterController, "identity-transition-end.ppm");

  NativeUiFixture mismatchFixture;
  auto& mismatchTrack = mismatchFixture.session.project().vocalTracks().front();
  mismatchTrack.voicebank = characterTrack.voicebank;
  mismatchFixture.session.project().settings().characterDisplay =
      seam::domain::CharacterDisplayMode::Full;
  seam::native_ui::NativeEditorController mismatchController{
      mismatchFixture.session, mismatchFixture.factory, mismatchFixture.regionId};
  mismatchController.resize(960.0, 600.0);
  mismatchController.setVoicebankCards({characterCard});
  mismatchController.setCharacterBinding({
      .id = "character.journey",
      .version = "1.0.0",
      .voicebankId = "different.voice",
  });
  mismatchController.setCharacterPortrait(&portrait);
  CHECK(!mismatchController.sceneState().voiceIdentity.characterActive);
  capture(mismatchController, "character-ready-mismatched.ppm");

  NativeUiFixture laneFixture;
  auto laneNow = std::chrono::steady_clock::time_point{};
  seam::native_ui::NativeEditorController laneController{
      laneFixture.session, laneFixture.factory, laneFixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .uiClock = [&laneNow] { return laneNow; },
          .reduceMotionEnabled = [] { return false; },
      }};
  laneController.resize(960.0, 600.0);
  laneController.rebuildAccessibilityTree();
  CHECK(laneController.dispatchAccessibility(
      "lane.pitch", seam::native_ui::SemanticAction::Toggle));
  capture(laneController, "lane-transition-start.ppm");
  laneNow += std::chrono::milliseconds{75};
  capture(laneController, "lane-transition-mid.ppm");
  laneNow += std::chrono::milliseconds{75};
  capture(laneController, "lane-transition-end.ppm");

  NativeUiFixture reducedFixture;
  seam::native_ui::NativeEditorController reducedController{
      reducedFixture.session, reducedFixture.factory, reducedFixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .reduceMotionEnabled = [] { return true; },
      }};
  reducedController.resize(960.0, 600.0);
  reducedController.rebuildAccessibilityTree();
  CHECK(reducedController.dispatchAccessibility(
      "lane.pitch", seam::native_ui::SemanticAction::Toggle));
  CHECK(!reducedController.sceneState().technicalLaneHeightsOverride.has_value());
  capture(reducedController, "lane-reduced-motion-final.ppm");
}

TEST_CASE("native scene captures a subpixel-duration note without collapsing it") {
  NativeUiFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  auto* note = region->findNote(fixture.noteId);
  CHECK(note != nullptr);
  note->startTick = seam::time::Tick{480};
  note->durationTick = seam::time::Tick{12};
  note->midiKey = 84U;
  region->sortNotes();
  fixture.session.selection().selectOnly(fixture.noteId);

  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  seam::native_ui::EditorScenePainter painter;
  controller.resize(1440.0, 900.0);
  const auto scaleOneNotes = controller.pianoRoll().visibleNotes();
  CHECK(scaleOneNotes.size() == 1U);
  CHECK(scaleOneNotes.front().bounds.width > 0.0);
  seam::native_ui::PixelSurface scaleOne{1440U, 900U};
  seam::native_ui::RasterCanvas scaleOneCanvas{scaleOne, 1.0};
  painter.paint(scaleOneCanvas, controller.pianoRoll(), controller.sceneState());

  controller.resize(720.0, 450.0);
  const auto scaleTwoNotes = controller.pianoRoll().visibleNotes();
  CHECK(scaleTwoNotes.size() == 1U);
  CHECK(scaleTwoNotes.front().bounds.width > 0.0);
  seam::native_ui::PixelSurface scaleTwo{1440U, 900U};
  seam::native_ui::RasterCanvas scaleTwoCanvas{scaleTwo, 2.0};
  painter.paint(scaleTwoCanvas, controller.pianoRoll(), controller.sceneState());
  CHECK(scaleOne.checksum() != 0U);
  CHECK(scaleTwo.checksum() != 0U);

  if (const auto* captureRoot =
          std::getenv("SEAM_NATIVE_UI_SUBPIXEL_CAPTURE_DIR");
      captureRoot != nullptr && *captureRoot != '\0') {
    const std::filesystem::path directory{captureRoot};
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    CHECK(!error);
    CHECK(scaleOne.writePpm(directory / "subpixel-note-scale1.ppm"));
    CHECK(scaleTwo.writePpm(directory / "subpixel-note-scale2.ppm"));
  }
}

TEST_CASE("native pitch lane moves removes and cycles automation points") {
  NativeUiFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  const auto originalTick = seam::time::Tick{960};
  CHECK(region->pitchAutomation.upsert(
      seam::domain::PitchAutomationPoint{
          .tick = originalTick,
          .cents = 0.0F,
          .interpolation = seam::domain::CurveInterpolation::Linear,
      }));

  std::size_t documentChanges = 0U;
  std::size_t cycleCalls = 0U;
  std::optional<seam::domain::PitchAutomationPoint> moved;
  std::optional<seam::time::Tick> removed;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .documentChanged = [&documentChanges] { ++documentChanges; },
          .movePitchPoint = [&moved, region](
              seam::time::Tick from, seam::domain::PitchAutomationPoint point) {
            CHECK(from == seam::time::Tick{960});
            moved = point;
            CHECK(region->pitchAutomation.erase(from));
            return region->pitchAutomation.upsert(point);
          },
          .removePitchPoint = [&removed, region](seam::time::Tick tick) {
            removed = tick;
            CHECK(region->pitchAutomation.erase(tick));
            return seam::core::success();
          },
          .cyclePitchInterpolation = [&cycleCalls, &moved](seam::time::Tick tick) {
            CHECK(moved.has_value());
            CHECK(tick == moved->tick);
            ++cycleCalls;
            return seam::core::success();
          },
      }};
  controller.resize(1280.0, 720.0);
  const seam::native_ui::EditorSceneLayout layout;
  const auto state = controller.sceneState();
  const auto technical = seam::native_ui::resolveTechnicalLaneHeights(
      seam::native_ui::TechnicalLaneLayoutInput{
          .presentation = state.technicalLanes,
          .populated = {!state.phonemes.tokens.empty(), !state.unitOverrides.empty(),
                        !state.seamOverrides.empty(), true},
          .previewHeights = {layout.phonemeLaneHeight, layout.unitLaneHeight,
                             layout.seamLaneHeight, layout.automationLaneHeight},
          .contentTop = layout.contentTop(),
          .contentBottom = 720.0 - layout.statusHeight,
      });
  const auto automationTop = technical.pianoBottom + technical.values[0U] +
                             technical.values[1U] + technical.values[2U];
  const auto automationHeight = technical.values[3U];
  const auto point = seam::ui::Point{
      layout.keyboardWidth +
          controller.pianoRoll().timeline().tickToPixel(originalTick),
      automationTop + automationHeight * layout.automationCenterFraction};

  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = point, .button = seam::native_ui::PointerButton::Left,
      .modifiers = {}, .clickCount = 1}));
  const auto movedPosition = seam::ui::Point{point.x + 112.0, point.y - 8.0};
  CHECK(controller.pointerMove(seam::native_ui::PointerEvent{
      .position = movedPosition, .button = seam::native_ui::PointerButton::Left,
      .modifiers = {}, .clickCount = 1}));
  CHECK(controller.pointerUp(seam::native_ui::PointerEvent{
      .position = movedPosition,
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {}, .clickCount = 1}));
  CHECK(moved.has_value());
  CHECK(moved->tick != originalTick);
  CHECK(moved->cents != 0.0F);

  const auto movedPoint = seam::ui::Point{
      layout.keyboardWidth +
          controller.pianoRoll().timeline().tickToPixel(moved->tick),
      automationTop + automationHeight * layout.automationCenterFraction -
          static_cast<double>(moved->cents) /
              layout.pitchAutomationCentsRange *
              (automationHeight * layout.pitchAutomationVerticalScale)};
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = movedPoint, .button = seam::native_ui::PointerButton::Left,
      .modifiers = {}, .clickCount = 2}));
  CHECK(cycleCalls == 1U);
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = movedPoint, .button = seam::native_ui::PointerButton::Left,
      .modifiers = seam::native_ui::InputModifiers{.shift = true},
      .clickCount = 1}));
  CHECK(removed.has_value());
  CHECK(removed.value() == moved->tick);
  CHECK(documentChanges == 3U);
}

TEST_CASE("native batch lyrics use IME composition and semantic activation") {
  NativeUiFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  auto [secondLyric, secondNote] = fixture.factory.makeNote(
      seam::time::Tick{2160}, seam::time::Tick{960}, 67U, U"old",
      seam::domain::Language::English);
  const auto secondNoteId = secondNote.id;
  region->lyrics.push_back(std::move(secondLyric));
  region->notes.push_back(std::move(secondNote));
  region->sortNotes();
  fixture.session.selection().replace({fixture.noteId, secondNoteId});

  std::optional<seam::native_ui::TextInputRequest> request;
  std::size_t endTextInputCalls = 0U;
  std::size_t documentChanges = 0U;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .beginTextInput = [&request](
              const seam::native_ui::TextInputRequest& value) {
            request = value;
          },
          .endTextInput = [&endTextInputCalls] { ++endTextInputCalls; },
          .documentChanged = [&documentChanges] { ++documentChanges; },
      }};
  controller.resize(1280.0, 720.0);
  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::L,
      .modifiers = seam::native_ui::InputModifiers{.shift = true},
      .repeat = false,
  }));
  CHECK(controller.textInputActive());
  CHECK(request.has_value());
  CHECK(request->currentText.empty());
  CHECK(controller.commitTextComposition(U"きゃ ね"));
  CHECK(!controller.textInputActive());
  CHECK(endTextInputCalls == 1U);
  CHECK(documentChanges == 0U);
  CHECK(controller.sceneState().replacementReview.visible);
  applyReadyLyricReview(controller);
  CHECK(documentChanges == 1U);
  const auto* first = region->findLyric(
      fixture.session.project().findNote(fixture.noteId)->lyricTokenId);
  const auto* second = region->findLyric(
      fixture.session.project().findNote(secondNoteId)->lyricTokenId);
  CHECK(first != nullptr);
  CHECK(second != nullptr);
  CHECK(first->surface == U"きゃ");
  CHECK(second->surface == U"ね");

  controller.rebuildAccessibilityTree();
  CHECK(controller.dispatchAccessibility(
      "toolbar.batch-lyrics", seam::native_ui::SemanticAction::Activate));
  CHECK(controller.textInputActive());
  controller.cancelTextComposition();
  CHECK(!controller.textInputActive());
}

// D2's exit names IME focus at the minimum window as uncovered: the existing batch-lyrics case runs
// at 1280x720, so nothing checked that opening composition at the enforced minimum still reports a
// usable input rectangle and still ends exactly once. A composer anchored off-surface or left open
// after a commit is the failure this looks for.
TEST_CASE("native IME composition holds at the minimum window and ends exactly once") {
  NativeUiFixture fixture;
  const auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  const auto noteId = fixture.noteId;
  fixture.session.selection().replace({noteId});

  std::optional<seam::native_ui::TextInputRequest> request;
  std::size_t beginCalls = 0U;
  std::size_t endCalls = 0U;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .beginTextInput = [&request, &beginCalls](
              const seam::native_ui::TextInputRequest& value) {
            ++beginCalls;
            request = value;
          },
          .endTextInput = [&endCalls] { ++endCalls; },
          .documentChanged = [] {},
      }};
  controller.resize(1024.0, 768.0);

  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::L,
      .modifiers = seam::native_ui::InputModifiers{.shift = true},
      .repeat = false,
  }));
  CHECK(controller.textInputActive());
  CHECK(beginCalls == 1U);
  CHECK(request.has_value());
  if (!request) return;

  // The anchor must be a real place on the surface. An empty or out-of-surface rectangle is how a
  // composition box ends up invisible at a small size even though the state says it is open.
  CHECK(request->logicalBounds.width > 0.0);
  CHECK(request->logicalBounds.height > 0.0);
  CHECK(request->logicalBounds.x >= 0.0);
  CHECK(request->logicalBounds.y >= 0.0);
  CHECK(request->logicalBounds.right() <= 1024.0);
  CHECK(request->logicalBounds.bottom() <= 768.0);
  CHECK(request->currentText.empty());
  // Distributing lyrics over a selection is not a single token's edit, so the request carries no
  // particular lyric identity. Recording that here keeps the assertion honest about which route ran.
  CHECK(request->lyricId != fixture.lyricId);

  // A composition that is cancelled closes the input once and writes nothing.
  controller.cancelTextComposition();
  CHECK(!controller.textInputActive());
  CHECK(endCalls == 1U);

  // A composition that commits closes the input once and reaches the review, so the keyboard route
  // and the semantic route behave the same at the minimum size.
  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::L,
      .modifiers = seam::native_ui::InputModifiers{.shift = true},
      .repeat = false,
  }));
  CHECK(controller.textInputActive());
  CHECK(endCalls == 1U);
  CHECK(controller.commitTextComposition(U"み"));
  CHECK(!controller.textInputActive());
  CHECK(endCalls == 2U);
  CHECK(controller.sceneState().replacementReview.visible);
  applyReadyLyricReview(controller);
  const auto* refreshed = fixture.session.project().findRegion(fixture.regionId);
  CHECK(refreshed != nullptr);
  const auto* note = refreshed != nullptr ? refreshed->findNote(noteId) : nullptr;
  CHECK(note != nullptr);
  if (note == nullptr) return;
  const auto* lyric = refreshed->findLyric(note->lyricTokenId);
  CHECK(lyric != nullptr);
  if (lyric != nullptr) CHECK(lyric->surface == U"み");
}

TEST_CASE("editor status bar paints within the supported minimum width") {
  NativeUiFixture fixture;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  controller.resize(480.0, 320.0);
  controller.setAudioState(false, "threaded-callback-clock");
  controller.setRenderStatus(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Failed,
      .requestedRevision = 27U,
      .audibleRevision = 26U,
      .hasAudibleAudio = true,
      .diagnostic = "Voicebank is missing",
  });
  seam::native_ui::EditorScenePainter painter;
  seam::native_ui::PixelSurface surface{480U, 320U};
  seam::native_ui::RasterCanvas canvas{surface, 1.0};
  painter.paint(canvas, controller.pianoRoll(), controller.sceneState());
  CHECK(surface.checksum() != 0U);

  controller.resize(1440.0, 900.0);
  seam::native_ui::PixelSurface normalSurface{1440U, 900U};
  seam::native_ui::RasterCanvas normalCanvas{normalSurface, 1.0};
  painter.paint(normalCanvas, controller.pianoRoll(), controller.sceneState());
  CHECK(normalSurface.checksum() != 0U);

  if (const auto* captureRoot = std::getenv("SEAM_NATIVE_UI_STATUS_CAPTURE_DIR");
      captureRoot != nullptr && *captureRoot != '\0') {
    const std::filesystem::path directory{captureRoot};
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    CHECK(!error);
    CHECK(surface.writePpm(directory / "render-status-narrow.ppm"));
    CHECK(normalSurface.writePpm(directory / "render-status-normal.ppm"));

    controller.setRenderStatus(seam::native_ui::RenderStatusView{
        .state = seam::native_ui::RenderStatusState::Failed,
        .requestedRevision = 0U,
        .audibleRevision = 0U,
        .hasAudibleAudio = false,
        .diagnostic = "No rendered audio is available",
    });
    seam::native_ui::PixelSurface noAudioSurface{1440U, 900U};
    seam::native_ui::RasterCanvas noAudioCanvas{noAudioSurface, 1.0};
    painter.paint(noAudioCanvas, controller.pianoRoll(), controller.sceneState());
    CHECK(noAudioSurface.writePpm(directory / "render-status-no-audio.ppm"));
  }
}

TEST_CASE("microscope close bounds stay inside the modal panel") {
  const seam::native_ui::EditorSceneLayout layout;
  for (const auto& [width, height] : {
           std::pair{320.0, 320.0}, std::pair{480.0, 320.0},
           std::pair{800.0, 480.0}, std::pair{1280.0, 720.0}}) {
    const auto panel = layout.microscopePanelBounds(width, height);
    const auto close = layout.microscopeCloseBounds(width, height);
    CHECK(close.x >= panel.x);
    CHECK(close.right() <= panel.right());
    CHECK(close.y >= panel.y);
    CHECK(close.bottom() <= panel.bottom());
    CHECK(close.height == layout.microscopeCloseHeight);
  }
}

TEST_CASE("accessibility dispatch rejects disabled controls and disabled ancestor surfaces") {
  using namespace seam::native_ui;
  AccessibilityTree tree;
  const auto makeRoot=[] {
    SemanticNode root{.id="root",.role=SemanticRole::Panel};
    root.children.push_back({.id="action",.role=SemanticRole::Button,.enabled=false,.actions={SemanticAction::Activate,SemanticAction::SetFocus}});
    root.children.push_back({.id="disabled-group",.role=SemanticRole::Panel,.enabled=false,
        .children={{.id="nested",.role=SemanticRole::Button,.actions={SemanticAction::Activate,SemanticAction::SetFocus}}}});
    return root;
  };
  bool called=false;
  const auto handler=[&](std::string_view,SemanticAction){called=true;return seam::core::success();};
  tree.rebuildCustom(makeRoot());
  CHECK(!tree.dispatch("action",SemanticAction::Activate,handler));
  CHECK(!tree.dispatch("nested",SemanticAction::Activate,handler)); CHECK(!called);
  CHECK(tree.setFocus("action")); CHECK(tree.setFocus("nested"));
  CHECK(tree.focusNext(false));
  CHECK(tree.dispatch("action",SemanticAction::SetFocus,handler)); CHECK(called); called=false;
  auto enabled=makeRoot(); enabled.children.front().enabled=true;
  tree.rebuildCustom(enabled); CHECK(tree.dispatch("action",SemanticAction::Activate,handler)); CHECK(called);
  CHECK(tree.setFocus("action"));
  CHECK(tree.focusNext(false)); CHECK(tree.focusedNode()); CHECK(tree.focusedNode()->id=="nested");
  called=false; enabled.enabled=false; tree.rebuildCustom(std::move(enabled));
  CHECK(!tree.dispatch("action",SemanticAction::Activate,handler)); CHECK(!called);
  CHECK(tree.setFocus("action")); CHECK(tree.focusNext(true));
  CHECK(tree.dispatch("nested",SemanticAction::SetFocus,handler)); CHECK(called);
}

TEST_CASE("accessibility dispatch owns its target ID while the callback replaces the tree") {
  using namespace seam::native_ui;
  AccessibilityTree tree;
  const std::string expected="designer."+std::string(256U,'x')+".action";
  SemanticNode root{.id="root",.role=SemanticRole::Panel};
  root.children.push_back({.id=expected,.role=SemanticRole::Button,.actions={SemanticAction::Activate}});
  tree.rebuildCustom(std::move(root));
  const auto* borrowed=tree.root().children.front().id.data();
  CHECK(tree.dispatch(tree.root().children.front().id,SemanticAction::Activate,
      [&](std::string_view id,SemanticAction action) {
        CHECK(id.data()!=borrowed);
        tree.rebuildCustom({.id="replacement",.role=SemanticRole::Panel});
        CHECK(id==expected); CHECK(action==SemanticAction::Activate);
        return seam::core::success();
      }));
  CHECK(tree.root().id=="replacement");
}

TEST_CASE("native accessibility dispatches notes outside the piano viewport") {
  NativeUiFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  region->durationTick = seam::time::Tick{2000000};
  auto [lyric, note] = fixture.factory.makeNote(
      seam::time::Tick{1000000}, seam::time::Tick{480}, 68U, U"あ",
      seam::domain::Language::Japanese);
  const auto offscreenNoteId = note.id;
  region->lyrics.push_back(std::move(lyric));
  region->notes.push_back(std::move(note));
  region->sortNotes();

  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  controller.resize(480.0, 320.0);
  controller.rebuildAccessibilityTree();
  CHECK(controller.accessibilityTree().virtualizedNoteCount() == 2U);
  const auto offscreen = controller.accessibilityTree().materializeNotes(1U, 1U);
  CHECK(offscreen.size() == 1U);
  CHECK(offscreen.front().id == "note." + offscreenNoteId.toString());
  CHECK(controller.dispatchAccessibility(
      offscreen.front().id, seam::native_ui::SemanticAction::Activate));
  CHECK(fixture.session.selection().contains(offscreenNoteId));
}

TEST_CASE("standalone controller opens a read-only sample microscope overlay") {
  NativeUiFixture fixture;
  auto audio = seam::test::support::sineWave(48000U, 220.0, 0.25);
  for (std::size_t index = 0U; index < audio.size(); ++index) {
    const auto seconds = static_cast<double>(index) / 48000.0;
    const auto harmonic = 0.20F * static_cast<float>(
        std::sin(2.0 * std::numbers::pi * 440.0 * seconds));
    const auto overtone = 0.12F * static_cast<float>(
        std::sin(2.0 * std::numbers::pi * 660.0 * seconds));
    const auto upper = 0.08F * static_cast<float>(
        std::sin(2.0 * std::numbers::pi * 1760.0 * seconds));
    const auto shimmer = 0.05F * static_cast<float>(
        std::sin(2.0 * std::numbers::pi * 3520.0 * seconds));
    const auto pulse = 0.62F + 0.38F * static_cast<float>(
        0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * 2.0 * seconds));
    audio[index] = std::clamp(
        (audio[index] + harmonic + overtone + upper + shimmer) * pulse,
        -1.0F, 1.0F);
  }
  auto unit = seam::test::support::makeUnit(
      "日本語サンプルユニット-01234567890123456789", {"a"}, "audio/a.wav", 64U,
      seam::voicebank::UnitKind::Cv, audio.size());
  unit.pitchMarks = {
      {.frame = 3200, .confidence = 0.98F, .locked = true},
      {.frame = 7200, .confidence = 0.91F, .locked = false},
      {.frame = 11200, .confidence = 0.95F, .locked = true},
      {.frame = 15200, .confidence = 0.88F, .locked = false},
  };
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .loadSampleMicroscope = [unit, audio](seam::domain::PhonemeKey)
              -> seam::core::Result<seam::native_ui::SampleMicroscopeData> {
            return seam::native_ui::SampleMicroscopeData{
                .unit = unit,
                .audio = seam::voicebank::AudioBuffer{
                    .sampleRate = 48000U, .channels = 1U,
                    .interleaved = audio},
                .destinationContext = "DESTINATION / Voice / Region / MIDI 64",
            };
          },
      }};
  controller.resize(1280.0, 720.0);
  CHECK(controller.openSampleMicroscope(seam::domain::PhonemeKey{
      .noteId = fixture.noteId, .ordinal = 0U}));
  CHECK(controller.sampleMicroscopeOpen());
  CHECK(controller.sampleMicroscope() != nullptr);
  CHECK(controller.sampleMicroscopeUnitId() ==
        "日本語サンプルユニット-01234567890123456789");
  CHECK(!controller.sampleMicroscope()->waveform().empty());
  CHECK(controller.sampleMicroscope()->spectrogram().columns > 1U);
  CHECK(!controller.sampleMicroscope()->markers().empty());
  CHECK(!controller.sampleMicroscope()->pitchMarks().empty());
  const auto& decibels = controller.sampleMicroscope()->spectrogram().decibels;
  const auto minimum = *std::min_element(decibels.begin(), decibels.end());
  const auto maximum = *std::max_element(decibels.begin(), decibels.end());
  CHECK(maximum - minimum > 5.0F);

  controller.rebuildAccessibilityTree();
  const auto& microscopeTree = controller.accessibilityTree().root();
  CHECK(seam::native_ui::EditorSemanticTree::containsId(
      microscopeTree, "microscope.panel"));
  CHECK(seam::native_ui::EditorSemanticTree::containsId(
      microscopeTree, "microscope.waveform"));
  CHECK(seam::native_ui::EditorSemanticTree::containsId(
      microscopeTree, "microscope.spectrogram"));
  CHECK(seam::native_ui::EditorSemanticTree::containsId(
      microscopeTree, "microscope.close"));
  CHECK(!seam::native_ui::EditorSemanticTree::containsId(
      microscopeTree, "toolbar.transport"));
  CHECK(controller.dispatchAccessibility(
      "microscope.close", seam::native_ui::SemanticAction::Activate));
  CHECK(!controller.sampleMicroscopeOpen());
  CHECK(controller.openSampleMicroscope(seam::domain::PhonemeKey{
      .noteId = fixture.noteId, .ordinal = 0U}));

  seam::native_ui::PixelSurface surface{1280U, 720U};
  auto textEngine = seam::text::TextEngine::createSystem();
  seam::native_ui::RasterCanvas canvas{
      surface, 1.0, textEngine ? textEngine.value().get() : nullptr};
  seam::native_ui::EditorScenePainter painter;
  painter.paint(canvas, controller.pianoRoll(), controller.sceneState());
  CHECK(surface.checksum() != 0U);
  if (const auto* captureRoot = std::getenv("SEAM_NATIVE_UI_MICROSCOPE_CAPTURE_DIR");
      captureRoot != nullptr && *captureRoot != '\0') {
    const std::filesystem::path directory{captureRoot};
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    CHECK(!error);
    CHECK(surface.writePpm(directory / "sample-microscope.ppm"));
  }

  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{100.0, 100.0},
      .button = seam::native_ui::PointerButton::Right,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(!controller.sampleMicroscopeOpen());
  CHECK(controller.openSampleMicroscope(seam::domain::PhonemeKey{
      .noteId = fixture.noteId, .ordinal = 0U}));
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{100.0, 100.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 2,
  }));
  CHECK(!controller.sampleMicroscopeOpen());
  CHECK(controller.openSampleMicroscope(seam::domain::PhonemeKey{
      .noteId = fixture.noteId, .ordinal = 0U}));
  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::Escape, .modifiers = {},
      .repeat = false}));
  CHECK(!controller.sampleMicroscopeOpen());
}

TEST_CASE("native microscope edits markers and plays a test sample through callbacks") {
  NativeUiFixture fixture;
  const auto audio = seam::test::support::sineWave(48000U, 220.0, 0.5);
  auto unit = seam::test::support::makeUnit(
      "microscope-edit-unit", {"a"}, "audio/a.wav", 64U,
      seam::voicebank::UnitKind::Cv, audio.size());
  unit.pitchMarks = {
      {.frame = 7200, .confidence = 0.9F, .locked = false},
      {.frame = 11200, .confidence = 0.8F, .locked = false},
  };
  std::optional<seam::voicebank::Unit> changed;
  std::size_t playbackRequests = 0U;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .loadSampleMicroscope = [unit, audio](seam::domain::PhonemeKey)
              -> seam::core::Result<seam::native_ui::SampleMicroscopeData> {
            return seam::native_ui::SampleMicroscopeData{
                .unit = unit,
                .audio = seam::voicebank::AudioBuffer{
                    .sampleRate = 48000U, .channels = 1U,
                    .interleaved = audio},
                .destinationContext = "DESTINATION / test-note",
            };
          },
          .microscopeUnitChanged =
              [&changed](seam::domain::PhonemeKey,
                         const seam::voicebank::Unit& updated) {
                changed = updated;
                return seam::core::success();
              },
          .playMicroscopeSample =
              [&playbackRequests](const seam::voicebank::Unit&,
                                  const seam::voicebank::AudioBuffer&) {
                ++playbackRequests;
                return seam::core::success();
              },
      }};
  controller.resize(1280.0, 720.0);
  const auto key = seam::domain::PhonemeKey{
      .noteId = fixture.noteId, .ordinal = 0U};
  CHECK(controller.openSampleMicroscope(key));
  const auto* microscope = controller.sampleMicroscope();
  CHECK(microscope != nullptr);
  const auto marker = std::find_if(
      microscope->markers().begin(), microscope->markers().end(),
      [](const auto& value) {
        return value.kind == seam::ui::AcousticMarkerKind::VowelOnset;
      });
  CHECK(marker != microscope->markers().end());
  const auto waveform = microscope->waveformBounds();
  const seam::ui::Point markerPoint{marker->x, waveform.y + 4.0};
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = markerPoint,
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(controller.pointerMove(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{markerPoint.x + 12.0, markerPoint.y},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(controller.pointerUp(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{markerPoint.x + 12.0, markerPoint.y},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(changed.has_value());
  CHECK(changed->markers.vowelOnset != unit.markers.vowelOnset);

  CHECK(controller.openSampleMicroscope(key));
  controller.rebuildAccessibilityTree();
  CHECK(controller.dispatchAccessibility(
      "microscope.waveform", seam::native_ui::SemanticAction::Activate));
  CHECK(playbackRequests == 1U);
  microscope = controller.sampleMicroscope();
  CHECK(microscope != nullptr);
  const auto pitch = microscope->pitchMarks().front();
  const seam::ui::Point pitchPoint{pitch.x, waveform.y + waveform.height * 0.5};
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = pitchPoint,
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(controller.pointerMove(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{pitchPoint.x + 10.0, pitchPoint.y},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(controller.pointerUp(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{pitchPoint.x + 10.0, pitchPoint.y},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(changed.has_value());
  CHECK(changed->pitchMarks.front().frame != unit.pitchMarks.front().frame);

  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{waveform.x + waveform.width * 0.5,
                                  waveform.y + waveform.height * 0.5},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 2,
  }));
  CHECK(playbackRequests == 2U);
  CHECK(controller.sampleMicroscopeOpen());
}

TEST_CASE("native controller routes transport controls through host callbacks") {
  NativeUiFixture fixture;
  bool playing = false;
  std::size_t stopRequests = 0U;
  std::size_t loopToggles = 0U;
  std::size_t cancelRequests = 0U;
  std::size_t retryRequests = 0U;
  std::vector<seam::time::Tick> seeks;
  std::vector<std::pair<seam::time::Tick, seam::time::Tick>> loops;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .requestRepaint = {},
          .beginTextInput = {},
          .endTextInput = {},
          .setPlaying = [&playing](bool value) {
            playing = value;
            return seam::core::success();
          },
          .documentChanged = {},
          .stopPlaying = [&stopRequests] {
            ++stopRequests;
            return seam::core::success();
          },
          .seekTick = [&seeks](seam::time::Tick tick) {
            seeks.push_back(tick);
            return seam::core::success();
          },
          .setLoopTicks = [&loops](seam::time::Tick start,
                                   seam::time::Tick end) {
            loops.emplace_back(start, end);
            return seam::core::success();
          },
          .toggleLoop = [&loopToggles] {
            ++loopToggles;
            return seam::core::success();
          },
          .cancelRender = [&cancelRequests] { ++cancelRequests; },
          .retryRender = [&retryRequests] { ++retryRequests; },
      }};
  controller.resize(1280.0, 720.0);
  controller.setRenderStatus(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Ready,
      .requestedRevision = 1U,
      .audibleRevision = 1U,
      .hasAudibleAudio = true,
  });

  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{340.0, 25.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(playing);
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{470.0, 25.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(stopRequests == 1U);
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{240.0, 80.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(seeks.size() == 1U);
  CHECK(seeks.front() > seam::time::Tick{0});
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{420.0, 80.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {.shift = true},
      .clickCount = 1,
  }));
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{620.0, 80.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {.shift = true},
      .clickCount = 1,
  }));
  CHECK(loops.size() == 1U);
  CHECK(loops.front().first < loops.front().second);
  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::L,
      .modifiers = {},
      .repeat = false,
  }));
  CHECK(loopToggles == 1U);
  controller.resize(1440.0, 900.0);
  controller.rebuildAccessibilityTree();
  CHECK(controller.dispatchAccessibility(
      "toolbar.loop", seam::native_ui::SemanticAction::Activate));
  CHECK(loopToggles == 2U);
  CHECK(controller.sceneState().loopEnabled);
  const auto wideLayout = seam::native_ui::EditorScenePainter{}.layout();
  const auto wideLoop = wideLayout.loopBoundsForWidth(1440.0, false);
  CHECK(wideLoop.width > 0.0);
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{wideLoop.x + wideLoop.width * 0.5,
                                  wideLoop.y + wideLoop.height * 0.5},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(loopToggles == 3U);
  CHECK(!controller.sceneState().loopEnabled);

  controller.setRenderStatus(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Queued,
      .requestedRevision = 3U,
  });
  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::Escape,
      .modifiers = {},
      .repeat = false,
  }));
  CHECK(cancelRequests == 1U);
  controller.setRenderStatus(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Failed,
      .requestedRevision = 3U,
  });
  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::R,
      .modifiers = {},
      .repeat = false,
  }));
  CHECK(retryRequests == 1U);

  controller.setRenderStatus(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Failed,
      .requestedRevision = 4U,
      .hasAudibleAudio = false,
  });
  playing = false;
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{340.0, 25.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(!playing);

  controller.resize(480.0, 320.0);
  controller.setRenderStatus(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Ready,
      .requestedRevision = 4U,
      .audibleRevision = 4U,
      .hasAudibleAudio = true,
  });
  const auto narrowLayout = seam::native_ui::EditorScenePainter{}.layout();
  const auto narrowTransport = narrowLayout.transportBoundsForWidth(480.0);
  const auto narrowStop = narrowLayout.stopBoundsForWidth(480.0);
  const auto narrowTempo = narrowLayout.bpmBoundsForWidth(480.0);
  CHECK(narrowTransport.right() <= 480.0);
  CHECK(narrowStop.right() <= 480.0);
  CHECK(narrowTempo.right() <= 480.0);
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{narrowTransport.x + narrowTransport.width * 0.5,
                                  narrowTransport.y + narrowTransport.height * 0.5},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(playing);
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{narrowStop.x + narrowStop.width * 0.5,
                                  narrowStop.y + narrowStop.height * 0.5},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(stopRequests == 2U);
}

TEST_CASE("native transport keeps play state unchanged when host rejects it") {
  NativeUiFixture fixture;
  std::size_t requests = 0U;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .setPlaying = [&requests](bool) {
            ++requests;
            return seam::core::failure(seam::core::ErrorCode::Conflict,
                                       "audio device is unavailable");
          },
      }};
  controller.resize(1280.0, 720.0);
  controller.setRenderStatus(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Ready,
      .requestedRevision = 1U,
      .audibleRevision = 1U,
      .hasAudibleAudio = true,
  });

  const auto result = controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{340.0, 25.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  });
  CHECK(!result);
  CHECK(result.error().code == seam::core::ErrorCode::Conflict);
  CHECK(requests == 1U);
  CHECK(!controller.playing());

  controller.rebuildAccessibilityTree();
  const auto semanticResult = controller.dispatchAccessibility(
      "toolbar.transport", seam::native_ui::SemanticAction::Activate);
  CHECK(!semanticResult);
  CHECK(semanticResult.error().code == seam::core::ErrorCode::Conflict);
  CHECK(requests == 2U);
  CHECK(!controller.playing());

  seam::native_ui::NativeEditorController stopController{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .stopPlaying = [] {
            return seam::core::failure(seam::core::ErrorCode::Conflict,
                                       "audio device stop failed");
          },
      }};
  stopController.resize(1280.0, 720.0);
  stopController.setPlaying(true);
  stopController.setRenderStatus(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Ready,
      .requestedRevision = 1U,
      .audibleRevision = 1U,
      .hasAudibleAudio = true,
  });
  const auto stopResult = stopController.pointerDown(
      seam::native_ui::PointerEvent{
          .position = seam::ui::Point{470.0, 25.0},
          .button = seam::native_ui::PointerButton::Left,
          .modifiers = {},
          .clickCount = 1,
      });
  CHECK(!stopResult);
  CHECK(stopResult.error().code == seam::core::ErrorCode::Conflict);
  CHECK(stopController.playing());

  std::size_t seekRequests = 0U;
  seam::native_ui::NativeEditorController seekController{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .seekTick = [&seekRequests](seam::time::Tick) {
            ++seekRequests;
            return seam::core::failure(seam::core::ErrorCode::Conflict,
                                       "audio seek failed");
          },
      }};
  seekController.resize(1280.0, 720.0);
  const auto seekResult = seekController.pointerDown(
      seam::native_ui::PointerEvent{
          .position = seam::ui::Point{240.0, 80.0},
          .button = seam::native_ui::PointerButton::Left,
          .modifiers = {},
          .clickCount = 1,
      });
  CHECK(!seekResult);
  CHECK(seekResult.error().code == seam::core::ErrorCode::Conflict);
  CHECK(seekRequests == 1U);

  seam::native_ui::NativeEditorController loopController{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .toggleLoop = [] {
            return seam::core::failure(seam::core::ErrorCode::Conflict,
                                       "audio loop failed");
          },
      }};
  loopController.resize(1440.0, 900.0);
  loopController.setRenderStatus(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Ready,
      .requestedRevision = 1U,
      .audibleRevision = 1U,
      .hasAudibleAudio = true,
  });
  const auto loopBounds =
      seam::native_ui::EditorScenePainter{}.layout().loopBoundsForWidth(
          1440.0, false);
  const auto loopResult = loopController.pointerDown(
      seam::native_ui::PointerEvent{
          .position = seam::ui::Point{loopBounds.x + loopBounds.width * 0.5,
                                      loopBounds.y + loopBounds.height * 0.5},
          .button = seam::native_ui::PointerButton::Left,
          .modifiers = {},
          .clickCount = 1,
      });
  CHECK(!loopResult);
  CHECK(loopResult.error().code == seam::core::ErrorCode::Conflict);
  CHECK(!loopController.sceneState().loopEnabled);
}

TEST_CASE("native bounce timing control chooses and reports the host authority") {
  NativeUiFixture fixture;
  std::vector<bool> requests;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .setBounceTiming = [&requests](bool followHost) {
            requests.push_back(followHost);
            return seam::core::success();
          },
      }};
  controller.resize(1440.0, 900.0);
  const auto layout = seam::native_ui::EditorScenePainter{}.layout();
  const auto bounds = layout.bounceTimingBoundsForWidth(1440.0, false);
  CHECK(bounds.width > 0.0);
  // The control never covers the loop control it follows, or the project header.
  const auto loop = layout.loopBoundsForWidth(1440.0, false);
  CHECK(loop.width > 0.0);
  CHECK(bounds.x >= loop.right());

  CHECK(controller.sceneState().bounceTimingAvailable);
  CHECK(!controller.sceneState().bounceFollowHost);
  controller.rebuildAccessibilityTree();
  CHECK(controller.dispatchAccessibility(
      "toolbar.bounce", seam::native_ui::SemanticAction::Activate));
  CHECK(requests.size() == 1U);
  CHECK(requests.front());
  CHECK(controller.sceneState().bounceFollowHost);

  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{bounds.x + bounds.width * 0.5,
                                  bounds.y + bounds.height * 0.5},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(requests.size() == 2U);
  CHECK(!requests.back());
  CHECK(!controller.sceneState().bounceFollowHost);

  // A refused choice does not move the control's state.
  seam::native_ui::NativeEditorController refusing{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .setBounceTiming = [](bool) {
            return seam::core::failure(seam::core::ErrorCode::Conflict,
                                       "bounce timing failed");
          },
      }};
  refusing.resize(1440.0, 900.0);
  const auto refused = refusing.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{bounds.x + bounds.width * 0.5,
                                  bounds.y + bounds.height * 0.5},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  });
  CHECK(!refused);
  CHECK(refused.error().code == seam::core::ErrorCode::Conflict);
  CHECK(!refusing.sceneState().bounceFollowHost);

  // A surface that cannot offer the choice does not show it.
  seam::native_ui::NativeEditorController without{fixture.session, fixture.factory,
                                                  fixture.regionId};
  without.resize(1440.0, 900.0);
  CHECK(!without.sceneState().bounceTimingAvailable);
}

TEST_CASE("native arrangement inspector commits track mix through shared commands") {
  NativeUiFixture fixture;
  fixture.session.project().settings().characterDisplay =
      seam::domain::CharacterDisplayMode::Off;
  fixture.session.project().routing().buses.push_back(
      seam::domain::AudioBus{.id = seam::domain::BusId{2U},
                             .name = "Vocal Bus",
                             .channelCount = 2U});
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  CHECK(controller.setSelectedTrackMix(-3.0F, 0.25F, true, false));
  const auto* track = fixture.session.project().findVocalTrack(
      controller.selectedTrack());
  CHECK(track != nullptr);
  CHECK_NEAR(track->gainDb, -3.0, 1e-6);
  CHECK_NEAR(track->pan, 0.25, 1e-6);
  CHECK(track->muted);
  CHECK(!track->solo);
  CHECK(controller.setSelectedTrackRoute(seam::domain::TrackOutputRoute{
      .bus = seam::domain::BusId{2U},
      .matrix = seam::domain::RoutingMatrix::monoToStereo()}));
  CHECK(track->outputRoute.bus == seam::domain::BusId{2U});
  controller.rebuildAccessibilityTree();
  CHECK(controller.dispatchAccessibility(
      "inspector.route", seam::native_ui::SemanticAction::Activate));
  CHECK(track->outputRoute.bus == seam::domain::BusId{1U});
}

TEST_CASE("native seam controls expose presets, fields, reset, and A/B preview") {
  NativeUiFixture fixture;
  std::size_t previewCalls = 0U;
  std::size_t rendererCycleCalls = 0U;
  bool previewAlternate = false;
  std::optional<seam::domain::PhonemeKey> selectedUnitKey;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .cycleUnitVariant = [&fixture, &selectedUnitKey](
                                  seam::domain::PhonemeKey key) {
            selectedUnitKey = key;
            return fixture.session.execute(
                std::make_unique<seam::application::UpsertUnitSelectionOverrideCommand>(
                    fixture.regionId,
                    seam::domain::UnitSelectionOverride{
                        .startKey = key,
                        .tokenCount = 1,
                        .unitId = "unit.preview",
                        .renderer = seam::domain::UnitRendererKind::Raw,
                        .locked = true,
                    }));
          },
          .cycleUnitRenderer = [&rendererCycleCalls](seam::domain::PhonemeKey) {
            ++rendererCycleCalls;
            return seam::core::success();
          },
          .previewSeam = [&previewCalls, &previewAlternate](
              seam::domain::PhonemeKey, bool alternate) {
            ++previewCalls;
            previewAlternate = alternate;
            return seam::core::success();
          },
      }};
  controller.resize(1280.0, 720.0);
  seam::native_ui::EditorScenePainter painter;
  const auto layout = painter.layout();
  const auto state = controller.sceneState();
  const auto technical = seam::native_ui::resolveTechnicalLaneHeights(
      seam::native_ui::TechnicalLaneLayoutInput{
          .presentation = state.technicalLanes,
          .populated = {!state.phonemes.tokens.empty(), true,
                        !state.seamOverrides.empty(), !state.pitchAutomation.empty()},
          .previewHeights = {layout.phonemeLaneHeight, layout.unitLaneHeight,
                             layout.seamLaneHeight, layout.automationLaneHeight},
          .contentTop = layout.contentTop(),
          .contentBottom = 720.0 - layout.statusHeight,
      });
  const auto pianoBottom = technical.pianoBottom;
  const auto unitTop = pianoBottom + technical.values[0U];
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{layout.keyboardWidth + 160.0,
                                  unitTop + 8.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::R,
      .modifiers = {},
      .repeat = false,
  }));
  CHECK(rendererCycleCalls == 1U);
  CHECK(controller.setSelectedUnitLoopPrint(0.35F));
  CHECK(controller.setSelectedUnitSourcePitchResidual(0.72F));
  CHECK(selectedUnitKey.has_value());
  const auto* unitOverride = fixture.session.project().findRegion(
      fixture.regionId)->findUnitSelectionOverride(*selectedUnitKey);
  CHECK(unitOverride != nullptr);
  CHECK_NEAR(unitOverride->loopPrint.value_or(0.0F), 0.35, 1e-6);
  CHECK_NEAR(unitOverride->sourcePitchResidual.value_or(0.0F), 0.72, 1e-6);
  const auto seamTop = unitTop + technical.values[1U];
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{layout.keyboardWidth + 160.0,
                                  seamTop + 8.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(controller.sceneState().selectedSeam.has_value());
  const auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  CHECK(region->seamOverrides.size() == 1U);
  CHECK(controller.setSelectedSeamAmount(0.8F));
  CHECK(controller.setSelectedSeamOverlap(seam::time::Microseconds{12'000}));
  CHECK(controller.setSelectedSeamPhaseReset(0.25F));
  CHECK(controller.setSelectedSeamEnvelopeBlend(0.7F));
  CHECK(controller.cycleSelectedSeamCurve());
  CHECK(controller.applySelectedSeamPreset(
      seam::native_ui::NativeEditorController::SeamPreset::Character));
  CHECK(controller.toggleSelectedSeamPreview());
  CHECK(previewCalls == 1U);
  CHECK(previewAlternate);
  CHECK(previewAlternate == controller.sceneState().seamPreviewAlternate);
  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::B,
      .modifiers = {.alt = true},
      .repeat = false,
  }));
  CHECK(previewCalls == 2U);
  CHECK(!previewAlternate);
  CHECK(controller.resetSelectedSeam());
  CHECK(region->seamOverrides.empty());
}

TEST_CASE("native arrangement controller exposes undoable track and region editing") {
  NativeUiFixture fixture;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};

  const auto addedTrack = controller.addVocalTrack("Harmony");
  CHECK(addedTrack);
  CHECK(controller.selectedTrack() == addedTrack.value());
  const auto addedRegion = controller.addVocalRegion(
      "Harmony phrase", seam::time::Tick{0}, seam::time::Tick{1920});
  CHECK(addedRegion);
  CHECK(controller.selectedRegion() == addedRegion.value());
  CHECK(controller.renameSelectedTrack("Harmony renamed"));
  CHECK(controller.renameSelectedRegion("Harmony verse"));
  CHECK(controller.splitSelectedRegion(seam::time::Tick{960}));
  CHECK(fixture.session.project().vocalTracks().size() == 2U);
  CHECK(fixture.session.project().findRegion(addedRegion.value()) != nullptr);
  CHECK(controller.removeSelectedTrack());
  CHECK(fixture.session.project().findVocalTrack(addedTrack.value()) == nullptr);
  CHECK(fixture.session.undo());
  CHECK(fixture.session.project().findVocalTrack(addedTrack.value()) != nullptr);
}

TEST_CASE("native tempo meter dispatch guards context and notifies document changes only on success") {
  using namespace seam;
  NativeUiFixture fixture;
  unsigned changes = 0U;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.documentChanged = [&] { ++changes; }}};
  const auto revision = fixture.session.revision();
  CHECK(controller.editTempo(revision, time::Tick{960}, 90.0));
  CHECK(changes == 1U); CHECK(fixture.session.lastImpact().projectWide);
  CHECK(!controller.editMeter(revision, time::Tick{3840}, application::EditMeterCommand::Signature{3, 4}));
  CHECK(changes == 1U);
  CHECK(!controller.editTempo(fixture.session.revision(), time::Tick{960}, -1.0));
  CHECK(changes == 1U);
  CHECK(controller.editMeter(fixture.session.revision(), time::Tick{3840}, application::EditMeterCommand::Signature{3, 4}));
  CHECK(changes == 2U);
  CHECK(controller.beginLyricEdit(fixture.noteId));
  CHECK(!controller.editTempo(fixture.session.revision(), time::Tick{0}, 80.0));
  CHECK(controller.textInputActive()); CHECK(changes == 2U);
  controller.cancelTextComposition();
  CHECK(controller.editTempo(fixture.session.revision(), time::Tick{960}, std::nullopt));
  CHECK(changes == 3U);
  CHECK(fixture.session.undo()); CHECK(fixture.session.project().tempoMap().bpmAt(time::Tick{960}) == 90.0);
}

TEST_CASE("native phone hint input preserves lyrics and supports clear undo and no-op") {
  using namespace seam;
  NativeUiFixture fixture;
  auto* lyric = fixture.session.project().findRegion(fixture.regionId)->findLyric(fixture.lyricId);
  lyric->language = domain::Language::Japanese;
  lyric->surface = U"き";
  unsigned changed = 0U;
  std::u32string input;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [&](const native_ui::TextInputRequest& request) { input = request.currentText; },
       .documentChanged = [&] { ++changed; }}};
  CHECK(!controller.keyDown({.key = native_ui::NativeKey::Enter, .modifiers = {.alt = true}}));
  fixture.session.selection().selectOnly(fixture.noteId);
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Enter, .modifiers = {.alt = true}}));
  CHECK(input.empty());
  CHECK(controller.sceneState().boundedInputLabel == "PHONE HINT (EMPTY = CLEAR)");
  CHECK(controller.sceneState().lyricEditor.has_value());
  CHECK(controller.commitTextComposition(U"sh a"));
  CHECK(fixture.session.project().findNote(fixture.noteId)->phoneticHint == "sh a");
  CHECK(fixture.session.project().findRegion(fixture.regionId)->findLyric(fixture.lyricId)->surface == U"き");
  CHECK(changed == 1U);
  const auto revision = fixture.session.revision();
  CHECK(controller.beginHintEdit(fixture.noteId)); CHECK(input == U"sh a");
  CHECK(controller.commitTextComposition(U"sh a"));
  CHECK(fixture.session.revision() == revision); CHECK(changed == 1U);
  CHECK(controller.beginHintEdit(fixture.noteId));
  CHECK(controller.commitTextComposition(U""));
  CHECK(!fixture.session.project().findNote(fixture.noteId)->phoneticHint);
  CHECK(fixture.session.undo());
  CHECK(fixture.session.project().findNote(fixture.noteId)->phoneticHint == "sh a");
  CHECK(fixture.session.redo());
  CHECK(!fixture.session.project().findNote(fixture.noteId)->phoneticHint);
  CHECK(!controller.textInputActive());
}

TEST_CASE("native phone hints reject invalid stale and unsupported edits without retargeting") {
  using namespace seam;
  NativeUiFixture fixture;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}}};
  CHECK(!controller.beginHintEdit(fixture.noteId)); // English has no registered hint validator.
  auto* lyric = fixture.session.project().findRegion(fixture.regionId)->findLyric(fixture.lyricId);
  lyric->language = domain::Language::Japanese; lyric->surface = U"き";
  CHECK(controller.beginHintEdit(fixture.noteId));
  const auto revision = fixture.session.revision();
  CHECK(!controller.commitTextComposition(U"not-a-phone"));
  CHECK(fixture.session.revision() == revision);
  CHECK(!fixture.session.project().findNote(fixture.noteId)->phoneticHint);
  CHECK(controller.beginHintEdit(fixture.noteId));
  CHECK(fixture.session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{960}, 80.0)));
  CHECK(!controller.commitTextComposition(U"sh a"));
  CHECK(!fixture.session.project().findNote(fixture.noteId)->phoneticHint);
  CHECK(controller.beginHintEdit(fixture.noteId));
  controller.cancelTextComposition();
  CHECK(!controller.textInputActive());
  CHECK(controller.beginHintEdit(fixture.noteId));
  CHECK(controller.beginLyricEdit(fixture.noteId));
  CHECK(controller.commitTextComposition(U"あ"));
  CHECK(fixture.session.project().findRegion(fixture.regionId)->findLyric(fixture.lyricId)->surface == U"あ");
  CHECK(!fixture.session.project().findNote(fixture.noteId)->phoneticHint);
  CHECK(controller.beginHintEdit(fixture.noteId));
  CHECK(controller.updateTextComposition(U"k a", {}));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Tab}));
  CHECK(!controller.textInputActive()); // Auxiliary input must not open the next lyric.
  CHECK(fixture.session.project().findNote(fixture.noteId)->phoneticHint == "k a");
  CHECK(controller.beginHintEdit(fixture.noteId));
  fixture.session.project().findNote(fixture.noteId)->phoneticHint = "sh a";
  CHECK(!controller.commitTextComposition(U"k a")); // Even a no-op checks its captured source.
  CHECK(fixture.session.project().findNote(fixture.noteId)->phoneticHint == "sh a");
}

TEST_CASE("phone hint semantics isolate active input and reject old interaction identities") {
  using namespace seam;
  NativeUiFixture fixture;
  auto* lyric = fixture.session.project().findRegion(fixture.regionId)->findLyric(fixture.lyricId);
  lyric->language = domain::Language::Japanese; lyric->surface = U"あ";
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}}};
  controller.resize(720.0, 520.0);
  const auto inputId = [&] {
    controller.rebuildAccessibilityTree();
    CHECK(controller.accessibilityTree().virtualizedNoteCount() == 0U);
    CHECK(controller.accessibilityTree().root().children.size() == 2U);
    const auto& input = controller.accessibilityTree().root().children.front();
    CHECK(input.role == native_ui::SemanticRole::TextField);
    CHECK(!input.bounds.intersects(controller.accessibilityTree().root().children.back().bounds));
    return input.id;
  };
  CHECK(controller.beginHintEdit(fixture.noteId));
  const auto first = inputId();
  CHECK(controller.updateTextComposition(U"sh a N cl k a", {}));
  if (const auto* capture = std::getenv("SEAM_HINT_INPUT_CAPTURE")) {
    native_ui::PixelSurface surface{720U, 520U}; native_ui::RasterCanvas canvas{surface, 1.0};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState());
    CHECK(surface.writePpm(capture));
  }
  CHECK(!controller.dispatchAccessibility("toolbar.time-map", native_ui::SemanticAction::Activate));
  CHECK(!controller.setAccessibilityValue("note." + fixture.noteId.toString(), "changed"));
  CHECK(!controller.setAccessibilityValue(first, std::string(4097U, 'a')));
  CHECK(!controller.setAccessibilityValue(first, std::string{"\xff"}));
  CHECK(controller.textInputActive());
  const auto cancel = controller.accessibilityTree().root().children.back().id;
  CHECK(controller.dispatchAccessibility(cancel, native_ui::SemanticAction::Activate));
  CHECK(!controller.setAccessibilityValue(first, "k a"));
  CHECK(controller.beginHintEdit(fixture.noteId));
  const auto second = inputId(); CHECK(second != first);
  CHECK(!controller.setAccessibilityValue(first, "k a"));
  CHECK(!controller.dispatchAccessibility(cancel, native_ui::SemanticAction::Activate));
  CHECK(controller.setAccessibilityValue(second, "sh a"));
  CHECK(fixture.session.project().findNote(fixture.noteId)->phoneticHint == "sh a");
  CHECK(controller.beginHintEdit(fixture.noteId));
  CHECK(controller.setAccessibilityValue(inputId(), ""));
  CHECK(!fixture.session.project().findNote(fixture.noteId)->phoneticHint);
  CHECK(controller.beginHintEdit(fixture.noteId));
  const auto stale = inputId();
  CHECK(fixture.session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{960}, 80.0)));
  CHECK(!controller.setAccessibilityValue(stale, "k a"));
  CHECK(!controller.setAccessibilityValue(inputId(), "k a"));
  CHECK(!fixture.session.project().findNote(fixture.noteId)->phoneticHint);
  CHECK(controller.beginHintEdit(fixture.noteId));
  const auto revision = fixture.session.revision();
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Delete}));
  CHECK(fixture.session.revision() == revision);
  const native_ui::EditorSceneLayout layout;
  const auto bounds = layout.hintCancelBounds(controller.sceneState().logicalWidth, controller.sceneState().logicalHeight);
  CHECK(controller.pointerDown({.position = {bounds.x + 2.0, bounds.y + 2.0}, .button = native_ui::PointerButton::Left}));
  CHECK(!controller.textInputActive());
}

TEST_CASE("native replacement review pages isolates refreshes and applies one undo group") {
  using namespace seam;
  NativeUiFixture fixture;
  for (int i = 0; i < 12; ++i) {
    auto [lyric, note] = fixture.factory.makeNote(time::Tick{i * 480}, time::Tick{480}, 60U, U"edge", domain::Language::English);
    auto* region = fixture.session.project().findRegion(fixture.regionId);
    region->lyrics.push_back(lyric); region->notes.push_back(note);
  }
  fixture.session.project().findRegion(fixture.regionId)->sortNotes();
  unsigned changes = 0U;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.documentChanged = [&] { ++changes; }}};
  controller.resize(720.0, 520.0);
  const auto waitReady = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < deadline) {
      controller.pollReplacementReview();
      const auto view = controller.sceneState().replacementReview;
      if (view.enabled[3]) return;
      CHECK(view.status == "Preparing replacement review...");
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    throw test::Failure{"Native replacement review did not become ready"};
  };
  const auto id = [&](std::size_t action) {
    controller.rebuildAccessibilityTree();
    for (const auto& node : controller.accessibilityTree().root().children)
      if (node.id.ends_with("action." + std::to_string(action))) return node.id;
    throw test::Failure{"Missing replacement action"};
  };
  const auto before = fixture.session.project();
  CHECK(controller.openReplacementReview("edge", "la"));
  const auto preparingApply = id(3U);
  CHECK(!controller.dispatchAccessibility(preparingApply, native_ui::SemanticAction::Activate));
  CHECK(!controller.setAccessibilityValue("note." + fixture.noteId.toString(), "changed"));
  CHECK(!controller.beginTempoEdit()); CHECK(!controller.beginLyricEdit(fixture.noteId));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Delete})); CHECK(fixture.session.project() == before);
  waitReady(); CHECK(changes == 0U);
  CHECK(!controller.dispatchAccessibility(preparingApply, native_ui::SemanticAction::Activate));
  CHECK(controller.accessibilityTree().virtualizedNoteCount() == 0U);
  CHECK(controller.sceneState().replacementReview.rows.size() == 6U);
  const auto oldNext = id(1U);
  CHECK(controller.dispatchAccessibility(oldNext, native_ui::SemanticAction::Activate));
  CHECK(!controller.dispatchAccessibility(oldNext, native_ui::SemanticAction::Activate));
  CHECK(controller.replacementReviewAction(1U));
  CHECK(controller.sceneState().replacementReview.rows.size() == 1U);
  CHECK(!controller.replacementReviewAction(1U));
  CHECK(controller.replacementReviewAction(2U));
  CHECK(controller.sceneState().replacementReview.rows.front() == "No retained dependency records");
  CHECK(controller.replacementReviewAction(2U));
  if (const auto* capture = std::getenv("SEAM_REPLACEMENT_REVIEW_CAPTURE")) {
    native_ui::PixelSurface surface{720U, 520U}; native_ui::RasterCanvas canvas{surface, 1.0};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState());
    CHECK(surface.writePpm(capture));
  }
  const auto staleApply = id(3U);
  CHECK(fixture.session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{0}, 90.0)));
  CHECK(!controller.sceneState().replacementReview.enabled[3]);
  CHECK(!controller.dispatchAccessibility(staleApply, native_ui::SemanticAction::Activate));
  CHECK(controller.replacementReviewAction(5U)); waitReady();
  const auto refreshed = fixture.session.project();
  CHECK(controller.dispatchAccessibility(id(3U), native_ui::SemanticAction::Activate));
  CHECK(changes == 1U); CHECK(!controller.sceneState().replacementReview.visible);
  for (const auto& lyric : fixture.session.project().findRegion(fixture.regionId)->lyrics) CHECK(lyric.surface == U"la");
  CHECK(fixture.session.undo()); CHECK(fixture.session.project() == refreshed);
  CHECK(controller.openReplacementReview("edge", "li"));
  CHECK(controller.replacementReviewAction(4U));
  CHECK(!controller.sceneState().replacementReview.visible); CHECK(changes == 1U);
  CHECK(fixture.session.project() == refreshed);
}

static void waitForNativeFind(seam::native_ui::NativeEditorController& controller) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (controller.findPreparing() && std::chrono::steady_clock::now() < deadline) {
    controller.pollReplacementReview(); std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  CHECK(!controller.findPreparing());
}

TEST_CASE("native diagnostic Find works without a vocal region and never runs recovery or selects notes") {
  using namespace seam;
  application::ProjectFactory factory{271000U}; application::EditorSession session{factory.createProject("Global diagnostic Find")};
  unsigned changes = 0U, actions = 0U;
  native_ui::NativeEditorController controller{session, factory, {},
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}, .documentChanged = [&] { ++changes; },
       .diagnosticAction = [&](const authoring::Diagnostic&, authoring::DiagnosticAction) { ++actions; return core::success(); }}};
  controller.resize(480.0, 320.0);
  authoring::Diagnostic issue{.code = "MEDIA_MISSING", .messageKey = "media.missing",
      .actions = {authoring::DiagnosticAction::RelinkMedia}};
  issue.setDetail(std::string(300U, 'x') + " missing vocal sample 歌"); controller.setDiagnostics({issue});
  const auto source = session.project();
  CHECK(!controller.beginFindInput()); CHECK(controller.beginDiagnosticFindInput());
  CHECK(controller.sceneState().boundedInputLabel == "FIND ACTIVE DIAGNOSTICS (LITERAL)");
  controller.rebuildAccessibilityTree(); const auto field = controller.accessibilityTree().root().children.front().id;
  CHECK(!controller.setAccessibilityValue(field, "")); CHECK(controller.commitTextComposition(U"MEDIA"));
  CHECK(controller.findPreparing()); CHECK(!controller.sceneState().replacementReview.enabled[3]); waitForNativeFind(controller);
  CHECK(controller.sceneState().replacementReview.status == "Find: Active diagnostics");
  CHECK(!controller.sceneState().replacementReview.enabled[2]); // No lyric region is invented.
  controller.rebuildAccessibilityTree(); const auto row = controller.accessibilityTree().root().children.front().id;
  CHECK(controller.dispatchAccessibility(row, native_ui::SemanticAction::SetFocus));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Enter})); CHECK(!controller.sceneState().replacementReview.enabled[3]);
  if (const auto* capture = std::getenv("SEAM_DIAGNOSTIC_FIND_CAPTURE")) {
    auto engine = text::TextEngine::createSystem(); CHECK(engine);
    native_ui::PixelSurface surface{480U,320U}; native_ui::RasterCanvas canvas{surface,1.0,engine.value().get()};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState()); CHECK(surface.writePpm(capture));
  }
  std::string full;
  for (unsigned page = 0U; page < 20U; ++page) {
    const auto view = controller.sceneState().replacementReview;
    for (const auto& line : view.rows) { CHECK(text::utf8DisplayWidth(line) <= 32U); full += line; }
    if (!view.enabled[1]) break;
    CHECK(controller.replacementReviewAction(1U));
  }
  CHECK(full.find("Scope: global or unspecified; no note binding") != std::string::npos);
  CHECK(full.find(issue.detail) != std::string::npos);
  CHECK(full.find("Relink media") != std::string::npos); CHECK(!controller.replacementReviewAction(3U));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Escape})); CHECK(controller.sceneState().replacementReview.rowsInspectable);
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Escape})); CHECK(!controller.replacementReviewOpen());
  CHECK(actions == 0U); CHECK(changes == 0U); CHECK(session.selection().empty()); CHECK(session.project() == source);
}

TEST_CASE("native diagnostic Find pages opaque references and rejects changed diagnostics or documents") {
  using namespace seam;
  NativeUiFixture fixture;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}}};
  std::vector<authoring::Diagnostic> diagnostics;
  std::string longId; for (unsigned i = 0U; i < 200U; ++i) longId += "媒体";
  for (unsigned i = 0U; i < 7U; ++i) diagnostics.push_back({.code = "MEDIA_MISSING", .messageKey = "media.missing",
      .affectedIds = {i == 6U ? longId : "opaque-" + std::to_string(i)}, .actions = {authoring::DiagnosticAction::RelinkMedia}});
  controller.setDiagnostics(diagnostics);
  CHECK(controller.openDiagnosticFindReview("MEDIA")); waitForNativeFind(controller);
  CHECK(controller.sceneState().replacementReview.rows.size() == 6U);
  controller.rebuildAccessibilityTree(); const auto oldRow = controller.accessibilityTree().root().children.front().id;
  CHECK(controller.replacementReviewAction(1U)); CHECK(!controller.dispatchAccessibility(oldRow, native_ui::SemanticAction::Activate));
  CHECK(controller.openReplacementRow(0U));
  std::string full;
  for (unsigned page = 0U; page < 100U; ++page) {
    const auto view = controller.sceneState().replacementReview; for (const auto& line : view.rows) full += line;
    if (!view.enabled[1]) break;
    CHECK(controller.replacementReviewAction(1U));
  }
  CHECK(full.find(longId) != std::string::npos); CHECK(full.find("not note targets") != std::string::npos);
  CHECK(controller.replacementReviewAction(4U));
  diagnostics.back().occurrenceCount = 2U; controller.setDiagnostics(diagnostics);
  CHECK(!controller.sceneState().replacementReview.rowsInspectable); CHECK(!controller.openReplacementRow(0U));
  CHECK(controller.replacementReviewAction(5U)); waitForNativeFind(controller); CHECK(controller.sceneState().replacementReview.rowsInspectable);
  const auto source = fixture.session.project(); CHECK(fixture.session.replaceProject(source));
  CHECK(!controller.openReplacementRow(0U)); CHECK(controller.replacementReviewAction(4U));
  CHECK(controller.openDiagnosticFindReview("MEDIA")); CHECK(controller.replacementReviewAction(4U));
  waitForNativeFind(controller); CHECK(!controller.replacementReviewOpen()); CHECK(fixture.session.selection().empty());
  CHECK(fixture.session.project() == source);
}

TEST_CASE("native derived Find searches 10000 Japanese notes and reveals the hinted final note") {
  using namespace seam;
  NativeUiFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  region->notes.clear(); region->lyrics.clear(); region->durationTick = time::Tick{20000};
  domain::NoteId target;
  for (int i = 0; i < 10000; ++i) {
    auto [lyric, note] = fixture.factory.makeNote(time::Tick{i}, time::Tick{1}, 60U, U"か", domain::Language::Japanese);
    if (i == 9999) { note.phoneticHint = "u"; target = note.id; }
    region->lyrics.push_back(lyric); region->notes.push_back(note);
  }
  const auto source = fixture.session.project(); unsigned changes = 0U;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.documentChanged = [&] { ++changes; }}};
  controller.resize(480.0, 320.0);
  CHECK(controller.openFindReview("u", ui::NoteSearchField::GeneratedPhoneme)); waitForNativeFind(controller);
  CHECK(controller.sceneState().replacementReview.summary.starts_with("1 matching notes"));
  CHECK(controller.openReplacementRow(0U)); CHECK(controller.sceneState().replacementReview.rows.front() == "u");
  CHECK(controller.replacementReviewAction(3U));
  CHECK(fixture.session.selection().noteIds() == std::vector<domain::NoteId>{target});
  CHECK(controller.pianoRoll().timeline().originTick() == time::Tick{9999});
  CHECK(controller.accessibilityTree().focusedNode()->id == "note." + target.toString());
  CHECK(fixture.session.project() == source); CHECK(fixture.session.revision() == 0U); CHECK(changes == 0U);
}

TEST_CASE("native Find preparation keeps Close available and never revives cancelled or stale results") {
  using namespace seam;
  NativeUiFixture fixture;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  const auto source = fixture.session.project();
  CHECK(controller.openFindReview("edge")); CHECK(controller.findPreparing());
  auto view = controller.sceneState().replacementReview; CHECK(view.status == "Preparing Find results...");
  CHECK(std::string_view{view.labels[3]} == "Inspect result");
  CHECK(view.enabled[4]); CHECK(!view.enabled[2]); CHECK(!view.enabled[3]); CHECK(!view.enabled[5]);
  if (const auto* capture = std::getenv("SEAM_FIND_PENDING_CAPTURE")) {
    controller.resize(480.0, 320.0);
    auto engine = text::TextEngine::createSystem(); CHECK(engine);
    native_ui::PixelSurface surface{480U,320U}; native_ui::RasterCanvas canvas{surface,1.0,engine.value().get()};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState()); CHECK(surface.writePpm(capture));
  }
  controller.rebuildAccessibilityTree();
  const auto pendingStatus = controller.accessibilityTree().root().children.back().id;
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Escape})); CHECK(!controller.replacementReviewOpen());
  CHECK(!controller.openFindReview("edge")); // Retire this exact worker, do not restart over it.
  waitForNativeFind(controller); CHECK(!controller.replacementReviewOpen()); CHECK(!controller.repeatFind());
  CHECK(fixture.session.project() == source); CHECK(fixture.session.selection().empty());
  CHECK(controller.openFindReview("edge"));
  CHECK(fixture.session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{0}, 90.0)));
  waitForNativeFind(controller); view = controller.sceneState().replacementReview;
  CHECK(view.status.find("source changed") != std::string::npos); CHECK(!view.enabled[3]); CHECK(view.enabled[5]);
  CHECK(!controller.dispatchAccessibility(pendingStatus, native_ui::SemanticAction::SetFocus));
  CHECK(controller.replacementReviewAction(5U)); waitForNativeFind(controller);
  CHECK(controller.sceneState().replacementReview.enabled[3]); CHECK(controller.replacementReviewAction(4U));
}

TEST_CASE("native repeat Find wraps reveals and focuses matches while guarding composition and source drift") {
  using namespace seam;
  NativeUiFixture fixture; unsigned changes = 0U;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  auto [lyric, note] = fixture.factory.makeNote(time::Tick{4800}, time::Tick{480}, 30U, U"edge", domain::Language::English);
  region->lyrics.push_back(lyric); region->notes.push_back(note);
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}, .documentChanged = [&] { ++changes; }}};
  controller.resize(480.0, 320.0);
  CHECK(!controller.repeatFind());
  CHECK(controller.openFindReview("edge")); CHECK(!controller.repeatFind()); waitForNativeFind(controller);
  CHECK(controller.openReplacementRow(0U)); CHECK(controller.replacementReviewAction(3U));
  const auto source = fixture.session.project();
  const auto checkSelected = [&](domain::NoteId expected) {
    CHECK(fixture.session.selection().noteIds() == std::vector<domain::NoteId>{expected});
    CHECK(controller.accessibilityTree().focusedNode());
    CHECK(controller.accessibilityTree().focusedNode()->id == "note." + expected.toString());
    const auto visible = controller.pianoRoll().visibleNotes();
    CHECK(std::any_of(visible.begin(), visible.end(), [&](const auto& visual) { return visual.noteId == expected; }));
  };
  checkSelected(fixture.noteId);
  CHECK(controller.repeatFind()); checkSelected(note.id);
  CHECK(controller.repeatFind()); checkSelected(fixture.noteId);
  CHECK(controller.repeatFind(true)); checkSelected(note.id);
  CHECK(controller.beginLyricEdit(note.id)); CHECK(!controller.repeatFind());
  CHECK(fixture.session.selection().noteIds() == std::vector<domain::NoteId>{note.id});
  controller.cancelTextComposition(); CHECK(controller.repeatFind()); checkSelected(fixture.noteId);
  CHECK(fixture.session.project() == source); CHECK(fixture.session.revision() == 0U); CHECK(changes == 0U);
  CHECK(fixture.session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{0}, 90.0)));
  CHECK(!controller.repeatFind()); checkSelected(fixture.noteId);
  CHECK(controller.openFindReview("edge")); waitForNativeFind(controller); CHECK(controller.openReplacementRow(1U)); CHECK(controller.replacementReviewAction(3U));
  const auto same = fixture.session.project(); CHECK(fixture.session.replaceProject(same));
  const auto selection = fixture.session.selection().noteIds(); CHECK(!controller.repeatFind(true));
  CHECK(fixture.session.selection().noteIds() == selection);
  CHECK(controller.openFindReview("edge")); CHECK(controller.replacementReviewAction(4U)); CHECK(!controller.repeatFind());
  CHECK(changes == 0U);
}

TEST_CASE("native Find input fields and complete result inspection select without changing the song") {
  using namespace seam;
  NativeUiFixture fixture; unsigned changes = 0U;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  region->lyrics.front().surface = std::u32string(400U, U'歌');
  const auto source = fixture.session.project();
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}, .documentChanged = [&] { ++changes; }}};
  controller.resize(480.0, 320.0);
  CHECK(controller.beginFindInput()); CHECK(controller.sceneState().boundedInputLabel.starts_with("FIND NOTES"));
  CHECK(!controller.commitTextComposition(U"")); CHECK(controller.textInputActive());
  CHECK(controller.commitTextComposition(U"歌")); CHECK(!controller.textInputActive());
  waitForNativeFind(controller);
  CHECK(controller.sceneState().replacementReview.rows.size() == 1U); CHECK(fixture.session.selection().empty());
  controller.rebuildAccessibilityTree(); const auto row = controller.accessibilityTree().root().children.front();
  CHECK(std::find(row.actions.begin(), row.actions.end(), native_ui::SemanticAction::SetFocus) != row.actions.end());
  CHECK(controller.dispatchAccessibility(row.id, native_ui::SemanticAction::SetFocus));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Enter}));
  CHECK(!controller.sceneState().replacementReview.rowsInspectable);
  CHECK(controller.accessibilityTree().focusedNode()->id.ends_with("action.4"));
  std::string reconstructed;
  for (unsigned page = 0U; page < 100U; ++page) {
    const auto view = controller.sceneState().replacementReview;
    for (const auto& line : view.rows) { CHECK(text::utf8DisplayWidth(line) <= 32U); reconstructed += line; }
    if (!view.enabled[1]) break;
    CHECK(controller.replacementReviewAction(1U));
  }
  CHECK(reconstructed == domain::toUtf8(region->lyrics.front().surface));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Escape}));
  CHECK(controller.sceneState().replacementReview.rowsInspectable); CHECK(fixture.session.selection().empty());
  CHECK(controller.openReplacementRow(0U));
  if (const auto* capture = std::getenv("SEAM_FIND_CAPTURE")) {
    auto engine = text::TextEngine::createSystem(); CHECK(engine);
    native_ui::PixelSurface surface{480U,320U}; native_ui::RasterCanvas canvas{surface,1.0,engine.value().get()};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState()); CHECK(surface.writePpm(capture));
  }
  CHECK(controller.replacementReviewAction(3U)); CHECK(!controller.replacementReviewOpen());
  CHECK(fixture.session.selection().noteIds() == std::vector<domain::NoteId>{fixture.noteId});
  CHECK(controller.pianoRoll().timeline().originTick() == region->notes.front().startTick);
  CHECK(!controller.pianoRoll().visibleNotes().empty());
  CHECK(controller.accessibilityTree().focusedNode());
  CHECK(controller.accessibilityTree().focusedNode()->id == "note." + fixture.noteId.toString());
  CHECK(fixture.session.project() == source); CHECK(fixture.session.revision() == 0U); CHECK(changes == 0U);
  CHECK(!controller.dispatchAccessibility(row.id, native_ui::SemanticAction::Activate));
  CHECK(controller.openFindReview(fixture.noteId.toString())); // Start in lyrics, then choose fields with the same query.
  waitForNativeFind(controller);
  CHECK(controller.replacementReviewAction(2U)); waitForNativeFind(controller); CHECK(controller.sceneState().replacementReview.status == "Find: Hints");
  CHECK(controller.replacementReviewAction(2U)); waitForNativeFind(controller); CHECK(controller.sceneState().replacementReview.status == "Find: Note IDs");
  CHECK(controller.sceneState().replacementReview.rows.size() == 1U);
  CHECK(controller.replacementReviewAction(2U)); waitForNativeFind(controller); CHECK(controller.sceneState().replacementReview.status == "Find: Resolved phones (Japanese)");
  CHECK(controller.replacementReviewAction(2U)); waitForNativeFind(controller); CHECK(controller.sceneState().replacementReview.status == "Find: Pronunciation warnings (Japanese)");
  CHECK(controller.replacementReviewAction(2U)); waitForNativeFind(controller); CHECK(controller.sceneState().replacementReview.status == "Find: Active diagnostics");
  CHECK(controller.replacementReviewAction(2U)); waitForNativeFind(controller); CHECK(controller.sceneState().replacementReview.status == "Find: Lyrics");
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Escape})); CHECK(!controller.replacementReviewOpen());
}

TEST_CASE("native Find pages rejects stale actions and keeps errors distinct from empty results") {
  using namespace seam;
  NativeUiFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  for (int i = 0; i < 7; ++i) {
    auto [lyric, note] = fixture.factory.makeNote(time::Tick{2400 + i * 480}, time::Tick{240}, 64U, U"edge", domain::Language::English);
    region->lyrics.push_back(lyric); region->notes.push_back(note);
  }
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}}};
  CHECK(controller.openFindReview("edge")); waitForNativeFind(controller); CHECK(controller.sceneState().replacementReview.rows.size() == 6U);
  controller.rebuildAccessibilityTree(); const auto old = controller.accessibilityTree().root().children.front().id;
  CHECK(controller.replacementReviewAction(1U)); CHECK(controller.sceneState().replacementReview.rows.size() == 2U);
  CHECK(!controller.dispatchAccessibility(old, native_ui::SemanticAction::Activate));
  CHECK(controller.openReplacementRow(1U));
  CHECK(fixture.session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{0}, 90.0)));
  CHECK(!controller.sceneState().replacementReview.enabled[3]); CHECK(!controller.replacementReviewAction(3U));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Escape})); CHECK(fixture.session.selection().empty());
  CHECK(controller.replacementReviewAction(5U)); waitForNativeFind(controller); CHECK(controller.sceneState().replacementReview.enabled[3]);
  const auto source = fixture.session.project(); CHECK(controller.keyDown({.key = native_ui::NativeKey::Delete}));
  CHECK(fixture.session.project() == source);
  CHECK(controller.replacementReviewAction(4U)); CHECK(controller.beginFindInput());
  CHECK(fixture.session.replaceProject(source)); CHECK(!controller.commitTextComposition(U"edge"));
  CHECK(!controller.replacementReviewOpen());
  CHECK(!controller.openFindReview("edge", static_cast<ui::NoteSearchField>(99)));
  CHECK(controller.sceneState().replacementReview.status.find("Invalid") != std::string::npos);
  CHECK(!controller.sceneState().replacementReview.enabled[3]);
  CHECK(controller.replacementReviewAction(4U));
  CHECK(controller.openFindReview("absent")); waitForNativeFind(controller); CHECK(controller.sceneState().replacementReview.rows.front() == "No matching notes in this field");
  CHECK(controller.replacementReviewAction(4U));
}

TEST_CASE("native find replace input separates stages and requires reviewed application") {
  using namespace seam;
  NativeUiFixture fixture;
  std::vector<std::u32string> opened;
  unsigned changes = 0U;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [&](const native_ui::TextInputRequest& request) { opened.push_back(request.currentText); },
       .documentChanged = [&] { ++changes; }}};
  const auto field = [&] {
    controller.rebuildAccessibilityTree(); return controller.accessibilityTree().root().children.front().id;
  };
  CHECK(controller.beginReplacementInput());
  CHECK(controller.sceneState().boundedInputLabel == "FIND LYRIC (LITERAL)");
  const auto query = field();
  CHECK(!controller.setAccessibilityValue(query, "")); CHECK(controller.textInputActive());
  CHECK(controller.sceneState().boundedInputLabel == "ENTER A NONEMPTY QUERY");
  CHECK(controller.setAccessibilityValue(field(), "ed"));
  CHECK(controller.textInputActive()); CHECK(opened.back().empty());
  CHECK(controller.sceneState().boundedInputLabel == "REPLACE WITH (EMPTY ALLOWED)");
  CHECK(!controller.setAccessibilityValue(query, "wrong stage"));
  CHECK(controller.setAccessibilityValue(field(), ""));
  CHECK(!controller.textInputActive()); CHECK(controller.sceneState().replacementReview.visible);
  CHECK(changes == 0U); CHECK(fixture.session.revision() == 0U);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (!controller.sceneState().replacementReview.enabled[3] && std::chrono::steady_clock::now() < deadline) {
    controller.pollReplacementReview(); std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  CHECK(controller.replacementReviewAction(3U)); CHECK(changes == 1U);
  CHECK(fixture.session.project().findRegion(fixture.regionId)->findLyric(fixture.lyricId)->surface == U"ge");
  CHECK(fixture.session.undo());
  CHECK(controller.beginReplacementInput()); const auto cancelled = field(); controller.cancelTextComposition();
  CHECK(!controller.setAccessibilityValue(cancelled, "edge"));
  CHECK(controller.beginReplacementInput()); CHECK(controller.commitTextComposition(U"ed"));
  controller.cancelTextComposition(); CHECK(!controller.textInputActive()); CHECK(!controller.replacementReviewOpen());
  CHECK(controller.beginReplacementInput());
  CHECK(!controller.commitTextComposition(std::u32string(257U, U'x'))); CHECK(controller.textInputActive());
  controller.cancelTextComposition();
  CHECK(controller.beginReplacementInput()); CHECK(controller.commitTextComposition(U"ed"));
  CHECK(fixture.session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{0}, 90.0)));
  CHECK(!controller.commitTextComposition(U"a")); CHECK(!controller.replacementReviewOpen());
  CHECK(controller.beginReplacementInput()); CHECK(controller.beginLyricEdit(fixture.noteId));
  CHECK(controller.commitTextComposition(U"word"));
  CHECK(!controller.sceneState().replacementReview.visible);
  CHECK(fixture.session.project().findRegion(fixture.regionId)->findLyric(fixture.lyricId)->surface == U"word");
}

TEST_CASE("replacement detail exposes every before and after byte and never applies from detail") {
  using namespace seam;
  NativeUiFixture fixture;
  const std::u32string original(400U, U'a');
  fixture.session.project().findRegion(fixture.regionId)->findLyric(fixture.lyricId)->surface = original;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId};
  controller.resize(720.0, 520.0);
  CHECK(controller.openReplacementReview("a", "日本"));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (!controller.sceneState().replacementReview.enabled[3] && std::chrono::steady_clock::now() < deadline) {
    controller.pollReplacementReview(); std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  CHECK(controller.sceneState().replacementReview.enabled[3]);
  controller.rebuildAccessibilityTree();
  const auto listPrefix = controller.accessibilityTree().root().id;
  const auto rowId = controller.accessibilityTree().root().children.front().id;
  std::string applyId;
  for (const auto& node : controller.accessibilityTree().root().children) if (node.id.ends_with("action.3")) applyId = node.id;
  CHECK(controller.dispatchAccessibility(rowId, native_ui::SemanticAction::Activate));
  CHECK(!controller.dispatchAccessibility(applyId, native_ui::SemanticAction::Activate));
  CHECK(!controller.sceneState().replacementReview.rowsInspectable);
  const auto collect = [&] {
    std::string value;
    for (std::size_t pages = 0U; pages < 100U; ++pages) {
      const auto view = controller.sceneState().replacementReview;
      for (const auto& row : view.rows) { CHECK(text::utf8DisplayWidth(row) <= 32U); value += row; }
      if (!view.enabled[1]) return value;
      CHECK(controller.replacementReviewAction(1U));
    }
    throw test::Failure{"Replacement detail exceeded expected page count"};
  };
  CHECK(collect() == domain::toUtf8(original));
  CHECK(controller.replacementReviewAction(2U));
  CHECK(controller.sceneState().replacementReview.summary.find("AFTER") != std::string::npos);
  if (const auto* capture = std::getenv("SEAM_REPLACEMENT_DETAIL_CAPTURE")) {
    auto engine = text::TextEngine::createSystem(); CHECK(engine);
    native_ui::PixelSurface surface{720U, 520U}; native_ui::RasterCanvas canvas{surface, 1.0, engine.value().get()};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState());
    CHECK(surface.writePpm(capture));
  }
  std::string expected; for (int i = 0; i < 400; ++i) expected += "日本";
  CHECK(collect() == expected); CHECK(fixture.session.revision() == 0U);
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Escape}));
  CHECK(controller.sceneState().replacementReview.rowsInspectable);
  CHECK(controller.replacementReviewAction(3U)); CHECK(fixture.session.revision() == 1U);
  CHECK(domain::toUtf8(fixture.session.project().findRegion(fixture.regionId)->findLyric(fixture.lyricId)->surface) == expected);
  CHECK(fixture.session.undo());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->findLyric(fixture.lyricId)->surface == original);
  CHECK(!listPrefix.empty());
}

TEST_CASE("batch lyric input captures targets and rejects selection document and generation drift") {
  using namespace seam;
  NativeUiFixture fixture;
  auto [lyric, note] = fixture.factory.makeNote(time::Tick{2160}, time::Tick{960}, 65U, U"old", domain::Language::English);
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  region->lyrics.push_back(lyric); region->notes.push_back(note); region->sortNotes();
  unsigned changes = 0U;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}, .documentChanged = [&] { ++changes; }}};
  const auto begin = [&] {
    fixture.session.selection().replace({fixture.noteId, note.id});
    return controller.keyDown({.key = native_ui::NativeKey::L, .modifiers = {.shift = true}});
  };
  const auto before = fixture.session.project();
  CHECK(begin());
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Delete}));
  CHECK(fixture.session.project() == before);
  fixture.session.selection().selectOnly(note.id);
  CHECK(!controller.commitTextComposition(U"あ")); CHECK(!controller.textInputActive());
  CHECK(fixture.session.project() == before); CHECK(changes == 0U); CHECK(!fixture.session.canUndo());
  CHECK(begin());
  CHECK(fixture.session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{0}, 100.0)));
  CHECK(!controller.commitTextComposition(U"あ い")); CHECK(changes == 0U);
  CHECK(begin()); const auto identical = fixture.session.project();
  CHECK(fixture.session.replaceProject(identical));
  fixture.session.selection().replace({fixture.noteId, note.id});
  CHECK(!controller.commitTextComposition(U"あ い")); CHECK(fixture.session.project() == identical);
  CHECK(begin()); fixture.session.project().findNote(note.id)->phoneticHint = "old metadata";
  CHECK(!controller.commitTextComposition(U"あ い")); CHECK(changes == 0U);
  fixture.session.project().findNote(note.id)->phoneticHint.reset();
  CHECK(begin()); fixture.session.selection().replace({note.id, fixture.noteId});
  const auto priorCommit = fixture.session.project();
  CHECK(controller.updateTextComposition(U"あ い", {}));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Tab}));
  CHECK(!controller.textInputActive()); CHECK(changes == 0U);
  applyReadyLyricReview(controller); CHECK(changes == 1U);
  CHECK(fixture.session.undo()); CHECK(fixture.session.project() == priorCommit);
  CHECK(begin()); controller.cancelTextComposition(); CHECK(!controller.textInputActive());
  fixture.session.selection().add(domain::NoteId{999999U});
  CHECK(!controller.keyDown({.key = native_ui::NativeKey::L, .modifiers = {.shift = true}}));
  CHECK(changes == 1U);
}

TEST_CASE("native distribution no-op preserves language revision and change notifications") {
  using namespace seam;
  NativeUiFixture fixture; fixture.session.selection().selectOnly(fixture.noteId);
  unsigned changes = 0U;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.documentChanged = [&] { ++changes; }}};
  const auto unchanged = controller.distributeSelectedLyrics(U"edge"); CHECK(unchanged);
  CHECK(unchanged.value().committed); CHECK(unchanged.value().changedLyrics == 0U);
  CHECK(fixture.session.revision() == 0U); CHECK(changes == 0U); CHECK(!fixture.session.canUndo());
  CHECK(controller.distributeSelectedLyrics(U"new")); CHECK(changes == 1U);
  CHECK(fixture.session.project().findRegion(fixture.regionId)->findLyric(fixture.lyricId)->language == domain::Language::English);
  CHECK(fixture.session.undo());
}

TEST_CASE("distribution mismatch stays in review with visible counts and no apply") {
  using namespace seam;
  NativeUiFixture fixture; fixture.session.selection().selectOnly(fixture.noteId);
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}}};
  CHECK(controller.keyDown({.key = native_ui::NativeKey::L, .modifiers = {.shift = true}}));
  CHECK(controller.commitTextComposition(U"too many"));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (controller.sceneState().replacementReview.status.starts_with("Preparing") && std::chrono::steady_clock::now() < deadline) {
    controller.pollReplacementReview(); std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  const auto view = controller.sceneState().replacementReview;
  CHECK(view.visible); CHECK(!view.enabled[3]); CHECK(view.summary == "requested=2, target=1");
  controller.rebuildAccessibilityTree();
  const auto& status = controller.accessibilityTree().root().children.back();
  CHECK(status.name == "Review status and counts"); CHECK(status.value.find("requested=2, target=1") != std::string::npos);
  CHECK(fixture.session.revision() == 0U); CHECK(!fixture.session.canUndo());
  CHECK(controller.replacementReviewAction(4U)); CHECK(!controller.replacementReviewOpen());
}

TEST_CASE("native clear vibrato review pages guards selection and commits only on Apply") {
  using namespace seam;
  NativeUiFixture fixture;
  fixture.session.project().findNote(fixture.noteId)->vibrato.enabled = true;
  fixture.session.selection().selectOnly(fixture.noteId);
  for (int i = 0; i < 6; ++i) {
    auto [lyric, note] = fixture.factory.makeNote(time::Tick{i * 480}, time::Tick{480}, 62U, U"la", domain::Language::English);
    note.vibrato.enabled = true; fixture.session.selection().add(note.id);
    auto* region = fixture.session.project().findRegion(fixture.regionId);
    region->lyrics.push_back(lyric); region->notes.push_back(note);
  }
  fixture.session.project().findRegion(fixture.regionId)->sortNotes();
  unsigned changes = 0U;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.documentChanged = [&] { ++changes; }}};
  controller.resize(720.0, 520.0);
  const auto before = fixture.session.project();
  if (const auto* projectPath = std::getenv("SEAM_CLEAR_VIBRATO_PROJECT")) {
    formats::ProjectJsonCodec codec; CHECK(codec.save(before, projectPath));
  }
  CHECK(controller.openClearVibratoReview()); CHECK(fixture.session.project() == before); CHECK(changes == 0U);
  auto view = controller.sceneState().replacementReview;
  CHECK(view.rows.size() == 6U); CHECK(view.enabled[3]); CHECK(std::string_view{view.labels[3]} == "Clear vibrato");
  controller.rebuildAccessibilityTree();
  std::string oldApply;
  for (const auto& node : controller.accessibilityTree().root().children) if (node.id.ends_with("action.3")) oldApply = node.id;
  CHECK(controller.replacementReviewAction(1U)); CHECK(controller.sceneState().replacementReview.rows.size() == 1U);
  CHECK(!controller.dispatchAccessibility(oldApply, native_ui::SemanticAction::Activate));
  fixture.session.selection().selectOnly(fixture.noteId);
  CHECK(!controller.sceneState().replacementReview.enabled[3]); CHECK(!controller.replacementReviewAction(3U));
  CHECK(controller.replacementReviewAction(5U));
  if (const auto* capture = std::getenv("SEAM_CLEAR_VIBRATO_CAPTURE")) {
    auto engine = text::TextEngine::createSystem(); CHECK(engine);
    native_ui::PixelSurface surface{720U, 520U}; native_ui::RasterCanvas canvas{surface, 1.0, engine.value().get()};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState()); CHECK(surface.writePpm(capture));
  }
  CHECK(controller.replacementReviewAction(3U)); CHECK(changes == 1U); CHECK(!controller.replacementReviewOpen());
  auto expected = before; expected.findNote(fixture.noteId)->vibrato.enabled = false;
  CHECK(fixture.session.project() == expected); CHECK(fixture.session.undo()); CHECK(fixture.session.project() == before);
  CHECK(controller.openClearVibratoReview()); CHECK(controller.replacementReviewAction(4U)); CHECK(fixture.session.project() == before);
  fixture.session.project().findNote(fixture.noteId)->vibrato.enabled = false;
  CHECK(controller.openClearVibratoReview()); CHECK(!controller.sceneState().replacementReview.enabled[3]);
  CHECK(controller.replacementReviewAction(4U)); CHECK(changes == 1U);
}

TEST_CASE("native cleanup review shows outcomes guards grid changes and applies exactly once") {
  using namespace seam;
  for (const auto kind : {ui::NoteCleanupKind::RemoveOverlap, ui::NoteCleanupKind::CloseGap, ui::NoteCleanupKind::AutoLegato}) {
    NativeUiFixture fixture;
    fixture.session.project().settings().snapGrid = time::Tick{240};
    fixture.session.project().findNote(fixture.noteId)->durationTick = kind == ui::NoteCleanupKind::RemoveOverlap ? time::Tick{1440} : time::Tick{960};
    auto [lyric, note] = fixture.factory.makeNote(time::Tick{2160}, time::Tick{960}, 65U, U"la", domain::Language::English);
    auto* region = fixture.session.project().findRegion(fixture.regionId);
    region->lyrics.push_back(lyric); region->notes.push_back(note); region->sortNotes();
    fixture.session.selection().replace({fixture.noteId, note.id});
    unsigned changes = 0U;
    native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
        {.documentChanged = [&] { ++changes; }}};
    controller.resize(720.0, 520.0); const auto before = fixture.session.project();
    CHECK(controller.openNoteCleanupReview(kind)); CHECK(fixture.session.project() == before); CHECK(changes == 0U);
    auto view = controller.sceneState().replacementReview;
    CHECK(view.rows.size() == 2U); CHECK(view.rows.back().find("no successor") != std::string::npos);
    CHECK(view.enabled[3]); CHECK(!controller.beginTempoEdit());
    controller.rebuildAccessibilityTree(); std::string oldApply;
    for (const auto& nodeValue : controller.accessibilityTree().root().children) if (nodeValue.id.ends_with("action.3")) oldApply = nodeValue.id;
    fixture.session.project().settings().snapGrid = time::Tick{480};
    CHECK(!controller.sceneState().replacementReview.enabled[3]);
    CHECK(!controller.dispatchAccessibility(oldApply, native_ui::SemanticAction::Activate));
    fixture.session.project().settings().snapGrid = time::Tick{240};
    CHECK(controller.replacementReviewAction(5U));
    CHECK(!controller.dispatchAccessibility(oldApply, native_ui::SemanticAction::Activate));
    if (const auto* capture = std::getenv("SEAM_NOTE_CLEANUP_CAPTURE_DIR")) {
      const auto name = kind == ui::NoteCleanupKind::RemoveOverlap ? "overlap" : (kind == ui::NoteCleanupKind::CloseGap ? "gap" : "legato");
      const std::filesystem::path root{capture}; formats::ProjectJsonCodec codec;
      CHECK(codec.save(before, root / (std::string{name} + ".seam")));
      auto engine = text::TextEngine::createSystem(); CHECK(engine);
      native_ui::PixelSurface surface{720U, 520U}; native_ui::RasterCanvas canvas{surface, 1.0, engine.value().get()};
      native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState());
      CHECK(surface.writePpm(root / (std::string{name} + ".ppm")));
    }
    CHECK(controller.replacementReviewAction(2U));
    CHECK(controller.sceneState().replacementReview.rows.front() == "No retained dependency records");
    CHECK(controller.replacementReviewAction(3U)); CHECK(changes == 1U); CHECK(!controller.replacementReviewOpen());
    CHECK(fixture.session.project().findNote(fixture.noteId)->startTick == time::Tick{960});
    CHECK(fixture.session.project().findNote(fixture.noteId)->durationTick == time::Tick{1200});
    if (kind == ui::NoteCleanupKind::AutoLegato) {
      CHECK(fixture.session.project().findNote(fixture.noteId)->articulation == domain::NoteArticulation::Legato);
      CHECK(fixture.session.project().findNote(note.id)->articulation == domain::NoteArticulation::Legato);
    }
    CHECK(controller.openNoteCleanupReview(kind)); CHECK(!controller.sceneState().replacementReview.enabled[3]);
    CHECK(controller.replacementReviewAction(4U));
    CHECK(fixture.session.undo()); CHECK(fixture.session.project() == before);
    CHECK(controller.openNoteCleanupReview(kind)); CHECK(controller.keyDown({.key = native_ui::NativeKey::Delete}));
    CHECK(fixture.session.project() == before); CHECK(controller.replacementReviewAction(4U)); CHECK(changes == 1U);
  }
}

TEST_CASE("shared review rows actions and text input fit supported short windows") {
  using namespace seam;
  const native_ui::EditorSceneLayout layout;
  for (const auto size : std::vector<ui::Point>{{480.0,320.0},{640.0,360.0},{720.0,520.0},{960.0,640.0}}) {
    const auto panel = layout.timeMapPanelBounds(size.x, size.y);
    CHECK(panel.x >= 0.0); CHECK(panel.y >= 0.0); CHECK(panel.right() <= size.x); CHECK(panel.bottom() <= size.y);
    std::vector<ui::Rect> controls;
    for (std::size_t i = 0U; i < 6U; ++i) {
      const auto row = layout.timeMapRowBounds(size.x, size.y, i);
      CHECK(row.height >= 22.0); controls.push_back(row);
    }
    for (std::size_t i = 0U; i < 8U; ++i) controls.push_back(layout.timeMapActionBounds(size.x, size.y, i));
    for (std::size_t i = 0U; i < controls.size(); ++i) {
      const auto& bounds = controls[i]; CHECK(bounds.x >= panel.x); CHECK(bounds.y >= panel.y);
      CHECK(bounds.right() <= panel.right()); CHECK(bounds.bottom() <= panel.bottom());
      for (std::size_t j = i + 1U; j < controls.size(); ++j) CHECK(!bounds.intersects(controls[j]));
    }
    const auto input = layout.timeMapTextBounds(size.x, size.y, true);
    CHECK(input.height >= 22.0); CHECK(input.bottom() <= controls[1].y);
  }
  NativeUiFixture fixture; fixture.session.selection().selectOnly(fixture.noteId);
  fixture.session.project().findNote(fixture.noteId)->vibrato.enabled = true;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId};
  controller.resize(480.0, 320.0); CHECK(controller.openClearVibratoReview());
  controller.rebuildAccessibilityTree();
  for (const auto& node : controller.accessibilityTree().root().children) {
    CHECK(node.bounds.x >= 0.0); CHECK(node.bounds.y >= 0.0);
    CHECK(node.bounds.right() <= 480.0); CHECK(node.bounds.bottom() <= 320.0);
  }
  if (const auto* capture = std::getenv("SEAM_SHORT_REVIEW_CAPTURE")) {
    auto engine = text::TextEngine::createSystem(); CHECK(engine);
    native_ui::PixelSurface surface{480U,320U}; native_ui::RasterCanvas canvas{surface,1.0,engine.value().get()};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState()); CHECK(surface.writePpm(capture));
  }
  const auto cancel = layout.timeMapActionBounds(480.0,320.0,4U);
  CHECK(controller.pointerDown({.position={cancel.x + 2.0,cancel.y + 2.0},.button=native_ui::PointerButton::Left}));
  CHECK(!controller.replacementReviewOpen()); CHECK(fixture.session.revision() == 0U);
}

TEST_CASE("native dynamics clear explicitly reviews the entire region and preserves other expressions") {
  using namespace seam;
  NativeUiFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  for (int i = 0; i < 7; ++i) CHECK(region->dynamicsAutomation.upsert({time::Tick{i * 240}, 0.2F + static_cast<float>(i) * 0.1F}));
  region->notes.front().vibrato.enabled = true;
  auto [lyric, note] = fixture.factory.makeNote(time::Tick{2160}, time::Tick{480}, 65U, U"la", domain::Language::English);
  region->lyrics.push_back(lyric); region->notes.push_back(note); region->sortNotes();
  const auto pronunciation = phonemizer::resolveJapanesePronunciation(*region); CHECK(pronunciation);
  region->performance.pronunciation = pronunciation.value().identity;
  region->performance.takes = {{.id = "retained-dynamics", .sourceRegionId = fixture.regionId,
      .capturedRevision = region->performance.revision,
      .resource = {domain::SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
      .pronunciation = pronunciation.value().identity, .generatorId = "fixture", .generatorVersion = "1",
      .range = {time::Tick{960}, time::Tick{1920}},
      .lanes = {{domain::PerformanceChannel::Dynamics, {{time::Tick{960}, 0.7}}}}}};
  region->performance.accepted = {{"retained-dynamics", domain::PerformanceChannel::Dynamics, fixture.noteId, time::Tick{0}}};
  fixture.session.selection().selectOnly(fixture.noteId);
  unsigned changes = 0U;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.documentChanged = [&] { ++changes; }}};
  controller.resize(480.0, 320.0); const auto before = fixture.session.project();
  CHECK(controller.openClearDynamicsReview());
  auto view = controller.sceneState().replacementReview;
  CHECK(view.status.find("ENTIRE region") != std::string::npos); CHECK(view.summary.starts_with("2 region notes / 7 points"));
  CHECK(view.status.find("generated dynamics may still apply") != std::string::npos);
  CHECK(view.summary.find("1 generated selections retained") != std::string::npos);
  CHECK(view.rows.size() == 6U); CHECK(view.enabled[3]); CHECK(fixture.session.project() == before);
  CHECK(controller.replacementReviewAction(1U)); CHECK(controller.sceneState().replacementReview.rows.size() == 1U);
  fixture.session.selection().clear(); CHECK(controller.sceneState().replacementReview.enabled[3]); // Explicit region scope.
  CHECK(fixture.session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{0}, 90.0)));
  CHECK(!controller.sceneState().replacementReview.enabled[3]); CHECK(controller.replacementReviewAction(5U));
  const auto source = fixture.session.project();
  if (const auto* capture = std::getenv("SEAM_DYNAMICS_CLEAR_CAPTURE")) {
    auto engine = text::TextEngine::createSystem(); CHECK(engine);
    native_ui::PixelSurface surface{480U,320U}; native_ui::RasterCanvas canvas{surface,1.0,engine.value().get()};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState()); CHECK(surface.writePpm(capture));
  }
  CHECK(controller.replacementReviewAction(3U)); CHECK(changes == 1U);
  auto expected = source; expected.findRegion(fixture.regionId)->dynamicsAutomation = {};
  CHECK(fixture.session.project() == expected); CHECK(fixture.session.undo()); CHECK(fixture.session.project() == source);
  CHECK(controller.openClearDynamicsReview()); CHECK(controller.replacementReviewAction(4U)); CHECK(fixture.session.project() == source);
  fixture.session.project().findRegion(fixture.regionId)->dynamicsAutomation = {};
  CHECK(controller.openClearDynamicsReview()); CHECK(!controller.sceneState().replacementReview.enabled[3]);
  CHECK(controller.replacementReviewAction(4U)); CHECK(changes == 1U);
}

TEST_CASE("native tempo input commits cancels and rejects stale or malformed text") {
  using namespace seam;
  NativeUiFixture fixture;
  unsigned opened = 0U, closed = 0U, changed = 0U;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [&](const native_ui::TextInputRequest&) { ++opened; },
       .endTextInput = [&] { ++closed; }, .documentChanged = [&] { ++changed; }}};
  CHECK(controller.beginTempoEdit()); CHECK(controller.textInputActive());
  CHECK(controller.commitTextComposition(U"123.75"));
  CHECK(fixture.session.project().tempoMap().bpmAt(time::Tick{0}) == 123.75);
  CHECK(changed == 1U); CHECK(closed == 1U);
  CHECK(controller.beginTempoEdit()); controller.cancelTextComposition();
  CHECK(changed == 1U); CHECK(!controller.textInputActive());
  CHECK(controller.setAccessibilityValue("toolbar.tempo", "99.5"));
  CHECK(fixture.session.project().tempoMap().bpmAt(time::Tick{0}) == 99.5);
  CHECK(!controller.setAccessibilityValue("toolbar.tempo", "100 BPM"));
  CHECK(changed == 2U);
  CHECK(controller.beginTempoEdit());
  CHECK(fixture.session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{960}, 80.0)));
  CHECK(!controller.commitTextComposition(U"60"));
  CHECK(fixture.session.project().tempoMap().bpmAt(time::Tick{0}) == 99.5);
  CHECK(controller.beginTempoEdit()); CHECK(controller.beginLyricEdit(fixture.noteId));
  CHECK(controller.commitTextComposition(U"lyric")); // Replacing the input clears tempo context.
  CHECK(fixture.session.project().tempoMap().bpmAt(time::Tick{0}) == 99.5);
  CHECK(opened > 0U); CHECK(closed > 0U); CHECK(!controller.textInputActive());
}

TEST_CASE("tempo text round trips precision and captured event ticks cannot retarget initial BPM") {
  using namespace seam;
  for (double value : {120.12345678901234, 0.00000000123456789, 999.9999999999999, 120.0}) {
    const auto parsed = native_ui::parseTempoEditText(native_ui::tempoEditText(value));
    CHECK(parsed); CHECK(parsed.value() == value);
  }
  for (const auto value : {"", "nan", "inf", "120x", " 120", "0", "1001", "+120", "0x1p7", "1e9999", "1e-9999", "120 "})
    CHECK(!native_ui::parseTempoEditText(value));
  const auto scientific = native_ui::parseTempoEditText("1.205e2");
  CHECK(scientific); CHECK(scientific.value() == 120.5);
  NativeUiFixture fixture;
  const double precise = 120.12345678901234;
  CHECK(fixture.session.project().tempoMap().addOrReplace(time::Tick{0}, precise));
  std::u32string field;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [&](const native_ui::TextInputRequest& input) { field = input.currentText; }}};
  CHECK(controller.beginTempoEdit()); CHECK(controller.commitTextComposition(field));
  CHECK(fixture.session.project().tempoMap().bpmAt(time::Tick{0}) == precise);
  CHECK(!controller.beginTempoEdit(time::Tick{-1}));
  CHECK(controller.beginTempoEdit(time::Tick{960}));
  CHECK(!controller.setAccessibilityValue("toolbar.tempo", "80"));
  CHECK(controller.textInputActive());
  CHECK(controller.commitTextComposition(U"90.25"));
  CHECK(fixture.session.project().tempoMap().bpmAt(time::Tick{0}) == precise);
  CHECK(fixture.session.project().tempoMap().bpmAt(time::Tick{960}) == 90.25);
  CHECK(fixture.session.undo()); CHECK(fixture.session.project().tempoMap().events().size() == 1U);
  CHECK(fixture.session.redo()); CHECK(fixture.session.project().tempoMap().bpmAt(time::Tick{960}) == 90.25);
}

TEST_CASE("native meter field validates signature keeps tempo separate and restores through undo") {
  using namespace seam;
  for (auto text : {"", "0/4", "33/4", "4/3", "4/256", "4/4x", "4/4/4", "-1/4"})
    CHECK(!native_ui::parseMeterEditText(text));
  NativeUiFixture fixture;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}}};
  CHECK(controller.setAccessibilityValue("toolbar.meter", "7/8"));
  CHECK(fixture.session.project().meterMap().meterAt(time::Tick{0}).numerator == 7U);
  CHECK(fixture.session.project().tempoMap().bpmAt(time::Tick{0}) == 120.0);
  CHECK(fixture.session.undo()); CHECK(controller.sceneState().meter.numerator == 4U);
  CHECK(controller.beginMeterEdit(time::Tick{3840}));
  CHECK(!controller.setAccessibilityValue("toolbar.tempo", "90"));
  CHECK(controller.commitTextComposition(U"3/4"));
  CHECK(fixture.session.project().meterMap().meterAt(time::Tick{3840}).numerator == 3U);
  CHECK(controller.beginMeterEdit()); controller.cancelTextComposition();
  CHECK(!controller.setAccessibilityValue("toolbar.meter", "4/3"));
  CHECK(controller.sceneState().meter.numerator == 4U);
  native_ui::EditorSceneLayout layout;
  for (double width : {640.0, 720.0, 1000.0, 1440.0}) {
    const auto tempo = layout.tempoInputBoundsForWidth(width);
    const auto meter = layout.meterInputBoundsForWidth(width);
    CHECK(tempo.width > 0.0); CHECK(tempo.right() < meter.x);
    CHECK(meter.right() == layout.bpmBoundsForWidth(width).right());
  }
}

TEST_CASE("time-map list pages preserve identities and selected deletion rejects stale snapshots") {
  using namespace seam;
  NativeUiFixture fixture;
  CHECK(fixture.session.project().tempoMap().addOrReplace(time::Tick{960}, 90.0));
  CHECK(fixture.session.project().meterMap().addOrReplace(time::Tick{960}, 3U, 4U));
  for (int i = 2; i < 20; ++i)
    CHECK(fixture.session.project().tempoMap().addOrReplace(time::Tick{i * 960}, 120.0));
  unsigned changes = 0U;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}, .documentChanged = [&] { ++changes; }}};
  auto model = controller.timeMapEvents(); CHECK(model); CHECK(model.value().size() == 22U);
  CHECK(!model.value().selected().removable());
  CHECK(!controller.removeSelectedTimeMapEvent(model.value())); CHECK(changes == 0U);
  CHECK(model.value().page(0U).size() == 6U); CHECK(model.value().page(3U).size() == 4U);
  CHECK(model.value().page(std::numeric_limits<std::size_t>::max()).empty());
  CHECK(model.value().select(2U)); CHECK(!model.value().selected().meter);
  CHECK(model.value().selected().tick == time::Tick{960});
  CHECK(!model.value().select(999U)); CHECK(model.value().selectedIndex() == 2U);
  CHECK(controller.beginSelectedTimeMapEdit(model.value()));
  CHECK(controller.commitTextComposition(U"95")); CHECK(changes == 1U);
  CHECK(!controller.removeSelectedTimeMapEvent(model.value()));
  auto fresh = controller.timeMapEvents(); CHECK(fresh); CHECK(fresh.value().select(3U));
  CHECK(fresh.value().selected().meter); CHECK(controller.removeSelectedTimeMapEvent(fresh.value()));
  CHECK(fixture.session.project().meterMap().events().size() == 1U);
  CHECK(fixture.session.project().tempoMap().bpmAt(time::Tick{960}) == 95.0);
  CHECK(changes == 2U); CHECK(fixture.session.undo());
  CHECK(fixture.session.project().meterMap().events().size() == 2U);
  CHECK(!controller.beginSelectedTimeMapEdit(fresh.value()));
  fresh = controller.timeMapEvents(); CHECK(fresh);
  CHECK(fixture.session.project().tempoMap().addOrReplace(time::Tick{0}, 121.0));
  CHECK(!controller.beginSelectedTimeMapEdit(fresh.value())); // Map changes are checked even without a session revision bump.
}

TEST_CASE("native time-map panel opens selects edits refreshes removes and blocks background actions") {
  using namespace seam;
  NativeUiFixture fixture;
  CHECK(fixture.session.project().tempoMap().addOrReplace(time::Tick{960}, 90.0));
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}}};
  native_ui::EditorSceneLayout layout;
  const auto button = layout.timeMapOpenBounds();
  CHECK(controller.pointerDown({.position = {button.x + 2.0, button.y + 2.0}, .button = native_ui::PointerButton::Left}));
  CHECK(controller.sceneState().timeMapVisible); CHECK(controller.sceneState().timeMapRows.size() == 3U);
  controller.resize(720.0, 520.0);
  if (const auto* capture = std::getenv("SEAM_TIME_MAP_CAPTURE")) {
    native_ui::PixelSurface surface{720U, 520U}; native_ui::RasterCanvas canvas{surface, 1.0};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState());
    CHECK(surface.writePpm(capture));
  }
  CHECK(!controller.setAccessibilityValue("toolbar.tempo", "50"));
  CHECK(!controller.timeMapPanelAction(3U)); // Initial row is protected.
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Down}));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Down}));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Enter}));
  CHECK(controller.commitTextComposition(U"95"));
  CHECK(controller.sceneState().timeMapStale);
  CHECK(!controller.timeMapPanelAction(3U));
  CHECK(controller.timeMapPanelAction(4U)); CHECK(!controller.sceneState().timeMapStale);
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Down}));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Down}));
  CHECK(controller.timeMapPanelAction(3U));
  CHECK(fixture.session.project().tempoMap().events().size() == 1U);
  CHECK(!controller.sceneState().timeMapStale);
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Escape}));
  CHECK(!controller.sceneState().timeMapVisible);
  CHECK(fixture.session.undo()); CHECK(fixture.session.project().tempoMap().bpmAt(time::Tick{960}) == 95.0);
}

TEST_CASE("time-map panel insertion is two-stage cancellable and never overwrites an existing event") {
  using namespace seam;
  NativeUiFixture fixture;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}}};
  CHECK(controller.openTimeMapPanel());
  CHECK(controller.timeMapPanelAction(6U));
  CHECK(!controller.commitTextComposition(U"0")); // Existing initial event must use Edit.
  CHECK(fixture.session.project().tempoMap().events().size() == 1U);
  CHECK(controller.timeMapPanelAction(6U)); CHECK(controller.commitTextComposition(U"1920"));
  CHECK(controller.textInputActive()); CHECK(fixture.session.project().tempoMap().events().size() == 1U);
  CHECK(controller.commitTextComposition(U"90.5"));
  CHECK(fixture.session.project().tempoMap().bpmAt(time::Tick{1920}) == 90.5);
  CHECK(controller.sceneState().timeMapStale); CHECK(!controller.timeMapPanelAction(7U));
  CHECK(controller.timeMapPanelAction(4U)); CHECK(controller.timeMapPanelAction(7U));
  CHECK(controller.commitTextComposition(U"1920")); // Same tick, different event type is allowed.
  controller.cancelTextComposition(); CHECK(fixture.session.project().meterMap().events().size() == 1U);
  CHECK(controller.timeMapPanelAction(7U)); CHECK(controller.commitTextComposition(U"1920"));
  CHECK(controller.commitTextComposition(U"7/8"));
  CHECK(fixture.session.project().meterMap().meterAt(time::Tick{1920}).numerator == 7U);
  CHECK(controller.timeMapPanelAction(4U)); CHECK(controller.timeMapPanelAction(6U));
  CHECK(controller.commitTextComposition(U"3840"));
  CHECK(fixture.session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{3840}, 80.0)));
  CHECK(!controller.commitTextComposition(U"70"));
  CHECK(fixture.session.project().tempoMap().bpmAt(time::Tick{3840}) == 80.0);
  for (auto input : {"-1", "1.5", "960ticks", "9223372036854775808"}) CHECK(!native_ui::parseTimeMapTick(input));
  const auto maximum = native_ui::parseTimeMapTick("9223372036854775807"); CHECK(maximum);
  CHECK(maximum.value().value() == std::numeric_limits<std::int64_t>::max());
}

TEST_CASE("time-map semantics expose modal actions and reject cancelled or previous-stage inputs") {
  using namespace seam;
  NativeUiFixture fixture;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}}};
  controller.rebuildAccessibilityTree();
  CHECK(controller.dispatchAccessibility("toolbar.time-map", native_ui::SemanticAction::Activate));
  const auto id = [&](std::string_view suffix) {
    controller.rebuildAccessibilityTree();
    for (const auto& node : controller.accessibilityTree().root().children)
      if (node.id.ends_with(suffix)) return node.id;
    throw test::Failure{"Missing time-map semantic node"};
  };
  const auto add = id(".action.6");
  CHECK(controller.accessibilityTree().virtualizedNoteCount() == 0U);
  CHECK(!controller.dispatchAccessibility("toolbar.tempo", native_ui::SemanticAction::Activate));
  CHECK(!controller.dispatchAccessibility(id(".action.3"), native_ui::SemanticAction::Activate));
  CHECK(controller.dispatchAccessibility(add, native_ui::SemanticAction::Activate));
  const auto cancelled = id(".input");
  CHECK(controller.dispatchAccessibility(id(".cancel"), native_ui::SemanticAction::Activate));
  CHECK(controller.dispatchAccessibility(id(".action.6"), native_ui::SemanticAction::Activate));
  CHECK(!controller.setAccessibilityValue(cancelled, "960"));
  const auto tickInput = id(".input"); CHECK(controller.setAccessibilityValue(tickInput, "1920"));
  CHECK(!controller.setAccessibilityValue(tickInput, "80"));
  CHECK(controller.setAccessibilityValue(id(".input"), "90"));
  CHECK(fixture.session.project().tempoMap().bpmAt(time::Tick{1920}) == 90.0);
  CHECK(!controller.dispatchAccessibility(id(".action.6"), native_ui::SemanticAction::Activate));
  CHECK(controller.dispatchAccessibility(id(".action.4"), native_ui::SemanticAction::Activate));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Tab}));
  CHECK(controller.accessibilityTree().focusedNode()->id.starts_with("time-map."));
  CHECK(controller.dispatchAccessibility(id(".action.5"), native_ui::SemanticAction::Activate));
  controller.rebuildAccessibilityTree(); CHECK(!controller.accessibilityTree().root().id.starts_with("time-map."));
}

TEST_CASE("time-map composition has visible shared input geometry in toolbar and panel") {
  using namespace seam;
  NativeUiFixture fixture;
  ui::Rect requested;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [&](const native_ui::TextInputRequest& input) { requested = input.logicalBounds; }}};
  controller.resize(720.0, 520.0);
  CHECK(controller.beginTempoEdit());
  CHECK(controller.sceneState().timeMapInputActive); CHECK(controller.sceneState().lyricEditor);
  CHECK(controller.sceneState().lyricEditor->x == requested.x);
  CHECK(controller.sceneState().lyricEditor->width == requested.width);
  CHECK(controller.sceneState().compositionPreview == "120");
  controller.cancelTextComposition(); CHECK(!controller.sceneState().timeMapInputActive);
  CHECK(controller.openTimeMapPanel()); CHECK(controller.timeMapPanelAction(6U));
  CHECK(controller.updateTextComposition(U"9223372036854775807", {}));
  const auto state = controller.sceneState(); CHECK(state.timeMapInputActive); CHECK(state.lyricEditor);
  CHECK(state.lyricEditor->width == requested.width); CHECK(state.lyricEditor->width > 400.0);
  CHECK(state.compositionPreview == "9223372036854775807");
  if (const auto* capture = std::getenv("SEAM_TIME_MAP_INPUT_CAPTURE")) {
    native_ui::PixelSurface surface{720U, 520U}; native_ui::RasterCanvas canvas{surface, 1.0};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), state); CHECK(surface.writePpm(capture));
  }
  controller.cancelTextComposition(); CHECK(!controller.sceneState().lyricEditor);
}

TEST_CASE("time-map refresh retains selected event across index shifts and deletion selects successor") {
  using namespace seam;
  NativeUiFixture fixture;
  for (int i = 1; i <= 12; ++i) CHECK(fixture.session.project().tempoMap().addOrReplace(time::Tick{i * 960}, 100.0));
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId};
  CHECK(controller.openTimeMapPanel());
  for (int i = 0; i < 7; ++i) CHECK(controller.keyDown({.key = native_ui::NativeKey::Down}));
  auto state = controller.sceneState(); CHECK(state.timeMapSelectedRow);
  CHECK(state.timeMapRows[*state.timeMapSelectedRow].starts_with("5760 ticks   TEMPO"));
  CHECK(fixture.session.execute(std::make_unique<application::EditTempoCommand>(time::Tick{480}, 90.0)));
  CHECK(controller.timeMapPanelAction(4U));
  state = controller.sceneState(); CHECK(state.timeMapSelectedRow);
  CHECK(state.timeMapRows[*state.timeMapSelectedRow].starts_with("5760 ticks   TEMPO"));
  CHECK(controller.timeMapPanelAction(3U));
  state = controller.sceneState(); CHECK(state.timeMapSelectedRow);
  CHECK(state.timeMapRows[*state.timeMapSelectedRow].starts_with("6720 ticks   TEMPO"));
  auto model = controller.timeMapEvents(); CHECK(model);
  model.value().selectNearest(time::Tick{0}, true); CHECK(model.value().selected().meter);
  model.value().selectNearest(time::Tick{std::numeric_limits<std::int64_t>::max()}, false);
  CHECK(model.value().selected().tick == time::Tick{11520});
}

TEST_CASE("native arrangement controller routes selected tracks through shared commands") {
  NativeUiFixture fixture;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  auto route = seam::domain::TrackOutputRoute{
      .bus = seam::domain::BusId{1U},
      .matrix = seam::domain::RoutingMatrix::monoToStereo(-0.75F),
  };
  CHECK(controller.setSelectedTrackRoute(route));
  const auto* track = fixture.session.project().findVocalTrack(
      controller.selectedTrack());
  CHECK(track != nullptr);
  CHECK(track->outputRoute == route);
  CHECK(fixture.session.undo());
  CHECK(track->outputRoute != route);
}

TEST_CASE("native arrangement exposes IME rename and keyboard reorder affordances") {
  NativeUiFixture fixture;
  std::size_t beginRequests = 0U;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .beginTextInput = [&beginRequests](const auto&) { ++beginRequests; },
          .endTextInput = [] {},
      }};
  CHECK(controller.beginSelectedTrackRename());
  CHECK(beginRequests == 1U);
  CHECK(controller.commitTextComposition(U"Lead renamed"));
  CHECK(fixture.session.project().vocalTracks().front().name == "Lead renamed");
  CHECK(controller.beginSelectedRegionRename());
  CHECK(controller.commitTextComposition(U"Verse renamed"));
  CHECK(fixture.session.project().findRegion(fixture.regionId)->name ==
        "Verse renamed");

  const auto added = controller.addVocalTrack("Second");
  CHECK(added);
  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::Up,
      .modifiers = seam::native_ui::InputModifiers{.shift = true},
      .repeat = false}));
  CHECK(fixture.session.project().vocalTracks().front().id == added.value());
  fixture.session.project().settings().characterDisplay =
      seam::domain::CharacterDisplayMode::Off;
  controller.resize(1280.0, 720.0);
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{1280.0 - 238.0 + 12.0, 99.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 2,
  }));
  CHECK(beginRequests == 3U);
  CHECK(controller.commitTextComposition(U"Pointer renamed"));
  CHECK(fixture.session.project().vocalTracks().front().name ==
        "Pointer renamed");
}

TEST_CASE("native arrangement toolbar exposes pointer and accessibility actions") {
  NativeUiFixture fixture;
  std::size_t beginRequests = 0U;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .beginTextInput = [&beginRequests](const auto&) { ++beginRequests; },
          .endTextInput = [] {},
      }};
  fixture.session.project().settings().characterDisplay =
      seam::domain::CharacterDisplayMode::Off;
  controller.resize(1280.0, 720.0);
  const auto layout = seam::native_ui::EditorScenePainter{}.layout();
  const auto actionPoint = [&](std::size_t index) {
    const auto bounds = layout.arrangementActionBoundsForWidth(1280.0, index);
    return seam::ui::Point{bounds.x + 4.0, bounds.y + 8.0};
  };
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = actionPoint(0U),
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(fixture.session.project().vocalTracks().size() == 2U);
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = actionPoint(1U),
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(controller.selectedRegion().valid());
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = actionPoint(2U),
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(beginRequests == 1U);
  CHECK(controller.commitTextComposition(U"Toolbar region"));

  controller.rebuildAccessibilityTree();
  CHECK(seam::native_ui::EditorSemanticTree::containsId(
      controller.accessibilityTree().root(), "arrangement.add-track"));
  CHECK(seam::native_ui::EditorSemanticTree::containsId(
      controller.accessibilityTree().root(), "arrangement.move-up"));
  CHECK(controller.dispatchAccessibility(
      "arrangement.add-track", seam::native_ui::SemanticAction::Activate));
  CHECK(fixture.session.project().vocalTracks().size() == 3U);
}


TEST_CASE("native vibrato inspector edits drafts through fields and applies the selection once") {
  using namespace seam;
  NativeUiFixture fixture; fixture.session.project().settings().characterDisplay = domain::CharacterDisplayMode::Off;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  auto [lyric, note] = fixture.factory.makeNote(time::Tick{2400}, time::Tick{480}, 67U, U"la", domain::Language::English);
  note.vibrato.enabled = true; note.vibrato.phaseTurns = 0.75F; note.vibrato.fadeOutFraction = 0.5F;
  region->lyrics.push_back(lyric); region->notes.push_back(note);
  fixture.session.selection().replace({fixture.noteId, note.id}); const auto source = fixture.session.project();
  unsigned changes = 0U; std::u32string initial;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [&](const native_ui::TextInputRequest& request) { initial = request.currentText; }, .documentChanged = [&] { ++changes; }}};
  controller.resize(960.0, 640.0); controller.rebuildAccessibilityTree();
  CHECK(controller.dispatchAccessibility("inspector.vibrato", native_ui::SemanticAction::Activate));
  controller.resize(480.0, 320.0);
  auto view = controller.sceneState().replacementReview; CHECK(view.dockedInspector); CHECK(view.rows.size() == 6U); CHECK(!view.enabled[3]);
  std::string oldApply;
  const native_ui::EditorSceneLayout layout;
  const auto row = layout.reviewRowBounds(480.0, 320.0, 0U, true);
  CHECK(controller.pointerDown({.position = {row.x + 5.0, row.y + 5.0}, .button = native_ui::PointerButton::Left}));
  CHECK(initial.empty()); CHECK(controller.sceneState().boundedInputLabel.starts_with("VIBRATO:"));
  CHECK(controller.commitTextComposition(U"On")); CHECK(controller.sceneState().replacementReview.visible);
  controller.rebuildAccessibilityTree();
  for (const auto& control : controller.accessibilityTree().root().children) if (control.id.ends_with("action.3")) { CHECK(control.enabled); oldApply = control.id; }
  CHECK(!oldApply.empty());
  CHECK(controller.openReplacementRow(2U)); CHECK(!controller.commitTextComposition(U"0.6"));
  CHECK(!controller.sceneState().replacementReview.enabled[3]); CHECK(fixture.session.project() == source);
  CHECK(controller.openReplacementRow(3U)); CHECK(controller.commitTextComposition(U"0.3"));
  controller.rebuildAccessibilityTree(); const auto depthField = controller.accessibilityTree().root().children.at(4U).id;
  CHECK(depthField.ends_with("row.4"));
  CHECK(controller.dispatchAccessibility(depthField, native_ui::SemanticAction::SetFocus));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Enter})); CHECK(controller.commitTextComposition(U"75"));
  CHECK(controller.replacementReviewAction(1U)); CHECK(controller.sceneState().replacementReview.rows.size() == 1U);
  CHECK(controller.openReplacementRow(0U)); controller.cancelTextComposition(); CHECK(controller.replacementReviewOpen());
  CHECK(controller.openReplacementRow(0U)); CHECK(controller.commitTextComposition(U"0.25"));
  CHECK(controller.replacementReviewAction(2U)); // Reset only the phase patch.
  CHECK(controller.sceneState().replacementReview.rows.front().find("Mixed") != std::string::npos);
  CHECK(controller.replacementReviewAction(0U));
  if (const auto* capture = std::getenv("SEAM_VIBRATO_INSPECTOR_CAPTURE")) {
    auto engine = text::TextEngine::createSystem(); CHECK(engine);
    native_ui::PixelSurface surface{480U,320U}; native_ui::RasterCanvas canvas{surface,1.0,engine.value().get()};
    native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState()); CHECK(surface.writePpm(capture));
  }
  CHECK(fixture.session.project() == source); CHECK(changes == 0U);
  CHECK(!controller.dispatchAccessibility(oldApply, native_ui::SemanticAction::Activate));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Delete})); CHECK(fixture.session.project() == source);
  CHECK(controller.replacementReviewAction(3U)); CHECK(changes == 1U); CHECK(!controller.replacementReviewOpen());
  auto expected = source;
  for (const auto id : {fixture.noteId, note.id}) {
    auto& value = expected.findNote(id)->vibrato; value.enabled = true; value.fadeInFraction = 0.6F; value.fadeOutFraction = 0.3F; value.depthCents = 75.0F;
  }
  CHECK(fixture.session.project() == expected); CHECK(fixture.session.undo()); CHECK(fixture.session.project() == source);
  CHECK(controller.openVibratoInspector()); CHECK(controller.replacementReviewAction(4U)); CHECK(fixture.session.project() == source);
}

TEST_CASE("native vibrato inspector rejects stale field input and keeps responsive controls in bounds") {
  using namespace seam;
  NativeUiFixture fixture; fixture.session.selection().selectOnly(fixture.noteId);
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId,
      {.beginTextInput = [](const native_ui::TextInputRequest&) {}}};
  CHECK(controller.openVibratoInspector()); CHECK(controller.openReplacementRow(4U));
  const auto source = fixture.session.project(); CHECK(fixture.session.replaceProject(source)); fixture.session.selection().selectOnly(fixture.noteId);
  CHECK(!controller.commitTextComposition(U"75")); CHECK(controller.replacementReviewOpen()); CHECK(!controller.sceneState().replacementReview.enabled[3]);
  CHECK(controller.replacementReviewAction(5U));
  const native_ui::EditorSceneLayout layout;
  for (const auto size : {ui::Point{480.0,320.0}, ui::Point{960.0,640.0}}) {
    controller.resize(size.x, size.y); controller.rebuildAccessibilityTree(); const auto panel = layout.reviewPanelBounds(size.x,size.y,true);
    CHECK(panel.x >= 0.0); CHECK(panel.y >= 0.0); CHECK(panel.right() <= size.x); CHECK(panel.bottom() <= size.y);
    for (const auto& child : controller.accessibilityTree().root().children) {
      CHECK(child.bounds.x >= panel.x); CHECK(child.bounds.y >= panel.y);
      CHECK(child.bounds.right() <= panel.right()); CHECK(child.bounds.bottom() <= panel.bottom());
    }
  }
  CHECK(controller.replacementReviewAction(4U)); CHECK(fixture.session.project() == source);
}

TEST_CASE("arrangement inspector visibility and pointer bounds agree with paint including overlays") {
  using namespace seam;
  for (const auto height : {320.0, 640.0}) for (const bool diagnostic : {false, true}) {
    NativeUiFixture fixture; fixture.session.project().settings().characterDisplay = domain::CharacterDisplayMode::Off;
    native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId}; controller.resize(960.0, height);
    if (diagnostic) controller.setDiagnostics({{.code = "MEDIA_MISSING", .messageKey = "media.missing",
        .actions = {authoring::DiagnosticAction::RelinkMedia}}});
    const auto state = controller.sceneState(); const native_ui::EditorSceneLayout layout;
    const auto bottom = height - layout.statusHeight - layout.diagnosticHeight(diagnostic);
    const auto top = native_ui::resolveArrangementInspectorTop(state, layout, bottom);
    controller.rebuildAccessibilityTree();
    CHECK(native_ui::EditorSemanticTree::containsId(controller.accessibilityTree().root(), "inspector.mute") == top.has_value());
    if (top) {
      CHECK(controller.dispatchAccessibility("inspector.mute", native_ui::SemanticAction::SetFocus));
      const auto bounds = controller.accessibilityTree().focusedNode()->bounds;
      CHECK(bounds.y >= *top); CHECK(bounds.bottom() <= bottom);
      CHECK(bounds.y == *top + layout.inspectorNameBaseline + layout.inspectorNameToFirstFieldAdvance + layout.inspectorFieldAdvance * 3.0);
      const auto track = controller.selectedTrack(); const auto before = fixture.session.project();
      CHECK(controller.pointerDown({.position = {bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5}, .button = native_ui::PointerButton::Left}));
      CHECK(fixture.session.project().findVocalTrack(track)->muted); CHECK(fixture.session.undo()); CHECK(fixture.session.project() == before);
      CHECK(controller.dispatchAccessibility("inspector.solo", native_ui::SemanticAction::SetFocus));
      const auto solo = controller.accessibilityTree().focusedNode()->bounds; CHECK(solo.x == bounds.right()); CHECK(solo.y == bounds.y);
      CHECK(controller.pointerDown({.position = {solo.x + solo.width * 0.5, solo.y + solo.height * 0.5}, .button = native_ui::PointerButton::Left}));
      CHECK(fixture.session.project().findVocalTrack(track)->solo); CHECK(!fixture.session.project().findVocalTrack(track)->muted);
      CHECK(fixture.session.undo()); CHECK(fixture.session.project() == before);
      if (height == 640.0 && diagnostic) if (const auto* capture = std::getenv("SEAM_INSPECTOR_LAYOUT_CAPTURE")) {
        auto engine = text::TextEngine::createSystem(); CHECK(engine);
        native_ui::PixelSurface surface{960U,640U}; native_ui::RasterCanvas canvas{surface,1.0,engine.value().get()};
        native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState()); CHECK(surface.writePpm(capture));
      }
    } else CHECK(!controller.dispatchAccessibility("inspector.mute", native_ui::SemanticAction::Activate));
  }
}

TEST_CASE("crowded arrangement does not expose hidden inspector or offscreen track targets") {
  using namespace seam;
  NativeUiFixture fixture; fixture.session.project().settings().characterDisplay = domain::CharacterDisplayMode::Off;
  for (unsigned i = 0U; i < 40U; ++i) static_cast<void>(fixture.factory.addVocalTrack(fixture.session.project(), "Track " + std::to_string(i)));
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId}; controller.resize(960.0, 640.0);
  const auto source = fixture.session.project(); const auto state = controller.sceneState(); const native_ui::EditorSceneLayout layout;
  CHECK(!native_ui::resolveArrangementInspectorTop(state, layout, 640.0 - layout.statusHeight));
  controller.rebuildAccessibilityTree(); const auto& root = controller.accessibilityTree().root();
  CHECK(!native_ui::EditorSemanticTree::containsId(root, "inspector.mute"));
  CHECK(!native_ui::EditorSemanticTree::containsId(root, "arrangement.track." + fixture.session.project().vocalTracks().back().id.toString()));
  CHECK(!controller.dispatchAccessibility("inspector.mute", native_ui::SemanticAction::Activate));
  CHECK(controller.pointerDown({.position = {900.0, 570.0}, .button = native_ui::PointerButton::Left}));
  CHECK(fixture.session.project() == source);
}

TEST_CASE("native semantic focus does not mutate note or arrangement selection") {
  NativeUiFixture fixture;
  fixture.session.project().settings().characterDisplay =
      seam::domain::CharacterDisplayMode::Off;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  controller.resize(1280.0, 720.0);
  controller.setRenderStatus(seam::native_ui::RenderStatusView{
      .state = seam::native_ui::RenderStatusState::Ready,
      .requestedRevision = 1U,
      .audibleRevision = 1U,
      .hasAudibleAudio = true,
  });
  controller.rebuildAccessibilityTree();

  CHECK(controller.dispatchAccessibility(
      "toolbar.transport", seam::native_ui::SemanticAction::SetFocus));
  CHECK(controller.accessibilityTree().focusedNode() != nullptr);
  CHECK(controller.accessibilityTree().focusedNode()->id == "toolbar.transport");
  CHECK(controller.dispatchAccessibility(
      "inspector.mute", seam::native_ui::SemanticAction::SetFocus));
  CHECK(controller.accessibilityTree().focusedNode()->id == "inspector.mute");
  CHECK(controller.dispatchAccessibility(
      "inspector.solo", seam::native_ui::SemanticAction::SetFocus));
  CHECK(controller.accessibilityTree().focusedNode()->id == "inspector.solo");

  const auto trackId = controller.selectedTrack();
  const auto regionId = controller.selectedRegion();
  CHECK(trackId.valid());
  CHECK(regionId.valid());
  fixture.session.selection().selectOnly(fixture.noteId);
  const auto noteSelection = fixture.session.selection().noteIds();

  CHECK(controller.dispatchAccessibility(
      "arrangement.track." + trackId.toString(),
      seam::native_ui::SemanticAction::SetFocus));
  CHECK(controller.selectedTrack() == trackId);
  CHECK(controller.selectedRegion() == regionId);
  CHECK(fixture.session.selection().noteIds() == noteSelection);

  CHECK(controller.dispatchAccessibility(
      "arrangement.region." + regionId.toString(),
      seam::native_ui::SemanticAction::SetFocus));
  CHECK(controller.selectedTrack() == trackId);
  CHECK(controller.selectedRegion() == regionId);
  CHECK(fixture.session.selection().noteIds() == noteSelection);

  CHECK(controller.dispatchAccessibility(
      "note." + fixture.noteId.toString(),
      seam::native_ui::SemanticAction::SetFocus));
  CHECK(fixture.session.selection().noteIds() == noteSelection);
}

TEST_CASE("native keyboard focus reveals full detail for a bounded note") {
  NativeUiFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  region->findLyric(fixture.lyricId)->surface =
      U"가나다라마바사 こんにちは世界 中文歌词 👩‍🎤";
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  controller.resize(1280.0, 720.0);

  for (std::size_t attempt = 0U; attempt < 32U; ++attempt) {
    CHECK(controller.keyDown(seam::native_ui::KeyEvent{
        .key = seam::native_ui::NativeKey::Tab}));
    const auto* focused = controller.accessibilityTree().focusedNode();
    if (focused != nullptr &&
        focused->id == "note." + fixture.noteId.toString()) {
      break;
    }
  }
  const auto state = controller.sceneState();
  CHECK(state.focusedNote == fixture.noteId);
  CHECK(state.detail.has_value());
  if (state.detail.has_value()) {
    CHECK(state.detail->value ==
          "가나다라마바사 こんにちは世界 中文歌词 👩‍🎤");
  }
}

TEST_CASE("native arrangement and diagnostics surfaces capture current layout") {
  NativeUiFixture fixture;
  fixture.session.project().setName("日本語 프로젝트 中文");
  auto* vocalTrack = fixture.session.project().findVocalTrack(
      fixture.session.project().vocalTracks().front().id);
  CHECK(vocalTrack != nullptr);
  vocalTrack->name = "歌声 트랙 中文";
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  region->name = "第一節 절";
  auto* lyric = region->findLyric(fixture.lyricId);
  CHECK(lyric != nullptr);
  lyric->surface = U"こんにちは 안녕 你好";
  auto* note = region->findNote(fixture.noteId);
  CHECK(note != nullptr);
  note->startTick = seam::time::Tick{0};
  region->sortNotes();
  region->unitSelectionOverrides.push_back(
      seam::domain::UnitSelectionOverride{
          .startKey = seam::domain::PhonemeKey{.noteId = fixture.noteId,
                                               .ordinal = 0U},
          .tokenCount = 20U,
          .unitId = "かな🎤中文Á超長いユニット名とても長い識別子",
          .renderer = seam::domain::UnitRendererKind::ClassicPsola,
          .locked = true,
      });
  CHECK(seam::text::utf8DisplayWidth(
            "かな🎤中文Á超長いユニット名とても長い識別子") > 29U);
  fixture.session.project().settings().characterDisplay =
      seam::domain::CharacterDisplayMode::Off;
  fixture.session.project().audioTracks().push_back(seam::domain::AudioTrack{
      .id = seam::domain::TrackId{9900U},
      .name = "伴奏 트랙 中文",
      .mediaPath = "backing.wav",
      .mediaHash = std::string(64U, 'b'),
      .mediaOwnership = seam::domain::MediaOwnership::ProjectCopy,
      .originalFilename = "backing.wav",
      .sourceSampleRate = 48000U,
      .sourceChannels = 2U,
      .sourceFrameCount = 48000U,
      .startTick = seam::time::Tick{0},
  });
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  controller.resize(1280.0, 720.0);
  controller.pianoRoll().pitch().setTopMidiKey(72);
  controller.pianoRoll().timeline().setPixelsPerQuarter(160.0);
  controller.pianoRoll().rebuildIndex();
  controller.setDiagnostics({seam::authoring::Diagnostic{
      .code = "MEDIA_MISSING",
      .severity = seam::authoring::DiagnosticSeverity::Error,
      .messageKey = "media.missing",
      .actions = {seam::authoring::DiagnosticAction::RelinkMedia},
  }});
  seam::native_ui::EditorScenePainter painter;
  seam::native_ui::PixelSurface surface{1280U, 720U};
  auto textEngine = seam::text::TextEngine::createSystem();
  seam::native_ui::RasterCanvas canvas{
      surface, 1.0, textEngine ? textEngine.value().get() : nullptr};
  painter.paint(canvas, controller.pianoRoll(), controller.sceneState());
  CHECK(surface.checksum() != 0U);
  if (const auto* captureRoot = std::getenv("SEAM_NATIVE_UI_ARRANGEMENT_CAPTURE_DIR");
      captureRoot != nullptr && *captureRoot != '\0') {
    const std::filesystem::path directory{captureRoot};
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    CHECK(!error);
    CHECK(surface.writePpm(directory / "arrangement-diagnostics-cjk.ppm"));
    controller.setExportProgress(seam::authoring::ExportProgress{
        .state = seam::authoring::ExportState::Staging,
        .currentOutput = "master.wav",
        .completedFiles = 1U,
        .totalFiles = 2U,
    });
    seam::native_ui::PixelSurface exportSurface{1280U, 720U};
    seam::native_ui::RasterCanvas exportCanvas{
        exportSurface, 1.0, textEngine ? textEngine.value().get() : nullptr};
    painter.paint(exportCanvas, controller.pianoRoll(), controller.sceneState());
    CHECK(exportSurface.writePpm(
        directory / "arrangement-diagnostics-export-cjk.ppm"));
    controller.resize(480.0, 320.0);
    seam::native_ui::PixelSurface exportNarrowSurface{480U, 320U};
    seam::native_ui::RasterCanvas exportNarrowCanvas{
        exportNarrowSurface, 1.0,
        textEngine ? textEngine.value().get() : nullptr};
    painter.paint(exportNarrowCanvas, controller.pianoRoll(),
                  controller.sceneState());
    CHECK(exportNarrowSurface.writePpm(
        directory / "export-diagnostics-narrow.ppm"));
    controller.setExportProgress(seam::authoring::ExportProgress{
        .state = seam::authoring::ExportState::Committed,
        .currentOutput = "master.wav",
        .completedFiles = 2U,
        .totalFiles = 2U,
    });
    controller.setLastExport(seam::authoring::ExportResult{
        .state = seam::authoring::ExportState::Committed,
        .receiptPath = directory / "receipt.json",
        .setPath = directory / "song-export",
    });
    seam::native_ui::PixelSurface committedSurface{1280U, 720U};
    seam::native_ui::RasterCanvas committedCanvas{
        committedSurface, 1.0, textEngine ? textEngine.value().get() : nullptr};
    painter.paint(committedCanvas, controller.pianoRoll(),
                  controller.sceneState());
    CHECK(committedSurface.writePpm(
        directory / "arrangement-diagnostics-export-committed.ppm"));
  }
}

TEST_CASE("native controller commits one move command at pointer release") {
  NativeUiFixture fixture;
  std::size_t repaints = 0U;
  std::size_t documentChanges = 0U;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .requestRepaint = [&repaints] { ++repaints; },
          .beginTextInput = {},
          .endTextInput = {},
          .setPlaying = {},
          .documentChanged = [&documentChanges] { ++documentChanges; },
      }};
  controller.resize(1280.0, 720.0);
  const auto visual = controller.pianoRoll().visibleNotes().front();
  const seam::ui::Point start{
      visual.bounds.x + 4.0,
      visual.bounds.y + 4.0 + 98.0,
  };
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = start,
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(controller.pointerMove(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{start.x + 112.0, start.y - 18.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(fixture.session.revision() == 0U);
  CHECK(controller.pointerUp(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{start.x + 112.0, start.y - 18.0},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(fixture.session.revision() == 1U);
  const auto* moved = fixture.session.project().findNote(fixture.noteId);
  CHECK(moved != nullptr);
  CHECK(moved->midiKey == 65U);
  CHECK(moved->startTick == seam::time::Tick{1920});
  CHECK(repaints > 0U);
  CHECK(documentChanges == 1U);
  CHECK(fixture.session.undo());
  CHECK(fixture.session.project().findNote(fixture.noteId)->midiKey == 64U);
}

TEST_CASE("native controller resizes a note from its right edge") {
  NativeUiFixture fixture;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  controller.resize(1280.0, 720.0);
  const auto visual = controller.pianoRoll().visibleNotes().front();
  const auto originalDuration =
      fixture.session.project().findNote(fixture.noteId)->durationTick;
  const seam::ui::Point start{
      visual.bounds.right() - 2.0,
      visual.bounds.y + 4.0 + 98.0,
  };
  CHECK(controller.pointerDown(seam::native_ui::PointerEvent{
      .position = start,
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(controller.pointerMove(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{start.x + 112.0, start.y},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(controller.pointerUp(seam::native_ui::PointerEvent{
      .position = seam::ui::Point{start.x + 112.0, start.y},
      .button = seam::native_ui::PointerButton::Left,
      .modifiers = {},
      .clickCount = 1,
  }));
  CHECK(fixture.session.project().findNote(fixture.noteId)->durationTick >
        originalDuration);
}

TEST_CASE("native text input commits Unicode lyric through undoable command") {
  NativeUiFixture fixture;
  std::size_t begins = 0U;
  std::size_t ends = 0U;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .requestRepaint = {},
          .beginTextInput = [&begins](const auto&) { ++begins; },
          .endTextInput = [&ends] { ++ends; },
          .setPlaying = {},
          .documentChanged = {},
      }};
  controller.resize(1280.0, 720.0);
  CHECK(controller.beginLyricEdit(fixture.noteId));
  CHECK(controller.textInputActive());
  CHECK(controller.updateTextComposition(
      U"継ぎ目", seam::ui::CompositionSelection{3U, 0U}));
  CHECK(controller.commitTextComposition(U"継ぎ目"));
  CHECK(!controller.textInputActive());
  const auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  CHECK(region->findLyric(fixture.lyricId)->surface == U"継ぎ目");
  CHECK(begins == 1U);
  CHECK(ends == 1U);
  CHECK(fixture.session.undo());
  CHECK(region->findLyric(fixture.lyricId)->surface == U"edge");
}

TEST_CASE("accessibility value setting commits the targeted lyric") {
  NativeUiFixture fixture;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  controller.resize(1280.0, 720.0);
  const auto id = "note." + fixture.noteId.toString();
  CHECK(controller.setAccessibilityValue(id, "きゃ"));
  const auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  CHECK(region->findLyric(fixture.lyricId)->surface == U"きゃ");
  CHECK(!controller.textInputActive());
  CHECK(fixture.session.undo());
  CHECK(region->findLyric(fixture.lyricId)->surface == U"edge");
}


TEST_CASE("character package loads low-poly portrait and display mode cycles") {
  const auto directory = seam::test::support::temporaryDirectory("character-package");
  std::filesystem::create_directories(directory / "runtime");
  seam::native_ui::PixelSurface portrait{8U, 12U};
  portrait.clear(seam::native_ui::Color{64, 48, 72, 255});
  for (const auto* name : {"neutral", "focused", "rendering", "complete", "warning", "error"}) {
    CHECK(portrait.writePpm(directory / "runtime" / (std::string{name} + ".ppm")));
  }
  const std::string manifest = R"json({
    "schemaVersion":1,
    "characterId":"official.character.test",
    "displayName":"Test Character",
    "version":"1.0.0",
    "voicebankId":"voice.test",
    "style":"emo-low-poly",
    "defaultState":"neutral",
    "accent":{"primary":"#8B4C69","secondary":"#6E5A86"},
    "states":{
      "neutral":"runtime/neutral.ppm",
      "focused":"runtime/focused.ppm",
      "rendering":"runtime/rendering.ppm",
      "complete":"runtime/complete.ppm",
      "warning":"runtime/warning.ppm",
      "error":"runtime/error.ppm"
    }
  })json";
  std::ofstream output(directory / "manifest.json", std::ios::binary | std::ios::trunc);
  output << manifest;
  output.close();

  seam::native_ui::CharacterPresentation presentation;
  CHECK(presentation.load(directory));
  CHECK(presentation.loaded());
  CHECK(presentation.displayName() == "Test Character");
  CHECK(presentation.portrait() != nullptr);
  CHECK(presentation.portrait()->width() == 8U);
  presentation.setState(seam::character::State::Warning);
  CHECK(presentation.portrait() != nullptr);
  CHECK(presentation.state() == seam::character::State::Warning);

  NativeUiFixture fixture;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  fixture.session.project().settings().characterDisplay =
      seam::domain::CharacterDisplayMode::Full;
  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::C, .modifiers = {}, .repeat = false}));
  CHECK(fixture.session.project().settings().characterDisplay ==
        seam::domain::CharacterDisplayMode::Minimal);
  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::C, .modifiers = {}, .repeat = false}));
  CHECK(fixture.session.project().settings().characterDisplay ==
        seam::domain::CharacterDisplayMode::Off);
}

TEST_CASE("character display cycling does not submit an audio document change") {
  NativeUiFixture fixture;
  std::size_t documentChanges = 0U;
  std::size_t viewChanges = 0U;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .documentChanged = [&documentChanges] { ++documentChanges; },
          .viewChanged = [&viewChanges] { ++viewChanges; },
      }};
  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::C, .modifiers = {}, .repeat = false}));
  CHECK(documentChanges == 0U);
  CHECK(viewChanges == 1U);
}

TEST_CASE("lane transitions publish deterministic shared geometry within 150 milliseconds") {
  NativeUiFixture fixture;
  auto now = std::chrono::steady_clock::time_point{};
  std::size_t repaintCount = 0U;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .requestRepaint = [&repaintCount] { ++repaintCount; },
          .uiClock = [&now] { return now; },
          .reduceMotionEnabled = [] { return false; },
      }};
  controller.resize(1280.0, 720.0);
  const seam::native_ui::EditorSceneLayout layout;
  const auto beforeState = controller.sceneState();
  const auto before = seam::native_ui::resolveEditorTechnicalLaneHeights(
      beforeState, layout, 720.0 - layout.statusHeight);

  controller.rebuildAccessibilityTree();
  CHECK(controller.dispatchAccessibility(
      "lane.pitch", seam::native_ui::SemanticAction::Toggle));
  const auto start = controller.sceneState();
  CHECK(start.technicalLaneHeightsOverride.has_value());
  CHECK_NEAR(start.technicalLaneHeightsOverride->at(3U), before.values[3U],
             1e-9);

  now += std::chrono::milliseconds{75};
  const auto midpoint = controller.sceneState();
  CHECK(midpoint.technicalLaneHeightsOverride.has_value());
  now += std::chrono::milliseconds{75};
  const auto finalState = controller.sceneState();
  CHECK(!finalState.technicalLaneHeightsOverride.has_value());
  const auto final = seam::native_ui::resolveEditorTechnicalLaneHeights(
      finalState, layout, 720.0 - layout.statusHeight);
  CHECK_NEAR(midpoint.technicalLaneHeightsOverride->at(3U),
             (before.values[3U] + final.values[3U]) * 0.5, 1e-9);
  CHECK(final.values[3U] > before.values[3U]);

  const auto midpointTree = seam::native_ui::EditorSemanticTree::build(
      midpoint, controller.pianoRoll());
  const auto pitchLane = std::find_if(
      midpointTree.children.begin(), midpointTree.children.end(),
      [](const auto& node) { return node.id == "lane.pitch"; });
  CHECK(pitchLane != midpointTree.children.end());
  if (pitchLane != midpointTree.children.end()) {
    CHECK_NEAR(pitchLane->bounds.height,
               midpoint.technicalLaneHeightsOverride->at(3U), 1e-9);
  }
  CHECK(repaintCount > 0U);
}

TEST_CASE("reduced motion commits lane geometry without an intermediate frame") {
  NativeUiFixture fixture;
  auto now = std::chrono::steady_clock::time_point{};
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .uiClock = [&now] { return now; },
          .reduceMotionEnabled = [] { return true; },
      }};
  controller.resize(1280.0, 720.0);
  const seam::native_ui::EditorSceneLayout layout;
  const auto beforeState = controller.sceneState();
  const auto before = seam::native_ui::resolveEditorTechnicalLaneHeights(
      beforeState, layout, 720.0 - layout.statusHeight);
  controller.rebuildAccessibilityTree();
  CHECK(controller.dispatchAccessibility(
      "lane.pitch", seam::native_ui::SemanticAction::Toggle));
  const auto afterState = controller.sceneState();
  CHECK(!afterState.technicalLaneHeightsOverride.has_value());
  CHECK(!afterState.dockWidthOverride.has_value());
  const auto after = seam::native_ui::resolveEditorTechnicalLaneHeights(
      afterState, layout, 720.0 - layout.statusHeight);
  CHECK(after.values[3U] > before.values[3U]);
}

TEST_CASE("character identity dock follows the injected transition clock") {
  NativeUiFixture fixture;
  auto& track = fixture.session.project().vocalTracks().front();
  track.voicebank = seam::domain::VoicebankReference{
      .id = "voice.motion",
      .version = "1.0.0",
      .contentHash = std::string(64U, 'a'),
  };
  fixture.session.project().settings().characterDisplay =
      seam::domain::CharacterDisplayMode::Full;
  auto now = std::chrono::steady_clock::time_point{};
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .uiClock = [&now] { return now; },
          .reduceMotionEnabled = [] { return false; },
      }};
  seam::native_ui::PixelSurface portrait{24U, 36U};
  portrait.clear(seam::native_ui::Color{64U, 48U, 72U, 255U});
  seam::authoring::VoicebankCard card{
      .id = track.voicebank.id,
      .version = track.voicebank.version,
      .displayName = "Motion Voice",
      .contentHash = track.voicebank.contentHash,
      .selectable = true,
      .characterAvailable = true,
      .characterId = "character.motion",
      .characterVersion = "1.0.0",
  };
  controller.setVoicebankCards({card});
  controller.setCharacterBinding({
      .id = "character.motion",
      .version = "1.0.0",
      .voicebankId = track.voicebank.id,
  });
  controller.setCharacterPortrait(&portrait);
  const seam::native_ui::EditorSceneLayout layout;
  CHECK_NEAR(seam::native_ui::resolveEditorDockWidth(controller.sceneState(), layout),
             layout.characterDockWidth, 1e-9);

  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::C}));
  const auto start = controller.sceneState();
  CHECK(start.dockWidthOverride.has_value());
  CHECK_NEAR(*start.dockWidthOverride, layout.characterDockWidth, 1e-9);
  now += std::chrono::milliseconds{75};
  const auto midpoint = controller.sceneState();
  CHECK(midpoint.dockWidthOverride.has_value());
  CHECK_NEAR(*midpoint.dockWidthOverride, layout.characterDockWidth * 0.5,
             1e-9);
  const auto midpointTree = seam::native_ui::EditorSemanticTree::build(
      midpoint, controller.pianoRoll());
  CHECK(seam::native_ui::EditorSemanticTree::containsId(
      midpointTree, "character.dock"));
  now += std::chrono::milliseconds{75};
  const auto finalState = controller.sceneState();
  CHECK(!finalState.dockWidthOverride.has_value());
  CHECK_NEAR(seam::native_ui::resolveEditorDockWidth(finalState, layout), 0.0,
             1e-9);
}

TEST_CASE("toolbar project metadata avoids the portrait at compact scale width") {
  const seam::native_ui::EditorSceneLayout layout;
  const auto compact = layout.projectHeaderBoundsForWidth(720.0, true);
  CHECK(!compact.has_value());

  const auto normal = layout.projectHeaderBoundsForWidth(1440.0, true);
  CHECK(normal.has_value());
  CHECK(normal->right() <= 1440.0 - layout.portraitRightInset -
                              layout.compactToolbarGap);
  const auto normalIdentity = layout.voiceIdentityBoundsForWidth(1440.0);
  CHECK(normalIdentity.has_value());
  if (normalIdentity.has_value()) {
    CHECK(normal->right() <= normalIdentity->x - layout.compactToolbarGap);
  }
}

TEST_CASE("toolbar identity reserves a collision-free responsive region") {
  const seam::native_ui::EditorSceneLayout layout;
  CHECK(!layout.voiceIdentityBoundsForWidth(720.0).has_value());

  const auto mediumIdentity = layout.voiceIdentityBoundsForWidth(960.0);
  CHECK(mediumIdentity.has_value());
  if (mediumIdentity.has_value()) {
    CHECK(mediumIdentity->x >=
          layout.bpmBoundsForWidth(960.0).right() + layout.compactToolbarGap);
    CHECK(layout.batchLyricsBoundsForWidth(960.0, false).width == 0.0);
    CHECK(layout.loopBoundsForWidth(960.0, false).width == 0.0);
  }
  CHECK(!layout.projectHeaderBoundsForWidth(960.0, false).has_value());

  const auto wideIdentity = layout.voiceIdentityBoundsForWidth(1188.0);
  const auto wideProject = layout.projectHeaderBoundsForWidth(1188.0, false);
  CHECK(wideIdentity.has_value());
  CHECK(wideProject.has_value());
  if (wideIdentity.has_value() && wideProject.has_value()) {
    CHECK(wideProject->right() <= wideIdentity->x - layout.compactToolbarGap);
  }
}

TEST_CASE("compact toolbar reserves a project identity subtitle") {
  const seam::native_ui::EditorSceneLayout layout;
  const auto compact = layout.compactProjectTitleBoundsForWidth(480.0);
  CHECK(compact.has_value());
  CHECK(compact->x == layout.toolbarTitleX);
  CHECK(compact->right() <=
        layout.transportBoundsForWidth(480.0).x - layout.compactToolbarGap);
  CHECK(!layout.compactProjectTitleBoundsForWidth(720.0).has_value());
}

TEST_CASE("native scene renders phoneme unit pitch and full character dock") {
  NativeUiFixture fixture;
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  region->lyrics.front().surface = U"こ";
  region->lyrics.front().language = seam::domain::Language::Japanese;
  region->unitSelectionOverrides.push_back(seam::domain::UnitSelectionOverride{
      .startKey = seam::domain::PhonemeKey{.noteId = fixture.noteId, .ordinal = 0U},
      .tokenCount = 2U,
          .unitId = "かな👩‍🎤中文Á超長いユニット名",
      .renderer = seam::domain::UnitRendererKind::ClassicPsola,
      .locked = true,
  });
  CHECK(region->pitchAutomation.upsert(seam::domain::PitchAutomationPoint{
      .tick = seam::time::Tick{960}, .cents = 80.0F,
      .interpolation = seam::domain::CurveInterpolation::Smooth}));
  fixture.session.project().settings().characterDisplay =
      seam::domain::CharacterDisplayMode::Full;

  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
  controller.resize(1280.0, 720.0);
  seam::native_ui::EditorScenePainter painter;
  seam::native_ui::PixelSurface character{64U, 96U};
  character.clear(seam::native_ui::Color{40, 30, 48, 255});
  seam::native_ui::PixelSurface target{1280U, 720U};
  seam::native_ui::RasterCanvas canvas{target, 1.0};
  auto state = controller.sceneState();
  state.characterName = "Character 01";
  state.characterStyle = "emo-low-poly";
  state.characterPortrait = &character;
  painter.paint(canvas, controller.pianoRoll(), state);
  controller.setCharacterPortrait(&character);
  controller.rebuildAccessibilityTree();
  const auto& liveTree = controller.accessibilityTree().root();
  const auto liveTimeline = std::find_if(
      liveTree.children.begin(), liveTree.children.end(), [](const auto& child) {
        return child.id == "timeline";
      });
  CHECK(liveTimeline != liveTree.children.end());
  CHECK(liveTimeline->bounds.width == 1280.0);
  CHECK(!state.phonemes.tokens.empty());
  CHECK(state.unitOverrides.size() == 1U);
  CHECK(state.pitchAutomation.size() == 1U);
  CHECK(target.checksum() != 0U);
  if (const auto* captureRoot =
          std::getenv("SEAM_NATIVE_UI_CHARACTER_CAPTURE_DIR");
      captureRoot != nullptr && *captureRoot != '\0') {
    const std::filesystem::path directory{captureRoot};
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    CHECK(!error);
    CHECK(target.writePpm(directory / "character-dock-full.ppm"));
  }
}

TEST_CASE("native scene paints exact voicebank browser cards") {
  NativeUiFixture fixture;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId};
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
  card.disabledUnitCount = 1U;
  card.hasSustain = true;
  card.hasRelease = true;
  controller.setVoicebankCards({card});
  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::V, .modifiers = {}, .repeat = false}));
  CHECK(controller.voicebankBrowserVisible());
  controller.resize(1280.0, 720.0);
  seam::native_ui::EditorScenePainter painter;
  seam::native_ui::PixelSurface surface{1280U, 720U};
  seam::native_ui::RasterCanvas canvas{surface, 1.0};
  painter.paint(canvas, controller.pianoRoll(), controller.sceneState());
  CHECK(surface.checksum() != 0U);
  if (const auto* captureRoot =
          std::getenv("SEAM_NATIVE_UI_VOICEBANK_CAPTURE_DIR");
      captureRoot != nullptr && *captureRoot != '\0') {
    const std::filesystem::path directory{captureRoot};
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    CHECK(!error);
    CHECK(surface.writePpm(directory / "voicebank-browser.ppm"));
  }
}

TEST_CASE("native voicebank browser routes refresh and standalone recovery") {
  NativeUiFixture fixture;
  std::size_t refreshRequests = 0U;
  std::size_t installerRequests = 0U;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .refreshVoicebanks = [&refreshRequests] {
            ++refreshRequests;
            return seam::core::success();
          },
          .openVoicebankInstaller = [&installerRequests] {
            ++installerRequests;
            return seam::core::success();
          },
      }};

  controller.showVoicebankBrowser();
  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::R, .modifiers = {}, .repeat = false}));
  CHECK(controller.voicebankBrowserVisible());
  CHECK(refreshRequests == 1U);
  CHECK(controller.keyDown(seam::native_ui::KeyEvent{
      .key = seam::native_ui::NativeKey::O, .modifiers = {}, .repeat = false}));
  CHECK(controller.voicebankBrowserVisible());
  CHECK(installerRequests == 1U);

  controller.resize(1280.0, 720.0);
  seam::native_ui::EditorScenePainter painter;
  seam::native_ui::PixelSurface surface{1280U, 720U};
  seam::native_ui::RasterCanvas canvas{surface, 1.0};
  painter.paint(canvas, controller.pianoRoll(), controller.sceneState());
  CHECK(surface.checksum() != 0U);
  if (const auto* captureRoot =
          std::getenv("SEAM_NATIVE_UI_VOICEBANK_CAPTURE_DIR");
      captureRoot != nullptr && *captureRoot != '\0') {
    const std::filesystem::path directory{captureRoot};
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    CHECK(!error);
    CHECK(surface.writePpm(directory / "voicebank-recovery-empty.ppm"));
  }
}

TEST_CASE("native scene exposes transactional audio settings controls") {
  NativeUiFixture fixture;
  std::size_t applyCount = 0U;
  std::optional<seam::authoring::AudioSettings> applied;
  seam::native_ui::NativeEditorController controller{
      fixture.session, fixture.factory, fixture.regionId,
      seam::native_ui::EditorHostCallbacks{
          .applyAudioSettings =
              [&applyCount, &applied](seam::authoring::AudioSettings settings) {
                ++applyCount;
                applied = settings;
                return seam::core::success();
              },
      }};
  controller.resize(1280.0, 720.0);
  controller.setAudioSettings(
      seam::authoring::AudioSettings{
          .deviceId = "threaded-callback-clock",
          .sampleRate = 48000U,
          .blockFrames = 256U,
          .outputChannels = 2U,
          .revision = 3U,
      },
      {
          seam::native_ui::EditorSceneState::AudioDeviceOption{
              .id = "threaded-callback-clock",
              .name = "Callback clock",
              .physical = false,
              .selected = true,
          },
          seam::native_ui::EditorSceneState::AudioDeviceOption{
              .id = "external-device",
              .name = "External device",
              .physical = true,
              .selected = false,
          },
      },
      4U, 2U);
  controller.showAudioSettings();
  CHECK(controller.audioSettingsVisible());

  seam::native_ui::EditorScenePainter painter;
  seam::native_ui::PixelSurface surface{1280U, 720U};
  seam::native_ui::RasterCanvas canvas{surface, 1.0};
  painter.paint(canvas, controller.pianoRoll(), controller.sceneState());
  CHECK(surface.checksum() != 0U);
  if (const auto* captureRoot = std::getenv(
          "SEAM_NATIVE_UI_AUDIO_SETTINGS_CAPTURE_DIR");
      captureRoot != nullptr && *captureRoot != '\0') {
    const std::filesystem::path directory{captureRoot};
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    CHECK(!error);
    CHECK(surface.writePpm(directory / "audio-settings.ppm"));
  }

  controller.rebuildAccessibilityTree();
  const auto& tree = controller.accessibilityTree().root();
  CHECK(seam::native_ui::EditorSemanticTree::containsId(tree,
                                                         "audio.settings"));
  CHECK(seam::native_ui::EditorSemanticTree::containsId(tree,
                                                         "audio.device.0"));
  CHECK(seam::native_ui::EditorSemanticTree::containsId(tree,
                                                         "audio.sample-rate"));
  CHECK(seam::native_ui::EditorSemanticTree::containsId(tree,
                                                         "audio.block-frames"));
  CHECK(seam::native_ui::EditorSemanticTree::containsId(tree,
                                                         "audio.channels"));
  CHECK(controller.dispatchAccessibility(
      "audio.sample-rate", seam::native_ui::SemanticAction::Activate));
  CHECK(applyCount == 1U);
  CHECK(applied.has_value());
  CHECK(applied->sampleRate == 96000U);
  CHECK(controller.dispatchAccessibility(
      "audio.device.1", seam::native_ui::SemanticAction::Activate));
  CHECK(applyCount == 2U);
  CHECK(applied->deviceId == "external-device");

  CHECK(controller.keyDown(
      seam::native_ui::KeyEvent{.key = seam::native_ui::NativeKey::Escape}));
  CHECK(!controller.audioSettingsVisible());
  CHECK(controller.keyDown(
      seam::native_ui::KeyEvent{.key = seam::native_ui::NativeKey::I}));
  CHECK(controller.audioSettingsVisible());
}

TEST_CASE("graphical voicebank studio loads audio and commits validated marker edits") {
  constexpr std::uint32_t sampleRate = 48000U;
  const auto directory = seam::test::support::temporaryDirectory("native-vb-studio");
  std::filesystem::create_directories(directory / "audio");
  const auto tone = seam::test::support::sineWave(sampleRate, 220.0, 0.6);
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      directory / "audio" / "a.wav", sampleRate, tone));

  auto unit = seam::test::support::makeUnit(
      "ja.original.a3.a.01", {"a"}, "audio/a.wav", 57,
      seam::voicebank::UnitKind::Sustain, tone.size());
  unit.alias = "a sustain";
  auto manifest = seam::test::support::makeManifest({unit});
  seam::voicebank::ManifestJsonCodec codec;
  const auto manifestPath = directory / "manifest.json";
  CHECK(codec.save(manifest, manifestPath));

  seam::native_ui::VoicebankStudioController controller;
  CHECK(controller.openManifest(manifestPath, 1200.0, 800.0));
  CHECK(controller.selectedUnit() != nullptr);
  CHECK(!controller.microscope().waveform().empty());
  CHECK(!controller.microscope().spectrogram().decibels.empty());

  const auto licensePath = directory / "provider-agreement.txt";
  CHECK(seam::core::durableAtomicWriteText(licensePath, "synthetic-ui-license"));
  const auto licenseDigest = seam::core::sha256File(licensePath);
  CHECK(licenseDigest);
  seam::voicebank_production::VoicebankProductionProject productionProject{
      .projectId = "native-studio-production",
      .inventoryId = "native-studio-inventory",
      .inventorySha256 = std::string(64U, 'a'),
      .selectedSourceStrategyId = "human-contract-recording",
      .licenseLocator = licensePath.string(),
      .licenseSha256 = licenseDigest.value(),
      .immutableAssetRoot = "assets",
  };
  productionProject.sourceStrategies.push_back(
      seam::voicebank_production::SourceStrategyAssessment{
          .id = "human-contract-recording",
          .kind = seam::voicebank_production::SourceStrategyKind::HumanRecording,
          .rights = seam::voicebank_production::Feasibility::Pass,
          .coverage = seam::voicebank_production::Feasibility::Pass,
          .listening = seam::voicebank_production::Feasibility::Pass,
          .permissions = {.sourceUse = true, .transformation = true,
                          .singingBankRedistribution = true,
                          .commercialRenders = true},
          .licenseLocator = licensePath.string(),
          .licenseSha256 = licenseDigest.value(),
          .evidenceState = "SYNTHETIC_TEST_ONLY",
      });
  productionProject.unitAssignments.push_back({
      .coverageKey = "sustain:a", .pitchLayer = 57,
      .promptId = "prompt-a3", .plannedTakeId = "take-a3",
  });
  productionProject.operators.push_back(
      {.operatorId = "operator-a", .role = "PRODUCER"});
  const auto workspace = directory / "production-workspace";
  seam::voicebank_production::ProductionProjectRepository productionRepository{workspace};
  CHECK(productionRepository.initialize(
      productionProject,
      {.action = "create", .subjectId = productionProject.projectId,
       .operatorId = "operator-a", .occurredAtUtc = "2026-08-31T11:00:00Z"}));
  seam::native_ui::VoicebankStudioController productionOnlyController;
  CHECK(productionOnlyController.openProductionProject(
      workspace, productionProject.inventorySha256, "operator-a"));
  CHECK(productionOnlyController.selectableUnitCount() == 1U);
  CHECK(productionOnlyController.selectedProductionAssignment() != nullptr);
  CHECK(productionOnlyController.selectedProductionAssignment()->promptId ==
        "prompt-a3");
  seam::native_ui::PixelSurface productionOnlySurface{720U, 520U};
  seam::native_ui::RasterCanvas productionOnlyCanvas{productionOnlySurface, 1.0};
  seam::native_ui::VoicebankStudioScenePainter productionOnlyPainter;
  productionOnlyPainter.paint(productionOnlyCanvas, productionOnlyController);
  CHECK(productionOnlySurface.checksum() != 0U);
  CHECK(controller.openProductionProject(
      workspace, productionProject.inventorySha256, "operator-a"));
  CHECK(controller.productionProject() != nullptr);
  CHECK(controller.productionQueues().missing == 1U);

  const auto takePath = directory / "accepted-take.wav";
  auto takeWriter = seam::voicebank::WavStreamWriter::create(
      takePath, seam::voicebank::WavOutputFormat{
                    48000U, 1U, seam::voicebank::WavSampleFormat::Pcm24});
  CHECK(takeWriter);
  CHECK(takeWriter.value()->writeFrames(tone));
  CHECK(takeWriter.value()->finalize());
  CHECK(productionOnlyController.inspectSelectedProductionTake(takePath));
  CHECK(productionOnlyController.takeInspection().has_value());
  CHECK(productionOnlyController.takeInspection()->accepted());
  CHECK(controller.inspectTake(takePath, 57));
  CHECK(controller.takeInspection().has_value());
  CHECK(controller.takeInspection()->accepted());
  const auto inspectionPath = controller.persistTakeInspection(takePath);
  CHECK(inspectionPath);
  const auto inspectionText = seam::core::readTextFileLimited(
      inspectionPath.value(), 64U * 1024U);
  CHECK(inspectionText);
  CHECK(inspectionText.value().find("\"status\": \"ACCEPTED\"") !=
        std::string::npos);
  CHECK(inspectionText.value().find("\"takeSha256\": ") !=
        std::string::npos);
  CHECK(!controller.persistTakeInspection(takePath));
  CHECK(controller.importSelectedTake(takePath, "2026-08-31T11:01:00Z"));
  CHECK(controller.productionProject()->takes.size() == 1U);
  CHECK(controller.productionProject()->reviews.size() == 1U);
  CHECK(controller.productionProject()->reviews.front().result == "PASS");
  CHECK(controller.productionProject()->reviews.front().reviewedAtUtc ==
        "2026-08-31T11:01:00Z");
  CHECK(controller.productionQueues().markerReview == 1U);
  const auto productionRawHash =
      controller.productionProject()->takes.front().rawAssetSha256;
  const auto productionRevision = controller.applySelectedProductionOperation(
      {.kind = seam::voicebank_production::OperationKind::NormalizeGain,
       .targetPeak = 0.8F},
      "2026-08-31T11:01:30Z");
  CHECK(productionRevision);
  CHECK(controller.productionProject()->derivedRevisions.size() == 1U);
  CHECK(controller.productionProject()->lastDurableGeneration == 3U);

  const auto changedTakePath = directory / "changed-take.wav";
  auto changedTakeWriter = seam::voicebank::WavStreamWriter::create(
      changedTakePath, seam::voicebank::WavOutputFormat{
                           48000U, 1U, seam::voicebank::WavSampleFormat::Pcm24});
  CHECK(changedTakeWriter);
  CHECK(changedTakeWriter.value()->writeFrames(tone));
  CHECK(changedTakeWriter.value()->finalize());
  CHECK(controller.inspectTake(changedTakePath, 57));
  auto replacementWriter = seam::voicebank::WavStreamWriter::create(
      changedTakePath, seam::voicebank::WavOutputFormat{
                           48000U, 1U, seam::voicebank::WavSampleFormat::Pcm24});
  CHECK(replacementWriter);
  CHECK(replacementWriter.value()->writeFrames(
      seam::test::support::sineWave(sampleRate, 330.0, 0.6)));
  CHECK(replacementWriter.value()->finalize());
  CHECK(!controller.persistTakeInspection(changedTakePath));

  const auto oldOnset = controller.selectedUnit()->markers.vowelOnset;
  const auto x = controller.microscope().frameToPixel(oldOnset + 8);
  CHECK(controller.moveSelectedMarker(seam::ui::AcousticMarkerKind::VowelOnset, x));
  CHECK(controller.dirty());
  CHECK(controller.selectedUnit()->markers.vowelOnset != oldOnset);
  CHECK(controller.save());
  CHECK(!controller.dirty());
  CHECK(controller.productionProject()->takes.front().rawAssetSha256 ==
        productionRawHash);
  CHECK(controller.productionProject()->metadataRevisions.size() == 1U);
  CHECK(controller.productionProject()->metadataRevisions.front().rawAssetSha256 ==
        productionRawHash);
  CHECK(controller.productionProject()->lastDurableGeneration == 4U);

  const auto firstSavedOnset = controller.selectedUnit()->markers.vowelOnset;
  const auto secondX = controller.microscope().frameToPixel(firstSavedOnset + 8);
  CHECK(controller.moveSelectedMarker(
      seam::ui::AcousticMarkerKind::VowelOnset, secondX));
  CHECK(controller.save());
  CHECK(controller.productionProject()->metadataRevisions.size() == 2U);
  const auto revisitX = controller.microscope().frameToPixel(firstSavedOnset);
  CHECK(controller.moveSelectedMarker(
      seam::ui::AcousticMarkerKind::VowelOnset, revisitX));
  CHECK(controller.save());
  CHECK(controller.productionProject()->metadataRevisions.size() == 3U);
  CHECK(controller.productionProject()->lastDurableGeneration == 6U);
  const auto exportedProduction = controller.exportProductionInputs(
      directory / "u57-inputs", "2026-08-31T11:02:00Z");
  CHECK(exportedProduction);
  CHECK(std::filesystem::is_regular_file(exportedProduction.value().briefPath));
  CHECK(controller.productionProject()->lastDurableGeneration == 7U);

  seam::native_ui::PixelSurface surface{1200U, 800U};
  seam::native_ui::RasterCanvas canvas{surface, 1.0};
  seam::native_ui::VoicebankStudioScenePainter painter;
  painter.paint(canvas, controller, true, "THREADED TEST INPUT");
  CHECK(surface.checksum() != 0U);
  if (const auto* captureRoot =
          std::getenv("SEAM_NATIVE_UI_VOICEBANK_CAPTURE_DIR");
      captureRoot != nullptr && *captureRoot != '\0') {
    const std::filesystem::path captureDirectory{captureRoot};
    std::error_code error;
    std::filesystem::create_directories(captureDirectory, error);
    CHECK(!error);
    CHECK(surface.writePpm(captureDirectory / "voicebank-studio.ppm"));
  }

  const auto reloaded = codec.load(manifestPath);
  CHECK(reloaded);
  CHECK(reloaded.value().units.front().markers.vowelOnset ==
        controller.selectedUnit()->markers.vowelOnset);
}

TEST_CASE("voicebank studio marker labels avoid overlap at minimum width") {
  const std::vector<seam::ui::AcousticMarkerVisual> markers{
      {seam::ui::AcousticMarkerKind::AudioOffset, "offset", 0, 272.0},
      {seam::ui::AcousticMarkerKind::ConsonantEnd, "consonant", 1, 278.0},
      {seam::ui::AcousticMarkerKind::VowelOnset, "vowel", 2, 284.0},
      {seam::ui::AcousticMarkerKind::StableStart, "stable", 3, 290.0},
      {seam::ui::AcousticMarkerKind::LoopStart, "loop-start", 4, 296.0},
      {seam::ui::AcousticMarkerKind::LoopEnd, "loop-end", 5, 302.0},
      {seam::ui::AcousticMarkerKind::ReleaseStart, "release", 6, 308.0},
      {seam::ui::AcousticMarkerKind::AudioEnd, "end", 7, 314.0},
  };
  const seam::ui::Rect waveform{270.0, 100.0, 182.0, 147.0};
  const auto labels = seam::native_ui::voicebankStudioMarkerLabelBounds(
      markers, waveform);
  CHECK(labels.size() == markers.size());
  for (const auto& label : labels) {
    CHECK(label.x >= waveform.x);
    CHECK(label.right() <= waveform.right());
    CHECK(label.y >= waveform.y);
    CHECK(label.bottom() <= waveform.y + 70.0);
  }
  for (std::size_t left = 0U; left < labels.size(); ++left) {
    for (std::size_t right = left + 1U; right < labels.size(); ++right) {
      CHECK(!labels[left].intersects(labels[right]));
    }
  }
}

TEST_CASE("voicebank studio marker labels use Unicode display width") {
  const std::vector<seam::ui::AcousticMarkerVisual> markers{
      {seam::ui::AcousticMarkerKind::VowelOnset, "かな", 0, 100.0},
  };
  const seam::ui::Rect waveform{0.0, 0.0, 240.0, 80.0};
  const auto labels = seam::native_ui::voicebankStudioMarkerLabelBounds(
      markers, waveform);
  CHECK(labels.size() == 1U);
  const auto displayWidth = seam::text::utf8DisplayWidth(markers.front().label);
  CHECK(labels.front().width >=
        static_cast<double>(displayWidth) * 3.8 + 4.0 - 1e-9);
  CHECK(labels.front().width <
        static_cast<double>(markers.front().label.size()) * 3.8 + 4.0);
}
