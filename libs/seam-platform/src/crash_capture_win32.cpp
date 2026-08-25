#include "seam/platform/crash_capture.hpp"

namespace seam::platform {

std::string_view crashCaptureBackendName() noexcept { return "windows-terminate"; }

}
