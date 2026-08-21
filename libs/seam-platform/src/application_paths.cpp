#include "seam/platform/application_paths.hpp"

#include <cstdlib>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#endif

namespace seam::platform {

namespace {

std::filesystem::path absolutePath(std::filesystem::path path) {
  if (path.empty()) return {};
  std::error_code error;
  auto absolute = std::filesystem::absolute(std::move(path), error);
  return error ? std::filesystem::path{} : absolute.lexically_normal();
}

std::filesystem::path environmentPath(const char* name) {
  const auto* value = std::getenv(name);
  return value == nullptr || *value == '\0'
             ? std::filesystem::path{}
             : absolutePath(std::filesystem::path{value});
}

ApplicationPaths fromRoots(std::filesystem::path installRoot,
                           std::filesystem::path resourcesRoot,
                           std::filesystem::path userDataRoot,
                           std::filesystem::path cacheRoot,
                           std::filesystem::path settingsRoot,
                           std::filesystem::path stateRoot) {
  ApplicationPaths result{
      .installRoot = absolutePath(std::move(installRoot)),
      .resourcesRoot = absolutePath(std::move(resourcesRoot)),
      .userDataRoot = absolutePath(std::move(userDataRoot)),
      .cacheRoot = absolutePath(std::move(cacheRoot)),
      .settingsRoot = absolutePath(std::move(settingsRoot)),
      .projectsRoot = {},
      .voicebankRoot = {},
      .voicebankTrustRoot = {},
      .autosaveRoot = {},
      .recoveryRoot = {},
      .logsRoot = {},
      .manualsRoot = {},
      .crashReportsRoot = {},
      .updateStagingRoot = {},
  };
  const auto state = absolutePath(std::move(stateRoot));
  result.projectsRoot = result.userDataRoot / "Projects";
  result.voicebankRoot = result.userDataRoot / "Voicebanks";
  result.voicebankTrustRoot = result.userDataRoot / "Trust";
  result.autosaveRoot = state / "Autosaves";
  result.recoveryRoot = state / "Recovery";
  result.logsRoot = state / "Logs";
  result.crashReportsRoot = state / "CrashReports";
  result.manualsRoot = result.resourcesRoot.empty()
                           ? std::filesystem::path{}
                           : result.resourcesRoot / "Manual";
  result.updateStagingRoot = result.cacheRoot / "UpdateStaging";
  return result;
}

std::filesystem::path executableInstallRoot(
    const std::filesystem::path& executablePath) {
  const auto absolute = absolutePath(executablePath);
  if (absolute.empty()) return {};
  return absolute.parent_path();
}

std::filesystem::path executableResourcesRoot(
    const std::filesystem::path& executablePath,
    const std::filesystem::path& installRoot) {
  if (installRoot.empty()) return {};
#ifdef __APPLE__
  if (installRoot.filename() == "MacOS") {
    return installRoot.parent_path() / "Resources";
  }
#endif
  static_cast<void>(executablePath);
  return installRoot / "Resources";
}

}

ApplicationPaths ApplicationPaths::forTestRoot(std::filesystem::path root) {
  const auto absolute = absolutePath(std::move(root));
  return fromRoots(absolute / "Install", absolute / "Resources",
                   absolute / "Data", absolute / "Cache",
                   absolute / "Settings", absolute / "State");
}

core::Result<ApplicationPaths> ApplicationPaths::resolve(
    std::filesystem::path executablePath) {
#ifdef _WIN32
  const auto dataBase = environmentPath("LOCALAPPDATA");
  if (dataBase.empty()) {
    return core::failure<ApplicationPaths>(
        core::ErrorCode::IoError,
        "Unable to locate the Windows application-data directory");
  }
  const auto data = dataBase / "ProjectSEAM";
  const auto install = executableInstallRoot(executablePath);
  return fromRoots(install, executableResourcesRoot(executablePath, install),
                   data, data / "Cache", data / "Settings", data);
#elif defined(__APPLE__)
  const auto home = environmentPath("HOME");
  if (home.empty()) {
    return core::failure<ApplicationPaths>(
        core::ErrorCode::IoError,
        "HOME is not available for the macOS application-support directory");
  }
  const auto data = home / "Library" / "Application Support" / "ProjectSEAM";
  const auto cache = home / "Library" / "Caches" / "ProjectSEAM";
  const auto install = executableInstallRoot(executablePath);
  return fromRoots(install, executableResourcesRoot(executablePath, install),
                   data, cache, data / "Settings", data);
#else
  const auto home = environmentPath("HOME");
  const auto dataBase = environmentPath("XDG_DATA_HOME");
  const auto cacheBase = environmentPath("XDG_CACHE_HOME");
  const auto configBase = environmentPath("XDG_CONFIG_HOME");
  const auto stateBase = environmentPath("XDG_STATE_HOME");
  if (dataBase.empty() && home.empty()) {
    return core::failure<ApplicationPaths>(
        core::ErrorCode::IoError, "Neither XDG_DATA_HOME nor HOME is available");
  }
  const auto data = (dataBase.empty() ? home / ".local" / "share" : dataBase) /
                    "project-seam";
  const auto cache = (cacheBase.empty() ? home / ".cache" : cacheBase) /
                     "project-seam";
  const auto settings = (configBase.empty() ? home / ".config" : configBase) /
                        "project-seam";
  const auto state = (stateBase.empty() ? home / ".local" / "state" : stateBase) /
                     "project-seam";
  const auto install = executableInstallRoot(executablePath);
  return fromRoots(install, executableResourcesRoot(executablePath, install),
                   data, cache, settings, state);
#endif
}

core::Result<ApplicationPaths> applicationPaths(
    std::filesystem::path executablePath) {
  return ApplicationPaths::resolve(std::move(executablePath));
}

core::Result<std::filesystem::path> applicationSupportDirectory() {
  auto paths = ApplicationPaths::resolve();
  if (!paths) return core::Result<std::filesystem::path>{paths.error()};
  return paths.value().userDataRoot;
}

}  // namespace seam::platform
