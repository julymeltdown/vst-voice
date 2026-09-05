#include "crash_capture_internal.hpp"

namespace seam::platform {
namespace detail {

core::Result<std::unique_ptr<CrashCaptureBackend>>
installCrashCaptureBackend(const std::filesystem::path&) {
  return core::failure<std::unique_ptr<CrashCaptureBackend>>(
      core::ErrorCode::Unsupported,
      "Crash-safe capture is unavailable on this platform");
}

}

std::string_view crashCaptureBackendName() noexcept { return "unavailable"; }

}
