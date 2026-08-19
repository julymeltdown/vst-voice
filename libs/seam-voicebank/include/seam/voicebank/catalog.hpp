#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace seam::voicebank {

enum class VoicebankRootKind {
  Installed,
  Development,
};

enum class VoicebankTrust {
  TrustedInstalled,
  UntrustedInstalled,
  DevelopmentFixture,
};

enum class VoicebankResolveStatus {
  Resolved,
  Missing,
  VersionMismatch,
  ContentHashMissing,
  ContentMismatch,
  Untrusted,
  InvalidReference,
};

struct VoicebankSearchRoot final {
  std::filesystem::path path;
  VoicebankRootKind kind{VoicebankRootKind::Installed};
};

struct VoicebankCandidate final {
  Manifest manifest;
  std::filesystem::path bankRoot;
  std::string contentHash;
  VoicebankTrust trust{VoicebankTrust::UntrustedInstalled};
  std::string packageDigest;
  std::string signerKeyId;
};

struct VoicebankResolveOptions final {
  bool requireTrustedInstalled{true};
  bool allowDevelopmentFixtures{true};
};

struct VoicebankResolution final {
  VoicebankResolveStatus status{VoicebankResolveStatus::Missing};
  std::optional<VoicebankCandidate> candidate;
  std::vector<std::string> availableVersions;
  std::string expectedContentHash;
  std::vector<std::string> actualContentHashes;
  std::string diagnostic;

  [[nodiscard]] bool resolved() const noexcept {
    return status == VoicebankResolveStatus::Resolved && candidate.has_value();
  }
};

class VoicebankCatalog final {
public:
  [[nodiscard]] core::Result<std::vector<VoicebankCandidate>> scan(
      const std::vector<VoicebankSearchRoot>& roots) const;

  [[nodiscard]] VoicebankResolution resolve(
      const domain::VoicebankReference& reference,
      const std::vector<VoicebankCandidate>& candidates,
      const VoicebankResolveOptions& options = {}) const;
};

[[nodiscard]] std::vector<VoicebankSearchRoot> defaultVoicebankSearchRoots();
[[nodiscard]] std::string_view voicebankTrustName(VoicebankTrust trust) noexcept;
[[nodiscard]] std::string_view voicebankResolveStatusName(
    VoicebankResolveStatus status) noexcept;

}  // namespace seam::voicebank
