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
    if ([defaults objectForKey:@"shellEnabled"] != nil)
      preferences.shellEnabled = [defaults boolForKey:@"shellEnabled"];
    if ([defaults boolForKey:@"highContrast"]) preferences.contrast = Contrast::High;
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
    [defaults setBool:preferences.shellEnabled forKey:@"shellEnabled"];
    [defaults setBool:preferences.contrast == Contrast::High forKey:@"highContrast"];
    [defaults setBool:preferences.reduceMotion forKey:@"reduceMotion"];
  }
}

}  // namespace seam::native_ui::design
