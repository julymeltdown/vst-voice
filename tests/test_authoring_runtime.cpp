#include "test_framework.hpp"

#include "seam/authoring/authoring_runtime.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/application/arrangement_commands.hpp"
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
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stop_token>
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
               std::uint64_t revision,
               std::chrono::seconds timeout = std::chrono::seconds{20}) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
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

// Holds the preview render in flight, so a test can look at the runtime while a newer attempt is
// running. The hook runs on the render worker after it has published "rendering".
struct RenderGate final {
  std::mutex mutex;
  std::condition_variable_any changed;
  bool armed{false};
  bool entered{false};
  bool released{false};

  seam::authoring::RenderCoordinatorHooks hooks() {
    seam::authoring::RenderCoordinatorHooks value;
    value.beforeRender = [this](std::uint64_t, std::stop_token token) {
      std::unique_lock lock(mutex);
      if (!armed) return;
      entered = true;
      changed.notify_all();
      static_cast<void>(changed.wait(lock, token, [this] { return released; }));
    };
    return value;
  }
  void arm() {
    std::lock_guard lock(mutex);
    armed = true;
    entered = false;
    released = false;
  }
  [[nodiscard]] bool waitEntered(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex);
    return changed.wait_for(lock, timeout, [this] { return entered; });
  }
  void release() {
    std::lock_guard lock(mutex);
    released = true;
    changed.notify_all();
  }
};

// Keeps the thread that called a runtime hook inside it until release(), so a test can act while
// that thread is at a known point. The wait is bounded, so a test that fails before it releases
// does not leave the runtime's destructor, which joins that thread, waiting for long.
struct HeldHook final {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered{false};
  bool released{false};

  void pass() {
    std::unique_lock lock(mutex);
    entered = true;
    changed.notify_all();
    static_cast<void>(
        changed.wait_for(lock, std::chrono::seconds{20}, [this] { return released; }));
  }
  [[nodiscard]] bool waitEntered(std::chrono::seconds timeout) {
    std::unique_lock lock(mutex);
    return changed.wait_for(lock, timeout, [this] { return entered; });
  }
  void release() {
    std::lock_guard lock(mutex);
    released = true;
    changed.notify_all();
  }
};

// Declared after the runtime, so it is destroyed first: a check that throws while a hook is held
// lets go of it, and whatever else is waiting on it, before the runtime is torn down.
struct ReleaseHeldOnExit final {
  HeldHook& hook;
  ~ReleaseHeldOnExit() { hook.release(); }
};

// A project whose only note sings a sound the bundled bank does not have (it has no stand-alone
// /a/), so its first render fails and the lyric can then be changed to one the bank sings.
struct UncoveredLyricFixture final {
  std::unique_ptr<seam::authoring::ProjectDocument> document;
  seam::domain::TrackId track{};
  seam::domain::RegionId region{};
  seam::domain::LyricTokenId lyric{};
};

UncoveredLyricFixture makeUncoveredLyricFixture() {
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
  auto project = factory.createProject("Uncovered lyric");
  static_cast<void>(project.tempoMap().addOrReplace(seam::time::Tick{0}, 120.0));
  const auto trackId = factory.addVocalTrack(project, "VOICE");
  const auto regionId = factory.addRegion(
      project, trackId, "PHRASE", seam::time::Tick{0}, seam::time::Tick{3840});
  auto* track = project.findVocalTrack(trackId);
  auto* region = project.findRegion(regionId);
  track->voicebank = seam::domain::VoicebankReference{
      .id = candidate.manifest.id,
      .version = candidate.manifest.version,
      .contentHash = candidate.contentHash,
  };
  auto [lyric, note] = factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{960}, 64U, U"あ",
      seam::domain::Language::Japanese);
  const auto lyricId = lyric.id;
  region->lyrics.push_back(std::move(lyric));
  region->notes.push_back(std::move(note));
  region->sortNotes();
  auto document = std::unique_ptr<seam::authoring::ProjectDocument>{
      new seam::authoring::ProjectDocument(
          std::move(project),
          seam::application::ProjectFactory{factory.nextIdValue()})};
  return UncoveredLyricFixture{.document = std::move(document),
                               .track = trackId,
                               .region = regionId,
                               .lyric = lyricId};
}

bool hasDiagnostic(const std::vector<seam::authoring::Diagnostic>& diagnostics,
                   std::string_view code) {
  return std::any_of(diagnostics.begin(), diagnostics.end(),
                     [code](const auto& value) { return value.code == code; });
}

bool waitForRenderState(seam::authoring::AuthoringRuntime& runtime,
                        seam::authoring::RenderState state,
                        std::uint64_t requestedRevision = 0U) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
  while (std::chrono::steady_clock::now() < deadline) {
    const auto progress = runtime.renderer().progress();
    if (progress.state == state &&
        (requestedRevision == 0U || progress.requestedRevision == requestedRevision)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  return false;
}

// The runtime records a failure from the completion callback, a moment after the coordinator
// publishes "failed", so a test that saw the state waits for the record too.
bool waitForDiagnostic(seam::authoring::AuthoringRuntime& runtime, std::string_view code) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (std::chrono::steady_clock::now() < deadline) {
    if (hasDiagnostic(runtime.diagnostics(), code)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  return false;
}

// True once the coordinator has counted `count` cancelled requests: the moment an abandoned render
// has finished winding down.
bool waitForCancelled(seam::authoring::AuthoringRuntime& runtime, std::uint64_t count) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (std::chrono::steady_clock::now() < deadline) {
    if (runtime.renderer().stats().cancelled >= count) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  return false;
}

// A short backing track, so that a render has something to sound when no vocal can be rendered.
seam::domain::AudioTrack makeBackingTrack(seam::authoring::AuthoringRuntime& runtime,
                                          const std::filesystem::path& directory) {
  const auto media = directory / "backing.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      media, 48000U, seam::test::support::sineWave(48000U, 220.0, 0.05)));
  const auto source = seam::rendering::StreamingPcmSource::open(media, 4096U);
  if (!source) throw seam::test::Failure{"the backing fixture cannot be opened"};
  return seam::domain::AudioTrack{
      .id = runtime.document().factory().nextTrackId(),
      .name = "Backing",
      .mediaPath = media.string(),
      .mediaHash = source.value()->info().contentHash,
      .mediaOwnership = seam::domain::MediaOwnership::ExternalReference,
      .originalFilename = media.filename().string(),
      .sourceSampleRate = source.value()->info().sampleRate,
      .sourceChannels = source.value()->info().channels,
      .sourceFrameCount = source.value()->info().frameCount,
      .startTick = seam::time::Tick{0},
      .outputRoute = {.bus = seam::domain::BusId{1U},
                      .matrix = seam::domain::RoutingMatrix::monoToStereo()},
  };
}

std::vector<seam::domain::NoteId> noteIdsOf(seam::authoring::AuthoringRuntime& runtime,
                                            seam::domain::RegionId regionId) {
  const auto* region = runtime.document().session().project().findRegion(regionId);
  if (region == nullptr) throw seam::test::Failure{"the region is missing"};
  std::vector<seam::domain::NoteId> ids;
  for (const auto& note : region->notes) ids.push_back(note.id);
  return ids;
}

// Stops the transport's feeder and fills its control queue, so that the transport refuses whatever
// it is asked next: a feeder that has stopped draining, or has fallen a long way behind.
void holdTransportFull(seam::authoring::AuthoringRuntime& runtime) {
  runtime.transport().shutdown();
  unsigned accepted = 0U;
  while (accepted < 4096U && runtime.transport().setLoop({})) ++accepted;
  CHECK(accepted > 0U);
  CHECK(accepted < 4096U);
}

template <typename Predicate>
bool eventually(Predicate predicate,
                std::chrono::milliseconds limit = std::chrono::seconds{10}) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (!predicate() && std::chrono::steady_clock::now() < end)
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  return predicate();
}

// True while the runtime reports, for the given reason, that the transport is behind the score.
bool reportsTransportBehind(const seam::authoring::AuthoringRuntime& runtime, std::string_view key) {
  const auto diagnostics = runtime.diagnostics();
  return std::any_of(diagnostics.begin(), diagnostics.end(), [key](const auto& value) {
    return value.code == "RENDER_STALE" && value.messageKey == key;
  });
}

// True once the ring has yielded nothing but silence for a run of reads. What the feeder produced
// before it was told to stop is read out first.
bool ringGoesSilent(seam::authoring::AuthoringRuntime& runtime) {
  std::vector<float> samples(2048U);
  unsigned quiet = 0U;
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{3};
  while (quiet < 20U && std::chrono::steady_clock::now() < end) {
    std::fill(samples.begin(), samples.end(), 0.0F);
    static_cast<void>(runtime.transport().ringBuffer().readFrames(samples));
    const bool silent = std::all_of(samples.begin(), samples.end(),
                                    [](float value) { return value == 0.0F; });
    quiet = silent ? quiet + 1U : 0U;
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return quiet >= 20U;
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

TEST_CASE("authoring runtime clearSelection lets go of the vocal target and its technical edits") {
  auto fixture = makeFixture();
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document),
      configFor(seam::test::support::temporaryDirectory("runtime-clear-selection"))};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  CHECK(runtime.selectedTrack() == fixture.resolvedTrack);
  CHECK(runtime.selectedRegion() == fixture.resolvedRegion);
  CHECK(runtime.technicalEdits().regionId() == fixture.resolvedRegion);

  runtime.clearSelection();
  CHECK(!runtime.selectedTrack().valid());
  CHECK(!runtime.selectedRegion().valid());
  CHECK(!runtime.technicalEdits().regionId().valid());

  // Clearing what is already clear is harmless, and a selection can be made again afterwards.
  runtime.clearSelection();
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectedTrack() == fixture.resolvedTrack);
  CHECK(runtime.selectedRegion().valid());
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

TEST_CASE("authoring runtime does not publish a seam preview that was revoked while its completion was on its way") {
  // A seam preview that has finished rendering reaches the runtime on the render thread. When the
  // score is emptied before it gets there, the audio of the score that was must not reach the
  // transport afterwards: nothing is left to play.
  HeldHook held;
  auto fixture = makeFixture();
  auto config = configFor(seam::test::support::temporaryDirectory("runtime-seam-preview-revoked"));
  config.beforeSeamPreviewPublication = [&held] { held.pass(); };
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  ReleaseHeldOnExit releaseOnExit{held};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  const auto notes = noteIdsOf(runtime, fixture.resolvedRegion);
  const auto key = seam::domain::PhonemeKey{.noteId = notes.front(), .ordinal = 0U};

  CHECK(runtime.previewSeam(key, true));
  CHECK(held.waitEntered(std::chrono::seconds{10}));
  CHECK(runtime.seamPreviewActive());

  CHECK(runtime.execute(std::make_unique<seam::application::RemoveNotesCommand>(notes)));
  CHECK(!runtime.seamPreviewActive());
  CHECK(!runtime.transport().state().available);

  held.release();
  // The completion now runs its course. Nothing it does may put audio back on the transport.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{300};
  while (std::chrono::steady_clock::now() < deadline &&
         !runtime.transport().state().available && !runtime.seamPreviewReady()) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  CHECK(!runtime.transport().state().available);
  CHECK(!runtime.seamPreviewReady());
  CHECK(!runtime.seamPreviewActive());
}

TEST_CASE("authoring runtime keeps a newer seam preview when an older one's completion arrives after it was asked for") {
  // The older preview has rendered and its completion is on its way when the newer one is asked
  // for. By the time the older completion runs, the coordinator describes the newer request. It
  // must neither publish the older audio nor end the newer request.
  HeldHook older;
  HeldHook newer;
  std::atomic<int> arrivals{0};
  std::atomic<bool> seamAnnounced{false};
  auto fixture = makeFixture();
  auto config = configFor(seam::test::support::temporaryDirectory("runtime-seam-preview-replaced"));
  config.beforeSeamPreviewPublication = [&] {
    (arrivals.fetch_add(1) == 0 ? older : newer).pass();
  };
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  // The runtime tells its observer when a seam preview becomes audible. The canonical render
  // completes through the same callback, and is told apart by the flag it cannot have set.
  runtime.setCompletionCallback([&] {
    if (runtime.seamPreviewReady()) seamAnnounced.store(true);
  });
  ReleaseHeldOnExit releaseOlder{older};
  ReleaseHeldOnExit releaseNewer{newer};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  const auto notes = noteIdsOf(runtime, fixture.resolvedRegion);
  CHECK(notes.size() >= 2U);
  const auto olderKey = seam::domain::PhonemeKey{.noteId = notes[0], .ordinal = 0U};
  const auto newerKey = seam::domain::PhonemeKey{.noteId = notes[1], .ordinal = 0U};

  CHECK(runtime.previewSeam(olderKey, true));
  CHECK(older.waitEntered(std::chrono::seconds{10}));
  CHECK(runtime.previewSeam(newerKey, true));
  CHECK(runtime.seamPreviewActive());
  CHECK(!runtime.seamPreviewReady());

  older.release();
  CHECK(newer.waitEntered(std::chrono::seconds{20}));
  // The older completion has come and gone, and the newer one is now on its way.
  CHECK(runtime.seamPreviewActive());
  CHECK(!runtime.seamPreviewReady());

  newer.release();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
  while (!(runtime.seamPreviewReady() && seamAnnounced.load()) &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  CHECK(runtime.seamPreviewActive());
  CHECK(runtime.seamPreviewReady());
  CHECK(seamAnnounced.load());
}

TEST_CASE("authoring runtime restores the canonical audio only after a seam preview that is being published has finished") {
  // The restore of the canonical audio is the last word on what the transport plays. A seam
  // preview completion that is already handing its audio to the transport must finish first, or
  // it lands after the restore and the preview keeps sounding after it was taken away.
  HeldHook held;
  std::atomic<bool> started{false};
  std::atomic<bool> finished{false};
  std::atomic<bool> restored{false};
  auto fixture = makeFixture();
  auto config = configFor(seam::test::support::temporaryDirectory("runtime-seam-preview-restore"));
  config.duringSeamPreviewPublication = [&held] { held.pass(); };
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  std::jthread restorer;
  ReleaseHeldOnExit releaseOnExit{held};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  const auto revision = runtime.document().session().revision();
  CHECK(waitReady(runtime, revision));
  const auto key = seam::domain::PhonemeKey{
      .noteId = noteIdsOf(runtime, fixture.resolvedRegion).front(), .ordinal = 0U};

  CHECK(runtime.previewSeam(key, true));
  CHECK(held.waitEntered(std::chrono::seconds{10}));

  restorer = std::jthread([&] {
    started.store(true);
    restored.store(static_cast<bool>(runtime.previewSeam(key, false)));
    finished.store(true);
  });
  const auto startDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (!started.load() && std::chrono::steady_clock::now() < startDeadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  CHECK(started.load());
  std::this_thread::sleep_for(std::chrono::milliseconds{200});
  CHECK(!finished.load());

  held.release();
  restorer.join();
  CHECK(restored.load());
  CHECK(!runtime.seamPreviewActive());
  CHECK(!runtime.seamPreviewReady());
  CHECK(runtime.transport().state().available);
  CHECK(runtime.transport().state().publishedRevision == revision);
}

TEST_CASE("authoring runtime stops wanting a seam preview whose render failed") {
  // A preview that cannot be rendered must not leave the runtime waiting for audio that will never
  // come: while it is wanted, a canonical render would not publish over it.
  HeldHook held;
  auto fixture = makeUncoveredLyricFixture();
  auto config = configFor(seam::test::support::temporaryDirectory("runtime-seam-preview-failed"));
  config.beforeSeamPreviewPublication = [&held] { held.pass(); };
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  ReleaseHeldOnExit releaseOnExit{held};
  CHECK(runtime.initialize());
  CHECK(waitForRenderState(runtime, seam::authoring::RenderState::Failed));
  const auto key = seam::domain::PhonemeKey{
      .noteId = noteIdsOf(runtime, fixture.region).front(), .ordinal = 0U};

  CHECK(runtime.previewSeam(key, true));
  CHECK(held.waitEntered(std::chrono::seconds{10}));
  CHECK(runtime.seamPreviewActive());

  held.release();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
  while (runtime.seamPreviewActive() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  CHECK(!runtime.seamPreviewActive());
  CHECK(!runtime.seamPreviewReady());
}

TEST_CASE("authoring runtime takes a request for a seam preview only after one that is being published has finished") {
  // The flags that say a seam preview is wanted and ready, and the request for it, change under
  // the lock a completion holds while it publishes. A request made while an older preview is being
  // handed to the transport waits for that. Without it the older one publishes after the newer was
  // asked for, and marks the newer one ready before it has rendered.
  HeldHook publishing;
  HeldHook newerArrived;
  std::atomic<int> publications{0};
  std::atomic<int> completions{0};
  std::atomic<bool> started{false};
  std::atomic<bool> finished{false};
  std::atomic<bool> accepted{false};
  auto fixture = makeFixture();
  auto config = configFor(seam::test::support::temporaryDirectory("runtime-seam-preview-asked-late"));
  config.duringSeamPreviewPublication = [&] {
    if (publications.fetch_add(1) == 0) publishing.pass();
  };
  config.beforeSeamPreviewPublication = [&] {
    if (completions.fetch_add(1) == 1) newerArrived.pass();
  };
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  std::jthread asker;
  ReleaseHeldOnExit releasePublishing{publishing};
  ReleaseHeldOnExit releaseNewer{newerArrived};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  const auto notes = noteIdsOf(runtime, fixture.resolvedRegion);
  CHECK(notes.size() >= 2U);
  const auto olderKey = seam::domain::PhonemeKey{.noteId = notes[0], .ordinal = 0U};
  const auto newerKey = seam::domain::PhonemeKey{.noteId = notes[1], .ordinal = 0U};

  CHECK(runtime.previewSeam(olderKey, true));
  CHECK(publishing.waitEntered(std::chrono::seconds{10}));

  asker = std::jthread([&] {
    started.store(true);
    accepted.store(static_cast<bool>(runtime.previewSeam(newerKey, true)));
    finished.store(true);
  });
  const auto startDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (!started.load() && std::chrono::steady_clock::now() < startDeadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  CHECK(started.load());
  std::this_thread::sleep_for(std::chrono::milliseconds{200});
  CHECK(!finished.load());

  publishing.release();
  asker.join();
  CHECK(accepted.load());

  // The newer completion has reached the runtime. The flags describe the newer request: wanted, and
  // not yet ready, whatever the older preview did on its way out.
  CHECK(newerArrived.waitEntered(std::chrono::seconds{20}));
  CHECK(runtime.seamPreviewActive());
  CHECK(!runtime.seamPreviewReady());

  newerArrived.release();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
  while (!runtime.seamPreviewReady() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  CHECK(runtime.seamPreviewActive());
  CHECK(runtime.seamPreviewReady());
}

TEST_CASE("authoring runtime ends a seam preview on every change that makes it stale") {
  // Each path that changes the score, or tells the runtime it changed, revokes the preview on its
  // own: an edit, its undo and its redo, and a document that changed under the runtime.
  auto fixture = makeFixture();
  seam::authoring::AuthoringRuntime runtime{
      std::move(fixture.document),
      configFor(seam::test::support::temporaryDirectory("runtime-seam-preview-stale"))};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  const auto notes = noteIdsOf(runtime, fixture.resolvedRegion);
  const auto key = seam::domain::PhonemeKey{.noteId = notes.front(), .ordinal = 0U};
  const auto hearSeamPreview = [&] {
    CHECK(runtime.previewSeam(key, true));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
    while (!runtime.seamPreviewReady() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    CHECK(runtime.seamPreviewActive());
    CHECK(runtime.seamPreviewReady());
  };
  const auto expectNoSeamPreview = [&] {
    CHECK(!runtime.seamPreviewActive());
    CHECK(!runtime.seamPreviewReady());
  };

  hearSeamPreview();
  CHECK(runtime.execute(std::make_unique<seam::application::RemoveNotesCommand>(
      std::vector<seam::domain::NoteId>{notes.back()})));
  expectNoSeamPreview();

  hearSeamPreview();
  CHECK(runtime.undo());
  expectNoSeamPreview();

  hearSeamPreview();
  CHECK(runtime.redo());
  expectNoSeamPreview();

  hearSeamPreview();
  runtime.handleDocumentChanged();
  expectNoSeamPreview();
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

TEST_CASE("authoring runtime hides the last render's failure while a newer render is in flight") {
  // The failure describes an attempt the newer one has replaced. While the creator's fix is
  // rendering, the status must not keep saying the render did not complete.
  RenderGate gate;
  auto fixture = makeUncoveredLyricFixture();
  auto config = configFor(seam::test::support::temporaryDirectory("runtime-superseded-failure"));
  config.renderHooks = gate.hooks();
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  CHECK(runtime.initialize());

  CHECK(waitForRenderState(runtime, seam::authoring::RenderState::Failed));
  CHECK(runtime.renderer().progress().diagnostic.find("No voicebank unit covers") !=
        std::string::npos);
  CHECK(waitForDiagnostic(runtime, "RENDER_FAILED"));

  // The creator changes the lyric to one the bank sings; the new render is held in flight.
  // Before that, an unrelated problem is on record too (a command that named no lyric): it is
  // not the last render's outcome, so a newer render must not hide it.
  CHECK(!runtime.execute(std::make_unique<seam::application::SetLyricCommand>(
      seam::domain::LyricTokenId{999999U}, U"こ", seam::domain::Language::Japanese)));
  CHECK(hasDiagnostic(runtime.diagnostics(), "PROJECT_NOT_FOUND"));
  gate.arm();
  CHECK(runtime.execute(std::make_unique<seam::application::SetLyricCommand>(
      fixture.lyric, U"こ", seam::domain::Language::Japanese)));
  const auto revision = runtime.document().session().revision();
  CHECK(gate.waitEntered(std::chrono::seconds{10}));
  CHECK(runtime.renderer().progress().state == seam::authoring::RenderState::Rendering);
  CHECK(!hasDiagnostic(runtime.diagnostics(), "RENDER_FAILED"));
  CHECK(hasDiagnostic(runtime.diagnostics(), "PROJECT_NOT_FOUND"));

  // When it finishes, the failure is gone for good.
  gate.release();
  CHECK(waitForRenderState(runtime, seam::authoring::RenderState::Ready, revision));
  CHECK(!hasDiagnostic(runtime.diagnostics(), "RENDER_FAILED"));
}

TEST_CASE("authoring runtime shows the last failure again when the newer render is cancelled") {
  // A newer attempt hides the last failure only while it is running. If it is cancelled, nothing
  // has succeeded, so the last outcome still stands.
  RenderGate gate;
  auto fixture = makeUncoveredLyricFixture();
  auto config = configFor(seam::test::support::temporaryDirectory("runtime-cancelled-retry"));
  config.renderHooks = gate.hooks();
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  CHECK(runtime.initialize());
  CHECK(waitForRenderState(runtime, seam::authoring::RenderState::Failed));
  CHECK(waitForDiagnostic(runtime, "RENDER_FAILED"));

  gate.arm();
  CHECK(runtime.execute(std::make_unique<seam::application::SetLyricCommand>(
      fixture.lyric, U"こ", seam::domain::Language::Japanese)));
  CHECK(gate.waitEntered(std::chrono::seconds{10}));
  CHECK(!hasDiagnostic(runtime.diagnostics(), "RENDER_FAILED"));

  runtime.renderer().cancel();
  CHECK(runtime.renderer().progress().state == seam::authoring::RenderState::Cancelled);
  CHECK(hasDiagnostic(runtime.diagnostics(), "RENDER_FAILED"));
  gate.release();
}

TEST_CASE("authoring runtime hides the last render's failure while a newer render is still queued") {
  // The new request waits out the coordinator's debounce before it starts. That interval is the
  // first thing the creator sees after a fix, so the old failure has to be gone from it too.
  auto fixture = makeUncoveredLyricFixture();
  auto config = configFor(seam::test::support::temporaryDirectory("runtime-queued-failure"));
  config.renderHooks.debounceInterval = std::chrono::seconds{30};
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  CHECK(runtime.initialize());
  // The first render is immediate, so it does not wait; it fails on the uncovered lyric.
  CHECK(waitForRenderState(runtime, seam::authoring::RenderState::Failed));
  CHECK(waitForDiagnostic(runtime, "RENDER_FAILED"));

  CHECK(runtime.execute(std::make_unique<seam::application::SetLyricCommand>(
      fixture.lyric, U"こ", seam::domain::Language::Japanese)));
  const auto revision = runtime.document().session().revision();
  CHECK(waitForRenderState(runtime, seam::authoring::RenderState::Queued, revision));
  CHECK(!hasDiagnostic(runtime.diagnostics(), "RENDER_FAILED"));

  // The request is cancelled before its debounce ends; nothing succeeded, so the failure is back.
  runtime.renderer().cancel();
  CHECK(runtime.renderer().progress().state == seam::authoring::RenderState::Cancelled);
  CHECK(hasDiagnostic(runtime.diagnostics(), "RENDER_FAILED"));
}

// A vocal whose bank cannot be found is a condition of the project, not the outcome of a render.
// Backing audio does not change it: the render that follows sounds the backing alone, and the
// vocal is still missing. None of the attempts below may hide it.
TEST_CASE("authoring runtime keeps an unresolved vocal bank on record while a backing render is in flight") {
  RenderGate gate;
  auto fixture = makeFixture();
  const auto root = seam::test::support::temporaryDirectory("runtime-missing-bank-in-flight");
  auto config = configFor(root / "cache");
  config.voicebankRoots.clear();
  config.enableTransport = false;
  config.renderHooks = gate.hooks();
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  CHECK(runtime.initialize());
  CHECK(runtime.renderer().progress().state == seam::authoring::RenderState::Idle);
  CHECK(hasDiagnostic(runtime.diagnostics(), "BANK_MISSING"));

  gate.arm();
  CHECK(runtime.execute(std::make_unique<seam::application::AddAudioTrackCommand>(
      makeBackingTrack(runtime, root))));
  CHECK(gate.waitEntered(std::chrono::seconds{10}));
  CHECK(runtime.renderer().progress().state == seam::authoring::RenderState::Rendering);
  CHECK(!runtime.voicebanks()
             .resolveTrack(runtime.document().session().project(), fixture.resolvedTrack)
             .resolved());
  CHECK(hasDiagnostic(runtime.diagnostics(), "BANK_MISSING"));

  runtime.renderer().cancel();
  CHECK(runtime.renderer().progress().state == seam::authoring::RenderState::Cancelled);
  CHECK(hasDiagnostic(runtime.diagnostics(), "BANK_MISSING"));
  gate.release();
}

TEST_CASE("authoring runtime keeps an unresolved vocal bank on record while a backing render is queued") {
  auto fixture = makeFixture();
  const auto root = seam::test::support::temporaryDirectory("runtime-missing-bank-queued");
  auto config = configFor(root / "cache");
  config.voicebankRoots.clear();
  config.enableTransport = false;
  config.renderHooks.debounceInterval = std::chrono::seconds{30};
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  CHECK(runtime.initialize());
  CHECK(hasDiagnostic(runtime.diagnostics(), "BANK_MISSING"));

  CHECK(runtime.execute(std::make_unique<seam::application::AddAudioTrackCommand>(
      makeBackingTrack(runtime, root))));
  const auto revision = runtime.document().session().revision();
  CHECK(waitForRenderState(runtime, seam::authoring::RenderState::Queued, revision));
  CHECK(hasDiagnostic(runtime.diagnostics(), "BANK_MISSING"));

  runtime.renderer().cancel();
  CHECK(runtime.renderer().progress().state == seam::authoring::RenderState::Cancelled);
  CHECK(hasDiagnostic(runtime.diagnostics(), "BANK_MISSING"));
}

TEST_CASE("authoring runtime still reports an unresolved vocal bank after a backing-only render succeeds") {
  // A successful render clears the outcome of the attempts before it. It says nothing about a vocal
  // bank it was never given.
  auto fixture = makeFixture();
  const auto root = seam::test::support::temporaryDirectory("runtime-missing-bank-partial");
  auto config = configFor(root / "cache");
  config.voicebankRoots.clear();
  config.enableTransport = false;
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  std::atomic<int> completions{0};
  runtime.setCompletionCallback([&completions] { completions.fetch_add(1); });
  CHECK(runtime.initialize());
  CHECK(hasDiagnostic(runtime.diagnostics(), "BANK_MISSING"));

  CHECK(runtime.execute(std::make_unique<seam::application::AddAudioTrackCommand>(
      makeBackingTrack(runtime, root))));
  const auto revision = runtime.document().session().revision();
  CHECK(waitForRenderState(runtime, seam::authoring::RenderState::Ready, revision));
  // The completion callback is where a finished render is folded into the diagnostics; it runs a
  // moment after the state is published.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (completions.load() < 1 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  CHECK(completions.load() >= 1);
  CHECK(hasDiagnostic(runtime.diagnostics(), "BANK_MISSING"));
}

TEST_CASE("authoring runtime stops reporting a missing vocal bank once the bank resolves") {
  auto fixture = makeFixture();
  auto config = configFor(seam::test::support::temporaryDirectory("runtime-missing-bank-resolves"));
  config.voicebankRoots.clear();
  config.enableTransport = false;
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  CHECK(runtime.initialize());
  CHECK(hasDiagnostic(runtime.diagnostics(), "BANK_MISSING"));

  // The bank is installed, and the application asks for a render as it does after any change.
  CHECK(runtime.voicebanks().addSearchRoot(seam::voicebank::VoicebankSearchRoot{
      .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
      .kind = seam::voicebank::VoicebankRootKind::Development}));
  runtime.requestPreview(true);
  CHECK(waitForRenderState(runtime, seam::authoring::RenderState::Ready));
  CHECK(!hasDiagnostic(runtime.diagnostics(), "BANK_MISSING"));
}

TEST_CASE("authoring runtime does not report a missing bank for a resolved track that has no region yet") {
  const auto reference = makeFixture().candidate;
  seam::application::ProjectFactory factory{5000U};
  auto project = factory.createProject("No region yet");
  const auto trackId = factory.addVocalTrack(project, "VOICE");
  project.findVocalTrack(trackId)->voicebank = seam::domain::VoicebankReference{
      .id = reference.manifest.id,
      .version = reference.manifest.version,
      .contentHash = reference.contentHash};
  auto document = std::unique_ptr<seam::authoring::ProjectDocument>{
      new seam::authoring::ProjectDocument(
          std::move(project), seam::application::ProjectFactory{factory.nextIdValue()})};
  auto config = configFor(seam::test::support::temporaryDirectory("runtime-resolved-no-region"));
  config.enableTransport = false;
  seam::authoring::AuthoringRuntime runtime{std::move(document), config};
  CHECK(runtime.initialize());
  CHECK(runtime.voicebanks()
            .resolveTrack(runtime.document().session().project(), trackId)
            .resolved());
  CHECK(!hasDiagnostic(runtime.diagnostics(), "BANK_MISSING"));
}

TEST_CASE("authoring runtime keeps reporting a missing bank after a dismissal until the bank resolves") {
  // Dismissing clears what was recorded. A condition that still holds is not a record.
  auto fixture = makeFixture();
  auto config = configFor(seam::test::support::temporaryDirectory("runtime-missing-bank-dismissed"));
  config.voicebankRoots.clear();
  config.enableTransport = false;
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  CHECK(runtime.initialize());
  CHECK(hasDiagnostic(runtime.diagnostics(), "BANK_MISSING"));
  runtime.clearDiagnostics();
  CHECK(hasDiagnostic(runtime.diagnostics(), "BANK_MISSING"));
}

TEST_CASE("authoring runtime settles on idle when the last note is deleted, and the old failure goes with it") {
  auto fixture = makeUncoveredLyricFixture();
  auto config = configFor(seam::test::support::temporaryDirectory("runtime-last-note-deleted"));
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  CHECK(runtime.initialize());
  CHECK(waitForRenderState(runtime, seam::authoring::RenderState::Failed));
  CHECK(waitForDiagnostic(runtime, "RENDER_FAILED"));

  const auto notes = noteIdsOf(runtime, fixture.region);
  CHECK(notes.size() == 1U);
  CHECK(runtime.execute(std::make_unique<seam::application::RemoveNotesCommand>(notes)));

  // The score is empty. There is no render, so no render failed and no attempt is on screen.
  const auto progress = runtime.renderer().progress();
  CHECK(progress.state == seam::authoring::RenderState::Idle);
  CHECK(progress.failure == seam::authoring::RenderFailureKind::None);
  CHECK(progress.diagnostic.empty());
  CHECK(!hasDiagnostic(runtime.diagnostics(), "RENDER_FAILED"));
  CHECK(!hasDiagnostic(runtime.diagnostics(), "BANK_MISSING"));
  CHECK(!hasDiagnostic(runtime.diagnostics(), "BANK_UNTRUSTED"));
}

TEST_CASE("authoring runtime settles on idle when the last note is deleted while its render is in flight") {
  // The render for the score that was finishes as a stale one and is dropped. Nothing was left to
  // report its end, so the status stayed on "rendering" for good.
  RenderGate gate;
  auto fixture = makeUncoveredLyricFixture();
  auto config = configFor(seam::test::support::temporaryDirectory("runtime-last-note-in-flight"));
  config.renderHooks = gate.hooks();
  gate.arm();
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  CHECK(runtime.initialize());
  CHECK(gate.waitEntered(std::chrono::seconds{10}));
  CHECK(runtime.renderer().progress().state == seam::authoring::RenderState::Rendering);

  CHECK(runtime.execute(std::make_unique<seam::application::RemoveNotesCommand>(
      noteIdsOf(runtime, fixture.region))));
  CHECK(runtime.renderer().progress().state == seam::authoring::RenderState::Idle);
  gate.release();
  CHECK(waitForCancelled(runtime, 1U));
  CHECK(runtime.renderer().progress().state == seam::authoring::RenderState::Idle);
  CHECK(!hasDiagnostic(runtime.diagnostics(), "RENDER_FAILED"));
}

TEST_CASE("authoring runtime stops offering the old audio to the transport when the score is emptied") {
  auto fixture = makeFixture();
  seam::authoring::AuthoringRuntime runtime{
      std::move(fixture.document),
      configFor(seam::test::support::temporaryDirectory("runtime-emptied-transport"))};
  CHECK(runtime.initialize());
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.transport().state().available);

  CHECK(runtime.execute(std::make_unique<seam::application::RemoveNotesCommand>(
      noteIdsOf(runtime, fixture.resolvedRegion))));
  CHECK(!runtime.transport().state().available);
  CHECK(!runtime.transport().state().playing);
  CHECK(runtime.renderer().progress().state == seam::authoring::RenderState::Idle);

  // Bringing the notes back renders and publishes again.
  CHECK(runtime.undo());
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.transport().state().available);
}

TEST_CASE("authoring runtime settles on idle when the only voice that could sing stops being available") {
  auto fixture = makeFixture();
  seam::authoring::AuthoringRuntime runtime{
      std::move(fixture.document),
      configFor(seam::test::support::temporaryDirectory("runtime-voice-unavailable"))};
  CHECK(runtime.initialize());
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.transport().state().available);
  CHECK(!hasDiagnostic(runtime.diagnostics(), "BANK_MISSING"));

  // The development bank stops being trusted, so the track's bank can no longer be used. The notes
  // are still in the score, but nothing can sing them and there is no backing audio.
  runtime.voicebanks().setAllowDevelopmentFixtures(false);
  runtime.requestPreview(true);
  CHECK(!runtime.voicebanks()
             .resolveTrack(runtime.document().session().project(), fixture.resolvedTrack)
             .resolved());
  CHECK(runtime.renderer().progress().state == seam::authoring::RenderState::Idle);
  CHECK(!runtime.transport().state().available);
  CHECK(hasDiagnostic(runtime.diagnostics(), "BANK_MISSING"));
}

TEST_CASE("authoring runtime keeps rendering the backing audio when the last note is deleted") {
  auto fixture = makeFixture();
  const auto root = seam::test::support::temporaryDirectory("runtime-backing-remains");
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document), configFor(root / "cache")};
  CHECK(runtime.initialize());
  CHECK(runtime.execute(std::make_unique<seam::application::AddAudioTrackCommand>(
      makeBackingTrack(runtime, root))));
  CHECK(waitReady(runtime, runtime.document().session().revision()));

  CHECK(runtime.execute(std::make_unique<seam::application::RemoveNotesCommand>(
      noteIdsOf(runtime, fixture.resolvedRegion))));
  // The backing still sounds, so a new render follows the edit and replaces the old audio.
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.renderer().progress().state == seam::authoring::RenderState::Ready);
  CHECK(runtime.transport().state().available);
}

TEST_CASE("authoring runtime renders the whole score when the region on screen has no notes") {
  // The region on screen has no notes, but the score still sounds in another region of the same
  // track, and the renderer renders every region that has notes. An edit made while the empty
  // region is selected changes what the creator hears, so it renders again like any other edit;
  // the audio must not stay at the score as it was.
  auto fixture = makeFixture();
  const auto emptyRegion = fixture.document->factory().addRegion(
      fixture.document->session().project(), fixture.resolvedTrack, "EMPTY",
      seam::time::Tick{7680}, seam::time::Tick{1920});
  const auto track = fixture.resolvedTrack;
  seam::authoring::AuthoringRuntime runtime{
      std::move(fixture.document),
      configFor(seam::test::support::temporaryDirectory("runtime-empty-selected-region"))};
  CHECK(runtime.initialize());
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.selectRegion(emptyRegion));
  const auto submitted = runtime.renderer().stats().submitted;

  CHECK(runtime.execute(std::make_unique<seam::application::SetVocalTrackMixCommand>(
      track, -6.0F, 0.0F, false, false)));
  CHECK(waitReady(runtime, runtime.document().session().revision(), std::chrono::seconds{5}));
  CHECK(runtime.renderer().stats().submitted == submitted + 1U);
  CHECK(runtime.transport().state().available);

  // Undoing it renders the earlier mix again, just the same.
  CHECK(runtime.undo());
  CHECK(waitReady(runtime, runtime.document().session().revision(), std::chrono::seconds{5}));
  CHECK(runtime.renderer().stats().submitted == submitted + 2U);
}

TEST_CASE("authoring runtime settles a render in flight when an edit is made with an empty region selected") {
  // The render in flight is for the score as it was. It finishes as a stale one and is dropped, so
  // the edit has to ask for a render of its own: nothing else is left to report the end of the old
  // one, and the status would stay on "rendering" for good.
  RenderGate gate;
  auto fixture = makeFixture();
  const auto emptyRegion = fixture.document->factory().addRegion(
      fixture.document->session().project(), fixture.resolvedTrack, "EMPTY",
      seam::time::Tick{7680}, seam::time::Tick{1920});
  const auto track = fixture.resolvedTrack;
  auto config = configFor(seam::test::support::temporaryDirectory("runtime-empty-region-in-flight"));
  config.renderHooks = gate.hooks();
  gate.arm();
  seam::authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  CHECK(runtime.initialize());
  CHECK(gate.waitEntered(std::chrono::seconds{10}));
  CHECK(runtime.renderer().progress().state == seam::authoring::RenderState::Rendering);

  CHECK(runtime.selectRegion(emptyRegion));
  CHECK(runtime.execute(std::make_unique<seam::application::SetVocalTrackMixCommand>(
      track, -6.0F, 0.0F, false, false)));
  gate.release();
  const auto revision = runtime.document().session().revision();
  CHECK(waitReady(runtime, revision, std::chrono::seconds{5}));
  CHECK(runtime.renderer().progress().state == seam::authoring::RenderState::Ready);
  CHECK(runtime.transport().state().publishedRevision == revision);
}

TEST_CASE("authoring runtime reports the performance of the region on screen, and none for an empty one") {
  // The renderer renders every region that has notes and reports the performance of one of them:
  // the region on screen. An empty region has none to report, and the score's other regions are not
  // offered in its place, so the views beside it never show another region's performance under
  // this one's name.
  auto fixture = makeFixture();
  const auto emptyRegion = fixture.document->factory().addRegion(
      fixture.document->session().project(), fixture.resolvedTrack, "EMPTY",
      seam::time::Tick{7680}, seam::time::Tick{1920});
  const auto track = fixture.resolvedTrack;
  const auto region = fixture.resolvedRegion;
  seam::authoring::AuthoringRuntime runtime{
      std::move(fixture.document),
      configFor(seam::test::support::temporaryDirectory("runtime-empty-region-performance"))};
  CHECK(runtime.initialize());
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  {
    const auto first = runtime.renderer().latest();
    CHECK(first->result.performanceRegionId == region);
    CHECK(!first->result.activeUnitPlan.empty());
  }

  CHECK(runtime.selectRegion(emptyRegion));
  CHECK(runtime.execute(std::make_unique<seam::application::SetVocalTrackMixCommand>(
      track, -6.0F, 0.0F, false, false)));
  CHECK(waitReady(runtime, runtime.document().session().revision(), std::chrono::seconds{5}));
  {
    const auto empty = runtime.renderer().latest();
    CHECK(empty->result.performanceRegionId == emptyRegion);
    CHECK(empty->result.performanceCues.empty());
    CHECK(empty->result.activeUnitPlan.empty());
    // The score itself was rendered: the region with notes is in it.
    CHECK(empty->result.phraseCount > 0U);
    CHECK(!empty->result.interleaved.empty());
  }

  CHECK(runtime.selectRegion(region));
  CHECK(runtime.execute(std::make_unique<seam::application::SetVocalTrackMixCommand>(
      track, -3.0F, 0.0F, false, false)));
  CHECK(waitReady(runtime, runtime.document().session().revision(), std::chrono::seconds{5}));
  {
    const auto back = runtime.renderer().latest();
    CHECK(back->result.performanceRegionId == region);
    CHECK(!back->result.activeUnitPlan.empty());
  }
}

TEST_CASE("authoring runtime does not settle as empty while the transport still holds the old audio") {
  // The transport's control queue is full, so it refuses to let go of the audio it holds. The score
  // was emptied, but the old audio is still loaded and would sound once the feeder ran again. The
  // runtime used to drop the refusal and report an idle, empty score all the same.
  using namespace seam;
  auto fixture = makeFixture();
  std::atomic<unsigned> announced{0U};
  authoring::AuthoringRuntime runtime{
      std::move(fixture.document),
      configFor(test::support::temporaryDirectory("runtime-refused-clear"))};
  CHECK(runtime.initialize());
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.transport().play());
  CHECK(eventually([&] { return runtime.transport().state().playing; }));
  holdTransportFull(runtime);

  const auto rejectedBefore = runtime.transport().feederStats().rejectedCommands;
  CHECK(runtime.execute(std::make_unique<application::RemoveNotesCommand>(
      noteIdsOf(runtime, fixture.resolvedRegion))));
  CHECK(runtime.transport().feederStats().rejectedCommands > rejectedBefore);

  // The audio is still there, and the runtime says that it is still there.
  CHECK(runtime.transport().state().available);
  CHECK(reportsTransportBehind(runtime, "playback.clear-pending"));
  // Dismissing records does not make a condition that still holds go away.
  runtime.clearDiagnostics();
  CHECK(reportsTransportBehind(runtime, "playback.clear-pending"));

  // The feeder takes commands again and the runtime finishes the clear on its own.
  runtime.setCompletionCallback([&announced] { announced.fetch_add(1U); });
  CHECK(runtime.transport().start());
  CHECK(eventually([&] { return !runtime.transport().state().available; }));
  CHECK(eventually([&] { return !runtime.transport().state().playing; }));
  CHECK(eventually([&] { return !reportsTransportBehind(runtime, "playback.clear-pending"); }));
  // A window that paints on demand has to be told: nothing else happens to ask it to repaint.
  CHECK(eventually([&] { return announced.load() > 0U; }));
  CHECK(ringGoesSilent(runtime));
  CHECK(runtime.renderer().progress().state == authoring::RenderState::Idle);
}

TEST_CASE("authoring runtime hands a finished render to the transport once the transport can take it") {
  // The render is ready but the transport refuses it, so the transport keeps playing the older
  // audio. The runtime used to drop the refusal; the status said ready and the old version played.
  using namespace seam;
  auto fixture = makeFixture();
  authoring::AuthoringRuntime runtime{
      std::move(fixture.document),
      configFor(test::support::temporaryDirectory("runtime-refused-publication"))};
  CHECK(runtime.initialize());
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  const auto oldRevision = runtime.transport().state().publishedRevision;
  holdTransportFull(runtime);

  CHECK(runtime.execute(std::make_unique<application::SetVocalTrackMixCommand>(
      fixture.resolvedTrack, -6.0F, 0.0F, false, false)));
  const auto revision = runtime.document().session().revision();
  CHECK(revision > oldRevision);
  CHECK(waitForRenderState(runtime, authoring::RenderState::Ready, revision));
  CHECK(eventually([&] { return reportsTransportBehind(runtime, "playback.update-pending"); },
                   std::chrono::seconds{3}));
  CHECK(runtime.transport().state().publishedRevision == oldRevision);

  CHECK(runtime.transport().start());
  CHECK(waitReady(runtime, revision));
  CHECK(eventually([&] { return !reportsTransportBehind(runtime, "playback.update-pending"); }));
}

TEST_CASE("authoring runtime lets a newer render replace a clear it still owes") {
  // Emptying the score owes the transport a clear; bringing the notes back owes it the new audio
  // instead. The clear is not carried out late over the audio that followed it.
  using namespace seam;
  auto fixture = makeFixture();
  authoring::AuthoringRuntime runtime{
      std::move(fixture.document),
      configFor(test::support::temporaryDirectory("runtime-owed-clear-replaced"))};
  CHECK(runtime.initialize());
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  holdTransportFull(runtime);

  CHECK(runtime.execute(std::make_unique<application::RemoveNotesCommand>(
      noteIdsOf(runtime, fixture.resolvedRegion))));
  CHECK(reportsTransportBehind(runtime, "playback.clear-pending"));
  CHECK(runtime.undo());
  const auto revision = runtime.document().session().revision();
  CHECK(eventually([&] { return reportsTransportBehind(runtime, "playback.update-pending"); },
                   std::chrono::seconds{5}));
  CHECK(!reportsTransportBehind(runtime, "playback.clear-pending"));

  CHECK(runtime.transport().start());
  CHECK(waitReady(runtime, revision));
  CHECK(eventually([&] { return !reportsTransportBehind(runtime, "playback.update-pending"); }));
  // Nothing is left owed to run late and take the audio away again.
  std::this_thread::sleep_for(std::chrono::milliseconds{200});
  CHECK(runtime.transport().state().available);
  CHECK(runtime.transport().state().publishedRevision == revision);
}

TEST_CASE("authoring runtime shuts down promptly while the transport still owes it a change") {
  using namespace seam;
  auto fixture = makeFixture();
  authoring::AuthoringRuntime runtime{
      std::move(fixture.document),
      configFor(test::support::temporaryDirectory("runtime-owed-shutdown"))};
  CHECK(runtime.initialize());
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  holdTransportFull(runtime);
  CHECK(runtime.execute(std::make_unique<application::RemoveNotesCommand>(
      noteIdsOf(runtime, fixture.resolvedRegion))));
  CHECK(reportsTransportBehind(runtime, "playback.clear-pending"));

  // Retrying is still under way when the runtime is asked to stop. It must not hold it up.
  const auto started = std::chrono::steady_clock::now();
  runtime.shutdown();
  CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{2});
}

TEST_CASE("authoring runtime asks the transport on its own only a bounded number of times") {
  // A feeder that stays away is not asked for ever: the tries end, the condition stays reported,
  // and the next request (what the creator's Retry sends) asks again. A debt that comes later gets
  // a helper of its own.
  using namespace seam;
  auto fixture = makeFixture();
  auto config = configFor(test::support::temporaryDirectory("runtime-owed-clear-bounded"));
  config.transportRetryAttempts = 50U;
  authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  CHECK(runtime.initialize());
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  holdTransportFull(runtime);
  CHECK(runtime.execute(std::make_unique<application::RemoveNotesCommand>(
      noteIdsOf(runtime, fixture.resolvedRegion))));
  CHECK(reportsTransportBehind(runtime, "playback.clear-pending"));

  // Fifty tries, 2 ms apart, are long over when the feeder comes back.
  std::this_thread::sleep_for(std::chrono::milliseconds{500});
  CHECK(runtime.transport().start());
  std::this_thread::sleep_for(std::chrono::milliseconds{300});
  CHECK(runtime.transport().state().available);
  CHECK(reportsTransportBehind(runtime, "playback.clear-pending"));

  // What the creator's Retry asks for. The feeder can take it now.
  runtime.requestPreview(true);
  CHECK(!runtime.transport().state().available);
  CHECK(!reportsTransportBehind(runtime, "playback.clear-pending"));

  // A second time: the helper of the first has ended, and this debt starts its own.
  CHECK(runtime.undo());
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  holdTransportFull(runtime);
  CHECK(runtime.execute(std::make_unique<application::RemoveNotesCommand>(
      noteIdsOf(runtime, fixture.resolvedRegion))));
  CHECK(reportsTransportBehind(runtime, "playback.clear-pending"));
  CHECK(runtime.transport().start());
  CHECK(eventually([&] { return !runtime.transport().state().available; }, std::chrono::seconds{3}));
  CHECK(eventually([&] { return !reportsTransportBehind(runtime, "playback.clear-pending"); }));
}

TEST_CASE("authoring runtime stops owing the transport once it takes the audio that follows") {
  // With no asking of its own, nothing pays the owed clear. The audio that follows the emptied
  // score does: the transport takes it, so it is no longer behind, and nothing is reported.
  using namespace seam;
  auto fixture = makeFixture();
  auto config = configFor(test::support::temporaryDirectory("runtime-owed-clear-settled-by-render"));
  config.transportRetryAttempts = 0U;
  authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  CHECK(runtime.initialize());
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  holdTransportFull(runtime);
  CHECK(runtime.execute(std::make_unique<application::RemoveNotesCommand>(
      noteIdsOf(runtime, fixture.resolvedRegion))));
  CHECK(reportsTransportBehind(runtime, "playback.clear-pending"));

  CHECK(runtime.transport().start());
  std::this_thread::sleep_for(std::chrono::milliseconds{150});
  CHECK(runtime.transport().state().available);
  CHECK(reportsTransportBehind(runtime, "playback.clear-pending"));

  CHECK(runtime.undo());
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.transport().state().available);
  CHECK(!reportsTransportBehind(runtime, "playback.clear-pending"));
  CHECK(!reportsTransportBehind(runtime, "playback.update-pending"));
}

TEST_CASE("authoring runtime owes the canonical audio when the transport refuses it after a seam preview") {
  using namespace seam;
  auto fixture = makeFixture();
  auto* fixtureRegion = fixture.document->session().project().findRegion(fixture.resolvedRegion);
  CHECK(fixtureRegion != nullptr);
  const auto key = domain::PhonemeKey{.noteId = fixtureRegion->notes.front().id, .ordinal = 0U};
  fixtureRegion->seamOverrides.push_back(domain::SeamOverride{
      .incomingStartKey = key,
      .seamAmount = 0.9F,
      .overlap = time::Microseconds{4'000},
      .phaseReset = 1.0F,
      .envelopeBlend = 0.1F,
      .curve = domain::SeamCurve::HardCharacter,
      .locked = true,
  });
  authoring::AuthoringRuntime runtime{
      std::move(fixture.document),
      configFor(test::support::temporaryDirectory("runtime-seam-restore-refused"))};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.previewSeam(key, true));
  CHECK(eventually([&] { return runtime.seamPreviewReady(); }, std::chrono::seconds{20}));

  holdTransportFull(runtime);
  // The preview ends, the canonical audio is not taken, and the transport still holds the preview.
  const auto restored = runtime.previewSeam(key, false);
  CHECK(!restored);
  CHECK(!runtime.seamPreviewActive());
  CHECK(reportsTransportBehind(runtime, "playback.update-pending"));

  CHECK(runtime.transport().start());
  CHECK(eventually([&] { return !reportsTransportBehind(runtime, "playback.update-pending"); }));
  CHECK(runtime.transport().state().available);
}

TEST_CASE("authoring runtime does not forgive an owed publication because a seam preview was asked for") {
  // A seam preview that has only been requested holds no audio, so the canonical audio the transport
  // is owed is still owed. If the preview then fails there is nothing left that would restore it.
  // Found by the second developer's review of b3d0394f.
  using namespace seam;
  auto fixture = makeFixture();
  const auto key = domain::PhonemeKey{
      .noteId = fixture.document->session().project().findRegion(fixture.resolvedRegion)->notes.front().id,
      .ordinal = 0U};
  HeldHook beforePublication;
  auto config = configFor(test::support::temporaryDirectory("runtime-owed-publication-seam-asked"));
  config.beforeSeamPreviewPublication = [&] { beforePublication.pass(); };
  authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  ReleaseHeldOnExit releaseOnExit{beforePublication};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  holdTransportFull(runtime);
  CHECK(runtime.execute(std::make_unique<application::SetVocalTrackMixCommand>(
      fixture.resolvedTrack, -6.0F, 0.0F, false, false)));
  const auto revision = runtime.document().session().revision();
  CHECK(waitForRenderState(runtime, authoring::RenderState::Ready, revision));
  CHECK(eventually([&] { return reportsTransportBehind(runtime, "playback.update-pending"); },
                   std::chrono::seconds{3}));

  // The preview is asked for and its completion is held, so it is active and holds no audio.
  CHECK(runtime.previewSeam(key, true));
  CHECK(beforePublication.waitEntered(std::chrono::seconds{10}));
  CHECK(runtime.seamPreviewActive());
  CHECK(!runtime.seamPreviewReady());
  std::this_thread::sleep_for(std::chrono::milliseconds{150});
  CHECK(reportsTransportBehind(runtime, "playback.update-pending"));

  // The transport is still full when the preview's audio arrives, so the preview is refused.
  beforePublication.release();
  CHECK(eventually([&] { return !runtime.seamPreviewActive(); }));
  CHECK(!runtime.seamPreviewReady());
  CHECK(reportsTransportBehind(runtime, "playback.update-pending"));

  CHECK(runtime.transport().start());
  CHECK(eventually([&] { return runtime.transport().state().publishedRevision == revision; },
                   std::chrono::seconds{5}));
  CHECK(eventually([&] { return !reportsTransportBehind(runtime, "playback.update-pending"); }));
}

TEST_CASE("authoring runtime hands over a render that finished while a seam preview that then failed was pending") {
  // A canonical render that finishes while a seam preview is only asked for is not handed to the
  // transport: the preview is about to replace it. When the preview then never arrives, the render
  // is still what the transport should hold, and nobody else is going to hand it over.
  using namespace seam;
  RenderGate gate;
  HeldHook beforePublication;
  auto fixture = makeFixture();
  const auto key = domain::PhonemeKey{
      .noteId = fixture.document->session().project().findRegion(fixture.resolvedRegion)->notes.front().id,
      .ordinal = 0U};
  auto config = configFor(test::support::temporaryDirectory("runtime-seam-failed-after-skipped-canonical"));
  config.renderHooks = gate.hooks();
  config.beforeSeamPreviewPublication = [&] { beforePublication.pass(); };
  authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  ReleaseHeldOnExit releaseOnExit{beforePublication};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  const auto oldRevision = runtime.transport().state().publishedRevision;

  // The edit's render is held in flight; the preview is asked for, and its completion is held too.
  gate.arm();
  CHECK(runtime.execute(std::make_unique<application::SetVocalTrackMixCommand>(
      fixture.resolvedTrack, -6.0F, 0.0F, false, false)));
  const auto revision = runtime.document().session().revision();
  CHECK(gate.waitEntered(std::chrono::seconds{10}));
  CHECK(runtime.previewSeam(key, true));
  CHECK(beforePublication.waitEntered(std::chrono::seconds{10}));
  holdTransportFull(runtime);

  // The render finishes while the preview is pending, so it is not handed to the transport.
  gate.release();
  CHECK(waitForRenderState(runtime, authoring::RenderState::Ready, revision));
  CHECK(runtime.transport().state().publishedRevision == oldRevision);
  CHECK(!reportsTransportBehind(runtime, "playback.update-pending"));

  // The preview arrives and the transport refuses it. The render is handed over, or owed.
  beforePublication.release();
  CHECK(eventually([&] { return !runtime.seamPreviewActive(); }));
  CHECK(eventually([&] { return reportsTransportBehind(runtime, "playback.update-pending"); },
                   std::chrono::seconds{3}));
  CHECK(runtime.transport().start());
  CHECK(eventually([&] { return runtime.transport().state().publishedRevision == revision; },
                   std::chrono::seconds{5}));
  CHECK(eventually([&] { return !reportsTransportBehind(runtime, "playback.update-pending"); }));
}

TEST_CASE("authoring runtime settles an owed publication when a seam preview takes the transport") {
  // With no asking of its own, nothing pays the owed publication. A seam preview that the transport
  // takes replaces what the transport held, so the canonical audio is no longer owed to it; the
  // preview's end hands the canonical audio back.
  using namespace seam;
  auto fixture = makeFixture();
  const auto key = domain::PhonemeKey{
      .noteId = fixture.document->session().project().findRegion(fixture.resolvedRegion)->notes.front().id,
      .ordinal = 0U};
  auto config = configFor(test::support::temporaryDirectory("runtime-owed-publication-seam-takes"));
  config.transportRetryAttempts = 0U;
  authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  holdTransportFull(runtime);
  CHECK(runtime.execute(std::make_unique<application::SetVocalTrackMixCommand>(
      fixture.resolvedTrack, -6.0F, 0.0F, false, false)));
  CHECK(eventually([&] { return reportsTransportBehind(runtime, "playback.update-pending"); },
                   std::chrono::seconds{10}));
  CHECK(runtime.transport().start());
  std::this_thread::sleep_for(std::chrono::milliseconds{150});
  CHECK(reportsTransportBehind(runtime, "playback.update-pending"));

  CHECK(runtime.previewSeam(key, true));
  CHECK(eventually([&] { return runtime.seamPreviewReady(); }, std::chrono::seconds{20}));
  CHECK(!reportsTransportBehind(runtime, "playback.update-pending"));
}

TEST_CASE("authoring runtime settles an owed publication when a comparison takes the transport") {
  // With no asking of its own, nothing pays the owed publication. A comparison that the transport
  // takes replaces what the transport held, so the canonical audio is no longer owed to it; the
  // comparison's end hands the canonical audio back.
  using namespace seam;
  using namespace seam::domain;
  using seam::time::Tick;
  auto fixture = makeFixture();
  auto* prepared = fixture.document->session().project().findRegion(fixture.resolvedRegion);
  prepared->notes.resize(1U);
  prepared->unitSelectionOverrides.resize(1U);
  auto config = configFor(test::support::temporaryDirectory("runtime-owed-publication-comparison-takes"));
  config.transportRetryAttempts = 0U;
  authoring::AuthoringRuntime runtime{std::move(fixture.document), config};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  const auto original = runtime.document().session().project();
  const auto& region = *original.findRegion(fixture.resolvedRegion);
  const auto pronunciation = phonemizer::resolveJapanesePronunciation(region);
  CHECK(pronunciation);
  const auto job = runtime.document().session().capturePerformanceJob();
  CHECK(job);
  PerformanceTake proposal{.id = "owed-attack-2", .sourceRegionId = region.id,
      .capturedRevision = region.performance.revision,
      .resource = {SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
      .pronunciation = pronunciation.value().identity,
      .generatorId = "fixture", .generatorVersion = "1", .range = {Tick{0}, region.durationTick},
      .lanes = {{PerformanceChannel::Attack, {{Tick{0}, 150.0}}}}};
  CHECK(runtime.executePerformanceResult(job.value(),
      std::make_unique<application::AddPerformanceProposalCommand>(region.id, region.performance, proposal)));
  const auto accepted = std::vector<AcceptedPerformanceSelection>{
      {proposal.id, PerformanceChannel::Attack, region.notes.front().id, Tick{0}}};

  holdTransportFull(runtime);
  CHECK(runtime.execute(std::make_unique<application::SetVocalTrackMixCommand>(
      fixture.resolvedTrack, -6.0F, 0.0F, false, false)));
  CHECK(eventually([&] { return reportsTransportBehind(runtime, "playback.update-pending"); },
                   std::chrono::seconds{10}));
  CHECK(runtime.transport().start());
  std::this_thread::sleep_for(std::chrono::milliseconds{150});
  CHECK(reportsTransportBehind(runtime, "playback.update-pending"));

  CHECK(runtime.auditionPerformance(region.id, accepted));
  CHECK(eventually([&] { return runtime.performanceAuditionReady(); }, std::chrono::seconds{20}));
  CHECK(!reportsTransportBehind(runtime, "playback.update-pending"));
}

TEST_CASE("authoring runtime hands over the canonical render of an accepted take that the transport refused") {
  // The accepted take stays on the transport until the render that includes it arrives. When that
  // render arrives and is refused, keeping the take does not excuse it: the render is still owed.
  // Found by the second developer's review of b3d0394f.
  using namespace seam;
  using namespace seam::domain;
  using seam::time::Tick;
  auto fixture = makeFixture();
  auto* prepared = fixture.document->session().project().findRegion(fixture.resolvedRegion);
  prepared->notes.resize(1U);
  prepared->unitSelectionOverrides.resize(1U);
  authoring::AuthoringRuntime runtime{
      std::move(fixture.document),
      configFor(test::support::temporaryDirectory("runtime-owed-publication-accepted-take"))};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  const auto original = runtime.document().session().project();
  const auto& region = *original.findRegion(fixture.resolvedRegion);
  const auto pronunciation = phonemizer::resolveJapanesePronunciation(region);
  CHECK(pronunciation);
  const auto job = runtime.document().session().capturePerformanceJob();
  CHECK(job);
  PerformanceTake proposal{.id = "owed-attack", .sourceRegionId = region.id,
      .capturedRevision = region.performance.revision,
      .resource = {SingerResourceKind::Neural, "fixture", "1", std::string(64U, 'a')},
      .pronunciation = pronunciation.value().identity,
      .generatorId = "fixture", .generatorVersion = "1", .range = {Tick{0}, region.durationTick},
      .lanes = {{PerformanceChannel::Attack, {{Tick{0}, 150.0}}}}};
  CHECK(runtime.executePerformanceResult(job.value(),
      std::make_unique<application::AddPerformanceProposalCommand>(region.id, region.performance, proposal)));
  const auto accepted = std::vector<AcceptedPerformanceSelection>{
      {proposal.id, PerformanceChannel::Attack, region.notes.front().id, Tick{0}}};
  CHECK(runtime.auditionPerformance(region.id, accepted));
  CHECK(eventually([&] { return runtime.performanceAuditionReady(); }, std::chrono::seconds{10}));
  const auto stored = runtime.document().session().project();
  holdTransportFull(runtime);

  CHECK(runtime.acceptPerformanceAudition(std::make_unique<application::SetAcceptedPerformanceCommand>(
      region.id, stored.findRegion(region.id)->performance, accepted)));
  const auto revision = runtime.document().session().revision();
  CHECK(waitForRenderState(runtime, authoring::RenderState::Ready, revision));
  // The canonical render has arrived and the transport refused it.
  CHECK(eventually([&] { return reportsTransportBehind(runtime, "playback.update-pending"); },
                   std::chrono::seconds{3}));
  std::this_thread::sleep_for(std::chrono::milliseconds{200});
  CHECK(reportsTransportBehind(runtime, "playback.update-pending"));

  CHECK(runtime.transport().start());
  CHECK(eventually([&] {
    return runtime.transport().state().publishedRevision == revision &&
           !runtime.audiblePublication().performanceAudition;
  }, std::chrono::seconds{5}));
  CHECK(eventually([&] { return !reportsTransportBehind(runtime, "playback.update-pending"); }));
}

TEST_CASE("authoring runtime leaves a transport that holds the newest audio alone when a seam preview fails") {
  // A seam preview that put nothing on the transport and left nothing waiting behind it changes
  // nothing when it fails: the transport holds the newest canonical audio already. Handing it over
  // again would republish it during playback and, when the transport refuses, report that playback
  // is behind when it is not.
  using namespace seam;
  auto fixture = makeFixture();
  const auto key = domain::PhonemeKey{
      .noteId = fixture.document->session().project().findRegion(fixture.resolvedRegion)->notes.front().id,
      .ordinal = 0U};
  authoring::AuthoringRuntime runtime{
      std::move(fixture.document),
      configFor(test::support::temporaryDirectory("runtime-seam-failed-nothing-behind"))};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  const auto revision = runtime.document().session().revision();
  CHECK(waitReady(runtime, revision));
  CHECK(runtime.transport().state().publishedRevision == revision);
  holdTransportFull(runtime);

  // The preview is refused: the transport is full, and what it holds is the newest audio.
  CHECK(runtime.previewSeam(key, true));
  CHECK(eventually([&] { return !runtime.seamPreviewActive(); }, std::chrono::seconds{20}));
  CHECK(!runtime.seamPreviewReady());
  std::this_thread::sleep_for(std::chrono::milliseconds{200});
  CHECK(!reportsTransportBehind(runtime, "playback.update-pending"));
  CHECK(!reportsTransportBehind(runtime, "playback.clear-pending"));
  CHECK(runtime.transport().state().publishedRevision == revision);
}

TEST_CASE("authoring runtime hands the canonical audio back when a seam preview fails after an earlier one took the transport") {
  // The earlier preview's audio is still on the transport when the next request fails, and the
  // request that failed is no longer wanted, so nothing else would replace it.
  using namespace seam;
  auto fixture = makeFixture();
  const auto key = domain::PhonemeKey{
      .noteId = fixture.document->session().project().findRegion(fixture.resolvedRegion)->notes.front().id,
      .ordinal = 0U};
  authoring::AuthoringRuntime runtime{
      std::move(fixture.document),
      configFor(test::support::temporaryDirectory("runtime-seam-failed-after-earlier-preview"))};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  CHECK(runtime.previewSeam(key, true));
  CHECK(eventually([&] { return runtime.seamPreviewReady(); }, std::chrono::seconds{20}));
  CHECK(!reportsTransportBehind(runtime, "playback.update-pending"));
  holdTransportFull(runtime);

  // The next preview is refused, and the transport still holds the earlier one's audio.
  CHECK(runtime.previewSeam(key, true));
  CHECK(eventually([&] { return !runtime.seamPreviewActive(); }, std::chrono::seconds{20}));
  CHECK(eventually([&] { return reportsTransportBehind(runtime, "playback.update-pending"); },
                   std::chrono::seconds{3}));

  CHECK(runtime.transport().start());
  CHECK(eventually([&] { return !reportsTransportBehind(runtime, "playback.update-pending"); }));
}

TEST_CASE("authoring runtime stops expecting audio behind a seam preview once the canonical audio is back") {
  // What waited behind a preview is handed over when that preview ends: by the creator's restore,
  // or by the render of the next edit. A preview that fails afterwards changes nothing on the
  // transport, and playback must not be reported as behind.
  using namespace seam;
  auto fixture = makeFixture();
  const auto key = domain::PhonemeKey{
      .noteId = fixture.document->session().project().findRegion(fixture.resolvedRegion)->notes.front().id,
      .ordinal = 0U};
  authoring::AuthoringRuntime runtime{
      std::move(fixture.document),
      configFor(test::support::temporaryDirectory("runtime-seam-failed-after-handback"))};
  CHECK(runtime.initialize());
  CHECK(runtime.selectTrack(fixture.resolvedTrack));
  CHECK(runtime.selectRegion(fixture.resolvedRegion));
  CHECK(waitReady(runtime, runtime.document().session().revision()));
  const auto refusedPreviewChangesNothing = [&] {
    holdTransportFull(runtime);
    CHECK(runtime.previewSeam(key, true));
    CHECK(eventually([&] { return !runtime.seamPreviewActive(); }, std::chrono::seconds{20}));
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    CHECK(!reportsTransportBehind(runtime, "playback.update-pending"));
    CHECK(runtime.transport().start());
  };

  // The creator's restore ends a preview.
  CHECK(runtime.previewSeam(key, true));
  CHECK(eventually([&] { return runtime.seamPreviewReady(); }, std::chrono::seconds{20}));
  CHECK(runtime.previewSeam(key, false));
  refusedPreviewChangesNothing();

  // The render of an edit ends a preview.
  CHECK(runtime.previewSeam(key, true));
  CHECK(eventually([&] { return runtime.seamPreviewReady(); }, std::chrono::seconds{20}));
  CHECK(runtime.execute(std::make_unique<application::SetVocalTrackMixCommand>(
      fixture.resolvedTrack, -6.0F, 0.0F, false, false)));
  const auto revision = runtime.document().session().revision();
  CHECK(waitReady(runtime, revision));
  CHECK(eventually([&] { return runtime.transport().state().publishedRevision == revision; },
                   std::chrono::seconds{5}));
  refusedPreviewChangesNothing();
}
