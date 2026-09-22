#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/synthesis/unit_selection.hpp"
#include "seam/text/text_engine.hpp"

#include <cstdlib>
#include <filesystem>
#include <utility>

namespace {
using namespace seam;

struct Fixture final {
  application::ProjectFactory factory{7100U};
  domain::RegionId region;
  domain::NoteId note;
  application::EditorSession session;
  std::string context;
  voicebank::AudioBuffer audio{.sampleRate = 48000U, .channels = 1U,
      .interleaved = test::support::sineWave(48000U, 220.0, 0.05)};
  voicebank::Unit unit{test::support::makeUnit("voice-" + std::string(240U, 'W') + "-日本語", {"a"},
      "audio/a.wav", 60U, voicebank::UnitKind::Cv, audio.frameCount())};
  std::size_t played{0U}, edited{0U};

  Fixture() : session(makeProject()) {
    synthesis::UnitPlanEntry entry;
    entry.rationale.acoustic = true; entry.rationale.joined = true;
    entry.rationale.predecessor = "previous-" + std::string(240U, 'M');
    entry.rationale.incomingCost = 0.125; entry.rationale.cumulativeCost = 2.125;
    entry.rationale.evidenceHash = std::string(64U, 'a');
    context = synthesis::describeUnitSelection(entry) + "\nDESTINATION / 日本語 / 한국어 / e\xcc\x81 / 🎵\r\n";
    for (int i = 0; i < 8; ++i) context += "Captured decision " + std::to_string(i) + ": source-boundary proxy; not listening qualification.\n";
  }
  domain::Project makeProject() {
    auto project = factory.createProject("Sample details");
    const auto track = factory.addVocalTrack(project, "Voice");
    region = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{1920});
    auto [lyric, value] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U, U"あ", domain::Language::Japanese);
    note = value.id;
    project.findRegion(region)->notes.push_back(value); project.findRegion(region)->lyrics.push_back(lyric);
    return project;
  }
  native_ui::EditorHostCallbacks callbacks() {
    return {
      .loadSampleMicroscope = [this](domain::PhonemeKey) -> core::Result<native_ui::SampleMicroscopeData> {
        return native_ui::SampleMicroscopeData{unit, audio, context};
      },
      .microscopeUnitChanged = [this](domain::PhonemeKey, const voicebank::Unit&) { ++edited; return core::success(); },
      .playMicroscopeSample = [this](const voicebank::Unit&, const voicebank::AudioBuffer&) { ++played; return core::success(); },
    };
  }
};

void contained(ui::Rect outer, ui::Rect inner) {
  CHECK(inner.width > 0.0); CHECK(inner.height > 0.0);
  CHECK(inner.x >= outer.x); CHECK(inner.y >= outer.y);
  CHECK(inner.right() <= outer.right() + 0.001); CHECK(inner.bottom() <= outer.bottom() + 0.001);
}

std::string readAll(native_ui::NativeEditorController& controller) {
  std::string result;
  for (;;) {
    const auto view = *controller.sceneState().sampleMicroscope;
    for (const auto& line : view.detailsLines) result += line;
    if (view.detailsPage + 1U == view.detailsPageCount) break;
    CHECK(controller.dispatchAccessibility("microscope.next", native_ui::SemanticAction::Activate));
  }
  return result;
}

void capture(native_ui::NativeEditorController& controller, std::uint32_t width, std::uint32_t height, const char* name) {
  const auto* root = std::getenv("SEAM_MICROSCOPE_DETAILS_CAPTURE_DIR");
  if (!root || !*root) return;
  const std::filesystem::path directory{root}; std::error_code error;
  std::filesystem::create_directories(directory, error); CHECK(!error);
  auto engine = text::TextEngine::createSystem(); CHECK(engine);
  native_ui::PixelSurface surface{width, height};
  native_ui::RasterCanvas canvas{surface, 1.0, engine.value().get()};
  native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState());
  CHECK(surface.writePpm(directory / name));
}
}  // namespace

TEST_CASE("sample microscope plots and paged details fit compact and desktop windows without losing text") {
  using namespace seam;
  // Bind by reference: GCC rejects a per-iteration copy under -Werror
  // (range-loop-construct) even though the initializer list elements are const.
  for (const auto& size : {std::pair{480U, 320U}, {700U, 480U}, {1280U, 720U}}) {
    Fixture fixture; const auto original = fixture.session.project();
    native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.region, fixture.callbacks()};
    controller.resize(size.first, size.second);
    CHECK(controller.openSampleMicroscope({fixture.note, 0U}));
    const native_ui::EditorSceneLayout layout;
    const auto panel = layout.microscopePanelBounds(size.first, size.second);
    contained({0.0, 0.0, static_cast<double>(size.first), static_cast<double>(size.second)}, panel);
    contained(panel, controller.sampleMicroscope()->waveformBounds());
    contained(panel, controller.sampleMicroscope()->spectrogramBounds());
    CHECK(controller.sampleMicroscope()->waveformBounds().bottom() < controller.sampleMicroscope()->spectrogramBounds().y);
    contained(panel, layout.microscopeCloseBounds(size.first, size.second));
    contained(panel, layout.microscopeDetailsToggleBounds(size.first, size.second));
    CHECK(layout.microscopeDetailsToggleBounds(size.first, size.second).right() < layout.microscopeCloseBounds(size.first, size.second).x);
    if (size.first == 480U) {
      capture(controller, size.first, size.second, "compact-waveform.ppm");
      native_ui::PixelSurface surface{480U, 320U}; native_ui::RasterCanvas canvas{surface};
      native_ui::EditorScenePainter{}.paint(canvas, controller.pianoRoll(), controller.sceneState());
      // This point is in the background Time Map button and in the inspector
      // header's empty gap. The modal must paint over that background control.
      CHECK(surface.pixels()[74U * 480U + 70U] != native_ui::EditorSceneTheme{}.panel.bgra());
    }
    CHECK(controller.dispatchAccessibility("microscope.details", native_ui::SemanticAction::Activate));
    const auto view = *controller.sceneState().sampleMicroscope;
    CHECK(view.detailsVisible);
    if (size.first == 480U) CHECK(view.detailsPageCount > 1U);
    contained(panel, layout.microscopeDetailsBounds(size.first, size.second));
    contained(panel, layout.microscopeDetailsPageBounds(size.first, size.second, true));
    CHECK(layout.microscopeDetailsBounds(size.first, size.second).bottom() < layout.microscopeDetailsPageBounds(size.first, size.second, true).y);
    CHECK(!controller.dispatchAccessibility("microscope.previous", native_ui::SemanticAction::Activate));
    if (size.first == 480U) capture(controller, size.first, size.second, "compact-details.ppm");
    if (size.first == 1280U) capture(controller, size.first, size.second, "desktop-details.ppm");
    CHECK(readAll(controller) == view.detailsText);
    CHECK(view.detailsText.find(fixture.unit.id) != std::string::npos);
    CHECK(view.detailsText.find(fixture.context) != std::string::npos);
    CHECK(!controller.dispatchAccessibility("microscope.next", native_ui::SemanticAction::Activate));
    CHECK(fixture.session.project() == original); CHECK(fixture.session.selection().empty()); CHECK(!fixture.session.canUndo());
    CHECK(fixture.played == 0U); CHECK(fixture.edited == 0U);
  }
}

TEST_CASE("sample details use modal keyboard pointer and accessibility controls and preserve the captured snapshot") {
  using namespace seam; Fixture fixture;
  const auto original = fixture.session.project();
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.region, fixture.callbacks()};
  controller.resize(480.0, 320.0); CHECK(controller.openSampleMicroscope({fixture.note, 0U}));
  CHECK(controller.dispatchAccessibility("microscope.details", native_ui::SemanticAction::SetFocus));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Enter}));
  CHECK(controller.sceneState().sampleMicroscope->detailsVisible);
  CHECK(controller.accessibilityTree().virtualizedNoteCount() == 0U);
  CHECK(controller.accessibilityTree().materializeNotes(0U, 100U).empty());
  for (const auto reverse : {false, true}) {
    for (std::size_t i = 0U; i < 12U; ++i) {
      CHECK(controller.keyDown({.key = native_ui::NativeKey::Tab, .modifiers = {.shift = reverse}}));
      const auto* focused = controller.accessibilityTree().focusedNode();
      CHECK(focused != nullptr); CHECK(focused->id.starts_with("microscope."));
    }
  }
  CHECK(!native_ui::EditorSemanticTree::containsId(controller.accessibilityTree().root(), "microscope.waveform"));
  CHECK(native_ui::EditorSemanticTree::containsId(controller.accessibilityTree().root(), "microscope.readout"));
  CHECK(!controller.dispatchAccessibility("microscope.waveform", native_ui::SemanticAction::Activate));
  CHECK(!controller.dispatchAccessibility("toolbar.transport", native_ui::SemanticAction::Activate));
  for (const auto& [id, value] : {std::pair{"note." + fixture.note.toString(), "changed lyric"},
      {std::string{"note.fffffffffffffff0"}, "stale reference"},
      {std::string{"toolbar.tempo"}, "130"}, {std::string{"toolbar.meter"}, "7/8"}}) {
    const auto changed = controller.setAccessibilityValue(id, value);
    CHECK(!changed); CHECK(changed.error().code == core::ErrorCode::Conflict);
    CHECK(fixture.session.project() == original); CHECK(fixture.session.selection().empty());
    CHECK(!fixture.session.canUndo()); CHECK(!controller.textInputActive());
  }
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Delete}));
  const auto timelineBefore = controller.pianoRoll().timeline().tickToPixel(time::Tick{0});
  controller.scroll(120.0, 120.0, {100.0, 120.0}, {});
  CHECK(controller.pianoRoll().timeline().tickToPixel(time::Tick{0}) == timelineBefore);
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Tab}));
  CHECK(controller.accessibilityTree().focusedNode()->id.starts_with("microscope."));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Right}));
  CHECK(controller.sceneState().sampleMicroscope->detailsPage == 1U);
  const native_ui::EditorSceneLayout layout;
  for (const auto next : {false, true}) {
    const auto bounds = layout.microscopeDetailsPageBounds(480.0, 320.0, next);
    CHECK(controller.pointerDown({{bounds.x + 1.0, bounds.y + 1.0}, native_ui::PointerButton::Left, {}, 1}));
    CHECK(controller.sceneState().sampleMicroscope->detailsPage == (next ? 1U : 0U));
  }
  const auto old = controller.sceneState().sampleMicroscope->detailsText;
  fixture.context = "Metadata-only replacement, not acoustic qualification";
  CHECK(controller.sceneState().sampleMicroscope->detailsText == old);
  CHECK(controller.pointerDown({{80.0, 100.0}, native_ui::PointerButton::Left, {}, 2}));
  CHECK(fixture.played == 0U); CHECK(fixture.edited == 0U);
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Left}));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Escape}));
  CHECK(controller.sampleMicroscopeOpen()); CHECK(!controller.sceneState().sampleMicroscope->detailsVisible);
  CHECK(controller.accessibilityTree().virtualizedNoteCount() == 0U);
  const auto details = layout.microscopeDetailsToggleBounds(480.0, 320.0);
  CHECK(controller.pointerDown({{details.x + 1.0, details.y + 1.0}, native_ui::PointerButton::Left, {}, 1}));
  CHECK(controller.sceneState().sampleMicroscope->detailsVisible);
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Escape}));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Escape})); CHECK(!controller.sampleMicroscopeOpen());
  CHECK(!controller.dispatchAccessibility("microscope.next", native_ui::SemanticAction::Activate));
  CHECK(controller.openSampleMicroscope({fixture.note, 0U}));
  CHECK(controller.sceneState().sampleMicroscope->detailsText.find(fixture.context) != std::string::npos);
  const auto close = native_ui::EditorSceneLayout{}.microscopeCloseBounds(480.0, 320.0);
  CHECK(controller.pointerDown({{close.x + 1.0, close.y + 1.0}, native_ui::PointerButton::Left, {}, 1}));
  CHECK(!controller.sampleMicroscopeOpen()); CHECK(fixture.played == 0U);
  controller.rebuildAccessibilityTree();
  CHECK(controller.accessibilityTree().virtualizedNoteCount() == 1U);
  CHECK(fixture.session.project() == original); CHECK(!fixture.session.canUndo());
}

TEST_CASE("sample details reflow without losing the current reading anchor and glyph lines fit actual system text") {
  using namespace seam; Fixture fixture;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.region, fixture.callbacks()};
  controller.resize(480.0, 320.0); CHECK(controller.openSampleMicroscope({fixture.note, 0U}));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::D}));
  CHECK(controller.keyDown({.key = native_ui::NativeKey::Right}));
  const auto anchor = controller.sceneState().sampleMicroscope->detailsLines.front();
  controller.resize(1280.0, 720.0);
  const auto resized = *controller.sceneState().sampleMicroscope;
  std::string page; for (const auto& line : resized.detailsLines) page += line;
  CHECK(page.find(anchor) != std::string::npos);
  controller.resize(480.0, 320.0);
  while (controller.sceneState().sampleMicroscope->detailsPage > 0U) CHECK(controller.keyDown({.key = native_ui::NativeKey::Left}));
  auto engine = text::TextEngine::createSystem(); CHECK(engine);
  const auto body = native_ui::EditorSceneLayout{}.microscopeDetailsBounds(480.0, 320.0);
  for (;;) {
    const auto view = *controller.sceneState().sampleMicroscope;
    const auto lineHeight = native_ui::EditorSceneLayout{}.microscopeDetailsLineHeight;
    CHECK(static_cast<double>(view.detailsLines.size()) * lineHeight <= body.height);
    for (auto line : view.detailsLines) {
      while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
      if (line.empty()) continue;
      const auto rendered = engine.value()->renderShared(line, {.pixelHeight = 12.0F}); CHECK(rendered);
      CHECK(rendered.value()->bitmap.width <= body.width);
      if (rendered.value()->bitmap.height > lineHeight)
        throw test::Failure{"Glyph bitmap height " + std::to_string(rendered.value()->bitmap.height) + ": " + line};
    }
    if (view.detailsPage + 1U == view.detailsPageCount) break;
    CHECK(controller.keyDown({.key = native_ui::NativeKey::Right}));
  }
}

TEST_CASE("sample inspection rejects oversized and invalid UTF8 context without replacing an open capture") {
  using namespace seam; Fixture fixture;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.region, fixture.callbacks()};
  CHECK(controller.openSampleMicroscope({fixture.note, 0U}));
  const auto original = controller.sceneState().sampleMicroscope->detailsText;
  fixture.context.assign(65537U, 'x'); CHECK(!controller.openSampleMicroscope({fixture.note, 0U}));
  CHECK(controller.sceneState().sampleMicroscope->detailsText == original);
  fixture.context = std::string(1U, static_cast<char>(0xff)); CHECK(!controller.openSampleMicroscope({fixture.note, 0U}));
  CHECK(controller.sceneState().sampleMicroscope->detailsText == original);
  fixture.context.clear(); CHECK(controller.openSampleMicroscope({fixture.note, 0U}));
  CHECK(controller.sceneState().sampleMicroscope->detailsText.find("Destination unknown") != std::string::npos);
  fixture.context.assign(65536U - fixture.unit.id.size(), 'x');
  CHECK(controller.openSampleMicroscope({fixture.note, 0U}));
  CHECK(controller.sceneState().sampleMicroscope->detailsText.ends_with(fixture.context));
}
