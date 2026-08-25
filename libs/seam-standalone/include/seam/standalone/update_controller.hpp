#pragma once

#include "seam/distribution/update_manifest.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace seam::standalone {

enum class UpdateCheckStatus { NoUpdate, Available, Blocked };

struct UpdateControllerConfig final {
  std::filesystem::path statePath;
  std::filesystem::path stagingRoot;
  std::string expectedPlatform;
  std::string installedVersion;
  std::string verificationTime;
  std::optional<distribution::Ed25519PublicKey> trustedRoot;
};

struct UpdateCheckResult final {
  UpdateCheckStatus status{UpdateCheckStatus::Blocked};
  std::optional<distribution::UpdateManifest> manifest;
  std::string diagnostic;
};

class UpdateController final {
public:
  [[nodiscard]] static core::Result<std::unique_ptr<UpdateController>> create(
      UpdateControllerConfig config);

  [[nodiscard]] core::Result<UpdateCheckResult> check(
      const std::filesystem::path& policyPath,
      const std::filesystem::path& manifestPath) const;
  [[nodiscard]] core::Result<distribution::SealedInstallerHandoff> stage(
      const std::filesystem::path& policyPath,
      const std::filesystem::path& manifestPath,
      const std::filesystem::path& packagePath);
  [[nodiscard]] const UpdateControllerConfig& config() const noexcept {
    return config_;
  }

private:
  explicit UpdateController(UpdateControllerConfig config)
      : config_(std::move(config)) {}

  [[nodiscard]] core::Result<std::optional<std::uint64_t>> highestEpoch() const;
  [[nodiscard]] core::Result<void> accept(
      const distribution::UpdateManifest& manifest) const;

  UpdateControllerConfig config_;
};

}
