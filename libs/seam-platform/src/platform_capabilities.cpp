#include "seam/platform/platform_capabilities.hpp"

namespace seam::platform {
static_assert(currentPlatformCapabilities().maximumOutputChannels <= 8U);
}  // namespace seam::platform
