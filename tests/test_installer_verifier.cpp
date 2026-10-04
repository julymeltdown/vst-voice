#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/distribution/update_manifest.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>

namespace {

using seam::distribution::InstallerHandoffVerificationOptions;
using seam::distribution::UpdateManifest;
using seam::distribution::UpdatePackage;
using seam::distribution::UpdateSignature;

UpdateManifest makeInstallerManifest(std::span<const std::byte> packageBytes) {
  return UpdateManifest{
      .schemaVersion = 1,
      .purpose = "update-manifest",
      .channel = "external-beta",
      .manifestId = "candidate-2026-08-31",
      .manifestEpoch = 9,
      .platform = "macos-arm64",
      .targetBuild = "0.13.1-beta.9",
      .targetVersion = "0.13.1",
      .minimumVersion = "0.13.0",
      .issuedAt = "2026-08-31T00:00:00Z",
      .expiresAt = "2026-09-30T00:00:00Z",
      .readRanges = {},
      .writeRanges = {},
      .downgradePolicy = "REJECT",
      .package = UpdatePackage{
          .fileName = "ProjectSEAM-0.13.1-macos-arm64.pkg",
          .url = "https://updates.example.invalid/ProjectSEAM.pkg",
          .size = packageBytes.size(),
          .sha256 = seam::core::sha256Hex(packageBytes)},
      .releaseNotesSha256 = seam::core::sha256Hex("release-notes"),
      .recoveryAuthorization = std::nullopt,
      .signature = UpdateSignature{
          .algorithm = "Ed25519",
          .keyId = "project-seam-update-2026",
          .payloadSha256 = std::string(64U, 'a'),
          .value = {}}};
}

InstallerHandoffVerificationOptions optionsFor(
    const seam::distribution::SealedInstallerHandoff& handoff,
    const std::filesystem::path& replayRoot, bool consume = false) {
  return InstallerHandoffVerificationOptions{
      .expectedCandidateId = handoff.candidateId,
      .expectedPlatform = "macos-arm64",
      .expectedPublisherKeyId = "project-seam-update-2026",
      .now = "2026-09-01T00:00:00Z",
      .replayStateRoot = replayRoot,
      .consume = consume};
}

}

TEST_CASE("privileged handoff verification rejects stale wrong and replayed input") {
  const auto root = seam::test::support::temporaryDirectory("installer-verifier");
  const auto packagePath = root / "candidate.pkg";
  const std::array<std::byte, 8U> packageBytes{
      std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3},
      std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7}};
  CHECK(seam::core::durableAtomicWrite(packagePath, packageBytes));
  auto manifest = makeInstallerManifest(packageBytes);
  auto handoff = seam::distribution::stageVerifiedUpdatePackage(
      packagePath, manifest, root / "staging");
  CHECK(handoff);
  auto fractionalTimestamp = handoff.value();
  fractionalTimestamp.createdAt = "2026-08-31T00:00:00.000Z";
  CHECK(!seam::distribution::parseSealedInstallerHandoff(
      seam::distribution::serializeSealedInstallerHandoff(
          fractionalTimestamp)));

  const auto absentStagingRoot = root / "absent-staging";
  CHECK(!seam::distribution::verifySealedInstallerHandoff(
      handoff.value(), manifest, absentStagingRoot,
      optionsFor(handoff.value(), root / "replay")));
  CHECK(!std::filesystem::exists(absentStagingRoot));

  auto wrongPublisher = optionsFor(handoff.value(), root / "replay");
  wrongPublisher.expectedPublisherKeyId = "wrong-publisher";
  CHECK(!seam::distribution::verifySealedInstallerHandoff(
      handoff.value(), manifest, root / "staging", wrongPublisher));

  auto wrongCandidate = optionsFor(handoff.value(), root / "replay");
  wrongCandidate.expectedCandidateId = "candidate-00000000000000000000000000000000";
  CHECK(!seam::distribution::verifySealedInstallerHandoff(
      handoff.value(), manifest, root / "staging", wrongCandidate));

  auto stale = optionsFor(handoff.value(), root / "replay");
  stale.now = "2026-09-30T00:00:00Z";
  CHECK(!seam::distribution::verifySealedInstallerHandoff(
      handoff.value(), manifest, root / "staging", stale));

  const auto consume = optionsFor(handoff.value(), root / "replay", true);
  CHECK(seam::distribution::verifySealedInstallerHandoff(
      handoff.value(), manifest, root / "staging", consume));
  CHECK(!seam::distribution::verifySealedInstallerHandoff(
      handoff.value(), manifest, root / "staging", consume));

  auto restaged = seam::distribution::stageVerifiedUpdatePackage(
      packagePath, manifest, root / "restaged");
  CHECK(restaged);
  const auto restagedOptions = optionsFor(
      restaged.value(), root / "replay", true);
  CHECK(!seam::distribution::verifySealedInstallerHandoff(
      restaged.value(), manifest, root / "restaged", restagedOptions));
}

TEST_CASE("privileged handoff verification rejects same-byte file replacement") {
  const auto root = seam::test::support::temporaryDirectory("installer-identity");
  const auto packagePath = root / "candidate.pkg";
  const std::array<std::byte, 4U> packageBytes{
      std::byte{4}, std::byte{3}, std::byte{2}, std::byte{1}};
  CHECK(seam::core::durableAtomicWrite(packagePath, packageBytes));
  auto manifest = makeInstallerManifest(packageBytes);
  auto handoff = seam::distribution::stageVerifiedUpdatePackage(
      packagePath, manifest, root / "staging");
  CHECK(handoff);
  const auto staged = root / "staging" / handoff.value().package.relativePath;
  std::filesystem::rename(staged, staged.string() + ".replaced");
  CHECK(seam::core::durableAtomicWrite(staged, packageBytes));

  CHECK(!seam::distribution::verifySealedInstallerHandoff(
      handoff.value(), manifest, root / "staging",
      optionsFor(handoff.value(), root / "replay")));
}

namespace {

// The two tests above build manifests whose signature fields are placeholders, so
// they prove the handoff rules but never prove that a real signature verifies
// against a real root. The shipped verifier binary additionally embeds its root
// public key at configure time, which means its accept path cannot be exercised
// from a test at all: the repository holds the public key and not the matching
// private key. These helpers sign genuinely so the library accept path is proven
// here, and the binary's accept path stays honestly marked as unproven.

struct SignedMaterial final {
  seam::distribution::SigningKeyPair root;
  seam::distribution::SigningKeyPair publisher;
  seam::distribution::UpdateTrustPolicy policy;
  seam::distribution::UpdateManifest manifest;
};

SignedMaterial makeSignedMaterial(std::span<const std::byte> packageBytes) {
  auto root = seam::distribution::generateSigningKeyPair();
  auto publisher = seam::distribution::generateSigningKeyPair();
  if (!root || !publisher) throw std::runtime_error("key generation failed");
  const std::string rootId = seam::distribution::publicKeyId(root.value().publicKey);
  const std::string publisherId =
      seam::distribution::publicKeyId(publisher.value().publicKey);

  seam::distribution::UpdateTrustPolicy policy{
      .schemaVersion = 1,
      .purpose = "update-trust-policy",
      .channel = "external-beta",
      .policyEpoch = 3,
      .rootKeyId = rootId,
      .rootPublicKey = root.value().publicKey,
      .allowedPlatforms = {"macos-arm64"},
      .issuedAt = "2026-08-01T00:00:00Z",
      .notBefore = "2026-08-01T00:00:00Z",
      .expiresAt = "2027-01-01T00:00:00Z",
      .compromiseCutoff = "1970-01-01T00:00:00Z",
      .delegatedKeys = {seam::distribution::DelegatedUpdateKey{
          .keyId = publisherId,
          .purpose = "update",
          .publicKey = publisher.value().publicKey,
          .notBefore = "2026-08-01T00:00:00Z",
          .expiresAt = "2027-01-01T00:00:00Z",
          .revokedAt = {}}},
      .signature = {}};
  const auto policyPayload = seam::distribution::canonicalUpdateTrustPolicyPayload(policy);
  const auto policySignature =
      seam::distribution::signEd25519(
          std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                         policyPayload.data()),
                                     policyPayload.size()),
          root.value().privateKey);
  if (!policySignature) throw std::runtime_error("policy signing failed");
  policy.signature = seam::distribution::UpdateSignature{
      .algorithm = "Ed25519", .keyId = rootId,
      .payloadSha256 = seam::core::sha256Hex(policyPayload),
      .value = policySignature.value()};

  seam::distribution::UpdateManifest manifest{
      .schemaVersion = 1,
      .purpose = "update-manifest",
      .channel = "external-beta",
      .manifestId = "signed-candidate",
      .manifestEpoch = 9,
      .platform = "macos-arm64",
      .targetBuild = "0.13.1-beta.9",
      .targetVersion = "0.13.1",
      .minimumVersion = "0.13.0",
      .issuedAt = "2026-08-31T00:00:00Z",
      .expiresAt = "2027-01-01T00:00:00Z",
      .readRanges = {},
      .writeRanges = {},
      .downgradePolicy = "REJECT",
      .package = seam::distribution::UpdatePackage{
          .fileName = "ProjectSEAM.pkg",
          .url = "https://updates.example.invalid/ProjectSEAM.pkg",
          .size = packageBytes.size(),
          .sha256 = seam::core::sha256Hex(packageBytes)},
      .releaseNotesSha256 = seam::core::sha256Hex("release-notes"),
      .recoveryAuthorization = std::nullopt,
      .signature = {}};
  const auto manifestPayload =
      seam::distribution::canonicalUpdateManifestPayload(manifest);
  const auto manifestSignature =
      seam::distribution::signEd25519(
          std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                         manifestPayload.data()),
                                         manifestPayload.size()),
          publisher.value().privateKey);
  if (!manifestSignature) throw std::runtime_error("manifest signing failed");
  manifest.signature = seam::distribution::UpdateSignature{
      .algorithm = "Ed25519", .keyId = publisherId,
      .payloadSha256 = seam::core::sha256Hex(manifestPayload),
      .value = manifestSignature.value()};
  return {root.value(), publisher.value(), policy, manifest};
}

}  // namespace

TEST_CASE("a real signed manifest verifies against a real trust policy root") {
  const auto root = seam::test::support::temporaryDirectory("installer-signed");
  const auto packagePath = root / "candidate.pkg";
  const std::array<std::byte, 8U> packageBytes{
      std::byte{9}, std::byte{8}, std::byte{7}, std::byte{6},
      std::byte{5}, std::byte{4}, std::byte{3}, std::byte{2}};
  CHECK(seam::core::durableAtomicWrite(packagePath, packageBytes));
  const auto signedMaterial =
      makeSignedMaterial(packageBytes);

  // Round-trip through the real parsers first: a signature over bytes that the
  // parser does not reproduce would verify in memory and fail on disk.
  const auto policyText = seam::distribution::serializeUpdateTrustPolicy(
      signedMaterial.policy);
  const auto parsedPolicy = seam::distribution::parseUpdateTrustPolicy(policyText);
  CHECK(parsedPolicy);
  const auto manifestText =
      seam::distribution::serializeUpdateManifest(signedMaterial.manifest);
  const auto parsedManifest =
      seam::distribution::parseUpdateManifest(manifestText);
  CHECK(parsedManifest);

  const seam::distribution::UpdateManifestVerificationOptions options{
      .expectedPlatform = "macos-arm64",
      .installedVersion = {},
      .highestAcceptedManifestEpoch = std::nullopt,
      .packageBytes = packageBytes,
      .now = "2026-09-01T00:00:00Z",
      .trustedRoot = &signedMaterial.root.publicKey};
  CHECK(seam::distribution::verifyUpdateManifest(
      parsedManifest.value(), parsedPolicy.value(), options));

  // And the signature must actually be load-bearing: flipping one signature byte
  // has to fail, or the test above proved only that an unsigned manifest passes.
  auto tampered = parsedManifest.value();
  tampered.signature.value[0] ^= std::byte{0x01};
  CHECK(!seam::distribution::verifyUpdateManifest(
      tampered, parsedPolicy.value(), options));

  // A manifest signed by an untrusted publisher must not be accepted either.
  auto otherPublisher = makeSignedMaterial(packageBytes);
  otherPublisher.manifest.signature.keyId =
      seam::distribution::publicKeyId(otherPublisher.publisher.publicKey);
  CHECK(!seam::distribution::verifyUpdateManifest(
      otherPublisher.manifest, parsedPolicy.value(), options));
}

TEST_CASE("a real signed handoff is consumed once and refuses replay") {
  const auto root = seam::test::support::temporaryDirectory("installer-replay");
  const auto packagePath = root / "candidate.pkg";
  const std::array<std::byte, 8U> packageBytes{
      std::byte{1}, std::byte{1}, std::byte{2}, std::byte{3},
      std::byte{5}, std::byte{8}, std::byte{13}, std::byte{21}};
  CHECK(seam::core::durableAtomicWrite(packagePath, packageBytes));
  const auto signedMaterial =
      makeSignedMaterial(packageBytes);

  const auto manifestText =
      seam::distribution::serializeUpdateManifest(signedMaterial.manifest);
  const auto manifest =
      seam::distribution::parseUpdateManifest(manifestText);
  CHECK(manifest);
  const auto handoff = seam::distribution::stageVerifiedUpdatePackage(
      packagePath, manifest.value(), root / "staging");
  CHECK(handoff);

  auto options = optionsFor(handoff.value(), root / "replay", true);
  options.expectedPublisherKeyId = signedMaterial.manifest.signature.keyId;
  CHECK(seam::distribution::verifySealedInstallerHandoff(
      handoff.value(), manifest.value(), root / "staging", options));
  // Consuming a handoff must make the identical second presentation fail, or
  // the replay guard is not guarding anything.
  CHECK(!seam::distribution::verifySealedInstallerHandoff(
      handoff.value(), manifest.value(), root / "staging", options));
}
