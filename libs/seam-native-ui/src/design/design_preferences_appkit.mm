#include "seam/native_ui/design/sing_shell.hpp"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

namespace seam::native_ui::design {
namespace {

// A named suite so the standalone app and the plug-in inside any host share one choice, and the
// plug-in never writes into the host application's own defaults.
NSUserDefaults* suite() {
  static NSUserDefaults* defaults =
      [[NSUserDefaults alloc] initWithSuiteName:@"com.project-seam.design"];
  return defaults;
}

}  // namespace

DesignPreferences loadDesignPreferences() {
  DesignPreferences preferences;
  @autoreleasepool {
    NSUserDefaults* defaults = suite();
    if (defaults == nil) return preferences;
    if (NSString* mode = [defaults stringForKey:@"mode"]; mode != nil)
      preferences.mode = parseDesignMode(mode.UTF8String, preferences.mode);
    // Contrast, like motion, has two sources: an explicit choice made in the app, and otherwise the
    // system's Increase Contrast. The older boolean key only ever recorded an explicit High.
    NSString* contrast = [defaults stringForKey:@"contrast"];
    if (contrast != nil && [contrast isEqualToString:@"high"]) {
      preferences.contrast = Contrast::High;
    } else if (contrast != nil && [contrast isEqualToString:@"standard"]) {
      preferences.contrast = Contrast::Standard;
    } else if ([defaults boolForKey:@"highContrast"]) {
      preferences.contrast = Contrast::High;
    } else {
      preferences.contrastFollowsSystem = true;
      preferences.contrast = systemIncreaseContrast() ? Contrast::High : Contrast::Standard;
    }
    // Two sources, one setting: an explicit preference when the user has set one, and otherwise the
    // system's own Reduce Motion, which is what the editor already honors. The stored value wins so a
    // user can turn motion back on for this app alone without changing the system setting.
    if ([defaults objectForKey:@"reduceMotion"] != nil)
      preferences.reduceMotion = [defaults boolForKey:@"reduceMotion"];
    else {
      preferences.reduceMotionFollowsSystem = true;
      preferences.reduceMotion =
          NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
    }
    // Language, like contrast: an explicit choice made in the app, or else the system's own
    // preferred language, which the shell reads in when it offers it.
    if (NSString* language = [defaults stringForKey:@"language"]; language != nil && language.length > 0) {
      preferences.language = std::string{shellLanguageFor(language.UTF8String)};
    } else {
      preferences.languageFollowsSystem = true;
      preferences.language = std::string{shellLanguageFor(systemPreferredLanguage())};
    }
  }
  return preferences;
}

void saveDesignPreferences(const DesignPreferences& preferences) {
  @autoreleasepool {
    NSUserDefaults* defaults = suite();
    if (defaults == nil) return;
    const auto mode = designModeName(preferences.mode);
    [defaults setObject:[[NSString alloc] initWithBytes:mode.data()
                                                 length:mode.size()
                                               encoding:NSUTF8StringEncoding]
                 forKey:@"mode"];
    // Following the system stores nothing, so a later change to Increase Contrast still applies.
    [defaults removeObjectForKey:@"highContrast"];
    if (preferences.contrastFollowsSystem)
      [defaults removeObjectForKey:@"contrast"];
    else
      [defaults setObject:(preferences.contrast == Contrast::High ? @"high" : @"standard")
                   forKey:@"contrast"];
    if (preferences.reduceMotionFollowsSystem)
      [defaults removeObjectForKey:@"reduceMotion"];
    else
      [defaults setBool:preferences.reduceMotion forKey:@"reduceMotion"];
    // Following the system stores nothing, so the system's language keeps applying.
    if (preferences.languageFollowsSystem)
      [defaults removeObjectForKey:@"language"];
    else
      [defaults setObject:[[NSString alloc] initWithBytes:preferences.language.data()
                                                   length:preferences.language.size()
                                                 encoding:NSUTF8StringEncoding]
                   forKey:@"language"];
  }
}

bool systemIncreaseContrast() {
  @autoreleasepool {
    return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldIncreaseContrast;
  }
}

bool systemReduceMotion() {
  @autoreleasepool {
    return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
  }
}

std::string systemPreferredLanguage() {
  @autoreleasepool {
    NSString* first = NSLocale.preferredLanguages.firstObject;
    return first != nil ? std::string{first.UTF8String} : std::string{};
  }
}

std::shared_ptr<void> observeSystemDisplayOptions(std::function<void()> onChange) {
  if (!onChange) return {};
  @autoreleasepool {
    NSNotificationCenter* center = NSWorkspace.sharedWorkspace.notificationCenter;
    // No queue: the block runs on the posting thread, which for System Settings is the main thread.
    id observer = [center addObserverForName:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
                                      object:nil
                                       queue:nil
                                  usingBlock:^(NSNotification*) { onChange(); }];
    return std::shared_ptr<void>{nullptr, [center, observer](void*) {
                                   [center removeObserver:observer];
                                 }};
  }
}

void postSystemDisplayOptionsChanged() {
  @autoreleasepool {
    [NSWorkspace.sharedWorkspace.notificationCenter
        postNotificationName:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
                      object:nil];
  }
}

}  // namespace seam::native_ui::design
