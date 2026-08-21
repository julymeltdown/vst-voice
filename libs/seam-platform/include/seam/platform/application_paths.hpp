#pragma once

#include "seam/core/result.hpp"

#include <filesystem>

namespace seam::platform {

struct ApplicationPaths final {
  std::filesystem::path installRoot;
  std::filesystem::path resourcesRoot;
  std::filesystem::path userDataRoot;
  std::filesystem::path cacheRoot;
  std::filesystem::path settingsRoot;
  std::filesystem::path projectsRoot;
  std::filesystem::path voicebankRoot;
  std::filesystem::path voicebankTrustRoot;
  std::filesystem::path autosaveRoot;
  std::filesystem::path recoveryRoot;
  std::filesystem::path logsRoot;
  std::filesystem::path manualsRoot;
  std::filesystem::path crashReportsRoot;
  std::filesystem::path updateStagingRoot;

  [[nodiscard]] static ApplicationPaths forTestRoot(
      std::filesystem::path root);
  [[nodiscard]] static core::Result<ApplicationPaths> resolve(
      std::filesystem::path executablePath = {});
};

[[nodiscard]] core::Result<ApplicationPaths> applicationPaths(
    std::filesystem::path executablePath = {});
[[nodiscard]] core::Result<std::filesystem::path> applicationSupportDirectory();

}  // namespace seam::platform
