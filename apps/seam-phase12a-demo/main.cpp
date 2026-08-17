#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank/wav.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required for the Phase 12A demo
#endif

int main(int argc, char** argv) {
  auto output = std::filesystem::path{"out/phase12a"};
  for (int index = 1; index + 1 < argc; ++index) {
    if (std::string_view{argv[index]} == "--output") output = argv[index + 1];
  }
  std::filesystem::create_directories(output);
  const std::vector roots{seam::voicebank::VoicebankSearchRoot{
      .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
      .kind = seam::voicebank::VoicebankRootKind::Development,
  }};
  seam::clap_editor::EditorRuntime runtime(
      std::nullopt, std::filesystem::path{"assets/character-01"}, roots);
  std::shared_ptr<const seam::clap_editor::RenderedPreview> preview;
  for (int attempt = 0; attempt < 800; ++attempt) {
    preview = runtime.renderedPreview();
    if (preview != nullptr &&
        preview->status == seam::clap_editor::PreviewStatus::Ready &&
        preview->revision == runtime.revision()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  if (preview == nullptr ||
      preview->status != seam::clap_editor::PreviewStatus::Ready ||
      preview->stereo.empty()) {
    std::cerr << "Phase 12A render failed: "
              << (preview == nullptr ? "no preview" : preview->diagnostic) << '\n';
    return 1;
  }
  const auto wav = seam::voicebank::writePcm16Wav(
      output / "phase12a-production-preview.wav", preview->sampleRate, 2U,
      preview->stereo);
  if (!wav) return 2;
  seam::formats::JsonValue::Array hashes;
  for (const auto& hash : preview->phraseContentHashes) hashes.emplace_back(hash);
  seam::formats::JsonValue summary{seam::formats::JsonValue::Object{
      {"phase", "12A"},
      {"status", std::string{seam::clap_editor::previewStatusName(preview->status)}},
      {"voicebankId", preview->voicebankId},
      {"voicebankVersion", preview->voicebankVersion},
      {"voicebankContentHash", preview->voicebankContentHash},
      {"phraseCount", static_cast<std::int64_t>(preview->phraseCount)},
      {"unitCount", static_cast<std::int64_t>(preview->unitCount)},
      {"fallbackCount", static_cast<std::int64_t>(preview->fallbackCount)},
      {"cacheHits", static_cast<std::int64_t>(preview->cacheHits)},
      {"frames", static_cast<std::int64_t>(preview->stereo.size() / 2U)},
      {"sampleRate", static_cast<std::int64_t>(preview->sampleRate)},
      {"phraseContentHashes", std::move(hashes)},
      {"diagnostic", preview->diagnostic},
  }};
  std::ofstream stream(output / "phase12a-summary.json");
  stream << seam::formats::stringifyJson(summary, true) << '\n';
  if (!stream) return 3;
  std::cout << seam::formats::stringifyJson(summary, true) << '\n';
  return 0;
}
