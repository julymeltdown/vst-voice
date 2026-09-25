#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/authoring/interchange_service.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "seam/core/sha256.hpp"

TEST_CASE("interchange service refuses an incomplete MIDI loss report") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("midi-loss-capacity");
  for (const std::size_t losses : {4096U, 4097U}) {
    std::vector<std::uint8_t> track;
    for (std::size_t index = 0; index < losses; ++index)
      track.insert(track.end(), {0U, 0xffU, 0x7fU, 0U});
    track.insert(track.end(), {0U, 0x90U, 60U, 100U,
        0U, 0xffU, 0x05U, 1U, 'a',
        0x83U, 0x60U, 0x80U, 60U, 0U, 0U, 0xffU, 0x2fU, 0U});
    std::vector<std::uint8_t> bytes{'M', 'T', 'h', 'd', 0U, 0U, 0U, 6U, 0U, 0U, 0U, 1U, 1U, 0xe0U,
                                    'M', 'T', 'r', 'k'};
    const auto count = static_cast<std::uint32_t>(track.size());
    for (const unsigned shift : {24U, 16U, 8U, 0U})
      bytes.push_back(static_cast<std::uint8_t>((count >> shift) & 0xffU));
    bytes.insert(bytes.end(), track.begin(), track.end());
    const auto source = root / (std::to_string(losses) + ".mid");
    CHECK(core::durableAtomicWriteNew(source, std::as_bytes(std::span{bytes})));
    const auto before = core::sha256File(source); CHECK(before);
    application::ProjectFactory factory{880000U};
    const auto result = authoring::InterchangeService{}.importFile(
        source, factory, {.format = authoring::InterchangeFormat::Smf,
                         .projectName = "Bounded MIDI losses"});
    if (losses == 4096U) {
      CHECK(result); CHECK(result.value().issues.size() == losses);
    } else {
      CHECK(!result); CHECK(result.error().code == core::ErrorCode::Unsupported);
      CHECK(result.error().message.find("diagnostic") != std::string::npos);
    }
    CHECK(core::sha256File(source).value() == before.value());
  }
}

TEST_CASE("interchange export refuses diagnostic overflow before touching destinations") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("ustx-export-loss-capacity");
  application::ProjectFactory factory{890000U};
  auto project = factory.createProject("Many losses");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto region = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{5000 * 960});
  auto* target = project.findRegion(region); CHECK(target);
  const auto initial = interchange::exportUstxProject(project); CHECK(initial);
  CHECK(initial.value().issues.size() < 4096U);
  const auto noteCount = 4096U - initial.value().issues.size();
  const auto addLoss = [&](std::size_t index) {
    auto [lyric, note] = factory.makeNote(time::Tick{static_cast<std::int64_t>(index) * 960},
                                        time::Tick{960}, 60U, U"a");
    note.articulation = domain::NoteArticulation::Staccato;
    target->lyrics.push_back(std::move(lyric)); target->notes.push_back(std::move(note));
  };
  for (std::size_t index = 0; index < noteCount; ++index) addLoss(index);
  const auto full = interchange::exportUstxProject(project); CHECK(full);
  CHECK(full.value().issues.size() == 4096U);
  addLoss(noteCount);
  const auto rejected = interchange::exportUstxProject(project);
  CHECK(!rejected); CHECK(rejected.error().code == core::ErrorCode::Unsupported);
  const auto existing = root / "existing.ustx";
  CHECK(core::durableAtomicWriteText(existing, "preserve existing file"));
  const auto before = core::sha256File(existing); CHECK(before);
  const auto fresh = root / "fresh.ustx";
  for (const auto& destination : {existing, fresh}) {
    const auto result = authoring::InterchangeService{}.exportFile(
        project, {.format = authoring::InterchangeFormat::Ustx, .destination = destination});
    CHECK(!result); CHECK(result.error().code == core::ErrorCode::Unsupported);
    CHECK(result.error().message.find("diagnostic") != std::string::npos);
  }
  CHECK(core::sha256File(existing).value() == before.value());
  CHECK(!std::filesystem::exists(fresh));
}

#ifndef _WIN32
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#endif

namespace {

const char kUstx[] =
    "ustx_version: \"0.9\"\n"
    "name: Service fixture\n"
    "time_signatures:\n"
    "  - {bar_position: 0, beat_per_bar: 4, beat_unit: 4}\n"
    "tempos:\n"
    "  - {position: 0, bpm: 120}\n"
    "tracks:\n"
    "  - singer: fixture\n"
    "    track_name: Lead\n"
    "    mute: false\n"
    "    solo: false\n"
    "    volume: 0\n"
    "    pan: 0\n"
    "voice_parts:\n"
    "  - name: Verse\n"
    "    track_no: 0\n"
    "    position: 0\n"
    "    duration: 480\n"
    "    notes:\n"
    "      - position: 0\n"
    "        duration: 480\n"
    "        tone: 60\n"
    "        lyric: \"la\"\n"
    "        pitch: {data: [{x: 0, y: 0, shape: l}], snap_first: false}\n";

void writeText(const std::filesystem::path& path, std::string_view text) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("unable to create fixture");
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

}  // namespace

TEST_CASE("interchange service imports an unsaved USTX draft with source identity") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-import");
  const auto source = root / "song.ustx";
  writeText(source, kUstx);
  seam::application::ProjectFactory factory{930000U};
  seam::authoring::InterchangeService service;
  auto imported = service.importFile(source, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx,
                                                .projectName = "Draft",
                                                .language = seam::domain::Language::English});
  CHECK(imported);
  CHECK(imported.value().format == seam::authoring::InterchangeFormat::Ustx);
  CHECK(imported.value().project.name() == "Draft");
  CHECK(imported.value().project.vocalTracks().size() == 1U);
  CHECK(imported.value().project.vocalTracks().front().regions.front().notes.size() == 1U);
  CHECK(imported.value().sourceHash.size() == 64U);
  CHECK(imported.value().sourcePath ==
        std::filesystem::weakly_canonical(source.parent_path()) / source.filename());
}

TEST_CASE("interchange service exports create-new and preserves the project on collision") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-export");
  seam::application::ProjectFactory factory{940000U};
  auto project = factory.createProject("Service export");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto region = factory.addRegion(project, track, "Verse", seam::time::Tick{0}, seam::time::Tick{960});
  auto* target = project.findRegion(region);
  CHECK(target != nullptr);
  auto [lyric, note] = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{960}, 64U, U"la", seam::domain::Language::English);
  target->lyrics.push_back(std::move(lyric));
  target->notes.push_back(std::move(note));
  seam::authoring::InterchangeService service;
  const auto destination = root / "export.ustx";
  auto exported = service.exportFile(project,
      seam::authoring::InterchangeExportRequest{.format = seam::authoring::InterchangeFormat::Ustx,
                                                .destination = destination});
  CHECK(exported);
  CHECK(exported.value().contentHash.size() == 64U);
  std::ifstream input(destination, std::ios::binary);
  const std::string before{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  CHECK(!before.empty());
  auto collision = service.exportFile(project,
      seam::authoring::InterchangeExportRequest{.format = seam::authoring::InterchangeFormat::Ustx,
                                                .destination = destination});
  CHECK(!collision);
  std::ifstream afterInput(destination, std::ios::binary);
  const std::string after{std::istreambuf_iterator<char>{afterInput}, std::istreambuf_iterator<char>{}};
  CHECK(after == before);
}

TEST_CASE("interchange export can be reviewed before any destination is created") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("interchange-export-review");
  application::ProjectFactory factory{942000U};
  auto project = factory.createProject("Reviewable score");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto region = factory.addRegion(project, track, "Phrase",
      time::Tick{0}, time::Tick{960});
  auto* target = project.findRegion(region);
  CHECK(target != nullptr);
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{960}, 64U, U"la");
  note.articulation = domain::NoteArticulation::Staccato;
  target->lyrics.push_back(std::move(lyric));
  target->notes.push_back(std::move(note));
  const auto destination = root / "review.ustx";
  authoring::InterchangeService service;
  auto prepared = service.prepareExport(project,
      {.format = authoring::InterchangeFormat::Ustx, .destination = destination});
  CHECK(prepared);
  CHECK(!std::filesystem::exists(destination));
  CHECK(!prepared.value().bytes.empty());
  CHECK(prepared.value().contentHash.size() == 64U);
  CHECK(std::any_of(prepared.value().issues.begin(), prepared.value().issues.end(),
      [](const auto& issue) { return issue.loss; }));

  auto tampered = prepared.value();
  tampered.bytes.front() ^= 1U;
  const auto rejected = service.writeExport(tampered);
  CHECK(!rejected);
  CHECK(rejected.error().code == core::ErrorCode::Conflict);
  CHECK(!std::filesystem::exists(destination));

  const auto written = service.writeExport(prepared.value());
  CHECK(written);
  CHECK(written.value().contentHash == prepared.value().contentHash);
  CHECK(written.value().issues == prepared.value().issues);
  CHECK(core::sha256File(destination).value() == prepared.value().contentHash);
  const auto collision = service.writeExport(prepared.value());
  CHECK(!collision);
  CHECK(core::sha256File(destination).value() == prepared.value().contentHash);
}

TEST_CASE("interchange service uses bounded SMF import and export paths") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-smf");
  seam::application::ProjectFactory factory{950000U};
  auto project = factory.createProject("SMF service");
  const auto track = factory.addVocalTrack(project, "MIDI");
  const auto region = factory.addRegion(project, track, "Phrase", seam::time::Tick{0}, seam::time::Tick{960});
  auto* target = project.findRegion(region);
  CHECK(target != nullptr);
  auto [lyric, note] = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{480}, 60U, U"la");
  target->lyrics.push_back(std::move(lyric));
  target->notes.push_back(std::move(note));
  seam::authoring::InterchangeService service;
  const auto destination = root / "phrase.mid";
  auto exported = service.exportFile(project,
      seam::authoring::InterchangeExportRequest{.format = seam::authoring::InterchangeFormat::Smf,
                                                .destination = destination,
                                                .trackId = track,
                                                .regionId = region});
  CHECK(exported);
  auto imported = service.importFile(destination, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Smf,
                                                .projectName = "Imported MIDI"});
  CHECK(imported);
  CHECK(imported.value().format == seam::authoring::InterchangeFormat::Smf);
  CHECK(imported.value().project.vocalTracks().front().regions.front().notes.size() == 1U);
}

TEST_CASE("interchange service imports Type-1 tracks as distinct vocal drafts") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("interchange-service-smf-tracks");
  const auto source = root / "two-vocal-tracks.mid";
  const std::vector<std::uint8_t> conductor{
      0U, 0xffU, 0x03U, 9U, 'C', 'o', 'n', 'd', 'u', 'c', 't', 'o', 'r',
      0U, 0xffU, 0x51U, 3U, 0x09U, 0x27U, 0xc0U,
      0U, 0xffU, 0x58U, 4U, 3U, 2U, 24U, 8U,
      0x83U, 0x60U, 0xffU, 0x51U, 3U, 0x06U, 0x1aU, 0x80U,
      0U, 0xffU, 0x58U, 4U, 5U, 3U, 24U, 8U,
      0U, 0xffU, 0x2fU, 0U};
  const std::vector<std::uint8_t> lead{
      0U, 0xffU, 0x03U, 4U, 'L', 'e', 'a', 'd',
      0U, 0xffU, 0x05U, 2U, 'l', 'a',
      0U, 0x90U, 60U, 100U,
      0x83U, 0x60U, 0x80U, 60U, 0U,
      0U, 0xffU, 0x2fU, 0U};
  const std::vector<std::uint8_t> harmony{
      0U, 0xffU, 0x03U, 7U, 'H', 'a', 'r', 'm', 'o', 'n', 'y',
      0U, 0xffU, 0x05U, 2U, 'd', 'o',
      0U, 0x90U, 64U, 100U,
      0x83U, 0x60U, 0x80U, 64U, 0U,
      0U, 0xffU, 0x2fU, 0U};
  std::vector<std::uint8_t> bytes{
      'M', 'T', 'h', 'd', 0U, 0U, 0U, 6U,
      0U, 1U, 0U, 3U, 1U, 0xe0U};
  const auto appendTrack = [&](const std::vector<std::uint8_t>& track) {
    bytes.insert(bytes.end(), {'M', 'T', 'r', 'k'});
    const auto size = static_cast<std::uint32_t>(track.size());
    bytes.push_back(static_cast<std::uint8_t>(size >> 24U));
    bytes.push_back(static_cast<std::uint8_t>(size >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(size >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(size));
    bytes.insert(bytes.end(), track.begin(), track.end());
  };
  appendTrack(conductor);
  appendTrack(lead);
  appendTrack(harmony);
  CHECK(core::durableAtomicWriteNew(source, std::as_bytes(std::span{bytes})));
  const auto sourceHash = core::sha256File(source); CHECK(sourceHash);

  application::ProjectFactory factory{960000U};
  const auto imported = authoring::InterchangeService{}.importFile(source, factory,
      {.format = authoring::InterchangeFormat::Smf,
       .projectName = "Two-track import"});
  CHECK(imported);
  CHECK(imported.value().project.vocalTracks().size() == 2U);
  CHECK(imported.value().project.name() == "Two-track import");
  CHECK(imported.value().project.ppq() == time::kDefaultPpq);
  const auto& tempoEvents = imported.value().project.tempoMap().events();
  CHECK(tempoEvents.size() == 2U);
  CHECK((tempoEvents[0] == time::TempoEvent{time::Tick{0}, 100.0}));
  CHECK((tempoEvents[1] == time::TempoEvent{time::Tick{960}, 150.0}));
  const auto& meterEvents = imported.value().project.meterMap().events();
  CHECK(meterEvents.size() == 2U);
  CHECK((meterEvents[0] == time::MeterEvent{time::Tick{0}, 3U, 4U}));
  CHECK((meterEvents[1] == time::MeterEvent{time::Tick{960}, 5U, 8U}));
  const auto& leadTrack = imported.value().project.vocalTracks()[0];
  const auto& harmonyTrack = imported.value().project.vocalTracks()[1];
  CHECK(leadTrack.name == "Lead");
  CHECK(harmonyTrack.name == "Harmony");
  CHECK(leadTrack.regions.size() == 1U);
  CHECK(harmonyTrack.regions.size() == 1U);
  CHECK(leadTrack.regions.front().lyrics.size() == 1U);
  CHECK(harmonyTrack.regions.front().lyrics.size() == 1U);
  CHECK(leadTrack.regions.front().notes.size() == 1U);
  CHECK(harmonyTrack.regions.front().notes.size() == 1U);
  CHECK(leadTrack.regions.front().notes.front().startTick == time::Tick{0});
  CHECK(harmonyTrack.regions.front().notes.front().startTick == time::Tick{0});
  CHECK(leadTrack.regions.front().notes.front().durationTick == time::Tick{960});
  CHECK(harmonyTrack.regions.front().notes.front().durationTick == time::Tick{960});
  CHECK(leadTrack.regions.front().notes.front().midiKey == 60U);
  CHECK(harmonyTrack.regions.front().notes.front().midiKey == 64U);
  CHECK(leadTrack.regions.front().lyrics.front().surface == U"la");
  CHECK(harmonyTrack.regions.front().lyrics.front().surface == U"do");
  const auto* leadBoundLyric = leadTrack.regions.front().findLyric(
      leadTrack.regions.front().notes.front().lyricTokenId);
  const auto* harmonyBoundLyric = harmonyTrack.regions.front().findLyric(
      harmonyTrack.regions.front().notes.front().lyricTokenId);
  CHECK(leadBoundLyric != nullptr);
  CHECK(harmonyBoundLyric != nullptr);
  CHECK(leadBoundLyric->surface == U"la");
  CHECK(harmonyBoundLyric->surface == U"do");
  CHECK(imported.value().issues.empty());

  // An unqualified score export must preserve the complete multi-track song,
  // not silently narrow it to the first selected/default region.
  const auto roundTripPath = root / "round-trip.mid";
  authoring::InterchangeService service;
  const auto exported = service.exportFile(imported.value().project,
      {.format = authoring::InterchangeFormat::Smf,
       .destination = roundTripPath});
  CHECK(exported);
  const auto roundTrip = service.importFile(roundTripPath, factory,
      {.format = authoring::InterchangeFormat::Smf,
       .projectName = "Round-trip"});
  CHECK(roundTrip);
  CHECK(roundTrip.value().project.vocalTracks().size() == 2U);
  CHECK(roundTrip.value().project.vocalTracks()[0].name == "Lead");
  CHECK(roundTrip.value().project.vocalTracks()[1].name == "Harmony");
  CHECK(roundTrip.value().project.vocalTracks()[0].regions[0].notes.size() == 1U);
  CHECK(roundTrip.value().project.vocalTracks()[1].regions[0].notes.size() == 1U);
  const auto& roundLead = roundTrip.value().project.vocalTracks()[0].regions[0];
  const auto& roundHarmony = roundTrip.value().project.vocalTracks()[1].regions[0];
  CHECK(roundLead.notes[0].startTick == time::Tick{0});
  CHECK(roundHarmony.notes[0].startTick == time::Tick{0});
  CHECK(roundLead.notes[0].durationTick == time::Tick{960});
  CHECK(roundHarmony.notes[0].durationTick == time::Tick{960});
  CHECK(roundLead.notes[0].midiKey == 60U);
  CHECK(roundHarmony.notes[0].midiKey == 64U);
  CHECK(roundTrip.value().project.vocalTracks()[0].regions[0].lyrics[0].surface == U"la");
  CHECK(roundTrip.value().project.vocalTracks()[1].regions[0].lyrics[0].surface == U"do");
  const auto* roundLeadLyric = roundLead.findLyric(roundLead.notes[0].lyricTokenId);
  const auto* roundHarmonyLyric = roundHarmony.findLyric(roundHarmony.notes[0].lyricTokenId);
  CHECK(roundLeadLyric != nullptr);
  CHECK(roundHarmonyLyric != nullptr);
  CHECK(roundLeadLyric->surface == U"la");
  CHECK(roundHarmonyLyric->surface == U"do");
  CHECK(roundTrip.value().issues.empty());
  CHECK(roundTrip.value().project.tempoMap().events().size() == 2U);
  CHECK(roundTrip.value().project.tempoMap().events()[0].bpm == 100.0);
  CHECK(roundTrip.value().project.tempoMap().events()[1].bpm == 150.0);
  CHECK(roundTrip.value().project.tempoMap().events()[1].tick == time::Tick{960});
  CHECK(roundTrip.value().project.meterMap().events().size() == 2U);
  CHECK(roundTrip.value().project.meterMap().events()[0].numerator == 3U);
  CHECK(roundTrip.value().project.meterMap().events()[0].denominator == 4U);
  CHECK(roundTrip.value().project.meterMap().events()[1].numerator == 5U);
  CHECK(roundTrip.value().project.meterMap().events()[1].denominator == 8U);
  CHECK(roundTrip.value().project.meterMap().events()[1].tick == time::Tick{960});

  const auto nonFirstRegion = service.prepareExport(imported.value().project,
      {.format = authoring::InterchangeFormat::Smf,
       .destination = root / "non-first-region.mid",
       .trackId = harmonyTrack.id,
       .regionId = harmonyTrack.regions.front().id});
  CHECK(nonFirstRegion);
  const auto nonFirstScore = interchange::decodeSmf(nonFirstRegion.value().bytes);
  CHECK(nonFirstScore);
  CHECK(nonFirstScore.value().tracks.size() == 1U);
  CHECK(nonFirstScore.value().tracks.front().name == "Harmony");
  CHECK(nonFirstScore.value().notes.size() == 1U);
  CHECK(nonFirstScore.value().notes.front().midi == 64U);
  CHECK(nonFirstScore.value().texts.size() == 1U);
  CHECK(nonFirstScore.value().texts.front().text == "do");

  const auto partialSelection = service.prepareExport(imported.value().project,
      {.format = authoring::InterchangeFormat::Smf,
       .destination = root / "invalid-selection.mid",
       .trackId = leadTrack.id});
  CHECK(!partialSelection);
  CHECK(partialSelection.error().code == core::ErrorCode::InvalidArgument);
  const auto regionOnlySelection = service.prepareExport(imported.value().project,
      {.format = authoring::InterchangeFormat::Smf,
       .destination = root / "region-only-selection.mid",
       .regionId = harmonyTrack.regions.front().id});
  CHECK(!regionOnlySelection);
  CHECK(regionOnlySelection.error().code == core::ErrorCode::InvalidArgument);
  CHECK(core::sha256File(source).value() == sourceHash.value());
}

TEST_CASE("whole-project MIDI export places every region on the absolute timeline") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("interchange-service-smf-project-export");
  application::ProjectFactory factory{962000U};
  auto project = factory.createProject("Multi-region score");
  const auto lead = factory.addVocalTrack(project, "Lead");
  const auto harmony = factory.addVocalTrack(project, "Harmony");
  const auto leadVerse = factory.addRegion(project, lead, "Verse",
      time::Tick{0}, time::Tick{960});
  const auto leadChorus = factory.addRegion(project, lead, "Chorus",
      time::Tick{1920}, time::Tick{2880});
  const auto harmonyRegion = factory.addRegion(project, harmony, "Harmony phrase",
      time::Tick{480}, time::Tick{1440});
  const auto addNote = [&](domain::RegionId regionId, std::int64_t start,
                           std::uint8_t key, std::u32string lyricText) {
    auto* region = project.findRegion(regionId);
    CHECK(region != nullptr);
    auto [lyric, note] = factory.makeNote(time::Tick{start}, time::Tick{240},
        key, std::move(lyricText));
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  };
  addNote(leadVerse, 120, 60U, U"la");
  addNote(leadChorus, 240, 67U, U"mi");
  addNote(harmonyRegion, 360, 64U, U"do");

  const auto draft = authoring::InterchangeService{}.prepareExport(project,
      {.format = authoring::InterchangeFormat::Smf,
       .destination = root / "whole-score.mid"});
  CHECK(draft);
  const auto score = interchange::decodeSmf(draft.value().bytes);
  CHECK(score);
  CHECK(score.value().tracks.size() == 2U);
  CHECK(score.value().tracks[0].name == "Lead");
  CHECK(score.value().tracks[1].name == "Harmony");
  CHECK(score.value().notes.size() == 3U);
  CHECK(score.value().notes[0].track == 0U);
  CHECK(score.value().notes[0].start == time::Tick{120});
  CHECK(score.value().notes[1].track == 0U);
  CHECK(score.value().notes[1].start == time::Tick{2160});
  CHECK(score.value().notes[2].track == 1U);
  CHECK(score.value().notes[2].start == time::Tick{840});
  CHECK(score.value().texts.size() == 3U);
  CHECK(std::count_if(score.value().texts.begin(), score.value().texts.end(),
      [](const auto& text) { return text.text == "la" && text.track == 0U; }) == 1);
  CHECK(std::count_if(score.value().texts.begin(), score.value().texts.end(),
      [](const auto& text) { return text.text == "mi" && text.tick == time::Tick{2160}; }) == 1);
  CHECK(std::count_if(score.value().texts.begin(), score.value().texts.end(),
      [](const auto& text) { return text.text == "do" && text.track == 1U; }) == 1);

  const auto selectedLaterRegion = authoring::InterchangeService{}.prepareExport(project,
      {.format = authoring::InterchangeFormat::Smf,
       .destination = root / "selected-later-region.mid",
       .trackId = lead,
       .regionId = leadChorus});
  CHECK(selectedLaterRegion);
  const auto selectedLaterScore = interchange::decodeSmf(selectedLaterRegion.value().bytes);
  CHECK(selectedLaterScore);
  CHECK(selectedLaterScore.value().notes.size() == 1U);
  CHECK(selectedLaterScore.value().notes.front().midi == 67U);
  CHECK(selectedLaterScore.value().notes.front().start == time::Tick{240});
  CHECK(selectedLaterScore.value().texts.size() == 1U);
  CHECK(selectedLaterScore.value().texts.front().text == "mi");
}

TEST_CASE("interchange service rejects oversized input before codec work") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-oversize");
  const auto source = root / "song.ustx";
  writeText(source, kUstx);
  seam::application::ProjectFactory factory{960000U};
  seam::authoring::InterchangeService service;
  seam::interchange::UstxLimits limits;
  limits.maximumInputBytes = 16U;  // Far below the fixture size.
  auto imported = service.importFile(source, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx},
      limits);
  CHECK(!imported);
  CHECK(imported.error().code == seam::core::ErrorCode::Unsupported);
}

#ifndef _WIN32
// POSIX-only: the held-input admission boundary is POSIX-verified; Windows
// reparse semantics are pending platform evidence, so these cases are not
// registered there rather than counted as trivial passes.
TEST_CASE("interchange service rejects symlinked import paths") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-symlink");
  const auto real = root / "real.ustx";
  writeText(real, kUstx);
  const auto link = root / "link.ustx";
  std::error_code error;
  std::filesystem::create_symlink(real, link, error);
  CHECK(!error);  // Fixture creation is required on POSIX, not optional.
  seam::application::ProjectFactory factory{970000U};
  seam::authoring::InterchangeService service;
  auto imported = service.importFile(link, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
  CHECK(!imported);
  CHECK(imported.error().code == seam::core::ErrorCode::Conflict);
}

TEST_CASE("interchange service canonicalizes an intermediate symlink component") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-midlink");
  const auto realDir = root / "real-dir";
  std::filesystem::create_directories(realDir);
  writeText(realDir / "song.ustx", kUstx);
  const auto linkDir = root / "linked-dir";
  std::error_code error;
  std::filesystem::create_directory_symlink(realDir, linkDir, error);
  CHECK(!error);
  seam::application::ProjectFactory factory{975000U};
  seam::authoring::InterchangeService service;
  // The admitted path resolves deterministically to the real directory;
  // the draft identity and recorded sourcePath are the canonical location,
  // identical to importing the real path directly.
  auto viaLink = service.importFile(linkDir / "song.ustx", factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
  auto direct = service.importFile(realDir / "song.ustx", factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
  CHECK(viaLink);
  CHECK(direct);
  CHECK(viaLink.value().sourceHash == direct.value().sourceHash);
  CHECK(viaLink.value().sourcePath == direct.value().sourcePath);
}

TEST_CASE("a held import reads the originally opened inode across parent replacement") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-held-parent");
  const auto directoryA = root / "phrase-a";
  const auto directoryB = root / "phrase-b";
  std::filesystem::create_directories(directoryA);
  std::filesystem::create_directories(directoryB);
  writeText(directoryA / "song.ustx", kUstx);
  writeText(directoryB / "song.ustx", std::string{kUstx} + "# different parent\n");
  seam::application::ProjectFactory factory{976000U};
  seam::authoring::InterchangeService service;
  const auto heldPath = directoryA / "song.ustx";
  // Baseline hash of the bytes the held import must retain.
  auto baseline = service.importFile(heldPath, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
  CHECK(baseline);
  // Swap the parent mid-admission: the descriptor pins the originally opened
  // inode, so the read still returns the original bytes rather than the
  // replacement directory's contents.
  auto injected = service.importFile(heldPath, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx},
      {}, {},
      [&](seam::core::HeldReadStage stage) -> seam::core::Result<void> {
        if (stage == seam::core::HeldReadStage::Opened) {
          const auto parked = root / "phrase-a-parked";
          std::filesystem::rename(directoryA, parked);
          std::filesystem::rename(directoryB, directoryA);
        }
        return seam::core::success();
      });
  CHECK(injected);
  CHECK(injected.value().sourceHash == baseline.value().sourceHash);
}

TEST_CASE("a held import rejects in-place mutation during the read") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-held-mutate");
  const auto source = root / "song.ustx";
  writeText(source, kUstx);
  seam::application::ProjectFactory factory{977000U};
  seam::authoring::InterchangeService service;
  auto injected = service.importFile(source, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx},
      {}, {},
      [&](seam::core::HeldReadStage stage) -> seam::core::Result<void> {
        if (stage == seam::core::HeldReadStage::ContentRead) {
          // Rewrite the same inode with different-length content; the
          // post-read identity check must reject rather than admit.
          writeText(source, std::string{kUstx} + "# mutated in place\n");
        }
        return seam::core::success();
      });
  CHECK(!injected);
  CHECK(injected.error().code == seam::core::ErrorCode::Conflict);
}

TEST_CASE("a held import rejects a same-size mutation with a restored mtime") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-held-ctime");
  const auto source = root / "song.ustx";
  writeText(source, kUstx);
  seam::application::ProjectFactory factory{978000U};
  seam::authoring::InterchangeService service;
  auto injected = service.importFile(source, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx},
      {}, {},
      [&](seam::core::HeldReadStage stage) -> seam::core::Result<void> {
        if (stage == seam::core::HeldReadStage::ContentRead) {
          // Rewrite the SAME inode with SAME-length content, then restore
          // the original mtime. ctime is kernel-maintained and cannot be
          // restored, so the post-read identity check must still reject.
          struct stat info {};
          CHECK(::stat(source.c_str(), &info) == 0);
          std::string mutated{kUstx};
          mutated[0] = (mutated[0] == 'u') ? 'v' : 'u';
          writeText(source, mutated);
#ifdef __APPLE__
          const timespec times[2] = {info.st_atimespec, info.st_mtimespec};
#else
          const timespec times[2] = {info.st_atim, info.st_mtim};
#endif
          CHECK(::utimensat(AT_FDCWD, source.c_str(), times, 0) == 0);
        }
        return seam::core::success();
      });
  CHECK(!injected);
  CHECK(injected.error().code == seam::core::ErrorCode::Conflict);
}

TEST_CASE("a held import rejects a FIFO without blocking the admission") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-fifo");
  const auto fifo = root / "song.ustx";
  CHECK(::mkfifo(fifo.c_str(), 0600) == 0);
  // A FIFO with no writer must not block inside open. The import runs in a
  // forked child so a hang can be bounded, terminated and reaped without
  // touching this process's fixtures; a detached in-process worker could
  // outlive them. Exit codes: 0 rejected with the specific non-regular-file
  // diagnostic, 2 rejected for another reason, 1 imported,
  // 3 unexpected exception.
  const pid_t child = ::fork();
  CHECK(child >= 0);
  if (child == 0) {
    try {
      seam::application::ProjectFactory factory{979000U};
      seam::authoring::InterchangeService service;
      auto imported = service.importFile(fifo, factory,
          seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
      if (imported) ::_exit(1);
      const auto& error = imported.error();
      ::_exit(error.code == seam::core::ErrorCode::IoError &&
              error.message == "Unable to read a non-regular file" ? 0 : 2);
    } catch (...) {
      ::_exit(3);
    }
  }
  int status = 0;
  bool reaped = false;
  int waitError = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (std::chrono::steady_clock::now() < deadline) {
    const auto waited = ::waitpid(child, &status, WNOHANG);
    if (waited == child) {
      reaped = true;
      break;
    }
    if (waited < 0 && errno != EINTR) {
      waitError = errno;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  if (!reaped && waitError != ECHILD) {
    // Finish cleanup before any assertion can throw; interrupted waits must
    // not leave an unreaped child. ECHILD means there is no child to signal.
    const auto killed = ::kill(child, SIGKILL);
    const auto killError = killed < 0 ? errno : 0;
    pid_t waited;
    do { waited = ::waitpid(child, &status, 0); }
    while (waited < 0 && errno == EINTR);
    CHECK(killed == 0 || killError == ESRCH);
    CHECK(waited == child);
  }
  CHECK(waitError == 0);
  CHECK(reaped);
  CHECK(WIFEXITED(status));
  CHECK(WEXITSTATUS(status) == 0);
}

TEST_CASE("a held import closes its descriptor when the admission throws") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-held-throw");
  const auto source = root / "song.ustx";
  writeText(source, kUstx);
  const auto openDescriptors = [] {
    std::error_code error;
    std::size_t count = 0;
    auto entry = std::filesystem::directory_iterator("/dev/fd", error);
    CHECK(!error);
    while (entry != std::filesystem::directory_iterator{}) {
      ++count;
      entry.increment(error);
      CHECK(!error);
    }
    return count;
  };
  const auto before = openDescriptors();
  seam::application::ProjectFactory factory{980100U};
  seam::authoring::InterchangeService service;
  // A sentinel type that does NOT derive from std::exception: a framework
  // Failure thrown by CHECK inside the try must not be swallowed here.
  struct InjectedFault {};
  bool threw = false;
  try {
    static_cast<void>(service.importFile(source, factory,
        seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx},
        {}, {},
        [](seam::core::HeldReadStage) -> seam::core::Result<void> {
          throw InjectedFault{};
        }));
  } catch (const InjectedFault&) {
    threw = true;
  }
  CHECK(threw);  // The injector must propagate.
  CHECK(openDescriptors() == before);
}
#endif  // _WIN32 (POSIX-only held-admission cases)

TEST_CASE("interchange draft identity tracks the bytes actually read") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-identity");
  const auto source = root / "song.ustx";
  writeText(source, kUstx);
  seam::application::ProjectFactory factory{980000U};
  seam::authoring::InterchangeService service;
  auto first = service.importFile(source, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
  CHECK(first);
  // Replace the file contents at the same path; the draft hash must change,
  // so a swapped or edited file cannot silently reuse the earlier identity.
  writeText(source, std::string{kUstx} + "# changed\n");
  auto second = service.importFile(source, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
  CHECK(second);
  CHECK(first.value().sourceHash != second.value().sourceHash);
}

TEST_CASE("interchange parent replacement cannot silently redirect an import") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-parent");
  const auto directoryA = root / "phrase-a";
  const auto directoryB = root / "phrase-b";
  std::filesystem::create_directories(directoryA);
  std::filesystem::create_directories(directoryB);
  writeText(directoryA / "song.ustx", kUstx);
  writeText(directoryB / "song.ustx", std::string{kUstx} + "# different parent\n");
  seam::application::ProjectFactory factory{990000U};
  seam::authoring::InterchangeService service;
  const auto heldPath = directoryA / "song.ustx";
  auto first = service.importFile(heldPath, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
  CHECK(first);
  // Swap the parent directory out from under the same path string.
  const auto parked = root / "phrase-a-parked";
  std::filesystem::rename(directoryA, parked);
  std::filesystem::rename(directoryB, directoryA);
  auto second = service.importFile(heldPath, factory,
      seam::authoring::InterchangeImportRequest{.format = seam::authoring::InterchangeFormat::Ustx});
  CHECK(second);
  CHECK(first.value().sourceHash != second.value().sourceHash);
}

#ifndef _WIN32
// POSIX-only: reparse-point semantics are pending Windows platform evidence.
TEST_CASE("interchange service rejects symlinked export destinations") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-exportlink");
  seam::application::ProjectFactory factory{991000U};
  auto project = factory.createProject("Link export");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto region = factory.addRegion(project, track, "Verse", seam::time::Tick{0}, seam::time::Tick{960});
  auto* target = project.findRegion(region);
  CHECK(target != nullptr);
  auto [lyric, note] = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{960}, 64U, U"la", seam::domain::Language::English);
  target->lyrics.push_back(std::move(lyric));
  target->notes.push_back(std::move(note));
  const auto real = root / "real.ustx";
  writeText(real, "pre-existing\n");
  const auto link = root / "link.ustx";
  std::error_code error;
  std::filesystem::create_symlink(real, link, error);
  CHECK(!error);
  seam::authoring::InterchangeService service;
  auto exported = service.exportFile(project,
      seam::authoring::InterchangeExportRequest{.format = seam::authoring::InterchangeFormat::Ustx,
                                                .destination = link});
  CHECK(!exported);
  std::ifstream input(real, std::ios::binary);
  const std::string after{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  CHECK(after == "pre-existing\n");
}
#endif  // _WIN32

TEST_CASE("interchange export failure leaves no destination and preserves the project") {
  const auto root = seam::test::support::temporaryDirectory("interchange-service-exportfail");
  seam::application::ProjectFactory factory{992000U};
  auto project = factory.createProject("Empty export");
  seam::authoring::InterchangeService service;
  const auto destination = root / "empty.mid";
  // SMF export requires a vocal track and region; this project has none.
  auto exported = service.exportFile(project,
      seam::authoring::InterchangeExportRequest{.format = seam::authoring::InterchangeFormat::Smf,
                                                .destination = destination});
  CHECK(!exported);
  CHECK(!std::filesystem::exists(destination));
  CHECK(project.vocalTracks().empty());
}
