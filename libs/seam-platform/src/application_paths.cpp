#include "seam/platform/application_paths.hpp"

#include <cstdlib>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#endif

namespace seam::platform {

core::Result<std::filesystem::path> applicationSupportDirectory() {
#ifdef _WIN32
  PWSTR widePath = nullptr;
  const auto status = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr,
                                            &widePath);
  if (FAILED(status) || widePath == nullptr) {
    if (widePath != nullptr) CoTaskMemFree(widePath);
    return core::failure<std::filesystem::path>(
        core::ErrorCode::IoError,
        "Unable to locate the Windows application-data directory");
  }
  std::filesystem::path result{widePath};
  CoTaskMemFree(widePath);
  return result / "ProjectSEAM";
#elif defined(__APPLE__)
  const auto* home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::IoError,
        "HOME is not available for the macOS application-support directory");
  }
  return std::filesystem::path{home} / "Library" / "Application Support" /
         "ProjectSEAM";
#else
  if (const auto* data = std::getenv("XDG_DATA_HOME");
      data != nullptr && *data != '\0') {
    return std::filesystem::path{data} / "project-seam";
  }
  const auto* home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::IoError,
        "Neither XDG_DATA_HOME nor HOME is available");
  }
  return std::filesystem::path{home} / ".local" / "share" / "project-seam";
#endif
}

}  // namespace seam::platform
