#include "seam/native_ui/sample_bank_package.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/distribution/installer.hpp"
#include "seam/voicebank/catalog.hpp"
#include "seam/voicebank/content_identity.hpp"
#include "seam/voicebank/manifest_json.hpp"

#include <algorithm>
#include <system_error>

namespace seam::native_ui {
namespace {

std::filesystem::path comparablePath(const std::filesystem::path& path) {
  std::error_code error;
  auto canonical = std::filesystem::weakly_canonical(path, error);
  return error ? path.lexically_normal() : canonical.lexically_normal();
}

bool isDigest(std::string_view value) noexcept {
  return value.size() == 64U &&
      std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
      });
}

}  // namespace

core::Result<distribution::SeambankPackageInfo> packPublishedSampleBank(
    const voicebank_production::PublishedSampleCandidate& candidate,
    const std::filesystem::path& packagePath,
    const distribution::SigningKeyPair& signingKey) {
  using Output = distribution::SeambankPackageInfo;
  // A published engineering candidate is deliberately not release-eligible: signing a package is
  // what makes reviewed material installable, and it never turns publication into a release
  // qualification. What is required here is the exact reviewed identity, not a release flag.
  if (candidate.root.empty() || !isDigest(candidate.contentSha256) ||
      !isDigest(candidate.manifestSha256))
    return core::failure<Output>(core::ErrorCode::InvalidState,
        "Publish a complete engineering candidate before packing it for installation");
  if (packagePath.empty() || !packagePath.is_absolute() || packagePath.filename().empty() ||
      packagePath.extension() != ".seambank")
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Choose an absolute package destination ending in .seambank");
  std::error_code statusError;
  const auto status = std::filesystem::symlink_status(packagePath, statusError);
  if (!statusError && std::filesystem::exists(status))
    return core::failure<Output>(core::ErrorCode::Conflict,
        "A package already exists there. Studio never replaces a signed package; choose a new name",
        packagePath.string());
  if (!std::filesystem::is_directory(packagePath.parent_path(), statusError))
    return core::failure<Output>(core::ErrorCode::NotFound,
        "The package destination folder does not exist", packagePath.parent_path().string());
  if (comparablePath(packagePath) == comparablePath(candidate.root))
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "The signed package cannot replace the published candidate directory");

  // Re-read the exact manifest the candidate recorded. A candidate directory that changed after
  // publication must never be signed under the reviewed identity.
  const auto manifestBytes = core::readFileBytesLimited(candidate.root / "manifest.json",
                                                       64ULL * 1024ULL * 1024ULL);
  if (!manifestBytes) return core::Result<Output>{manifestBytes.error()};
  if (core::sha256Hex(manifestBytes.value()) != candidate.manifestSha256)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "The published candidate manifest changed after publication; publish a new candidate before packing",
        candidate.root.string());
  const auto manifestText = std::string{reinterpret_cast<const char*>(manifestBytes.value().data()),
                                       manifestBytes.value().size()};
  voicebank::ManifestJsonCodec codec;
  auto decoded = codec.decode(manifestText);
  if (!decoded) return core::Result<Output>{decoded.error()};
  auto reencoded = codec.encode(decoded.value());
  if (!reencoded) return core::Result<Output>{reencoded.error()};
  if (core::sha256Hex(reencoded.value()) != candidate.manifestSha256)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "The published candidate manifest is not a canonical manifest; refusing to sign it");
  const auto contentHash = voicebank::computeVoicebankContentHash(decoded.value(), candidate.root);
  if (!contentHash) return core::Result<Output>{contentHash.error()};
  if (contentHash.value() != candidate.contentSha256)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "The published candidate audio changed after publication; publish a new candidate before packing",
        candidate.root.string());

  auto packed = distribution::packSeambank(candidate.root, packagePath, signingKey);
  if (!packed) return core::Result<Output>{packed.error()};
  if (!packed.value().signatureValid || !packed.value().signerTrusted)
    return core::failure<Output>(core::ErrorCode::Internal,
        "The signed package did not verify against the key that signed it");
  return packed;
}

core::Result<SampleBankInstallation> installSignedSampleBank(
    const std::filesystem::path& packagePath, const std::filesystem::path& installRoot,
    const std::vector<distribution::Ed25519PublicKey>& trustedPublicKeys,
    std::string_view expectedContentHash, std::stop_token stop) {
  using Output = SampleBankInstallation;
  if (packagePath.empty() || installRoot.empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Installing a signed bank needs its package and an installation folder");
  if (!isDigest(expectedContentHash) || trustedPublicKeys.empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Installing a signed bank needs the reviewed content hash and an explicitly trusted key");
  auto installed = distribution::installSeambank(packagePath, installRoot,
      distribution::InstallSeambankOptions{
          .verification = {.trustedPublicKeys = trustedPublicKeys, .requireTrustedSigner = true},
          .replaceExisting = false,
          .expectedContentHash = std::string{expectedContentHash}}, stop);
  if (!installed) return core::Result<Output>{installed.error()};

  // The catalog the song editor uses is the only authority on whether a bank is a trusted
  // installation, so the installation is re-scanned from its root instead of trusted from the
  // installer's return value alone.
  const voicebank::VoicebankCatalog catalog;
  auto scanned = catalog.scan({voicebank::VoicebankSearchRoot{
      .path = installRoot, .kind = voicebank::VoicebankRootKind::Installed}});
  if (!scanned) return core::Result<Output>{scanned.error()};
  const auto installDirectory = comparablePath(installed.value().installDirectory);
  const auto match = std::find_if(scanned.value().begin(), scanned.value().end(),
      [&](const voicebank::VoicebankCandidate& candidate) {
        return comparablePath(candidate.bankRoot) == installDirectory;
      });
  if (match == scanned.value().end())
    return core::failure<Output>(core::ErrorCode::Conflict,
        "The installed bank is not in the bank folder after installation",
        installRoot.string());
  if (match->trust != voicebank::VoicebankTrust::TrustedInstalled)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "The installed bank did not resolve as a trusted installation", installRoot.string());
  if (match->contentHash != expectedContentHash)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "The installed bank is not the reviewed candidate content; install the exact published package",
        match->contentHash);
  return Output{.voicebankId = match->manifest.id,
                .voicebankVersion = match->manifest.version,
                .contentHash = match->contentHash,
                .packageDigest = installed.value().packageDigest,
                .signerKeyId = installed.value().signerKeyId,
                .installDirectory = installed.value().installDirectory,
                .durabilityConfirmed = installed.value().durabilityConfirmed,
                .diagnostic = installed.value().diagnostic};
}

}  // namespace seam::native_ui
