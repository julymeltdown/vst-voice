#include "test_framework.hpp"

#include "seam/platform/platform_capabilities.hpp"

#include <string>

TEST_CASE("platform capability contract names the native composition and audio adapters") {
  constexpr auto capabilities = seam::platform::currentPlatformCapabilities();
  CHECK(!capabilities.operatingSystem.empty());
  CHECK(!capabilities.nativeWindowBackend.empty());
  CHECK(!capabilities.textInputBackend.empty());
  CHECK(!capabilities.audioOutputBackend.empty());
  CHECK(!capabilities.audioInputBackend.empty());
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
  CHECK(capabilities.nativeWindowImplemented);
  CHECK(capabilities.compositionInputImplemented);
  CHECK(capabilities.physicalOutputImplemented);
  CHECK(capabilities.physicalInputImplemented);
  CHECK(capabilities.maximumOutputChannels == 8U);
#endif
}
