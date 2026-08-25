#include "seam/authoring/support_bundle.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <span>
#include <system_error>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace seam::authoring {
namespace {

using formats::JsonValue;

constexpr std::size_t kMaximumEvents = 256U;
constexpr std::size_t kMaximumAttachments = 8U;
constexpr std::uint64_t kMaximumEntryBytes = 1024U * 1024U;
constexpr std::uint64_t kMaximumArchiveBytes = 8U * 1024U * 1024U;

std::string timestampNow() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

bool safeIdentifier(std::string_view value) noexcept {
  if (value.empty() || value.size() > 128U ||
      std::isalnum(static_cast<unsigned char>(value.front())) == 0) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](char character) {
    const auto byte = static_cast<unsigned char>(character);
    return std::isalnum(byte) != 0 || character == '.' || character == '_' ||
           character == '-' || character == ':';
  });
}

bool safeValue(std::string_view value, std::size_t maximum = 160U) noexcept {
  if (value.empty() || value.size() > maximum) return false;
  return std::all_of(value.begin(), value.end(), [](char character) {
    const auto byte = static_cast<unsigned char>(character);
    return byte >= 0x20U && byte != 0x7fU && character != '/' &&
           character != '\\';
  });
}

bool lowercaseHex(std::string_view value) noexcept {
  if (value.size() < 16U || value.size() > 128U) return false;
  return std::all_of(value.begin(), value.end(), [](char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
  });
}

bool exportField(std::string_view key) noexcept {
  constexpr std::array<std::string_view, 21U> names{
      "buildId",          "sourceCommit",       "artifactId",
      "artifactSha256",   "bankId",             "bankVersion",
      "bankContentHash",  "osFamily",           "osMajor",
      "hostFamily",       "hostMajor",          "deviceFamily",
      "sampleRate",       "bufferFrames",       "channels",
      "diagnosticCount",  "xrunCount",          "renderCount",
      "recoveryState",    "manifestVersion",    "sanitizedStackSymbols"};
  return std::find(names.begin(), names.end(), key) != names.end();
}

core::Result<void> ensurePrivateRoot(const std::filesystem::path& root) {
  if (root.empty() || root == root.root_path()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Support bundle private root is invalid");
  }
  std::error_code error;
  if (std::filesystem::exists(root, error)) {
    const auto status = std::filesystem::symlink_status(root, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
      return core::failure(core::ErrorCode::Conflict,
                           "Support bundle private root must be a real directory",
                           root.string());
    }
  } else {
    std::filesystem::create_directories(root, error);
    if (error) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to create support bundle private root",
                           error.message());
    }
  }
#ifndef _WIN32
  if (::chmod(root.c_str(), S_IRWXU) != 0) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to restrict support bundle private root",
                         root.string());
  }
#endif
  return core::success();
}

JsonValue eventJson(const core::LogEvent& event, std::string_view createdAt) {
  JsonValue::Object fields;
  for (const auto& field : event.fields) {
    if ((field.privacy != core::LogPrivacyClass::PublicTechnical &&
         field.privacy != core::LogPrivacyClass::ExportSafe) ||
        !exportField(field.key) || !safeValue(field.value) ||
        ((field.key.ends_with("Sha256") || field.key.ends_with("Hash") ||
          field.key == "sourceCommit") && !lowercaseHex(field.value))) {
      continue;
    }
    fields.emplace(field.key, field.value);
  }
  JsonValue::Object result;
  result.emplace("code", event.code);
  result.emplace("severity", std::string{core::toString(event.level)});
  result.emplace("occurredAt", std::string{createdAt});
  result.emplace("occurrenceCount", static_cast<std::int64_t>(event.occurrenceCount));
  result.emplace("fields", std::move(fields));
  return JsonValue{std::move(result)};
}

struct BundleEntry final {
  std::string name;
  std::vector<std::byte> bytes;
};

struct BundleData final {
  std::vector<std::byte> archive;
  SupportBundlePreview preview;
};

std::uint32_t crc32(std::span<const std::byte> bytes) noexcept {
  std::uint32_t crc = 0xffffffffU;
  for (const auto byte : bytes) {
    crc ^= std::to_integer<unsigned char>(byte);
    for (unsigned bit = 0U; bit < 8U; ++bit) {
      crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xedb88320U : crc >> 1U;
    }
  }
  return ~crc;
}

void appendU16(std::vector<std::byte>& output, std::uint16_t value) {
  output.push_back(static_cast<std::byte>(value & 0xffU));
  output.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void appendU32(std::vector<std::byte>& output, std::uint32_t value) {
  for (unsigned shift = 0U; shift < 32U; shift += 8U) {
    output.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
  }
}

void appendText(std::vector<std::byte>& output, std::string_view value) {
  const auto bytes = std::as_bytes(std::span{value.data(), value.size()});
  output.insert(output.end(), bytes.begin(), bytes.end());
}

std::vector<std::byte> zipStored(const std::vector<BundleEntry>& entries) {
  std::vector<std::byte> output;
  std::vector<std::uint32_t> offsets;
  offsets.reserve(entries.size());
  for (const auto& entry : entries) {
    offsets.push_back(static_cast<std::uint32_t>(output.size()));
    const auto checksum = crc32(entry.bytes);
    appendU32(output, 0x04034b50U);
    appendU16(output, 20U);
    appendU16(output, 0U);
    appendU16(output, 0U);
    appendU16(output, 0U);
    appendU16(output, 0U);
    appendU32(output, checksum);
    appendU32(output, static_cast<std::uint32_t>(entry.bytes.size()));
    appendU32(output, static_cast<std::uint32_t>(entry.bytes.size()));
    appendU16(output, static_cast<std::uint16_t>(entry.name.size()));
    appendU16(output, 0U);
    appendText(output, entry.name);
    output.insert(output.end(), entry.bytes.begin(), entry.bytes.end());
  }
  const auto centralOffset = static_cast<std::uint32_t>(output.size());
  for (std::size_t index = 0U; index < entries.size(); ++index) {
    const auto& entry = entries[index];
    const auto checksum = crc32(entry.bytes);
    appendU32(output, 0x02014b50U);
    appendU16(output, 20U);
    appendU16(output, 20U);
    appendU16(output, 0U);
    appendU16(output, 0U);
    appendU16(output, 0U);
    appendU16(output, 0U);
    appendU32(output, checksum);
    appendU32(output, static_cast<std::uint32_t>(entry.bytes.size()));
    appendU32(output, static_cast<std::uint32_t>(entry.bytes.size()));
    appendU16(output, static_cast<std::uint16_t>(entry.name.size()));
    appendU16(output, 0U);
    appendU16(output, 0U);
    appendU16(output, 0U);
    appendU16(output, 0U);
    appendU32(output, 0100644U << 16U);
    appendU32(output, offsets[index]);
    appendText(output, entry.name);
  }
  const auto centralSize = static_cast<std::uint32_t>(output.size()) - centralOffset;
  appendU32(output, 0x06054b50U);
  appendU16(output, 0U);
  appendU16(output, 0U);
  appendU16(output, static_cast<std::uint16_t>(entries.size()));
  appendU16(output, static_cast<std::uint16_t>(entries.size()));
  appendU32(output, centralSize);
  appendU32(output, centralOffset);
  appendU16(output, 0U);
  return output;
}

core::Result<BundleData> buildBundle(const SupportBundleRequest& request) {
  if (request.events.size() > kMaximumEvents ||
      (request.attachments.size() > 0U && !request.attachmentConsent) ||
      request.attachments.size() > kMaximumAttachments) {
    return core::failure<BundleData>(core::ErrorCode::InvalidArgument,
                                     "Support bundle request exceeds its consent or count policy");
  }
  const auto createdAt = request.createdAt.empty() ? timestampNow() : request.createdAt;
  std::vector<BundleEntry> entries;
  JsonValue::Array events;
  events.reserve(request.events.size());
  for (const auto& event : request.events) {
    if (!core::isValidLogEvent(event) || !safeIdentifier(event.code)) {
      return core::failure<BundleData>(core::ErrorCode::InvalidArgument,
                                       "Support bundle contains an invalid diagnostic event");
    }
    events.emplace_back(eventJson(event, createdAt));
  }
  JsonValue::Object diagnostics;
  diagnostics.emplace("schemaVersion", static_cast<std::int64_t>(1));
  diagnostics.emplace("events", std::move(events));
  const auto diagnosticsText = formats::stringifyJson(JsonValue{std::move(diagnostics)}, false);
  if (diagnosticsText.size() > kMaximumEntryBytes) {
    return core::failure<BundleData>(core::ErrorCode::Unsupported,
                                     "Support diagnostics exceed the entry limit");
  }
  entries.push_back(BundleEntry{
      .name = "diagnostics.json",
      .bytes = std::vector<std::byte>(
          std::as_bytes(std::span{diagnosticsText.data(), diagnosticsText.size()}).begin(),
          std::as_bytes(std::span{diagnosticsText.data(), diagnosticsText.size()}).end())});
  for (const auto& attachment : request.attachments) {
    const auto name = attachment.filename().string();
    if (!safeIdentifier(name) || name.find('.') == std::string::npos) {
      return core::failure<BundleData>(core::ErrorCode::InvalidArgument,
                                       "Support attachment name is not portable", name);
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(attachment, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status)) {
      return core::failure<BundleData>(core::ErrorCode::Conflict,
                                       "Support attachment must be a regular file", name);
    }
    auto bytes = core::readFileBytesLimited(attachment, kMaximumEntryBytes);
    if (!bytes) return core::Result<BundleData>{bytes.error()};
    if (std::find_if(entries.begin(), entries.end(), [&](const BundleEntry& entry) {
          return entry.name == "attachments/" + name;
        }) != entries.end()) {
      return core::failure<BundleData>(core::ErrorCode::Conflict,
                                       "Support attachment names must be unique", name);
    }
    entries.push_back(BundleEntry{.name = "attachments/" + name,
                                  .bytes = std::move(bytes).value()});
  }
  JsonValue::Array manifestEntries;
  for (const auto& entry : entries) {
    JsonValue::Object item;
    item.emplace("path", entry.name);
    item.emplace("size", static_cast<std::int64_t>(entry.bytes.size()));
    item.emplace("sha256", core::sha256Hex(entry.bytes));
    manifestEntries.emplace_back(std::move(item));
  }
  JsonValue::Object manifest;
  manifest.emplace("schemaVersion", static_cast<std::int64_t>(1));
  manifest.emplace("purpose", "export-safe-support-bundle");
  manifest.emplace("privacyClass", "ExportSafe");
  manifest.emplace("createdAt", createdAt);
  manifest.emplace("consent", request.attachmentConsent);
  manifest.emplace("entries", std::move(manifestEntries));
  const auto manifestText = formats::stringifyJson(JsonValue{std::move(manifest)}, false);
  entries.insert(entries.begin(), BundleEntry{
      .name = "manifest.json",
      .bytes = std::vector<std::byte>(
          std::as_bytes(std::span{manifestText.data(), manifestText.size()}).begin(),
          std::as_bytes(std::span{manifestText.data(), manifestText.size()}).end())});
  auto archive = zipStored(entries);
  if (archive.size() > kMaximumArchiveBytes) {
    return core::failure<BundleData>(core::ErrorCode::Unsupported,
                                     "Support bundle exceeds the archive limit");
  }
  SupportBundlePreview preview{
      .archiveBytes = archive.size(),
      .archiveSha256 = core::sha256Hex(archive),
      .entryNames = {},
      .attachmentConsent = request.attachmentConsent};
  preview.entryNames.reserve(entries.size());
  for (const auto& entry : entries) preview.entryNames.push_back(entry.name);
  return BundleData{.archive = std::move(archive), .preview = std::move(preview)};
}

}

core::Result<SupportBundlePreview> SupportBundleService::preview(
    const SupportBundleRequest& request) const {
  auto bundle = buildBundle(request);
  if (!bundle) return core::Result<SupportBundlePreview>{bundle.error()};
  return bundle.value().preview;
}

core::Result<SupportBundleExport> SupportBundleService::exportBundle(
    const SupportBundleRequest& request,
    const std::filesystem::path& destination) const {
  auto bundle = buildBundle(request);
  if (!bundle) return core::Result<SupportBundleExport>{bundle.error()};
  if (destination.empty()) {
    return core::failure<SupportBundleExport>(core::ErrorCode::InvalidArgument,
                                              "Support bundle destination is empty");
  }
  std::error_code error;
  const bool destinationExists = std::filesystem::exists(destination, error);
  if (error && error != std::errc::no_such_file_or_directory) {
    return core::failure<SupportBundleExport>(core::ErrorCode::IoError,
                                              "Unable to inspect support bundle destination",
                                              error.message());
  }
  error.clear();
  const auto destinationStatus = std::filesystem::symlink_status(destination, error);
  if (error == std::errc::no_such_file_or_directory) error.clear();
  if (destinationExists || std::filesystem::is_symlink(destinationStatus)) {
    return core::failure<SupportBundleExport>(core::ErrorCode::Conflict,
                                              "Support bundle destination already exists",
                                              destination.string());
  }
  if (error) {
    return core::failure<SupportBundleExport>(core::ErrorCode::IoError,
                                              "Unable to inspect support bundle destination",
                                              error.message());
  }
  const auto parent = destination.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, error);
    if (error) {
      return core::failure<SupportBundleExport>(core::ErrorCode::IoError,
                                                "Unable to create support bundle directory",
                                                error.message());
    }
    const auto status = std::filesystem::symlink_status(parent, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
      return core::failure<SupportBundleExport>(core::ErrorCode::Conflict,
                                                "Support bundle directory is unsafe",
                                                parent.string());
    }
  }
  auto written = core::durableAtomicWrite(destination, bundle.value().archive);
  if (!written) return core::Result<SupportBundleExport>{written.error()};
#ifndef _WIN32
  if (::chmod(destination.c_str(), S_IRUSR | S_IWUSR) != 0) {
    return core::failure<SupportBundleExport>(core::ErrorCode::IoError,
                                              "Unable to restrict support bundle permissions");
  }
#endif
  return SupportBundleExport{.destination = destination,
                             .preview = std::move(bundle.value().preview)};
}

core::Result<std::filesystem::path> SupportBundleService::writePrivateReport(
    std::string_view reportId, std::string_view payload) const {
  auto root = ensurePrivateRoot(privateRoot_);
  if (!root) return core::Result<std::filesystem::path>{root.error()};
  if (!safeIdentifier(reportId) || payload.size() > kMaximumEntryBytes) {
    return core::failure<std::filesystem::path>(core::ErrorCode::InvalidArgument,
                                                "Private support report is invalid");
  }
  const auto path = privateRoot_ / (std::string{reportId} + ".json");
  std::error_code error;
  const bool pathExists = std::filesystem::exists(path, error);
  if (error && error != std::errc::no_such_file_or_directory) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::IoError, "Unable to inspect private support report",
        error.message());
  }
  error.clear();
  const auto pathStatus = std::filesystem::symlink_status(path, error);
  if (error == std::errc::no_such_file_or_directory) error.clear();
  if (pathExists || std::filesystem::is_symlink(pathStatus)) {
    return core::failure<std::filesystem::path>(core::ErrorCode::Conflict,
                                                "Private support report already exists",
                                                path.string());
  }
  auto written = core::durableAtomicWriteText(path, payload);
  if (!written) return core::Result<std::filesystem::path>{written.error()};
#ifndef _WIN32
  if (::chmod(path.c_str(), S_IRUSR | S_IWUSR) != 0) {
    return core::failure<std::filesystem::path>(core::ErrorCode::IoError,
                                                "Unable to restrict private report permissions");
  }
#endif
  return path;
}

core::Result<void> SupportBundleService::deletePrivateReport(
    const std::filesystem::path& path) const {
  auto root = ensurePrivateRoot(privateRoot_);
  if (!root) return root;
  const auto normalizedRoot = std::filesystem::absolute(privateRoot_).lexically_normal();
  const auto normalizedPath = std::filesystem::absolute(path).lexically_normal();
  if (normalizedPath.parent_path() != normalizedRoot ||
      !safeIdentifier(normalizedPath.stem().string())) {
    return core::failure(core::ErrorCode::Conflict,
                         "Private support report is outside the owned store",
                         path.string());
  }
  std::error_code error;
  const auto status = std::filesystem::symlink_status(normalizedPath, error);
  if (error && error != std::errc::no_such_file_or_directory) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to inspect private support report", error.message());
  }
  if (!std::filesystem::exists(status) || std::filesystem::is_symlink(status) ||
      !std::filesystem::is_regular_file(status)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Private support report is not an owned regular file",
                         path.string());
  }
  if (!std::filesystem::remove(normalizedPath, error) || error) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to delete private support report", error.message());
  }
  return core::success();
}

}
