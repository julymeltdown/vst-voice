#include "test_framework.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/native_ui/accessibility_tree.hpp"
#include "seam/native_ui/editor_semantics.hpp"
#include "seam/native_ui/native_window.hpp"

#if __has_include("seam/clap_editor/embedded_view.hpp")
#include "seam/clap_editor/embedded_view.hpp"
#include "seam/native_ui/design/sing_shell.hpp"
#endif

#import <AppKit/AppKit.h>

#include <cstdint>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <unistd.h>

@interface NSView (SEAMAccessibilityChildren)
- (NSArray*)accessibilityChildren;
- (BOOL)accessibilityFocused;
@end

@interface NSView (SEAMAccessibilityEnabled)
- (BOOL)isAccessibilityEnabled;
@end

@interface NSAccessibilityElement (SEAMAccessibilityEnabled)
- (BOOL)isAccessibilityEnabled;
- (void)setAccessibilityValue:(id)value;
- (BOOL)accessibilityIsAttributeSettable:(NSString*)attribute;
- (void)accessibilitySetValue:(id)value forAttribute:(NSString*)attribute;
@end

@interface NSView (SEAMTextInputClient)
- (NSRange)markedRange;
- (NSRange)selectedRange;
- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selectedRange
      replacementRange:(NSRange)replacementRange;
- (void)insertText:(id)string replacementRange:(NSRange)replacementRange;
- (void)doCommandBySelector:(SEL)selector;
- (NSRect)firstRectForCharacterRange:(NSRange)range
                         actualRange:(NSRangePointer)actualRange;
@end

namespace {

class AppKitAccessibilityClient final : public seam::native_ui::INativeWindowClient {
public:
  seam::application::ProjectFactory factory{30000U};
  seam::domain::RegionId regionId{};
  seam::application::EditorSession session;
  seam::ui::PianoRollModel model;
  seam::native_ui::AccessibilityTree tree;
  // A custom surface tree (like the SING shell's) whose notes are delegated to 'tree'.
  seam::native_ui::AccessibilityTree shellTree;
  bool presentShell{false};
  std::string lastId;
  std::string lastValue;
  std::filesystem::path openedPath;
  std::u32string lastComposition;
  std::u32string lastCommit;
  seam::ui::CompositionSelection lastCompositionSelection{};
  seam::native_ui::SemanticAction lastAction{
      seam::native_ui::SemanticAction::Activate};
  bool actionReceived{false};
  bool valueReceived{false};
  bool transportActivationMovesFocus{false};

  AppKitAccessibilityClient()
      : session(makeProject()), model(session, factory, regionId) {
    model.pitch().setTopMidiKey(127);
    model.pitch().setRowHeight(12.0);
    model.setViewport(seam::ui::PianoRollViewport{
        .bounds = seam::ui::Rect{0.0, 0.0, 40000.0, 20000.0},
        .keyboardWidth = 0.0,
    });
    seam::native_ui::EditorSceneState state;
    state.projectName = "日本語プロジェクト";
    state.logicalWidth = 1440.0;
    state.logicalHeight = 900.0;
    state.renderStatus.hasAudibleAudio = true;
    auto* region = session.project().findRegion(regionId);
    if (region != nullptr && !region->notes.empty()) {
      auto& selected = region->notes.front();
      selected.vibrato = {.enabled = true, .startFraction = 0.2F,
          .fadeInFraction = 0.1F, .fadeOutFraction = 0.1F,
          .depthCents = 75.0F, .periodMilliseconds = 50.0F,
          .phaseTurns = 0.25F};
      session.selection().selectOnly(selected.id);
      state.selectedNoteCount = 1U;
      state.vibratoEditable = true;
    }
    tree.rebuild(state, model,
                 seam::native_ui::AccessibilityTreeConfig{
                     .maximumMaterializedNotes = 64U});
  }

  void paint(seam::native_ui::RasterCanvas&) noexcept override {}
  void resized(double, double, double) noexcept override {}
  void pointerDown(const seam::native_ui::PointerEvent&) noexcept override {}
  void pointerMove(const seam::native_ui::PointerEvent&) noexcept override {}
  void pointerUp(const seam::native_ui::PointerEvent&) noexcept override {}
  void scroll(double, double, seam::ui::Point,
              seam::native_ui::InputModifiers) noexcept override {}
  void keyDown(const seam::native_ui::KeyEvent&) noexcept override {}
  void textComposition(std::u32string text,
                       seam::ui::CompositionSelection selection) noexcept override {
    lastComposition = std::move(text);
    lastCompositionSelection = selection;
  }
  void textCommit(std::u32string text) noexcept override {
    lastCommit = std::move(text);
  }
  void textCancel() noexcept override {}
  void openProjectPath(const std::filesystem::path& path) noexcept override {
    openedPath = path;
  }
  [[nodiscard]] std::optional<std::filesystem::path> documentPath()
      const noexcept override {
    return openedPath.empty()
               ? std::nullopt
               : std::optional<std::filesystem::path>{openedPath};
  }

  [[nodiscard]] const seam::native_ui::AccessibilityTree* accessibilityTree()
      const noexcept override {
    return presentShell ? &shellTree : &tree;
  }

  [[nodiscard]] seam::core::Result<void> dispatchAccessibility(
      std::string_view id, seam::native_ui::SemanticAction action) noexcept override {
    lastId = std::string{id};
    lastAction = action;
    actionReceived = true;
    if (presentShell && action == seam::native_ui::SemanticAction::SetFocus) {
      return shellTree.setFocus(id);
    }
    if (action == seam::native_ui::SemanticAction::SetFocus ||
        (action == seam::native_ui::SemanticAction::Activate &&
         id.starts_with("editor.vibrato.handle."))) {
      return tree.setFocus(id);
    }
    if (transportActivationMovesFocus &&
        action == seam::native_ui::SemanticAction::Activate &&
        id == "toolbar.transport") {
      return tree.setFocus("toolbar.tempo");
    }
    return seam::core::success();
  }

  [[nodiscard]] seam::core::Result<void> setAccessibilityValue(
      std::string_view id, std::string_view value) override {
    lastId = std::string{id};
    lastValue = std::string{value};
    valueReceived = true;
    return seam::core::success();
  }

  [[nodiscard]] bool wantsClose() const noexcept override { return false; }

private:
  seam::domain::Project makeProject() {
    auto project = factory.createProject("日本語アクセシビリティ");
    const auto track = factory.addVocalTrack(project, "歌声");
    regionId = factory.addRegion(project, track, "第一節", seam::time::Tick{0},
                                  seam::time::Tick{16000});
    auto* region = project.findRegion(regionId);
    for (std::size_t index = 0U; index < 600U; ++index) {
      auto [lyric, note] = factory.makeNote(
          seam::time::Tick{index == 0U
              ? 0
              : static_cast<std::int64_t>(240U + (index - 1U) * 24U)},
          seam::time::Tick{240}, static_cast<std::uint8_t>(60U + index % 12U),
          U"あ", seam::domain::Language::Japanese);
      region->lyrics.push_back(std::move(lyric));
      region->notes.push_back(std::move(note));
    }
    region->sortNotes();
    return project;
  }
};

id findAccessibilityElement(NSArray* elements, NSString* identifier) {
  for (id element in elements) {
    if ([[element accessibilityIdentifier] isEqualToString:identifier]) {
      return element;
    }
    id nested = findAccessibilityElement([element accessibilityChildren],
                                         identifier);
    if (nested != nil) return nested;
  }
  return nil;
}

void checkNativeRect(NSRect actual, NSRect expected) {
  CHECK_NEAR(actual.origin.x, expected.origin.x, 1e-7);
  CHECK_NEAR(actual.origin.y, expected.origin.y, 1e-7);
  CHECK_NEAR(actual.size.width, expected.size.width, 1e-7);
  CHECK_NEAR(actual.size.height, expected.size.height, 1e-7);
}

#if __has_include("seam/clap_editor/embedded_view.hpp")
NSTextField* findNativeTextField(NSView* parent) {
  for (NSView* child in parent.subviews) {
    if ([child isKindOfClass:[NSTextField class]]) return static_cast<NSTextField*>(child);
    if (auto* field = findNativeTextField(child); field != nil) return field;
  }
  return nil;
}

// requestRepaint queues main-thread blocks that capture the embedded owner. Retire its callbacks
// and drain those blocks while that owner is still alive, including when a CHECK throws.
struct CocoaEmbeddedWindow final {
  NSWindow* __strong window{nil};
  std::unique_ptr<seam::clap_editor::IEmbeddedView> view;
  explicit CocoaEmbeddedWindow(seam::clap_editor::EditorRuntime& runtime)
      : view(seam::clap_editor::createEmbeddedView(runtime)) {}
  ~CocoaEmbeddedWindow() {
    if (view) {
      static_cast<void>(view->hide());
      view->destroy();
      [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.001]];
      view.reset();
    }
    [window orderOut:nil];
    [window close];
  }
  void frame() {
    view->requestRepaint();
    view->onTimer();
    [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.001]];
    [NSApp updateWindows];
    [window displayIfNeeded];
  }
};
#endif

}

TEST_CASE("AppKit accessibility bridge exposes runtime hierarchy and lazy pages") {
  @autoreleasepool {
    NSApplication* application = [NSApplication sharedApplication];
    [application finishLaunching];
    AppKitAccessibilityClient client;
    CHECK(client.tree.virtualizedNoteCount() == 600U);
    auto window = seam::native_ui::createNativeWindow();
    const auto opened = window->open(
        seam::native_ui::NativeWindowConfig{
            .title = "Accessibility Runtime Test",
            .width = 1920U,
            .height = 1360U,
            .scale = 2.0,
            .restoreLastDocument = false,
        },
        client);
    CHECK(opened);
    [application updateWindows];

    NSWindow* nativeWindow = nil;
    for (NSWindow* candidate in application.windows) {
      if ([candidate.title isEqualToString:@"Accessibility Runtime Test"]) {
        nativeWindow = candidate;
        break;
      }
    }
    CHECK(nativeWindow != nil);
    CHECK_NEAR(nativeWindow.contentMinSize.width, 480.0, 1e-9);
    CHECK_NEAR(nativeWindow.contentMinSize.height, 320.0, 1e-9);
    NSView* view = nativeWindow.contentView;
    CHECK(view != nil);
    std::cout << "[NATIVE_SCALE] standalone backingScaleFactor="
              << nativeWindow.backingScaleFactor << '\n';
    CHECK(nativeWindow.backingScaleFactor > 0.0);
    CHECK([view isAccessibilityEnabled]);
    NSArray* children = [view accessibilityChildren];
    CHECK(children != nil);

    const auto* selectedRegion =
        client.session.project().findRegion(client.regionId);
    CHECK(selectedRegion != nullptr);
    CHECK(!selectedRegion->notes.empty());
    const auto periodId = seam::native_ui::vibratoHandleSemanticId(
        selectedRegion->notes.front().id,
        seam::native_ui::VibratoHandleKind::Period);
    id period = findAccessibilityElement(
        children, [NSString stringWithUTF8String:periodId.c_str()]);
    CHECK(period != nil);
    CHECK([[period accessibilityRole] isEqual:NSAccessibilityButtonRole]);
    CHECK([[period accessibilityTitle] isEqualToString:@"Vibrato period"]);
    CHECK([[period accessibilityValue] isEqualToString:@"50 ms"]);
    CHECK([[period accessibilityHelp] length] > 0U);
    [period setAccessibilityFocused:YES];
    CHECK(client.lastId == periodId);
    CHECK(client.lastAction == seam::native_ui::SemanticAction::SetFocus);
    id focusedPeriod = [view accessibilityFocusedUIElement];
    CHECK(focusedPeriod != nil);
    CHECK([[focusedPeriod accessibilityIdentifier]
        isEqualToString:[NSString stringWithUTF8String:periodId.c_str()]]);
    CHECK([focusedPeriod accessibilityFocused]);
    CHECK([period accessibilityPerformPress]);
    CHECK(client.lastId == periodId);
    CHECK(client.lastAction == seam::native_ui::SemanticAction::Activate);

    const auto offscreenNotes = client.tree.materializeNotes(599U, 1U);
    CHECK(offscreenNotes.size() == 1U);
    CHECK(client.tree.setFocus(offscreenNotes.front().id));
    id focused = [view accessibilityFocusedUIElement];
    CHECK(focused != nil);
    CHECK([focused accessibilityFocused]);
    CHECK([[focused accessibilityIdentifier]
        isEqualToString:[NSString stringWithUTF8String:
                                      offscreenNotes.front().id.c_str()]]);

    id transport = findAccessibilityElement(children, @"toolbar.transport");
    CHECK(transport != nil);
    CHECK([[transport accessibilityRole] isEqual:NSAccessibilityButtonRole]);
    CHECK([transport isAccessibilityEnabled]);
    client.transportActivationMovesFocus = true;
    CHECK([transport accessibilityPerformPress]);
    CHECK(client.actionReceived);
    CHECK(client.lastId == "toolbar.transport");
    CHECK(client.lastAction == seam::native_ui::SemanticAction::Activate);
    children = [view accessibilityChildren];

    id tempo = findAccessibilityElement(children, @"toolbar.tempo");
    CHECK(tempo != nil);
    CHECK([tempo accessibilityFocused]);
    CHECK([[tempo accessibilityRole] isEqual:NSAccessibilityTextFieldRole]);
    CHECK([tempo accessibilityIsAttributeSettable:NSAccessibilityValueAttribute]);
    CHECK([tempo accessibilityFrame].size.width > 0.0);
    [tempo setAccessibilityFocused:YES];
    CHECK(client.actionReceived);
    CHECK(client.lastId == "toolbar.tempo");
    CHECK(client.lastAction == seam::native_ui::SemanticAction::SetFocus);

    id page = findAccessibilityElement(children, @"timeline.notes.page.64");
    CHECK(page != nil);
    CHECK([[page accessibilityRole] isEqual:NSAccessibilityGroupRole]);
    NSArray* notes = [page accessibilityChildren];
    CHECK(notes.count > 0U);
    CHECK(notes.count <= 512U);
    id firstNote = notes.firstObject;
    CHECK([firstNote accessibilityParent] == page);
    CHECK([[firstNote accessibilityRole] isEqual:NSAccessibilityTextFieldRole]);
    CHECK([firstNote isAccessibilityEnabled]);
    CHECK([[firstNote accessibilityValue] isEqualToString:@"あ"]);
    CHECK([[firstNote accessibilityHelp] length] > 0U);
    CHECK([firstNote accessibilityIsAttributeSettable:
                                  NSAccessibilityValueAttribute]);
    [firstNote setAccessibilityValue:@"い"];
    CHECK(client.valueReceived);
    CHECK(client.lastValue == "い");
    CHECK([firstNote accessibilityPerformConfirm]);
    CHECK(client.lastAction == seam::native_ui::SemanticAction::EditText);
    CHECK([[firstNote accessibilityIdentifier] length] > 0U);

    window->beginTextInput(seam::native_ui::TextInputRequest{
        .lyricId = seam::domain::LyricTokenId{1U},
        .logicalBounds = seam::ui::Rect{40.0, 40.0, 160.0, 28.0},
        .currentText = U"edge",
    });
    NSRange actualRange = NSMakeRange(NSNotFound, 0U);
    const auto candidateRect =
        [view firstRectForCharacterRange:NSMakeRange(4U, 0U)
                             actualRange:&actualRange];
    CHECK(candidateRect.size.width > 0.0);
    CHECK(candidateRect.size.height > 0.0);
    CHECK(actualRange.location == 4U);
    CHECK(actualRange.length == 0U);
    [view setMarkedText:@"き"
          selectedRange:NSMakeRange(1U, 0U)
        replacementRange:NSMakeRange(0U, 4U)];
    CHECK(client.lastComposition == U"き");
    CHECK(client.lastCompositionSelection.start == 1U);
    [view insertText:@"きゃ" replacementRange:NSMakeRange(NSNotFound, 0U)];
    CHECK(client.lastComposition == U"きゃ");
    [view doCommandBySelector:@selector(insertNewline:)];
    CHECK(client.lastCommit == U"きゃ");

    // Exercise the native candidate anchor's live converted rectangle, rather than only the
    // original request snapshot. Updating it must not begin input again or reset marked ranges.
    auto presented = std::make_shared<seam::ui::Rect>(seam::ui::Rect{60.0, 70.0, 96.0, 24.0});
    window->beginTextInput(seam::native_ui::TextInputRequest{
        .lyricId = seam::domain::LyricTokenId{1U},
        .logicalBounds = *presented,
        .currentText = U"あ",
        .anchor = seam::native_ui::TextInputAnchor::NoteGrid,
        .presentedLyricBounds = presented,
    });
    [view setMarkedText:@"こもれび"
          selectedRange:NSMakeRange(1U, 2U)
        replacementRange:NSMakeRange(0U, 1U)];
    const auto marked = [view markedRange];
    const auto selected = [view selectedRange];
    CHECK(marked.location == 0U && marked.length == 4U);
    CHECK(selected.location == 1U && selected.length == 2U);
    const auto composition = client.lastComposition;
    const auto checkCandidate = [&](NSRect local) {
      NSRange actual = NSMakeRange(NSNotFound, 0U);
      const auto candidate = [view firstRectForCharacterRange:selected actualRange:&actual];
      const auto expected = NSEqualRects(local, NSZeroRect) ? NSZeroRect
          : [nativeWindow convertRectToScreen:[view convertRect:local toView:nil]];
      checkNativeRect(candidate, expected);
      CHECK(NSEqualRanges(actual, selected));
      CHECK(NSEqualRanges([view markedRange], marked));
      CHECK(NSEqualRanges([view selectedRange], selected));
      CHECK(client.lastComposition == composition);
      CHECK(client.lastCommit == U"きゃ");
    };
    checkCandidate(NSMakeRect(60.0, 70.0, 96.0, 24.0));
    *presented = {110.0, 85.0, 326.0, 24.0};
    checkCandidate(NSMakeRect(110.0, 85.0, 326.0, 24.0));
    *presented = {};
    checkCandidate(NSZeroRect);
    *presented = {110.0, 85.0, 60.0, 20.0};
    checkCandidate(NSMakeRect(110.0, 85.0, 60.0, 20.0));
    window->endTextInput();

    window.reset();
  }
}

#if __has_include("seam/clap_editor/embedded_view.hpp")
TEST_CASE("AppKit converted lyric field preserves native marked text and selection through geometry refresh") {
  using namespace seam;
  @autoreleasepool {
    CHECK([NSThread isMainThread]);
    CHECK(native_ui::paint::vectorBackendAvailable());
    NSApplication* app = [NSApplication sharedApplication];
    [app finishLaunching];
    static_cast<void>([app setActivationPolicy:NSApplicationActivationPolicyRegular]);
    application::ProjectFactory factory{31000U};
    auto project = factory.createProject("Native lyric geometry");
    project.settings().characterDisplay = domain::CharacterDisplayMode::Off;
    const auto track = factory.addVocalTrack(project, "Singer");
    const auto regionId = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{7680});
    auto [lyric, note] = factory.makeNote(time::Tick{960}, time::Tick{240}, 72U,
                                         U"あ", domain::Language::Japanese);
    const auto noteId = note.id;
    const auto lyricId = lyric.id;
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    clap_editor::EditorRuntime runtime{std::move(project)};
    runtime.activateDesignShell(native_ui::design::DesignPreferences{
        .mode = native_ui::design::DesignMode::Emo, .reduceMotion = true});
    CocoaEmbeddedWindow native{runtime};
    native.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0.0, 0.0, 1600.0, 900.0)
        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    native.window.releasedWhenClosed = NO;
    CHECK(native.window != nil && native.view != nullptr);
    CHECK(native.view->create("cocoa", false));
    CHECK(native.view->setParent(reinterpret_cast<std::uintptr_t>(
        (__bridge void*)native.window.contentView)));
    CHECK(native.view->setSize(1600U, 900U));
    [native.window makeKeyAndOrderFront:nil];
    [app activateIgnoringOtherApps:YES];
    CHECK(native.view->show());
    native.frame();
    // Cocoa owns the actual backing scale. This records one physical configuration, not a
    // synthetic claim of native 1x/2x coverage from a requested scale or raster-only test.
    std::cout << "[NATIVE_SCALE] embedded backingScaleFactor="
              << native.window.backingScaleFactor << '\n';
    CHECK(native.window.backingScaleFactor > 0.0);
    const auto elementId = "note." + noteId.toString();
    CHECK(runtime.dispatchAccessibility(elementId, native_ui::SemanticAction::SetFocus));
    CHECK(runtime.dispatchAccessibility(elementId, native_ui::SemanticAction::EditText));
    NSTextField* field = findNativeTextField(native.window.contentView);
    CHECK(field != nil && !field.hidden);
    CHECK_NEAR(field.frame.size.width, native_ui::design::kLyricEditorMinWidth, 1e-7);
    CHECK([field.currentEditor isKindOfClass:[NSTextView class]]);
    NSTextView* editor = static_cast<NSTextView*>(field.currentEditor);
    CHECK(native.window.firstResponder == editor);
    NSString* markedText = @"こもれび こもれび こもれび こもれび";
    const std::string utf8{markedText.UTF8String};
    const auto revision = runtime.revision();
    [editor setMarkedText:markedText selectedRange:NSMakeRange(2U, 3U)
        replacementRange:NSMakeRange(0U, editor.string.length)];
    // Public NSTextView change delivery exercises the existing NSControl delegate bridge;
    // do not substitute a direct runtime.textComposition call for native marked-text input.
    [editor didChangeText];
    CHECK(editor.hasMarkedText);
    CHECK([editor.string isEqualToString:markedText]);
    const auto markedRange = editor.markedRange;
    const auto selectedRange = editor.selectedRange;
    CHECK(markedRange.location == 0U && markedRange.length == markedText.length);
    CHECK(selectedRange.location == 2U && selectedRange.length == 3U);
    const auto unchangedInput = [&] {
      CHECK(field.currentEditor == editor);
      CHECK(native.window.firstResponder == editor);
      CHECK(editor.hasMarkedText);
      CHECK(NSEqualRanges(editor.markedRange, markedRange));
      CHECK(NSEqualRanges(editor.selectedRange, selectedRange));
      CHECK([editor.string isEqualToString:markedText]);
      CHECK(!field.hidden && runtime.textInputActive());
      CHECK(runtime.revision() == revision);
      CHECK(runtime.controller().sceneState().compositionPreview == utf8);
    };
    const auto paintedFrame = [&] {
      native_ui::paint::ScopedTextCapture capture;
      native.frame();
      const native_ui::paint::TextRecord* painted = nullptr;
      for (const auto& record : capture.records()) if (record.text == utf8) painted = &record;
      CHECK(painted != nullptr);
      const auto inset = native_ui::design::kLyricEditorInset;
      checkNativeRect(field.frame, NSMakeRect(painted->bounds.x - inset, painted->bounds.y,
          painted->bounds.width + 2.0 * inset, painted->bounds.height));
      unchangedInput();
      return field.frame;
    };
    const auto expanded = paintedFrame();
    CHECK(expanded.size.width > native_ui::design::kLyricEditorMinWidth);
    const auto notes = runtime.accessibilityNotes(0U, 1U);
    CHECK(notes.size() == 1U && notes.front().id == elementId);
    const ui::Point scrollAnchor{notes.front().bounds.x + notes.front().bounds.width * 0.5,
                                 notes.front().bounds.y + notes.front().bounds.height * 0.5};
    runtime.scroll(16.0, 0.0, scrollAnchor, {});
    const auto scrolled = paintedFrame();
    CHECK(std::abs(scrolled.origin.x - expanded.origin.x) > 1.0);
    runtime.scroll(100000.0, 0.0, scrollAnchor, {});
    native.frame();
    CHECK(!runtime.controller().sceneState().lyricEditor.has_value());
    checkNativeRect(field.frame, NSZeroRect);
    unchangedInput();
    runtime.scroll(-100016.0, 0.0, scrollAnchor, {});
    checkNativeRect(paintedFrame(), expanded);
    CHECK(native.view->setSize(1100U, 720U));
    native.frame();
    CHECK(!runtime.textInputActive() && field.hidden);
    CHECK(field.currentEditor != editor && native.window.firstResponder != editor);
    CHECK(runtime.revision() == revision);
    CHECK(runtime.dispatchAccessibility(elementId, native_ui::SemanticAction::EditText));
    CHECK([field.currentEditor isKindOfClass:[NSTextView class]]);
    editor = static_cast<NSTextView*>(field.currentEditor);
    [editor setMarkedText:markedText selectedRange:NSMakeRange(2U, 3U)
        replacementRange:NSMakeRange(0U, editor.string.length)];
    [editor didChangeText];
    CHECK(editor.hasMarkedText && runtime.textInputActive());
    CHECK(native.view->hide());
    CHECK(!runtime.textInputActive() && field.hidden);
    CHECK(field.currentEditor != editor && native.window.firstResponder != editor);
    CHECK(runtime.revision() == revision);
    CHECK(runtime.projectCopy().findRegion(regionId)->findLyric(lyricId)->surface == U"あ");
  }
}
#endif

TEST_CASE("AppKit pages and focuses notes a custom surface delegates to the editor tree") {
  @autoreleasepool {
    NSApplication* application = [NSApplication sharedApplication];
    [application finishLaunching];
    AppKitAccessibilityClient client;
    // The surface moves every note 7 points right and clips it to 100 points wide.
    client.shellTree.rebuildCustom(
        seam::native_ui::SemanticNode{
            .id = "shell",
            .role = seam::native_ui::SemanticRole::Window,
            .name = "Shell",
            .bounds = seam::ui::Rect{0.0, 0.0, 960.0, 680.0},
            .children = {seam::native_ui::SemanticNode{
                .id = "shell.settings",
                .role = seam::native_ui::SemanticRole::Button,
                .name = "Audio settings",
                .bounds = seam::ui::Rect{10.0, 10.0, 40.0, 24.0},
                .actions = {seam::native_ui::SemanticAction::SetFocus}}}},
        {},
        seam::native_ui::VirtualNoteSource{
            .tree = &client.tree,
            .present = [](seam::native_ui::SemanticNode& node) {
              node.bounds.x += 7.0;
              node.bounds.width = std::min(node.bounds.width, 100.0);
            }});
    client.presentShell = true;
    CHECK(client.shellTree.virtualizedNoteCount() == 600U);
    auto window = seam::native_ui::createNativeWindow();
    CHECK(window->open(
        seam::native_ui::NativeWindowConfig{
            .title = "Delegated Notes Test",
            .width = 960U,
            .height = 680U,
            .scale = 1.0,
            .restoreLastDocument = false,
        },
        client));
    [application updateWindows];
    NSWindow* nativeWindow = nil;
    for (NSWindow* candidate in application.windows) {
      if ([candidate.title isEqualToString:@"Delegated Notes Test"]) nativeWindow = candidate;
    }
    CHECK(nativeWindow != nil);
    NSView* view = nativeWindow.contentView;
    NSArray* children = [view accessibilityChildren];
    CHECK(findAccessibilityElement(children, @"shell.settings") != nil);
    // No note is materialized eagerly: all 600 are reachable through pages starting at zero.
    id page = findAccessibilityElement(children, @"timeline.notes.page.0");
    CHECK(page != nil);
    CHECK(findAccessibilityElement(children, @"timeline.notes.page.512") != nil);
    NSArray* notes = [page accessibilityChildren];
    CHECK(notes.count == 512U);
    const auto expected = client.tree.materializeNotes(0U, 1U);
    CHECK(expected.size() == 1U);
    id first = notes.firstObject;
    CHECK([[first accessibilityIdentifier]
        isEqualToString:[NSString stringWithUTF8String:expected.front().id.c_str()]]);
    // The frame comes from the surface's presentation, not the editor's own geometry.
    CHECK([first accessibilityFrame].size.width <= 100.0 + 1e-6);
    // Focusing a delegated note goes through the client and is reported back as focused.
    [first setAccessibilityFocused:YES];
    CHECK(client.lastAction == seam::native_ui::SemanticAction::SetFocus);
    CHECK(client.lastId == expected.front().id);
    id focused = [view accessibilityFocusedUIElement];
    CHECK(focused != nil);
    CHECK([[focused accessibilityIdentifier]
        isEqualToString:[NSString stringWithUTF8String:expected.front().id.c_str()]]);
    CHECK([focused accessibilityFrame].size.width <= 100.0 + 1e-6);
    window.reset();
  }
}

TEST_CASE("AppKit restores only the last document path") {
  @autoreleasepool {
    const auto path = std::filesystem::temp_directory_path() /
                      ("ProjectSEAM-復元-日本語-" +
                       std::to_string(static_cast<long long>(getpid())) +
                       ".seam");
    {
      std::ofstream output(path);
      CHECK(output.good());
      output << "fixture";
    }
    NSString* pathString = [[NSString alloc]
        initWithBytes:path.string().data()
               length:path.string().size()
             encoding:NSUTF8StringEncoding];
    [[NSUserDefaults standardUserDefaults]
        setObject:pathString forKey:@"ProjectSEAM.LastDocumentPath"];

    NSApplication* application = [NSApplication sharedApplication];
    [application finishLaunching];
    AppKitAccessibilityClient client;
    auto window = seam::native_ui::createNativeWindow();
    CHECK(window->open(
        seam::native_ui::NativeWindowConfig{
            .title = "AppKit Restoration Test",
            .width = 960U,
            .height = 680U,
            .scale = 1.0,
            .restoreLastDocument = true,
        },
        client));
    CHECK(client.openedPath == path);
    window->saveRestorationState();
    CHECK([[[NSUserDefaults standardUserDefaults]
        stringForKey:@"ProjectSEAM.LastDocumentPath"] isEqualToString:pathString]);
    NSWindow* restoredWindow = nil;
    for (NSWindow* candidate in application.windows) {
      if ([candidate.title isEqualToString:@"AppKit Restoration Test"]) {
        restoredWindow = candidate;
        break;
      }
    }
    CHECK(restoredWindow != nil);
    [restoredWindow close];
    CHECK([[[NSUserDefaults standardUserDefaults]
        stringForKey:@"ProjectSEAM.LastDocumentPath"] isEqualToString:pathString]);
    window.reset();

    [[NSUserDefaults standardUserDefaults]
        removeObjectForKey:@"ProjectSEAM.LastDocumentPath"];
    std::error_code error;
    std::filesystem::remove(path, error);
    CHECK(!error);
  }
}
