#include "seam/platform/application_menu.hpp"

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>

@interface SEAMMenuTarget : NSObject
@property(nonatomic, assign) seam::platform::IApplicationCommandDispatcher* dispatcher;
- (void)newProject:(id)sender;
- (void)openProject:(id)sender;
- (void)recoverLatestAutosave:(id)sender;
- (void)recoverAutosave:(id)sender;
- (void)openRecentProject:(id)sender;
- (void)saveProject:(id)sender;
- (void)saveProjectAs:(id)sender;
- (void)importAudio:(id)sender;
- (void)installVoicebank:(id)sender;
- (void)relinkVoicebank:(id)sender;
- (void)relinkBackingAudio:(id)sender;
- (void)openAudioSettings:(id)sender;
- (void)selectVoicebank:(id)sender;
- (void)openDocumentation:(id)sender;
- (void)exportAudio:(id)sender;
- (void)exportSet:(id)sender;
- (void)quitApplication:(id)sender;
- (void)undoAction:(id)sender;
- (void)redoAction:(id)sender;
- (void)togglePlayback:(id)sender;
- (void)stopPlayback:(id)sender;
- (void)toggleLoop:(id)sender;
@end

@implementation SEAMMenuTarget
- (void)send:(seam::platform::ApplicationCommand)command {
  if (_dispatcher != nullptr) static_cast<void>(_dispatcher->dispatch(command));
}
- (void)newProject:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::NewProject]; }
- (void)openProject:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::OpenProject]; }
- (void)recoverLatestAutosave:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::RecoverLatestAutosave]; }
- (void)recoverAutosave:(id)sender {
  if (_dispatcher == nullptr || ![sender isKindOfClass:[NSMenuItem class]]) return;
  NSMenuItem* menuItem = static_cast<NSMenuItem*>(sender);
  NSString* represented = menuItem.representedObject;
  if (represented == nil) return;
  const char* path = represented.fileSystemRepresentation;
  if (path != nullptr) {
    static_cast<void>(_dispatcher->recoverAutosave(std::filesystem::path{path}));
  }
}
- (void)openRecentProject:(id)sender {
  if (_dispatcher == nullptr || ![sender isKindOfClass:[NSMenuItem class]]) return;
  NSMenuItem* menuItem = static_cast<NSMenuItem*>(sender);
  NSString* represented = menuItem.representedObject;
  if (represented == nil) return;
  const char* path = represented.fileSystemRepresentation;
  if (path != nullptr) {
    static_cast<void>(_dispatcher->openRecentProject(std::filesystem::path{path}));
  }
}
- (void)saveProject:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::SaveProject]; }
- (void)saveProjectAs:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::SaveProjectAs]; }
- (void)importAudio:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::ImportAudio]; }
- (void)installVoicebank:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::InstallVoicebank]; }
- (void)relinkVoicebank:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::RelinkVoicebank]; }
- (void)relinkBackingAudio:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::RelinkBackingAudio]; }
- (void)openAudioSettings:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::OpenAudioSettings]; }
- (void)selectVoicebank:(id)sender {
  if (_dispatcher == nullptr || ![sender isKindOfClass:[NSMenuItem class]]) return;
  NSDictionary* value = static_cast<NSMenuItem*>(sender).representedObject;
  if (![value isKindOfClass:[NSDictionary class]]) return;
  NSString* identifier = value[@"id"];
  NSString* version = value[@"version"];
  NSString* contentHash = value[@"contentHash"];
  if (identifier == nil || version == nil || contentHash == nil) return;
  static_cast<void>(_dispatcher->selectVoicebank(
      identifier.UTF8String, version.UTF8String, contentHash.UTF8String));
}
- (void)exportAudio:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::ExportAudio]; }
- (void)exportSet:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::ExportSet]; }
- (void)quitApplication:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::Quit]; }
- (void)undoAction:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::Undo]; }
- (void)redoAction:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::Redo]; }
- (void)togglePlayback:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::TogglePlayback]; }
- (void)stopPlayback:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::StopPlayback]; }
- (void)toggleLoop:(id)sender { (void)sender; [self send:seam::platform::ApplicationCommand::ToggleLoop]; }
- (void)openDocumentation:(id)sender {
  if (_dispatcher == nullptr || ![sender isKindOfClass:[NSMenuItem class]]) return;
  NSString* identifier = static_cast<NSMenuItem*>(sender).representedObject;
  if (identifier != nil) static_cast<void>(_dispatcher->openDocumentation(
      identifier.UTF8String));
}
@end

namespace seam::platform {
namespace {

NSMenuItem* item(NSString* title, SEL action, NSString* key,
                 NSEventModifierFlags modifiers, id target) {
  auto* result = [[NSMenuItem alloc] initWithTitle:title action:action
                                     keyEquivalent:key];
  result.keyEquivalentModifierMask = modifiers;
  result.target = target;
  return result;
}

void addSubmenu(NSMenu* root, NSString* title, NSMenu* menu) {
  auto* holder = [[NSMenuItem alloc] initWithTitle:title action:nil
                                      keyEquivalent:@""];
  holder.submenu = menu;
  [root addItem:holder];
}

class AppKitApplicationMenu final : public IApplicationMenu {
public:
  core::Result<void> install(IApplicationCommandDispatcher& dispatcher) override {
    if (![NSThread isMainThread]) {
      return core::failure(core::ErrorCode::InvalidState,
                           "AppKit menus must be installed on the main thread");
    }
    uninstall();
    auto* application = [NSApplication sharedApplication];
    target_ = [[SEAMMenuTarget alloc] init];
    target_.dispatcher = &dispatcher;
    dispatcher_ = &dispatcher;
    previous_ = application.mainMenu;
    root_ = [[NSMenu alloc] initWithTitle:@"Project SEAM"];

    auto* app = [[NSMenu alloc] initWithTitle:@"Project SEAM"];
    [app addItem:item(@"Quit Project SEAM", @selector(quitApplication:), @"q",
                      NSEventModifierFlagCommand, target_)];
    addSubmenu(root_, @"Project SEAM", app);

    fileMenu_ = [[NSMenu alloc] initWithTitle:@"File"];
    [fileMenu_ addItem:item(@"New Project", @selector(newProject:), @"n",
                            NSEventModifierFlagCommand, target_)];
    [fileMenu_ addItem:item(@"Open…", @selector(openProject:), @"o",
                            NSEventModifierFlagCommand, target_)];
    recentHolder_ = [[NSMenuItem alloc] initWithTitle:@"Open Recent"
                                                action:nil keyEquivalent:@""];
    recentMenu_ = [[NSMenu alloc] initWithTitle:@"Open Recent"];
    recentHolder_.submenu = recentMenu_;
    [fileMenu_ addItem:recentHolder_];
    recoveryHolder_ = [[NSMenuItem alloc] initWithTitle:@"Recover Autosave"
                                                  action:nil keyEquivalent:@""];
    recoveryMenu_ = [[NSMenu alloc] initWithTitle:@"Recover Autosave"];
    recoveryHolder_.submenu = recoveryMenu_;
    [fileMenu_ addItem:recoveryHolder_];
    [fileMenu_ addItem:[NSMenuItem separatorItem]];
    [fileMenu_ addItem:item(@"Save", @selector(saveProject:), @"s",
                            NSEventModifierFlagCommand, target_)];
    [fileMenu_ addItem:item(@"Save As…", @selector(saveProjectAs:), @"s",
                            NSEventModifierFlagCommand |
                                NSEventModifierFlagShift,
                            target_)];
    [fileMenu_ addItem:[NSMenuItem separatorItem]];
    [fileMenu_ addItem:item(@"Import Backing Audio…", @selector(importAudio:), @"",
                            0, target_)];
    [fileMenu_ addItem:item(@"Relink Backing Audio…", @selector(relinkBackingAudio:), @"",
                            0, target_)];
    [fileMenu_ addItem:item(@"Audio Settings…", @selector(openAudioSettings:), @"",
                            0, target_)];
    [fileMenu_ addItem:item(@"Install Voicebank…", @selector(installVoicebank:), @"",
                            0, target_)];
    [fileMenu_ addItem:item(@"Export Audio…", @selector(exportAudio:), @"e",
                            0, target_)];
    [fileMenu_ addItem:item(@"Export Set…", @selector(exportSet:), @"e",
                            NSEventModifierFlagCommand, target_)];
    addSubmenu(root_, @"File", fileMenu_);

    voicebankMenu_ = [[NSMenu alloc] initWithTitle:@"Voicebank"];
    addSubmenu(root_, @"Voicebank", voicebankMenu_);

    auto* edit = [[NSMenu alloc] initWithTitle:@"Edit"];
    [edit addItem:item(@"Undo", @selector(undoAction:), @"z",
                       NSEventModifierFlagCommand, target_)];
    [edit addItem:item(@"Redo", @selector(redoAction:), @"z",
                       NSEventModifierFlagCommand | NSEventModifierFlagShift,
                       target_)];
    addSubmenu(root_, @"Edit", edit);

    auto* transport = [[NSMenu alloc] initWithTitle:@"Transport"];
    [transport addItem:item(@"Play / Pause", @selector(togglePlayback:), @" ",
                            0, target_)];
    [transport addItem:item(@"Stop", @selector(stopPlayback:), @".",
                            NSEventModifierFlagCommand, target_)];
    [transport addItem:item(@"Toggle Loop", @selector(toggleLoop:), @"l",
                            0, target_)];
    addSubmenu(root_, @"Transport", transport);
    addSubmenu(root_, @"View", [[NSMenu alloc] initWithTitle:@"View"]);
    helpMenu_ = [[NSMenu alloc] initWithTitle:@"Help"];
    addSubmenu(root_, @"Help", helpMenu_);
    application.mainMenu = root_;
    refresh();
    return core::success();
  }

  void refresh() noexcept override {
    if (recentMenu_ == nil || dispatcher_ == nullptr) return;
    [recentMenu_ removeAllItems];
    const auto entries = dispatcher_->recentProjects();
    if (entries.empty()) {
      auto* empty = [[NSMenuItem alloc] initWithTitle:@"No Recent Projects"
                                                action:nil keyEquivalent:@""];
      empty.enabled = NO;
      [recentMenu_ addItem:empty];
    } else {
      for (const auto& entry : entries) {
        NSString* title = [NSString stringWithUTF8String:entry.displayName.c_str()];
        if (title == nil) title = @"Project";
        if (entry.missing) title = [title stringByAppendingString:@" — Missing"];
        auto* menuItem = item(title, @selector(openRecentProject:), @"", 0,
                              target_);
        menuItem.enabled = !entry.missing;
        menuItem.representedObject = [NSString stringWithUTF8String:
            entry.path.string().c_str()];
        [recentMenu_ addItem:menuItem];
      }
    }

    if (voicebankMenu_ != nil) {
      [voicebankMenu_ removeAllItems];
      [voicebankMenu_ addItem:item(@"Relink Voicebank Search Folder…",
                                   @selector(relinkVoicebank:), @"", 0,
                                   target_)];
      [voicebankMenu_ addItem:[NSMenuItem separatorItem]];
      const auto banks = dispatcher_->voicebanks();
      if (banks.empty()) {
        auto* empty = [[NSMenuItem alloc] initWithTitle:@"No Voicebanks Found"
                                                  action:nil keyEquivalent:@""];
        empty.enabled = NO;
        [voicebankMenu_ addItem:empty];
      } else {
        for (const auto& bank : banks) {
          NSString* display = [NSString stringWithUTF8String:bank.displayName.c_str()];
          NSString* version = [NSString stringWithUTF8String:bank.version.c_str()];
          NSString* trust = [NSString stringWithUTF8String:bank.trustLabel.c_str()];
          NSString* title = [NSString stringWithFormat:@"%@ — %@ [%@]",
              display == nil ? @"Voicebank" : display,
              version == nil ? @"?" : version,
              trust == nil ? @"unknown" : trust];
          auto* menuItem = item(title, @selector(selectVoicebank:), @"", 0, target_);
          menuItem.enabled = bank.selectable;
          menuItem.state = bank.selected ? NSControlStateValueOn : NSControlStateValueOff;
          menuItem.representedObject = @{
            @"id": [NSString stringWithUTF8String:bank.id.c_str()],
            @"version": [NSString stringWithUTF8String:bank.version.c_str()],
            @"contentHash": [NSString stringWithUTF8String:bank.contentHash.c_str()]
          };
          [voicebankMenu_ addItem:menuItem];
        }
      }
    }

    if (recoveryMenu_ != nil) {
      [recoveryMenu_ removeAllItems];
      const auto recovery = dispatcher_->recoveryItems();
      if (recovery.empty()) {
        auto* empty = [[NSMenuItem alloc] initWithTitle:@"No Recoverable Autosaves"
                                                  action:nil keyEquivalent:@""];
        empty.enabled = NO;
        [recoveryMenu_ addItem:empty];
      } else {
        for (const auto& entry : recovery) {
          NSString* title = [NSString stringWithUTF8String:entry.displayName.c_str()];
          if (title == nil) title = @"Autosave";
          auto* menuItem = item(title, @selector(recoverAutosave:), @"", 0,
                                target_);
          menuItem.representedObject = [NSString stringWithUTF8String:
              entry.metadataPath.string().c_str()];
          [recoveryMenu_ addItem:menuItem];
        }
      }
    }

    if (helpMenu_ != nil) {
      [helpMenu_ removeAllItems];
      const auto documents = dispatcher_->documentation();
      if (documents.empty()) {
        auto* empty = [[NSMenuItem alloc] initWithTitle:@"Documentation unavailable"
                                                  action:nil keyEquivalent:@""];
        empty.enabled = NO;
        [helpMenu_ addItem:empty];
      } else {
        for (const auto& document : documents) {
          NSString* title = [NSString stringWithUTF8String:document.displayName.c_str()];
          NSString* identifier = [NSString stringWithUTF8String:document.id.c_str()];
          auto* menuItem = item(title == nil ? @"Documentation" : title,
                                @selector(openDocumentation:), @"", 0, target_);
          menuItem.representedObject = identifier;
          [helpMenu_ addItem:menuItem];
        }
      }
    }
  }

  void uninstall() noexcept override {
    auto* application = [NSApplication sharedApplication];
    if (root_ != nil && application.mainMenu == root_) {
      application.mainMenu = previous_;
    }
    if (target_ != nil) target_.dispatcher = nullptr;
    dispatcher_ = nullptr;
    recoveryMenu_ = nil;
    recoveryHolder_ = nil;
    recentMenu_ = nil;
    recentHolder_ = nil;
    fileMenu_ = nil;
    helpMenu_ = nil;
    voicebankMenu_ = nil;
    root_ = nil;
    previous_ = nil;
    target_ = nil;
  }

private:
  SEAMMenuTarget* target_{nil};
  IApplicationCommandDispatcher* dispatcher_{nullptr};
  NSMenu* root_{nil};
  NSMenu* previous_{nil};
  NSMenu* fileMenu_{nil};
  NSMenu* helpMenu_{nil};
  NSMenu* voicebankMenu_{nil};
  NSMenuItem* recentHolder_{nil};
  NSMenu* recentMenu_{nil};
  NSMenuItem* recoveryHolder_{nil};
  NSMenu* recoveryMenu_{nil};
};

class AppKitUnsavedPrompt final : public IUnsavedChangesPrompt {
public:
  core::Result<UnsavedDecision> choose(std::string_view projectName) override {
    if (![NSThread isMainThread]) {
      return core::failure<UnsavedDecision>(
          core::ErrorCode::InvalidState,
          "AppKit unsaved-project prompts must run on the main thread");
    }
    @autoreleasepool {
      auto* alert = [[NSAlert alloc] init];
      alert.messageText = @"Save changes before closing?";
      NSString* name = [[NSString alloc] initWithBytes:projectName.data()
                                                  length:projectName.size()
                                                encoding:NSUTF8StringEncoding];
      alert.informativeText = [NSString stringWithFormat:
          @"Changes to “%@” will be lost if you do not save them.",
          name == nil ? @"Untitled" : name];
      [alert addButtonWithTitle:@"Save"];
      [alert addButtonWithTitle:@"Cancel"];
      [alert addButtonWithTitle:@"Discard"];
      const auto response = [alert runModal];
      if (response == NSAlertFirstButtonReturn) return UnsavedDecision::Save;
      if (response == NSAlertThirdButtonReturn) return UnsavedDecision::Discard;
      return UnsavedDecision::Cancel;
    }
  }
};

}  // namespace

std::unique_ptr<IApplicationMenu> createNativeApplicationMenu() {
  return std::make_unique<AppKitApplicationMenu>();
}

std::unique_ptr<IUnsavedChangesPrompt> createNativeUnsavedChangesPrompt() {
  return std::make_unique<AppKitUnsavedPrompt>();
}

core::Result<void> openDocumentationPath(const std::filesystem::path& path) {
  if (path.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Documentation path is empty");
  }
  NSString* value = [NSString stringWithUTF8String:path.string().c_str()];
  NSURL* url = value == nil ? nil : [NSURL fileURLWithPath:value];
  if (url == nil || ![[NSWorkspace sharedWorkspace] openURL:url]) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to open offline documentation", path.string());
  }
  return core::success();
}

core::Result<void> openExternalPath(const std::filesystem::path& path) {
  if (path.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "External path is empty");
  }
  NSString* value = [NSString stringWithUTF8String:path.string().c_str()];
  NSURL* url = value == nil ? nil : [NSURL fileURLWithPath:value];
  if (url == nil || ![[NSWorkspace sharedWorkspace] openURL:url]) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to open external path", path.string());
  }
  return core::success();
}

core::Result<void> copyTextToClipboard(std::string_view text) {
  NSString* value = [NSString stringWithUTF8String:
                                     std::string{text}.c_str()];
  if (value == nil) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Diagnostic text is not valid UTF-8");
  }
  NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
  [pasteboard clearContents];
  if (![pasteboard setString:value forType:NSPasteboardTypeString]) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to copy diagnostic text");
  }
  return core::success();
}

core::Result<bool> requestEulaAcceptance(const std::filesystem::path& path) {
  if (path.empty()) {
    return core::failure<bool>(core::ErrorCode::InvalidArgument,
                               "EULA path is empty");
  }
  if (![NSThread isMainThread]) {
    return core::failure<bool>(core::ErrorCode::InvalidState,
                               "EULA acceptance prompt must run on the main thread");
  }
  @autoreleasepool {
    NSString* pathText = [NSString stringWithUTF8String:path.string().c_str()];
    NSURL* url = pathText == nil ? nil : [NSURL fileURLWithPath:pathText];
    if (url == nil || ![[NSWorkspace sharedWorkspace] openURL:url]) {
      return core::failure<bool>(core::ErrorCode::IoError,
                                 "Unable to open the bundled EULA",
                                 path.string());
    }
    auto* alert = [[NSAlert alloc] init];
    alert.messageText = @"Project SEAM External Beta EULA";
    alert.informativeText =
        @"Review the bundled EULA that just opened, then choose Accept to continue. Acceptance is stored locally as the document version, SHA-256 digest, and UTC timestamp.";
    [alert addButtonWithTitle:@"Accept"];
    [alert addButtonWithTitle:@"Decline"];
    return [alert runModal] == NSAlertFirstButtonReturn;
  }
}

}  // namespace seam::platform
#endif
