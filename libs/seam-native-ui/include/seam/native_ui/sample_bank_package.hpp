#pragma once

#include "seam/core/result.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/voicebank_production/candidate_publication.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace seam::native_ui {

struct SampleBankInstallation final {
  std::string voicebankId;
  std::string voicebankVersion;
  // The installed content identity the catalog computed, which must equal the published candidate's
  // content hash. A mismatch means the package is not the reviewed material and is refused.
  std::string contentHash;
  std::string packageDigest;
  std::string signerKeyId;
  std::filesystem::path installDirectory;
};

// Packs an already published engineering candidate into a signed .seambank. The manifest inside the
// candidate is decoded, re-encoded and compared with the digest recorded at publication, so a
// package is never signed from material that no longer matches what reviewers saw. Publication does
// not sign or distribute; this step is what makes the reviewed material installable.
[[nodiscard]] core::Result<distribution::SeambankPackageInfo> packPublishedSampleBank(
    const voicebank_production::PublishedSampleCandidate& candidate,
    const std::filesystem::path& packagePath,
    const distribution::SigningKeyPair& signingKey);

// Installs a signed bank and proves it is the reviewed content: the installation must re-scan from
// its root as a trusted installation whose content hash is exactly the published one. A signature
// proves publisher authenticity only; it is never quality approval.
[[nodiscard]] core::Result<SampleBankInstallation> installSignedSampleBank(
    const std::filesystem::path& packagePath,
    const std::filesystem::path& installRoot,
    const std::vector<distribution::Ed25519PublicKey>& trustedPublicKeys,
    std::string_view expectedContentHash);

}  // namespace seam::native_ui
