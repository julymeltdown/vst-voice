#include "seam/authoring/voicebank_installer_service.hpp"

#include "seam/core/sha256.hpp"
#include "seam/voicebank/manifest_json.hpp"

#include <algorithm>
#include <array>
#include <set>

namespace seam::authoring {
namespace {

void addField(core::Sha256& hash, std::string_view value) noexcept {
  const auto size = static_cast<std::uint64_t>(value.size());
  std::array<std::byte, sizeof(size)> encoded{};
  for (std::size_t index = 0; index < encoded.size(); ++index) {
    encoded[index] = static_cast<std::byte>((size >> (index * 8U)) & 0xffU);
  }
  hash.update(encoded);
  hash.update(value);
}

std::string digestHex(const std::array<std::byte, 32>& bytes) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 2U);
  for (const auto value : bytes) {
    const auto byte = std::to_integer<unsigned int>(value);
    result.push_back(digits[(byte >> 4U) & 0x0fU]);
    result.push_back(digits[byte & 0x0fU]);
  }
  return result;
}

core::Result<std::string> packageContentHash(
    const distribution::SeambankPackageInfo& package) {
  auto synthesisManifest = package.manifest;
  synthesisManifest.displayName = "project-seam-synthesis-identity";
  synthesisManifest.characterId.clear();
  synthesisManifest.characterVersion.clear();
  voicebank::ManifestJsonCodec codec;
  auto encoded = codec.encode(synthesisManifest);
  if (!encoded) return core::Result<std::string>{encoded.error()};

  std::set<std::string> audioPaths;
  for (const auto& unit : package.manifest.units) {
    audioPaths.insert(unit.audioPath.generic_string());
  }
  core::Sha256 hash;
  addField(hash, "project-seam-voicebank-content-v1");
  addField(hash, encoded.value());
  for (const auto& path : audioPaths) {
    const auto entry = std::find_if(
        package.entries.begin(), package.entries.end(),
        [&path](const distribution::SeambankEntry& value) {
          return value.path == path;
        });
    if (entry == package.entries.end()) {
      return core::failure<std::string>(
          core::ErrorCode::NotFound,
          "Signed seambank does not contain a referenced unit WAV", path);
    }
    addField(hash, path);
    addField(hash, digestHex(entry->sha256));
  }
  return hash.hexDigest();
}

}  // namespace

VoicebankInstallerService::VoicebankInstallerService(
    VoicebankSession& session,
    std::filesystem::path installRoot,
    std::optional<distribution::Ed25519PublicKey> developmentTrustRoot)
    : session_(session),
      installRoot_(std::move(installRoot)),
      developmentTrustRoot_(std::move(developmentTrustRoot)) {}

core::Result<VoicebankInstallResult> VoicebankInstallerService::install(
    const VoicebankInstallRequest& request) {
  if (request.packagePath.empty() || installRoot_.empty()) {
    return core::failure<VoicebankInstallResult>(
        core::ErrorCode::InvalidArgument,
        "Voicebank installation requires package and installation paths");
  }
  auto trustedKeys = request.trustedPublicKeys;
  if (request.useDevelopmentTrustRoot) {
    if (!developmentTrustRoot_.has_value()) {
      return core::failure<VoicebankInstallResult>(
          core::ErrorCode::InvalidState,
          "Development trust root was requested but is not configured");
    }
    trustedKeys.push_back(*developmentTrustRoot_);
  }
  if (trustedKeys.empty()) {
    return core::failure<VoicebankInstallResult>(
        core::ErrorCode::InvalidArgument,
        "Voicebank installation requires an explicitly trusted public key");
  }

  distribution::VerifySeambankOptions verification{
      .trustedPublicKeys = trustedKeys,
      .requireTrustedSigner = true,
  };
  auto verified = distribution::verifySeambank(request.packagePath,
                                                verification);
  if (!verified) return core::Result<VoicebankInstallResult>{verified.error()};
  auto incomingHash = packageContentHash(verified.value());
  if (!incomingHash) {
    return core::Result<VoicebankInstallResult>{incomingHash.error()};
  }

  auto refreshed = session_.refresh();
  if (!refreshed) return core::Result<VoicebankInstallResult>{refreshed.error()};
  const auto before = session_.candidates();
  const auto existing = std::find_if(
      before.begin(), before.end(), [&verified](const auto& candidate) {
        return candidate.manifest.id == verified.value().manifest.id &&
               candidate.manifest.version == verified.value().manifest.version;
      });
  if (existing != before.end()) {
    if (existing->contentHash == incomingHash.value()) {
      return VoicebankInstallResult{
          .voicebankId = existing->manifest.id,
          .voicebankVersion = existing->manifest.version,
          .contentHash = existing->contentHash,
          .packageDigest = existing->packageDigest,
          .signerKeyId = existing->signerKeyId,
          .installDirectory = existing->bankRoot,
          .candidate = *existing,
      };
    }
    return core::failure<VoicebankInstallResult>(
        core::ErrorCode::Conflict,
        "Voicebank ID and version are already bound to different synthesis content; install a new version");
  }

  auto installed = distribution::installSeambank(
      request.packagePath, installRoot_,
      distribution::InstallSeambankOptions{
          .verification = verification,
          .replaceExisting = false,
      });
  if (!installed) return core::Result<VoicebankInstallResult>{installed.error()};

  auto rootAdded = session_.addSearchRoot(voicebank::VoicebankSearchRoot{
      .path = installRoot_,
      .kind = voicebank::VoicebankRootKind::Installed,
  });
  if (!rootAdded) return core::Result<VoicebankInstallResult>{rootAdded.error()};
  const auto after = session_.candidates();
  const auto candidate = std::find_if(
      after.begin(), after.end(), [&installed, &incomingHash](const auto& value) {
        return value.manifest.id == installed.value().voicebankId &&
               value.manifest.version == installed.value().voicebankVersion &&
               value.contentHash == incomingHash.value();
      });
  if (candidate == after.end() ||
      candidate->trust != voicebank::VoicebankTrust::TrustedInstalled) {
    return core::failure<VoicebankInstallResult>(
        core::ErrorCode::Conflict,
        "Installed voicebank receipt did not resolve as a trusted installation");
  }

  return VoicebankInstallResult{
      .voicebankId = candidate->manifest.id,
      .voicebankVersion = candidate->manifest.version,
      .contentHash = candidate->contentHash,
      .packageDigest = candidate->packageDigest,
      .signerKeyId = candidate->signerKeyId,
      .installDirectory = candidate->bankRoot,
      .candidate = *candidate,
  };
}

}  // namespace seam::authoring
