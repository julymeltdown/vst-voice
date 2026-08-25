#include "platform_host.hpp"

#if defined(__APPLE__)

#import <AppKit/AppKit.h>

#include <algorithm>
#include <array>
#include <fstream>

namespace seam::clap_host {

struct HostWindow::Impl final {
  NSApplication* __strong application{nil};
  NSWindow* __strong window{nil};
  NSView* __strong content{nil};
};

HostWindow::HostWindow() : impl_(std::make_unique<Impl>()) {}

HostWindow::~HostWindow() { destroy(); }

bool HostWindow::create(std::uint32_t width, std::uint32_t height) {
  if (impl_ == nullptr || width == 0U || height == 0U || available()) {
    return false;
  }
  impl_->application = [NSApplication sharedApplication];
  if (impl_->application == nil) return false;
  static_cast<void>([impl_->application
      setActivationPolicy:NSApplicationActivationPolicyRegular]);
  impl_->window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0.0, 0.0, width, height)
                styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                           NSWindowStyleMaskResizable)
                  backing:NSBackingStoreBuffered
                    defer:NO];
  if (impl_->window == nil) {
    destroy();
    return false;
  }
  impl_->window.releasedWhenClosed = NO;
  impl_->window.title = @"Project SEAM CLAP host";
  impl_->content = [[NSView alloc]
      initWithFrame:NSMakeRect(0.0, 0.0, width, height)];
  impl_->content.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  impl_->window.contentView = impl_->content;
  [impl_->window center];
  [impl_->window makeKeyAndOrderFront:nil];
  [impl_->application activateIgnoringOtherApps:YES];
  return available();
}

bool HostWindow::attach(clap_window_t& parent) const noexcept {
  if (!available()) return false;
  parent = clap_window_t{};
  parent.api = CLAP_WINDOW_API_COCOA;
  parent.cocoa = (__bridge void*)impl_->content;
  return true;
}

bool HostWindow::pump() noexcept {
  if (!available()) return false;
  @autoreleasepool {
    while (true) {
      NSEvent* event = [impl_->application
          nextEventMatchingMask:NSEventMaskAny
                      untilDate:[NSDate dateWithTimeIntervalSinceNow:0.0]
                         inMode:NSDefaultRunLoopMode
                        dequeue:YES];
      if (event == nil) break;
      [impl_->application sendEvent:event];
    }
    [impl_->application updateWindows];
  }
  return available();
}

bool HostWindow::capture(const std::filesystem::path& path) const {
  if (!available()) return false;
  [impl_->window displayIfNeeded];
  const auto bounds = impl_->content.bounds;
  NSBitmapImageRep* representation =
      [impl_->content bitmapImageRepForCachingDisplayInRect:bounds];
  if (representation == nil) return false;
  [impl_->content cacheDisplayInRect:bounds toBitmapImageRep:representation];
  const auto* pixels = representation.bitmapData;
  if (pixels == nullptr || representation.samplesPerPixel < 3) return false;
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << "P6\n" << representation.pixelsWide << ' '
         << representation.pixelsHigh << "\n255\n";
  const auto bytesPerPixel = representation.bitsPerPixel / 8U;
  for (NSInteger y = 0; y < representation.pixelsHigh; ++y) {
    const auto sourceRow = representation.pixelsHigh - 1 - y;
    const auto* row = pixels + sourceRow * representation.bytesPerRow;
    for (NSInteger x = 0; x < representation.pixelsWide; ++x) {
      const auto* pixel = row + x * bytesPerPixel;
      const std::array<char, 3> rgb{static_cast<char>(pixel[0]),
                                    static_cast<char>(pixel[1]),
                                    static_cast<char>(pixel[2])};
      output.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
    }
  }
  return static_cast<bool>(output);
}

void HostWindow::destroy() noexcept {
  if (impl_ == nullptr) return;
  if (impl_->window != nil) {
    [impl_->window orderOut:nil];
    [impl_->window close];
  }
  impl_->content = nil;
  impl_->window = nil;
  impl_->application = nil;
}

bool HostWindow::available() const noexcept {
  return impl_ != nullptr && impl_->application != nil &&
         impl_->window != nil && impl_->content != nil;
}

const char* HostWindow::api() const noexcept { return CLAP_WINDOW_API_COCOA; }

}

#endif
