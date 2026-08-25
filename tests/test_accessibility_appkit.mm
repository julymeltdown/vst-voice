#include "test_framework.hpp"

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/native_ui/accessibility_tree.hpp"
#include "seam/native_ui/native_window.hpp"

#import <AppKit/AppKit.h>

#include <cstdint>
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

  AppKitAccessibilityClient()
      : session(makeProject()), model(session, factory, regionId) {
    model.pitch().setTopMidiKey(127);
    model.pitch().setRowHeight(4.0);
    model.setViewport(seam::ui::PianoRollViewport{
        .bounds = seam::ui::Rect{0.0, 0.0, 40000.0, 20000.0},
        .keyboardWidth = 0.0,
    });
    seam::native_ui::EditorSceneState state;
    state.projectName = "日本語プロジェクト";
    state.logicalWidth = 1440.0;
    state.logicalHeight = 900.0;
    state.renderStatus.hasAudibleAudio = true;
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
    return &tree;
  }

  [[nodiscard]] seam::core::Result<void> dispatchAccessibility(
      std::string_view id, seam::native_ui::SemanticAction action) noexcept override {
    lastId = std::string{id};
    lastAction = action;
    actionReceived = true;
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
          seam::time::Tick{static_cast<std::int64_t>(index * 12U)},
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
    CHECK([view isAccessibilityEnabled]);
    NSArray* children = [view accessibilityChildren];
    CHECK(children != nil);

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
    CHECK([transport accessibilityPerformPress]);
    CHECK(client.actionReceived);
    CHECK(client.lastId == "toolbar.transport");
    CHECK(client.lastAction == seam::native_ui::SemanticAction::Activate);

    id tempo = findAccessibilityElement(children, @"toolbar.tempo");
    CHECK(tempo != nil);
    CHECK([[tempo accessibilityRole] isEqual:NSAccessibilityGroupRole]);
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
