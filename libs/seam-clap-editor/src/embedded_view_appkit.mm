#include "seam/clap_editor/embedded_view.hpp"

#if defined(__APPLE__)

#include "seam/domain/note.hpp"
#include "seam/text/text_engine.hpp"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>

namespace seam::clap_editor {
class AppKitEmbeddedView;
}

@interface SeamClapChildView : NSView <NSTextFieldDelegate> {
 @public seam::clap_editor::AppKitEmbeddedView* owner_;
}
@property(nonatomic, strong) NSTextField* editField;
- (id)accessibilityFocusedUIElement;
@end

@interface SeamClapAccessibilityElement : NSAccessibilityElement
- (instancetype)initWithOwner:(seam::clap_editor::AppKitEmbeddedView*)owner
                           node:(const seam::native_ui::SemanticNode&)node
                         parent:(id)parent
                          frame:(NSRect)frame;
- (void)setChildren:(NSArray*)children;
- (void)seamApplyAccessibilityValue:(id)value;
@end

@interface SeamClapNotesPage : NSAccessibilityElement
- (instancetype)initWithOwner:(seam::clap_editor::AppKitEmbeddedView*)owner
                         parent:(id)parent
                          frame:(NSRect)frame
                         offset:(std::size_t)offset
                          limit:(std::size_t)limit
                     identifier:(NSString*)identifier
                           title:(NSString*)title
                          value:(NSString*)value;
@end

namespace seam::clap_editor {
namespace {

native_ui::InputModifiers modifiers(NSEventModifierFlags flags) noexcept {
  return native_ui::InputModifiers{
      .shift = (flags & NSEventModifierFlagShift) != 0,
      .control = (flags & NSEventModifierFlagControl) != 0,
      .alt = (flags & NSEventModifierFlagOption) != 0,
      .command = (flags & NSEventModifierFlagCommand) != 0,
  };
}

native_ui::NativeKey keyFor(NSEvent* event) noexcept {
  if (event == nil) return native_ui::NativeKey::Unknown;
  const auto value = event.keyCode;
  switch (value) {
    case 49: return native_ui::NativeKey::Space;
    case 36:
    case 76: return native_ui::NativeKey::Enter;
    case 53: return native_ui::NativeKey::Escape;
    case 51: return native_ui::NativeKey::Backspace;
    case 117: return native_ui::NativeKey::Delete;
    case 123: return native_ui::NativeKey::Left;
    case 124: return native_ui::NativeKey::Right;
    case 125: return native_ui::NativeKey::Down;
    case 126: return native_ui::NativeKey::Up;
    case 6: return native_ui::NativeKey::Z;
    case 16: return native_ui::NativeKey::Y;
    case 8: return native_ui::NativeKey::C;
    case 24: return native_ui::NativeKey::Plus;
    case 27: return native_ui::NativeKey::Minus;
    default: return native_ui::NativeKey::Unknown;
  }
}

std::u32string utf32(NSString* text) {
  if (text == nil) return {};
  const char* utf8 = text.UTF8String;
  if (utf8 == nullptr) return {};
  const auto decoded = domain::fromUtf8(utf8);
  return decoded ? decoded.value() : std::u32string{};
}

}  // namespace

class AppKitEmbeddedView final : public IEmbeddedView {
public:
  explicit AppKitEmbeddedView(EditorRuntime& runtime) : runtime_(runtime) {
    runtime_.setRepaintCallback([this] { requestRepaint(); });
    runtime_.setTextInputCallbacks(
        [this](const native_ui::TextInputRequest& request) {
          beginTextInput(request);
        },
        [this] { endTextInput(); });
  }

  ~AppKitEmbeddedView() override { destroy(); }

  [[nodiscard]] bool supportsApi(std::string_view api) const noexcept override {
    return api == "cocoa";
  }

  [[nodiscard]] bool create(std::string_view api, bool floating) override {
    if (floating || !supportsApi(api) || created_) return false;
    auto textEngine = text::TextEngine::createSystem();
    if (textEngine) textEngine_ = std::move(textEngine.value());
    created_ = true;
    return true;
  }

  void destroy() noexcept override {
    runtime_.setTextInputCallbacks({}, {});
    if (view_ != nil) {
      view_->owner_ = nullptr;
      [view_ removeFromSuperview];
    }
    view_ = nil;
    parent_ = nil;
    textEngine_.reset();
    accessibilitySnapshot_ = nil;
    accessibilitySnapshotDirty_.store(true, std::memory_order_release);
    created_ = false;
    visible_ = false;
  }

  [[nodiscard]] bool setScale(double scale) override {
    if (!std::isfinite(scale) || scale < 0.5 || scale > 4.0) return false;
    scale_ = scale;
    updateSurface();
    requestRepaint();
    return true;
  }

  [[nodiscard]] bool setSize(std::uint32_t width,
                             std::uint32_t height) override {
    if (width < 640U || height < 420U || width > 8192U || height > 8192U) {
      return false;
    }
    width_ = width;
    height_ = height;
    if (view_ != nil) {
      view_.frame = NSMakeRect(0.0, 0.0, static_cast<CGFloat>(width_),
                               static_cast<CGFloat>(height_));
    }
    updateSurface();
    requestRepaint();
    return true;
  }

  [[nodiscard]] bool setParent(std::uintptr_t parent) override {
    if (!created_ || parent == 0U || view_ != nil) return false;
    parent_ = (__bridge NSView*)reinterpret_cast<void*>(parent);
    if (parent_ == nil) return false;
    view_ = [[SeamClapChildView alloc]
        initWithFrame:NSMakeRect(0.0, 0.0, static_cast<CGFloat>(width_),
                                 static_cast<CGFloat>(height_))];
    view_->owner_ = this;
    view_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [parent_ addSubview:view_];
    updateSurface();
    return true;
  }

  [[nodiscard]] bool show() override {
    if (view_ == nil) return false;
    view_.hidden = NO;
    visible_ = true;
    requestRepaint();
    return true;
  }

  [[nodiscard]] bool hide() override {
    if (view_ == nil) return false;
    view_.hidden = YES;
    visible_ = false;
    return true;
  }

  void onTimer() noexcept override {
    runtime_.tick();
    if (visible_ && repaint_.exchange(false, std::memory_order_acq_rel) &&
        view_ != nil) {
      [view_ setNeedsDisplay:YES];
    }
  }

  void requestRepaint() noexcept override {
    repaint_.store(true, std::memory_order_release);
    accessibilityAnnouncementPending_.store(true, std::memory_order_release);
    accessibilitySnapshotDirty_.store(true, std::memory_order_release);
    if (view_ != nil) {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (view_ != nil) [view_ setNeedsDisplay:YES];
      });
    }
  }

  [[nodiscard]] std::string_view apiName() const noexcept override {
    return "cocoa";
  }

  void draw(NSView* view) noexcept {
    if (view == nil) return;
    updateSurface();
    if (surface_.pixels().empty()) return;
    native_ui::RasterCanvas canvas{surface_, scale_, textEngine_.get()};
    runtime_.paint(canvas);
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
      CGContextDrawImage(context, CGRectMake(0.0, 0.0, view.bounds.size.width,
                                              view.bounds.size.height), image);
      CGImageRelease(image);
    }
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpace);
  }

  NSArray* accessibilityChildren() {
    if (view_ == nil || view_.window == nil) return @[];
    if (accessibilitySnapshot_ != nil &&
        !accessibilitySnapshotDirty_.exchange(false, std::memory_order_acq_rel)) {
      return accessibilitySnapshot_;
    }
    const auto snapshot = runtime_.accessibilitySnapshot();
    std::size_t visibleNotes = 0U;
    for (const auto& node : snapshot.children) {
      if (node.role == native_ui::SemanticRole::Note) ++visibleNotes;
    }
    const auto pageSize = std::size_t{512U};
    const auto pageCount = snapshot.virtualizedNoteCount > visibleNotes
                               ? (snapshot.virtualizedNoteCount - visibleNotes +
                                  pageSize - 1U) /
                                     pageSize
                               : 0U;
    NSMutableArray* result = [NSMutableArray arrayWithCapacity:
        snapshot.children.size() + pageCount];
    for (const auto& node : snapshot.children) {
      [result addObject:makeAccessibilityElement(node, view_)];
    }
    const auto pageFrame = screenFrame(ui::Rect{0.0, 0.0,
                                                view_.bounds.size.width,
                                                view_.bounds.size.height});
    for (std::size_t offset = visibleNotes;
         offset < snapshot.virtualizedNoteCount; offset += pageSize) {
      const auto limit = std::min(pageSize,
                                  snapshot.virtualizedNoteCount - offset);
      NSString* identifier = [NSString stringWithFormat:
          @"timeline.notes.page.%lu", static_cast<unsigned long>(offset)];
      NSString* title = [NSString stringWithFormat:
          @"Notes %lu-%lu", static_cast<unsigned long>(offset + 1U),
          static_cast<unsigned long>(offset + limit)];
      NSString* value = [NSString stringWithFormat:
          @"%lu notes", static_cast<unsigned long>(limit)];
      [result addObject:[[SeamClapNotesPage alloc]
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
    return accessibilitySnapshot_;
  }

  NSArray* accessibilityNotes(std::size_t offset, std::size_t limit,
                              id parent) {
    NSMutableArray* result = [NSMutableArray arrayWithCapacity:limit];
    for (const auto& node : runtime_.accessibilityNotes(offset, limit)) {
      [result addObject:makeAccessibilityElement(node, parent)];
    }
    return result;
  }

  NSRect screenFrame(ui::Rect bounds) const {
    if (view_ == nil || view_.window == nil) return NSZeroRect;
    const NSRect logical = NSMakeRect(bounds.x, bounds.y, bounds.width,
                                      bounds.height);
    return [view_.window convertRectToScreen:
                [view_ convertRect:logical toView:nil]];
  }

  core::Result<void> dispatchAccessibility(std::string_view id,
                                           native_ui::SemanticAction action) {
    const auto result = runtime_.dispatchAccessibility(id, action);
    if (result && action == native_ui::SemanticAction::SetFocus && view_ != nil) {
      NSAccessibilityPostNotification(
          view_, NSAccessibilityFocusedUIElementChangedNotification);
    }
    return result;
  }

  core::Result<void> setAccessibilityValue(std::string_view id,
                                            std::string_view value) {
    return runtime_.setAccessibilityValue(id, value);
  }

  id accessibilityFocusedElement() {
    if (view_ == nil) return nil;
    const auto focused = runtime_.accessibilityFocusedNode();
    return focused.has_value() ? makeAccessibilityElement(*focused, view_)
                                : nil;
  }

  void resized() noexcept {
    updateSurface();
    requestRepaint();
  }

  void mouseDown(NSEvent* event) noexcept {
    if (event == nil || view_ == nil) return;
    const auto point = [view_ convertPoint:event.locationInWindow fromView:nil];
    runtime_.pointerDown(native_ui::PointerEvent{
        .position = ui::Point{point.x, point.y},
        .button = event.type == NSEventTypeRightMouseDown
                      ? native_ui::PointerButton::Right
                      : native_ui::PointerButton::Left,
        .modifiers = modifiers(event.modifierFlags),
        .clickCount = static_cast<int>(event.clickCount),
    });
  }

  void mouseMove(NSEvent* event, bool dragging) noexcept {
    if (event == nil || view_ == nil) return;
    const auto point = [view_ convertPoint:event.locationInWindow fromView:nil];
    runtime_.pointerMove(native_ui::PointerEvent{
        .position = ui::Point{point.x, point.y},
        .button = dragging ? native_ui::PointerButton::Left
                           : native_ui::PointerButton::NoButton,
        .modifiers = modifiers(event.modifierFlags),
        .clickCount = 1,
    });
  }

  void mouseUp(NSEvent* event) noexcept {
    if (event == nil || view_ == nil) return;
    const auto point = [view_ convertPoint:event.locationInWindow fromView:nil];
    runtime_.pointerUp(native_ui::PointerEvent{
        .position = ui::Point{point.x, point.y},
        .button = native_ui::PointerButton::Left,
        .modifiers = modifiers(event.modifierFlags),
        .clickCount = 1,
    });
  }

  void scroll(NSEvent* event) noexcept {
    if (event == nil || view_ == nil) return;
    const auto point = [view_ convertPoint:event.locationInWindow fromView:nil];
    runtime_.scroll(event.scrollingDeltaX, -event.scrollingDeltaY,
                    ui::Point{point.x, point.y},
                    modifiers(event.modifierFlags));
  }

  void keyDown(NSEvent* event) noexcept {
    if (event == nil || runtime_.textInputActive()) return;
    runtime_.keyDown(native_ui::KeyEvent{
        .key = keyFor(event),
        .modifiers = modifiers(event.modifierFlags),
        .repeat = event.isARepeat,
    });
  }

  void textChanged() noexcept {
    if (view_ == nil || view_.editField == nil || !runtime_.textInputActive()) {
      return;
    }
    runtime_.textComposition(utf32(view_.editField.stringValue), {});
  }

  void commitText() noexcept {
    if (view_ == nil || view_.editField == nil) return;
    runtime_.textCommit(utf32(view_.editField.stringValue));
    endTextInput();
  }

private:
  SeamClapAccessibilityElement* makeAccessibilityElement(
      const native_ui::SemanticNode& node, id parent) {
    auto* element = [[SeamClapAccessibilityElement alloc]
        initWithOwner:this
                  node:node
                parent:parent
                 frame:screenFrame(node.bounds)];
    NSMutableArray* children = [NSMutableArray arrayWithCapacity:
        node.children.size()];
    for (const auto& child : node.children) {
      [children addObject:makeAccessibilityElement(child, element)];
    }
    [element setChildren:children];
    return element;
  }

  void updateSurface() noexcept {
    if (view_ == nil) return;
    const auto backing = view_.window.backingScaleFactor;
    const auto effective = scale_ * static_cast<double>(backing > 0.0 ? backing : 1.0);
    const auto width = static_cast<std::uint32_t>(std::max(
        1.0, std::ceil(static_cast<double>(view_.bounds.size.width) * effective)));
    const auto height = static_cast<std::uint32_t>(std::max(
        1.0, std::ceil(static_cast<double>(view_.bounds.size.height) * effective)));
    if (width != surface_.width() || height != surface_.height()) {
      static_cast<void>(surface_.resize(width, height));
    }
    runtime_.resize(view_.bounds.size.width, view_.bounds.size.height);
  }

  void beginTextInput(const native_ui::TextInputRequest& request) {
    if (view_ == nil) return;
    if (view_.editField == nil) {
      view_.editField = [[NSTextField alloc] initWithFrame:NSZeroRect];
      view_.editField.delegate = view_;
      view_.editField.bordered = YES;
      view_.editField.drawsBackground = YES;
      [view_ addSubview:view_.editField];
    }
    view_.editField.frame = NSMakeRect(request.logicalBounds.x,
                                       request.logicalBounds.y,
                                       std::max(80.0, request.logicalBounds.width),
                                       std::max(24.0, request.logicalBounds.height));
    const auto utf8 = domain::toUtf8(request.currentText);
    view_.editField.stringValue = [NSString stringWithUTF8String:utf8.c_str()];
    view_.editField.hidden = NO;
    [view_.window makeFirstResponder:view_.editField];
  }

  void endTextInput() noexcept {
    if (view_ == nil || view_.editField == nil) return;
    view_.editField.hidden = YES;
    [view_.window makeFirstResponder:view_];
  }

  EditorRuntime& runtime_;
  NSView* __strong parent_{nil};
  SeamClapChildView* __strong view_{nil};
  native_ui::PixelSurface surface_;
  std::unique_ptr<text::TextEngine> textEngine_;
  std::uint32_t width_{960U};
  std::uint32_t height_{680U};
  double scale_{1.0};
  bool created_{false};
  bool visible_{false};
  std::atomic<bool> repaint_{true};
  std::atomic<bool> accessibilityAnnouncementPending_{true};
  std::atomic<bool> accessibilitySnapshotDirty_{true};
  NSArray* __strong accessibilitySnapshot_{nil};
};

}  // namespace seam::clap_editor

@implementation SeamClapAccessibilityElement {
  seam::clap_editor::AppKitEmbeddedView* _owner;
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
  BOOL _hasEditText;
}

- (instancetype)initWithOwner:(seam::clap_editor::AppKitEmbeddedView*)owner
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
      case seam::native_ui::SemanticRole::Note:
        _role = editable ? NSAccessibilityTextFieldRole
                         : NSAccessibilityGroupRole;
        break;
      default:
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
    _hasEditText = editable;
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
  if (_hasActivate || _hasToggle) [result addObject:NSAccessibilityPressAction];
  if (_hasEditText) [result addObject:NSAccessibilityConfirmAction];
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
  if (_owner == nullptr || _identifier == nil || !_hasEditText) return NO;
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

@implementation SeamClapNotesPage {
  seam::clap_editor::AppKitEmbeddedView* _owner;
  __weak id _parent;
  NSRect _frame;
  std::size_t _offset;
  std::size_t _limit;
  NSString* __strong _identifier;
  NSString* __strong _title;
  NSString* __strong _value;
}

- (instancetype)initWithOwner:(seam::clap_editor::AppKitEmbeddedView*)owner
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
  return _owner == nullptr ? @[] : _owner->accessibilityNotes(_offset, _limit, self);
}
@end

@implementation SeamClapChildView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isAccessibilityElement { return YES; }
- (BOOL)isAccessibilityEnabled { return YES; }
- (BOOL)accessibilityIsIgnored { return NO; }
- (id)accessibilityRole { return NSAccessibilityGroupRole; }
- (id)accessibilityTitle { return @"Project SEAM CLAP editor"; }
- (id)accessibilityFocusedUIElement {
  return owner_ == nullptr ? nil : owner_->accessibilityFocusedElement();
}
- (NSArray*)accessibilityChildren {
  return owner_ == nullptr ? @[] : owner_->accessibilityChildren();
}
- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  if (owner_ != nullptr) owner_->draw(self);
}
- (void)setFrameSize:(NSSize)newSize {
  [super setFrameSize:newSize];
  if (owner_ != nullptr) owner_->resized();
}
- (void)mouseDown:(NSEvent*)event {
  if (owner_ != nullptr) owner_->mouseDown(event);
}
- (void)mouseDragged:(NSEvent*)event {
  if (owner_ != nullptr) owner_->mouseMove(event, true);
}
- (void)mouseMoved:(NSEvent*)event {
  if (owner_ != nullptr) owner_->mouseMove(event, false);
}
- (void)mouseUp:(NSEvent*)event {
  if (owner_ != nullptr) owner_->mouseUp(event);
}
- (void)scrollWheel:(NSEvent*)event {
  if (owner_ != nullptr) owner_->scroll(event);
}
- (void)keyDown:(NSEvent*)event {
  if (owner_ != nullptr) owner_->keyDown(event);
}
- (void)controlTextDidChange:(NSNotification*)notification {
  (void)notification;
  if (owner_ != nullptr) owner_->textChanged();
}
- (void)controlTextDidEndEditing:(NSNotification*)notification {
  (void)notification;
  if (owner_ != nullptr) owner_->commitText();
}
@end

namespace seam::clap_editor {
std::unique_ptr<IEmbeddedView> createEmbeddedView(EditorRuntime& runtime) {
  return std::make_unique<AppKitEmbeddedView>(runtime);
}
}  // namespace seam::clap_editor

#endif
