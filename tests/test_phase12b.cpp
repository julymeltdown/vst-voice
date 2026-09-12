#include "seam/application/project_factory.hpp"
#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/clap_editor/host_timeline.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/voicebank/catalog.hpp"

#include <algorithm>
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
  return nullptr;
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
  // The original fixture edits the interior end of /k/ while forcing /k o/.
  // Without source alignment that unit cannot render this edit. Previously a
  // partial Preview (other successful phrases) was mistaken for complete Final.
  const auto partialFinal = runtime.prepareOfflineRender(std::chrono::seconds{30});
  if (partialFinal || runtime.offlineRenderReady() || runtime.acquireOfflineRenderedPreview() ||
      partialFinal.error().message.find("interior phoneme timing edit") == std::string::npos) {
    std::cerr << "unsupported interior edit was not rejected as incomplete Final";
    if (!partialFinal) std::cerr << ": " << partialFinal.error().message;
    std::cerr << '\n';
    return 38;
  }
  // Undo both experimental actions through the real edit history, then use the
  // supported nucleus-start boundary for the successful offline lifecycle.
  for (int action = 0; action < 2; ++action) runtime.keyDown(native_ui::KeyEvent{
      .key = native_ui::NativeKey::Z, .modifiers = native_ui::InputModifiers{.control = true}});
  if (runtime.projectCopy() != activeProject) {
    std::cerr << "undo did not restore the exact pre-experiment project\n";
    return 39;
  }
  // Undo schedules a replacement render. A retained partial publication has
  // no plan for the previously failed phrase and cannot authorize selection.
  const auto restoredPreview = waitReady(runtime);
  if (!restoredPreview || std::none_of(restoredPreview->unitPlan.begin(), restoredPreview->unitPlan.end(),
      [&](const auto& entry) { return entry.unitId == unitEntry.unitId && entry.tokenStart == unitEntry.tokenStart; })) {
    std::cerr << "undo did not publish a current selectable unit plan\n";
    return 41;
  }
  const auto begin = phonemes.tokens.begin() + static_cast<std::ptrdiff_t>(unitEntry.tokenStart);
  const auto end = begin + static_cast<std::ptrdiff_t>(std::min<std::size_t>(
      unitEntry.tokenCount, phonemes.tokens.size() - unitEntry.tokenStart));
  const auto nucleus = std::find_if(begin, end, [](const auto& token) {
    return token.role == domain::PhonemeRole::Nucleus;
  });
  if (nucleus == end || !runtime.selectUnitVariant(key, unitEntry.unitId, domain::UnitRendererKind::SpectralClassic) ||
      !runtime.movePhonemeBoundary(nucleus->key, true, time::Microseconds{45000})) return 40;
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
  const auto offlinePrepared = runtime.prepareOfflineRender(
      std::chrono::seconds{30});
  const auto offlineView = runtime.offlineRenderView();
  if (!offlinePrepared ||
      offlineView.state != clap_editor::OfflineRenderState::Ready ||
      !offlineView.hasAudio || offlineView.identity.projectRevision !=
          runtime.revision()) {
    std::cerr << "initial Final preparation failed: result=" << static_cast<bool>(offlinePrepared)
              << " state=" << clap_editor::OfflineRenderSession::stateName(offlineView.state)
              << " diagnostic=" << offlineView.diagnostic
              << " identityRevision=" << offlineView.identity.projectRevision
              << " currentRevision=" << runtime.revision();
    if (!offlinePrepared) std::cerr << " error=" << offlinePrepared.error().message
                                    << " context=" << offlinePrepared.error().context;
    std::cerr << '\n';
    return 21;
  }
  {
    const auto finalAudio = runtime.acquireOfflineRenderedPreview();
    if (!finalAudio || !finalAudio->offlineSource ||
        finalAudio->offlineSource->quality != rendering::RenderQuality::Final ||
        finalAudio->offlineSource->projectRevision != runtime.revision() ||
        finalAudio->offlineSource->result.sampleRate != finalAudio->sampleRate ||
        finalAudio->offlineSource->result.interleaved.storageIdentity() != finalAudio->interleaved.storageIdentity()) return 27;
    // Same project revision but a new rate/request must invalidate the old
    // captured Final source before its replacement reaches Ready.
    runtime.requestRender(44100U);
    if (runtime.offlineRenderReady() || runtime.acquireOfflineRenderedPreview() ||
        runtime.offlineRenderView().state != clap_editor::OfflineRenderState::Stale) return 28;
  }
  if (!runtime.prepareOfflineRender(std::chrono::seconds{30})) return 29;
  {
    const auto finalAudio = runtime.acquireOfflineRenderedPreview();
    if (!finalAudio || finalAudio->sampleRate != 44100U) return 30;
  }
  runtime.setRenderQuality(rendering::RenderQuality::Preview);
  if (runtime.offlineRenderReady() || runtime.acquireOfflineRenderedPreview()) return 31;
  runtime.setOfflineTimingAuthority(clap_editor::OfflineTimingAuthority::FollowHost);
  const auto unsupportedFollowHost = runtime.prepareOfflineRender(std::chrono::seconds{30});
  if (unsupportedFollowHost || unsupportedFollowHost.error().code != core::ErrorCode::Unsupported ||
      runtime.offlineRenderReady() || runtime.offlineRenderView().state != clap_editor::OfflineRenderState::Failed) return 32;
  runtime.setOfflineTimingAuthority(clap_editor::OfflineTimingAuthority::FixedAudio);
  runtime.requestRender(48000U);
  if (!runtime.prepareOfflineRender(std::chrono::seconds{30})) return 33;
  // A supported edit submits a new coordinator identity even before PCM is
  // published. It must invalidate captured Final audio without callback locks.
  if (!runtime.setTrackMix(secondTrack, -3.0F, 0.0F, false, false)) return 34;
  if (runtime.offlineRenderReady() || runtime.acquireOfflineRenderedPreview()) return 35;
  if (!runtime.prepareOfflineRender(std::chrono::seconds{30})) return 36;
  // Non-stereo auxiliary previews must not detach the exact Final PCM, and
  // routing edits must invalidate it before the debounced request is submitted.
  for (const auto channels : std::array<std::uint8_t, 4>{1U, 2U, 8U, 4U}) {
    if (!runtime.configureOutputChannels(channels)) return 42;
    if (runtime.offlineRenderReady() || runtime.acquireOfflineRenderedPreview()) return 43;
    if (!runtime.prepareOfflineRender(std::chrono::seconds{30})) return 44;
    const auto audio = runtime.acquireOfflineRenderedPreview();
    if (!audio || audio->channelCount != channels || !audio->offlineSource ||
        audio->interleaved.storageIdentity() != audio->offlineSource->result.interleaved.storageIdentity()) return 45;
  }
  auto edited = waitReady(runtime);
  if (edited == nullptr || edited->channelCount != 4U ||
      edited->revision != runtime.revision()) {
    std::cerr << "post-invalidation Preview publication failed: currentRevision=" << runtime.revision();
    if (edited) std::cerr << " previewRevision=" << edited->revision
                          << " channels=" << static_cast<unsigned>(edited->channelCount)
                          << " diagnostic=" << edited->diagnostic;
    std::cerr << '\n';
    return 37;
  }

  auto persisted = runtime.projectCopy();
  formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(persisted);
  if (!encoded || encoded.value().find("\"schemaVersion\": " + std::to_string(formats::ProjectJsonCodec::kSchemaVersion)) ==
                      std::string::npos ||
      encoded.value().find("\"hostStartOffsetTick\": 960") ==
                      std::string::npos) {
    return 22;
  }
  const auto decoded = codec.decode(encoded.value());
  if (!decoded || decoded.value() != persisted) return 23;

  // Exercise a real historical writer payload, not a current document with
  // its version number relabelled while retaining newer fields.
  const auto migrated = codec.load(std::filesystem::path{SEAM_SOURCE_SCHEMA4_FIXTURE});
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
