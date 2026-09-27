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
    else
      preferences.reduceMotion =
          NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
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
    [defaults setBool:preferences.reduceMotion forKey:@"reduceMotion"];
  }
}

bool systemIncreaseContrast() {
  @autoreleasepool {
    return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldIncreaseContrast;
  }
}

}  // namespace seam::native_ui::design
