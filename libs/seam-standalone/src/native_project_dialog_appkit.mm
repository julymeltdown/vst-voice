#include "seam/standalone/native_project_dialog.hpp"

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <dispatch/dispatch.h>

#include "seam/native_ui/new_project_dialog.hpp"
#include "seam/native_ui/conversion_review_model.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

NSString* conversionString(std::string_view value) {
  auto* text = [[NSString alloc] initWithBytes:value.data()
                                      length:value.size()
                                    encoding:NSUTF8StringEncoding];
  return text != nil ? text : @"[Invalid UTF-8 text]";
}

}  // namespace

@interface SEAMConversionReviewTable : NSObject <NSTableViewDataSource, NSTableViewDelegate>
@property(nonatomic, assign) const std::vector<seam::authoring::InterchangeIssue>* issues;
@property(nonatomic, strong) NSTextView* details;
@end

static std::string conversionIssueDetails(
    const seam::authoring::InterchangeIssue& item) {
  return std::string{item.loss ? "Loss" : "Warning"} + " (" +
      (item.format == seam::authoring::InterchangeFormat::Ustx ? "USTX" : "MIDI") +
      ")\nLocation: " + item.path + "\n\n" + item.message;
}

@implementation SEAMConversionReviewTable
- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView {
  (void)tableView;
  return self.issues == nullptr ? 0 : static_cast<NSInteger>(self.issues->size());
}

- (NSView*)tableView:(NSTableView*)tableView
    viewForTableColumn:(NSTableColumn*)column row:(NSInteger)row {
  if (self.issues == nullptr || row < 0 ||
      static_cast<std::size_t>(row) >= self.issues->size()) return nil;
  const auto* item = &(*self.issues)[static_cast<std::size_t>(row)];
  auto* cell = static_cast<NSTextField*>(
      [tableView makeViewWithIdentifier:column.identifier owner:self]);
  if (cell == nil) {
    cell = [NSTextField labelWithString:@""];
    cell.identifier = column.identifier;
    cell.maximumNumberOfLines = 1;
    cell.lineBreakMode = NSLineBreakByTruncatingTail;
  }
  std::string_view value;
  if ([column.identifier isEqualToString:@"severity"]) {
    value = item->loss ? "Loss" : "Warning";
  } else if ([column.identifier isEqualToString:@"location"]) {
    value = item->path;
  } else {
    value = item->message;
  }
  cell.stringValue = conversionString(value);
  cell.toolTip = cell.stringValue;
  cell.accessibilityLabel = [NSString stringWithFormat:@"%@: %@",
      column.title, cell.stringValue];
  return cell;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification {
  auto* table = static_cast<NSTableView*>(notification.object);
  if (self.issues == nullptr || table.selectedRow < 0 ||
      static_cast<std::size_t>(table.selectedRow) >= self.issues->size()) return;
  self.details.string = conversionString(
      conversionIssueDetails((*self.issues)[static_cast<std::size_t>(table.selectedRow)]));
  [self.details scrollRangeToVisible:NSMakeRange(0U, 0U)];
}
@end

@interface SEAMVoicebankToggleTarget : NSObject
@property(nonatomic, assign) NSPopUpButton* voicebank;
@property(nonatomic, assign) NSPopUpButton* proceduralSinger;
@end

@implementation SEAMVoicebankToggleTarget
- (void)toggleVoicebank:(NSButton*)sender {
  const auto enabled = sender.state == NSControlStateValueOn;
  self.voicebank.enabled = enabled;
  self.proceduralSinger.enabled = enabled;
  if (!enabled) {
    [self.voicebank selectItemAtIndex:0];
    [self.proceduralSinger selectItemAtIndex:0];
  }
}
- (void)selectInitialSinger:(NSPopUpButton*)sender {
  if (sender == self.voicebank && sender.indexOfSelectedItem > 0) {
    [self.proceduralSinger selectItemAtIndex:0];
  } else if (sender == self.proceduralSinger && sender.indexOfSelectedItem > 0) {
    [self.voicebank selectItemAtIndex:0];
  }
}
@end

namespace seam::standalone {
namespace {

NSString* nsString(std::string_view value) {
  return [[NSString alloc] initWithBytes:value.data()
                                  length:value.size()
                                encoding:NSUTF8StringEncoding];
}

std::string utf8(NSString* value) {
  const auto* bytes = value == nil ? nullptr : value.UTF8String;
  return bytes == nullptr ? std::string{} : std::string{bytes};
}

NSTextField* label(NSString* title, NSRect frame) {
  auto* result = [NSTextField labelWithString:title];
  result.frame = frame;
  return result;
}

NSPopUpButton* popup(NSRect frame, std::initializer_list<NSString*> values) {
  auto* result = [[NSPopUpButton alloc] initWithFrame:frame pullsDown:NO];
  for (NSString* value : values) [result addItemWithTitle:value];
  return result;
}

std::optional<std::uint32_t> unsignedValue(NSString* value) {
  try {
    const auto parsed = std::stoul(utf8(value));
    if (parsed > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    return static_cast<std::uint32_t>(parsed);
  } catch (...) {
    return std::nullopt;
  }
}

std::string suggestedProjectName(std::string_view suggested) {
  const auto fallback = suggested.empty() ? std::string{"Untitled"}
                                           : std::string{suggested};
  const auto path = std::filesystem::path{fallback};
  return path.extension() == ".seam" ? path.stem().string() : fallback;
}

std::string suggestedProjectFileName(std::string_view name) {
  const auto path = std::filesystem::path{name};
  return path.extension() == ".seam" ? std::string{name}
                                     : std::string{name} + ".seam";
}

NSTextView* conversionTextView(NSView* parent, NSRect frame,
                              NSString* accessibilityLabel, NSString* text) {
  auto* scroll = [[NSScrollView alloc] initWithFrame:frame];
  scroll.hasVerticalScroller = YES;
  scroll.hasHorizontalScroller = NO;
  scroll.borderType = NSBezelBorder;
  auto* contents = [[NSTextView alloc] initWithFrame:
      NSMakeRect(0.0, 0.0, scroll.contentSize.width, scroll.contentSize.height)];
  contents.editable = NO;
  contents.selectable = YES;
  contents.richText = NO;
  contents.usesFindPanel = YES;
  contents.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
  contents.textContainerInset = NSMakeSize(6.0, 6.0);
  contents.verticallyResizable = YES;
  contents.horizontallyResizable = NO;
  contents.autoresizingMask = NSViewWidthSizable;
  contents.textContainer.widthTracksTextView = YES;
  contents.textContainer.containerSize =
      NSMakeSize(scroll.contentSize.width, std::numeric_limits<CGFloat>::max());
  contents.maxSize = NSMakeSize(std::numeric_limits<CGFloat>::max(),
                                std::numeric_limits<CGFloat>::max());
  contents.accessibilityLabel = accessibilityLabel;
  contents.string = text;
  scroll.documentView = contents;
  [parent addSubview:scroll];
  return contents;
}

class AppKitNativeInterchangeReviewDialog final
    : public INativeInterchangeReviewDialog {
public:
  core::Result<bool> review(
      const authoring::InterchangeImportDraft& draft) override {
    const native_ui::ConversionReviewModel model{draft};
    return reviewIssues(draft.issues, "Review Interchange Import",
        model.summary() +
            "\nImport replaces the current SEAM song with an unsaved document. Save the "
            "current song or host session first; Undo cannot restore it. The source file "
            "is unchanged.",
        model.sourceDetails(), model.singerDisclosure(),
        "Imported source path, SHA-256, and project identity", "Import");
  }

  core::Result<bool> reviewExport(
      const authoring::InterchangeExportDraft& draft) override {
    const auto losses = std::count_if(draft.issues.begin(), draft.issues.end(),
        [](const auto& issue) { return issue.loss; });
    const auto format = draft.format == authoring::InterchangeFormat::Ustx
        ? "USTX" : "MIDI";
    return reviewIssues(draft.issues, "Review Score Export",
        std::string{format} + " export: " + std::to_string(losses) + " losses; " +
            std::to_string(draft.issues.size() - static_cast<std::size_t>(losses)) +
            " warnings\nThe destination does not exist yet. Export creates a new file and "
            "does not change the SEAM song. Musical information listed as a loss will "
            "not be present in the exported file.",
        "Destination: " + draft.destination.string() + "\nSHA-256: " +
            draft.contentHash + "\nBytes: " + std::to_string(draft.bytes.size()),
        "Review all losses and warnings before writing. Cancel leaves the destination "
        "uncreated; an existing file is never replaced.",
        "Export destination, SHA-256, and byte count", "Export");
  }

private:
  core::Result<bool> reviewIssues(
      const std::vector<authoring::InterchangeIssue>& issues,
      std::string_view title, const std::string& summary,
      const std::string& fileDetails, const std::string& disclosureText,
      std::string_view fileAccessibilityLabel, std::string_view acceptVerb) {
    if (![NSThread isMainThread]) {
      return core::failure<bool>(core::ErrorCode::InvalidState,
          "AppKit interchange review must run on the main thread");
    }
    @autoreleasepool {
      const bool hasLosses = std::any_of(issues.begin(), issues.end(),
          [](const auto& issue) { return issue.loss; });
      const bool hasIssues = !issues.empty();
      // Only the selected detail is materialized. NSTableView requests visible
      // rows lazily; a large admitted conversion report stays scrollable. A
      // loss-free conversion gets a compact status row instead of an empty table.
      const auto screenHeight = NSScreen.mainScreen.visibleFrame.size.height;
      const auto desiredTableHeight =
          static_cast<CGFloat>(issues.size()) * 22.0 + 24.0;
      const auto availableTableHeight = std::max(46.0, screenHeight - 500.0);
      const auto tableHeight = hasIssues
          ? std::clamp(std::min(desiredTableHeight, availableTableHeight),
                       46.0, 230.0)
          : 0.0;
      constexpr CGFloat width = 680.0;
      const CGFloat viewHeight = hasIssues ? 254.0 + tableHeight : 164.0;
      auto* view = [[NSView alloc] initWithFrame:
          NSMakeRect(0.0, 0.0, width, viewHeight)];
      NSTextView* details = nil;
      NSTableView* table = nil;
      SEAMConversionReviewTable* dataSource = nil;
      if (hasIssues) {
        details = conversionTextView(view,
            NSMakeRect(0.0, 0.0, width, 82.0),
            @"Selected conversion issue, full text",
            conversionString(conversionIssueDetails(issues.front())));
        [view addSubview:label(@"Selected issue — full location and message",
                              NSMakeRect(0.0, 85.0, width, 18.0))];

        auto* tableScroll = [[NSScrollView alloc] initWithFrame:
            NSMakeRect(0.0, 107.0, width, tableHeight)];
        tableScroll.hasVerticalScroller = YES;
        // The fixed columns fit this viewport. A horizontal scroller consumed
        // the last row's height even though every cell has a full-detail view.
        tableScroll.hasHorizontalScroller = NO;
        tableScroll.borderType = NSBezelBorder;
        table = [[NSTableView alloc] initWithFrame:tableScroll.bounds];
        table.accessibilityLabel = @"Conversion losses and warnings";
        table.rowHeight = 22.0;
        table.usesAlternatingRowBackgroundColors = YES;
        table.allowsMultipleSelection = NO;
        table.allowsEmptySelection = NO;
        table.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
        auto* severity = [[NSTableColumn alloc] initWithIdentifier:@"severity"];
        severity.title = @"Type";
        severity.width = 80.0;
        auto* location = [[NSTableColumn alloc] initWithIdentifier:@"location"];
        location.title = @"Location";
        location.width = 190.0;
        auto* message = [[NSTableColumn alloc] initWithIdentifier:@"message"];
        message.title = @"Message";
        message.width = 390.0;
        [table addTableColumn:severity];
        [table addTableColumn:location];
        [table addTableColumn:message];
        dataSource = [[SEAMConversionReviewTable alloc] init];
        dataSource.issues = &issues;
        dataSource.details = details;
        table.dataSource = dataSource;
        table.delegate = dataSource;
        tableScroll.documentView = table;
        [view addSubview:tableScroll];
        [table reloadData];
        [table selectRowIndexes:[NSIndexSet indexSetWithIndex:0U]
            byExtendingSelection:NO];
        [view addSubview:label(@"Conversion report — select a row to read its full details",
            NSMakeRect(0.0, 111.0 + tableHeight, width, 18.0))];
      } else {
        auto* emptyState = [NSTextField wrappingLabelWithString:
            @"No conversion issues were reported."];
        emptyState.frame = NSMakeRect(0.0, 0.0, width, 22.0);
        emptyState.accessibilityLabel = emptyState.stringValue;
        [view addSubview:emptyState];
      }
      auto* source = conversionTextView(view,
          NSMakeRect(0.0, hasIssues ? 133.0 + tableHeight : 30.0,
                     width, 70.0),
          conversionString(fileAccessibilityLabel),
          conversionString(fileDetails));
      auto* disclosure = [NSTextField wrappingLabelWithString:
          conversionString(disclosureText)];
      disclosure.frame = NSMakeRect(0.0,
          hasIssues ? 209.0 + tableHeight : 106.0, width, 50.0);
      disclosure.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
      disclosure.accessibilityLabel = disclosure.stringValue;
      [view addSubview:disclosure];

      auto* alert = [[NSAlert alloc] init];
      alert.messageText = conversionString(title);
      alert.informativeText = conversionString(summary);
      alert.alertStyle = hasLosses ? NSAlertStyleWarning
                                  : NSAlertStyleInformational;
      alert.accessoryView = view;
      // Cancel is deliberately the default, especially for lossy conversion.
      // Pressing Return or Escape is not consent to discard musical data.
      auto* cancel = [alert addButtonWithTitle:@"Cancel"];
      cancel.keyEquivalent = @"\033";
      const auto acceptTitle = std::string{acceptVerb} +
          (hasLosses ? " With Losses" : "");
      auto* accept = [alert addButtonWithTitle:conversionString(acceptTitle)];
      accept.keyEquivalent = @"";
      accept.accessibilityLabel = hasLosses
          ? @"Proceed after accepting the disclosed conversion losses"
          : @"Proceed with the reviewed conversion";
      [alert layout];
      alert.window.defaultButtonCell = cancel.cell;
      alert.window.initialFirstResponder = cancel;
      [alert.window makeFirstResponder:cancel];
      cancel.nextKeyView = hasIssues ? table : source;
      if (hasIssues) {
        table.nextKeyView = details;
        details.nextKeyView = source;
      }
      source.nextKeyView = accept;
      accept.nextKeyView = cancel;
      NSWindow* owner = NSApp.keyWindow;
      NSResponder* responder = owner.firstResponder;
      // Read-only accessory text views can consume Return/Escape before the
      // alert's default button sees them. Keep both keys safely cancel-only.
      id keyMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
          handler:^NSEvent* (NSEvent* event) {
            if (event.window != alert.window) return event;
            if (event.keyCode == 36U || event.keyCode == 53U ||
                event.keyCode == 76U) {
              [cancel performClick:nil];
              return nil;
            }
            return event;
          }];
      const auto response = [alert runModal];
      if (keyMonitor != nil) [NSEvent removeMonitor:keyMonitor];
      if (hasIssues) {
        table.delegate = nil;
        table.dataSource = nil;
        dataSource.issues = nullptr;
      }
      if (owner != nil && owner.visible) {
        [owner makeKeyWindow];
        if (responder != nil) [owner makeFirstResponder:responder];
      }
      return response == NSAlertSecondButtonReturn;
    }
  }
};

class AppKitNativeNewProjectDialog final : public INativeNewProjectDialog {
public:
  core::Result<std::optional<authoring::NewProjectRequest>> choose(
      NativeNewProjectDialogConfig config) override {
    if (![NSThread isMainThread]) {
      return core::failure<std::optional<authoring::NewProjectRequest>>(
          core::ErrorCode::InvalidState,
          "AppKit New Project form must run on the main thread");
    }

    @autoreleasepool {
      native_ui::NewProjectDialogModel model{config.candidates};
      const auto initialName = suggestedProjectName(config.suggestedName);
      model.setName(initialName);
      model.setTempoBpm(120.0);
      model.setMeter(4U, 4U);
      model.setSampleRate(config.sampleRate);
      model.setOutputChannels(config.outputChannels);

      auto* view = [[NSView alloc] initWithFrame:NSMakeRect(0.0, 0.0, 460.0, 336.0)];
      auto* name = [[NSTextField alloc] initWithFrame:NSMakeRect(150.0, 298.0, 294.0, 24.0)];
      name.stringValue = nsString(initialName);
      [view addSubview:label(@"Project name", NSMakeRect(12.0, 302.0, 126.0, 18.0))];
      [view addSubview:name];

      auto* tempo = [[NSTextField alloc] initWithFrame:NSMakeRect(150.0, 264.0, 90.0, 24.0)];
      tempo.stringValue = @"120";
      [view addSubview:label(@"Tempo (BPM)", NSMakeRect(12.0, 268.0, 126.0, 18.0))];
      [view addSubview:tempo];

      auto* numerator = popup(NSMakeRect(150.0, 230.0, 70.0, 26.0),
                              {@"2", @"3", @"4", @"5", @"6", @"7", @"8", @"9", @"12"});
      [numerator selectItemWithTitle:@"4"];
      auto* denominator = popup(NSMakeRect(230.0, 230.0, 70.0, 26.0),
                                {@"1", @"2", @"4", @"8", @"16", @"32"});
      [denominator selectItemWithTitle:@"4"];
      [view addSubview:label(@"Time signature", NSMakeRect(12.0, 234.0, 126.0, 18.0))];
      [view addSubview:numerator];
      [view addSubview:denominator];

      auto* sampleRate = popup(NSMakeRect(150.0, 196.0, 120.0, 26.0),
                               {@"44100", @"48000", @"96000"});
      [sampleRate selectItemWithTitle:nsString(std::to_string(config.sampleRate))];
      [view addSubview:label(@"Sample rate", NSMakeRect(12.0, 200.0, 126.0, 18.0))];
      [view addSubview:sampleRate];

      auto* channels = popup(NSMakeRect(150.0, 162.0, 90.0, 26.0),
                             {@"1", @"2", @"4", @"8"});
      [channels selectItemWithTitle:nsString(std::to_string(config.outputChannels))];
      [view addSubview:label(@"Output channels", NSMakeRect(12.0, 166.0, 126.0, 18.0))];
      [view addSubview:channels];

      auto* track = [[NSButton alloc] initWithFrame:NSMakeRect(12.0, 126.0, 300.0, 24.0)];
      track.buttonType = NSButtonTypeSwitch;
      track.title = @"Create initial vocal track";
      track.state = NSControlStateValueOn;
      [view addSubview:track];

      auto* bank = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(150.0, 26.0, 294.0, 26.0)
                                             pullsDown:NO];
      [bank addItemWithTitle:@"No Voicebank"];
      for (const auto& candidate : config.candidates) {
        const auto title = candidate.manifest.displayName + " / " +
                           candidate.manifest.id + " " + candidate.manifest.version;
        [bank addItemWithTitle:nsString(title)];
      }
      auto* proceduralSinger = [[NSPopUpButton alloc]
          initWithFrame:NSMakeRect(150.0, 58.0, 294.0, 26.0) pullsDown:NO];
      [proceduralSinger addItemWithTitle:@"No Procedural Singer"];
      for (const auto& option : config.proceduralSingers) {
        [proceduralSinger addItemWithTitle:nsString(option.label)];
      }
      if (!config.unavailableProceduralSingers.empty()) {
        [proceduralSinger.menu addItem:[NSMenuItem separatorItem]];
        for (const auto& option : config.unavailableProceduralSingers) {
          [proceduralSinger addItemWithTitle:nsString(option.label)];
          NSMenuItem* menuItem = [proceduralSinger itemAtIndex:
              proceduralSinger.numberOfItems - 1];
          menuItem.enabled = NO;
          menuItem.toolTip = nsString(option.detail);
        }
      }
      [view addSubview:label(@"Initial Voicebank", NSMakeRect(12.0, 30.0, 126.0, 18.0))];
      [view addSubview:bank];
      [view addSubview:label(@"Initial Procedural Singer", NSMakeRect(12.0, 62.0, 136.0, 18.0))];
      [view addSubview:proceduralSinger];
      auto* toggleTarget = [[SEAMVoicebankToggleTarget alloc] init];
      toggleTarget.voicebank = bank;
      toggleTarget.proceduralSinger = proceduralSinger;
      track.target = toggleTarget;
      track.action = @selector(toggleVoicebank:);
      bank.target = toggleTarget;
      bank.action = @selector(selectInitialSinger:);
      proceduralSinger.target = toggleTarget;
      proceduralSinger.action = @selector(selectInitialSinger:);

      auto* alert = [[NSAlert alloc] init];
      alert.messageText = @"Create New Project";
      alert.informativeText = @"Choose the project identity, timing, output, and an optional initial singer.";
      alert.accessoryView = view;
      [alert addButtonWithTitle:@"Choose Location…"];
      [alert addButtonWithTitle:@"Cancel"];
      if ([alert runModal] != NSAlertFirstButtonReturn) {
        return std::optional<authoring::NewProjectRequest>{};
      }

      const auto nameValue = utf8(name.stringValue);
      double tempoValue = 0.0;
      try {
        tempoValue = std::stod(utf8(tempo.stringValue));
      } catch (...) {
        return core::failure<std::optional<authoring::NewProjectRequest>>(
            core::ErrorCode::InvalidArgument, "Tempo must be a number");
      }
      const auto numeratorValue = unsignedValue(numerator.titleOfSelectedItem);
      const auto denominatorValue = unsignedValue(denominator.titleOfSelectedItem);
      const auto sampleRateValue = unsignedValue(sampleRate.titleOfSelectedItem);
      const auto channelValue = unsignedValue(channels.titleOfSelectedItem);
      if (!numeratorValue.has_value() || !denominatorValue.has_value() ||
          !sampleRateValue.has_value() || !channelValue.has_value()) {
        return core::failure<std::optional<authoring::NewProjectRequest>>(
            core::ErrorCode::InvalidArgument,
            "New Project numeric selections are invalid");
      }
      model.setName(nameValue);
      model.setTempoBpm(tempoValue);
      model.setMeter(static_cast<std::uint8_t>(*numeratorValue),
                     static_cast<std::uint8_t>(*denominatorValue));
      model.setSampleRate(*sampleRateValue);
      model.setOutputChannels(static_cast<std::uint8_t>(*channelValue));
      const auto createTrack = track.state == NSControlStateValueOn;
      model.setCreateInitialVocalTrack(createTrack);
      if (createTrack && bank.indexOfSelectedItem > 0) {
        const auto selected = model.selectVoicebank(
            static_cast<std::size_t>(bank.indexOfSelectedItem - 1));
        if (!selected) return core::Result<std::optional<authoring::NewProjectRequest>>{
            selected.error()};
      }

      auto* save = [NSSavePanel savePanel];
      save.title = @"Choose Project Location";
      const auto fileName = suggestedProjectFileName(
          nameValue.empty() ? std::string_view{"Untitled"}
                            : std::string_view{nameValue});
      save.nameFieldStringValue = nsString(fileName);
      save.canCreateDirectories = YES;
      save.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"seam"]];
      if (!config.initialDirectory.empty()) {
        save.directoryURL = [NSURL fileURLWithPath:
            [NSString stringWithUTF8String:config.initialDirectory.string().c_str()]
                                      isDirectory:YES];
      }
      if ([save runModal] != NSModalResponseOK || save.URL == nil) {
        return std::optional<authoring::NewProjectRequest>{};
      }
      const char* path = save.URL.fileSystemRepresentation;
      if (path == nullptr) {
        return core::failure<std::optional<authoring::NewProjectRequest>>(
            core::ErrorCode::IoError, "Unable to resolve project location");
      }
      model.setProjectPath(std::filesystem::path{path});
      auto submitted = model.submit();
      if (!submitted) {
        return core::Result<std::optional<authoring::NewProjectRequest>>{
            submitted.error()};
      }
      auto request = std::move(submitted).value();
      if (createTrack && proceduralSinger.indexOfSelectedItem > 0) {
        const auto index = static_cast<std::size_t>(proceduralSinger.indexOfSelectedItem - 1);
        if (index >= config.proceduralSingers.size()) {
          return core::failure<std::optional<authoring::NewProjectRequest>>(
              core::ErrorCode::InvalidArgument,
              "Selected procedural singer is not one of the offered options");
        }
        request.initialProceduralSinger = config.proceduralSingers[index].reference;
      }
      return std::optional<authoring::NewProjectRequest>{std::move(request)};
    }
  }
};

}

std::unique_ptr<INativeNewProjectDialog> createNativeNewProjectDialog() {
  return std::make_unique<AppKitNativeNewProjectDialog>();
}

std::unique_ptr<INativeInterchangeReviewDialog>
createNativeInterchangeReviewDialog() {
  return std::make_unique<AppKitNativeInterchangeReviewDialog>();
}

void presentNativeInterchangeFailure(std::string_view title, std::string_view detail) {
  NSString* titleText = conversionString(title);
  NSString* detailText = conversionString(detail);
  void (^present)(void) = ^{
    auto* alert = [[NSAlert alloc] init];
    alert.messageText = titleText;
    alert.informativeText = detailText;
    alert.alertStyle = NSAlertStyleWarning;
    [alert addButtonWithTitle:@"OK"];
    NSWindow* owner = NSApp.keyWindow;
    NSResponder* responder = owner.firstResponder;
    [alert runModal];
    if (owner != nil && owner.visible) {
      [owner makeKeyWindow];
      if (responder != nil) [owner makeFirstResponder:responder];
    }
  };
  if ([NSThread isMainThread]) present();
  else dispatch_async(dispatch_get_main_queue(), present);
}

}

#endif
