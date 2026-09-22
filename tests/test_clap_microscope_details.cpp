#include "test_framework.hpp"
#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/synthesis/unit_selection.hpp"

#include <chrono>
#include <thread>
#include <utility>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required
#endif

namespace {
using namespace seam;
clap_editor::EditorRuntime runtimeFixture() {
  return clap_editor::EditorRuntime{std::nullopt, {},
      {{std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK}, voicebank::VoicebankRootKind::Development}}};
}
std::shared_ptr<const clap_editor::RenderedPreview> ready(clap_editor::EditorRuntime& runtime) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
  while (std::chrono::steady_clock::now() < deadline) {
    const auto preview = runtime.renderedPreview();
    if (preview && preview->revision == runtime.revision() && preview->status == clap_editor::PreviewStatus::Ready) return preview;
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  throw test::Failure{"CLAP microscope fixture did not publish a current ready preview"};
}
domain::PhonemeKey keyFor(clap_editor::EditorRuntime& runtime, const clap_editor::RenderedPreview& preview) {
  const auto project = runtime.projectCopy();
  const auto* region = project.findRegion(runtime.regionId()); CHECK(region != nullptr);
  const auto phones = phonemizer::inspectPronunciation(*region);
  CHECK(!preview.unitPlan.empty()); CHECK(preview.unitPlan.front().tokenStart < phones.tokens.size());
  return phones.tokens[preview.unitPlan.front().tokenStart].key;
}
void click(clap_editor::EditorRuntime& runtime, ui::Rect bounds) {
  runtime.pointerDown({{bounds.x + 1.0, bounds.y + 1.0}, native_ui::PointerButton::Left, {}, 1});
}
}

TEST_CASE("CLAP microscope shares full captured selection details and working pointer keyboard and AX controls") {
  using namespace seam; auto runtime = runtimeFixture();
  const auto preview = ready(runtime); const auto key = keyFor(runtime, *preview);
  const auto project = runtime.projectCopy(); const auto revision = runtime.revision();
  runtime.resize(480.0, 320.0); CHECK(runtime.openSampleMicroscope(key));
  CHECK(runtime.controller().sampleMicroscopeOpen()); CHECK(runtime.sampleMicroscope() != nullptr);
  CHECK(runtime.selectedUnitId() == std::optional<std::string>{preview->unitPlan.front().unitId});
  CHECK(runtime.dispatchAccessibility("microscope.details", native_ui::SemanticAction::Activate));
  auto view = *runtime.controller().sceneState().sampleMicroscope;
  CHECK(view.detailsVisible);
  CHECK(view.detailsText.find(synthesis::describeUnitSelection(preview->unitPlan.front())) != std::string::npos);
  const auto expected = view.detailsText; std::string complete;
  for (;;) {
    view = *runtime.controller().sceneState().sampleMicroscope;
    for (const auto& line : view.detailsLines) complete += line;
    if (view.detailsPage + 1U == view.detailsPageCount) break;
    CHECK(runtime.dispatchAccessibility("microscope.next", native_ui::SemanticAction::Activate));
  }
  CHECK(complete == expected);
  const native_ui::EditorSceneLayout layout;
  click(runtime, layout.microscopeDetailsToggleBounds(480.0, 320.0));
  CHECK(!runtime.controller().sceneState().sampleMicroscope->detailsVisible);
  runtime.keyDown({.key = native_ui::NativeKey::D});
  CHECK(runtime.controller().sceneState().sampleMicroscope->detailsVisible);
  runtime.resize(700.0, 480.0);
  CHECK(runtime.controller().sceneState().sampleMicroscope->detailsText == expected);
  runtime.resize(480.0, 320.0);
  CHECK(runtime.sampleMicroscope()->spectrogramBounds().bottom() <= layout.microscopePanelBounds(480.0, 320.0).bottom());
  click(runtime, layout.microscopeCloseBounds(480.0, 320.0));
  CHECK(!runtime.sampleMicroscopeOpen()); CHECK(!runtime.selectedUnitId());
  CHECK(runtime.projectCopy() == project); CHECK(runtime.revision() == revision);
}

TEST_CASE("CLAP microscope isolates complete focus traversal retained SetValue and background edit shortcuts") {
  using namespace seam; auto runtime = runtimeFixture();
  const auto preview = ready(runtime); const auto key = keyFor(runtime, *preview);
  const auto project = runtime.projectCopy(); const auto revision = runtime.revision();
  CHECK(runtime.openSampleMicroscope(key)); runtime.keyDown({.key = native_ui::NativeKey::D});
  CHECK(runtime.accessibilitySnapshot().virtualizedNoteCount == 0U);
  CHECK(runtime.accessibilityNotes(0U, 100U).empty());
  for (const auto reverse : {false, true}) {
    for (std::size_t i = 0U; i < 12U; ++i) {
      runtime.keyDown({.key = native_ui::NativeKey::Tab, .modifiers = {.shift = reverse}});
      const auto focus = runtime.accessibilityFocusedNode(); CHECK(focus);
      CHECK(focus->id.starts_with("microscope."));
    }
  }
  for (const auto& [id, value] : {std::pair{"note." + key.noteId.toString(), "あ"},
      {std::string{"note.fffffffffffffff0"}, "stale reference"},
      {std::string{"toolbar.tempo"}, "130"}, {std::string{"toolbar.meter"}, "7/8"}}) {
    const auto changed = runtime.setAccessibilityValue(id, value);
    CHECK(!changed); CHECK(changed.error().code == core::ErrorCode::Conflict);
    CHECK(!runtime.controller().textInputActive());
  }
  for (const auto keyCode : {native_ui::NativeKey::S, native_ui::NativeKey::R,
      native_ui::NativeKey::Delete, native_ui::NativeKey::Z}) runtime.keyDown({.key = keyCode});
  CHECK(runtime.projectCopy() == project); CHECK(runtime.revision() == revision);
  runtime.keyDown({.key = native_ui::NativeKey::Escape}); CHECK(runtime.sampleMicroscopeOpen());
  runtime.keyDown({.key = native_ui::NativeKey::Escape}); CHECK(!runtime.sampleMicroscopeOpen());
  CHECK(runtime.accessibilitySnapshot().virtualizedNoteCount > 0U);
  CHECK(!runtime.dispatchAccessibility("microscope.details", native_ui::SemanticAction::Activate));
}
