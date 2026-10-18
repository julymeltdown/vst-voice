#include "seam/application/project_factory.hpp"
#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/voicebank/catalog.hpp"
#include "seam/voicebank/wav.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required
#endif

namespace {
std::shared_ptr<const seam::clap_editor::RenderedPreview> waitReady(
    seam::clap_editor::EditorRuntime& runtime) {
  for (int i = 0; i < 1200; ++i) {
    auto value = runtime.renderedPreview();
    if (value && value->revision == runtime.revision() &&
        value->status == seam::clap_editor::PreviewStatus::Ready) return value;
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  return runtime.renderedPreview();
}
void addPhrase(seam::application::ProjectFactory& factory,
               seam::domain::VocalRegion& region, std::uint8_t key) {
  const std::array<const char32_t*, 4> lyrics{U"こ", U"え", U"な", U"ぐ"};
  for (std::size_t i = 0; i < lyrics.size(); ++i) {
    auto [lyric, note] = factory.makeNote(
        seam::time::Tick{static_cast<std::int64_t>(i * 720U)},
        seam::time::Tick{720}, static_cast<std::uint8_t>(key + i),
        std::u32string{lyrics[i]}, seam::domain::Language::Japanese);
    region.lyrics.push_back(std::move(lyric));
    region.notes.push_back(std::move(note));
  }
  region.sortNotes();
}
}  // namespace

int main(int argc, char** argv) {
  using namespace seam;
  std::filesystem::path output = "out/phase12b";
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string{argv[i]} == "--output") output = argv[++i];
  }
  std::filesystem::create_directories(output);
  const auto fixture = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK};
  const std::vector roots{voicebank::VoicebankSearchRoot{
      .path = fixture, .kind = voicebank::VoicebankRootKind::Development}};
  voicebank::VoicebankCatalog catalog;
  auto scanned = catalog.scan(roots);
  if (!scanned || scanned.value().empty()) return 1;
  const auto& bank = scanned.value().front();
  const domain::VoicebankReference reference{
      .id = bank.manifest.id, .version = bank.manifest.version,
      .contentHash = bank.contentHash};

  clap_editor::EditorRuntime runtime(std::nullopt,
      std::filesystem::path{"assets/character-01"}, roots);
  if (!waitReady(runtime)) return 2;
  auto project = runtime.projectCopy();
  application::ProjectFactory factory{12000U};
  factory.synchronizeWith(project);
  const auto harmony = factory.addVocalTrack(project, "HARMONY");
  project.findVocalTrack(harmony)->voicebank = reference;
  const auto regionA = factory.addRegion(project, harmony, "HARMONY A",
                                          time::Tick{960}, time::Tick{7680});
  const auto regionB = factory.addRegion(project, harmony, "HARMONY B",
                                          time::Tick{4800}, time::Tick{7680});
  addPhrase(factory, *project.findRegion(regionA), 55U);
  addPhrase(factory, *project.findRegion(regionB), 60U);
  if (!runtime.replaceProject(std::move(project)) ||
      !runtime.configureOutputChannels(4U) ||
      !runtime.setHostStartOffset(time::Tick{960}) ||
      !runtime.selectTrack(harmony) || !runtime.selectRegion(regionA)) return 3;

  auto preview = waitReady(runtime);
  if (!preview || preview->status != clap_editor::PreviewStatus::Ready ||
      preview->channelCount != 4U) {
    if (preview) {
      std::cerr << "preview status=" << clap_editor::previewStatusName(preview->status)
                << " diagnostic=" << preview->diagnostic
                << " channels=" << static_cast<unsigned>(preview->channelCount)
                << " tracks=" << preview->trackCount
                << " regions=" << preview->regionCount
                << " revision=" << preview->revision
                << " expected=" << runtime.revision() << '\n';
    }
    return 4;
  }
  const auto active = runtime.projectCopy();
  phonemizer::JapaneseKanaPhonemizer phonemizer;
  const auto tokens = phonemizer.phonemize(*active.findRegion(regionA));
  if (tokens.tokens.empty() || preview->unitPlan.empty()) return 5;
  const auto& entry = preview->unitPlan.front();
  const auto key = tokens.tokens.at(entry.tokenStart).key;
  if (!runtime.movePhonemeBoundary(key, false, time::Microseconds{42000}) ||
      !runtime.selectUnitVariant(key, entry.unitId,
                                 domain::UnitRendererKind::SpectralClassic) ||
      !runtime.upsertPitchPoint(domain::PitchAutomationPoint{
          .tick = time::Tick{360}, .cents = 24.0F,
          .interpolation = domain::CurveInterpolation::Smooth}) ||
      !runtime.openSampleMicroscope(key)) return 6;
  runtime.closeSampleMicroscope();
  runtime.setRenderQuality(rendering::RenderQuality::Final);
  preview = waitReady(runtime);
  if (!preview || preview->status != clap_editor::PreviewStatus::Ready) return 7;

  const auto wavPath = output / "phase12b-multichannel.wav";
  if (!voicebank::writePcm16Wav(wavPath, preview->sampleRate,
          preview->channelCount, preview->interleaved)) return 8;
  formats::ProjectJsonCodec codec;
  const auto json = codec.encode(runtime.projectCopy());
  if (!json) return 9;
  std::ofstream(output / "phase12b-project.json") << json.value();
  std::ofstream summary(output / "phase12b-summary.json");
  summary << "{\n"
          << "  \"schemaVersion\": 5,\n"
          << "  \"tracks\": " << preview->trackCount << ",\n"
          << "  \"regions\": " << preview->regionCount << ",\n"
          << "  \"phrases\": " << preview->phraseCount << ",\n"
          << "  \"units\": " << preview->unitCount << ",\n"
          << "  \"channels\": " << static_cast<unsigned>(preview->channelCount) << ",\n"
          << "  \"frames\": " << preview->interleaved.size() / preview->channelCount << ",\n"
          << "  \"hostStartOffsetTick\": 960,\n"
          << "  \"sampleMicroscope\": true,\n"
          << "  \"technicalLaneEdits\": true,\n"
          << "  \"result\": \"PASS\"\n"
          << "}\n";
  std::cout << "Phase 12B demo PASS: " << wavPath << '\n';
  return 0;
}
