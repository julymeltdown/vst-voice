#include "seam/clap/session.hpp"
#include "seam/clap/state_codec.hpp"
#include "seam/voicebank/wav.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void usage() {
  std::cout
      << "Project SEAM CLAP render-state tool\n\n"
      << "Usage:\n"
      << "  seam_clap_state_tool pack INPUT.wav OUTPUT.seamclapstate [--title TEXT] [--gain-db VALUE]\n"
      << "  seam_clap_state_tool inspect INPUT.seamclapstate\n"
      << "  seam_clap_state_tool extract INPUT.seamclapstate OUTPUT.wav\n";
}

void printError(const seam::core::Error& error) {
  std::cerr << "error: " << error.message;
  if (!error.context.empty()) std::cerr << " (" << error.context << ')';
  std::cerr << '\n';
}

int packState(int argc, char** argv) {
  if (argc < 4) {
    usage();
    return 2;
  }
  std::string title = std::filesystem::path{argv[2]}.filename().string();
  double gainDb = seam::clap::kDefaultMasterGainDb;
  for (int index = 4; index < argc; ++index) {
    const std::string_view option{argv[index]};
    if (option == "--title" && index + 1 < argc) {
      title = argv[++index];
    } else if (option == "--gain-db" && index + 1 < argc) {
      const auto parsed = seam::clap::parseMasterGainDb(argv[++index]);
      if (!parsed) {
        std::cerr << "error: " << parsed.error().message << '\n';
        return 2;
      }
      gainDb = parsed.value();
    } else {
      std::cerr << "error: unknown or incomplete option: " << option << '\n';
      return 2;
    }
  }

  const auto audio = seam::voicebank::readWav(argv[2]);
  if (!audio) {
    printError(audio.error());
    return 3;
  }
  seam::clap::PluginSession session;
  session.sampleRate = audio.value().sampleRate;
  session.channelCount = static_cast<std::uint8_t>(audio.value().channels);
  session.masterGainDb = gainDb;
  session.title = std::move(title);
  session.interleavedSamples = audio.value().interleaved;
  const auto saved = seam::clap::writeStateFile(argv[3], session);
  if (!saved) {
    printError(saved.error());
    return 4;
  }
  std::cout << "state=" << std::filesystem::path{argv[3]}.string() << '\n'
            << "sampleRate=" << session.sampleRate << '\n'
            << "channels=" << static_cast<unsigned>(session.channelCount) << '\n'
            << "frames=" << session.frameCount() << '\n'
            << "masterGainDb=" << session.masterGainDb << '\n'
            << "title=" << session.title << '\n';
  return 0;
}

int inspectState(int argc, char** argv) {
  if (argc != 3) {
    usage();
    return 2;
  }
  const auto session = seam::clap::readStateFile(argv[2]);
  if (!session) {
    printError(session.error());
    return 3;
  }
  std::cout << std::boolalpha << std::setprecision(12)
            << "{\n"
            << "  \"format\": \"SEAMCLP1\",\n"
            << "  \"sampleRate\": " << session.value().sampleRate << ",\n"
            << "  \"channels\": " << static_cast<unsigned>(session.value().channelCount) << ",\n"
            << "  \"frames\": " << session.value().frameCount() << ",\n"
            << "  \"masterGainDb\": " << session.value().masterGainDb << ",\n"
            << "  \"titleBytes\": " << session.value().title.size() << ",\n"
            << "  \"finitePcm\": true\n"
            << "}\n";
  return 0;
}

int extractState(int argc, char** argv) {
  if (argc != 4) {
    usage();
    return 2;
  }
  const auto session = seam::clap::readStateFile(argv[2]);
  if (!session) {
    printError(session.error());
    return 3;
  }
  const auto written = seam::voicebank::writePcm16Wav(
      argv[3], session.value().sampleRate, session.value().channelCount,
      session.value().interleavedSamples);
  if (!written) {
    printError(written.error());
    return 4;
  }
  std::cout << "wav=" << std::filesystem::path{argv[3]}.string() << '\n'
            << "sampleRate=" << session.value().sampleRate << '\n'
            << "channels=" << static_cast<unsigned>(session.value().channelCount) << '\n'
            << "frames=" << session.value().frameCount() << '\n';
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || std::string_view{argv[1]} == "--help" ||
      std::string_view{argv[1]} == "help") {
    usage();
    return 0;
  }
  const std::string_view command{argv[1]};
  if (command == "pack") return packState(argc, argv);
  if (command == "inspect") return inspectState(argc, argv);
  if (command == "extract") return extractState(argc, argv);
  usage();
  return 2;
}
