#include "test_framework.hpp"

#include "seam/authoring/authoring_runtime.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/voicebank/catalog.hpp"

#include <chrono>
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
    region->unitSelectionOverrides.push_back(
        seam::domain::UnitSelectionOverride{
            .startKey = seam::domain::PhonemeKey{.noteId = noteId, .ordinal = 0U},
            .tokenCount = static_cast<std::uint16_t>(index == 1U || index == 2U ? 1U : 2U),
            .unitId = unitIds[index],
            .renderer = index % 2U == 0U
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
                        std::chrono::seconds{8};
  while (std::chrono::steady_clock::now() < deadline) {
    const auto progress = runtime.renderer().progress();
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
  CHECK(runtime.renderer().stats().submitted == beforeSubmitted + 1U);
  CHECK(waitReady(runtime, revision));
  const auto after = runtime.renderer().latest();
  CHECK(after->result.phraseContentHashes != before->result.phraseContentHashes);
  CHECK(runtime.transport().state().publishedRevision == revision);
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

TEST_CASE("authoring_runtime_technical_edit_submits_once") {
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
      .noteId = region->notes.front().id, .ordinal = 0U};
  const auto beforeRevision = runtime.document().session().revision();
  const auto beforeSubmitted = runtime.renderer().stats().submitted;
  CHECK(runtime.technicalEdits().movePhonemeBoundary(
      key, false, seam::time::Microseconds{42000}));
  CHECK(runtime.document().session().revision() == beforeRevision + 1U);
  CHECK(runtime.renderer().stats().submitted == beforeSubmitted + 1U);
}
