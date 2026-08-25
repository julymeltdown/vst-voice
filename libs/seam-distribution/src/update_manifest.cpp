#include "seam/distribution/update_manifest.hpp"

#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace seam::distribution {
namespace {

using formats::JsonValue;

constexpr std::uint64_t kMaximumPackageBytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaximumMetadataBytes = 256U * 1024U;

int base64Value(char value) noexcept {
  if (value >= 'A' && value <= 'Z') return value - 'A';
  if (value >= 'a' && value <= 'z') return value - 'a' + 26;
  if (value >= '0' && value <= '9') return value - '0' + 52;
  if (value == '+') return 62;
  if (value == '/') return 63;
  return -1;
}

template <std::size_t Size>
core::Result<std::array<std::byte, Size>> decodeBase64(std::string_view text,
                                                       std::string_view field) {
  if (text.empty() || text.size() % 4U != 0U) {
    return core::failure<std::array<std::byte, Size>>(
        core::ErrorCode::ParseError, "Base64 field has an invalid length",
        std::string{field});
  }
  std::array<std::byte, Size> result{};
  std::size_t output = 0U;
  for (std::size_t index = 0U; index < text.size(); index += 4U) {
    const auto first = base64Value(text[index]);
    const auto second = base64Value(text[index + 1U]);
    const bool paddedThird = text[index + 2U] == '=';
    const bool paddedFourth = text[index + 3U] == '=';
    const auto third = paddedThird ? 0 : base64Value(text[index + 2U]);
    const auto fourth = paddedFourth ? 0 : base64Value(text[index + 3U]);
    const bool finalGroup = index + 4U == text.size();
    if (first < 0 || second < 0 || third < 0 || fourth < 0 ||
        (paddedThird && !paddedFourth) ||
        ((paddedThird || paddedFourth) && !finalGroup)) {
      return core::failure<std::array<std::byte, Size>>(
          core::ErrorCode::ParseError, "Base64 field contains invalid characters",
          std::string{field});
    }
    const auto block = (static_cast<std::uint32_t>(first) << 18U) |
                       (static_cast<std::uint32_t>(second) << 12U) |
                       (static_cast<std::uint32_t>(third) << 6U) |
                       static_cast<std::uint32_t>(fourth);
    if (output >= result.size()) {
      return core::failure<std::array<std::byte, Size>>(
          core::ErrorCode::ParseError, "Base64 field decodes to too many bytes",
          std::string{field});
    }
    result[output++] = static_cast<std::byte>((block >> 16U) & 0xffU);
    if (!paddedThird) {
      if (output >= result.size()) {
        return core::failure<std::array<std::byte, Size>>(
            core::ErrorCode::ParseError, "Base64 field decodes to too many bytes",
            std::string{field});
      }
      result[output++] = static_cast<std::byte>((block >> 8U) & 0xffU);
    }
    if (!paddedFourth) {
      if (output >= result.size()) {
        return core::failure<std::array<std::byte, Size>>(
            core::ErrorCode::ParseError, "Base64 field decodes to too many bytes",
            std::string{field});
      }
      result[output++] = static_cast<std::byte>(block & 0xffU);
    }
  }
  if (output != result.size()) {
    return core::failure<std::array<std::byte, Size>>(
        core::ErrorCode::ParseError, "Base64 field decodes to the wrong size",
        std::string{field});
  }
  return result;
}

char base64Character(unsigned value) noexcept {
  constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  return alphabet[value & 0x3fU];
}

std::string encodeBase64(std::span<const std::byte> bytes) {
  std::string result;
  result.reserve(((bytes.size() + 2U) / 3U) * 4U);
  for (std::size_t index = 0U; index < bytes.size(); index += 3U) {
    const auto remaining = bytes.size() - index;
    const auto first = std::to_integer<unsigned>(bytes[index]);
    const auto second = remaining > 1U
                            ? std::to_integer<unsigned>(bytes[index + 1U])
                            : 0U;
    const auto third = remaining > 2U
                           ? std::to_integer<unsigned>(bytes[index + 2U])
                           : 0U;
    const auto block = (first << 16U) | (second << 8U) | third;
    result.push_back(base64Character(block >> 18U));
    result.push_back(base64Character(block >> 12U));
    result.push_back(remaining > 1U ? base64Character(block >> 6U) : '=');
    result.push_back(remaining > 2U ? base64Character(block) : '=');
  }
  return result;
}

bool isKeyId(std::string_view value) noexcept {
  if (value.empty() || value.size() > 96U ||
      std::isalnum(static_cast<unsigned char>(value.front())) == 0) {
    return false;
  }
  return std::all_of(value.begin() + 1, value.end(), [](char character) {
    const auto byte = static_cast<unsigned char>(character);
    return std::isalnum(byte) != 0 || character == '.' || character == '_' ||
           character == '-';
  });
}

bool isLowerHex64(std::string_view value) noexcept {
  if (value.size() != 64U) return false;
  return std::all_of(value.begin(), value.end(), [](char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f');
  });
}

bool isTimestamp(std::string_view value) noexcept {
  if (value.size() < 20U || value[4] != '-' || value[7] != '-' ||
      value[10] != 'T' || value[13] != ':' || value[16] != ':') {
    return false;
  }
  const auto digit = [](char character) {
    return character >= '0' && character <= '9';
  };
  for (std::size_t index : {0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U, 11U, 12U,
                            14U, 15U, 17U, 18U}) {
    if (!digit(value[index])) return false;
  }
  if (value.back() == 'Z') return true;
  if (value.size() < 25U) return false;
  const auto offset = value.size() - 6U;
  return (value[offset] == '+' || value[offset] == '-') &&
         value[offset + 3U] == ':' && digit(value[offset + 1U]) &&
         digit(value[offset + 2U]) && digit(value[offset + 4U]) &&
         digit(value[offset + 5U]);
}

bool isSafeFilename(std::string_view value) noexcept {
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

bool isHttpsUrl(std::string_view value) noexcept {
  if (value.size() <= 8U || !value.starts_with("https://")) return false;
  return std::none_of(value.begin(), value.end(), [](char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
  });
}

bool isPlatform(std::string_view value) noexcept {
  return value == "macos-arm64" || value == "windows-x64" || value == "linux-x64";
}

bool isAllowedRange(std::string_view value) noexcept {
  return value == "project" || value == "media" || value == "bank" ||
         value == "settings" || value == "autosave" || value == "clap-state" ||
         value == "host-state";
}

bool isAllowedKey(std::string_view key,
                  std::initializer_list<std::string_view> allowed) noexcept {
  return std::find(allowed.begin(), allowed.end(), key) != allowed.end();
}

core::Result<void> rejectUnknown(const JsonValue& value,
                                 std::initializer_list<std::string_view> allowed,
                                 std::string_view context) {
  if (!value.isObject()) {
    return core::failure(core::ErrorCode::ParseError,
                         "Metadata value must be an object", std::string{context});
  }
  for (const auto& [key, unused] : value.asObject()) {
    static_cast<void>(unused);
    if (!isAllowedKey(key, allowed)) {
      return core::failure(core::ErrorCode::ParseError,
                           "Metadata contains an unknown field",
                           std::string{context} + "." + key);
    }
  }
  return core::success();
}

core::Result<std::string> requiredString(const JsonValue& root,
                                         std::string_view key) {
  const auto* value = root.find(key);
  if (value == nullptr || !value->isString() || value->asString().empty()) {
    return core::failure<std::string>(core::ErrorCode::ParseError,
                                      "Metadata field must be a non-empty string",
                                      std::string{key});
  }
  return value->asString();
}

core::Result<std::int64_t> requiredInteger(const JsonValue& root,
                                           std::string_view key) {
  const auto* value = root.find(key);
  if (value == nullptr || !value->isInteger()) {
    return core::failure<std::int64_t>(core::ErrorCode::ParseError,
                                       "Metadata field must be an integer",
                                       std::string{key});
  }
  return value->asInt64();
}

core::Result<UpdateSignature> parseSignature(const JsonValue& value,
                                             std::string_view context) {
  auto keys = rejectUnknown(value, {"algorithm", "keyId", "payloadSha256", "value"},
                            context);
  if (!keys) return core::Result<UpdateSignature>{keys.error()};
  auto algorithm = requiredString(value, "algorithm");
  auto keyId = requiredString(value, "keyId");
  auto payloadSha256 = requiredString(value, "payloadSha256");
  auto encoded = requiredString(value, "value");
  if (!algorithm) return core::Result<UpdateSignature>{algorithm.error()};
  if (!keyId) return core::Result<UpdateSignature>{keyId.error()};
  if (!payloadSha256) return core::Result<UpdateSignature>{payloadSha256.error()};
  if (!encoded) return core::Result<UpdateSignature>{encoded.error()};
  if (algorithm.value() != "Ed25519" || !isKeyId(keyId.value()) ||
      !isLowerHex64(payloadSha256.value())) {
    return core::failure<UpdateSignature>(core::ErrorCode::ParseError,
                                          "Metadata signature fields are invalid",
                                          std::string{context});
  }
  auto decoded = decodeBase64<64U>(encoded.value(), std::string{context} + ".value");
  if (!decoded) return core::Result<UpdateSignature>{decoded.error()};
  return UpdateSignature{.algorithm = std::move(algorithm).value(),
                         .keyId = std::move(keyId).value(),
                         .payloadSha256 = std::move(payloadSha256).value(),
                         .value = decoded.value()};
}

JsonValue signatureJson(const UpdateSignature& signature) {
  JsonValue::Object value;
  value.emplace("algorithm", signature.algorithm);
  value.emplace("keyId", signature.keyId);
  value.emplace("payloadSha256", signature.payloadSha256);
  value.emplace("value", encodeBase64(signature.value));
  return JsonValue{std::move(value)};
}

JsonValue rangesJson(const std::map<std::string, UpdateRange, std::less<>>& ranges) {
  JsonValue::Object object;
  for (const auto& [name, range] : ranges) {
    JsonValue::Object value;
    value.emplace("min", static_cast<std::int64_t>(range.min));
    value.emplace("max", static_cast<std::int64_t>(range.max));
    object.emplace(name, std::move(value));
  }
  return JsonValue{std::move(object)};
}

JsonValue manifestJson(const UpdateManifest& manifest, bool includeSignature) {
  JsonValue::Object root;
  root.emplace("schemaVersion", manifest.schemaVersion);
  root.emplace("purpose", manifest.purpose);
  root.emplace("channel", manifest.channel);
  root.emplace("manifestId", manifest.manifestId);
  root.emplace("manifestEpoch", static_cast<std::int64_t>(manifest.manifestEpoch));
  root.emplace("platform", manifest.platform);
  root.emplace("targetBuild", manifest.targetBuild);
  root.emplace("targetVersion", manifest.targetVersion);
  root.emplace("minimumVersion", manifest.minimumVersion);
  root.emplace("issuedAt", manifest.issuedAt);
  root.emplace("expiresAt", manifest.expiresAt);
  root.emplace("readRanges", rangesJson(manifest.readRanges));
  root.emplace("writeRanges", rangesJson(manifest.writeRanges));
  root.emplace("downgradePolicy", manifest.downgradePolicy);
  JsonValue::Object package;
  package.emplace("fileName", manifest.package.fileName);
  package.emplace("url", manifest.package.url);
  package.emplace("size", static_cast<std::int64_t>(manifest.package.size));
  package.emplace("sha256", manifest.package.sha256);
  root.emplace("package", std::move(package));
  root.emplace("releaseNotesSha256", manifest.releaseNotesSha256);
  if (manifest.recoveryAuthorization.has_value()) {
    auto recovery = manifest.recoveryAuthorization->additionalFields;
    recovery.emplace("purpose", manifest.recoveryAuthorization->purpose);
    recovery.emplace("signature", signatureJson(manifest.recoveryAuthorization->signature));
    root.emplace("recoveryAuthorization", std::move(recovery));
  }
  if (includeSignature) root.emplace("signature", signatureJson(manifest.signature));
  return JsonValue{std::move(root)};
}

core::Result<std::map<std::string, UpdateRange, std::less<>>>
parseRanges(const JsonValue& value, std::string_view context) {
  if (!value.isObject()) {
    return core::failure<std::map<std::string, UpdateRange, std::less<>>>(
        core::ErrorCode::ParseError, "Update range collection must be an object",
        std::string{context});
  }
  std::map<std::string, UpdateRange, std::less<>> ranges;
  for (const auto& [name, range] : value.asObject()) {
    if (!isAllowedRange(name)) {
      return core::failure<std::map<std::string, UpdateRange, std::less<>>>(
          core::ErrorCode::ParseError, "Update range field is not supported",
          std::string{context} + "." + name);
    }
    auto rangeKeys = rejectUnknown(range, {"min", "max"}, std::string{context} + "." + name);
    if (!rangeKeys) {
      return core::Result<std::map<std::string, UpdateRange, std::less<>>>{rangeKeys.error()};
    }
    auto minimum = requiredInteger(range, "min");
    auto maximum = requiredInteger(range, "max");
    if (!minimum) return core::Result<std::map<std::string, UpdateRange, std::less<>>>{minimum.error()};
    if (!maximum) return core::Result<std::map<std::string, UpdateRange, std::less<>>>{maximum.error()};
    if (minimum.value() < 0 || maximum.value() < 0 || minimum.value() > maximum.value()) {
      return core::failure<std::map<std::string, UpdateRange, std::less<>>>(
          core::ErrorCode::ParseError, "Update range bounds are invalid",
          std::string{context} + "." + name);
    }
    ranges.emplace(name, UpdateRange{
                              .min = static_cast<std::uint64_t>(minimum.value()),
                              .max = static_cast<std::uint64_t>(maximum.value())});
  }
  return ranges;
}

struct Semver final {
  std::uint64_t major{0};
  std::uint64_t minor{0};
  std::uint64_t patch{0};
  std::vector<std::string_view> prerelease;
};

std::optional<std::uint64_t> parseUnsigned(std::string_view value) {
  if (value.empty() || (value.size() > 1U && value.front() == '0')) return std::nullopt;
  std::uint64_t result = 0U;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
    return std::nullopt;
  }
  return result;
}

std::optional<Semver> parseSemver(std::string_view value) {
  const auto dash = value.find('-');
  const auto core = value.substr(0U, dash);
  const auto first = core.find('.');
  if (first == std::string_view::npos) return std::nullopt;
  const auto second = core.find('.', first + 1U);
  if (second == std::string_view::npos || core.find('.', second + 1U) != std::string_view::npos) {
    return std::nullopt;
  }
  auto major = parseUnsigned(core.substr(0U, first));
  auto minor = parseUnsigned(core.substr(first + 1U, second - first - 1U));
  auto patch = parseUnsigned(core.substr(second + 1U));
  if (!major || !minor || !patch) return std::nullopt;
  Semver result{
      .major = *major,
      .minor = *minor,
      .patch = *patch,
      .prerelease = {},
  };
  if (dash != std::string_view::npos) {
    const auto prerelease = value.substr(dash + 1U);
    if (prerelease.empty()) return std::nullopt;
    std::size_t start = 0U;
    while (start < prerelease.size()) {
      const auto end = prerelease.find('.', start);
      const auto token = prerelease.substr(start, end == std::string_view::npos
                                                   ? prerelease.size() - start
                                                   : end - start);
      if (token.empty() || (token.size() > 1U && token.front() == '0') ||
          std::any_of(token.begin(), token.end(), [](char character) {
            const auto byte = static_cast<unsigned char>(character);
            return std::isalnum(byte) == 0 && character != '-';
          })) {
        return std::nullopt;
      }
      result.prerelease.push_back(token);
      if (end == std::string_view::npos) break;
      start = end + 1U;
    }
  }
  return result;
}

int compareSemver(const Semver& lhs, const Semver& rhs) noexcept {
  if (lhs.major != rhs.major) return lhs.major < rhs.major ? -1 : 1;
  if (lhs.minor != rhs.minor) return lhs.minor < rhs.minor ? -1 : 1;
  if (lhs.patch != rhs.patch) return lhs.patch < rhs.patch ? -1 : 1;
  if (lhs.prerelease.empty() && rhs.prerelease.empty()) return 0;
  if (lhs.prerelease.empty()) return 1;
  if (rhs.prerelease.empty()) return -1;
  const auto count = std::min(lhs.prerelease.size(), rhs.prerelease.size());
  for (std::size_t index = 0U; index < count; ++index) {
    const auto leftNumber = parseUnsigned(lhs.prerelease[index]);
    const auto rightNumber = parseUnsigned(rhs.prerelease[index]);
    if (leftNumber && rightNumber && *leftNumber != *rightNumber) {
      return *leftNumber < *rightNumber ? -1 : 1;
    }
    if (leftNumber && !rightNumber) return -1;
    if (!leftNumber && rightNumber) return 1;
    if (lhs.prerelease[index] != rhs.prerelease[index]) {
      return lhs.prerelease[index] < rhs.prerelease[index] ? -1 : 1;
    }
  }
  if (lhs.prerelease.size() == rhs.prerelease.size()) return 0;
  return lhs.prerelease.size() < rhs.prerelease.size() ? -1 : 1;
}

core::Result<UpdateManifest> parseManifestRoot(const JsonValue& root) {
  auto keys = rejectUnknown(
      root, {"schemaVersion", "purpose", "channel", "manifestId", "manifestEpoch",
             "platform", "targetBuild", "targetVersion", "minimumVersion", "issuedAt",
             "expiresAt", "readRanges", "writeRanges", "downgradePolicy", "package",
             "releaseNotesSha256", "recoveryAuthorization", "signature"},
      "manifest");
  if (!keys) return core::Result<UpdateManifest>{keys.error()};
  auto schemaVersion = requiredInteger(root, "schemaVersion");
  auto purpose = requiredString(root, "purpose");
  auto channel = requiredString(root, "channel");
  auto manifestId = requiredString(root, "manifestId");
  auto epoch = requiredInteger(root, "manifestEpoch");
  auto platform = requiredString(root, "platform");
  auto targetBuild = requiredString(root, "targetBuild");
  auto targetVersion = requiredString(root, "targetVersion");
  auto minimumVersion = requiredString(root, "minimumVersion");
  auto issuedAt = requiredString(root, "issuedAt");
  auto expiresAt = requiredString(root, "expiresAt");
  auto downgradePolicy = requiredString(root, "downgradePolicy");
  auto releaseNotesSha256 = requiredString(root, "releaseNotesSha256");
  if (!schemaVersion) return core::Result<UpdateManifest>{schemaVersion.error()};
  if (!purpose) return core::Result<UpdateManifest>{purpose.error()};
  if (!channel) return core::Result<UpdateManifest>{channel.error()};
  if (!manifestId) return core::Result<UpdateManifest>{manifestId.error()};
  if (!epoch) return core::Result<UpdateManifest>{epoch.error()};
  if (!platform) return core::Result<UpdateManifest>{platform.error()};
  if (!targetBuild) return core::Result<UpdateManifest>{targetBuild.error()};
  if (!targetVersion) return core::Result<UpdateManifest>{targetVersion.error()};
  if (!minimumVersion) return core::Result<UpdateManifest>{minimumVersion.error()};
  if (!issuedAt) return core::Result<UpdateManifest>{issuedAt.error()};
  if (!expiresAt) return core::Result<UpdateManifest>{expiresAt.error()};
  if (!downgradePolicy) return core::Result<UpdateManifest>{downgradePolicy.error()};
  if (!releaseNotesSha256) return core::Result<UpdateManifest>{releaseNotesSha256.error()};
  if (schemaVersion.value() != 1 || epoch.value() < 0) {
    return core::failure<UpdateManifest>(core::ErrorCode::ParseError,
                                         "Update manifest version or epoch is invalid");
  }
  const auto* readRanges = root.find("readRanges");
  const auto* writeRanges = root.find("writeRanges");
  const auto* package = root.find("package");
  const auto* signature = root.find("signature");
  if (readRanges == nullptr || writeRanges == nullptr || package == nullptr || signature == nullptr) {
    return core::failure<UpdateManifest>(core::ErrorCode::ParseError,
                                         "Update manifest has a missing required object");
  }
  auto parsedReadRanges = parseRanges(*readRanges, "readRanges");
  auto parsedWriteRanges = parseRanges(*writeRanges, "writeRanges");
  if (!parsedReadRanges) return core::Result<UpdateManifest>{parsedReadRanges.error()};
  if (!parsedWriteRanges) return core::Result<UpdateManifest>{parsedWriteRanges.error()};
  auto packageKeys = rejectUnknown(*package, {"fileName", "url", "size", "sha256"}, "package");
  if (!packageKeys) return core::Result<UpdateManifest>{packageKeys.error()};
  auto fileName = requiredString(*package, "fileName");
  auto url = requiredString(*package, "url");
  auto size = requiredInteger(*package, "size");
  auto sha256 = requiredString(*package, "sha256");
  if (!fileName) return core::Result<UpdateManifest>{fileName.error()};
  if (!url) return core::Result<UpdateManifest>{url.error()};
  if (!size) return core::Result<UpdateManifest>{size.error()};
  if (!sha256) return core::Result<UpdateManifest>{sha256.error()};
  if (size.value() <= 0) {
    return core::failure<UpdateManifest>(core::ErrorCode::ParseError,
                                         "Update package size must be positive");
  }
  auto parsedSignature = parseSignature(*signature, "manifest.signature");
  if (!parsedSignature) return core::Result<UpdateManifest>{parsedSignature.error()};
  std::optional<UpdateRecoveryAuthorization> recovery;
  if (const auto* recoveryValue = root.find("recoveryAuthorization"); recoveryValue != nullptr) {
    if (!recoveryValue->isObject()) {
      return core::failure<UpdateManifest>(core::ErrorCode::ParseError,
                                           "Recovery authorization must be an object");
    }
    auto recoveryPurpose = requiredString(*recoveryValue, "purpose");
    const auto* recoverySignature = recoveryValue->find("signature");
    if (!recoveryPurpose || recoverySignature == nullptr) {
      return core::failure<UpdateManifest>(core::ErrorCode::ParseError,
                                           "Recovery authorization is incomplete");
    }
    auto parsedRecoverySignature = parseSignature(*recoverySignature,
                                                  "recoveryAuthorization.signature");
    if (!parsedRecoverySignature) {
      return core::Result<UpdateManifest>{parsedRecoverySignature.error()};
    }
    JsonValue::Object additionalFields;
    for (const auto& [key, value] : recoveryValue->asObject()) {
      if (key != "purpose" && key != "signature") {
        additionalFields.emplace(key, value);
      }
    }
    recovery = UpdateRecoveryAuthorization{
        .purpose = std::move(recoveryPurpose).value(),
        .signature = parsedRecoverySignature.value(),
        .additionalFields = std::move(additionalFields)};
  }
  return UpdateManifest{
      .schemaVersion = schemaVersion.value(),
      .purpose = std::move(purpose).value(),
      .channel = std::move(channel).value(),
      .manifestId = std::move(manifestId).value(),
      .manifestEpoch = static_cast<std::uint64_t>(epoch.value()),
      .platform = std::move(platform).value(),
      .targetBuild = std::move(targetBuild).value(),
      .targetVersion = std::move(targetVersion).value(),
      .minimumVersion = std::move(minimumVersion).value(),
      .issuedAt = std::move(issuedAt).value(),
      .expiresAt = std::move(expiresAt).value(),
      .readRanges = std::move(parsedReadRanges).value(),
      .writeRanges = std::move(parsedWriteRanges).value(),
      .downgradePolicy = std::move(downgradePolicy).value(),
      .package = UpdatePackage{
          .fileName = std::move(fileName).value(),
          .url = std::move(url).value(),
          .size = static_cast<std::uint64_t>(size.value()),
          .sha256 = std::move(sha256).value()},
      .releaseNotesSha256 = std::move(releaseNotesSha256).value(),
      .recoveryAuthorization = std::move(recovery),
      .signature = parsedSignature.value()};
}

core::Result<void> validateManifestFields(const UpdateManifest& manifest) {
  const auto targetVersion = parseSemver(manifest.targetVersion);
  const auto minimumVersion = parseSemver(manifest.minimumVersion);
  if (manifest.schemaVersion != 1 || manifest.purpose != "update-manifest" ||
      manifest.channel != kExternalBetaUpdateChannel || manifest.manifestId.empty() ||
      manifest.targetBuild.empty() || !isPlatform(manifest.platform) || !targetVersion ||
      !minimumVersion || compareSemver(*targetVersion, *minimumVersion) <= 0 ||
      !isTimestamp(manifest.issuedAt) || !isTimestamp(manifest.expiresAt) ||
      manifest.issuedAt >= manifest.expiresAt || manifest.downgradePolicy != "REJECT" ||
      !isLowerHex64(manifest.releaseNotesSha256) || !isSafeFilename(manifest.package.fileName) ||
      !isHttpsUrl(manifest.package.url) || manifest.package.size == 0U ||
      manifest.package.size > kMaximumPackageBytes || !isLowerHex64(manifest.package.sha256) ||
      !isKeyId(manifest.signature.keyId) || manifest.signature.algorithm != "Ed25519" ||
      !isLowerHex64(manifest.signature.payloadSha256)) {
    return core::failure(core::ErrorCode::ParseError,
                         "Update manifest fields are invalid");
  }
  for (const auto& [name, range] : manifest.readRanges) {
    if (!isAllowedRange(name) || range.min > range.max) {
      return core::failure(core::ErrorCode::ParseError,
                           "Update manifest read range is invalid", name);
    }
  }
  for (const auto& [name, range] : manifest.writeRanges) {
    if (!isAllowedRange(name) || range.min > range.max) {
      return core::failure(core::ErrorCode::ParseError,
                           "Update manifest write range is invalid", name);
    }
  }
  if (manifest.recoveryAuthorization.has_value() &&
      manifest.recoveryAuthorization->purpose != "update-recovery") {
    return core::failure(core::ErrorCode::ParseError,
                         "Update recovery authorization purpose is invalid");
  }
  return core::success();
}

}

std::string canonicalUpdateManifestPayload(const UpdateManifest& manifest) {
  return formats::stringifyJson(manifestJson(manifest, false), false);
}

std::string serializeUpdateManifest(const UpdateManifest& manifest) {
  return formats::stringifyJson(manifestJson(manifest, true), false);
}

std::string updateManifestIdentity(const UpdateManifest& manifest) {
  return core::sha256Hex(serializeUpdateManifest(manifest));
}

core::Result<UpdateManifest> parseUpdateManifest(std::string_view json) {
  auto parsed = formats::parseJson(json, formats::JsonParseLimits{
      .maximumInputBytes = kMaximumMetadataBytes,
      .maximumDepth = 16U,
      .maximumNodes = 4096U,
      .maximumStringBytes = 64U * 1024U,
      .maximumCollectionEntries = 512U});
  if (!parsed) return core::Result<UpdateManifest>{parsed.error()};
  auto manifest = parseManifestRoot(parsed.value());
  if (!manifest) return manifest;
  auto valid = validateManifestFields(manifest.value());
  if (!valid) return core::Result<UpdateManifest>{valid.error()};
  return manifest;
}

core::Result<void> validateUpdateManifest(const UpdateManifest& manifest,
                                          const UpdateTrustPolicy& policy,
                                          std::string_view now) {
  auto fields = validateManifestFields(manifest);
  if (!fields) return fields;
  auto policyValid = validateUpdateTrustPolicy(policy, now);
  if (!policyValid) return policyValid;
  if (manifest.manifestEpoch < policy.policyEpoch ||
      std::find(policy.allowedPlatforms.begin(), policy.allowedPlatforms.end(), manifest.platform) ==
          policy.allowedPlatforms.end()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Update manifest is outside the trust policy epoch or platform scope");
  }
  if (findActiveUpdateKey(policy, manifest.signature.keyId, "update", now) == nullptr) {
    return core::failure(core::ErrorCode::Conflict,
                         "Update manifest signer is not an active delegated key");
  }
  if (manifest.recoveryAuthorization.has_value() &&
      findActiveUpdateKey(policy, manifest.recoveryAuthorization->signature.keyId,
                          "update-recovery", now) == nullptr) {
    return core::failure(core::ErrorCode::Conflict,
                         "Update recovery signer is not an active delegated key");
  }
  if (!now.empty() && now >= manifest.expiresAt) {
    return core::failure(core::ErrorCode::Conflict, "Update manifest is expired");
  }
  return core::success();
}

core::Result<void> verifyUpdateManifest(
    const UpdateManifest& manifest, const UpdateTrustPolicy& policy,
    const UpdateManifestVerificationOptions& options) {
  auto valid = validateUpdateManifest(manifest, policy, options.now);
  if (!valid) return valid;
  if (options.trustedRoot == nullptr) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Update manifest verification requires an offline trusted root");
  }
  auto trusted = verifyUpdateTrustPolicy(policy, *options.trustedRoot, options.now);
  if (!trusted) return trusted;
  if (!options.expectedPlatform.empty() && manifest.platform != options.expectedPlatform) {
    return core::failure(core::ErrorCode::Conflict,
                         "Update manifest platform does not match this host");
  }
  if (options.highestAcceptedManifestEpoch.has_value() &&
      manifest.manifestEpoch <= *options.highestAcceptedManifestEpoch) {
    return core::failure(core::ErrorCode::Conflict,
                         "Update manifest epoch is stale or replayed");
  }
  if (!options.installedVersion.empty()) {
    const auto installed = parseSemver(options.installedVersion);
    const auto target = parseSemver(manifest.targetVersion);
    if (!installed || !target || compareSemver(*target, *installed) <= 0) {
      return core::failure(core::ErrorCode::Conflict,
                           "Normal downgrade or same-version update is rejected");
    }
  }
  const auto* updateKey = findActiveUpdateKey(policy, manifest.signature.keyId,
                                              "update", options.now);
  if (updateKey == nullptr) {
    return core::failure(core::ErrorCode::Conflict,
                         "Update manifest signer is unavailable");
  }
  const auto payload = canonicalUpdateManifestPayload(manifest);
  if (manifest.signature.payloadSha256 != core::sha256Hex(payload)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Update manifest payload hash does not match");
  }
  auto signature = verifyEd25519(
      std::as_bytes(std::span{payload.data(), payload.size()}), manifest.signature.value,
      updateKey->publicKey);
  if (!signature) return signature;
  if (!options.packageBytes.empty()) {
    if (options.packageBytes.size() != manifest.package.size ||
        core::sha256Hex(options.packageBytes) != manifest.package.sha256) {
      return core::failure(core::ErrorCode::Conflict,
                           "Update package bytes do not match the signed metadata");
    }
  }
  return core::success();
}

}
