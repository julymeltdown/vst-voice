#pragma once

#include <cstdint>
#include <string_view>

namespace seam::platform {

struct PlatformCapabilities final {
  std::string_view operatingSystem;
  std::string_view nativeWindowBackend;
  std::string_view textInputBackend;
  std::string_view audioOutputBackend;
  std::string_view audioInputBackend;
  std::uint8_t maximumOutputChannels{0U};
  bool nativeWindowImplemented{false};
  bool compositionInputImplemented{false};
  bool physicalOutputImplemented{false};
  bool physicalInputImplemented{false};
};

[[nodiscard]] constexpr PlatformCapabilities currentPlatformCapabilities() noexcept {
#if defined(_WIN32)
  return PlatformCapabilities{
      .operatingSystem = "Windows",
      .nativeWindowBackend = "Win32",
      .textInputBackend = "Win32 EDIT with TSF text services",
      .audioOutputBackend = "WASAPI",
      .audioInputBackend = "WASAPI Capture",
      .maximumOutputChannels = 8U,
      .nativeWindowImplemented = true,
      .compositionInputImplemented = true,
      .physicalOutputImplemented = true,
      .physicalInputImplemented = true,
  };
#elif defined(__APPLE__)
  return PlatformCapabilities{
      .operatingSystem = "macOS",
      .nativeWindowBackend = "AppKit",
      .textInputBackend = "NSTextInputClient",
      .audioOutputBackend = "CoreAudio AudioUnit",
      .audioInputBackend = "CoreAudio HAL",
      .maximumOutputChannels = 8U,
      .nativeWindowImplemented = true,
      .compositionInputImplemented = true,
      .physicalOutputImplemented = true,
      .physicalInputImplemented = true,
  };
#elif defined(__linux__)
  return PlatformCapabilities{
      .operatingSystem = "Linux",
      .nativeWindowBackend = "X11",
      .textInputBackend = "XIM/XIC",
      .audioOutputBackend = "PulseAudio Simple",
      .audioInputBackend = "PulseAudio Simple Capture",
      .maximumOutputChannels = 8U,
      .nativeWindowImplemented = true,
      .compositionInputImplemented = true,
      .physicalOutputImplemented = true,
      .physicalInputImplemented = true,
  };
#else
  return PlatformCapabilities{
      .operatingSystem = "Unknown",
      .nativeWindowBackend = "Unavailable",
      .textInputBackend = "Unavailable",
      .audioOutputBackend = "Unavailable",
      .audioInputBackend = "Unavailable",
  };
#endif
}

}  // namespace seam::platform
