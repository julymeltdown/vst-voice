#include "test_framework.hpp"

#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/voicebank/catalog.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
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
using seam::clap_editor::RealtimePreviewPublication;
using seam::clap_editor::RenderedPreview;
using Handle = RealtimePreviewPublication::ReadHandle;

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
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds{2};
  while (runtime.renderStats().submitted <= before &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  CHECK(runtime.renderStats().submitted > before);
}

RenderedPreview previewOf(std::uint64_t revision) {
  RenderedPreview preview;
  preview.revision = revision;
  preview.status = PreviewStatus::Ready;
  preview.sampleRate = 48000U;
  preview.interleaved.assign(16U, 0.5F);
  return preview;
}

bool eventually(const std::function<bool()>& condition,
                std::chrono::milliseconds timeout = std::chrono::milliseconds{2000}) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (condition()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  return condition();
}

// Readers hold all three slots of a publication, and the newest of them is the published one, so
// the publication has nowhere to put anything: the situation the cases below are about.
std::vector<Handle> holdEverySlot(RealtimePreviewPublication& publication) {
  std::vector<Handle> held;
  held.push_back(publication.acquire());  // the slot that starts out published, empty
  CHECK(publication.publish(previewOf(1U)));
  held.push_back(publication.acquire());
  CHECK(publication.publish(previewOf(2U)));
  held.push_back(publication.acquire());
  CHECK(held[2]->revision == 2U);
  CHECK(!publication.publish(previewOf(99U)));  // no slot is free
  return held;
}

// The same for a runtime: readers hold three different previews it published, so it has no slot
// for the next one.
std::vector<Handle> holdEveryPreviewSlot(EditorRuntime& runtime) {
  std::vector<Handle> held;
  held.push_back(runtime.acquireRenderedPreview());
  for (int render = 0; render < 2; ++render) {
    runtime.requestRender(48000U);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    bool distinct = false;
    while (!distinct && std::chrono::steady_clock::now() < deadline) {
      auto candidate = runtime.acquireRenderedPreview();
      distinct = candidate &&
                 std::none_of(held.begin(), held.end(),
                              [&](const Handle& prior) { return prior.get() == candidate.get(); });
      if (distinct) {
        held.push_back(std::move(candidate));
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
      }
    }
    CHECK(distinct);
  }
  return held;
}

seam::domain::Project withNothingToSing(seam::domain::Project project) {
  for (auto& track : project.vocalTracks()) {
    for (auto& region : track.regions) {
      region.notes.clear();
      region.lyrics.clear();
      region.unitSelectionOverrides.clear();
      region.phonemeOverrides.clear();
      region.seamOverrides.clear();
    }
  }
  return project;
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
  CHECK(project.settings().characterDisplay == seam::domain::CharacterDisplayMode::Full);

  const auto resolution = runtime.voicebankResolution();
  CHECK(resolution.resolved());
  CHECK(resolution.candidate.has_value());
  CHECK(resolution.candidate->manifest.id == "demo.public-domain.human.production");
  CHECK(resolution.candidate->manifest.version == "0.12.0");
  CHECK(!resolution.candidate->contentHash.empty());
}

TEST_CASE("authoring_characterization_quality_switch_submits_one_render") {
  EditorRuntime runtime(std::nullopt, std::filesystem::path{"assets/character-01"},
                        fixtureRoots());
  const auto initial = waitReady(runtime);
  CHECK(initial != nullptr);
  const auto beforeSubmitted = runtime.renderStats().submitted;
  const auto beforeCompleted = runtime.renderStats().completed;

  runtime.setRenderQuality(seam::rendering::RenderQuality::Final);

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds{20};
  while (std::chrono::steady_clock::now() < deadline) {
    const auto stats = runtime.renderStats();
    if (stats.submitted > beforeSubmitted &&
        stats.completed > beforeCompleted) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  std::this_thread::sleep_for(std::chrono::milliseconds{100});
  CHECK(runtime.renderQuality() == seam::rendering::RenderQuality::Final);
  CHECK(runtime.renderStats().submitted == beforeSubmitted + 1U);
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
  CHECK(initialPreview->interleaved.storageIdentity() ==
        initialPreview->stereo.storageIdentity());

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
  const auto initial = waitReady(runtime);
  CHECK(initial != nullptr);
  CHECK(initial->status == PreviewStatus::Ready);

  auto first = runtime.projectCopy();
  auto* firstRegion = first.findRegion(runtime.regionId());
  CHECK(firstRegion != nullptr);
  CHECK(!firstRegion->notes.empty());
  firstRegion->notes.front().midiKey = 65U;
  CHECK(runtime.replaceProject(std::move(first)));

  auto second = runtime.projectCopy();
  auto* secondRegion = second.findRegion(runtime.regionId());
  CHECK(secondRegion != nullptr);
  CHECK(!secondRegion->notes.empty());
  secondRegion->notes.front().midiKey = 68U;
  CHECK(runtime.replaceProject(std::move(second)));

  const auto preview = waitReady(runtime);
  CHECK(preview != nullptr);
  CHECK(preview->revision == runtime.revision());
  CHECK(preview->status == PreviewStatus::Ready);
  const auto stats = runtime.renderStats();
  CHECK(stats.submitted >= 2U);
  CHECK(stats.completed >= 2U);
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

TEST_CASE("authoring_characterization_a_project_with_nothing_to_sing_no_longer_offers_the_old_preview") {
  // Replacing the project by one with no notes leaves nothing to play. The coordinator keeps the
  // last audio it rendered as history, and the plug-in must not hand that history to the host as the
  // preview: a vocal that is no longer in the project would go on sounding.
  EditorRuntime runtime(std::nullopt, std::filesystem::path{"assets/character-01"},
                        fixtureRoots());
  const auto ready = waitReady(runtime);
  CHECK(ready != nullptr);
  CHECK(ready->status == PreviewStatus::Ready);
  CHECK(!ready->interleaved.empty());

  const auto original = runtime.projectCopy();
  auto emptied = original;
  for (auto& track : emptied.vocalTracks()) {
    for (auto& region : track.regions) {
      region.notes.clear();
      region.lyrics.clear();
      region.unitSelectionOverrides.clear();
      region.phonemeOverrides.clear();
      region.seamOverrides.clear();
    }
  }
  CHECK(runtime.replaceProject(std::move(emptied)));
  CHECK(runtime.projectCopy().noteCount() == 0U);

  // The reset reaches the plug-in on the thread that made the change, so nothing is waited for.
  const auto preview = runtime.renderedPreview();
  CHECK(preview != nullptr);
  CHECK(preview->status == PreviewStatus::Empty);
  CHECK(preview->interleaved.empty());
  CHECK(preview->stereo.empty());
  CHECK(runtime.renderStatusView().state == seam::native_ui::RenderStatusState::Idle);
  CHECK(!runtime.renderStatusView().hasAudibleAudio);

  // Writing the notes back renders and publishes again.
  CHECK(runtime.replaceProject(original));
  const auto restored = waitReady(runtime);
  CHECK(restored != nullptr);
  CHECK(restored->status == PreviewStatus::Ready);
  CHECK(!restored->interleaved.empty());
}

TEST_CASE("a preview that finds every slot held is published as soon as a slot is free") {
  RealtimePreviewPublication publication;
  auto held = holdEverySlot(publication);
  CHECK(!publication.publishWhenFree(previewOf(3U)));
  CHECK(publication.acquire()->revision == 2U);  // the newest that got in, for now
  held[0] = Handle{};                            // a reader lets go of a slot
  CHECK(eventually([&] { return publication.acquire()->revision == 3U; }));
  // The readers that kept their slots still read what they were given.
  CHECK(held[1]->revision == 1U);
  CHECK(held[2]->revision == 2U);
}

TEST_CASE("only the newest of the previews that wait for a slot is published") {
  RealtimePreviewPublication publication;
  auto held = holdEverySlot(publication);
  CHECK(!publication.publishWhenFree(previewOf(3U)));
  CHECK(!publication.publishWhenFree(previewOf(4U)));
  held[0] = Handle{};
  bool sawThird = false;
  CHECK(eventually([&] {
    const auto handle = publication.acquire();
    sawThird = sawThird || handle->revision == 3U;
    return handle->revision == 4U;
  }));
  CHECK(!sawThird);
}

TEST_CASE("a preview that waits for a slot is still offered the second time readers hold every slot") {
  // Two periods of pressure in a row: the helper that served the first is gone when the second
  // begins, so the second needs a new one.
  RealtimePreviewPublication publication;
  auto held = holdEverySlot(publication);
  CHECK(!publication.publishWhenFree(previewOf(3U)));
  held[0] = Handle{};
  CHECK(eventually([&] { return publication.acquire()->revision == 3U; }));
  held[0] = publication.acquire();  // holds revision 3: every slot is held again
  CHECK(held[0]->revision == 3U);
  CHECK(!publication.publishWhenFree(previewOf(4U)));
  held[1] = Handle{};
  CHECK(eventually([&] { return publication.acquire()->revision == 4U; }));
}

TEST_CASE("a revoked publication offers nothing while readers hold every slot, and forgets what was waiting") {
  RealtimePreviewPublication publication;
  auto held = holdEverySlot(publication);
  CHECK(!publication.publishWhenFree(previewOf(3U)));
  publication.revoke();
  {
    const auto handle = publication.acquire();
    CHECK(handle);
    CHECK(handle->status == PreviewStatus::Empty);
    CHECK(handle->interleaved.empty());
  }
  // A reader that held a slot before the revocation reads it until it lets go.
  CHECK(held[2]->status == PreviewStatus::Ready);
  CHECK(held[2]->revision == 2U);
  held.clear();
  // The preview that was waiting for a slot does not come back now that there is one.
  std::this_thread::sleep_for(std::chrono::milliseconds{100});
  CHECK(publication.acquire()->status == PreviewStatus::Empty);
  CHECK(publication.publish(previewOf(5U)));
  CHECK(publication.acquire()->revision == 5U);
}

TEST_CASE("a publication that was revoked uses the free slot, whichever it is, and leaves the slots readers hold") {
  RealtimePreviewPublication publication;
  CHECK(publication.publish(previewOf(1U)));
  const auto first = publication.acquire();   // holds the slot of revision 1
  CHECK(publication.publish(previewOf(2U)));
  const auto second = publication.acquire();  // holds the slot of revision 2
  publication.revoke();
  // The one slot that is free is the one that started out published, and nothing is published
  // now, so it is a candidate like any other.
  CHECK(publication.publish(previewOf(3U)));
  CHECK(publication.acquire()->revision == 3U);
  CHECK(first->revision == 1U);
  CHECK(second->revision == 2U);
  // With the third slot held as well there is nowhere left to put a fourth.
  const auto third = publication.acquire();
  CHECK(!publication.publish(previewOf(4U)));
}

TEST_CASE("authoring_characterization_a_project_with_nothing_to_sing_is_revoked_while_readers_hold_every_slot") {
  // The plug-in's own readers (the audio callback, a paint, a copy) hold slots for a moment, and
  // enough of them at once leave the publication with nowhere to put an empty preview. The preview
  // must be gone all the same, and stay gone when they let go.
  EditorRuntime runtime(std::nullopt, std::filesystem::path{"assets/character-01"},
                        fixtureRoots());
  CHECK(waitReady(runtime)->status == PreviewStatus::Ready);
  auto held = holdEveryPreviewSlot(runtime);
  CHECK(runtime.replaceProject(withNothingToSing(runtime.projectCopy())));
  // Nothing is waited for and no reader lets go.
  CHECK(runtime.renderedPreview()->status == PreviewStatus::Empty);
  {
    const auto handle = runtime.acquireRenderedPreview();
    CHECK(handle->status == PreviewStatus::Empty);
  }
  CHECK(runtime.renderStatusView().state == seam::native_ui::RenderStatusState::Idle);
  held.clear();
  runtime.requestRender(48000U);  // the coordinator is idle: this emits no completion
  std::this_thread::sleep_for(std::chrono::milliseconds{100});
  CHECK(runtime.renderedPreview()->status == PreviewStatus::Empty);
}

TEST_CASE("authoring_characterization_a_render_that_finishes_while_readers_hold_every_slot_is_published_when_one_is_free") {
  EditorRuntime runtime(std::nullopt, std::filesystem::path{"assets/character-01"},
                        fixtureRoots());
  CHECK(waitReady(runtime)->status == PreviewStatus::Ready);
  auto held = holdEveryPreviewSlot(runtime);
  const auto completedBefore = runtime.renderStats().completed;
  auto edited = runtime.projectCopy();
  auto* region = edited.findRegion(runtime.regionId());
  CHECK(region != nullptr);
  CHECK(!region->notes.empty());
  region->notes.front().midiKey = 66U;
  CHECK(runtime.replaceProject(std::move(edited)));
  const auto revision = runtime.revision();
  CHECK(eventually([&] { return runtime.renderStats().completed > completedBefore; },
                   std::chrono::seconds{20}));
  std::this_thread::sleep_for(std::chrono::milliseconds{100});
  // The new audio has nowhere to go yet: the old is still what the host hears.
  CHECK(runtime.renderedPreview()->revision != revision);
  held[0] = Handle{};
  CHECK(eventually(
      [&] {
        const auto preview = runtime.renderedPreview();
        return preview->revision == revision && preview->status == PreviewStatus::Ready &&
               !preview->interleaved.empty();
      },
      std::chrono::seconds{5}));
}

TEST_CASE("authoring_characterization_a_project_emptied_while_a_render_waits_for_a_slot_stays_empty") {
  EditorRuntime runtime(std::nullopt, std::filesystem::path{"assets/character-01"},
                        fixtureRoots());
  CHECK(waitReady(runtime)->status == PreviewStatus::Ready);
  auto held = holdEveryPreviewSlot(runtime);
  const auto completedBefore = runtime.renderStats().completed;
  auto edited = runtime.projectCopy();
  auto* region = edited.findRegion(runtime.regionId());
  CHECK(region != nullptr);
  CHECK(!region->notes.empty());
  region->notes.front().midiKey = 66U;
  CHECK(runtime.replaceProject(edited));
  CHECK(eventually([&] { return runtime.renderStats().completed > completedBefore; },
                   std::chrono::seconds{20}));
  std::this_thread::sleep_for(std::chrono::milliseconds{100});
  // A render is waiting for a slot. The score is emptied before one is free.
  CHECK(runtime.replaceProject(withNothingToSing(edited)));
  CHECK(runtime.renderedPreview()->status == PreviewStatus::Empty);
  held.clear();
  // The render that was waiting does not come back for the notes that are no longer there.
  std::this_thread::sleep_for(std::chrono::milliseconds{150});
  CHECK(runtime.renderedPreview()->status == PreviewStatus::Empty);
}
