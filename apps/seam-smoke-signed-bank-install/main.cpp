#include "seam/distribution/installer.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

seam::voicebank::Manifest createManifest() {
  seam::voicebank::Manifest manifest;
  manifest.id = "official.voice.01.demo";
  manifest.version = "0.7.0";
  manifest.displayName = "Official Voice 01 Distribution Demo";
  manifest.language = seam::domain::Language::Japanese;
  manifest.expectedSampleRate = 48000U;
  manifest.styles = {"original"};
  manifest.units.push_back(seam::voicebank::Unit{
      .id = "ja.original.a4.a",
      .alias = "a",
      .phones = {"a"},
      .kind = seam::voicebank::UnitKind::Sustain,
      .audioPath = "audio/a.wav",
      .rootMidi = 69,
      .style = "original",
      .take = 1,
      .priority = 0,
      .gainDb = 0.0F,
      .renderer = seam::voicebank::RendererHint::Raw,
      .markers = seam::voicebank::UnitMarkers{
          .audioOffset = 0,
          .consonantEnd = 0,
          .vowelOnset = 0,
          .stableStart = 128,
          .loopStart = 256,
          .loopEnd = 4096,
          .releaseStart = 4352,
          .audioEnd = 4800,
      },
      .pitchMarks = {},
      .enabled = true,
  });
  return manifest;
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path output = "out/phase7";
  for (int index = 1; index < argc; ++index) {
    if (std::string_view{argv[index]} == "--output" && index + 1 < argc) {
      output = argv[++index];
    }
  }
  std::error_code error;
  std::filesystem::remove_all(output, error);
  std::filesystem::create_directories(output / "source/audio", error);
  if (error) return 1;

  const auto manifest = createManifest();
  seam::voicebank::ManifestJsonCodec codec;
  auto manifestSaved = codec.save(manifest, output / "source/manifest.json");
  if (!manifestSaved) { std::cerr << manifestSaved.error().message << " (" << manifestSaved.error().context << ")\n"; return 2; }
  std::vector<float> samples(4800U);
  constexpr double twoPi = 6.28318530717958647692;
  for (std::size_t index = 0U; index < samples.size(); ++index) {
    samples[index] = 0.25F * static_cast<float>(
        std::sin(twoPi * 440.0 * static_cast<double>(index) / 48000.0));
  }
  auto wavSaved = seam::voicebank::writePcm16Wav(output / "source/audio/a.wav", 48000U, 1U, samples);
  if (!wavSaved) { std::cerr << wavSaved.error().message << " (" << wavSaved.error().context << ")\n"; return 3; }
  std::ofstream(output / "source/license.txt") << "Phase 7 synthetic distribution evidence only.\n";

  auto key = seam::distribution::generateSigningKeyPair();
  if (!key) return 4;
  if (!seam::distribution::savePublicKey(key.value().publicKey,
                                         output / "official-demo-public-key.json")) return 5;
  auto package = seam::distribution::packSeambank(
      output / "source", output / "official-voice-01-demo.seambank", key.value());
  if (!package || !package.value().signatureValid || !package.value().signerTrusted) return 6;
  auto verified = seam::distribution::verifySeambank(
      output / "official-voice-01-demo.seambank",
      seam::distribution::VerifySeambankOptions{
          .trustedPublicKeys = {key.value().publicKey},
          .requireTrustedSigner = true});
  if (!verified) return 7;
  auto installed = seam::distribution::installSeambank(
      output / "official-voice-01-demo.seambank", output / "installed",
      seam::distribution::InstallSeambankOptions{
          .verification = seam::distribution::VerifySeambankOptions{
              .trustedPublicKeys = {key.value().publicKey},
              .requireTrustedSigner = true},
          .replaceExisting = false});
  if (!installed) return 8;

  std::ofstream summary(output / "phase7-summary.json", std::ios::trunc);
  summary << "{\n"
          << "  \"phase\": \"7\",\n"
          << "  \"voicebankId\": \"" << verified.value().manifest.id << "\",\n"
          << "  \"version\": \"" << verified.value().manifest.version << "\",\n"
          << "  \"entries\": " << verified.value().entries.size() << ",\n"
          << "  \"signatureValid\": true,\n"
          << "  \"signerTrusted\": true,\n"
          << "  \"signerKeyId\": \"" << verified.value().signerKeyId << "\",\n"
          << "  \"packageDigest\": \"" << verified.value().packageDigest << "\",\n"
          << "  \"installDirectory\": \""
          << installed.value().installDirectory.generic_string() << "\"\n"
          << "}\n";
  std::cout << "Project SEAM Phase 7 signed package installed\n"
            << "  package: " << package.value().packagePath << '\n'
            << "  signer: " << package.value().signerKeyId << '\n'
            << "  installed: " << installed.value().installDirectory << '\n';
  return 0;
}
