#include "seam/distribution/installer.hpp"

#include "install_transaction_internal.hpp"

#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank/content_identity.hpp"
#include "seam/voicebank/manifest_json.hpp"

namespace seam::distribution {

core::Result<InstalledSeambank> installSeambank(
    const std::filesystem::path& packagePath,
    const std::filesystem::path& installRoot,
    const InstallSeambankOptions& options,
    std::stop_token stop) {
  using Output = InstalledSeambank;
  if (!options.verification.requireTrustedSigner || options.verification.trustedPublicKeys.empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "Voicebank installation requires an explicit trusted public key");
  if (stop.stop_requested())
    return core::failure<Output>(core::ErrorCode::Conflict, "Voicebank installation cancelled; nothing changed");
  auto package = verifySeambank(packagePath, options.verification);
  if (!package) return core::Result<Output>{package.error()};
  if (options.expectedPackageDigest && package.value().packageDigest != *options.expectedPackageDigest)
    return core::failure<Output>(core::ErrorCode::Conflict,
                                 "Voicebank package differs from the caller's captured package digest");
  const auto& info = package.value();
  const auto& manifest = info.manifest;
  const SignedContainerInfo container{.packagePath = info.packagePath, .formatVersion = info.formatVersion,
      .packageDigest = info.packageDigest, .signerPublicKey = info.signerPublicKey, .signerKeyId = info.signerKeyId,
      .signature = info.signature, .entries = info.entries, .payloadBytes = info.payloadBytes,
      .signatureValid = info.signatureValid, .signerTrusted = info.signerTrusted};
  std::string contentHash;
  install_internal::ContainerInstallRequest request;
  request.container = &container;
  request.installRoot = installRoot;
  request.id = manifest.id;
  request.version = manifest.version;
  request.replaceExisting = options.replaceExisting;
  request.maximumEntryBytes = options.verification.limits.maximumEntryBytes;
  request.maximumArchiveBytes = options.verification.limits.maximumArchiveBytes;
  request.faultInjector = options.faultInjector;
  request.finalizeStaged = [&](const std::filesystem::path& staging) -> core::Result<void> {
    auto installedManifest = voicebank::ManifestJsonCodec{}.load(staging / "manifest.json");
    if (!installedManifest || installedManifest.value() != manifest)
      return core::failure(core::ErrorCode::Conflict, "Installed voicebank manifest differs from signed manifest");
    auto hash = voicebank::computeVoicebankContentHash(installedManifest.value(), staging);
    if (!hash) return core::Result<void>{hash.error()};
    if (options.expectedContentHash && hash.value() != *options.expectedContentHash)
      return core::failure(core::ErrorCode::Conflict,
                           "Staged voicebank content differs from the expected content identity");
    contentHash = hash.value();
    formats::JsonValue::Object receipt;
    receipt.emplace("schemaVersion", static_cast<std::int64_t>(2));
    receipt.emplace("voicebankId", manifest.id);
    receipt.emplace("voicebankVersion", manifest.version);
    receipt.emplace("contentHash", contentHash);
    receipt.emplace("packageDigest", info.packageDigest);
    receipt.emplace("signerKeyId", info.signerKeyId);
    receipt.emplace("signatureValid", info.signatureValid);
    receipt.emplace("signerTrusted", info.signerTrusted);
    const auto receiptText = formats::stringifyJson(formats::JsonValue{std::move(receipt)}, true) + "\n";
    return core::durableAtomicWriteTextNew(staging / std::string{install_internal::kInstallReceiptEntry}, receiptText);
  };
  auto outcome = install_internal::installContainerTree(request, stop);
  if (!outcome) return core::Result<Output>{outcome.error()};
  return InstalledSeambank{
      .voicebankId = manifest.id,
      .voicebankVersion = manifest.version,
      .packageDigest = info.packageDigest,
      .signerKeyId = info.signerKeyId,
      .installDirectory = outcome.value().installDirectory,
      .contentHash = contentHash,
      .replacedExisting = outcome.value().replacedExisting,
      .durabilityConfirmed = outcome.value().durabilityConfirmed,
      .diagnostic = outcome.value().diagnostic,
  };
}

}  // namespace seam::distribution
