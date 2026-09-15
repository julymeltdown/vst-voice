#pragma once

#include "seam/core/result.hpp"

#include <filesystem>

namespace seam::platform {

// Locate the loaded binary containing a stable static-data address. Callers
// must supply an anchor in their SEAM surface, not in the DAW or another DLL.
[[nodiscard]] core::Result<std::filesystem::path> loadedModulePath(const void* address);

struct ApplicationPaths final {
  std::filesystem::path installRoot;
  std::filesystem::path resourcesRoot;
  std::filesystem::path userDataRoot;
  std::filesystem::path cacheRoot;
  std::filesystem::path settingsRoot;
  std::filesystem::path projectsRoot;
  std::filesystem::path voicebankRoot;
  std::filesystem::path voicebankTrustRoot;
  // Where installed procedural singers are catalogued and where their review decisions are kept.
  // Both sit beside the sample-bank roots because they are the same kind of user-owned resource store.
  std::filesystem::path proceduralSingerRoot;
  std::filesystem::path proceduralReviewStorePath;
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
