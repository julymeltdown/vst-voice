#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/distribution/update_manifest.hpp"
#include "seam/standalone/update_controller.hpp"
#include "seam/native_ui/update_panel.hpp"

#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <stdexcept>

namespace {

using seam::distribution::UpdateManifest;
using seam::distribution::UpdatePackage;
using seam::distribution::UpdateRange;
using seam::distribution::UpdateSignature;
using seam::distribution::UpdateTrustPolicy;

const std::string kTime = "2026-08-22T00:00:00Z";

UpdateTrustPolicy policyFor(const seam::distribution::SigningKeyPair& root,
                            const seam::distribution::SigningKeyPair& update) {
  UpdateTrustPolicy policy{
      .schemaVersion = 1,
      .purpose = "update-trust-policy",
      .channel = "external-beta",
      .policyEpoch = 3,
      .rootKeyId = "root-key",
      .rootPublicKey = root.publicKey,
      .allowedPlatforms = {"macos-arm64"},
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
  auto signedPayload = seam::distribution::signEd25519(
      std::as_bytes(std::span{payload.data(), payload.size()}), root.privateKey);
  if (!signedPayload) throw std::runtime_error(signedPayload.error().message);
  policy.signature = UpdateSignature{
      .algorithm = "Ed25519",
      .keyId = "root-key",
      .payloadSha256 = seam::core::sha256Hex(payload),
      .value = signedPayload.value()};
  return policy;
}

UpdateManifest manifestFor(const seam::distribution::SigningKeyPair& update,
                           std::span<const std::byte> bytes) {
  UpdateManifest manifest{
      .schemaVersion = 1,
      .purpose = "update-manifest",
      .channel = "external-beta",
      .manifestId = "candidate-1",
      .manifestEpoch = 4,
      .platform = "macos-arm64",
      .targetBuild = "beta-4",
      .targetVersion = "0.14.0",
      .minimumVersion = "0.13.0",
      .issuedAt = "2026-08-22T00:00:00Z",
      .expiresAt = "2026-09-22T00:00:00Z",
      .readRanges = {{"project", UpdateRange{.min = 1, .max = 4}}},
      .writeRanges = {{"project", UpdateRange{.min = 4, .max = 4}}},
      .downgradePolicy = "REJECT",
      .package = UpdatePackage{.fileName = "update.pkg",
                               .url = "https://updates.invalid/update.pkg",
                               .size = bytes.size(),
                               .sha256 = seam::core::sha256Hex(bytes)},
      .releaseNotesSha256 = seam::core::sha256Hex("notes"),
      .recoveryAuthorization = std::nullopt,
      .signature = {}};
  const auto payload = seam::distribution::canonicalUpdateManifestPayload(manifest);
  auto signedPayload = seam::distribution::signEd25519(
      std::as_bytes(std::span{payload.data(), payload.size()}), update.privateKey);
  if (!signedPayload) throw std::runtime_error(signedPayload.error().message);
  manifest.signature = UpdateSignature{
      .algorithm = "Ed25519",
      .keyId = "update-key",
      .payloadSha256 = seam::core::sha256Hex(payload),
      .value = signedPayload.value()};
  return manifest;
}

}

TEST_CASE("update controller verifies, stages, and persists accepted epochs") {
  const auto root = seam::test::support::temporaryDirectory("update-controller");
  const std::array<std::byte, 4U> bytes{
      std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
  const auto package = root / "update.pkg";
  CHECK(seam::core::durableAtomicWrite(package, bytes));
  auto rootKey = seam::distribution::generateSigningKeyPair();
  auto updateKey = seam::distribution::generateSigningKeyPair();
  CHECK(rootKey);
  CHECK(updateKey);
  const auto policy = policyFor(rootKey.value(), updateKey.value());
  const auto manifest = manifestFor(updateKey.value(), bytes);
  const auto policyPath = root / "policy.json";
  const auto manifestPath = root / "manifest.json";
  CHECK(seam::core::durableAtomicWriteText(
      policyPath, seam::distribution::serializeUpdateTrustPolicy(policy)));
  CHECK(seam::core::durableAtomicWriteText(
      manifestPath, seam::distribution::serializeUpdateManifest(manifest)));
  auto controller = seam::standalone::UpdateController::create(
      seam::standalone::UpdateControllerConfig{
          .statePath = root / "state.json",
          .stagingRoot = root / "staging",
          .expectedPlatform = "macos-arm64",
          .installedVersion = "0.13.0",
          .verificationTime = kTime,
          .trustedRoot = rootKey.value().publicKey});
  CHECK(controller);
  auto available = controller.value()->check(policyPath, manifestPath);
  CHECK(available);
  CHECK(available.value().status == seam::standalone::UpdateCheckStatus::Available);
  auto handoff = controller.value()->stage(policyPath, manifestPath, package);
  CHECK(handoff);
  CHECK(std::filesystem::exists(root / "state.json"));
  auto replay = controller.value()->check(policyPath, manifestPath);
  CHECK(replay);
  CHECK(replay.value().status == seam::standalone::UpdateCheckStatus::Blocked);
}

TEST_CASE("update panel exposes explicit confirmation state") {
  seam::native_ui::UpdatePanelModel panel;
  panel.update(seam::native_ui::UpdatePanelView{
      .state = seam::native_ui::UpdatePanelState::Available,
      .targetVersion = "0.14.0",
      .targetBuild = "beta-4",
      .diagnostic = {},
      .explicitConfirmationRequired = true});
  CHECK(panel.view().state == seam::native_ui::UpdatePanelState::Available);
  CHECK(panel.view().targetVersion == "0.14.0");
  CHECK(panel.view().explicitConfirmationRequired);
}
