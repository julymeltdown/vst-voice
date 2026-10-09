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

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
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

bool utcTimestamp(std::string_view value) noexcept {
  if (value.size() != 20U || value.back() != 'Z') return false;
  constexpr std::array<std::size_t, 5U> separators{4U, 7U, 10U, 13U, 16U};
  constexpr std::array<char, 5U> expected{'-', '-', 'T', ':', ':'};
  for (std::size_t index = 0; index < separators.size(); ++index) {
    if (value[separators[index]] != expected[index]) return false;
  }
  for (std::size_t index = 0; index < value.size() - 1U; ++index) {
    if (std::find(separators.begin(), separators.end(), index) !=
        separators.end()) {
      continue;
    }
    if (value[index] < '0' || value[index] > '9') return false;
  }
  return true;
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

core::Result<void> validateExistingRoot(const std::filesystem::path& root) {
  if (root.empty() || root == root.root_path()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Update staging root is invalid");
  }
  std::error_code error;
  const auto status = std::filesystem::symlink_status(root, error);
  if (error || std::filesystem::is_symlink(status) ||
      !std::filesystem::is_directory(status)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Update staging root must already be a real directory",
                         root.string());
  }
#ifndef _WIN32
  struct stat info{};
  if (::stat(root.c_str(), &info) != 0 ||
      (info.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
    return core::failure(core::ErrorCode::Conflict,
                         "Update staging root permissions are unsafe",
                         root.string());
  }
#endif
  return core::success();
}

struct StableFileSnapshot final {
  std::uint64_t size{0};
  std::uint64_t device{0};
  std::uint64_t inode{0};
  std::string sha256;
};

core::Result<StableFileSnapshot> inspectStableRegularFile(
    const std::filesystem::path& path, std::uint64_t maximumBytes) {
#ifdef _WIN32
  const HANDLE file = ::CreateFileW(
      path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return core::failure<StableFileSnapshot>(
        core::ErrorCode::IoError, "Unable to open sealed update package",
        std::to_string(::GetLastError()));
  }
  BY_HANDLE_FILE_INFORMATION before{};
  if (::GetFileInformationByHandle(file, &before) == 0 ||
      (before.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
    const auto error = ::GetLastError();
    ::CloseHandle(file);
    return core::failure<StableFileSnapshot>(
        core::ErrorCode::Conflict, "Sealed update package handle is unsafe",
        std::to_string(error));
  }
  const auto size = (static_cast<std::uint64_t>(before.nFileSizeHigh) << 32U) |
                    before.nFileSizeLow;
  if (size > maximumBytes) {
    ::CloseHandle(file);
    return core::failure<StableFileSnapshot>(
        core::ErrorCode::Conflict, "Sealed update package is too large");
  }
  core::Sha256 hasher;
  std::array<std::byte, 1024U * 1024U> buffer{};
  std::uint64_t total = 0U;
  while (true) {
    DWORD count = 0U;
    if (::ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                   &count, nullptr) == 0) {
      const auto error = ::GetLastError();
      ::CloseHandle(file);
      return core::failure<StableFileSnapshot>(
          core::ErrorCode::IoError, "Unable to read sealed update package",
          std::to_string(error));
    }
    if (count == 0U) break;
    total += count;
    if (total > maximumBytes) {
      ::CloseHandle(file);
      return core::failure<StableFileSnapshot>(
          core::ErrorCode::Conflict, "Sealed update package is too large");
    }
    hasher.update(std::span{buffer.data(), static_cast<std::size_t>(count)});
  }
  BY_HANDLE_FILE_INFORMATION after{};
  const bool stable = ::GetFileInformationByHandle(file, &after) != 0 &&
                      before.dwVolumeSerialNumber == after.dwVolumeSerialNumber &&
                      before.nFileIndexHigh == after.nFileIndexHigh &&
                      before.nFileIndexLow == after.nFileIndexLow &&
                      before.nFileSizeHigh == after.nFileSizeHigh &&
                      before.nFileSizeLow == after.nFileSizeLow;
  ::CloseHandle(file);
  if (!stable || total != size) {
    return core::failure<StableFileSnapshot>(
        core::ErrorCode::Conflict, "Sealed update package changed while open");
  }
  return StableFileSnapshot{
      .size = size,
      .device = before.dwVolumeSerialNumber,
      .inode = (static_cast<std::uint64_t>(before.nFileIndexHigh) << 32U) |
               before.nFileIndexLow,
      .sha256 = hasher.hexDigest()};
#else
  const int file = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (file < 0) {
    return core::failure<StableFileSnapshot>(
        core::ErrorCode::IoError, "Unable to open sealed update package",
        std::to_string(errno));
  }
  struct stat before{};
  if (::fstat(file, &before) != 0 || !S_ISREG(before.st_mode) ||
      before.st_size < 0) {
    const auto error = errno;
    ::close(file);
    return core::failure<StableFileSnapshot>(
        core::ErrorCode::Conflict, "Sealed update package handle is unsafe",
        std::to_string(error));
  }
  const auto size = static_cast<std::uint64_t>(before.st_size);
  if (size > maximumBytes) {
    ::close(file);
    return core::failure<StableFileSnapshot>(
        core::ErrorCode::Conflict, "Sealed update package is too large");
  }
  core::Sha256 hasher;
  std::array<std::byte, 1024U * 1024U> buffer{};
  std::uint64_t total = 0U;
  while (true) {
    const auto count = ::read(file, buffer.data(), buffer.size());
    if (count < 0) {
      if (errno == EINTR) continue;
      const auto error = errno;
      ::close(file);
      return core::failure<StableFileSnapshot>(
          core::ErrorCode::IoError, "Unable to read sealed update package",
          std::to_string(error));
    }
    if (count == 0) break;
    total += static_cast<std::uint64_t>(count);
    if (total > maximumBytes) {
      ::close(file);
      return core::failure<StableFileSnapshot>(
          core::ErrorCode::Conflict, "Sealed update package is too large");
    }
    hasher.update(std::span{buffer.data(), static_cast<std::size_t>(count)});
  }
  struct stat after{};
  const bool stable = ::fstat(file, &after) == 0 &&
                      before.st_dev == after.st_dev &&
                      before.st_ino == after.st_ino &&
                      before.st_size == after.st_size;
  ::close(file);
  if (!stable || total != size) {
    return core::failure<StableFileSnapshot>(
        core::ErrorCode::Conflict, "Sealed update package changed while open");
  }
  return StableFileSnapshot{
      .size = size,
      .device = static_cast<std::uint64_t>(before.st_dev),
      .inode = static_cast<std::uint64_t>(before.st_ino),
      .sha256 = hasher.hexDigest()};
#endif
}

core::Result<void> consumeHandoff(
    const SealedInstallerHandoff& handoff,
    const InstallerHandoffVerificationOptions& options) {
  if (!options.consume) return core::success();
  if (options.replayStateRoot.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Replay state root is required when consuming a handoff");
  }
  auto root = ensureRoot(options.replayStateRoot);
  if (!root) return root;
  const auto marker = options.replayStateRoot /
                      (handoff.manifestSha256 + ".used");
  const auto payload = handoff.manifestSha256 + "\n";
#ifdef _WIN32
  const HANDLE file = ::CreateFileW(marker.c_str(), GENERIC_WRITE, 0U, nullptr,
                                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return core::failure(core::ErrorCode::Conflict,
                         "Sealed update handoff was already consumed");
  }
  DWORD written = 0U;
  const bool complete = ::WriteFile(file, payload.data(),
                                    static_cast<DWORD>(payload.size()), &written,
                                    nullptr) != 0 &&
                        written == static_cast<DWORD>(payload.size()) &&
                        ::FlushFileBuffers(file) != 0;
  ::CloseHandle(file);
  if (!complete) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to persist sealed handoff replay state");
  }
#else
  const int file = ::open(marker.c_str(),
                          O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                          S_IRUSR | S_IWUSR);
  if (file < 0) {
    return core::failure(core::ErrorCode::Conflict,
                         "Sealed update handoff was already consumed");
  }
  std::size_t offset = 0U;
  while (offset < payload.size()) {
    const auto count = ::write(file, payload.data() + offset,
                               payload.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      ::close(file);
      return core::failure(core::ErrorCode::IoError,
                           "Unable to persist sealed handoff replay state");
    }
    offset += static_cast<std::size_t>(count);
  }
  const bool synced = ::fsync(file) == 0;
  ::close(file);
  if (!synced) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to sync sealed handoff replay state");
  }
#endif
  return core::success();
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
  root.emplace("platform", handoff.platform);
  root.emplace("publisherKeyId", handoff.publisherKeyId);
  root.emplace("manifestSha256", handoff.manifestSha256);
  root.emplace("package", std::move(package));
  root.emplace("requiresExplicitUserAction", handoff.requiresExplicitUserAction);
  root.emplace("requiresInstallerRevalidation", handoff.requiresInstallerRevalidation);
  root.emplace("createdAt", handoff.createdAt);
  root.emplace("expiresAt", handoff.expiresAt);
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
                             "platform", "publisherKeyId",
                             "manifestSha256", "package",
                             "requiresExplicitUserAction",
                             "requiresInstallerRevalidation", "createdAt",
                             "expiresAt"},
                            "handoff");
  if (!keys) return core::Result<SealedInstallerHandoff>{keys.error()};
  auto schema = requiredInteger(root, "schemaVersion");
  auto purpose = requiredString(root, "purpose");
  auto candidate = requiredString(root, "candidateId");
  auto platform = requiredString(root, "platform");
  auto publisher = requiredString(root, "publisherKeyId");
  auto manifest = requiredString(root, "manifestSha256");
  auto explicitAction = requiredBool(root, "requiresExplicitUserAction");
  auto revalidation = requiredBool(root, "requiresInstallerRevalidation");
  auto createdAt = requiredString(root, "createdAt");
  auto expiresAt = requiredString(root, "expiresAt");
  const auto* package = root.find("package");
  if (!schema || !purpose || !candidate || !platform || !publisher || !manifest ||
      !explicitAction || !revalidation || !createdAt || !expiresAt ||
      package == nullptr) {
    if (!schema) return core::Result<SealedInstallerHandoff>{schema.error()};
    if (!purpose) return core::Result<SealedInstallerHandoff>{purpose.error()};
    if (!candidate) return core::Result<SealedInstallerHandoff>{candidate.error()};
    if (!platform) return core::Result<SealedInstallerHandoff>{platform.error()};
    if (!publisher) return core::Result<SealedInstallerHandoff>{publisher.error()};
    if (!manifest) return core::Result<SealedInstallerHandoff>{manifest.error()};
    if (!explicitAction) return core::Result<SealedInstallerHandoff>{explicitAction.error()};
    if (!revalidation) return core::Result<SealedInstallerHandoff>{revalidation.error()};
    if (!createdAt) return core::Result<SealedInstallerHandoff>{createdAt.error()};
    if (!expiresAt) return core::Result<SealedInstallerHandoff>{expiresAt.error()};
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
  if (schema.value() != 2 || purpose.value() != "sealed-installer-handoff" ||
      !safeFilename(fileName.value()) || fileName.value() !=
          std::filesystem::path{relativePath.value()}.filename().string() ||
      size.value() <= 0 || device.value() < 0 || inode.value() < 0 ||
      !hex64(sha256.value()) || !hex64(manifest.value()) ||
      !safeCandidateId(candidate.value()) || !safeFilename(platform.value()) ||
      !safeFilename(publisher.value()) || !utcTimestamp(createdAt.value()) ||
      !utcTimestamp(expiresAt.value()) || createdAt.value() >= expiresAt.value()) {
    return core::failure<SealedInstallerHandoff>(core::ErrorCode::ParseError,
                                                 "Handoff fields are invalid");
  }
  return SealedInstallerHandoff{
      .schemaVersion = schema.value(),
      .purpose = std::move(purpose).value(),
      .candidateId = std::move(candidate).value(),
      .platform = std::move(platform).value(),
      .publisherKeyId = std::move(publisher).value(),
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
      .createdAt = std::move(createdAt).value(),
      .expiresAt = std::move(expiresAt).value()};
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
  auto snapshot = inspectStableRegularFile(destination, manifest.package.size);
  if (!snapshot) {
    std::filesystem::remove_all(candidateRoot, error);
    return core::failure<SealedInstallerHandoff>(
        core::ErrorCode::Conflict,
        "Staged update package failed stable-handle verification");
  }
  if (snapshot.value().size != manifest.package.size ||
      snapshot.value().sha256 != manifest.package.sha256) {
    std::filesystem::remove_all(candidateRoot, error);
    return core::failure<SealedInstallerHandoff>(
        core::ErrorCode::Conflict, "Staged update package failed byte verification");
  }
  const SealedInstallerHandoff handoff{
      .schemaVersion = 2,
      .purpose = "sealed-installer-handoff",
      .candidateId = candidateId,
      .platform = manifest.platform,
      .publisherKeyId = manifest.signature.keyId,
      .manifestSha256 = updateManifestIdentity(manifest),
      .package = SealedUpdatePackage{
          .fileName = manifest.package.fileName,
          .relativePath = candidateId + "/" + manifest.package.fileName,
          .size = snapshot.value().size,
          .sha256 = snapshot.value().sha256,
          .device = snapshot.value().device,
          .inode = snapshot.value().inode},
      .requiresExplicitUserAction = true,
      .requiresInstallerRevalidation = true,
      .createdAt = manifest.issuedAt,
      .expiresAt = manifest.expiresAt};
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
    const std::filesystem::path& stagingRoot,
    const InstallerHandoffVerificationOptions& options) {
  if (handoff.schemaVersion != 2 || handoff.purpose != "sealed-installer-handoff" ||
      !handoff.requiresExplicitUserAction ||
      !handoff.requiresInstallerRevalidation ||
      handoff.platform != manifest.platform ||
      handoff.publisherKeyId != manifest.signature.keyId ||
      handoff.createdAt != manifest.issuedAt ||
      handoff.expiresAt != manifest.expiresAt ||
      handoff.manifestSha256 != updateManifestIdentity(manifest) ||
      handoff.package.fileName != manifest.package.fileName ||
      handoff.package.size != manifest.package.size ||
      handoff.package.sha256 != manifest.package.sha256 ||
      !safeCandidateId(handoff.candidateId) || handoff.candidateId.empty() ||
      !hex64(handoff.manifestSha256) || !hex64(handoff.package.sha256)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Sealed update handoff identity is invalid");
  }
  if ((!options.expectedCandidateId.empty() &&
       handoff.candidateId != options.expectedCandidateId) ||
      (!options.expectedPlatform.empty() &&
       handoff.platform != options.expectedPlatform) ||
      (!options.expectedPublisherKeyId.empty() &&
       handoff.publisherKeyId != options.expectedPublisherKeyId)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Sealed update handoff expectation differs");
  }
  if (!options.now.empty() &&
      (!utcTimestamp(options.now) || options.now < handoff.createdAt ||
       options.now >= handoff.expiresAt)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Sealed update handoff is not current");
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
  auto root = validateExistingRoot(stagingRoot);
  if (!root) return root;
  const auto candidateRoot = stagingRoot / handoff.candidateId;
  const auto candidateStatus = std::filesystem::symlink_status(candidateRoot);
  if (std::filesystem::is_symlink(candidateStatus) ||
      !std::filesystem::is_directory(candidateStatus)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Sealed update candidate root is unsafe");
  }
  const auto packagePath = stagingRoot / handoff.package.relativePath;
  if (packagePath.filename().string() != handoff.package.fileName) {
    return core::failure(core::ErrorCode::Conflict,
                         "Sealed update package path is unsafe");
  }
  auto snapshot = inspectStableRegularFile(packagePath, manifest.package.size);
  if (!snapshot) {
    return core::failure(core::ErrorCode::Conflict,
                         "Sealed update package path is unsafe");
  }
  if (snapshot.value().size != manifest.package.size ||
      snapshot.value().sha256 != manifest.package.sha256) {
    return core::failure(core::ErrorCode::Conflict,
                         "Sealed update package bytes changed");
  }
  if (snapshot.value().device != handoff.package.device ||
      snapshot.value().inode != handoff.package.inode) {
    return core::failure(core::ErrorCode::Conflict,
                         "Sealed update package file identity changed");
  }
  return consumeHandoff(handoff, options);
}

}
