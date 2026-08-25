#include "seam/application/project_factory.hpp"
#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/clap_editor/host_timeline.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/voicebank/catalog.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required for Phase 12B tests
#endif

namespace {
std::shared_ptr<const seam::clap_editor::RenderedPreview> waitReady(
    seam::clap_editor::EditorRuntime& runtime) {
  for (int attempt = 0; attempt < 1200; ++attempt) {
    auto preview = runtime.renderedPreview();
    if (preview != nullptr && preview->revision == runtime.revision() &&
        preview->status == seam::clap_editor::PreviewStatus::Ready) {
      return preview;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  return runtime.renderedPreview();
}

void addNotes(seam::application::ProjectFactory& factory,
              seam::domain::VocalRegion& region,
              std::uint8_t baseKey) {
  const std::array<const char32_t*, 4> lyrics{U"こ", U"え", U"な", U"ぐ"};
  for (std::size_t index = 0U; index < lyrics.size(); ++index) {
    auto [lyric, note] = factory.makeNote(
        seam::time::Tick{static_cast<std::int64_t>(index * 720U)},
        seam::time::Tick{720},
        static_cast<std::uint8_t>(baseKey + static_cast<std::uint8_t>(index)),
        std::u32string{lyrics[index]}, seam::domain::Language::Japanese);
    region.lyrics.push_back(std::move(lyric));
    region.notes.push_back(std::move(note));
  }
  region.sortNotes();
}
}  // namespace

int main() {
  using namespace seam;
  const auto fixture = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK};
  voicebank::VoicebankCatalog catalog;
  const std::vector roots{voicebank::VoicebankSearchRoot{
      .path = fixture,
      .kind = voicebank::VoicebankRootKind::Development,
  }};
  auto scanned = catalog.scan(roots);
  if (!scanned || scanned.value().size() != 1U) return 1;
  const auto& candidate = scanned.value().front();
  const domain::VoicebankReference exact{
      .id = candidate.manifest.id,
      .version = candidate.manifest.version,
      .contentHash = candidate.contentHash,
  };

  clap_editor::EditorRuntime runtime(std::nullopt,
      std::filesystem::path{"assets/character-01"}, roots);
  auto basePreview = waitReady(runtime);
  if (basePreview == nullptr || basePreview->status !=
          clap_editor::PreviewStatus::Ready) return 2;

  auto project = runtime.projectCopy();
  application::ProjectFactory factory{8000U};
  factory.synchronizeWith(project);
  const auto secondTrack = factory.addVocalTrack(project, "HARMONY");
  auto* track = project.findVocalTrack(secondTrack);
  if (track == nullptr) return 3;
  track->voicebank = exact;
  const auto secondRegion = factory.addRegion(
      project, secondTrack, "HARMONY A", time::Tick{960}, time::Tick{7680});
  const auto thirdRegion = factory.addRegion(
      project, secondTrack, "HARMONY B", time::Tick{4800}, time::Tick{7680});
  auto* regionA = project.findRegion(secondRegion);
  auto* regionB = project.findRegion(thirdRegion);
  if (regionA == nullptr || regionB == nullptr) return 4;
  addNotes(factory, *regionA, 55U);
  addNotes(factory, *regionB, 60U);

  if (!runtime.replaceProject(std::move(project))) return 5;
  if (!runtime.configureOutputChannels(4U)) return 6;
  if (!runtime.setHostStartOffset(time::Tick{960})) return 7;
  if (!runtime.selectTrack(secondTrack) || !runtime.selectRegion(secondRegion)) {
    return 8;
  }
  auto preview = waitReady(runtime);
  if (preview == nullptr || preview->status != clap_editor::PreviewStatus::Ready ||
      preview->channelCount != 4U || preview->interleaved.empty() ||
      preview->trackCount != 2U || preview->regionCount != 3U ||
      preview->unitPlan.empty()) {
    if (preview != nullptr) {
      std::cerr << "preview status=" << clap_editor::previewStatusName(preview->status)
                << " diagnostic=" << preview->diagnostic
                << " channels=" << static_cast<unsigned>(preview->channelCount)
                << " tracks=" << preview->trackCount
                << " regions=" << preview->regionCount
                << " units=" << preview->unitPlan.size()
                << " interleaved=" << preview->interleaved.size()
                << " previewRevision=" << preview->revision
                << " runtimeRevision=" << runtime.revision();
      const auto stats = runtime.renderStats();
      std::cerr << " submitted=" << stats.submitted
                << " completed=" << stats.completed
                << " cancelled=" << stats.cancelled
                << " stale=" << stats.stale << '\n';
    }
    return 9;
  }

  const auto activeProject = runtime.projectCopy();
  const auto* activeRegion = activeProject.findRegion(secondRegion);
  if (activeRegion == nullptr) return 10;
  phonemizer::JapaneseKanaPhonemizer jp;
  const auto phonemes = jp.phonemize(*activeRegion);
  if (phonemes.tokens.empty()) return 11;
  const auto unitEntry = preview->unitPlan.front();
  if (unitEntry.tokenStart >= phonemes.tokens.size()) return 12;
  const auto key = phonemes.tokens[unitEntry.tokenStart].key;

  if (!runtime.movePhonemeBoundary(key, false, time::Microseconds{45000})) {
    return 13;
  }
  if (!runtime.selectUnitVariant(key, unitEntry.unitId,
                                 domain::UnitRendererKind::SpectralClassic)) {
    return 14;
  }
  if (!runtime.upsertPitchPoint(domain::PitchAutomationPoint{
          .tick = time::Tick{240}, .cents = 32.0F,
          .interpolation = domain::CurveInterpolation::Linear})) {
    return 15;
  }
  if (!runtime.movePitchPoint(time::Tick{240}, domain::PitchAutomationPoint{
          .tick = time::Tick{360}, .cents = -18.0F,
          .interpolation = domain::CurveInterpolation::Smooth})) {
    return 16;
  }
  if (!runtime.cyclePitchInterpolation(time::Tick{360})) return 17;
  if (!runtime.openSampleMicroscope(key) || !runtime.sampleMicroscopeOpen() ||
      runtime.sampleMicroscope() == nullptr) {
    return 18;
  }
  runtime.closeSampleMicroscope();

  const auto revisionBeforeUndo = runtime.revision();
  runtime.keyDown(native_ui::KeyEvent{
      .key = native_ui::NativeKey::Z,
      .modifiers = native_ui::InputModifiers{.control = true}});
  if (runtime.revision() <= revisionBeforeUndo) return 19;
  runtime.keyDown(native_ui::KeyEvent{
      .key = native_ui::NativeKey::Y,
      .modifiers = native_ui::InputModifiers{.control = true}});

  runtime.setRenderQuality(rendering::RenderQuality::Final);
  if (runtime.renderQuality() != rendering::RenderQuality::Final) return 20;
  auto edited = waitReady(runtime);
  if (edited == nullptr || edited->channelCount != 4U ||
      edited->revision != runtime.revision()) return 21;

  auto persisted = runtime.projectCopy();
  formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(persisted);
  if (!encoded || encoded.value().find("\"schemaVersion\": 6") ==
                      std::string::npos ||
      encoded.value().find("\"hostStartOffsetTick\": 960") ==
                      std::string::npos) {
    return 22;
  }
  const auto decoded = codec.decode(encoded.value());
  if (!decoded || decoded.value() != persisted) return 23;

  auto legacy = encoded.value();
  const auto schema = legacy.find("\"schemaVersion\": 6");
  const auto hostOffset = legacy.find(",\n    \"hostStartOffsetTick\": 960");
  if (schema == std::string::npos || hostOffset == std::string::npos) return 24;
  legacy.replace(schema, std::string{"\"schemaVersion\": 6"}.size(),
                 "\"schemaVersion\": 4");
  legacy.erase(hostOffset, std::string{",\n    \"hostStartOffsetTick\": 960"}.size());
  const auto migrated = codec.decode(legacy);
  if (!migrated || migrated.value().settings().hostStartOffsetTick !=
                       time::Tick{0}) return 25;

  clap_editor::HostTimelineState hostState{
      .playing = true,
      .hasSeconds = true,
      .seconds = 0.6,
      .loopActive = true,
      .loopHasSeconds = true,
      .loopStartSeconds = 0.5,
      .loopEndSeconds = 0.75,
      .hasTimeSignature = true,
      .numerator = 7U,
      .denominator = 8U,
  };
  const auto mapped = clap_editor::HostTimelineMapper::map(
      hostState, persisted, 48000.0, 9600U);
  if (!mapped.audible || mapped.sourceFrame == 0U ||
      mapped.hostSeconds < 0.5 || mapped.hostSeconds >= 0.75) return 26;

  std::cout << "Phase 12B tests PASS: tracks=" << edited->trackCount
            << " regions=" << edited->regionCount
            << " channels=" << static_cast<unsigned>(edited->channelCount)
            << " frames=" << edited->interleaved.size() / edited->channelCount
            << '\n';
  return 0;
}
