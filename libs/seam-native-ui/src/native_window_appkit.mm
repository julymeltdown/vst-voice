#include "seam/native_ui/native_window.hpp"

#if defined(SEAM_NATIVE_APPKIT)

#include "seam/domain/note.hpp"
#include "seam/text/text_engine.hpp"

#include <cstddef>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace seam::native_ui {
class AppKitNativeWindow;
}

@interface SeamAccessibilityElement : NSAccessibilityElement
- (instancetype)initWithOwner:(seam::native_ui::AppKitNativeWindow*)owner
                           node:(const seam::native_ui::SemanticNode&)node
                         parent:(id)parent
                          frame:(NSRect)frame;
- (void)setChildren:(NSArray*)children;
- (void)seamApplyAccessibilityValue:(id)value;
@end

@interface SeamAccessibilityNotesPage : NSAccessibilityElement
- (instancetype)initWithOwner:(seam::native_ui::AppKitNativeWindow*)owner
                         parent:(id)parent
                          frame:(NSRect)frame
                         offset:(std::size_t)offset
                          limit:(std::size_t)limit
                     identifier:(NSString*)identifier
                           title:(NSString*)title
                          value:(NSString*)value;
@end

@interface SeamNativeEditorView : NSView <NSTextInputClient>
- (instancetype)initWithFrame:(NSRect)frame
                        owner:(seam::native_ui::AppKitNativeWindow*)owner;
- (id)accessibilityFocusedUIElement;
@end

@interface SeamNativeWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) seam::native_ui::AppKitNativeWindow* owner;
@end

namespace seam::native_ui {
namespace {

NSString* stringFromUtf32(const std::u32string& text) {
  const auto utf8 = domain::toUtf8(text);
  return [[NSString alloc] initWithBytes:utf8.data()
                                  length:utf8.size()
                                encoding:NSUTF8StringEncoding];
}

core::Result<std::u32string> utf32FromString(NSString* text) {
  if (text == nil || text.length == 0U) return std::u32string{};
  const char* bytes = [text UTF8String];
  if (bytes == nullptr) {
    return core::failure<std::u32string>(
        core::ErrorCode::ParseError, "Unable to convert NSString to UTF-8");
  }
  return domain::fromUtf8(std::string{bytes});
}

std::size_t codePointLength(NSString* text, NSRange range) {
  if (text == nil || range.location == NSNotFound ||
      NSMaxRange(range) > text.length) {
    return 0U;
  }
  auto decoded = utf32FromString([text substringWithRange:range]);
  return decoded ? decoded.value().size() : 0U;
}

NSString* lastDocumentPathKey() {
  return @"ProjectSEAM.LastDocumentPath";
}

std::optional<std::filesystem::path> storedDocumentPath() {
  NSString* value = [[NSUserDefaults standardUserDefaults]
      stringForKey:lastDocumentPathKey()];
  if (value == nil) return std::nullopt;
  const char* bytes = value.fileSystemRepresentation;
  if (bytes == nullptr) return std::nullopt;
  const std::filesystem::path path{bytes};
  std::error_code error;
  if (path.extension() != ".seam" ||
      !std::filesystem::is_regular_file(path, error) || error) {
    [[NSUserDefaults standardUserDefaults]
        removeObjectForKey:lastDocumentPathKey()];
    return std::nullopt;
  }
  return path;
}

void persistDocumentPath(const INativeWindowClient* client) {
  if (client == nullptr) return;
  const auto path = client->documentPath();
  if (!path.has_value()) return;
  const auto text = path->string();
  NSString* value = [[NSString alloc] initWithBytes:text.data()
                                               length:text.size()
                                             encoding:NSUTF8StringEncoding];
  if (value == nil) return;
  [[NSUserDefaults standardUserDefaults] setObject:value
                                            forKey:lastDocumentPathKey()];
}

InputModifiers modifiers(NSEventModifierFlags flags) noexcept {
  return InputModifiers{
      .shift = (flags & NSEventModifierFlagShift) != 0U,
      .control = (flags & NSEventModifierFlagControl) != 0U,
      .alt = (flags & NSEventModifierFlagOption) != 0U,
      .command = (flags & NSEventModifierFlagCommand) != 0U,
  };
}

NativeKey nativeKey(NSEvent* event) noexcept {
  switch (event.keyCode) {
    case 36U:
    case 76U: return NativeKey::Enter;
    case 48U: return NativeKey::Tab;
    case 49U: return NativeKey::Space;
    case 51U: return NativeKey::Backspace;
    case 53U: return NativeKey::Escape;
    case 117U: return NativeKey::Delete;
    case 123U: return NativeKey::Left;
    case 124U: return NativeKey::Right;
    case 125U: return NativeKey::Down;
    case 126U: return NativeKey::Up;
    default: break;
  }
  NSString* characters = event.charactersIgnoringModifiers;
  if (characters.length == 0U) return NativeKey::Unknown;
  switch ([[characters uppercaseString] characterAtIndex:0U]) {
    case 'Z': return NativeKey::Z;
    case 'Y': return NativeKey::Y;
    case 'C': return NativeKey::C;
    case 'B': return NativeKey::B;
    case 'D': return NativeKey::D;
    case 'E': return NativeKey::E;
    case 'N': return NativeKey::N;
    case 'O': return NativeKey::O;
    case 'P': return NativeKey::P;
    case 'Q': return NativeKey::Q;
    case 'S': return NativeKey::S;
    case 'R': return NativeKey::R;
    case 'X': return NativeKey::X;
    case 'V': return NativeKey::V;
    case 'I': return NativeKey::I;
    case 'L': return NativeKey::L;
    case '+':
    case '=': return NativeKey::Plus;
    case '-': return NativeKey::Minus;
    default: return NativeKey::Unknown;
  }
}

}  // namespace

class AppKitNativeWindow final : public INativeWindow {
public:
  ~AppKitNativeWindow() override { close(); }

  core::Result<void> open(const NativeWindowConfig& config,
                          INativeWindowClient& client) override {
    if (window_ != nil) {
      return core::failure(core::ErrorCode::Conflict,
                           "AppKit window is already open");
    }
    if (!nativeWindowConfigSizeIsValid(config)) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Native window dimensions or scale are invalid");
    }
    config_ = config;
    client_ = &client;
    auto textEngine = text::TextEngine::createSystem();
    if (textEngine) textEngine_ = std::move(textEngine.value());
    application_ = [NSApplication sharedApplication];
    [application_ setActivationPolicy:NSApplicationActivationPolicyRegular];
    [application_ finishLaunching];

    const NSRect frame = NSMakeRect(
        0.0, 0.0, static_cast<CGFloat>(config.width) / config.scale,
        static_cast<CGFloat>(config.height) / config.scale);
    window_ = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled |
                            NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable |
                            NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    if (window_ == nil) {
      close();
      return core::failure(core::ErrorCode::IoError,
                           "Unable to create the AppKit editor window");
    }
    NSString* title = [[NSString alloc]
        initWithBytes:config.title.data()
                length:config.title.size()
              encoding:NSUTF8StringEncoding];
    window_.title = title == nil ? @"Project SEAM" : title;
    window_.releasedWhenClosed = NO;
    window_.contentMinSize = NSMakeSize(
        static_cast<CGFloat>(config.minimumWidth),
        static_cast<CGFloat>(config.minimumHeight));
    if (nativeWindowShouldRestoreSavedFrame(config)) {
      window_.frameAutosaveName = @"ProjectSEAM.Editor";
    }
    if (window_.frameAutosaveName.length == 0U ||
        ![window_ setFrameUsingName:window_.frameAutosaveName]) {
      [window_ center];
    }
    window_.acceptsMouseMovedEvents = YES;
    delegate_ = [[SeamNativeWindowDelegate alloc] init];
    delegate_.owner = this;
    window_.delegate = delegate_;
    view_ = [[SeamNativeEditorView alloc] initWithFrame:frame owner:this];
    window_.contentView = view_;
    const auto accessibility = installAccessibilityBridge(
        (__bridge void*)view_);
    if (!accessibility) {
      close();
      return accessibility;
    }
    [window_ makeKeyAndOrderFront:nil];

    updateScaleAndSurface();
    if (surface_.pixels().empty()) {
      close();
      return core::failure(core::ErrorCode::Internal,
                           "Unable to create the AppKit backing surface");
    }
    client_->resized(static_cast<double>(view_.bounds.size.width),
                     static_cast<double>(view_.bounds.size.height), scale_);
    restoreLastDocument();
    openedAt_ = std::chrono::steady_clock::now();
    repaintRequested_.store(true, std::memory_order_release);
    return core::success();
  }

  int run() override {
    if (window_ == nil || client_ == nullptr) return 1;
    while (window_.visible && !client_->wantsClose()) {
      @autoreleasepool {
        NSEvent* event = [application_
            nextEventMatchingMask:NSEventMaskAny
                        untilDate:[NSDate dateWithTimeIntervalSinceNow:0.016]
                           inMode:NSDefaultRunLoopMode
                          dequeue:YES];
        if (event != nil) [application_ sendEvent:event];
        if (repaintRequested_.exchange(false, std::memory_order_acq_rel)) {
          [view_ setNeedsDisplay:YES];
        }
        [application_ updateWindows];
      }
      if (config_.autoCloseAfter.count() > 0 &&
          std::chrono::steady_clock::now() - openedAt_ >=
              config_.autoCloseAfter) {
        programmaticClose_ = true;
        [window_ close];
        programmaticClose_ = false;
      }
    }
    drawNow();
    if (config_.screenshotPath.has_value()) {
      const auto written = surface_.writePpm(*config_.screenshotPath);
      if (!written) return 2;
    }
    return 0;
  }

  void requestRepaint() noexcept override {
    repaintRequested_.store(true, std::memory_order_release);
    accessibilitySnapshotDirty_.store(true, std::memory_order_release);
    accessibilityAnnouncementPending_.store(true, std::memory_order_release);
  }

  void beginTextInput(const TextInputRequest& request) override {
    textInputActive_ = true;
    textRequest_ = request;
    textStorage_ = [stringFromUtf32(request.currentText) mutableCopy];
    if (textStorage_ == nil) textStorage_ = [NSMutableString string];
    selectedRange_ = NSMakeRange(textStorage_.length, 0U);
    markedRange_ = NSMakeRange(NSNotFound, 0U);
    [window_ makeFirstResponder:view_];
    [[view_ inputContext] activate];
    publishComposition();
  }

  void endTextInput() noexcept override {
    textInputActive_ = false;
    textRequest_.reset();
    textStorage_ = nil;
    selectedRange_ = NSMakeRange(NSNotFound, 0U);
    markedRange_ = NSMakeRange(NSNotFound, 0U);
    requestRepaint();
  }

  void saveRestorationState() noexcept override {
    if (window_ != nil && window_.frameAutosaveName.length > 0U) {
      [window_ saveFrameUsingName:window_.frameAutosaveName];
    }
    persistDocumentPath(client_);
  }

  PixelSurface snapshot() const override { return surface_; }

  std::string backendName() const override {
    return "AppKit software raster + NSTextInputClient";
  }

  NSArray* accessibilityChildren() {
    if (client_ == nullptr || view_ == nil || window_ == nil) return @[];
    if (accessibilitySnapshot_ != nil &&
        !accessibilitySnapshotDirty_.exchange(false, std::memory_order_acq_rel)) {
      return accessibilitySnapshot_;
    }
    const auto* tree = client_->accessibilityTree();
    if (tree == nullptr) return @[];
    std::size_t visibleNotes = 0U;
    for (const auto& node : tree->root().children) {
      if (node.role == SemanticRole::Note) ++visibleNotes;
    }
    const auto totalNotes = tree->virtualizedNoteCount();
    const auto pageSize = std::size_t{512U};
    const auto pageCount = totalNotes > visibleNotes
                               ? (totalNotes - visibleNotes + pageSize - 1U) /
                                     pageSize
                               : 0U;
    NSMutableArray* result = [NSMutableArray arrayWithCapacity:
        tree->root().children.size() + pageCount];
    for (const auto& node : tree->root().children) {
      [result addObject:makeAccessibilityElement(node, view_)];
    }
    const auto pageFrame = screenFrame(tree->root().bounds, view_);
    for (std::size_t offset = visibleNotes; offset < totalNotes;
         offset += pageSize) {
      const auto limit = std::min(pageSize, totalNotes - offset);
      NSString* identifier = [NSString stringWithFormat:
          @"timeline.notes.page.%lu", static_cast<unsigned long>(offset)];
      NSString* title = [NSString stringWithFormat:
          @"Notes %lu-%lu", static_cast<unsigned long>(offset + 1U),
          static_cast<unsigned long>(offset + limit)];
      NSString* value = [NSString stringWithFormat:
          @"%lu notes", static_cast<unsigned long>(limit)];
      [result addObject:[[SeamAccessibilityNotesPage alloc]
                            initWithOwner:this
                                   parent:view_
                                    frame:pageFrame
                                   offset:offset
                                    limit:limit
                               identifier:identifier
                                     title:title
                                    value:value]];
    }
    accessibilitySnapshot_ = [result copy];
    accessibilitySnapshotDirty_.store(false, std::memory_order_release);
    return accessibilitySnapshot_;
  }

  NSArray* accessibilityNotes(std::size_t offset, std::size_t limit,
                              id parent) {
    if (client_ == nullptr || view_ == nil) return @[];
    const auto* tree = client_->accessibilityTree();
    if (tree == nullptr) return @[];
    const auto notes = tree->materializeNotes(offset, limit);
    NSMutableArray* result = [NSMutableArray arrayWithCapacity:notes.size()];
    for (const auto& node : notes) {
      [result addObject:makeAccessibilityElement(node, parent)];
    }
    return result;
  }

  core::Result<void> dispatchAccessibility(std::string_view id,
                                           SemanticAction action) noexcept {
    if (client_ == nullptr) {
      return core::failure(core::ErrorCode::InvalidState,
                           "Accessibility client is unavailable");
    }
    const auto result = client_->dispatchAccessibility(id, action);
    if (result && action == SemanticAction::SetFocus && view_ != nil) {
      accessibilitySnapshotDirty_.store(true, std::memory_order_release);
      NSAccessibilityPostNotification(
          view_, NSAccessibilityFocusedUIElementChangedNotification);
    }
    return result;
  }

  core::Result<void> setAccessibilityValue(std::string_view id,
                                           std::string_view value) noexcept {
    if (client_ == nullptr) {
      return core::failure(core::ErrorCode::InvalidState,
                           "Accessibility client is unavailable");
    }
    return client_->setAccessibilityValue(id, value);
  }

  id accessibilityFocusedElement() {
    if (client_ == nullptr || view_ == nil) return nil;
    const auto* tree = client_->accessibilityTree();
    if (tree == nullptr) return nil;
    const auto* focused = tree->focusedNode();
    return focused == nullptr ? nil : makeAccessibilityElement(*focused, view_);
  }

  NSRect screenFrame(ui::Rect bounds, NSView* parentView) const {
    if (window_ == nil || parentView == nil) return NSZeroRect;
    const auto logical = NSMakeRect(bounds.x, bounds.y, bounds.width,
                                    bounds.height);
    return [window_ convertRectToScreen:
        [parentView convertRect:logical toView:nil]];
  }

  void drawInView(NSView* view) noexcept {
    if (client_ == nullptr || view == nil) return;
    updateScaleAndSurface();
    if (surface_.pixels().empty()) return;
    RasterCanvas canvas{surface_, scale_, textEngine_.get()};
    client_->paint(canvas);
    if (accessibilityAnnouncementPending_.exchange(
            false, std::memory_order_acq_rel)) {
      NSAccessibilityPostNotification(
          view_, NSAccessibilityValueChangedNotification);
    }

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    if (colorSpace == nullptr) return;
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        nullptr, surface_.pixels().data(),
        surface_.pixels().size() * sizeof(std::uint32_t), nullptr);
    if (provider == nullptr) {
      CGColorSpaceRelease(colorSpace);
      return;
    }
    const auto bitmapInfo = static_cast<CGBitmapInfo>(
        static_cast<std::uint32_t>(kCGImageAlphaPremultipliedFirst) |
        static_cast<std::uint32_t>(kCGBitmapByteOrder32Little));
    CGImageRef image = CGImageCreate(
        surface_.width(), surface_.height(), 8U, 32U, surface_.strideBytes(),
        colorSpace, bitmapInfo, provider, nullptr, false,
        kCGRenderingIntentDefault);
    if (image != nullptr) {
      CGContextRef context = NSGraphicsContext.currentContext.CGContext;
      CGContextSetInterpolationQuality(context, kCGInterpolationNone);
      CGContextSaveGState(context);
      // `SeamNativeEditorView` is flipped so logical input/accessibility
      // coordinates use a top-left origin. CoreGraphics still presents
      // CGImage rows from a bottom-left origin, so mirror the presentation
      // transform once at the final boundary. Without this, the real AppKit
      // window is vertically inverted even though software-raster captures
      // and hit-testing appear correct.
      CGContextTranslateCTM(context, 0.0, view.bounds.size.height);
      CGContextScaleCTM(context, 1.0, -1.0);
      CGContextDrawImage(context,
                         CGRectMake(0.0, 0.0, view.bounds.size.width,
                                    view.bounds.size.height),
                         image);
      CGContextRestoreGState(context);
      CGImageRelease(image);
    }
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpace);
  }

  void viewResized() noexcept {
    updateScaleAndSurface();
    if (client_ != nullptr && view_ != nil) {
      client_->resized(static_cast<double>(view_.bounds.size.width),
                       static_cast<double>(view_.bounds.size.height), scale_);
    }
    requestRepaint();
  }

  void pointerDown(NSEvent* event) noexcept {
    if (client_ == nullptr) return;
    const auto point = [view_ convertPoint:event.locationInWindow fromView:nil];
    client_->pointerDown(PointerEvent{
        .position = ui::Point{static_cast<double>(point.x),
                              static_cast<double>(point.y)},
        .button = event.type == NSEventTypeRightMouseDown
                      ? PointerButton::Right
                      : event.type == NSEventTypeOtherMouseDown
                            ? PointerButton::Middle
                            : PointerButton::Left,
        .modifiers = modifiers(event.modifierFlags),
        .clickCount = static_cast<int>(event.clickCount),
    });
  }

  void pointerMove(NSEvent* event, bool dragging) noexcept {
    if (client_ == nullptr) return;
    const auto point = [view_ convertPoint:event.locationInWindow fromView:nil];
    client_->pointerMove(PointerEvent{
        .position = ui::Point{static_cast<double>(point.x),
                              static_cast<double>(point.y)},
        .button = !dragging
                      ? PointerButton::NoButton
                      : event.type == NSEventTypeRightMouseDragged
                            ? PointerButton::Right
                            : event.type == NSEventTypeOtherMouseDragged
                                  ? PointerButton::Middle
                                  : PointerButton::Left,
        .modifiers = modifiers(event.modifierFlags),
        .clickCount = 1,
    });
  }

  void pointerUp(NSEvent* event) noexcept {
    if (client_ == nullptr) return;
    const auto point = [view_ convertPoint:event.locationInWindow fromView:nil];
    client_->pointerUp(PointerEvent{
        .position = ui::Point{static_cast<double>(point.x),
                              static_cast<double>(point.y)},
        .button = event.type == NSEventTypeRightMouseUp
                      ? PointerButton::Right
                      : event.type == NSEventTypeOtherMouseUp
                            ? PointerButton::Middle
                            : PointerButton::Left,
        .modifiers = modifiers(event.modifierFlags),
        .clickCount = static_cast<int>(event.clickCount),
    });
  }

  void scroll(NSEvent* event) noexcept {
    if (client_ == nullptr) return;
    const auto point = [view_ convertPoint:event.locationInWindow fromView:nil];
    client_->scroll(static_cast<double>(event.scrollingDeltaX),
                    -static_cast<double>(event.scrollingDeltaY),
                    ui::Point{static_cast<double>(point.x),
                              static_cast<double>(point.y)},
                    modifiers(event.modifierFlags));
  }

  void keyDown(NSEvent* event, NSView* view) noexcept {
    if (textInputActive_) {
      [view interpretKeyEvents:@[ event ]];
      return;
    }
    if (client_ != nullptr) {
      client_->keyDown(KeyEvent{
          .key = nativeKey(event),
          .modifiers = modifiers(event.modifierFlags),
          .repeat = event.isARepeat,
      });
    }
  }

  bool hasMarkedText() const noexcept {
    return markedRange_.location != NSNotFound && markedRange_.length > 0U;
  }
  NSRange markedRange() const noexcept { return markedRange_; }
  NSRange selectedRange() const noexcept { return selectedRange_; }
  bool textInputActive() const noexcept { return textInputActive_; }

  NSAttributedString* attributedSubstring(NSRange range,
                                           NSRangePointer actual) const {
    if (textStorage_ == nil || range.location == NSNotFound ||
        range.location > textStorage_.length) {
      return nil;
    }
    const auto clamped = NSIntersectionRange(
        range, NSMakeRange(0U, textStorage_.length));
    if (actual != nullptr) *actual = clamped;
    return [[NSAttributedString alloc]
        initWithString:[textStorage_ substringWithRange:clamped]];
  }

  void setMarkedText(id value, NSRange selection,
                     NSRange replacement) noexcept {
    if (!textInputActive_) return;
    NSString* incoming = [value isKindOfClass:[NSAttributedString class]]
                             ? [value string]
                             : static_cast<NSString*>(value);
    if (incoming == nil) incoming = @"";
    const auto replace = replacementRange(replacement);
    [textStorage_ replaceCharactersInRange:replace withString:incoming];
    markedRange_ = NSMakeRange(replace.location, incoming.length);
    const auto relativeStart = std::min(selection.location, incoming.length);
    const auto relativeLength = std::min(
        selection.length, incoming.length - relativeStart);
    selectedRange_ = NSMakeRange(markedRange_.location + relativeStart,
                                 relativeLength);
    publishComposition();
  }

  void insertText(id value, NSRange replacement) noexcept {
    if (!textInputActive_) return;
    NSString* incoming = [value isKindOfClass:[NSAttributedString class]]
                             ? [value string]
                             : static_cast<NSString*>(value);
    if (incoming == nil) incoming = @"";
    const auto replace = replacementRange(replacement);
    [textStorage_ replaceCharactersInRange:replace withString:incoming];
    selectedRange_ = NSMakeRange(replace.location + incoming.length, 0U);
    markedRange_ = NSMakeRange(NSNotFound, 0U);
    publishComposition();
  }

  void unmarkText() noexcept {
    markedRange_ = NSMakeRange(NSNotFound, 0U);
    publishComposition();
  }

  void command(SEL selector) noexcept {
    if (!textInputActive_ || client_ == nullptr) return;
    if (selector == @selector(insertTab:) ||
        selector == @selector(insertBacktab:)) {
      client_->keyDown(KeyEvent{
          .key = NativeKey::Tab,
          .modifiers = InputModifiers{
              .shift = selector == @selector(insertBacktab:),
          },
          .repeat = false,
      });
      return;
    }
    if (selector == @selector(insertNewline:) ||
        selector == @selector(insertNewlineIgnoringFieldEditor:)) {
      auto decoded = utf32FromString(textStorage_);
      if (decoded) {
        client_->textCommit(std::move(decoded.value()));
        endTextInput();
      }
      return;
    }
    if (selector == @selector(cancelOperation:)) {
      client_->textCancel();
      endTextInput();
      return;
    }
    if (selector == @selector(deleteBackward:)) {
      deleteBackward();
      return;
    }
    if (selector == @selector(deleteForward:)) {
      deleteForward();
      return;
    }
    if (selector == @selector(moveLeft:)) {
      if (selectedRange_.location != NSNotFound && selectedRange_.location > 0U) {
        selectedRange_ = NSMakeRange(selectedRange_.location - 1U, 0U);
      }
      publishComposition();
      return;
    }
    if (selector == @selector(moveRight:)) {
      if (selectedRange_.location != NSNotFound &&
          selectedRange_.location < textStorage_.length) {
        selectedRange_ = NSMakeRange(selectedRange_.location + 1U, 0U);
      }
      publishComposition();
    }
  }

  NSRect firstRectForCharacterRange(NSRange, NSRangePointer actual) const {
    if (actual != nullptr) *actual = selectedRange_;
    if (view_ == nil || window_ == nil || !textRequest_.has_value()) {
      return NSZeroRect;
    }
    const auto& bounds = textRequest_->logicalBounds;
    const NSRect viewRect = NSMakeRect(bounds.x, bounds.y, bounds.width,
                                       bounds.height);
    return [window_ convertRectToScreen:[view_ convertRect:viewRect toView:nil]];
  }

  NSUInteger characterIndexForPoint(NSPoint) const noexcept {
    return selectedRange_.location == NSNotFound ? 0U : selectedRange_.location;
  }

  bool requestCloseFromWindow() noexcept {
    const auto accepted =
        programmaticClose_ || client_ == nullptr || client_->requestClose();
    if (accepted) saveRestorationState();
    return accepted;
  }

private:
  SeamAccessibilityElement* makeAccessibilityElement(
      const SemanticNode& node, id parent) {
    const auto frame = screenFrame(node.bounds, view_);
    auto* element = [[SeamAccessibilityElement alloc]
        initWithOwner:this node:node parent:parent frame:frame];
    NSMutableArray* children = [NSMutableArray arrayWithCapacity:
        node.children.size()];
    for (const auto& child : node.children) {
      [children addObject:makeAccessibilityElement(child, element)];
    }
    [element setChildren:children];
    return element;
  }

  NSRange replacementRange(NSRange requested) const noexcept {
    NSRange candidate = requested;
    if (candidate.location == NSNotFound) {
      candidate = hasMarkedText() ? markedRange_ : selectedRange_;
    }
    if (candidate.location == NSNotFound ||
        candidate.location > textStorage_.length) {
      candidate = NSMakeRange(textStorage_.length, 0U);
    }
    candidate.length = std::min(candidate.length,
                                textStorage_.length - candidate.location);
    return candidate;
  }

  void publishComposition() noexcept {
    if (client_ == nullptr || textStorage_ == nil) return;
    auto decoded = utf32FromString(textStorage_);
    if (!decoded) return;
    const auto prefix = selectedRange_.location == NSNotFound
                            ? 0U
                            : codePointLength(
                                  textStorage_, NSMakeRange(
                                                    0U, selectedRange_.location));
    const auto length = selectedRange_.location == NSNotFound
                            ? 0U
                            : codePointLength(textStorage_, selectedRange_);
    client_->textComposition(
        std::move(decoded.value()),
        ui::CompositionSelection{.start = prefix, .length = length});
    requestRepaint();
  }

  void deleteBackward() noexcept {
    if (selectedRange_.location == NSNotFound || textStorage_ == nil) return;
    NSRange target = selectedRange_;
    if (target.length == 0U && target.location > 0U) {
      target = [textStorage_ rangeOfComposedCharacterSequenceAtIndex:
                                target.location - 1U];
    }
    if (target.location != NSNotFound && NSMaxRange(target) <= textStorage_.length) {
      [textStorage_ deleteCharactersInRange:target];
      selectedRange_ = NSMakeRange(target.location, 0U);
      markedRange_ = NSMakeRange(NSNotFound, 0U);
      publishComposition();
    }
  }

  void deleteForward() noexcept {
    if (selectedRange_.location == NSNotFound || textStorage_ == nil) return;
    NSRange target = selectedRange_;
    if (target.length == 0U && target.location < textStorage_.length) {
      target = [textStorage_ rangeOfComposedCharacterSequenceAtIndex:
                                target.location];
    }
    if (target.location != NSNotFound && NSMaxRange(target) <= textStorage_.length) {
      [textStorage_ deleteCharactersInRange:target];
      selectedRange_ = NSMakeRange(target.location, 0U);
      markedRange_ = NSMakeRange(NSNotFound, 0U);
      publishComposition();
    }
  }

  void updateScaleAndSurface() noexcept {
    if (view_ == nil || window_ == nil) return;
    const auto backing = window_.backingScaleFactor;
    scale_ = config_.scale * static_cast<double>(backing <= 0.0 ? 1.0 : backing);
    const auto width = static_cast<std::uint32_t>(std::max(
        1.0, std::ceil(static_cast<double>(view_.bounds.size.width) * scale_)));
    const auto height = static_cast<std::uint32_t>(std::max(
        1.0, std::ceil(static_cast<double>(view_.bounds.size.height) * scale_)));
    if (width != surface_.width() || height != surface_.height()) {
      static_cast<void>(surface_.resize(width, height));
    }
  }

  void restoreLastDocument() noexcept {
    if (!config_.restoreLastDocument || client_ == nullptr ||
        client_->documentPath().has_value()) {
      return;
    }
    const auto path = storedDocumentPath();
    if (path.has_value()) client_->openProjectPath(*path);
  }

  void drawNow() noexcept {
    if (view_ == nil) return;
    updateScaleAndSurface();
    if (client_ != nullptr && !surface_.pixels().empty()) {
      RasterCanvas canvas{surface_, scale_, textEngine_.get()};
      client_->paint(canvas);
    }
  }

  void close() noexcept {
    if (window_ != nil) {
      window_.delegate = nil;
      [window_ orderOut:nil];
      [window_ close];
    }
    if (delegate_ != nil) delegate_.owner = nullptr;
    delegate_ = nil;
    view_ = nil;
    window_ = nil;
    application_ = nil;
    textStorage_ = nil;
    accessibilitySnapshot_ = nil;
  }

  NativeWindowConfig config_;
  INativeWindowClient* client_{nullptr};
  NSApplication* __strong application_{nil};
  NSWindow* __strong window_{nil};
  SeamNativeWindowDelegate* __strong delegate_{nil};
  SeamNativeEditorView* __strong view_{nil};
  NSMutableString* __strong textStorage_{nil};
  PixelSurface surface_;
  std::unique_ptr<text::TextEngine> textEngine_;
  NSArray* __strong accessibilitySnapshot_{nil};
  double scale_{1.0};
  std::atomic<bool> repaintRequested_{false};
  std::atomic<bool> accessibilitySnapshotDirty_{true};
  std::atomic<bool> accessibilityAnnouncementPending_{true};
  std::chrono::steady_clock::time_point openedAt_{};
  std::optional<TextInputRequest> textRequest_;
  NSRange selectedRange_{NSMakeRange(NSNotFound, 0U)};
  NSRange markedRange_{NSMakeRange(NSNotFound, 0U)};
  bool textInputActive_{false};
  bool programmaticClose_{false};
};

std::unique_ptr<INativeWindow> createNativeWindow() {
  return std::make_unique<AppKitNativeWindow>();
}

}  // namespace seam::native_ui

@implementation SeamAccessibilityElement {
  seam::native_ui::AppKitNativeWindow* _owner;
  __weak id _parent;
  NSArray* __strong _children;
  NSString* __strong _identifier;
  NSString* __strong _role;
  NSString* __strong _title;
  NSString* __strong _value;
  NSString* __strong _description;
  NSRect _frame;
  BOOL _enabled;
  BOOL _focused;
  BOOL _selected;
  BOOL _editable;
  BOOL _hasActivate;
  BOOL _hasToggle;
  std::vector<seam::native_ui::SemanticAction> _actions;
}

- (instancetype)initWithOwner:(seam::native_ui::AppKitNativeWindow*)owner
                           node:(const seam::native_ui::SemanticNode&)node
                         parent:(id)parent
                          frame:(NSRect)frame {
  self = [super init];
  if (self != nil) {
    _owner = owner;
    _parent = parent;
    _identifier = [[NSString alloc]
        initWithBytes:node.id.data()
               length:node.id.size()
             encoding:NSUTF8StringEncoding];
    _title = [[NSString alloc]
        initWithBytes:node.name.data()
               length:node.name.size()
             encoding:NSUTF8StringEncoding];
    const auto editable = std::find(node.actions.begin(), node.actions.end(),
                                    seam::native_ui::SemanticAction::EditText) !=
                          node.actions.end();
    const auto& accessibleValue = editable ? node.editableValue : node.value;
    _value = [[NSString alloc]
        initWithBytes:accessibleValue.data()
               length:accessibleValue.size()
             encoding:NSUTF8StringEncoding];
    _description = [[NSString alloc]
        initWithBytes:node.description.data()
               length:node.description.size()
             encoding:NSUTF8StringEncoding];
    switch (node.role) {
      case seam::native_ui::SemanticRole::Window:
        _role = NSAccessibilityWindowRole; break;
      case seam::native_ui::SemanticRole::Toolbar:
        _role = NSAccessibilityToolbarRole; break;
      case seam::native_ui::SemanticRole::Button:
        _role = NSAccessibilityButtonRole; break;
      case seam::native_ui::SemanticRole::TextField:
        _role = NSAccessibilityTextFieldRole; break;
      case seam::native_ui::SemanticRole::Timeline:
      case seam::native_ui::SemanticRole::Lane:
        _role = NSAccessibilityGroupRole; break;
      case seam::native_ui::SemanticRole::Note:
        _role = editable ? NSAccessibilityTextFieldRole
                         : NSAccessibilityGroupRole;
        break;
      case seam::native_ui::SemanticRole::Panel:
      case seam::native_ui::SemanticRole::Status:
        _role = NSAccessibilityGroupRole; break;
    }
    _frame = frame;
    _enabled = node.enabled;
    _focused = node.focused;
    _selected = node.selected;
    _editable = editable;
    _hasActivate = std::find(node.actions.begin(), node.actions.end(),
                             seam::native_ui::SemanticAction::Activate) !=
                   node.actions.end();
    _hasToggle = std::find(node.actions.begin(), node.actions.end(),
                           seam::native_ui::SemanticAction::Toggle) !=
                 node.actions.end();
    _actions = node.actions;
    _children = @[];
  }
  return self;
}

- (void)setChildren:(NSArray*)children { _children = [children copy]; }
- (BOOL)isAccessibilityElement { return YES; }
- (BOOL)accessibilityIsIgnored { return NO; }
- (id)accessibilityRole { return _role; }
- (id)accessibilityRoleDescription {
  return NSAccessibilityRoleDescription(_role, nil);
}
- (NSString*)accessibilityIdentifier { return _identifier; }
- (id)accessibilityTitle { return _title; }
- (id)accessibilityValue { return _value.length == 0U ? nil : _value; }
- (void)setAccessibilityValue:(id)value {
  [self seamApplyAccessibilityValue:value];
}
- (id)accessibilityHelp {
  return _description.length == 0U ? nil : _description;
}
- (id)accessibilityParent { return _parent; }
- (NSArray*)accessibilityChildren { return _children; }
- (NSRect)accessibilityFrame { return _frame; }
- (BOOL)isAccessibilityEnabled { return _enabled; }
- (BOOL)accessibilityFocused { return _focused; }
- (BOOL)accessibilitySelected { return _selected; }
- (BOOL)accessibilityIsAttributeSettable:(NSString*)attribute {
  return _editable && [attribute isEqualToString:NSAccessibilityValueAttribute];
}

- (void)seamApplyAccessibilityValue:(id)value {
  if (!_editable || _owner == nullptr) return;
  NSString* string = [value isKindOfClass:[NSAttributedString class]]
                         ? [value string]
                         : [value isKindOfClass:[NSString class]]
                               ? static_cast<NSString*>(value)
                               : nil;
  const char* bytes = string.UTF8String;
  const char* identifier = _identifier.UTF8String;
  if (bytes == nullptr || identifier == nullptr) return;
  static_cast<void>(_owner->setAccessibilityValue(identifier, bytes));
}

- (void)accessibilitySetValue:(id)value forAttribute:(NSString*)attribute {
  if (![attribute isEqualToString:NSAccessibilityValueAttribute]) return;
  [self seamApplyAccessibilityValue:value];
}

- (NSArray*)accessibilityActionNames {
  NSMutableArray* result = [NSMutableArray array];
  for (const auto action : _actions) {
    switch (action) {
      case seam::native_ui::SemanticAction::Activate:
      case seam::native_ui::SemanticAction::Toggle:
        if (![result containsObject:NSAccessibilityPressAction]) {
          [result addObject:NSAccessibilityPressAction];
        }
        break;
      case seam::native_ui::SemanticAction::SetFocus:
        break;
      case seam::native_ui::SemanticAction::EditText:
        if (![result containsObject:NSAccessibilityConfirmAction]) {
          [result addObject:NSAccessibilityConfirmAction];
        }
        break;
    }
  }
  return result;
}

- (void)accessibilityPerformAction:(NSString*)action {
  if ([action isEqualToString:NSAccessibilityPressAction]) {
    static_cast<void>([self accessibilityPerformPress]);
  } else if ([action isEqualToString:NSAccessibilityConfirmAction]) {
    static_cast<void>([self accessibilityPerformConfirm]);
  }
}

- (BOOL)accessibilityPerformPress {
  if (_owner == nullptr || _identifier == nil) return NO;
  seam::native_ui::SemanticAction requested;
  if (_hasActivate) {
    requested = seam::native_ui::SemanticAction::Activate;
  } else if (_hasToggle) {
    requested = seam::native_ui::SemanticAction::Toggle;
  } else {
    return NO;
  }
  const char* bytes = _identifier.UTF8String;
  if (bytes == nullptr) return NO;
  return static_cast<bool>(_owner->dispatchAccessibility(bytes, requested));
}

- (BOOL)accessibilityPerformConfirm {
  if (_owner == nullptr || _identifier == nil) return NO;
  if (std::find(_actions.begin(), _actions.end(),
                seam::native_ui::SemanticAction::EditText) == _actions.end()) {
    return NO;
  }
  const char* bytes = _identifier.UTF8String;
  if (bytes == nullptr) return NO;
  return static_cast<bool>(_owner->dispatchAccessibility(
      bytes, seam::native_ui::SemanticAction::EditText));
}

- (void)setAccessibilityFocused:(BOOL)focused {
  if (!focused || _owner == nullptr || _identifier == nil) return;
  const char* bytes = _identifier.UTF8String;
  if (bytes != nullptr) {
    static_cast<void>(_owner->dispatchAccessibility(
        bytes, seam::native_ui::SemanticAction::SetFocus));
  }
}
@end

@implementation SeamAccessibilityNotesPage {
  seam::native_ui::AppKitNativeWindow* _owner;
  __weak id _parent;
  NSRect _frame;
  std::size_t _offset;
  std::size_t _limit;
  NSString* __strong _identifier;
  NSString* __strong _title;
  NSString* __strong _value;
}

- (instancetype)initWithOwner:(seam::native_ui::AppKitNativeWindow*)owner
                         parent:(id)parent
                          frame:(NSRect)frame
                         offset:(std::size_t)offset
                          limit:(std::size_t)limit
                     identifier:(NSString*)identifier
                           title:(NSString*)title
                          value:(NSString*)value {
  self = [super init];
  if (self != nil) {
    _owner = owner;
    _parent = parent;
    _frame = frame;
    _offset = offset;
    _limit = limit;
    _identifier = [identifier copy];
    _title = [title copy];
    _value = [value copy];
  }
  return self;
}

- (BOOL)isAccessibilityElement { return YES; }
- (BOOL)accessibilityIsIgnored { return NO; }
- (id)accessibilityRole { return NSAccessibilityGroupRole; }
- (id)accessibilityRoleDescription {
  return NSAccessibilityRoleDescription(NSAccessibilityGroupRole, nil);
}
- (NSString*)accessibilityIdentifier { return _identifier; }
- (id)accessibilityTitle { return _title; }
- (id)accessibilityValue { return _value; }
- (id)accessibilityParent { return _parent; }
- (NSRect)accessibilityFrame { return _frame; }
- (BOOL)isAccessibilityEnabled { return YES; }
- (NSArray*)accessibilityChildren {
  return _owner == nullptr
             ? @[]
             : _owner->accessibilityNotes(_offset, _limit, self);
}
@end

@implementation SeamNativeWindowDelegate
- (BOOL)windowShouldClose:(id)sender {
  (void)sender;
  return _owner == nullptr || _owner->requestCloseFromWindow();
}
@end

@implementation SeamNativeEditorView {
  seam::native_ui::AppKitNativeWindow* _owner;
}

- (instancetype)initWithFrame:(NSRect)frame
                        owner:(seam::native_ui::AppKitNativeWindow*)owner {
  self = [super initWithFrame:frame];
  if (self != nil) {
    _owner = owner;
    [self setPostsFrameChangedNotifications:YES];
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(frameChanged:)
               name:NSViewFrameDidChangeNotification
             object:self];
  }
  return self;
}

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isAccessibilityElement { return YES; }
- (BOOL)isAccessibilityEnabled { return YES; }
- (BOOL)accessibilityIsIgnored { return NO; }
- (id)accessibilityRole { return NSAccessibilityGroupRole; }
- (id)accessibilityTitle { return @"Project SEAM editor"; }
- (id)accessibilityFocusedUIElement {
  return _owner == nullptr ? nil : _owner->accessibilityFocusedElement();
}
- (NSArray*)accessibilityChildren {
  return _owner == nullptr ? @[] : _owner->accessibilityChildren();
}
- (BOOL)acceptsFirstMouse:(NSEvent*)event {
  static_cast<void>(event);
  return YES;
}

- (void)frameChanged:(NSNotification*)notification {
  static_cast<void>(notification);
  if (_owner != nullptr) _owner->viewResized();
}

- (void)drawRect:(NSRect)dirtyRect {
  static_cast<void>(dirtyRect);
  if (_owner != nullptr) _owner->drawInView(self);
}

- (void)mouseDown:(NSEvent*)event {
  if (_owner != nullptr) _owner->pointerDown(event);
}
- (void)rightMouseDown:(NSEvent*)event {
  if (_owner != nullptr) _owner->pointerDown(event);
}
- (void)otherMouseDown:(NSEvent*)event {
  if (_owner != nullptr) _owner->pointerDown(event);
}
- (void)mouseDragged:(NSEvent*)event {
  if (_owner != nullptr) _owner->pointerMove(event, true);
}
- (void)rightMouseDragged:(NSEvent*)event {
  if (_owner != nullptr) _owner->pointerMove(event, true);
}
- (void)otherMouseDragged:(NSEvent*)event {
  if (_owner != nullptr) _owner->pointerMove(event, true);
}
- (void)mouseMoved:(NSEvent*)event {
  if (_owner != nullptr) _owner->pointerMove(event, false);
}
- (void)mouseUp:(NSEvent*)event {
  if (_owner != nullptr) _owner->pointerUp(event);
}
- (void)rightMouseUp:(NSEvent*)event {
  if (_owner != nullptr) _owner->pointerUp(event);
}
- (void)otherMouseUp:(NSEvent*)event {
  if (_owner != nullptr) _owner->pointerUp(event);
}
- (void)scrollWheel:(NSEvent*)event {
  if (_owner != nullptr) _owner->scroll(event);
}
- (void)keyDown:(NSEvent*)event {
  if (_owner != nullptr) _owner->keyDown(event, self);
}

- (BOOL)hasMarkedText {
  return _owner != nullptr && _owner->hasMarkedText();
}
- (NSRange)markedRange {
  return _owner == nullptr ? NSMakeRange(NSNotFound, 0U)
                           : _owner->markedRange();
}
- (NSRange)selectedRange {
  return _owner == nullptr ? NSMakeRange(NSNotFound, 0U)
                           : _owner->selectedRange();
}
- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selectedRange
      replacementRange:(NSRange)replacementRange {
  if (_owner != nullptr) {
    _owner->setMarkedText(string, selectedRange, replacementRange);
  }
}
- (void)unmarkText {
  if (_owner != nullptr) _owner->unmarkText();
}
- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText {
  return @[];
}
- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
                                               actualRange:(NSRangePointer)actualRange {
  return _owner == nullptr ? nil : _owner->attributedSubstring(range, actualRange);
}
- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
  if (_owner != nullptr) _owner->insertText(string, replacementRange);
}
- (NSUInteger)characterIndexForPoint:(NSPoint)point {
  return _owner == nullptr ? 0U : _owner->characterIndexForPoint(point);
}
- (NSRect)firstRectForCharacterRange:(NSRange)range
                         actualRange:(NSRangePointer)actualRange {
  return _owner == nullptr ? NSZeroRect
                           : _owner->firstRectForCharacterRange(range, actualRange);
}
- (void)doCommandBySelector:(SEL)selector {
  if (_owner != nullptr) _owner->command(selector);
}
- (BOOL)drawsVerticallyForCharacterAtIndex:(NSUInteger)charIndex {
  static_cast<void>(charIndex);
  return NO;
}

@end

#endif
