#include "seam/distribution/update_manifest.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <span>
#include <system_error>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace seam::distribution {
namespace {

using formats::JsonValue;
std::atomic<std::uint64_t> gCandidateSequence{0U};

bool safeFilename(std::string_view value) noexcept {
  if (value.empty() || value.size() > 128U || value == "." || value == ".." ||
      std::isalnum(static_cast<unsigned char>(value.front())) == 0) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](char character) {
    const auto byte = static_cast<unsigned char>(character);
    return std::isalnum(byte) != 0 || character == '.' || character == '_' ||
           character == '-';
  });
}

bool safeCandidateId(std::string_view value) noexcept {
  if (value.size() != 42U || value.substr(0U, 10U) != "candidate-") {
    return false;
  }
  return std::all_of(value.begin() + 10, value.end(), [](char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
  });
}

bool hex64(std::string_view value) noexcept {
  if (value.size() != 64U) return false;
  return std::all_of(value.begin(), value.end(), [](char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f');
  });
}

core::Result<void> ensureRoot(const std::filesystem::path& root) {
  if (root.empty() || root == root.root_path()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Update staging root is invalid");
  }
  std::error_code error;
  if (std::filesystem::exists(root, error)) {
    const auto status = std::filesystem::symlink_status(root, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
      return core::failure(core::ErrorCode::Conflict,
                           "Update staging root must be a real directory",
                           root.string());
    }
  } else {
    std::filesystem::create_directories(root, error);
    if (error) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to create update staging root", error.message());
    }
  }
#ifndef _WIN32
  if (::chmod(root.c_str(), S_IRWXU) != 0) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to restrict update staging root", root.string());
  }
#endif
  return core::success();
}

std::pair<std::uint64_t, std::uint64_t> fileIdentity(
    const std::filesystem::path& path) noexcept {
#ifdef _WIN32
  static_cast<void>(path);
  return {0U, 0U};
#else
  struct stat info{};
  if (::stat(path.c_str(), &info) != 0) return {0U, 0U};
  return {static_cast<std::uint64_t>(info.st_dev),
          static_cast<std::uint64_t>(info.st_ino)};
#endif
}

core::Result<void> copyRegularFile(const std::filesystem::path& source,
                                   const std::filesystem::path& destination) {
  std::ifstream input(source, std::ios::binary);
  std::ofstream output(destination, std::ios::binary | std::ios::trunc);
  if (!input || !output) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to open update staging copy");
  }
  std::array<char, 1024U * 1024U> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = input.gcount();
    if (count > 0) output.write(buffer.data(), count);
    if (!output) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to write update staging copy");
    }
  }
  output.flush();
  return output ? core::success()
                : core::failure(core::ErrorCode::IoError,
                                "Unable to flush update staging copy");
}

JsonValue handoffJson(const SealedInstallerHandoff& handoff) {
  JsonValue::Object package;
  package.emplace("fileName", handoff.package.fileName);
  package.emplace("relativePath", handoff.package.relativePath);
  package.emplace("size", static_cast<std::int64_t>(handoff.package.size));
  package.emplace("sha256", handoff.package.sha256);
  package.emplace("device", static_cast<std::int64_t>(handoff.package.device));
  package.emplace("inode", static_cast<std::int64_t>(handoff.package.inode));
  JsonValue::Object root;
  root.emplace("schemaVersion", handoff.schemaVersion);
  root.emplace("purpose", handoff.purpose);
  root.emplace("candidateId", handoff.candidateId);
  root.emplace("manifestSha256", handoff.manifestSha256);
  root.emplace("package", std::move(package));
  root.emplace("requiresExplicitUserAction", handoff.requiresExplicitUserAction);
  root.emplace("requiresInstallerRevalidation", handoff.requiresInstallerRevalidation);
  root.emplace("createdAt", handoff.createdAt);
  return JsonValue{std::move(root)};
}

core::Result<std::string> requiredString(const JsonValue& value,
                                         std::string_view field) {
  const auto* child = value.find(field);
  if (child == nullptr || !child->isString() || child->asString().empty()) {
    return core::failure<std::string>(core::ErrorCode::ParseError,
                                      "Handoff field must be a non-empty string",
                                      std::string{field});
  }
  return child->asString();
}

core::Result<std::int64_t> requiredInteger(const JsonValue& value,
                                           std::string_view field) {
  const auto* child = value.find(field);
  if (child == nullptr || !child->isInteger()) {
    return core::failure<std::int64_t>(core::ErrorCode::ParseError,
                                       "Handoff field must be an integer",
                                       std::string{field});
  }
  return child->asInt64();
}

core::Result<bool> requiredBool(const JsonValue& value, std::string_view field) {
  const auto* child = value.find(field);
  if (child == nullptr || !child->isBool()) {
    return core::failure<bool>(core::ErrorCode::ParseError,
                               "Handoff field must be a boolean",
                               std::string{field});
  }
  return child->asBool();
}

core::Result<void> rejectUnknown(const JsonValue& value,
                                 std::initializer_list<std::string_view> allowed,
                                 std::string_view context) {
  if (!value.isObject()) {
    return core::failure(core::ErrorCode::ParseError,
                         "Handoff value must be an object", std::string{context});
  }
  for (const auto& [key, unused] : value.asObject()) {
    static_cast<void>(unused);
    if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
      return core::failure(core::ErrorCode::ParseError,
                           "Handoff contains an unknown field",
                           std::string{context} + "." + key);
    }
  }
  return core::success();
}

core::Result<SealedInstallerHandoff> parseRoot(const JsonValue& root) {
  auto keys = rejectUnknown(root,
                            {"schemaVersion", "purpose", "candidateId",
                             "manifestSha256", "package",
                             "requiresExplicitUserAction",
                             "requiresInstallerRevalidation", "createdAt"},
                            "handoff");
  if (!keys) return core::Result<SealedInstallerHandoff>{keys.error()};
  auto schema = requiredInteger(root, "schemaVersion");
  auto purpose = requiredString(root, "purpose");
  auto candidate = requiredString(root, "candidateId");
  auto manifest = requiredString(root, "manifestSha256");
  auto explicitAction = requiredBool(root, "requiresExplicitUserAction");
  auto revalidation = requiredBool(root, "requiresInstallerRevalidation");
  auto createdAt = requiredString(root, "createdAt");
  const auto* package = root.find("package");
  if (!schema || !purpose || !candidate || !manifest || !explicitAction ||
      !revalidation || !createdAt || package == nullptr) {
    if (!schema) return core::Result<SealedInstallerHandoff>{schema.error()};
    if (!purpose) return core::Result<SealedInstallerHandoff>{purpose.error()};
    if (!candidate) return core::Result<SealedInstallerHandoff>{candidate.error()};
    if (!manifest) return core::Result<SealedInstallerHandoff>{manifest.error()};
    if (!explicitAction) return core::Result<SealedInstallerHandoff>{explicitAction.error()};
    if (!revalidation) return core::Result<SealedInstallerHandoff>{revalidation.error()};
    if (!createdAt) return core::Result<SealedInstallerHandoff>{createdAt.error()};
    return core::failure<SealedInstallerHandoff>(core::ErrorCode::ParseError,
                                                 "Handoff package is required");
  }
  auto packageKeys = rejectUnknown(*package,
                                   {"fileName", "relativePath", "size", "sha256",
                                    "device", "inode"},
                                   "handoff.package");
  if (!packageKeys) return core::Result<SealedInstallerHandoff>{packageKeys.error()};
  auto fileName = requiredString(*package, "fileName");
  auto relativePath = requiredString(*package, "relativePath");
  auto size = requiredInteger(*package, "size");
  auto sha256 = requiredString(*package, "sha256");
  auto device = requiredInteger(*package, "device");
  auto inode = requiredInteger(*package, "inode");
  if (!fileName) return core::Result<SealedInstallerHandoff>{fileName.error()};
  if (!relativePath) return core::Result<SealedInstallerHandoff>{relativePath.error()};
  if (!size) return core::Result<SealedInstallerHandoff>{size.error()};
  if (!sha256) return core::Result<SealedInstallerHandoff>{sha256.error()};
  if (!device) return core::Result<SealedInstallerHandoff>{device.error()};
  if (!inode) return core::Result<SealedInstallerHandoff>{inode.error()};
  if (schema.value() != 1 || purpose.value() != "sealed-installer-handoff" ||
      !safeFilename(fileName.value()) || fileName.value() !=
          std::filesystem::path{relativePath.value()}.filename().string() ||
      size.value() <= 0 || device.value() < 0 || inode.value() < 0 ||
      !hex64(sha256.value()) || !hex64(manifest.value()) ||
      !safeCandidateId(candidate.value())) {
    return core::failure<SealedInstallerHandoff>(core::ErrorCode::ParseError,
                                                 "Handoff fields are invalid");
  }
  return SealedInstallerHandoff{
      .schemaVersion = schema.value(),
      .purpose = std::move(purpose).value(),
      .candidateId = std::move(candidate).value(),
      .manifestSha256 = std::move(manifest).value(),
      .package = SealedUpdatePackage{
          .fileName = std::move(fileName).value(),
          .relativePath = std::move(relativePath).value(),
          .size = static_cast<std::uint64_t>(size.value()),
          .sha256 = std::move(sha256).value(),
          .device = static_cast<std::uint64_t>(device.value()),
          .inode = static_cast<std::uint64_t>(inode.value())},
      .requiresExplicitUserAction = explicitAction.value(),
      .requiresInstallerRevalidation = revalidation.value(),
      .createdAt = std::move(createdAt).value()};
}

}

std::string serializeSealedInstallerHandoff(
    const SealedInstallerHandoff& handoff) {
  return formats::stringifyJson(handoffJson(handoff), false);
}

core::Result<SealedInstallerHandoff> parseSealedInstallerHandoff(
    std::string_view json) {
  auto parsed = formats::parseJson(json, formats::JsonParseLimits{
      .maximumInputBytes = 64U * 1024U,
      .maximumDepth = 8U,
      .maximumNodes = 128U,
      .maximumStringBytes = 8192U,
      .maximumCollectionEntries = 32U});
  if (!parsed) return core::Result<SealedInstallerHandoff>{parsed.error()};
  return parseRoot(parsed.value());
}

core::Result<SealedInstallerHandoff> stageVerifiedUpdatePackage(
    const std::filesystem::path& packagePath, const UpdateManifest& manifest,
    const std::filesystem::path& stagingRoot) {
  auto root = ensureRoot(stagingRoot);
  if (!root) return core::Result<SealedInstallerHandoff>{root.error()};
  std::error_code error;
  const auto sourceStatus = std::filesystem::symlink_status(packagePath, error);
  if (error || std::filesystem::is_symlink(sourceStatus) ||
      !std::filesystem::is_regular_file(sourceStatus)) {
    return core::failure<SealedInstallerHandoff>(
        core::ErrorCode::Conflict, "Update package must be a regular non-link file",
        packagePath.string());
  }
  if (!safeFilename(manifest.package.fileName)) {
    return core::failure<SealedInstallerHandoff>(core::ErrorCode::InvalidArgument,
                                                 "Signed update filename is unsafe");
  }
  const auto size = std::filesystem::file_size(packagePath, error);
  if (error || size != manifest.package.size) {
    return core::failure<SealedInstallerHandoff>(core::ErrorCode::Conflict,
                                                 "Update package size differs from signed metadata");
  }
  auto digest = core::sha256File(packagePath, manifest.package.size);
  if (!digest || digest.value() != manifest.package.sha256) {
    return core::failure<SealedInstallerHandoff>(core::ErrorCode::Conflict,
                                                 "Update package hash differs from signed metadata");
  }
  const auto sequence = gCandidateSequence.fetch_add(
      1U, std::memory_order_relaxed) + 1U;
  const auto seed = manifest.manifestId + ":" + manifest.package.sha256 + ":" +
                    std::to_string(sequence);
  const auto candidateId = "candidate-" + core::sha256Hex(seed).substr(0U, 32U);
  const auto candidateRoot = stagingRoot / candidateId;
  std::filesystem::create_directory(candidateRoot, error);
  if (error) {
    return core::failure<SealedInstallerHandoff>(core::ErrorCode::IoError,
                                                 "Unable to create update candidate root",
                                                 error.message());
  }
#ifndef _WIN32
  static_cast<void>(::chmod(candidateRoot.c_str(), S_IRWXU));
#endif
  const auto destination = candidateRoot / manifest.package.fileName;
  auto copied = copyRegularFile(packagePath, destination);
  if (!copied) {
    std::filesystem::remove_all(candidateRoot, error);
    return core::Result<SealedInstallerHandoff>{copied.error()};
  }
  const auto copiedSize = std::filesystem::file_size(destination, error);
  auto copiedDigest = core::sha256File(destination, manifest.package.size);
  if (error || !copiedDigest || copiedSize != manifest.package.size ||
      copiedDigest.value() != manifest.package.sha256) {
    std::filesystem::remove_all(candidateRoot, error);
    return core::failure<SealedInstallerHandoff>(
        core::ErrorCode::Conflict, "Staged update package failed byte verification");
  }
  const auto identity = fileIdentity(destination);
  const SealedInstallerHandoff handoff{
      .schemaVersion = 1,
      .purpose = "sealed-installer-handoff",
      .candidateId = candidateId,
      .manifestSha256 = updateManifestIdentity(manifest),
      .package = SealedUpdatePackage{
          .fileName = manifest.package.fileName,
          .relativePath = candidateId + "/" + manifest.package.fileName,
          .size = copiedSize,
          .sha256 = copiedDigest.value(),
          .device = identity.first,
          .inode = identity.second},
      .requiresExplicitUserAction = true,
      .requiresInstallerRevalidation = true,
      .createdAt = "1970-01-01T00:00:00Z"};
  const auto handoffPath = candidateRoot / "handoff.json";
  auto written = core::durableAtomicWriteText(
      handoffPath, serializeSealedInstallerHandoff(handoff));
  if (!written) {
    std::filesystem::remove_all(candidateRoot, error);
    return core::Result<SealedInstallerHandoff>{written.error()};
  }
#ifndef _WIN32
  static_cast<void>(::chmod(handoffPath.c_str(), S_IRUSR | S_IWUSR));
#endif
  return handoff;
}

core::Result<void> verifySealedInstallerHandoff(
    const SealedInstallerHandoff& handoff, const UpdateManifest& manifest,
    const std::filesystem::path& stagingRoot) {
  if (handoff.schemaVersion != 1 || handoff.purpose != "sealed-installer-handoff" ||
      !handoff.requiresExplicitUserAction ||
      !handoff.requiresInstallerRevalidation ||
      handoff.manifestSha256 != updateManifestIdentity(manifest) ||
      handoff.package.fileName != manifest.package.fileName ||
      handoff.package.size != manifest.package.size ||
      handoff.package.sha256 != manifest.package.sha256 ||
      !safeCandidateId(handoff.candidateId) || handoff.candidateId.empty() ||
      !hex64(handoff.manifestSha256) || !hex64(handoff.package.sha256)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Sealed update handoff identity is invalid");
  }
  const auto relativePath = std::filesystem::path{handoff.package.relativePath};
  const bool escapesRoot = std::find_if(
      relativePath.begin(), relativePath.end(), [](const auto& component) {
        return component == std::filesystem::path{".."};
      }) != relativePath.end();
  const auto relativeParent = relativePath.parent_path();
  if (relativePath.is_absolute() || escapesRoot || relativeParent !=
          std::filesystem::path{handoff.candidateId}) {
    return core::failure(core::ErrorCode::Conflict,
                         "Sealed update handoff path escapes staging root");
  }
  auto root = ensureRoot(stagingRoot);
  if (!root) return root;
  const auto candidateRoot = stagingRoot / handoff.candidateId;
  const auto candidateStatus = std::filesystem::symlink_status(candidateRoot);
  if (std::filesystem::is_symlink(candidateStatus) ||
      !std::filesystem::is_directory(candidateStatus)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Sealed update candidate root is unsafe");
  }
  const auto packagePath = stagingRoot / handoff.package.relativePath;
  std::error_code error;
  const auto status = std::filesystem::symlink_status(packagePath, error);
  if (error || std::filesystem::is_symlink(status) ||
      !std::filesystem::is_regular_file(status) || packagePath.filename().string() !=
          handoff.package.fileName) {
    return core::failure(core::ErrorCode::Conflict,
                         "Sealed update package path is unsafe");
  }
  const auto size = std::filesystem::file_size(packagePath, error);
  auto digest = core::sha256File(packagePath, manifest.package.size);
  if (error || !digest || size != manifest.package.size ||
      digest.value() != manifest.package.sha256) {
    return core::failure(core::ErrorCode::Conflict,
                         "Sealed update package bytes changed");
  }
  const auto identity = fileIdentity(packagePath);
  if ((handoff.package.device != 0U && identity.first != handoff.package.device) ||
      (handoff.package.inode != 0U && identity.second != handoff.package.inode)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Sealed update package file identity changed");
  }
  return core::success();
}

}
