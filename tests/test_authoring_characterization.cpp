#include "test_framework.hpp"

#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/voicebank/catalog.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required for authoring characterization tests
#endif

namespace {

using seam::clap_editor::EditorRuntime;
using seam::clap_editor::PreviewStatus;
using seam::clap_editor::RenderedPreview;

std::vector<seam::voicebank::VoicebankSearchRoot> fixtureRoots() {
  return {seam::voicebank::VoicebankSearchRoot{
      .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
      .kind = seam::voicebank::VoicebankRootKind::Development,
  }};
}

std::shared_ptr<const RenderedPreview> waitReady(EditorRuntime& runtime) {
  for (int attempt = 0; attempt < 1200; ++attempt) {
    auto preview = runtime.renderedPreview();
    if (preview != nullptr && preview->revision == runtime.revision() &&
        preview->status == PreviewStatus::Ready && !preview->interleaved.empty()) {
      return preview;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  return runtime.renderedPreview();
}

void checkSubmitted(EditorRuntime& runtime, const std::function<seam::core::Result<void>()>& edit) {
  const auto before = runtime.renderStats().submitted;
  const auto result = edit();
  CHECK(result);
  CHECK(runtime.renderStats().submitted > before);
}

}  // namespace

TEST_CASE("authoring_characterization_default_clap_project_is_stable") {
  EditorRuntime runtime(std::nullopt, std::filesystem::path{"assets/character-01"},
                        fixtureRoots());
  const auto project = runtime.projectCopy();

  CHECK(project.name() == "SEAM / CLAP EDITOR");
  CHECK(project.noteCount() == 8U);
  CHECK(project.vocalTracks().size() == 1U);
  CHECK(project.vocalTracks().front().name == "VOICE 01");
  CHECK(project.vocalTracks().front().regions.size() == 1U);
  CHECK(project.vocalTracks().front().regions.front().name == "DAW PHRASE");
  CHECK_NEAR(project.tempoMap().bpmAt(seam::time::Tick{0}), 154.0, 0.0001);
  CHECK(project.settings().characterDisplay == seam::domain::CharacterDisplayMode::Minimal);

  const auto resolution = runtime.voicebankResolution();
  CHECK(resolution.resolved());
  CHECK(resolution.candidate.has_value());
  CHECK(resolution.candidate->manifest.id == "demo.public-domain.human.production");
  CHECK(resolution.candidate->manifest.version == "0.12.0");
  CHECK(!resolution.candidate->contentHash.empty());
}

TEST_CASE("authoring_characterization_public_adapter_surface_remains_available") {
  static_assert(requires(EditorRuntime& runtime,
                         seam::native_ui::RasterCanvas& canvas,
                         const seam::native_ui::PointerEvent& pointer,
                         const seam::native_ui::KeyEvent& key,
                         seam::domain::Project project) {
    { runtime.controller() } -> std::same_as<seam::native_ui::NativeEditorController&>;
    runtime.resize(1100.0, 720.0);
    runtime.paint(canvas);
    runtime.pointerDown(pointer);
    runtime.pointerMove(pointer);
    runtime.pointerUp(pointer);
    runtime.keyDown(key);
    { runtime.projectCopy() } -> std::same_as<seam::domain::Project>;
    { runtime.replaceProject(std::move(project)) } ->
        std::same_as<seam::core::Result<void>>;
    runtime.requestRender(48000U);
    { runtime.renderedPreview() } ->
        std::same_as<std::shared_ptr<const RenderedPreview>>;
    { runtime.revision() } -> std::same_as<std::uint64_t>;
  });
  CHECK(true);
}

TEST_CASE("authoring_characterization_exact_voicebank_resolution_states_are_stable") {
  seam::voicebank::VoicebankCatalog catalog;
  const auto scanned = catalog.scan(fixtureRoots());
  CHECK(scanned);
  CHECK(scanned.value().size() == 1U);
  const auto& candidate = scanned.value().front();

  seam::domain::VoicebankReference exact{
      .id = candidate.manifest.id,
      .version = candidate.manifest.version,
      .contentHash = candidate.contentHash,
  };
  CHECK(catalog.resolve(exact, scanned.value()).resolved());

  auto missing = exact;
  missing.id = "missing.bank";
  CHECK(catalog.resolve(missing, scanned.value()).status ==
        seam::voicebank::VoicebankResolveStatus::Missing);

  auto wrongVersion = exact;
  wrongVersion.version = "999.0.0";
  CHECK(catalog.resolve(wrongVersion, scanned.value()).status ==
        seam::voicebank::VoicebankResolveStatus::VersionMismatch);

  auto noHash = exact;
  noHash.contentHash.clear();
  CHECK(catalog.resolve(noHash, scanned.value()).status ==
        seam::voicebank::VoicebankResolveStatus::ContentHashMissing);

  auto wrongHash = exact;
  wrongHash.contentHash.assign(64U, '0');
  CHECK(catalog.resolve(wrongHash, scanned.value()).status ==
        seam::voicebank::VoicebankResolveStatus::ContentMismatch);
}

TEST_CASE("authoring_characterization_every_canonical_edit_submits_preview_render") {
  EditorRuntime runtime(std::nullopt, std::filesystem::path{"assets/character-01"},
                        fixtureRoots());
  const auto initialPreview = waitReady(runtime);
  CHECK(initialPreview != nullptr);
  CHECK(initialPreview->status == PreviewStatus::Ready);
  CHECK(!initialPreview->unitPlan.empty());

  const auto trackId = runtime.trackId();
  const auto regionId = runtime.regionId();
  auto project = runtime.projectCopy();
  auto* region = project.findRegion(regionId);
  CHECK(region != nullptr);
  CHECK(!region->notes.empty());
  CHECK(!region->lyrics.empty());

  seam::phonemizer::JapaneseKanaPhonemizer phonemizer;
  const auto initialPhonemes = phonemizer.phonemize(*region);
  CHECK(!initialPhonemes.tokens.empty());
  const auto key = initialPhonemes.tokens.front().key;
  const auto unitId = initialPreview->unitPlan.front().unitId;

  region->notes.front().startTick = region->notes.front().startTick + seam::time::Tick{30};
  region->sortNotes();
  checkSubmitted(runtime, [&] { return runtime.replaceProject(project); });

  project = runtime.projectCopy();
  region = project.findRegion(regionId);
  CHECK(region != nullptr);
  region->lyrics.back().surface = U"ま";
  checkSubmitted(runtime, [&] { return runtime.replaceProject(project); });

  checkSubmitted(runtime, [&] {
    return runtime.movePhonemeBoundary(key, false, seam::time::Microseconds{42000});
  });

  checkSubmitted(runtime, [&] {
    return runtime.selectUnitVariant(
        key, unitId, seam::domain::UnitRendererKind::ClassicPsola);
  });

  checkSubmitted(runtime, [&] {
    return runtime.upsertPitchPoint(seam::domain::PitchAutomationPoint{
        .tick = seam::time::Tick{480},
        .cents = 24.0F,
        .interpolation = seam::domain::CurveInterpolation::Linear,
    });
  });
  checkSubmitted(runtime, [&] { return runtime.setPrimarySeamAmount(0.74F); });
  checkSubmitted(runtime, [&] {
    return runtime.setTrackMix(trackId, -1.5F, 0.2F, false, false);
  });
  checkSubmitted(runtime, [&] { return runtime.configureOutputChannels(4U); });

  const auto banks = runtime.availableVoicebanks();
  CHECK(!banks.empty());
  const auto& bank = banks.front();
  checkSubmitted(runtime, [&] {
    return runtime.selectVoicebank(bank.manifest.id, bank.manifest.version,
                                   bank.contentHash);
  });
}

TEST_CASE("authoring_characterization_state_codec_round_trips_canonical_project") {
  EditorRuntime runtime(std::nullopt, std::filesystem::path{"assets/character-01"},
                        fixtureRoots());
  const auto project = runtime.projectCopy();
  const auto encoded = seam::clap_editor::encodeEditorState(project);
  CHECK(encoded);
  CHECK(!encoded.value().empty());
  const auto decoded = seam::clap_editor::decodeEditorState(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value() == project);
}

TEST_CASE("authoring_characterization_newer_preview_revision_wins") {
  EditorRuntime runtime(std::nullopt, std::filesystem::path{"assets/character-01"},
                        fixtureRoots());
  const auto resolution = runtime.voicebankResolution();
  CHECK(resolution.resolved());

  seam::clap_editor::AsyncPreviewRenderService service;
  std::vector<seam::clap_editor::TrackVoicebankResolution> resolutions{
      seam::clap_editor::TrackVoicebankResolution{
          .trackId = runtime.trackId(),
          .resolution = resolution,
      }};
  auto project = runtime.projectCopy();
  service.submit(project, runtime.trackId(), runtime.regionId(), resolutions,
                 101U, 48000U);
  service.submit(std::move(project), runtime.trackId(), runtime.regionId(),
                 std::move(resolutions), 102U, 48000U);

  std::shared_ptr<const RenderedPreview> preview;
  for (int attempt = 0; attempt < 1200; ++attempt) {
    preview = service.latest();
    if (preview != nullptr && preview->revision == 102U &&
        preview->status == PreviewStatus::Ready) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  CHECK(preview != nullptr);
  CHECK(preview->revision == 102U);
  CHECK(preview->status == PreviewStatus::Ready);
  const auto stats = service.stats();
  CHECK(stats.submitted == 2U);
  CHECK(stats.completed == 1U);
  CHECK(stats.cancelled + stats.stale >= 1U);
}

TEST_CASE("authoring_characterization_character_display_does_not_change_render") {
  EditorRuntime minimal(std::nullopt, std::filesystem::path{"assets/character-01"},
                        fixtureRoots());
  const auto minimalPreview = waitReady(minimal);
  CHECK(minimalPreview != nullptr);
  CHECK(minimalPreview->status == PreviewStatus::Ready);

  auto characterOffProject = minimal.projectCopy();
  characterOffProject.settings().characterDisplay =
      seam::domain::CharacterDisplayMode::Off;
  EditorRuntime hidden(std::move(characterOffProject),
                       std::filesystem::path{"assets/character-01"}, fixtureRoots());
  const auto hiddenPreview = waitReady(hidden);
  CHECK(hiddenPreview != nullptr);
  CHECK(hiddenPreview->status == PreviewStatus::Ready);

  CHECK(hiddenPreview->phraseContentHashes == minimalPreview->phraseContentHashes);
  CHECK(hiddenPreview->channelCount == minimalPreview->channelCount);
  CHECK(hiddenPreview->interleaved == minimalPreview->interleaved);
}

TEST_CASE("authoring_characterization_revision_zero_preview_is_published") {
  EditorRuntime seeded(std::nullopt, std::filesystem::path{"assets/character-01"},
                       fixtureRoots());
  const auto seededPreview = waitReady(seeded);
  CHECK(seededPreview != nullptr);
  CHECK(seededPreview->status == PreviewStatus::Ready);

  auto project = seeded.projectCopy();
  EditorRuntime runtime(std::move(project),
                        std::filesystem::path{"assets/character-01"},
                        fixtureRoots());
  CHECK(runtime.revision() == 0U);
  const auto preview = waitReady(runtime);
  CHECK(preview != nullptr);
  CHECK(preview->revision == 0U);
  CHECK(preview->status == PreviewStatus::Ready);
  CHECK(!preview->interleaved.empty());
}
