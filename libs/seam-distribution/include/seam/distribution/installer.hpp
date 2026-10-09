#pragma once

#include "seam/core/result.hpp"
#include "seam/distribution/seambank.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>

namespace seam::distribution {

// Points at which an installation can be interrupted. Before the commit, a failure at any stage
// (including cancellation or a simulated full disk) leaves the previously installed version and the
// install root exactly as they were. AfterCommitBeforeSync reports a committed installation whose
// directory entry may not yet be durable.
enum class InstallStage { Verified, EntryStaged, StagedVerified, BeforeCommit, AfterCommitBeforeSync };

struct InstallSeambankOptions final {
  VerifySeambankOptions verification{};
  bool replaceExisting{false};
  // Install only the exact package bytes an earlier step reported, not merely a trusted package.
  std::optional<std::string> expectedPackageDigest{};
  // The staged voicebank content hash must equal this before anything is published.
  std::optional<std::string> expectedContentHash{};
  // Diagnostic interruption hook. It can only fail an installation; it cannot bypass verification.
  std::function<core::Result<void>(InstallStage)> faultInjector{};
};

struct InstalledSeambank final {
  std::string voicebankId;
  std::string voicebankVersion;
  std::string packageDigest;
  std::string signerKeyId;
  std::filesystem::path installDirectory;
  std::string contentHash;
  bool replacedExisting{false};
  // False only for a committed installation whose directory entry may not be durable yet.
  bool durabilityConfirmed{true};
  std::string diagnostic;
};

// Verifies, stages every signed entry privately, re-checks the staged bytes, writes the receipt and
// only then publishes atomically. Replacement swaps the new version in and removes the old one only
// after the new one is durable. Nothing this call did not create is removed.
[[nodiscard]] core::Result<InstalledSeambank> installSeambank(
    const std::filesystem::path& packagePath,
    const std::filesystem::path& installRoot,
    const InstallSeambankOptions& options,
    std::stop_token stop = {});

}  // namespace seam::distribution
