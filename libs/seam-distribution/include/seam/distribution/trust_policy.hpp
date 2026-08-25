#pragma once

#include "seam/core/result.hpp"
#include "seam/distribution/signing.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace seam::distribution {

inline constexpr std::string_view kExternalBetaUpdateChannel = "external-beta";

struct UpdateSignature final {
  std::string algorithm;
  std::string keyId;
  std::string payloadSha256;
  Ed25519Signature value{};

  friend bool operator==(const UpdateSignature&, const UpdateSignature&) = default;
};

struct DelegatedUpdateKey final {
  std::string keyId;
  std::string purpose;
  Ed25519PublicKey publicKey{};
  std::string notBefore;
  std::string expiresAt;
  std::string revokedAt;

  friend bool operator==(const DelegatedUpdateKey&, const DelegatedUpdateKey&) = default;
};

struct UpdateTrustPolicy final {
  std::int64_t schemaVersion{0};
  std::string purpose;
  std::string channel;
  std::uint64_t policyEpoch{0};
  std::string rootKeyId;
  Ed25519PublicKey rootPublicKey{};
  std::vector<std::string> allowedPlatforms;
  std::string issuedAt;
  std::string notBefore;
  std::string expiresAt;
  std::string compromiseCutoff;
  std::vector<DelegatedUpdateKey> delegatedKeys;
  UpdateSignature signature;

  friend bool operator==(const UpdateTrustPolicy&, const UpdateTrustPolicy&) = default;
};

[[nodiscard]] core::Result<UpdateTrustPolicy> parseUpdateTrustPolicy(
    std::string_view json);
[[nodiscard]] std::string serializeUpdateTrustPolicy(
    const UpdateTrustPolicy& policy);
[[nodiscard]] std::string canonicalUpdateTrustPolicyPayload(
    const UpdateTrustPolicy& policy);
[[nodiscard]] core::Result<void> validateUpdateTrustPolicy(
    const UpdateTrustPolicy& policy, std::string_view now = {});
[[nodiscard]] core::Result<void> verifyUpdateTrustPolicy(
    const UpdateTrustPolicy& policy, const Ed25519PublicKey& trustedRoot,
    std::string_view now = {});
[[nodiscard]] const DelegatedUpdateKey* findActiveUpdateKey(
    const UpdateTrustPolicy& policy, std::string_view keyId,
    std::string_view purpose, std::string_view now = {});

}
