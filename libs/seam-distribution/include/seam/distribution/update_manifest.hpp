#pragma once

#include "seam/distribution/trust_policy.hpp"
#include "seam/formats/json_value.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace seam::distribution {

struct UpdateRange final {
  std::uint64_t min{0};
  std::uint64_t max{0};

  friend bool operator==(const UpdateRange&, const UpdateRange&) = default;
};

struct UpdatePackage final {
  std::string fileName;
  std::string url;
  std::uint64_t size{0};
  std::string sha256;

  friend bool operator==(const UpdatePackage&, const UpdatePackage&) = default;
};

struct UpdateRecoveryAuthorization final {
  std::string purpose;
  UpdateSignature signature;
  formats::JsonValue::Object additionalFields;

  friend bool operator==(const UpdateRecoveryAuthorization&,
                         const UpdateRecoveryAuthorization&) = default;
};

struct UpdateManifest final {
  std::int64_t schemaVersion{0};
  std::string purpose;
  std::string channel;
  std::string manifestId;
  std::uint64_t manifestEpoch{0};
  std::string platform;
  std::string targetBuild;
  std::string targetVersion;
  std::string minimumVersion;
  std::string issuedAt;
  std::string expiresAt;
  std::map<std::string, UpdateRange, std::less<>> readRanges;
  std::map<std::string, UpdateRange, std::less<>> writeRanges;
  std::string downgradePolicy;
  UpdatePackage package;
  std::string releaseNotesSha256;
  std::optional<UpdateRecoveryAuthorization> recoveryAuthorization;
  UpdateSignature signature;

  friend bool operator==(const UpdateManifest&, const UpdateManifest&) = default;
};

struct UpdateManifestVerificationOptions final {
  std::string_view expectedPlatform;
  std::string_view installedVersion;
  std::optional<std::uint64_t> highestAcceptedManifestEpoch;
  std::span<const std::byte> packageBytes{};
  std::string_view now;
  const Ed25519PublicKey* trustedRoot{nullptr};
};

struct SealedUpdatePackage final {
  std::string fileName;
  std::string relativePath;
  std::uint64_t size{0};
  std::string sha256;
  std::uint64_t device{0};
  std::uint64_t inode{0};
};

struct SealedInstallerHandoff final {
  std::int64_t schemaVersion{0};
  std::string purpose;
  std::string candidateId;
  std::string manifestSha256;
  SealedUpdatePackage package;
  bool requiresExplicitUserAction{false};
  bool requiresInstallerRevalidation{false};
  std::string createdAt;
};

[[nodiscard]] core::Result<UpdateManifest> parseUpdateManifest(
    std::string_view json);
[[nodiscard]] std::string serializeUpdateManifest(
    const UpdateManifest& manifest);
[[nodiscard]] std::string canonicalUpdateManifestPayload(
    const UpdateManifest& manifest);
[[nodiscard]] std::string updateManifestIdentity(
    const UpdateManifest& manifest);
[[nodiscard]] core::Result<void> validateUpdateManifest(
    const UpdateManifest& manifest, const UpdateTrustPolicy& policy,
    std::string_view now = {});
[[nodiscard]] core::Result<void> verifyUpdateManifest(
    const UpdateManifest& manifest, const UpdateTrustPolicy& policy,
    const UpdateManifestVerificationOptions& options = {});
[[nodiscard]] core::Result<SealedInstallerHandoff> stageVerifiedUpdatePackage(
    const std::filesystem::path& packagePath, const UpdateManifest& manifest,
    const std::filesystem::path& stagingRoot);
[[nodiscard]] std::string serializeSealedInstallerHandoff(
    const SealedInstallerHandoff& handoff);
[[nodiscard]] core::Result<SealedInstallerHandoff> parseSealedInstallerHandoff(
    std::string_view json);
[[nodiscard]] core::Result<void> verifySealedInstallerHandoff(
    const SealedInstallerHandoff& handoff, const UpdateManifest& manifest,
    const std::filesystem::path& stagingRoot);

}
