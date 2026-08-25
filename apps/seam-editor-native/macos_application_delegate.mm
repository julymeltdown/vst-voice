#include "macos_application_delegate.hpp"

#if defined(__APPLE__)

#import <AppKit/AppKit.h>

#include <filesystem>
#include <memory>
#include <utility>

@interface SeamApplicationDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, assign) seam::standalone::NativeEditorApp* owner;
@end

namespace seam::standalone::macos {

struct ApplicationDelegateHandle::Impl final {
  SeamApplicationDelegate* __strong delegate{nil};
  id __strong previous{nil};
  id __strong activity{nil};
};

ApplicationDelegateHandle::ApplicationDelegateHandle(
    std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

ApplicationDelegateHandle::~ApplicationDelegateHandle() {
  if (impl_ == nullptr) return;
  NSApplication* application = [NSApplication sharedApplication];
  if (application.delegate == impl_->delegate) {
    application.delegate = impl_->previous;
  }
  if (impl_->activity != nil) {
    [[NSProcessInfo processInfo] endActivity:impl_->activity];
    impl_->activity = nil;
  }
  impl_->delegate.owner = nullptr;
}

ApplicationDelegateHandle::ApplicationDelegateHandle(
    ApplicationDelegateHandle&& other) noexcept = default;

ApplicationDelegateHandle& ApplicationDelegateHandle::operator=(
    ApplicationDelegateHandle&& other) noexcept = default;

core::Result<std::unique_ptr<ApplicationDelegateHandle>>
ApplicationDelegateHandle::install(
    ::seam::standalone::NativeEditorApp& app) {
  NSApplication* application = [NSApplication sharedApplication];
  auto impl = std::make_unique<Impl>();
  impl->previous = application.delegate;
  impl->activity = [[NSProcessInfo processInfo]
      beginActivityWithOptions:(NSActivityUserInitiated |
                                NSActivityLatencyCritical)
                         reason:@"Project SEAM audio and export"];
  impl->delegate = [[SeamApplicationDelegate alloc] init];
  impl->delegate.owner = &app;
  application.delegate = impl->delegate;
  return std::unique_ptr<ApplicationDelegateHandle>{
      new ApplicationDelegateHandle(std::move(impl))};
}

}

@implementation SeamApplicationDelegate

- (BOOL)applicationShouldHandleReopen:(NSApplication*)application
                    hasVisibleWindows:(BOOL)hasVisibleWindows {
  if (!hasVisibleWindows) {
    for (NSWindow* window in application.windows) {
      if (window != nil) {
        [window makeKeyAndOrderFront:nil];
        break;
      }
    }
  }
  [application activateIgnoringOtherApps:YES];
  return YES;
}

- (void)application:(NSApplication*)application
          openFiles:(NSArray<NSString*>*)filenames {
  BOOL success = YES;
  if (_owner != nullptr) {
    for (NSString* filename in filenames) {
      const char* path = filename.fileSystemRepresentation;
      if (path == nullptr) continue;
      const auto projectPath = std::filesystem::path{path};
      if (projectPath.extension() != ".seam") continue;
      if (!_owner->openProject(projectPath)) {
        success = NO;
      }
    }
  }
  [application replyToOpenOrPrint:
                    success ? NSApplicationDelegateReplySuccess
                            : NSApplicationDelegateReplyFailure];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:
    (NSApplication*)application {
  (void)application;
  if (_owner == nullptr) return NSTerminateNow;
  return _owner->requestClose() ? NSTerminateNow : NSTerminateCancel;
}

@end

#endif
