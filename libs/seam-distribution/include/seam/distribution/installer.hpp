#pragma once

#include "seam/core/result.hpp"
#include "seam/distribution/seambank.hpp"

#include <filesystem>
#include <string>

namespace seam::distribution {

struct InstallSeambankOptions final {
  VerifySeambankOptions verification{};
  bool replaceExisting{false};
};

struct InstalledSeambank final {
  std::string voicebankId;
  std::string voicebankVersion;
  std::string packageDigest;
  std::string signerKeyId;
  std::filesystem::path installDirectory;
};

[[nodiscard]] core::Result<InstalledSeambank> installSeambank(
    const std::filesystem::path& packagePath,
    const std::filesystem::path& installRoot,
    const InstallSeambankOptions& options);

}  // namespace seam::distribution
