#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/sha256.hpp"
#include "seam/core/file_io.hpp"
#include "seam/distribution/update_manifest.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <stdexcept>

namespace {

using seam::distribution::UpdateManifest;
using seam::distribution::UpdatePackage;
using seam::distribution::UpdateRange;
using seam::distribution::UpdateSignature;
using seam::distribution::UpdateTrustPolicy;

const std::string_view kNow = "2026-08-22T00:00:00Z";

UpdateTrustPolicy makePolicy(const seam::distribution::SigningKeyPair& root,
                             const seam::distribution::SigningKeyPair& update) {
  UpdateTrustPolicy policy{
      .schemaVersion = 1,
      .purpose = "update-trust-policy",
      .channel = "external-beta",
      .policyEpoch = 7,
      .rootKeyId = "offline-root",
      .rootPublicKey = root.publicKey,
      .allowedPlatforms = {"macos-arm64", "windows-x64"},
      .issuedAt = "2026-08-21T00:00:00Z",
      .notBefore = "2026-08-21T00:00:00Z",
      .expiresAt = "2027-08-21T00:00:00Z",
      .compromiseCutoff = "2027-08-21T00:00:00Z",
      .delegatedKeys = {seam::distribution::DelegatedUpdateKey{
          .keyId = "update-key",
          .purpose = "update",
          .publicKey = update.publicKey,
          .notBefore = "2026-08-21T00:00:00Z",
          .expiresAt = "2027-08-21T00:00:00Z",
          .revokedAt = ""}},
      .signature = {}};
  const auto payload = seam::distribution::canonicalUpdateTrustPolicyPayload(policy);
  auto signature = seam::distribution::signEd25519(
      std::as_bytes(std::span{payload.data(), payload.size()}), root.privateKey);
  if (!signature) throw std::runtime_error(signature.error().message);
  policy.signature = UpdateSignature{
      .algorithm = "Ed25519",
      .keyId = policy.rootKeyId,
      .payloadSha256 = seam::core::sha256Hex(payload),
      .value = signature.value()};
  return policy;
}

UpdateManifest makeManifest(const seam::distribution::SigningKeyPair& update,
                            std::span<const std::byte> packageBytes) {
  UpdateManifest manifest{
      .schemaVersion = 1,
      .purpose = "update-manifest",
      .channel = "external-beta",
      .manifestId = "candidate-2026-08-22",
      .manifestEpoch = 8,
      .platform = "macos-arm64",
      .targetBuild = "0.13.1-beta.8",
      .targetVersion = "0.13.1",
      .minimumVersion = "0.13.0",
      .issuedAt = "2026-08-22T00:00:00Z",
      .expiresAt = "2026-09-22T00:00:00Z",
      .readRanges = {{"project", UpdateRange{.min = 1, .max = 8}}},
      .writeRanges = {{"project", UpdateRange{.min = 8, .max = 8}}},
      .downgradePolicy = "REJECT",
      .package = UpdatePackage{
          .fileName = "ProjectSEAM-0.13.1-macos-arm64.pkg",
          .url = "https://updates.example.invalid/ProjectSEAM-0.13.1.pkg",
          .size = packageBytes.size(),
          .sha256 = seam::core::sha256Hex(packageBytes)},
      .releaseNotesSha256 = seam::core::sha256Hex("release-notes"),
      .recoveryAuthorization = std::nullopt,
      .signature = {}};
  const auto payload = seam::distribution::canonicalUpdateManifestPayload(manifest);
  auto signature = seam::distribution::signEd25519(
      std::as_bytes(std::span{payload.data(), payload.size()}), update.privateKey);
  if (!signature) throw std::runtime_error(signature.error().message);
  manifest.signature = UpdateSignature{
      .algorithm = "Ed25519",
      .keyId = "update-key",
      .payloadSha256 = seam::core::sha256Hex(payload),
      .value = signature.value()};
  return manifest;
}

}

TEST_CASE("signed update manifest verifies platform epoch package and replay policy") {
  const auto root = seam::distribution::generateSigningKeyPair();
  const auto update = seam::distribution::generateSigningKeyPair();
  CHECK(root);
  CHECK(update);
  const std::array<std::byte, 7U> packageBytes{
      std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
      std::byte{5}, std::byte{6}, std::byte{7}};
  auto policy = makePolicy(root.value(), update.value());
  auto manifest = makeManifest(update.value(), packageBytes);
  const auto policyJson = seam::distribution::serializeUpdateTrustPolicy(policy);
  const auto manifestJson = seam::distribution::serializeUpdateManifest(manifest);
  auto parsedPolicy = seam::distribution::parseUpdateTrustPolicy(policyJson);
  auto parsedManifest = seam::distribution::parseUpdateManifest(manifestJson);
  CHECK(parsedPolicy);
  CHECK(parsedManifest);
  const auto trustedRoot = root.value().publicKey;
  const seam::distribution::UpdateManifestVerificationOptions options{
      .expectedPlatform = "macos-arm64",
      .installedVersion = "0.13.0",
      .highestAcceptedManifestEpoch = 7,
      .packageBytes = packageBytes,
      .now = kNow,
      .trustedRoot = &trustedRoot};
  CHECK(seam::distribution::verifyUpdateManifest(
      parsedManifest.value(), parsedPolicy.value(), options));
  CHECK(seam::distribution::updateManifestIdentity(parsedManifest.value()) ==
        seam::distribution::updateManifestIdentity(manifest));

  auto stale = options;
  stale.highestAcceptedManifestEpoch = 8;
  CHECK(!seam::distribution::verifyUpdateManifest(
      parsedManifest.value(), parsedPolicy.value(), stale));
  auto wrongPlatform = options;
  wrongPlatform.expectedPlatform = "windows-x64";
  CHECK(!seam::distribution::verifyUpdateManifest(
      parsedManifest.value(), parsedPolicy.value(), wrongPlatform));
  auto wrongPackage = options;
  const std::array<std::byte, 7U> tampered{
      std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
      std::byte{5}, std::byte{6}, std::byte{8}};
  wrongPackage.packageBytes = tampered;
  CHECK(!seam::distribution::verifyUpdateManifest(
      parsedManifest.value(), parsedPolicy.value(), wrongPackage));
  auto missingRoot = options;
  missingRoot.trustedRoot = nullptr;
  CHECK(!seam::distribution::verifyUpdateManifest(
      parsedManifest.value(), parsedPolicy.value(), missingRoot));
}

TEST_CASE("update metadata rejects forged signatures and malformed replay state") {
  const auto root = seam::distribution::generateSigningKeyPair();
  const auto update = seam::distribution::generateSigningKeyPair();
  const auto other = seam::distribution::generateSigningKeyPair();
  CHECK(root);
  CHECK(update);
  CHECK(other);
  const std::array<std::byte, 3U> packageBytes{
      std::byte{9}, std::byte{8}, std::byte{7}};
  auto policy = makePolicy(root.value(), update.value());
  auto manifest = makeManifest(update.value(), packageBytes);
  manifest.signature.keyId = "unknown-key";
  const auto trustedRoot = root.value().publicKey;
  const seam::distribution::UpdateManifestVerificationOptions options{
      .expectedPlatform = "macos-arm64",
      .installedVersion = "0.13.0",
      .highestAcceptedManifestEpoch = 0,
      .packageBytes = packageBytes,
      .now = kNow,
      .trustedRoot = &trustedRoot};
  CHECK(!seam::distribution::verifyUpdateManifest(manifest, policy, options));

  policy.signature.value[0] ^= std::byte{1};
  CHECK(!seam::distribution::verifyUpdateTrustPolicy(
      policy, root.value().publicKey, kNow));
  CHECK(!seam::distribution::verifyUpdateTrustPolicy(
      makePolicy(root.value(), other.value()), other.value().publicKey, kNow));
}

TEST_CASE("verified update bytes produce an exact sealed installer handoff") {
  const auto root = seam::test::support::temporaryDirectory("update-handoff");
  const auto packagePath = root / "candidate.pkg";
  const std::array<std::byte, 9U> packageBytes{
      std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
      std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
  CHECK(seam::core::durableAtomicWrite(packagePath, packageBytes));
  const auto rootKey = seam::distribution::generateSigningKeyPair();
  const auto updateKey = seam::distribution::generateSigningKeyPair();
  CHECK(rootKey);
  CHECK(updateKey);
  auto policy = makePolicy(rootKey.value(), updateKey.value());
  auto manifest = makeManifest(updateKey.value(), packageBytes);
  auto handoff = seam::distribution::stageVerifiedUpdatePackage(
      packagePath, manifest, root / "staging");
  CHECK(handoff);
  CHECK(handoff.value().candidateId.size() == 42U);
  CHECK(handoff.value().candidateId.rfind("candidate-", 0U) == 0U);
  CHECK(handoff.value().candidateId.find_first_not_of(
            "0123456789abcdefABCDEF", 10U) == std::string::npos);
  CHECK(handoff.value().requiresExplicitUserAction);
  CHECK(handoff.value().requiresInstallerRevalidation);
  CHECK(seam::distribution::verifySealedInstallerHandoff(
      handoff.value(), manifest, root / "staging"));
  const auto text = seam::distribution::serializeSealedInstallerHandoff(handoff.value());
  auto parsed = seam::distribution::parseSealedInstallerHandoff(text);
  CHECK(parsed);
  CHECK(parsed.value().candidateId == handoff.value().candidateId);
  CHECK(seam::distribution::verifySealedInstallerHandoff(
      parsed.value(), manifest, root / "staging"));
  const auto staged = root / "staging" / handoff.value().package.relativePath;
  CHECK(std::filesystem::is_regular_file(staged));
  CHECK(seam::core::durableAtomicWriteText(staged, "tampered"));
  CHECK(!seam::distribution::verifySealedInstallerHandoff(
      handoff.value(), manifest, root / "staging"));
}
