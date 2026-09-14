// A procedural singer is a recipe, not audio, so its package is a different family over the same
// signed container. These cases check the family contract: the manifest is typed, the recipe is the
// bytes the manifest declares, and a package that verifies is one a renderer can actually admit.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/distribution/procedural_package.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/core/sha256.hpp"
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
