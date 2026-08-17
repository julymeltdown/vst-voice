#include "seam/clap/session.hpp"
#include "seam/clap/state_codec.hpp"
#include "seam/voicebank/wav.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  std::filesystem::path output = "out/phase10";
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--output" && index + 1 < argc) output = argv[++index];
  }
  std::filesystem::create_directories(output);
  auto session = seam::clap::makeDiagnosticSession(48000U, 4U, 2.0);
  if (!session) {
    std::cerr << session.error().message << '\n';
    return 1;
  }
  const auto statePath = output / "phase10-diagnostic.seamclapstate";
  const auto stateResult = seam::clap::writeStateFile(statePath, session.value());
  if (!stateResult) {
    std::cerr << stateResult.error().message << '\n';
    return 1;
  }
  const auto wavResult = seam::voicebank::writePcm16Wav(
      output / "phase10-diagnostic-4ch.wav", session.value().sampleRate,
      session.value().channelCount, session.value().interleavedSamples);
  if (!wavResult) {
    std::cerr << wavResult.error().message << '\n';
    return 1;
  }
  const auto roundTrip = seam::clap::readStateFile(statePath);
  if (!roundTrip || !(roundTrip.value() == session.value())) {
    std::cerr << "Phase 10 state round trip failed\n";
    return 1;
  }
  std::ofstream summary(output / "summary.json", std::ios::binary);
  summary << "{\n"
          << "  \"phase\": \"10\",\n"
          << "  \"sampleRate\": " << session.value().sampleRate << ",\n"
          << "  \"channels\": " << static_cast<unsigned>(session.value().channelCount) << ",\n"
          << "  \"frames\": " << session.value().frameCount() << ",\n"
          << "  \"stateRoundTrip\": true\n"
          << "}\n";
  std::cout << "Phase 10 state: " << statePath << '\n';
  return 0;
}
