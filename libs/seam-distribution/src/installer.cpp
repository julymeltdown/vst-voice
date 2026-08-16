#include "seam/distribution/installer.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank/manifest_json.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <system_error>

namespace seam::distribution {
namespace {

std::atomic<std::uint64_t> gInstallCounter{0U};

bool safeComponent(std::string_view value) noexcept {
  if (value.empty() || value == "." || value == "..") return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return std::isalnum(character) != 0 || character == '.' || character == '-' || character == '_';
  });
}

core::Result<std::vector<std::byte>> readVerifiedEntry(
    std::ifstream& stream, const SeambankEntry& entry,
    std::uint64_t maximumBytes) {
  if (entry.payloadSize > maximumBytes) {
    return core::failure<std::vector<std::byte>>(core::ErrorCode::Unsupported,
                                                 "Install entry exceeds configured limit", entry.path);
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(entry.payloadSize));
  stream.clear();
  stream.seekg(static_cast<std::streamoff>(entry.payloadOffset), std::ios::beg);
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!stream || static_cast<std::size_t>(stream.gcount()) != bytes.size()) {
    return core::failure<std::vector<std::byte>>(core::ErrorCode::IoError,
                                                 "Unable to read install entry", entry.path);
  }
  core::Sha256 hash;
  hash.update(bytes);
  if (hash.digest() != entry.sha256) {
    return core::failure<std::vector<std::byte>>(core::ErrorCode::Conflict,
                                                 "Install entry checksum mismatch", entry.path);
  }
  return bytes;
}

}  // namespace

core::Result<InstalledSeambank> installSeambank(
    const std::filesystem::path& packagePath,
    const std::filesystem::path& installRoot,
    const InstallSeambankOptions& options) {
  if (!options.verification.requireTrustedSigner ||
      options.verification.trustedPublicKeys.empty()) {
    return core::failure<InstalledSeambank>(
        core::ErrorCode::InvalidArgument,
        "Voicebank installation requires an explicit trusted public key");
  }
  auto package = verifySeambank(packagePath, options.verification);
  if (!package) return core::Result<InstalledSeambank>{package.error()};
  const auto& manifest = package.value().manifest;
  if (!safeComponent(manifest.id) || !safeComponent(manifest.version)) {
    return core::failure<InstalledSeambank>(core::ErrorCode::Unsupported,
                                            "Voicebank ID or version is unsafe for installation");
  }
  std::error_code error;
  if (std::filesystem::exists(installRoot, error)) {
    const auto status = std::filesystem::symlink_status(installRoot, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
      return core::failure<InstalledSeambank>(
          core::ErrorCode::Conflict,
          "Voicebank install root must be a real directory",
          installRoot.string());
    }
  } else {
    std::filesystem::create_directories(installRoot, error);
    if (error) {
      return core::failure<InstalledSeambank>(
          core::ErrorCode::IoError, "Unable to create voicebank install root",
          error.message());
    }
  }
  const auto canonicalRoot = std::filesystem::canonical(installRoot, error);
  if (error) {
    return core::failure<InstalledSeambank>(
        core::ErrorCode::IoError, "Unable to canonicalize voicebank install root",
        error.message());
  }
  const auto productRoot = canonicalRoot / manifest.id;
  if (std::filesystem::exists(productRoot, error)) {
    const auto status = std::filesystem::symlink_status(productRoot, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
      return core::failure<InstalledSeambank>(
          core::ErrorCode::Conflict,
          "Voicebank product install path is unsafe", productRoot.string());
    }
  } else {
    std::filesystem::create_directory(productRoot, error);
    if (error) {
      return core::failure<InstalledSeambank>(
          core::ErrorCode::IoError, "Unable to create voicebank product directory",
          error.message());
    }
  }

  const auto target = productRoot / manifest.version;
  if (std::filesystem::exists(target) && !options.replaceExisting) {
    return core::failure<InstalledSeambank>(core::ErrorCode::Conflict,
                                            "Voicebank version is already installed", target.string());
  }
  const auto token = gInstallCounter.fetch_add(1U, std::memory_order_relaxed);
  const auto staging = canonicalRoot / (".staging-" + manifest.id + "-" +
                                      manifest.version + "-" + std::to_string(token));
  const auto backup = canonicalRoot / (".backup-" + manifest.id + "-" +
                                     manifest.version + "-" + std::to_string(token));
  std::filesystem::remove_all(staging, error);
  std::filesystem::create_directories(staging, error);
  if (error) return core::failure<InstalledSeambank>(core::ErrorCode::IoError, "Unable to create voicebank staging directory", error.message());

  std::ifstream stream(packagePath, std::ios::binary);
  if (!stream) {
    std::filesystem::remove_all(staging, error);
    return core::failure<InstalledSeambank>(
        core::ErrorCode::IoError, "Unable to reopen verified seambank package",
        packagePath.string());
  }
  for (const auto& entry : package.value().entries) {
    auto bytes = readVerifiedEntry(stream, entry, options.verification.limits.maximumEntryBytes);
    if (!bytes) {
      std::filesystem::remove_all(staging, error);
      return core::Result<InstalledSeambank>{bytes.error()};
    }
    const auto destination = staging / std::filesystem::path{entry.path};
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
      std::filesystem::remove_all(staging, error);
      return core::failure<InstalledSeambank>(core::ErrorCode::IoError, "Unable to create installed asset directory", entry.path);
    }
    auto written = core::durableAtomicWrite(destination, bytes.value());
    if (!written) {
      std::filesystem::remove_all(staging, error);
      return core::Result<InstalledSeambank>{written.error()};
    }
  }

  voicebank::ManifestJsonCodec codec;
  auto installedManifest = codec.load(staging / "manifest.json");
  if (!installedManifest || installedManifest.value() != manifest) {
    std::filesystem::remove_all(staging, error);
    return core::failure<InstalledSeambank>(core::ErrorCode::Conflict,
                                            "Installed voicebank manifest differs from signed manifest");
  }
  formats::JsonValue::Object receipt;
  receipt.emplace("schemaVersion", static_cast<std::int64_t>(1));
  receipt.emplace("voicebankId", manifest.id);
  receipt.emplace("voicebankVersion", manifest.version);
  receipt.emplace("packageDigest", package.value().packageDigest);
  receipt.emplace("signerKeyId", package.value().signerKeyId);
  receipt.emplace("signatureValid", package.value().signatureValid);
  receipt.emplace("signerTrusted", package.value().signerTrusted);
  auto receiptText = formats::stringifyJson(formats::JsonValue{std::move(receipt)}, true) + "\n";
  auto receiptWritten = core::durableAtomicWriteText(staging / "install-receipt.json", receiptText);
  if (!receiptWritten) {
    std::filesystem::remove_all(staging, error);
    return core::Result<InstalledSeambank>{receiptWritten.error()};
  }

  std::filesystem::create_directories(target.parent_path(), error);
  if (error) {
    std::filesystem::remove_all(staging, error);
    return core::failure<InstalledSeambank>(core::ErrorCode::IoError, "Unable to create voicebank product directory", error.message());
  }
  auto finalDigest = core::sha256File(packagePath, options.verification.limits.maximumArchiveBytes);
  if (!finalDigest || finalDigest.value() != package.value().packageDigest) {
    std::filesystem::remove_all(staging, error);
    return core::failure<InstalledSeambank>(core::ErrorCode::Conflict,
                                            "Seambank package changed during installation");
  }

  bool movedExisting = false;
  if (std::filesystem::exists(target)) {
    const auto targetStatus = std::filesystem::symlink_status(target, error);
    if (error || std::filesystem::is_symlink(targetStatus) ||
        !std::filesystem::is_directory(targetStatus)) {
      std::filesystem::remove_all(staging, error);
      return core::failure<InstalledSeambank>(
          core::ErrorCode::Conflict,
          "Existing voicebank installation target is unsafe", target.string());
    }
    std::filesystem::remove_all(backup, error);
    std::filesystem::rename(target, backup, error);
    if (error) {
      std::filesystem::remove_all(staging, error);
      return core::failure<InstalledSeambank>(core::ErrorCode::IoError, "Unable to stage existing voicebank for replacement", error.message());
    }
    movedExisting = true;
  }
  std::filesystem::rename(staging, target, error);
  if (error) {
    if (movedExisting) {
      std::error_code rollbackError;
      std::filesystem::rename(backup, target, rollbackError);
    }
    std::filesystem::remove_all(staging, error);
    return core::failure<InstalledSeambank>(core::ErrorCode::IoError, "Unable to publish installed voicebank", error.message());
  }
  if (movedExisting) std::filesystem::remove_all(backup, error);

  return InstalledSeambank{
      .voicebankId = manifest.id,
      .voicebankVersion = manifest.version,
      .packageDigest = package.value().packageDigest,
      .signerKeyId = package.value().signerKeyId,
      .installDirectory = target,
  };
}

}  // namespace seam::distribution
