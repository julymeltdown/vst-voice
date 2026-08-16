#include "seam/formats/json_value.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/voicebank/spectrogram.hpp"
#include "seam/voicebank/validator.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank/waveform.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void printError(const seam::core::Error& error) {
  std::cerr << "error: " << error.message;
  if (!error.context.empty()) std::cerr << " (" << error.context << ')';
  std::cerr << '\n';
}

seam::core::Result<void> writeJson(const std::filesystem::path& path,
                                   const seam::formats::JsonValue& value) {
  std::error_code error;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
      return seam::core::failure(seam::core::ErrorCode::IoError,
                                 "Unable to create JSON output directory",
                                 error.message());
    }
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return seam::core::failure(seam::core::ErrorCode::IoError,
                               "Unable to create JSON output", path.string());
  }
  stream << seam::formats::stringifyJson(value, true) << '\n';
  stream.flush();
  if (!stream) {
    return seam::core::failure(seam::core::ErrorCode::IoError,
                               "Unable to write JSON output", path.string());
  }
  return seam::core::success();
}

int validateCommand(const std::filesystem::path& manifestPath,
                    const std::filesystem::path& requestedRoot) {
  seam::voicebank::ManifestJsonCodec codec;
  const auto manifest = codec.load(manifestPath);
  if (!manifest) {
    printError(manifest.error());
    return 2;
  }
  const auto root = requestedRoot.empty() ? manifestPath.parent_path() : requestedRoot;
  seam::voicebank::BankValidator validator;
  const auto report = validator.validate(manifest.value(), root);
  seam::formats::JsonValue::Array issues;
  for (const auto& issue : report.issues) {
    issues.emplace_back(seam::formats::JsonValue::Object{
        {"severity", std::string{seam::voicebank::issueSeverityName(issue.severity)}},
        {"code", std::string{seam::voicebank::issueCodeName(issue.code)}},
        {"unitId", issue.unitId},
        {"message", issue.message},
    });
  }
  const seam::formats::JsonValue result{seam::formats::JsonValue::Object{
      {"voicebankId", manifest.value().id},
      {"version", manifest.value().version},
      {"root", root.generic_string()},
      {"unitsChecked", static_cast<std::int64_t>(report.unitsChecked)},
      {"errors", static_cast<std::int64_t>(report.errorCount())},
      {"warnings", static_cast<std::int64_t>(report.warningCount())},
      {"ok", report.ok()},
      {"issues", std::move(issues)},
  }};
  std::cout << seam::formats::stringifyJson(result, true) << '\n';
  return report.ok() ? 0 : 3;
}

int inspectCommand(const std::filesystem::path& manifestPath) {
  seam::voicebank::ManifestJsonCodec codec;
  const auto manifest = codec.load(manifestPath);
  if (!manifest) {
    printError(manifest.error());
    return 2;
  }
  seam::formats::JsonValue::Array units;
  for (const auto& unit : manifest.value().units) {
    seam::formats::JsonValue::Array phones;
    for (const auto& phone : unit.phones) phones.emplace_back(phone);
    units.emplace_back(seam::formats::JsonValue::Object{
        {"id", unit.id},
        {"alias", unit.alias},
        {"phones", std::move(phones)},
        {"kind", std::string{seam::voicebank::unitKindName(unit.kind)}},
        {"rootMidi", std::int64_t{unit.rootMidi}},
        {"style", unit.style},
        {"take", std::int64_t{unit.take}},
        {"renderer", std::string{seam::voicebank::rendererHintName(unit.renderer)}},
        {"audio", unit.audioPath.generic_string()},
    });
  }
  const seam::formats::JsonValue result{seam::formats::JsonValue::Object{
      {"id", manifest.value().id},
      {"version", manifest.value().version},
      {"displayName", manifest.value().displayName},
      {"sampleRate", static_cast<std::int64_t>(manifest.value().expectedSampleRate)},
      {"unitCount", static_cast<std::int64_t>(manifest.value().units.size())},
      {"units", std::move(units)},
  }};
  std::cout << seam::formats::stringifyJson(result, true) << '\n';
  return 0;
}

int analyzeCommand(const std::filesystem::path& wavPath,
                   const std::filesystem::path& outputDirectory) {
  const auto audio = seam::voicebank::readWav(wavPath);
  if (!audio) {
    printError(audio.error());
    return 2;
  }
  const auto mono = audio.value().monoMix();
  const auto waveform = seam::voicebank::WaveformPyramid::build(mono, 64, 14);
  if (!waveform) {
    printError(waveform.error());
    return 3;
  }
  const auto spectrogram = seam::voicebank::buildSpectrogram(
      mono, seam::voicebank::SpectrogramConfig{
                .fftSize = 1024,
                .hopSize = 256,
                .minimumDb = -90.0F,
                .maximumDb = -6.0F,
            });
  if (!spectrogram) {
    printError(spectrogram.error());
    return 4;
  }
  const auto pitch = seam::voicebank::analyzePitch(mono, audio.value().sampleRate);
  if (!pitch) {
    printError(pitch.error());
    return 5;
  }
  std::error_code error;
  std::filesystem::create_directories(outputDirectory, error);
  if (error) {
    std::cerr << "error: unable to create output directory (" << error.message() << ")\n";
    return 6;
  }
  const auto waveformResult = seam::voicebank::writeWaveformSvg(
      outputDirectory / "waveform.svg", waveform.value().levelFor(64.0),
      1400.0, 320.0, wavPath.filename().generic_string());
  if (!waveformResult) {
    printError(waveformResult.error());
    return 7;
  }
  const auto spectrogramResult = seam::voicebank::writeSpectrogramPgm(
      outputDirectory / "spectrogram.pgm", spectrogram.value());
  if (!spectrogramResult) {
    printError(spectrogramResult.error());
    return 8;
  }

  seam::formats::JsonValue::Array pitchFrames;
  for (const auto& frame : pitch.value()) {
    pitchFrames.emplace_back(seam::formats::JsonValue::Object{
        {"sourceFrame", static_cast<std::int64_t>(frame.sourceFrame)},
        {"f0Hz", frame.f0Hz},
        {"confidence", frame.confidence},
        {"voiced", frame.voiced},
    });
  }
  const auto stats = seam::voicebank::analyzeAudio(mono);
  const seam::formats::JsonValue result{seam::formats::JsonValue::Object{
      {"source", wavPath.generic_string()},
      {"sampleRate", static_cast<std::int64_t>(audio.value().sampleRate)},
      {"channels", static_cast<std::int64_t>(audio.value().channels)},
      {"frames", static_cast<std::int64_t>(audio.value().frameCount())},
      {"peak", static_cast<double>(stats.peak)},
      {"rms", stats.rms},
      {"dcOffset", stats.dcOffset},
      {"clippedSamples", static_cast<std::int64_t>(stats.clippedSamples)},
      {"medianPitchHz", seam::voicebank::medianVoicedPitch(pitch.value())},
      {"pitchFrames", std::move(pitchFrames)},
  }};
  const auto jsonResult = writeJson(outputDirectory / "analysis.json", result);
  if (!jsonResult) {
    printError(jsonResult.error());
    return 9;
  }
  std::cout << "analysis written to " << outputDirectory << '\n';
  return 0;
}

void printUsage() {
  std::cout
      << "SEAM Voicebank CLI\n\n"
      << "Usage:\n"
      << "  seam_voicebank_cli validate MANIFEST [BANK_ROOT]\n"
      << "  seam_voicebank_cli inspect MANIFEST\n"
      << "  seam_voicebank_cli analyze WAV OUTPUT_DIRECTORY\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    printUsage();
    return 1;
  }
  const std::string_view command{argv[1]};
  if (command == "--help" || command == "-h" || command == "help") {
    printUsage();
    return 0;
  }
  if (command == "validate") {
    if (argc < 3 || argc > 4) {
      printUsage();
      return 1;
    }
    return validateCommand(argv[2], argc == 4 ? std::filesystem::path{argv[3]}
                                               : std::filesystem::path{});
  }
  if (command == "inspect") {
    if (argc != 3) {
      printUsage();
      return 1;
    }
    return inspectCommand(argv[2]);
  }
  if (command == "analyze") {
    if (argc != 4) {
      printUsage();
      return 1;
    }
    return analyzeCommand(argv[2], argv[3]);
  }
  std::cerr << "unknown command: " << command << '\n';
  printUsage();
  return 1;
}
