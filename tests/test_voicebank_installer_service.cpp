#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/voicebank_installer_service.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"

#include <filesystem>
#include <fstream>

namespace {
std::filesystem::path createSource(const std::filesystem::path& root,
                                   double frequency,
                                   std::string version = "1.0.0") {
  const auto source = root / ("source-" + std::to_string(static_cast<int>(frequency)));
  std::filesystem::create_directories(source / "audio");
  const auto samples = seam::test::support::sineWave(48000U, frequency, 0.12);
  CHECK(seam::voicebank::writePcm16Wav(source / "audio/a.wav", 48000U, 1U,
                                       samples));
  auto manifest = seam::test::support::makeManifest({
      seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 60,
                                    seam::voicebank::UnitKind::Sustain,
                                    samples.size())});
  manifest.id = "install.test.bank";
  manifest.version = std::move(version);
  seam::voicebank::ManifestJsonCodec codec;
  CHECK(codec.save(manifest, source / "manifest.json"));
  std::ofstream(source / "license.txt") << "Synthetic test fixture\n";
  return source;
}

seam::authoring::VoicebankInstallRequest request(
    const std::filesystem::path& package,
    const seam::distribution::Ed25519PublicKey& key,
    seam::authoring::ExistingVoicebankDecision decision =
        seam::authoring::ExistingVoicebankDecision::Reject) {
  return seam::authoring::VoicebankInstallRequest{
      .packagePath = package,
      .trustedPublicKeys = {key},
      .useDevelopmentTrustRoot = false,
      .existingDecision = decision,
  };
}
}  // namespace

TEST_CASE("voicebank_installer_service_installs_and_exposes_trusted_candidate") {
  const auto root = seam::test::support::temporaryDirectory("u3-install");
  auto key = seam::distribution::generateSigningKeyPair();
  CHECK(key);
  const auto package = root / "bank.seambank";
  CHECK(seam::distribution::packSeambank(createSource(root, 220.0), package,
                                         key.value()));

  seam::authoring::VoicebankSession session({
      seam::voicebank::VoicebankSearchRoot{
          .path = root / "installed",
          .kind = seam::voicebank::VoicebankRootKind::Installed}} , false);
  CHECK(session.refresh());
  seam::authoring::VoicebankInstallerService installer(
      session, root / "installed");
  auto installed = installer.install(request(package, key.value().publicKey));
  CHECK(installed);
  CHECK(installed.value().candidate.trust ==
        seam::voicebank::VoicebankTrust::TrustedInstalled);
  CHECK(installed.value().candidate.contentHash == installed.value().contentHash);
  CHECK(std::filesystem::is_regular_file(
      installed.value().installDirectory / "install-receipt.json"));
  CHECK(session.candidates().size() == 1U);
}

TEST_CASE("voicebank_installer_service_requires_replace_and_different_content") {
  const auto root = seam::test::support::temporaryDirectory("u3-replace");
  auto key = seam::distribution::generateSigningKeyPair();
  CHECK(key);
  const auto packageA = root / "a.seambank";
  const auto packageB = root / "b.seambank";
  CHECK(seam::distribution::packSeambank(createSource(root, 220.0), packageA,
                                         key.value()));
  CHECK(seam::distribution::packSeambank(createSource(root, 330.0), packageB,
                                         key.value()));
  seam::authoring::VoicebankSession session({
      {.path = root / "installed",
       .kind = seam::voicebank::VoicebankRootKind::Installed}}, false);
  CHECK(session.refresh());
  seam::authoring::VoicebankInstallerService installer(session,
                                                        root / "installed");
  const auto first = installer.install(request(packageA, key.value().publicKey));
  CHECK(first);
  CHECK(!installer.install(request(packageA, key.value().publicKey,
                                   seam::authoring::ExistingVoicebankDecision::Replace)));
  CHECK(!installer.install(request(packageB, key.value().publicKey)));
  const auto replaced = installer.install(request(
      packageB, key.value().publicKey,
      seam::authoring::ExistingVoicebankDecision::Replace));
  CHECK(replaced);
  CHECK(replaced.value().contentHash != first.value().contentHash);
  CHECK(session.candidates().size() == 1U);
  CHECK(session.candidates().front().contentHash == replaced.value().contentHash);
}

TEST_CASE("voicebank_installer_service_rejects_untrusted_and_tampered_packages") {
  const auto root = seam::test::support::temporaryDirectory("u3-untrusted");
  auto key = seam::distribution::generateSigningKeyPair();
  auto other = seam::distribution::generateSigningKeyPair();
  CHECK(key);
  CHECK(other);
  const auto package = root / "bank.seambank";
  auto packed = seam::distribution::packSeambank(createSource(root, 220.0),
                                                  package, key.value());
  CHECK(packed);
  seam::authoring::VoicebankSession session({
      {.path = root / "installed",
       .kind = seam::voicebank::VoicebankRootKind::Installed}}, false);
  CHECK(session.refresh());
  seam::authoring::VoicebankInstallerService installer(session,
                                                        root / "installed");
  CHECK(!installer.install(request(package, other.value().publicKey)));
  CHECK(session.candidates().empty());

  std::fstream stream(package, std::ios::binary | std::ios::in | std::ios::out);
  stream.seekg(static_cast<std::streamoff>(packed.value().entries.front().payloadOffset));
  char byte = 0;
  stream.read(&byte, 1);
  byte ^= 0x20;
  stream.seekp(static_cast<std::streamoff>(packed.value().entries.front().payloadOffset));
  stream.write(&byte, 1);
  stream.close();
  CHECK(!installer.install(request(package, key.value().publicKey)));
  CHECK(!std::filesystem::exists(root / "installed" / "install.test.bank"));
}

TEST_CASE("voicebank_installer_service_does_not_expose_receipt_mismatch_as_trusted") {
  const auto root = seam::test::support::temporaryDirectory("u3-receipt");
  auto key = seam::distribution::generateSigningKeyPair();
  CHECK(key);
  const auto package = root / "bank.seambank";
  CHECK(seam::distribution::packSeambank(createSource(root, 220.0), package,
                                         key.value()));
  seam::authoring::VoicebankSession session({
      {.path = root / "installed",
       .kind = seam::voicebank::VoicebankRootKind::Installed}}, false);
  CHECK(session.refresh());
  seam::authoring::VoicebankInstallerService installer(session,
                                                        root / "installed");
  auto installed = installer.install(request(package, key.value().publicKey));
  CHECK(installed);
  std::ofstream(installed.value().installDirectory / "install-receipt.json",
                std::ios::binary | std::ios::trunc)
      << "{\"voicebankId\":\"wrong\"}\n";
  CHECK(session.refresh());
  CHECK(session.candidates().front().trust ==
        seam::voicebank::VoicebankTrust::UntrustedInstalled);
}
