#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/distribution/installer.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

namespace {

std::filesystem::path createBankSource(const std::filesystem::path& root) {
  const auto source = root / "source";
  std::filesystem::create_directories(source / "audio");
  const auto samples = seam::test::support::sineWave(48000U, 440.0, 0.1);
  const auto audio = source / "audio/a.wav";
  const auto written = seam::voicebank::writePcm16Wav(audio, 48000U, 1U, samples);
  if (!written) throw std::runtime_error(written.error().message);
  auto manifest = seam::test::support::makeManifest(
      {seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 69,
                                             seam::voicebank::UnitKind::Sustain,
                                             samples.size())});
  seam::voicebank::ManifestJsonCodec codec;
  const auto saved = codec.save(manifest, source / "manifest.json");
  if (!saved) throw std::runtime_error(saved.error().message);
  std::ofstream(source / "license.txt") << "Synthetic test bank.\n";
  return source;
}

}  // namespace

TEST_CASE("Ed25519 keys sign verify and persist") {
  const auto root = seam::test::support::temporaryDirectory("distribution-key");
  auto key = seam::distribution::generateSigningKeyPair();
  CHECK(key);
  const std::array<std::byte, 5U> message{
      std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
  auto signature = seam::distribution::signEd25519(message, key.value().privateKey);
  CHECK(signature);
  CHECK(seam::distribution::verifyEd25519(
      message, signature.value(), key.value().publicKey));
  auto tampered = message;
  tampered[2] = std::byte{9};
  CHECK(!seam::distribution::verifyEd25519(
      tampered, signature.value(), key.value().publicKey));
  CHECK(seam::distribution::savePrivateKey(key.value(), root / "private.json"));
  CHECK(seam::distribution::savePublicKey(key.value().publicKey, root / "public.json"));
  auto loadedPrivate = seam::distribution::loadPrivateKey(root / "private.json");
  auto loadedPublic = seam::distribution::loadPublicKey(root / "public.json");
  CHECK(loadedPrivate);
  CHECK(loadedPublic);
  CHECK(loadedPrivate.value() == key.value());
  CHECK(loadedPublic.value() == key.value().publicKey);
}

TEST_CASE("signed seambank packs verifies and reads canonical entries") {
  const auto root = seam::test::support::temporaryDirectory("distribution-pack");
  const auto source = createBankSource(root);
  auto key = seam::distribution::generateSigningKeyPair();
  CHECK(key);
  const auto packagePath = root / "test.seambank";
  auto packed = seam::distribution::packSeambank(source, packagePath, key.value());
  CHECK(packed);
  CHECK(packed.value().signatureValid);
  CHECK(packed.value().signerTrusted);
  CHECK(packed.value().entries.size() == 3U);
  CHECK(packed.value().entries[0].path == "audio/a.wav");
  CHECK(packed.value().entries[1].path == "license.txt");
  CHECK(packed.value().entries[2].path == "manifest.json");
  auto verified = seam::distribution::verifySeambank(
      packagePath, seam::distribution::VerifySeambankOptions{
                       .trustedPublicKeys = {key.value().publicKey},
                       .requireTrustedSigner = true});
  CHECK(verified);
  CHECK(verified.value().manifest.id == "test.voicebank");
  auto manifestBytes = seam::distribution::readSeambankEntry(
      packagePath, "manifest.json",
      seam::distribution::VerifySeambankOptions{
          .trustedPublicKeys = {key.value().publicKey},
          .requireTrustedSigner = true});
  CHECK(manifestBytes);
  CHECK(!manifestBytes.value().empty());
}

TEST_CASE("seambank rejects payload tampering and untrusted signers") {
  const auto root = seam::test::support::temporaryDirectory("distribution-tamper");
  const auto source = createBankSource(root);
  auto key = seam::distribution::generateSigningKeyPair();
  auto other = seam::distribution::generateSigningKeyPair();
  CHECK(key);
  CHECK(other);
  const auto packagePath = root / "test.seambank";
  auto packed = seam::distribution::packSeambank(source, packagePath, key.value());
  CHECK(packed);
  CHECK(!seam::distribution::verifySeambank(
      packagePath, seam::distribution::VerifySeambankOptions{
                       .trustedPublicKeys = {other.value().publicKey},
                       .requireTrustedSigner = true}));

  const auto offset = packed.value().entries.front().payloadOffset;
  std::fstream stream(packagePath, std::ios::binary | std::ios::in | std::ios::out);
  stream.seekg(static_cast<std::streamoff>(offset));
  char value = 0;
  stream.read(&value, 1);
  value ^= 0x01;
  stream.seekp(static_cast<std::streamoff>(offset));
  stream.write(&value, 1);
  stream.close();
  CHECK(!seam::distribution::verifySeambank(packagePath));
}

TEST_CASE("trusted seambank installs atomically with receipt") {
  const auto root = seam::test::support::temporaryDirectory("distribution-install");
  const auto source = createBankSource(root);
  auto key = seam::distribution::generateSigningKeyPair();
  CHECK(key);
  const auto packagePath = root / "test.seambank";
  CHECK(seam::distribution::packSeambank(source, packagePath, key.value()));
  const seam::distribution::InstallSeambankOptions options{
      .verification = seam::distribution::VerifySeambankOptions{
          .trustedPublicKeys = {key.value().publicKey},
          .requireTrustedSigner = true},
      .replaceExisting = false};
  auto installed = seam::distribution::installSeambank(
      packagePath, root / "installed", options);
  CHECK(installed);
  CHECK(std::filesystem::is_regular_file(installed.value().installDirectory /
                                          "manifest.json"));
  CHECK(std::filesystem::is_regular_file(installed.value().installDirectory /
                                          "audio/a.wav"));
  CHECK(std::filesystem::is_regular_file(installed.value().installDirectory /
                                          "install-receipt.json"));
  CHECK(!seam::distribution::installSeambank(packagePath, root / "installed", options));
  auto replaceOptions = options;
  replaceOptions.replaceExisting = true;
  CHECK(seam::distribution::installSeambank(
      packagePath, root / "installed", replaceOptions));
}

TEST_CASE("seambank path policy rejects traversal executable and hidden assets") {
  CHECK(seam::distribution::isSafeSeambankPath("audio/a.wav"));
  CHECK(!seam::distribution::isSafeSeambankPath("../audio/a.wav"));
  CHECK(!seam::distribution::isSafeSeambankPath("audio\\a.wav"));
  CHECK(!seam::distribution::isSafeSeambankPath(".hidden"));
  CHECK(!seam::distribution::isAllowedSeambankAsset("plugin.dll"));
  CHECK(!seam::distribution::isAllowedSeambankAsset("script.sh"));
  CHECK(seam::distribution::isAllowedSeambankAsset("character/neutral.png"));
}

TEST_CASE("seambank installer requires a trusted signer at the core API") {
  const auto root = seam::test::support::temporaryDirectory("distribution-trust-required");
  const auto source = createBankSource(root);
  auto key = seam::distribution::generateSigningKeyPair();
  CHECK(key);
  const auto packagePath = root / "test.seambank";
  CHECK(seam::distribution::packSeambank(source, packagePath, key.value()));
  CHECK(!seam::distribution::installSeambank(
      packagePath, root / "installed",
      seam::distribution::InstallSeambankOptions{}));
}

TEST_CASE("character-bound seambank requires matching embedded runtime assets") {
  const auto root = seam::test::support::temporaryDirectory("distribution-character");
  const auto source = createBankSource(root);
  seam::voicebank::ManifestJsonCodec codec;
  auto manifest = codec.load(source / "manifest.json");
  CHECK(manifest);
  manifest.value().characterId = "official.character.test";
  manifest.value().characterVersion = "1.0.0";
  CHECK(codec.save(manifest.value(), source / "manifest.json"));
  auto key = seam::distribution::generateSigningKeyPair();
  CHECK(key);
  const auto packagePath = root / "character.seambank";
  CHECK(!seam::distribution::packSeambank(source, packagePath, key.value()));

  std::filesystem::create_directories(source / "character/runtime");
  std::ofstream(source / "character/runtime/neutral.ppm", std::ios::binary)
      << "P6\n1 1\n255\n\0\0\0";
  std::ofstream characterManifest(source / "character/manifest.json",
                                  std::ios::binary);
  characterManifest
      << "{\n"
      << "  \"schemaVersion\": 1,\n"
      << "  \"characterId\": \"official.character.test\",\n"
      << "  \"displayName\": \"Test Character\",\n"
      << "  \"version\": \"1.0.0\",\n"
      << "  \"voicebankId\": \"test.voicebank\",\n"
      << "  \"style\": \"low-poly-emo\",\n"
      << "  \"states\": {\"neutral\": \"runtime/neutral.ppm\"}\n"
      << "}\n";
  characterManifest.close();
  auto packed = seam::distribution::packSeambank(source, packagePath,
                                                  key.value());
  CHECK(packed);
  CHECK(packed.value().manifest.characterId == "official.character.test");
}
