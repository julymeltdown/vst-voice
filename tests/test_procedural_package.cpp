// A procedural singer is a recipe, not audio, so its package is a different family over the same
// signed container. These cases check the family contract: the manifest is typed, the recipe is the
// bytes the manifest declares, and a package that verifies is one a renderer can actually admit.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/distribution/procedural_package.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voice_design/articulation_plan.hpp"
#include "seam/voice_design/voice_recipe.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

using namespace seam;

voice_design::VoiceRecipe testRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "procedural-package";
  recipe.seed = 4242U;
  recipe.poses = {{"a", "neutral", 0.0,
                   {{800.0, 90.0, 0.0}, {1250.0, 110.0, -3.0}, {2800.0, 160.0, -6.0}}}};
  return recipe;
}

std::filesystem::path createProceduralSource(const std::filesystem::path& root,
                                             const std::string& recipeOverride = {}) {
  const auto source = root / "source";
  std::filesystem::create_directories(source);
  auto recipe = testRecipe();
  auto encoded = voice_design::encodeVoiceRecipe(recipe);
  if (!encoded) throw std::runtime_error(encoded.error().message);
  const auto bytes = recipeOverride.empty() ? encoded.value() : recipeOverride;
  std::ofstream(source / "recipe.json", std::ios::binary | std::ios::trunc) << bytes;
  distribution::ProceduralSingerManifest manifest;
  manifest.id = "original.singer.pilot";
  manifest.version = "1.0.0";
  manifest.displayName = "Original Singer Pilot";
  manifest.language = "ja";
  manifest.styles = {"neutral"};
  manifest.engineId = recipe.engineId;
  manifest.engineRevision = voice_design::ArticulationPlan::algorithmRevision;
  manifest.recipeEntry = "recipe.json";
  manifest.recipeSha256 = core::sha256Hex(std::as_bytes(std::span{bytes.data(), bytes.size()}));
  manifest.phones = {"a", "i", "u", "e", "o", "s"};
  distribution::ProceduralSingerManifestJsonCodec codec;
  auto text = codec.encode(manifest);
  if (!text) throw std::runtime_error(text.error().message);
  std::ofstream(source / "manifest.json", std::ios::binary | std::ios::trunc) << text.value();
  return source;
}

}  // namespace

TEST_CASE("A procedural manifest round-trips through its typed codec") {
  distribution::ProceduralSingerManifest manifest;
  manifest.id = "original.singer.pilot";
  manifest.version = "1.0.0";
  manifest.displayName = "Original Singer Pilot";
  manifest.language = "ja";
  manifest.styles = {"neutral", "soft"};
  manifest.engineId = "seam.source-filter.v1";
  manifest.engineRevision = 13U;
  manifest.recipeEntry = "recipe.json";
  manifest.recipeSha256 = std::string(64U, 'a');
  manifest.phones = {"a", "s"};
  distribution::ProceduralSingerManifestJsonCodec codec;
  const auto encoded = codec.encode(manifest);
  CHECK(encoded.hasValue());
  if (!encoded) return;
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded.hasValue());
  if (!decoded) return;
  CHECK(decoded.value() == manifest);

  // The declared coverage, styles and identity are bounded rather than trusted.
  auto unbound = manifest;
  unbound.phones.clear();
  CHECK(!codec.encode(unbound).hasValue());
  auto unsafe = manifest;
  unsafe.id = "../escape";
  CHECK(!codec.encode(unsafe).hasValue());
  auto badDigest = manifest;
  badDigest.recipeSha256 = "ABCDEF";
  CHECK(!codec.encode(badDigest).hasValue());
  auto noEngine = manifest;
  noEngine.engineRevision = 0U;
  CHECK(!codec.encode(noEngine).hasValue());
}

TEST_CASE("A signed procedural package verifies and returns its exact recipe") {
  const auto root = test::support::temporaryDirectory("procedural-package");
  const auto source = createProceduralSource(root);
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto packagePath = root / "pilot.seamsinger";
  auto packed = distribution::packProceduralPackage(source, packagePath, key.value());
  CHECK(packed.hasValue());
  if (!packed) return;
  CHECK(packed.value().manifest.id == "original.singer.pilot");
  CHECK(packed.value().manifest.recipeSha256 == packed.value().manifest.recipeSha256);
  CHECK(packed.value().container.signatureValid);

  // Admitting the package re-derives the digest from the packaged bytes.
  auto verified = distribution::verifyProceduralPackage(packagePath, distribution::VerifySeambankOptions{
      .limits = {},
      .trustedPublicKeys = {key.value().publicKey},
      .requireTrustedSigner = true});
  CHECK(verified.hasValue());
  if (!verified) return;
  auto recipe = distribution::readProceduralRecipe(verified.value());
  CHECK(recipe.hasValue());
  if (!recipe) return;
  const std::string text(reinterpret_cast<const char*>(recipe.value().data()),
                         recipe.value().size());
  auto decoded = voice_design::decodeVoiceRecipe(text);
  CHECK(decoded.hasValue());
  if (!decoded) return;
  CHECK(decoded.value().id == "procedural-package");
  CHECK(decoded.value().poses.size() == 1U);

  // The recipe is read-only relative to the signed bytes: the entry digest is unchanged.
  const auto entry = std::find_if(verified.value().container.entries.begin(),
                                  verified.value().container.entries.end(),
                                  [](const auto& candidate) { return candidate.path == "recipe.json"; });
  CHECK(entry != verified.value().container.entries.end());
  CHECK(core::sha256Hex(recipe.value()) == verified.value().manifest.recipeSha256);
}

TEST_CASE("A procedural package refuses a recipe that is not the one it declares") {
  const auto root = test::support::temporaryDirectory("procedural-package-mismatch");
  // The manifest declares the digest of the honest recipe; the file holds different valid JSON.
  const auto honest = createProceduralSource(root / "honest");
  (void)honest;
  const auto source = root / "mismatch";
  std::filesystem::create_directories(source);
  auto recipe = testRecipe();
  recipe.seed = 999U;
  auto encoded = voice_design::encodeVoiceRecipe(recipe);
  CHECK(encoded.hasValue());
  if (!encoded) return;
  std::ofstream(source / "recipe.json", std::ios::binary | std::ios::trunc) << encoded.value();
  distribution::ProceduralSingerManifest manifest;
  manifest.id = "original.singer.mismatch";
  manifest.version = "1.0.0";
  manifest.displayName = "Mismatch";
  manifest.language = "ja";
  manifest.styles = {"neutral"};
  manifest.engineId = recipe.engineId;
  manifest.engineRevision = 13U;
  manifest.recipeEntry = "recipe.json";
  manifest.recipeSha256 = std::string(64U, 'b');
  manifest.phones = {"a"};
  distribution::ProceduralSingerManifestJsonCodec codec;
  auto text = codec.encode(manifest);
  CHECK(text.hasValue());
  if (!text) return;
  std::ofstream(source / "manifest.json", std::ios::binary | std::ios::trunc) << text.value();

  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto packagePath = root / "mismatch.seamsinger";
  auto packed = distribution::packProceduralPackage(source, packagePath, key.value());
  CHECK(!packed.hasValue());
  CHECK(packed.error().code == core::ErrorCode::Conflict);
  CHECK(!std::filesystem::exists(packagePath));
}

TEST_CASE("A procedural package is not a sample bank and a bank is not procedural") {
  const auto root = test::support::temporaryDirectory("procedural-package-family");
  const auto source = createProceduralSource(root);
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto packagePath = root / "pilot.seamsinger";
  CHECK(distribution::packProceduralPackage(source, packagePath, key.value()).hasValue());

  // The sample-bank reader refuses the procedural manifest rather than treating it as a bank.
  auto asBank = distribution::verifySeambank(packagePath, distribution::VerifySeambankOptions{
      .limits = {},
      .trustedPublicKeys = {key.value().publicKey},
      .requireTrustedSigner = true});
  CHECK(!asBank.hasValue());

  // And the procedural reader refuses a package whose root manifest is a voicebank manifest.
  const auto bankSource = root / "bank";
  std::filesystem::create_directories(bankSource);
  std::ofstream(bankSource / "manifest.json") << R"({"formatId":"com.project-seam.voicebank"})";
  std::ofstream(bankSource / "note.txt") << "not a procedural singer\n";
  const auto bankPackage = root / "bank.seamsinger";
  auto packedBank = distribution::packSignedContainer(bankSource, bankPackage, key.value(),
      distribution::PackSignedContainerOptions{.limits = {},
                                               .rootManifest = std::string{"manifest.json"}});
  CHECK(packedBank.hasValue());
  auto asProcedural = distribution::verifyProceduralPackage(bankPackage,
      distribution::VerifySeambankOptions{.limits = {},
                                          .trustedPublicKeys = {key.value().publicKey},
                                          .requireTrustedSigner = true});
  CHECK(!asProcedural.hasValue());
  CHECK(asProcedural.error().code == core::ErrorCode::Unsupported);
}

TEST_CASE("A tampered procedural package no longer verifies") {
  const auto root = test::support::temporaryDirectory("procedural-package-tamper");
  const auto source = createProceduralSource(root);
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto packagePath = root / "pilot.seamsinger";
  CHECK(distribution::packProceduralPackage(source, packagePath, key.value()).hasValue());

  // Flip one byte inside the payload and require the container to reject it.
  std::fstream file(packagePath, std::ios::binary | std::ios::in | std::ios::out);
  CHECK(file.is_open());
  if (!file.is_open()) return;
  file.seekg(static_cast<std::streamoff>(std::filesystem::file_size(packagePath) / 2U));
  char byte = 0;
  file.read(&byte, 1);
  byte = static_cast<char>(byte ^ 0x5A);
  file.seekp(static_cast<std::streamoff>(std::filesystem::file_size(packagePath) / 2U));
  file.write(&byte, 1);
  file.close();
  auto verified = distribution::verifyProceduralPackage(packagePath,
      distribution::VerifySeambankOptions{.limits = {},
                                          .trustedPublicKeys = {key.value().publicKey},
                                          .requireTrustedSigner = true});
  CHECK(!verified.hasValue());
}

TEST_CASE("An installed procedural singer is transactional and receipted") {
  const auto root = test::support::temporaryDirectory("procedural-install");
  const auto source = createProceduralSource(root);
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto packagePath = root / "pilot.seamsinger";
  CHECK(distribution::packProceduralPackage(source, packagePath, key.value()).hasValue());

  const auto installRoot = root / "installed";
  distribution::InstallProceduralOptions options;
  options.verification = distribution::VerifySeambankOptions{
      .limits = {},
      .trustedPublicKeys = {key.value().publicKey},
      .requireTrustedSigner = true};
  auto installed = distribution::installProceduralPackage(packagePath, installRoot, options);
  CHECK(installed.hasValue());
  if (!installed) return;
  CHECK(installed.value().id == "original.singer.pilot");
  CHECK(!installed.value().contentHash.empty());
  const auto directory = installed.value().installDirectory;
  CHECK(std::filesystem::is_regular_file(directory / "manifest.json"));
  CHECK(std::filesystem::is_regular_file(directory / "recipe.json"));
  CHECK(std::filesystem::is_regular_file(directory / "install-receipt.json"));
  // The receipt carries the exact resource identity a song can bind to.
  auto receiptText = core::readTextFileLimited(directory / "install-receipt.json", 1024U * 1024U);
  CHECK(receiptText.hasValue());
  if (!receiptText) return;
  auto receipt = formats::parseJson(receiptText.value());
  CHECK(receipt.hasValue());
  if (!receipt) return;
  const auto* contentHash = receipt.value().find("contentHash");
  CHECK(contentHash != nullptr && contentHash->isString());
  CHECK(contentHash->asString() == installed.value().contentHash);
  const auto* family = receipt.value().find("resourceFamily");
  CHECK(family != nullptr && family->asString() == "procedural-singer");

  // Installing the same version again is refused, and leaves the first installation intact.
  auto duplicate = distribution::installProceduralPackage(packagePath, installRoot, options);
  CHECK(!duplicate.hasValue());
  CHECK(duplicate.error().code == core::ErrorCode::Conflict);
  CHECK(std::filesystem::is_regular_file(directory / "install-receipt.json"));
  // No staging directory survives a successful or refused installation.
  for (const auto& entry : std::filesystem::directory_iterator(installRoot)) {
    const auto name = entry.path().filename().string();
    CHECK(name.starts_with(".staging-") == false);
    CHECK(name.starts_with(".backup-") == false);
  }

  // Replacement is explicit and produces the same identity for identical content.
  options.replaceExisting = true;
  auto replaced = distribution::installProceduralPackage(packagePath, installRoot, options);
  CHECK(replaced.hasValue());
  if (!replaced) return;
  CHECK(replaced.value().contentHash == installed.value().contentHash);
}

TEST_CASE("A procedural installation requires a trusted signer and writes nothing without one") {
  const auto root = test::support::temporaryDirectory("procedural-install-trust");
  const auto source = createProceduralSource(root);
  auto key = distribution::generateSigningKeyPair();
  auto other = distribution::generateSigningKeyPair();
  CHECK(key.hasValue() && other.hasValue());
  if (!key || !other) return;
  const auto packagePath = root / "pilot.seamsinger";
  CHECK(distribution::packProceduralPackage(source, packagePath, key.value()).hasValue());
  const auto installRoot = root / "installed";

  distribution::InstallProceduralOptions untrusted;
  untrusted.verification = distribution::VerifySeambankOptions{
      .limits = {},
      .trustedPublicKeys = {other.value().publicKey},
      .requireTrustedSigner = true};
  auto refused = distribution::installProceduralPackage(packagePath, installRoot, untrusted);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Conflict);
  CHECK(!std::filesystem::exists(installRoot / "original.singer.pilot"));

  // A caller that does not require a trusted signer is rejected outright, because installation is
  // exactly the operation that must not happen on an unverified key.
  distribution::InstallProceduralOptions permissive;
  permissive.verification = distribution::VerifySeambankOptions{
      .limits = {},
      .trustedPublicKeys = {},
      .requireTrustedSigner = false};
  auto rejected = distribution::installProceduralPackage(packagePath, installRoot, permissive);
  CHECK(!rejected.hasValue());
  CHECK(rejected.error().code == core::ErrorCode::InvalidArgument);
}

TEST_CASE("An installed procedural singer is discovered and resolved by exact identity") {
  const auto root = test::support::temporaryDirectory("procedural-catalogue");
  const auto source = createProceduralSource(root);
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto packagePath = root / "pilot.seamsinger";
  CHECK(distribution::packProceduralPackage(source, packagePath, key.value()).hasValue());
  const auto installRoot = root / "installed";
  distribution::InstallProceduralOptions installOptions;
  installOptions.verification = distribution::VerifySeambankOptions{
      .limits = {},
      .trustedPublicKeys = {key.value().publicKey},
      .requireTrustedSigner = true};
  auto installed = distribution::installProceduralPackage(packagePath, installRoot, installOptions);
  CHECK(installed.hasValue());
  if (!installed) return;

  distribution::ProceduralCatalogue catalogue;
  auto scanned = catalogue.scan({distribution::ProceduralSearchRoot{
      .path = installRoot, .kind = distribution::ProceduralRootKind::Installed}});
  CHECK(scanned.hasValue());
  if (!scanned) return;
  CHECK(scanned.value().size() == 1U);
  if (scanned.value().size() != 1U) return;
  CHECK(scanned.value().front().trust == distribution::ProceduralTrust::TrustedInstalled);
  CHECK(scanned.value().front().contentHash == installed.value().contentHash);

  const domain::SingerResourceIdentity reference{domain::SingerResourceKind::Procedural,
                                                 "original.singer.pilot", "1.0.0",
                                                 installed.value().contentHash};
  const auto resolved = distribution::resolveProceduralSinger(reference, scanned.value());
  CHECK(resolved.resolved());
  // The catalogue reports the canonical install root; the installer returns the caller's path. Both
  // name the same directory, so compare canonically rather than textually.
  CHECK(std::filesystem::canonical(resolved.candidate->resourceRoot) ==
        std::filesystem::canonical(installed.value().installDirectory));

  // A different content hash for the same version is a mismatch, and names both sides.
  auto altered = reference;
  altered.contentHash = std::string(64U, 'c');
  const auto mismatch = distribution::resolveProceduralSinger(altered, scanned.value());
  CHECK(mismatch.status == distribution::ProceduralResolveStatus::ContentMismatch);
  CHECK(mismatch.expectedContentHash == altered.contentHash);
  CHECK(mismatch.actualContentHashes.size() == 1U);

  // A missing version is distinguished from a missing singer.
  auto otherVersion = reference;
  otherVersion.version = "9.9.9";
  const auto versionMismatch = distribution::resolveProceduralSinger(otherVersion, scanned.value());
  CHECK(versionMismatch.status == distribution::ProceduralResolveStatus::VersionMismatch);
  CHECK(versionMismatch.availableVersions.size() == 1U);
  auto otherId = reference;
  otherId.id = "original.singer.absent";
  CHECK(distribution::resolveProceduralSinger(otherId, scanned.value()).status ==
        distribution::ProceduralResolveStatus::Missing);

  // A reference without a content hash cannot be resolved silently.
  auto unbound = reference;
  unbound.contentHash.clear();
  CHECK(distribution::resolveProceduralSinger(unbound, scanned.value()).status ==
        distribution::ProceduralResolveStatus::ContentHashMissing);

  // Relink is the same identity against a different root; it does not rewrite the identity.
  const auto relinkRoot = root / "relinked";
  distribution::InstallProceduralOptions relinkOptions = installOptions;
  relinkOptions.replaceExisting = false;
  auto relinked = distribution::installProceduralPackage(packagePath, relinkRoot, relinkOptions);
  CHECK(relinked.hasValue());
  if (!relinked) return;
  auto secondScan = catalogue.scan({distribution::ProceduralSearchRoot{
      .path = relinkRoot, .kind = distribution::ProceduralRootKind::Installed}});
  CHECK(secondScan.hasValue());
  if (!secondScan) return;
  const auto relinkResolution =
      distribution::resolveProceduralSinger(reference, secondScan.value());
  CHECK(relinkResolution.resolved());
  CHECK(std::filesystem::canonical(relinkResolution.candidate->resourceRoot) ==
        std::filesystem::canonical(relinked.value().installDirectory));
  CHECK(relinkResolution.candidate->manifest.id == reference.id);
  CHECK(relinkResolution.candidate->contentHash == reference.contentHash);
}

TEST_CASE("A development procedural resource is labelled and never trusted by default") {
  const auto root = test::support::temporaryDirectory("procedural-catalogue-dev");
  const auto source = createProceduralSource(root);
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto packagePath = root / "pilot.seamsinger";
  CHECK(distribution::packProceduralPackage(source, packagePath, key.value()).hasValue());
  const auto installRoot = root / "installed";
  distribution::InstallProceduralOptions installOptions;
  installOptions.verification = distribution::VerifySeambankOptions{
      .limits = {},
      .trustedPublicKeys = {key.value().publicKey},
      .requireTrustedSigner = true};
  auto installed = distribution::installProceduralPackage(packagePath, installRoot, installOptions);
  CHECK(installed.hasValue());
  if (!installed) return;

  // A development root reports fixtures regardless of a valid receipt, and a caller that requires
  // trusted installs refuses them by name.
  distribution::ProceduralCatalogue catalogue;
  auto scanned = catalogue.scan({distribution::ProceduralSearchRoot{
      .path = installRoot, .kind = distribution::ProceduralRootKind::Development}});
  CHECK(scanned.hasValue());
  if (!scanned) return;
  CHECK(scanned.value().size() == 1U);
  if (scanned.value().size() != 1U) return;
  CHECK(scanned.value().front().trust == distribution::ProceduralTrust::DevelopmentFixture);
  const domain::SingerResourceIdentity reference{domain::SingerResourceKind::Procedural,
                                                 "original.singer.pilot", "1.0.0",
                                                 installed.value().contentHash};
  distribution::ProceduralResolveOptions strict;
  strict.requireTrustedInstalled = true;
  strict.allowDevelopmentFixtures = false;
  const auto refused = distribution::resolveProceduralSinger(reference, scanned.value(), strict);
  CHECK(refused.status == distribution::ProceduralResolveStatus::Untrusted);
  CHECK(refused.diagnostic.find("trust") != std::string::npos);
  distribution::ProceduralResolveOptions permissive;
  permissive.requireTrustedInstalled = false;
  permissive.allowDevelopmentFixtures = true;
  CHECK(distribution::resolveProceduralSinger(reference, scanned.value(), permissive).resolved());

  // A receipt that no longer matches the installed bytes downgrades trust rather than being trusted.
  const auto& directory = installed.value().installDirectory;
  std::filesystem::remove(directory / "install-receipt.json");
  auto rescan = catalogue.scan({distribution::ProceduralSearchRoot{
      .path = installRoot, .kind = distribution::ProceduralRootKind::Installed}});
  CHECK(rescan.hasValue());
  if (!rescan) return;
  CHECK(rescan.value().size() == 1U);
  if (rescan.value().size() != 1U) return;
  CHECK(rescan.value().front().trust == distribution::ProceduralTrust::UntrustedInstalled);
  CHECK(distribution::resolveProceduralSinger(reference, rescan.value(), strict).status ==
        distribution::ProceduralResolveStatus::Untrusted);
}

TEST_CASE("A procedural singer built for another engine is reported as incompatible, not untrusted") {
  const auto root = test::support::temporaryDirectory("procedural-engine");
  const auto source = createProceduralSource(root);
  auto key = distribution::generateSigningKeyPair();
  CHECK(key.hasValue());
  if (!key) return;
  const auto packagePath = root / "pilot.seamsinger";
  CHECK(distribution::packProceduralPackage(source, packagePath, key.value()).hasValue());
  const auto installRoot = root / "installed";
  distribution::InstallProceduralOptions installOptions;
  installOptions.verification = distribution::VerifySeambankOptions{
      .limits = {},
      .trustedPublicKeys = {key.value().publicKey},
      .requireTrustedSigner = true};
  auto installed = distribution::installProceduralPackage(packagePath, installRoot, installOptions);
  CHECK(installed.hasValue());
  if (!installed) return;
  distribution::ProceduralCatalogue catalogue;
  auto scanned = catalogue.scan({distribution::ProceduralSearchRoot{
      .path = installRoot, .kind = distribution::ProceduralRootKind::Installed}});
  CHECK(scanned.hasValue());
  if (!scanned) return;
  const domain::SingerResourceIdentity reference{domain::SingerResourceKind::Procedural,
                                                 "original.singer.pilot", "1.0.0",
                                                 installed.value().contentHash};

  // The engine this build renders is reported by the resource, so a matching request resolves.
  const auto& declared = scanned.value().front().manifest;
  distribution::ProceduralResolveOptions matching;
  matching.renderableEngineId = declared.engineId;
  matching.renderableEngineRevision = declared.engineRevision;
  CHECK(distribution::resolveProceduralSinger(reference, scanned.value(), matching).resolved());

  // An engine the build does not render gives an incompatible-engine reason that names both sides,
  // and is distinct from an untrusted signer.
  distribution::ProceduralResolveOptions otherEngine = matching;
  otherEngine.renderableEngineId = "seam.source-filter.v0";
  const auto incompatible =
      distribution::resolveProceduralSinger(reference, scanned.value(), otherEngine);
  CHECK(incompatible.status == distribution::ProceduralResolveStatus::IncompatibleEngine);
  CHECK(incompatible.diagnostic.find(declared.engineId) != std::string::npos);
  CHECK(incompatible.diagnostic.find("seam.source-filter.v0") != std::string::npos);
  // A revision mismatch is the same verdict, not a different one.
  distribution::ProceduralResolveOptions otherRevision = matching;
  otherRevision.renderableEngineRevision = declared.engineRevision + 1U;
  CHECK(distribution::resolveProceduralSinger(reference, scanned.value(), otherRevision).status ==
        distribution::ProceduralResolveStatus::IncompatibleEngine);
  // With no compatibility request the caller still resolves, which is what a browser view wants.
  distribution::ProceduralResolveOptions unchecked;
  CHECK(distribution::resolveProceduralSinger(reference, scanned.value(), unchecked).resolved());
}
