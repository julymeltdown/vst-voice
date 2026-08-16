#pragma once

#include "seam/core/result.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace seam::distribution {

struct SeambankLimits final {
  std::uint32_t maximumEntries{100'000U};
  std::uint64_t maximumEntryBytes{512ULL * 1024ULL * 1024ULL};
  std::uint64_t maximumPayloadBytes{2ULL * 1024ULL * 1024ULL * 1024ULL};
  std::uint64_t maximumArchiveBytes{3ULL * 1024ULL * 1024ULL * 1024ULL};
  std::size_t maximumPathBytes{1024U};
};

struct SeambankEntry final {
  std::string path;
  std::uint64_t payloadOffset{0U};
  std::uint64_t payloadSize{0U};
  std::array<std::byte, 32> sha256{};

  friend bool operator==(const SeambankEntry&, const SeambankEntry&) = default;
};

struct SeambankPackageInfo final {
  static constexpr std::uint32_t kFormatVersion = 1U;

  std::filesystem::path packagePath;
  std::uint32_t formatVersion{kFormatVersion};
  std::string packageDigest;
  Ed25519PublicKey signerPublicKey{};
  std::string signerKeyId;
  Ed25519Signature signature{};
  voicebank::Manifest manifest;
  std::vector<SeambankEntry> entries;
  std::uint64_t payloadBytes{0U};
  bool signatureValid{false};
  bool signerTrusted{false};
};

struct PackSeambankOptions final {
  SeambankLimits limits{};
};

struct VerifySeambankOptions final {
  SeambankLimits limits{};
  std::vector<Ed25519PublicKey> trustedPublicKeys;
  bool requireTrustedSigner{false};
};

[[nodiscard]] core::Result<SeambankPackageInfo> packSeambank(
    const std::filesystem::path& sourceDirectory,
    const std::filesystem::path& outputPackage,
    const SigningKeyPair& signingKey,
    const PackSeambankOptions& options = {});

[[nodiscard]] core::Result<SeambankPackageInfo> verifySeambank(
    const std::filesystem::path& packagePath,
    const VerifySeambankOptions& options = {});

[[nodiscard]] core::Result<std::vector<std::byte>> readSeambankEntry(
    const std::filesystem::path& packagePath, std::string_view entryPath,
    const VerifySeambankOptions& options = {});

[[nodiscard]] bool isSafeSeambankPath(std::string_view path) noexcept;
[[nodiscard]] bool isAllowedSeambankAsset(std::string_view path) noexcept;

}  // namespace seam::distribution
