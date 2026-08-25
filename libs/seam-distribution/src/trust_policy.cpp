#include "seam/distribution/trust_policy.hpp"

#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>

namespace seam::distribution {
namespace {

using formats::JsonValue;

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

JsonValue policyJson(const UpdateTrustPolicy& policy, bool includeSignature) {
  JsonValue::Object root;
  root.emplace("schemaVersion", policy.schemaVersion);
  root.emplace("purpose", policy.purpose);
  root.emplace("channel", policy.channel);
  root.emplace("policyEpoch", static_cast<std::int64_t>(policy.policyEpoch));
  root.emplace("rootKeyId", policy.rootKeyId);
  root.emplace("rootPublicKey", encodeBase64(policy.rootPublicKey));
  JsonValue::Array platforms;
  for (const auto& platform : policy.allowedPlatforms) platforms.emplace_back(platform);
  root.emplace("allowedPlatforms", std::move(platforms));
  root.emplace("issuedAt", policy.issuedAt);
  root.emplace("notBefore", policy.notBefore);
  root.emplace("expiresAt", policy.expiresAt);
  root.emplace("compromiseCutoff", policy.compromiseCutoff);
  JsonValue::Array delegated;
  for (const auto& key : policy.delegatedKeys) {
    JsonValue::Object item;
    item.emplace("keyId", key.keyId);
    item.emplace("purpose", key.purpose);
    item.emplace("algorithm", "Ed25519");
    item.emplace("publicKey", encodeBase64(key.publicKey));
    item.emplace("notBefore", key.notBefore);
    item.emplace("expiresAt", key.expiresAt);
    if (!key.revokedAt.empty()) item.emplace("revokedAt", key.revokedAt);
    delegated.emplace_back(std::move(item));
  }
  root.emplace("delegatedKeys", std::move(delegated));
  if (includeSignature) root.emplace("signature", signatureJson(policy.signature));
  return JsonValue{std::move(root)};
}

core::Result<UpdateTrustPolicy> parsePolicyRoot(const JsonValue& root) {
  auto keys = rejectUnknown(
      root, {"schemaVersion", "purpose", "channel", "policyEpoch", "rootKeyId",
             "rootPublicKey", "allowedPlatforms", "issuedAt", "notBefore",
             "expiresAt", "compromiseCutoff", "delegatedKeys", "signature"},
      "policy");
  if (!keys) return core::Result<UpdateTrustPolicy>{keys.error()};
  auto schemaVersion = requiredInteger(root, "schemaVersion");
  auto purpose = requiredString(root, "purpose");
  auto channel = requiredString(root, "channel");
  auto epoch = requiredInteger(root, "policyEpoch");
  auto rootKeyId = requiredString(root, "rootKeyId");
  auto rootPublicKey = requiredString(root, "rootPublicKey");
  auto issuedAt = requiredString(root, "issuedAt");
  auto notBefore = requiredString(root, "notBefore");
  auto expiresAt = requiredString(root, "expiresAt");
  auto compromiseCutoff = requiredString(root, "compromiseCutoff");
  if (!schemaVersion) return core::Result<UpdateTrustPolicy>{schemaVersion.error()};
  if (!purpose) return core::Result<UpdateTrustPolicy>{purpose.error()};
  if (!channel) return core::Result<UpdateTrustPolicy>{channel.error()};
  if (!epoch) return core::Result<UpdateTrustPolicy>{epoch.error()};
  if (!rootKeyId) return core::Result<UpdateTrustPolicy>{rootKeyId.error()};
  if (!rootPublicKey) return core::Result<UpdateTrustPolicy>{rootPublicKey.error()};
  if (!issuedAt) return core::Result<UpdateTrustPolicy>{issuedAt.error()};
  if (!notBefore) return core::Result<UpdateTrustPolicy>{notBefore.error()};
  if (!expiresAt) return core::Result<UpdateTrustPolicy>{expiresAt.error()};
  if (!compromiseCutoff) return core::Result<UpdateTrustPolicy>{compromiseCutoff.error()};
  if (schemaVersion.value() != 1 || epoch.value() < 0) {
    return core::failure<UpdateTrustPolicy>(core::ErrorCode::ParseError,
                                            "Update trust policy version or epoch is invalid");
  }
  auto decodedRoot = decodeBase64<32U>(rootPublicKey.value(), "rootPublicKey");
  if (!decodedRoot) return core::Result<UpdateTrustPolicy>{decodedRoot.error()};

  UpdateTrustPolicy policy{
      .schemaVersion = schemaVersion.value(),
      .purpose = std::move(purpose).value(),
      .channel = std::move(channel).value(),
      .policyEpoch = static_cast<std::uint64_t>(epoch.value()),
      .rootKeyId = std::move(rootKeyId).value(),
      .rootPublicKey = decodedRoot.value(),
      .allowedPlatforms = {},
      .issuedAt = std::move(issuedAt).value(),
      .notBefore = std::move(notBefore).value(),
      .expiresAt = std::move(expiresAt).value(),
      .compromiseCutoff = std::move(compromiseCutoff).value(),
      .delegatedKeys = {},
      .signature = {},
  };

  const auto* platforms = root.find("allowedPlatforms");
  if (platforms == nullptr || !platforms->isArray() || platforms->asArray().empty()) {
    return core::failure<UpdateTrustPolicy>(core::ErrorCode::ParseError,
                                            "allowedPlatforms must be a non-empty array");
  }
  for (const auto& item : platforms->asArray()) {
    if (!item.isString() ||
        (item.asString() != "macos-arm64" &&
         item.asString() != "windows-x64" && item.asString() != "linux-x64")) {
      return core::failure<UpdateTrustPolicy>(core::ErrorCode::ParseError,
                                              "allowedPlatforms contains an unsupported platform");
    }
    policy.allowedPlatforms.push_back(item.asString());
  }
  if (!std::is_sorted(policy.allowedPlatforms.begin(), policy.allowedPlatforms.end()) ||
      std::adjacent_find(policy.allowedPlatforms.begin(), policy.allowedPlatforms.end()) !=
          policy.allowedPlatforms.end()) {
    return core::failure<UpdateTrustPolicy>(core::ErrorCode::ParseError,
                                            "allowedPlatforms must be sorted and unique");
  }

  const auto* delegated = root.find("delegatedKeys");
  if (delegated == nullptr || !delegated->isArray() || delegated->asArray().empty()) {
    return core::failure<UpdateTrustPolicy>(core::ErrorCode::ParseError,
                                            "delegatedKeys must be a non-empty array");
  }
  for (const auto& item : delegated->asArray()) {
    auto itemKeys = rejectUnknown(item, {"keyId", "purpose", "algorithm", "publicKey",
                                         "notBefore", "expiresAt", "revokedAt"},
                                  "delegatedKey");
    if (!itemKeys) return core::Result<UpdateTrustPolicy>{itemKeys.error()};
    auto keyId = requiredString(item, "keyId");
    auto purposeValue = requiredString(item, "purpose");
    auto algorithm = requiredString(item, "algorithm");
    auto publicKey = requiredString(item, "publicKey");
    auto keyNotBefore = requiredString(item, "notBefore");
    auto keyExpiresAt = requiredString(item, "expiresAt");
    if (!keyId) return core::Result<UpdateTrustPolicy>{keyId.error()};
    if (!purposeValue) return core::Result<UpdateTrustPolicy>{purposeValue.error()};
    if (!algorithm) return core::Result<UpdateTrustPolicy>{algorithm.error()};
    if (!publicKey) return core::Result<UpdateTrustPolicy>{publicKey.error()};
    if (!keyNotBefore) return core::Result<UpdateTrustPolicy>{keyNotBefore.error()};
    if (!keyExpiresAt) return core::Result<UpdateTrustPolicy>{keyExpiresAt.error()};
    if (algorithm.value() != "Ed25519" ||
        (purposeValue.value() != "update" && purposeValue.value() != "update-recovery") ||
        !isKeyId(keyId.value())) {
      return core::failure<UpdateTrustPolicy>(core::ErrorCode::ParseError,
                                              "delegated key identity is invalid");
    }
    auto decodedKey = decodeBase64<32U>(publicKey.value(), "delegatedKey.publicKey");
    if (!decodedKey) return core::Result<UpdateTrustPolicy>{decodedKey.error()};
    std::string revokedAt;
    if (const auto* revoked = item.find("revokedAt"); revoked != nullptr) {
      if (!revoked->isString() || revoked->asString().empty()) {
        return core::failure<UpdateTrustPolicy>(core::ErrorCode::ParseError,
                                                "delegatedKey.revokedAt is invalid");
      }
      revokedAt = revoked->asString();
    }
    policy.delegatedKeys.push_back(DelegatedUpdateKey{
        .keyId = std::move(keyId).value(),
        .purpose = std::move(purposeValue).value(),
        .publicKey = decodedKey.value(),
        .notBefore = std::move(keyNotBefore).value(),
        .expiresAt = std::move(keyExpiresAt).value(),
        .revokedAt = std::move(revokedAt)});
  }
  const auto* signature = root.find("signature");
  if (signature == nullptr) {
    return core::failure<UpdateTrustPolicy>(core::ErrorCode::ParseError,
                                            "policy.signature is required");
  }
  auto parsedSignature = parseSignature(*signature, "policy.signature");
  if (!parsedSignature) return core::Result<UpdateTrustPolicy>{parsedSignature.error()};
  policy.signature = parsedSignature.value();
  auto valid = validateUpdateTrustPolicy(policy);
  if (!valid) return core::Result<UpdateTrustPolicy>{valid.error()};
  return policy;
}

}

std::string canonicalUpdateTrustPolicyPayload(const UpdateTrustPolicy& policy) {
  return formats::stringifyJson(policyJson(policy, false), false);
}

std::string serializeUpdateTrustPolicy(const UpdateTrustPolicy& policy) {
  return formats::stringifyJson(policyJson(policy, true), false);
}

core::Result<UpdateTrustPolicy> parseUpdateTrustPolicy(std::string_view json) {
  auto parsed = formats::parseJson(json, formats::JsonParseLimits{
      .maximumInputBytes = kMaximumMetadataBytes,
      .maximumDepth = 16U,
      .maximumNodes = 4096U,
      .maximumStringBytes = 64U * 1024U,
      .maximumCollectionEntries = 512U});
  if (!parsed) return core::Result<UpdateTrustPolicy>{parsed.error()};
  return parsePolicyRoot(parsed.value());
}

core::Result<void> validateUpdateTrustPolicy(const UpdateTrustPolicy& policy,
                                             std::string_view now) {
  if (policy.schemaVersion != 1 || policy.purpose != "update-trust-policy" ||
      policy.channel != kExternalBetaUpdateChannel || policy.rootKeyId.empty() ||
      !isKeyId(policy.rootKeyId) || policy.allowedPlatforms.empty() ||
      !std::is_sorted(policy.allowedPlatforms.begin(), policy.allowedPlatforms.end()) ||
      std::adjacent_find(policy.allowedPlatforms.begin(), policy.allowedPlatforms.end()) !=
          policy.allowedPlatforms.end()) {
    return core::failure(core::ErrorCode::ParseError,
                         "Update trust policy identity is invalid");
  }
  for (const auto& platform : policy.allowedPlatforms) {
    if (platform != "macos-arm64" && platform != "windows-x64" && platform != "linux-x64") {
      return core::failure(core::ErrorCode::ParseError,
                           "Update trust policy contains an unsupported platform");
    }
  }
  if (!isTimestamp(policy.issuedAt) || !isTimestamp(policy.notBefore) ||
      !isTimestamp(policy.expiresAt) || !isTimestamp(policy.compromiseCutoff) ||
      policy.issuedAt > policy.notBefore || policy.notBefore >= policy.expiresAt ||
      policy.compromiseCutoff > policy.expiresAt) {
    return core::failure(core::ErrorCode::ParseError,
                         "Update trust policy time window is invalid");
  }
  if (!isKeyId(policy.signature.keyId) || policy.signature.keyId != policy.rootKeyId ||
      policy.signature.algorithm != "Ed25519" ||
      !isLowerHex64(policy.signature.payloadSha256) || policy.delegatedKeys.empty()) {
    return core::failure(core::ErrorCode::ParseError,
                         "Update trust policy signature is invalid");
  }
  if (!now.empty() && (now < policy.notBefore || now >= policy.expiresAt)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Update trust policy is outside its validity window");
  }
  std::vector<std::string_view> seen;
  for (const auto& key : policy.delegatedKeys) {
    if (!isKeyId(key.keyId) || key.keyId == policy.rootKeyId ||
        (key.purpose != "update" && key.purpose != "update-recovery") ||
        key.notBefore >= key.expiresAt || !isTimestamp(key.notBefore) ||
        !isTimestamp(key.expiresAt) ||
        std::find(seen.begin(), seen.end(), key.keyId) != seen.end()) {
      return core::failure(core::ErrorCode::ParseError,
                           "Update trust policy delegated key is invalid");
    }
    if (!key.revokedAt.empty() &&
        (!isTimestamp(key.revokedAt) || key.revokedAt < key.notBefore)) {
      return core::failure(core::ErrorCode::ParseError,
                           "Update trust policy delegated key revocation is invalid");
    }
    seen.push_back(key.keyId);
  }
  return core::success();
}

core::Result<void> verifyUpdateTrustPolicy(const UpdateTrustPolicy& policy,
                                           const Ed25519PublicKey& trustedRoot,
                                           std::string_view now) {
  auto valid = validateUpdateTrustPolicy(policy, now);
  if (!valid) return valid;
  if (policy.rootPublicKey != trustedRoot) {
    return core::failure(core::ErrorCode::Conflict,
                         "Update trust policy root differs from the offline trusted root");
  }
  const auto payload = canonicalUpdateTrustPolicyPayload(policy);
  if (policy.signature.payloadSha256 != core::sha256Hex(payload)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Update trust policy payload hash does not match");
  }
  const auto signature = verifyEd25519(
      std::as_bytes(std::span{payload.data(), payload.size()}), policy.signature.value,
      trustedRoot);
  if (!signature) return signature;
  return core::success();
}

const DelegatedUpdateKey* findActiveUpdateKey(const UpdateTrustPolicy& policy,
                                              std::string_view keyId,
                                              std::string_view purpose,
                                              std::string_view now) {
  for (const auto& key : policy.delegatedKeys) {
    if (key.keyId != keyId || key.purpose != purpose) continue;
    if (now.empty()) return key.revokedAt.empty() ? &key : nullptr;
    if (now < key.notBefore || now >= key.expiresAt ||
        (!key.revokedAt.empty() && now >= key.revokedAt)) {
      return nullptr;
    }
    return &key;
  }
  return nullptr;
}

}
