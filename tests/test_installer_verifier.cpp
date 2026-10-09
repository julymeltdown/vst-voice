#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
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
