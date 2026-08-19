#pragma once

#include "seam/authoring/voicebank_session.hpp"
#include "seam/distribution/installer.hpp"
#include "seam/distribution/signing.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace seam::authoring {

enum class ExistingVoicebankDecision { Reject, Replace };

struct VoicebankInstallRequest final {
  std::filesystem::path packagePath;
  std::vector<distribution::Ed25519PublicKey> trustedPublicKeys;
  bool useDevelopmentTrustRoot{false};
  ExistingVoicebankDecision existingDecision{
      ExistingVoicebankDecision::Reject};
};

struct VoicebankInstallResult final {
  std::string voicebankId;
  std::string voicebankVersion;
  std::string contentHash;
  std::string packageDigest;
  std::string signerKeyId;
  std::filesystem::path installDirectory;
  voicebank::VoicebankCandidate candidate;
};

class VoicebankInstallerService final {
public:
  VoicebankInstallerService(
      VoicebankSession& session,
      std::filesystem::path installRoot,
      std::optional<distribution::Ed25519PublicKey> developmentTrustRoot =
          std::nullopt);

  [[nodiscard]] core::Result<VoicebankInstallResult> install(
      const VoicebankInstallRequest& request);

  [[nodiscard]] const std::filesystem::path& installRoot() const noexcept {
    return installRoot_;
  }

private:
  VoicebankSession& session_;
  std::filesystem::path installRoot_;
  std::optional<distribution::Ed25519PublicKey> developmentTrustRoot_;
};

}  // namespace seam::authoring
