#include "seam/voicebank/catalog.hpp"

#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank/content_identity.hpp"
#include "seam/voicebank/manifest_json.hpp"

#include <algorithm>
#include <cstdlib>
#include <system_error>

namespace seam::voicebank {
namespace {

constexpr std::uint64_t kMaximumReceiptBytes = 1024U * 1024U;
constexpr std::size_t kMaximumCandidates = 4096U;

bool isRealDirectory(const std::filesystem::path& path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  return !error && !std::filesystem::is_symlink(status) &&
         std::filesystem::is_directory(status);
}

bool isRegularManifest(const std::filesystem::path& path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  return !error && !std::filesystem::is_symlink(status) &&
         std::filesystem::is_regular_file(status);
}

struct Receipt final {
  bool present{false};
  bool signatureValid{false};
  bool signerTrusted{false};
  std::string voicebankId;
  std::string voicebankVersion;
  std::string contentHash;
  std::string packageDigest;
  std::string signerKeyId;
};

Receipt loadReceipt(const std::filesystem::path& bankRoot) {
  Receipt result;
  const auto path = bankRoot / "install-receipt.json";
  if (!isRegularManifest(path)) return result;
  auto text = core::readTextFileLimited(path, kMaximumReceiptBytes);
  if (!text) return result;
  auto parsed = formats::parseJson(text.value());
  if (!parsed || !parsed.value().isObject()) return result;
  result.present = true;
  const auto readString = [&parsed](std::string_view key) -> std::string {
    const auto* value = parsed.value().find(key);
    return value != nullptr && value->isString() ? value->asString() : std::string{};
  };
  const auto readBool = [&parsed](std::string_view key) -> bool {
    const auto* value = parsed.value().find(key);
    return value != nullptr && value->isBool() && value->asBool();
  };
  result.voicebankId = readString("voicebankId");
  result.voicebankVersion = readString("voicebankVersion");
  result.contentHash = readString("contentHash");
  result.packageDigest = readString("packageDigest");
  result.signerKeyId = readString("signerKeyId");
  result.signatureValid = readBool("signatureValid");
  result.signerTrusted = readBool("signerTrusted");
  return result;
}

void addManifestIfPresent(std::vector<std::filesystem::path>& output,
                          const std::filesystem::path& bankRoot) {
  const auto path = bankRoot / "manifest.json";
  if (isRegularManifest(path)) output.push_back(path);
}

std::vector<std::filesystem::path> manifestPathsFor(
    const VoicebankSearchRoot& root) {
  std::vector<std::filesystem::path> result;
  if (!isRealDirectory(root.path)) return result;
  addManifestIfPresent(result, root.path);
  if (!result.empty()) return result;

  std::error_code error;
  for (const auto& product : std::filesystem::directory_iterator(root.path, error)) {
    if (error || result.size() >= kMaximumCandidates) break;
    if (!isRealDirectory(product.path())) continue;
    addManifestIfPresent(result, product.path());
    if (!result.empty() && root.kind == VoicebankRootKind::Development) continue;
    std::error_code versionError;
    for (const auto& version :
         std::filesystem::directory_iterator(product.path(), versionError)) {
      if (versionError || result.size() >= kMaximumCandidates) break;
      if (!isRealDirectory(version.path())) continue;
      addManifestIfPresent(result, version.path());
    }
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

void addEnvironmentRoots(std::vector<VoicebankSearchRoot>& roots) {
  const auto* value = std::getenv("SEAM_VOICEBANK_PATH");
  if (value == nullptr || *value == '\0') return;
#ifdef _WIN32
  constexpr char separator = ';';
#else
  constexpr char separator = ':';
#endif
  std::string current;
  for (const char character : std::string_view{value}) {
    if (character == separator) {
      if (!current.empty()) {
        roots.push_back({std::filesystem::path{current}, VoicebankRootKind::Installed});
      }
      current.clear();
    } else {
      current.push_back(character);
    }
  }
  if (!current.empty()) {
    roots.push_back({std::filesystem::path{current}, VoicebankRootKind::Installed});
  }
}

}  // namespace

core::Result<std::vector<VoicebankCandidate>> VoicebankCatalog::scan(
    const std::vector<VoicebankSearchRoot>& roots) const {
  ManifestJsonCodec codec;
  std::vector<VoicebankCandidate> result;
  for (const auto& root : roots) {
    for (const auto& manifestPath : manifestPathsFor(root)) {
      auto manifest = codec.load(manifestPath);
      if (!manifest) {
        return core::Result<std::vector<VoicebankCandidate>>{manifest.error()};
      }
      const auto bankRoot = manifestPath.parent_path();
      auto contentHash = computeVoicebankContentHash(manifest.value(), bankRoot);
      if (!contentHash) {
        return core::Result<std::vector<VoicebankCandidate>>{contentHash.error()};
      }
      const auto receipt = loadReceipt(bankRoot);
      const auto installedReceiptMatches =
          receipt.present && receipt.voicebankId == manifest.value().id &&
          receipt.voicebankVersion == manifest.value().version &&
          receipt.contentHash == contentHash.value();
      VoicebankTrust trust = VoicebankTrust::DevelopmentFixture;
      if (root.kind == VoicebankRootKind::Installed) {
        trust = installedReceiptMatches && receipt.signatureValid && receipt.signerTrusted
                    ? VoicebankTrust::TrustedInstalled
                    : VoicebankTrust::UntrustedInstalled;
      }
      result.push_back(VoicebankCandidate{
          .manifest = std::move(manifest).value(),
          .bankRoot = bankRoot,
          .contentHash = std::move(contentHash).value(),
          .trust = trust,
          .packageDigest = receipt.packageDigest,
          .signerKeyId = receipt.signerKeyId,
      });
      if (result.size() > kMaximumCandidates) {
        return core::failure<std::vector<VoicebankCandidate>>(
            core::ErrorCode::Unsupported,
            "Voicebank catalog exceeds the supported candidate count");
      }
    }
  }
  const auto trustRank = [](VoicebankTrust trust) noexcept {
    switch (trust) {
      case VoicebankTrust::TrustedInstalled: return 0;
      case VoicebankTrust::DevelopmentFixture: return 1;
      case VoicebankTrust::UntrustedInstalled: return 2;
    }
    return 3;
  };
  std::stable_sort(result.begin(), result.end(), [&](const auto& lhs, const auto& rhs) {
    if (lhs.manifest.id != rhs.manifest.id) return lhs.manifest.id < rhs.manifest.id;
    if (lhs.manifest.version != rhs.manifest.version) {
      return lhs.manifest.version < rhs.manifest.version;
    }
    if (lhs.contentHash != rhs.contentHash) return lhs.contentHash < rhs.contentHash;
    const auto lhsRank = trustRank(lhs.trust);
    const auto rhsRank = trustRank(rhs.trust);
    if (lhsRank != rhsRank) return lhsRank < rhsRank;
    return lhs.bankRoot.generic_string() < rhs.bankRoot.generic_string();
  });
  result.erase(std::unique(result.begin(), result.end(), [](const auto& lhs,
                                                            const auto& rhs) {
    return lhs.manifest.id == rhs.manifest.id &&
           lhs.manifest.version == rhs.manifest.version &&
           lhs.contentHash == rhs.contentHash &&
           lhs.bankRoot == rhs.bankRoot;
  }), result.end());
  return result;
}

VoicebankResolution VoicebankCatalog::resolve(
    const domain::VoicebankReference& reference,
    const std::vector<VoicebankCandidate>& candidates,
    const VoicebankResolveOptions& options) const {
  VoicebankResolution result;
  if (reference.id.empty() || reference.version.empty()) {
    result.status = VoicebankResolveStatus::InvalidReference;
    result.diagnostic = "Project track has no exact Voicebank ID and version";
    return result;
  }
  std::vector<const VoicebankCandidate*> idMatches;
  std::vector<const VoicebankCandidate*> versionMatches;
  for (const auto& candidate : candidates) {
    if (candidate.manifest.id != reference.id) continue;
    idMatches.push_back(&candidate);
    result.availableVersions.push_back(candidate.manifest.version);
    if (candidate.manifest.version == reference.version) {
      versionMatches.push_back(&candidate);
    }
  }
  std::sort(result.availableVersions.begin(), result.availableVersions.end());
  result.availableVersions.erase(
      std::unique(result.availableVersions.begin(), result.availableVersions.end()),
      result.availableVersions.end());
  if (idMatches.empty()) {
    result.status = VoicebankResolveStatus::Missing;
    result.diagnostic = "Voicebank is not installed: " + reference.id;
    return result;
  }
  if (versionMatches.empty()) {
    result.status = VoicebankResolveStatus::VersionMismatch;
    result.diagnostic = "Voicebank version is unavailable: " + reference.id + " " +
                        reference.version;
    return result;
  }
  if (reference.contentHash.empty()) {
    result.status = VoicebankResolveStatus::ContentHashMissing;
    result.diagnostic =
        "Project Voicebank reference has no content hash; explicit rebind is required";
    return result;
  }

  std::vector<const VoicebankCandidate*> contentMatches;
  for (const auto* candidate : versionMatches) {
    if (candidate->contentHash == reference.contentHash) {
      contentMatches.push_back(candidate);
    }
  }
  if (contentMatches.empty()) {
    result.status = VoicebankResolveStatus::ContentMismatch;
    result.expectedContentHash = reference.contentHash;
    for (const auto* candidate : versionMatches) {
      result.actualContentHashes.push_back(candidate->contentHash);
    }
    std::sort(result.actualContentHashes.begin(), result.actualContentHashes.end());
    result.actualContentHashes.erase(
        std::unique(result.actualContentHashes.begin(),
                    result.actualContentHashes.end()),
        result.actualContentHashes.end());
    result.diagnostic =
        "Voicebank content hash does not match the saved project state";
    return result;
  }

  const auto acceptable = [&options](const VoicebankCandidate* candidate) noexcept {
    if (candidate->trust == VoicebankTrust::TrustedInstalled) return true;
    if (candidate->trust == VoicebankTrust::DevelopmentFixture) {
      return options.allowDevelopmentFixtures;
    }
    return !options.requireTrustedInstalled;
  };
  const auto selected = std::find_if(contentMatches.begin(), contentMatches.end(),
                                     acceptable);
  if (selected == contentMatches.end()) {
    result.status = VoicebankResolveStatus::Untrusted;
    result.diagnostic =
        "Matching Voicebank content exists, but its trust policy is not accepted";
    return result;
  }

  result.status = VoicebankResolveStatus::Resolved;
  result.candidate = **selected;
  result.diagnostic = std::string{"Resolved "} + (*selected)->manifest.displayName +
                      " (" + std::string{voicebankTrustName((*selected)->trust)} + ")";
  return result;
}

std::vector<VoicebankSearchRoot> defaultVoicebankSearchRoots() {
  std::vector<VoicebankSearchRoot> result;
  addEnvironmentRoots(result);
#ifdef _WIN32
  if (const auto* local = std::getenv("LOCALAPPDATA"); local != nullptr) {
    result.push_back({std::filesystem::path{local} / "ProjectSEAM" / "Voicebanks",
                      VoicebankRootKind::Installed});
  }
#elif defined(__APPLE__)
  if (const auto* home = std::getenv("HOME"); home != nullptr) {
    result.push_back({std::filesystem::path{home} / "Library" /
                          "Application Support" / "ProjectSEAM" / "Voicebanks",
                      VoicebankRootKind::Installed});
  }
#else
  if (const auto* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr) {
    result.push_back({std::filesystem::path{xdg} / "project-seam" / "voicebanks",
                      VoicebankRootKind::Installed});
  } else if (const auto* home = std::getenv("HOME"); home != nullptr) {
    result.push_back({std::filesystem::path{home} / ".local" / "share" /
                          "project-seam" / "voicebanks",
                      VoicebankRootKind::Installed});
  }
#endif
  return result;
}

std::string_view voicebankTrustName(VoicebankTrust trust) noexcept {
  switch (trust) {
    case VoicebankTrust::TrustedInstalled: return "trusted-installed";
    case VoicebankTrust::UntrustedInstalled: return "untrusted-installed";
    case VoicebankTrust::DevelopmentFixture: return "development-fixture";
  }
  return "unknown";
}

std::string_view voicebankResolveStatusName(
    VoicebankResolveStatus status) noexcept {
  switch (status) {
    case VoicebankResolveStatus::Resolved: return "resolved";
    case VoicebankResolveStatus::Missing: return "missing";
    case VoicebankResolveStatus::VersionMismatch: return "version-mismatch";
    case VoicebankResolveStatus::ContentHashMissing: return "content-hash-missing";
    case VoicebankResolveStatus::ContentMismatch: return "content-mismatch";
    case VoicebankResolveStatus::Untrusted: return "untrusted";
    case VoicebankResolveStatus::InvalidReference: return "invalid-reference";
  }
  return "unknown";
}

}  // namespace seam::voicebank
