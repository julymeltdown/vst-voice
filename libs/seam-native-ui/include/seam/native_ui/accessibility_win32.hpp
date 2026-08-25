#pragma once

#if defined(_WIN32)

#include <cstdint>
#include <memory>

namespace seam::native_ui {

class INativeWindowClient;

class Win32AccessibilityBridge final {
public:
  Win32AccessibilityBridge(void* nativeWindow, INativeWindowClient& client);
  ~Win32AccessibilityBridge();

  Win32AccessibilityBridge(const Win32AccessibilityBridge&) = delete;
  Win32AccessibilityBridge& operator=(const Win32AccessibilityBridge&) = delete;

  [[nodiscard]] std::intptr_t handleGetObject(
      std::uintptr_t wParam, std::intptr_t lParam) noexcept;
  void invalidate() noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}

#endif
