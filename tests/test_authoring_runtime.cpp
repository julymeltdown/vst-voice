#include "test_framework.hpp"

#include "seam/authoring/authoring_runtime.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/application/performance_commands.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/rendering/streaming_pcm_source.hpp"
#include "seam/voicebank/catalog.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/core/sha256.hpp"

#include "test_support.hpp"

#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required for authoring runtime tests
#endif

namespace {

struct RuntimeFixture final {
  std::unique_ptr<seam::authoring::ProjectDocument> document;
  seam::domain::TrackId resolvedTrack{};
  seam::domain::RegionId resolvedRegion{};
  seam::domain::TrackId unresolvedTrack{};
  seam::domain::RegionId unresolvedRegion{};
  seam::voicebank::VoicebankCandidate candidate;
};

RuntimeFixture makeFixture(bool includeUnresolved = false) {
  seam::voicebank::VoicebankCatalog catalog;
  const std::vector roots{seam::voicebank::VoicebankSearchRoot{
      .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
      .kind = seam::voicebank::VoicebankRootKind::Development,
  }};
  const auto scanned = catalog.scan(roots);
  if (!scanned || scanned.value().empty()) {
    throw seam::test::Failure{"production voicebank fixture is unavailable"};
  }
  const auto candidate = scanned.value().front();

  seam::application::ProjectFactory factory{1000U};
  auto project = factory.createProject("Authoring runtime test");
  static_cast<void>(project.tempoMap().addOrReplace(seam::time::Tick{0}, 154.0));
  const auto trackId = factory.addVocalTrack(project, "RESOLVED");
  const auto regionId = factory.addRegion(
      project, trackId, "REGION A", seam::time::Tick{0}, seam::time::Tick{7680});
  auto* track = project.findVocalTrack(trackId);
  auto* region = project.findRegion(regionId);
  track->voicebank = seam::domain::VoicebankReference{
      .id = candidate.manifest.id,
      .version = candidate.manifest.version,
      .contentHash = candidate.contentHash,
  };
  const std::vector<std::tuple<std::int64_t, std::uint8_t, const char32_t*>> notes{
      {0, 64U, U"こ"}, {960, 67U, U"え"}, {1920, 69U, U"を"},
      {2880, 67U, U"つ"}, {3840, 64U, U"な"}, {4800, 62U, U"ぐ"},
      {5760, 64U, U"ま"}, {6720, 67U, U"で"},
  };
  const std::vector<std::string> unitIds{
      "demo.ja.g4.k-o.01", "demo.ja.g4.e.01", "demo.ja.g4.o.01",
      "demo.ja.g4.ts-u.01", "demo.ja.g4.n-a.01", "demo.ja.g4.g-u.01",
      "demo.ja.g4.m-a.01", "demo.ja.g4.d-e.01",
  };
  for (std::size_t index = 0U; index < notes.size(); ++index) {
    const auto& [start, key, lyricText] = notes[index];
    auto [lyric, note] = factory.makeNote(
        seam::time::Tick{start}, seam::time::Tick{720}, key,
        std::u32string{lyricText}, seam::domain::Language::Japanese);
    const auto noteId = note.id;
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    const auto* unit = candidate.manifest.findUnit(unitIds[index]);
    CHECK(unit);
    region->unitSelectionOverrides.push_back(
        seam::domain::UnitSelectionOverride{
            .startKey = seam::domain::PhonemeKey{.noteId = noteId, .ordinal = 0U},
            .tokenCount = static_cast<std::uint16_t>(index == 1U || index == 2U ? 1U : 2U),
            .unitId = unitIds[index],
            .renderer = index % 2U == 0U && unit->pitchMarks.size() >= 3U
                            ? seam::domain::UnitRendererKind::ClassicPsola
                            : seam::domain::UnitRendererKind::Raw,
            .locked = true,
        });
  }
  region->sortNotes();

  seam::domain::TrackId missingTrack{};
  seam::domain::RegionId missingRegion{};
  if (includeUnresolved) {
    missingTrack = factory.addVocalTrack(project, "UNRESOLVED");
    missingRegion = factory.addRegion(project, missingTrack, "REGION B",
                                      seam::time::Tick{0}, seam::time::Tick{1920});
    auto* missing = project.findVocalTrack(missingTrack);
    missing->voicebank = seam::domain::VoicebankReference{
        .id = "missing.voicebank", .version = "1.0.0",
        .contentHash = std::string(64U, 'a')};
    auto* missingRegionPtr = project.findRegion(missingRegion);
    auto [lyric, note] = factory.makeNote(seam::time::Tick{0},
                                          seam::time::Tick{960}, 60U, U"こ");
    missingRegionPtr->lyrics.push_back(std::move(lyric));
    missingRegionPtr->notes.push_back(std::move(note));
  }

  auto document = std::unique_ptr<seam::authoring::ProjectDocument>{
      new seam::authoring::ProjectDocument(
          std::move(project),
          seam::application::ProjectFactory{factory.nextIdValue()})};
  return RuntimeFixture{.document = std::move(document),
                        .resolvedTrack = trackId,
                        .resolvedRegion = regionId,
                        .unresolvedTrack = missingTrack,
                        .unresolvedRegion = missingRegion,
                        .candidate = candidate};
}

seam::authoring::AuthoringRuntimeConfig configFor(
    const std::filesystem::path& cacheRoot) {
  return seam::authoring::AuthoringRuntimeConfig{
      .cacheRoot = cacheRoot,
      .voicebankRoots = {seam::voicebank::VoicebankSearchRoot{
          .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
          .kind = seam::voicebank::VoicebankRootKind::Development}},
      .previewSampleRate = 48000U,
      .outputChannels = 2U,
      .allowDevelopmentVoicebanks = true,
  };
}

bool waitReady(seam::authoring::AuthoringRuntime& runtime,
               std::uint64_t revision) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds{20};
  while (std::chrono::steady_clock::now() < deadline) {
    const auto progress = runtime.renderer().progress();
    if (progress.state == seam::authoring::RenderState::Failed) {
      throw seam::test::Failure{progress.diagnostic};
    }
    if (progress.publishedRevision == revision &&
        progress.state == seam::authoring::RenderState::Ready &&
        runtime.transport().state().publishedRevision == revision) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  return false;
}

}  // namespace

TEST_CASE("authoring runtime reopens relative recipe references and rejects changed content") {
  const auto root = seam::test::support::temporaryDirectory("runtime-recipe");
  seam::voice_design::VoiceRecipe recipe;
  recipe.id = "runtime-draft";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  CHECK(seam::voice_design::saveVoiceRecipeFile(root / "singer.json", recipe));
  const auto resource = seam::voice_design::freezeVoiceRecipeResource(recipe); CHECK(resource);
  seam::application::ProjectFactory factory{73000U};
  auto project = factory.createProject("Saved recipe");
  const auto trackId = factory.addVocalTrack(project, "Singer");
  const auto regionId = factory.addRegion(project, trackId, "Vowel", seam::time::Tick{0}, seam::time::Tick{960});
  auto [lyric, note] = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{960}, 69U, U"あ", seam::domain::Language::Japanese);
  const auto noteId = note.id;
  project.findRegion(regionId)->lyrics.push_back(std::move(lyric));
  project.findRegion(regionId)->notes.push_back(std::move(note));
  project.findVocalTrack(trackId)->proceduralRecipe = seam::domain::ProceduralRecipeReference{
      resource.value().identity, "singer.json", "neutral"};
  const std::vector<seam::rendering::TrackSingerSource> noBase{
      seam::rendering::TrackRecipeFileSource{trackId, *project.findVocalTrack(trackId)->proceduralRecipe, {}}};
  const auto unresolved = seam::rendering::ProductionProjectRenderer{}.renderWithSources(
      project, noBase, trackId, regionId, 0U, 48000U);
  CHECK(!unresolved); CHECK(unresolved.error().message.find("saved project directory") != std::string::npos);
  const seam::formats::ProjectJsonCodec codec;
  CHECK(codec.save(project, root / "song.seam"));
  const auto reopened = codec.load(root / "song.seam"); CHECK(reopened);
  auto document = std::unique_ptr<seam::authoring::ProjectDocument>{new seam::authoring::ProjectDocument{
      reopened.value(), seam::application::ProjectFactory{factory.nextIdValue()}}};
  document->markSaved(root / "song.seam", seam::core::sha256Hex(codec.encode(reopened.value()).value()));
  auto config = configFor(root / "cache"); config.voicebankRoots.clear();
  seam::authoring::AuthoringRuntime runtime{std::move(document), config};
  CHECK(runtime.initialize()); CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.selectedTrack() == trackId);
  const auto original = runtime.renderer().latest(); CHECK(original);
  CHECK(original->activeRenderer == "seam.source-filter.v1");
  CHECK(original->activeVoicebankId.empty()); CHECK(!original->result.interleaved.empty());
  CHECK(runtime.execute(std::make_unique<seam::application::MoveNotesCommand>(
      std::vector<seam::application::NoteMove>{{.noteId = noteId, .before = seam::time::Tick{0},
        .after = seam::time::Tick{0}, .beforeKey = 69U, .afterKey = 72U}})));
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.renderer().latest()->result.interleaved != original->result.interleaved);
  CHECK(runtime.undo()); CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.renderer().latest()->result.interleaved == original->result.interleaved);
  auto changed = recipe; changed.seed = 123U;
  CHECK(seam::voice_design::saveVoiceRecipeFile(root / "singer.json", changed));
  runtime.requestPreview(true);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (runtime.renderer().progress().state != seam::authoring::RenderState::Failed && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  CHECK(runtime.renderer().progress().state == seam::authoring::RenderState::Failed);
  CHECK(runtime.renderer().progress().diagnostic.find("identity") != std::string::npos);
  CHECK(runtime.renderer().latest()->result.interleaved == original->result.interleaved);
  CHECK(seam::voice_design::saveVoiceRecipeFile(root / "singer.json", recipe));
  runtime.requestPreview(true); CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.renderer().latest()->result.interleaved == original->result.interleaved);
  CHECK(seam::voice_design::saveVoiceRecipeFile(root / "changed.json", changed));
  const auto changedResource = seam::voice_design::freezeVoiceRecipeResource(changed); CHECK(changedResource);
  const auto beforeSelection = runtime.document().session().project().findVocalTrack(trackId)->proceduralRecipe;
  const seam::domain::ProceduralRecipeReference afterSelection{changedResource.value().identity, "changed.json", "neutral"};
  CHECK(runtime.execute(std::make_unique<seam::application::SetTrackProceduralRecipeCommand>(trackId,
      beforeSelection, afterSelection)));
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.document().dirty());
  CHECK(runtime.renderer().latest()->result.interleaved != original->result.interleaved);
  CHECK(runtime.undo()); CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.renderer().latest()->result.interleaved == original->result.interleaved);
  CHECK(runtime.redo()); CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.renderer().latest()->result.interleaved != original->result.interleaved);
}

TEST_CASE("authoring runtime reports unsupported compiled PSOLA without raw publication") {
  auto fixture = makeFixture();
  auto* region = fixture.document->session().project().findRegion(fixture.resolvedRegion);
  region->unitSelectionOverrides[2].renderer = seam::domain::UnitRendererKind::ClassicPsola;
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document),
      configFor(seam::test::support::temporaryDirectory("unsupported-compiled-psola"))};
  CHECK(runtime.initialize());
  runtime.requestPreview();
  for (int attempt = 0; attempt < 400 && runtime.renderer().progress().state != seam::authoring::RenderState::Failed; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  const auto progress = runtime.renderer().progress();
  CHECK(progress.state == seam::authoring::RenderState::Failed);
  CHECK(progress.diagnostic.find("pitch marks") != std::string::npos);
  CHECK(runtime.renderer().latest()->state != seam::authoring::RenderState::Ready);
  CHECK(runtime.renderer().latest()->result.phraseContentHashes.empty());
}

TEST_CASE("authoring_runtime_note_edit_renders_and_publishes_transport_audio") {
  auto fixture = makeFixture();
  seam::authoring::AuthoringRuntime runtime{
      std::move(fixture.document), configFor(
          std::filesystem::temp_directory_path() / "seam-runtime-note-edit")};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  runtime.requestPreview();
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  const auto before = runtime.renderer().latest();
  CHECK(!before->result.phraseContentHashes.empty());

  const auto* region = runtime.document().session().project().findRegion(
      fixture.resolvedRegion);
  CHECK(region != nullptr);
  const auto& note = region->notes.front();
  const auto beforeRevision = runtime.document().session().revision();
  const auto beforeSubmitted = runtime.renderer().stats().submitted;
  CHECK(runtime.execute(std::make_unique<seam::application::MoveNotesCommand>(
      std::vector<seam::application::NoteMove>{seam::application::NoteMove{
          .noteId = note.id,
          .before = note.startTick,
          .after = note.startTick + seam::time::Tick{120},
          .beforeKey = note.midiKey,
          .afterKey = static_cast<std::uint8_t>(note.midiKey + 1U),
      }})));
  const auto revision = runtime.document().session().revision();
  CHECK(revision == beforeRevision + 1U);
  CHECK(waitReady(runtime, revision));
  CHECK(runtime.renderer().stats().submitted >= beforeSubmitted + 1U);
  const auto after = runtime.renderer().latest();
  CHECK(after->result.phraseContentHashes != before->result.phraseContentHashes);
  CHECK(runtime.transport().state().publishedRevision == revision);
}

TEST_CASE("authoring runtime publishes overlapping voices with consistent progress and undo audio") {
  auto fixture = makeFixture();
  auto* region = fixture.document->session().project().findRegion(fixture.resolvedRegion);
  region->notes.resize(2U);
  region->unitSelectionOverrides.resize(2U);
  region->notes[1].startTick = seam::time::Tick{0};
  const auto editedNote = region->notes[1];
  const auto originalProject = fixture.document->session().project();
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document),
      configFor(seam::test::support::temporaryDirectory("runtime-overlapping-voices"))};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  runtime.requestPreview();
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  const auto checkProgress = [&] {
    const auto progress = runtime.renderer().progress();
    CHECK(progress.totalPhrases == 2U);
    CHECK(progress.completedPhrases == 2U);
    CHECK(progress.fraction == 1.0);
    CHECK(!progress.audibleAudioStale);
    CHECK(runtime.transport().state().available);
  };
  checkProgress();
  const auto initial = runtime.renderer().latest();
  const auto initialHashes = initial->result.phraseContentHashes;
  const std::vector<float> initialPcm(initial->result.interleaved.begin(), initial->result.interleaved.end());
  CHECK(initialHashes.size() == 2U);
  CHECK(!initialPcm.empty());
  CHECK(runtime.execute(std::make_unique<seam::application::MoveNotesCommand>(
      std::vector<seam::application::NoteMove>{{.noteId = editedNote.id,
          .before = editedNote.startTick, .after = editedNote.startTick,
          .beforeKey = editedNote.midiKey,
          .afterKey = static_cast<std::uint8_t>(editedNote.midiKey + 2U)}})));
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  checkProgress();
  const auto changed = runtime.renderer().latest();
  CHECK(changed->result.phraseContentHashes != initialHashes);
  CHECK(std::vector<float>(changed->result.interleaved.begin(), changed->result.interleaved.end()) != initialPcm);
  CHECK(runtime.undo());
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  checkProgress();
  const auto restored = runtime.renderer().latest();
  CHECK(restored->result.phraseContentHashes == initialHashes);
  CHECK(std::vector<float>(restored->result.interleaved.begin(), restored->result.interleaved.end()) == initialPcm);
  CHECK(restored->result.cacheHits == 2U);
  CHECK(runtime.document().session().project() == originalProject);
  CHECK(runtime.transport().seek(0));
  CHECK(runtime.transport().play());
  std::array<float, 512U> block{};
  bool receivedAudio = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
  while (!receivedAudio && std::chrono::steady_clock::now() < deadline) {
    const auto frames = runtime.transport().ringBuffer().readFrames(block);
    CHECK(std::all_of(block.begin(), block.end(), [](float sample) { return std::isfinite(sample); }));
    receivedAudio = frames > 0U && std::any_of(block.begin(), block.end(),
        [](float sample) { return std::abs(sample) > 0.00001F; });
    if (!receivedAudio) std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  CHECK(receivedAudio);
  CHECK(runtime.transport().pause());
}

TEST_CASE("live proposal delivery stays silent until acceptance and undo restores cached audio") {
  using namespace seam::domain;
  using seam::time::Tick;
  auto fixture = makeFixture();
  auto* prepared = fixture.document->session().project().findRegion(fixture.resolvedRegion);
  prepared->notes.resize(1U);
  prepared->unitSelectionOverrides.resize(1U);
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document),
      configFor(seam::test::support::temporaryDirectory("runtime-proposal-acceptance"))};
  CHECK(runtime.initialize());
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  const auto initial = runtime.renderer().latest();
  const auto initialHashes = initial->result.phraseContentHashes;
  const std::vector<float> initialPcm(initial->result.interleaved.begin(), initial->result.interleaved.end());
  const auto submitted = runtime.renderer().stats().submitted;
  const auto originalRevision = runtime.document().session().revision();
  const auto original = runtime.document().session().project();
  const auto& region = *original.findRegion(fixture.resolvedRegion);
  const auto pronunciation = seam::phonemizer::resolveJapanesePronunciation(region);
  CHECK(pronunciation);
  const auto job = runtime.document().session().capturePerformanceJob();
  CHECK(job);
  PerformanceTake proposal{.id = "generated-attack", .sourceRegionId = region.id,
      .capturedRevision = region.performance.revision,
      .resource = {SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
      .pronunciation = pronunciation.value().identity,
      .generatorId = "fixture", .generatorVersion = "1", .range = {Tick{0}, region.durationTick},
      .lanes = {{PerformanceChannel::Attack, {{Tick{0}, 150.0}}}}};
  CHECK(runtime.executePerformanceResult(job.value(),
      std::make_unique<seam::application::AddPerformanceProposalCommand>(region.id, region.performance, proposal)));
  CHECK(runtime.document().session().revision() == originalRevision + 1U);
  CHECK(runtime.document().dirty());
  // Observe beyond the runtime's 20 ms debounce deadline, not just before a
  // mistakenly scheduled render would have had a chance to submit.
  std::this_thread::sleep_for(std::chrono::milliseconds{80});
  CHECK(runtime.renderer().stats().submitted == submitted);
  CHECK(runtime.transport().state().publishedRevision == originalRevision);
  CHECK(!runtime.renderer().progress().audibleAudioStale);
  CHECK(runtime.renderer().latest()->result.phraseContentHashes == initialHashes);
  const auto stored = runtime.document().session().project();
  CHECK(runtime.execute(std::make_unique<seam::application::SetAcceptedPerformanceCommand>(
      region.id, stored.findRegion(region.id)->performance,
      std::vector<AcceptedPerformanceSelection>{{proposal.id, PerformanceChannel::Attack, region.notes[0].id, Tick{0}}})));
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.renderer().stats().submitted == submitted + 1U);
  const auto accepted = runtime.renderer().latest();
  CHECK(accepted->result.phraseContentHashes != initialHashes);
  CHECK(std::vector<float>(accepted->result.interleaved.begin(), accepted->result.interleaved.end()) != initialPcm);
  CHECK(runtime.undo());
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.document().session().project() == stored);
  const auto restored = runtime.renderer().latest();
  CHECK(restored->result.phraseContentHashes == initialHashes);
  CHECK(restored->result.cacheHits == restored->result.phraseCount);
  CHECK(std::vector<float>(restored->result.interleaved.begin(), restored->result.interleaved.end()) == initialPcm);
  const auto afterUndoSubmissions = runtime.renderer().stats().submitted;
  CHECK(runtime.undo()); // Remove only the inert proposal.
  CHECK(runtime.document().session().project() == original);
  std::this_thread::sleep_for(std::chrono::milliseconds{80});
  CHECK(runtime.renderer().stats().submitted == afterUndoSubmissions);
}

TEST_CASE("authoring_runtime_renders_backing_only_projects") {
  const auto root = seam::test::support::temporaryDirectory(
      "authoring-backing-only");
  const auto media = root / "backing.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      media, 48000U,
      seam::test::support::sineWave(48000U, 220.0, 0.05)));
  const auto source = seam::rendering::StreamingPcmSource::open(media, 4096U);
  CHECK(source);
  seam::application::ProjectFactory factory{30000U};
  auto project = factory.createProject("Backing only");
  project.audioTracks().push_back(seam::domain::AudioTrack{
      .id = factory.nextTrackId(),
      .name = "Backing",
      .mediaPath = media.string(),
      .mediaHash = source.value()->info().contentHash,
      .mediaOwnership = seam::domain::MediaOwnership::ExternalReference,
      .originalFilename = media.filename().string(),
      .sourceSampleRate = source.value()->info().sampleRate,
      .sourceChannels = source.value()->info().channels,
      .sourceFrameCount = source.value()->info().frameCount,
      .startTick = seam::time::Tick{0},
      .outputRoute = seam::domain::TrackOutputRoute{
          .bus = seam::domain::BusId{1U},
          .matrix = seam::domain::RoutingMatrix::monoToStereo(),
      },
  });
  CHECK(project.validate());
  auto document = std::unique_ptr<seam::authoring::ProjectDocument>{
      new seam::authoring::ProjectDocument(
          std::move(project),
          seam::application::ProjectFactory{factory.nextIdValue()})};
  seam::authoring::AuthoringRuntime runtime{
      std::move(document),
      seam::authoring::AuthoringRuntimeConfig{
          .cacheRoot = root / "cache",
          .voicebankRoots = {},
          .previewSampleRate = 48000U,
          .outputChannels = 2U,
          .allowDevelopmentVoicebanks = false,
  }};
  CHECK(runtime.initialize());
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  const auto preview = runtime.renderer().latest();
  CHECK(preview->state == seam::authoring::RenderState::Ready);
  CHECK(!preview->result.interleaved.empty());
  CHECK(runtime.transport().state().available);
}

TEST_CASE("authoring_runtime_quality_change_renders_final_audio") {
  auto fixture = makeFixture();
  seam::authoring::AuthoringRuntime runtime{
      std::move(fixture.document), configFor(
          std::filesystem::temp_directory_path() / "seam-runtime-quality")};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  const auto revision = runtime.document().session().revision();
  CHECK(waitReady(runtime, revision));
  const auto beforeSubmitted = runtime.renderer().stats().submitted;
  const auto before = runtime.renderer().latest();
  CHECK(before->quality == seam::rendering::RenderQuality::Preview);

  runtime.setRenderQuality(seam::rendering::RenderQuality::Final);

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds{20};
  while (std::chrono::steady_clock::now() < deadline) {
    const auto progress = runtime.renderer().progress();
    const auto latest = runtime.renderer().latest();
    if (progress.state == seam::authoring::RenderState::Ready &&
        progress.publishedRevision == revision &&
        latest->projectRevision == revision &&
        latest->quality == seam::rendering::RenderQuality::Final) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }

  const auto after = runtime.renderer().latest();
  CHECK(runtime.renderQuality() == seam::rendering::RenderQuality::Final);
  CHECK(after->quality == seam::rendering::RenderQuality::Final);
  CHECK(after->projectRevision == revision);
  CHECK(runtime.renderer().stats().submitted >= beforeSubmitted + 1U);
}

TEST_CASE("authoring_runtime_seam_preview is transient and restores canonical audio") {
  auto fixture = makeFixture();
  auto* fixtureRegion = fixture.document->session().project().findRegion(
      fixture.resolvedRegion);
  CHECK(fixtureRegion != nullptr);
  const auto key = seam::domain::PhonemeKey{
      .noteId = fixtureRegion->notes.front().id, .ordinal = 0U};
  fixtureRegion->seamOverrides.push_back(seam::domain::SeamOverride{
      .incomingStartKey = key,
      .seamAmount = 0.9F,
      .overlap = seam::time::Microseconds{4'000},
      .phaseReset = 1.0F,
      .envelopeBlend = 0.1F,
      .curve = seam::domain::SeamCurve::HardCharacter,
      .locked = true,
  });
  seam::authoring::AuthoringRuntime runtime{
      std::move(fixture.document), configFor(
          std::filesystem::temp_directory_path() / "seam-runtime-seam-preview")};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  const auto revision = runtime.document().session().revision();
  CHECK(waitReady(runtime, revision));
  const auto beforeProject = runtime.document().session().project();
  const auto beforeSubmitted = runtime.renderer().stats().submitted;

  CHECK(runtime.previewSeam(key, true));
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds{20};
  while (!runtime.seamPreviewReady() &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  CHECK(runtime.seamPreviewActive());
  CHECK(runtime.seamPreviewReady());
  CHECK(runtime.document().session().project() == beforeProject);
  CHECK(runtime.document().session().revision() == revision);
  CHECK(runtime.renderer().stats().submitted == beforeSubmitted);
  CHECK(runtime.transport().state().publishedRevision == revision);

  CHECK(runtime.previewSeam(key, false));
  CHECK(!runtime.seamPreviewActive());
  CHECK(!runtime.seamPreviewReady());
  CHECK(runtime.document().session().project() == beforeProject);
  CHECK(runtime.transport().state().publishedRevision == revision);
}

TEST_CASE("authoring_runtime_development_voicebank_policy_is_enforced") {
  auto fixture = makeFixture();
  auto config = configFor(
      std::filesystem::temp_directory_path() / "seam-runtime-dev-policy");
  config.allowDevelopmentVoicebanks = false;
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document),
                                             std::move(config)};
  CHECK(runtime.initialize());

  const auto resolution = runtime.voicebanks().resolveTrack(
      runtime.document().session().project(), fixture.resolvedTrack);
  CHECK(resolution.status ==
        seam::voicebank::VoicebankResolveStatus::Untrusted);
  CHECK(!resolution.candidate.has_value());
}

TEST_CASE("authoring_runtime_selection_does_not_mutate_project") {
  auto fixture = makeFixture();
  seam::authoring::AuthoringRuntime runtime{
      std::move(fixture.document), configFor(
          std::filesystem::temp_directory_path() / "seam-runtime-selection")};
  CHECK(runtime.initialize());
  const auto before = runtime.document().session().project();
  const auto revision = runtime.document().session().revision();
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  CHECK(runtime.document().session().project() == before);
  CHECK(runtime.document().session().revision() == revision);
}

TEST_CASE("authoring_runtime_mutes_unresolved_tracks_in_render_copy") {
  auto fixture = makeFixture(true);
  seam::authoring::AuthoringRuntime runtime{
      std::move(fixture.document), configFor(
          std::filesystem::temp_directory_path() / "seam-runtime-unresolved")};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  runtime.requestPreview();
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  const auto audio = runtime.renderer().latest();
  CHECK(audio->state == seam::authoring::RenderState::Ready);
  CHECK(audio->result.trackCount == 1U);
  const auto* unresolved = runtime.document().session().project().findVocalTrack(
      fixture.unresolvedTrack);
  CHECK(unresolved != nullptr);
  CHECK(!unresolved->muted);
}

TEST_CASE("authoring_runtime_supported_technical_edit_submits_once") {
  auto fixture = makeFixture();
  seam::authoring::AuthoringRuntime runtime{
      std::move(fixture.document), configFor(
          std::filesystem::temp_directory_path() / "seam-runtime-technical")};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  runtime.requestPreview();
  CHECK(waitReady(runtime, runtime.document().session().revision()));

  const auto* region = runtime.document().session().project().findRegion(
      fixture.resolvedRegion);
  CHECK(region != nullptr);
  const seam::domain::PhonemeKey key{
      .noteId = region->notes.front().id, .ordinal = 1U};
  const auto beforeRevision = runtime.document().session().revision();
  const auto beforeSubmitted = runtime.renderer().stats().submitted;
  CHECK(runtime.technicalEdits().movePhonemeBoundary(
      key, false, seam::time::Microseconds{200000}));
  CHECK(runtime.document().session().revision() == beforeRevision + 1U);
  CHECK(waitReady(runtime, beforeRevision + 1U));
  CHECK(runtime.renderer().stats().submitted >= beforeSubmitted + 1U);
  const auto published = runtime.renderer().latest();
  CHECK(published->impact.scope ==
        seam::application::CommandAudioImpact::PhraseAudio);
  CHECK(published->impact.regionIds.size() == 1U);
  CHECK(published->impact.regionIds.front() == fixture.resolvedRegion);
}

TEST_CASE("authoring_runtime_coalesces_rapid_audio_edits_to_latest_revision") {
  auto fixture = makeFixture();
  auto* fixtureRegion = fixture.document->session().project().findRegion(
      fixture.resolvedRegion);
  CHECK(fixtureRegion != nullptr);
  fixtureRegion->unitSelectionOverrides.clear();
  seam::authoring::AuthoringRuntime runtime{
      std::move(fixture.document), configFor(
          std::filesystem::temp_directory_path() / "seam-runtime-debounce")};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  CHECK(waitReady(runtime, runtime.document().session().revision()));

  const auto* region = runtime.document().session().project().findRegion(
      fixture.resolvedRegion);
  CHECK(region != nullptr);
  CHECK(!region->lyrics.empty());
  const auto lyricId = region->lyrics.front().id;
  const auto beforeSubmitted = runtime.renderer().stats().submitted;

  for (std::size_t index = 0U; index < 100U; ++index) {
    CHECK(runtime.execute(std::make_unique<seam::application::SetLyricCommand>(
        lyricId, index % 2U == 0U ? U"こ" : U"え",
        seam::domain::Language::Japanese)));
  }

  const auto latestRevision = runtime.document().session().revision();
  CHECK(latestRevision >= 100U);
  CHECK(waitReady(runtime, latestRevision));
  const auto stats = runtime.renderer().stats();
  CHECK(stats.submitted - beforeSubmitted <= 2U);
  CHECK(stats.completed >= 1U);
  CHECK(runtime.renderer().latest()->projectRevision == latestRevision);
}
