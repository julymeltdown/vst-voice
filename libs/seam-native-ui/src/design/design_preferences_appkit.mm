#include "seam/native_ui/design/sing_shell.hpp"

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
  }
}

}  // namespace seam::native_ui::design
