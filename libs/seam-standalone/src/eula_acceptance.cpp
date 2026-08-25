#include "seam/standalone/eula_acceptance.hpp"

#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <system_error>
#include <utility>

namespace seam::standalone {
namespace {

constexpr std::uint64_t kMaximumAcceptanceBytes = 16U * 1024U;

bool validDocumentVersion(std::string_view value) noexcept {
  if (value.empty() || value.size() > 128U) return false;
  return std::all_of(value.begin(), value.end(), [](const char character) {
    return character >= 0x21 && character <= 0x7e;
  });
}

bool validSha256(std::string_view value) noexcept {
  return value.size() == 64U &&
         std::all_of(value.begin(), value.end(), [](const char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool validUtcTimestamp(std::string_view value) noexcept {
  if (value.size() != 20U || value[4] != '-' || value[7] != '-' ||
      value[10] != 'T' || value[13] != ':' || value[16] != ':' ||
      value[19] != 'Z') {
    return false;
  }
  for (std::size_t index = 0U; index < value.size(); ++index) {
    if (index == 4U || index == 7U || index == 10U || index == 13U ||
        index == 16U || index == 19U) {
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(value[index])) == 0) {
      return false;
    }
  }
  return true;
}

bool validRecord(const EulaAcceptanceRecord& record) noexcept {
  return validDocumentVersion(record.documentVersion) &&
         validSha256(record.documentSha256) &&
         validUtcTimestamp(record.acceptedAtUtc);
}

core::Result<EulaAcceptanceRecord> parseRecord(
    const formats::JsonValue& root) {
  if (!root.isObject() || root.asObject().size() != 3U) {
    return core::failure<EulaAcceptanceRecord>(
        core::ErrorCode::ParseError,
        "EULA acceptance state must contain exactly three fields");
  }
  const auto* version = root.find("documentVersion");
  const auto* digest = root.find("documentSha256");
  const auto* accepted = root.find("acceptedAtUtc");
  if (version == nullptr || digest == nullptr || accepted == nullptr ||
      !version->isString() || !digest->isString() || !accepted->isString()) {
    return core::failure<EulaAcceptanceRecord>(
        core::ErrorCode::ParseError, "EULA acceptance fields are invalid");
  }
  EulaAcceptanceRecord record{
      .documentVersion = version->asString(),
      .documentSha256 = digest->asString(),
      .acceptedAtUtc = accepted->asString(),
  };
  if (!validRecord(record)) {
    return core::failure<EulaAcceptanceRecord>(
        core::ErrorCode::ParseError, "EULA acceptance values are invalid");
  }
  return record;
}

}

core::Result<std::optional<EulaAcceptanceRecord>> EulaAcceptanceStore::load(
    const std::filesystem::path& path) {
  if (path.empty()) {
    return core::failure<std::optional<EulaAcceptanceRecord>>(
        core::ErrorCode::InvalidArgument,
        "EULA acceptance state path cannot be empty");
  }
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    if (error) {
      return core::failure<std::optional<EulaAcceptanceRecord>>(
          core::ErrorCode::IoError, "Unable to inspect EULA acceptance state",
          error.message());
    }
    return std::optional<EulaAcceptanceRecord>{};
  }
  const auto status = std::filesystem::symlink_status(path, error);
  if (error || std::filesystem::is_symlink(status) ||
      !std::filesystem::is_regular_file(status)) {
    return core::failure<std::optional<EulaAcceptanceRecord>>(
        core::ErrorCode::Conflict,
        "EULA acceptance state must be a regular file", path.string());
  }
  auto text = core::readTextFileLimited(path, kMaximumAcceptanceBytes);
  if (!text) return core::Result<std::optional<EulaAcceptanceRecord>>{text.error()};
  auto parsed = formats::parseJson(text.value(), formats::JsonParseLimits{
      .maximumInputBytes = static_cast<std::size_t>(kMaximumAcceptanceBytes),
      .maximumDepth = 4U,
      .maximumNodes = 16U,
      .maximumStringBytes = 4096U,
      .maximumCollectionEntries = 8U});
  if (!parsed) return core::Result<std::optional<EulaAcceptanceRecord>>{parsed.error()};
  auto record = parseRecord(parsed.value());
  if (!record) return core::Result<std::optional<EulaAcceptanceRecord>>{record.error()};
  return std::optional<EulaAcceptanceRecord>{std::move(record).value()};
}

core::Result<void> EulaAcceptanceStore::save(
    const std::filesystem::path& path, const EulaAcceptanceRecord& record) {
  if (path.empty() || !validRecord(record)) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "EULA acceptance record is invalid");
  }
  formats::JsonValue::Object value;
  value.emplace("documentVersion", record.documentVersion);
  value.emplace("documentSha256", record.documentSha256);
  value.emplace("acceptedAtUtc", record.acceptedAtUtc);
  return core::durableAtomicWriteText(
      path, formats::stringifyJson(formats::JsonValue{std::move(value)}, true));
}

bool EulaAcceptanceStore::matches(const EulaAcceptanceRecord& record,
                                  std::string_view documentVersion,
                                  std::string_view documentSha256) noexcept {
  return validRecord(record) && record.documentVersion == documentVersion &&
         record.documentSha256 == documentSha256;
}

}
