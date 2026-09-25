#include "test_framework.hpp"
#include "seam/interchange/smf_codec.hpp"
#include "seam/interchange/smf_project_conversion.hpp"
#include "seam/application/project_factory.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

void u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}
void u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}
std::vector<std::uint8_t> fileWithTrack(std::vector<std::uint8_t> track,
    std::uint16_t division = 480U, std::uint16_t format = 0U,
    std::uint16_t tracks = 1U) {
  std::vector<std::uint8_t> bytes{'M', 'T', 'h', 'd'};
  u32(bytes, 6U); u16(bytes, format); u16(bytes, tracks); u16(bytes, division);
  bytes.insert(bytes.end(), {'M', 'T', 'r', 'k'}); u32(bytes, static_cast<std::uint32_t>(track.size()));
  bytes.insert(bytes.end(), track.begin(), track.end());
  return bytes;
}
std::vector<std::uint8_t> fileWithTracks(
    const std::vector<std::vector<std::uint8_t>>& tracks,
    std::uint16_t division = 480U) {
  std::vector<std::uint8_t> bytes{'M', 'T', 'h', 'd'};
  u32(bytes, 6U); u16(bytes, 1U); u16(bytes, static_cast<std::uint16_t>(tracks.size()));
  u16(bytes, division);
  for (const auto& track : tracks) {
    bytes.insert(bytes.end(), {'M', 'T', 'r', 'k'});
    u32(bytes, static_cast<std::uint32_t>(track.size()));
    bytes.insert(bytes.end(), track.begin(), track.end());
  }
  return bytes;
}
}

TEST_CASE("SMF codec round-trips deterministic PPQ notes, tempo, meter and lyric text") {
  using namespace seam;
  interchange::SmfScore score;
  score.ppq = 480U;
  score.tracks = {{}};
  score.tempos = {{time::Tick{0}, 120.0}, {time::Tick{960}, 90.0}};
  score.meters = {{time::Tick{0}, 4U, 2U}, {time::Tick{1920}, 3U, 2U}};
  score.texts = {{time::Tick{0}, "la", true}, {time::Tick{480}, "marker", false}};
  score.notes = {{time::Tick{0}, time::Tick{480}, 60U, 100U, 0U},
                 {time::Tick{0}, time::Tick{240}, 60U, 90U, 0U},
                 {time::Tick{960}, time::Tick{720}, 64U, 110U, 2U}};
  const auto encoded = interchange::encodeSmf(score); CHECK(encoded);
  CHECK(encoded.value().size() > 14U);
  CHECK(encoded.value()[8] == 0U && encoded.value()[9] == 1U); // Type 1.
  const auto decoded = interchange::decodeSmf(encoded.value()); CHECK(decoded);
  CHECK(decoded.value().ppq == score.ppq);
  CHECK(decoded.value().tempos.size() == 2U);
  CHECK(decoded.value().meters.size() == 2U);
  CHECK(decoded.value().texts == score.texts);
  CHECK(decoded.value().notes.size() == score.notes.size());
  CHECK(std::find(decoded.value().notes.begin(), decoded.value().notes.end(), score.notes[0]) != decoded.value().notes.end());
  CHECK(std::find(decoded.value().notes.begin(), decoded.value().notes.end(), score.notes[1]) != decoded.value().notes.end());
  CHECK(std::find(decoded.value().notes.begin(), decoded.value().notes.end(), score.notes[2]) != decoded.value().notes.end());
  CHECK(decoded.value().issues.empty());
}

TEST_CASE("SMF decoder handles running status and reports missing note-offs") {
  using namespace seam;
  const std::vector<std::uint8_t> running{
      0U, 0x90U, 60U, 100U,
      0x83U, 0x60U, 60U, 0U,
      0U, 0xffU, 0x2fU, 0U};
  const auto decoded = interchange::decodeSmf(fileWithTrack(running)); CHECK(decoded);
  CHECK(decoded.value().notes.size() == 1U);
  CHECK(decoded.value().notes.front().start == time::Tick{0});
  CHECK(decoded.value().notes.front().duration == time::Tick{480});
  CHECK(decoded.value().issues.empty());

  const std::vector<std::uint8_t> dangling{0U, 0x90U, 62U, 80U,
      0x83U, 0x60U, 0xffU, 0x2fU, 0U};
  const auto closed = interchange::decodeSmf(fileWithTrack(dangling)); CHECK(closed);
  CHECK(closed.value().notes.size() == 1U);
  CHECK(!closed.value().issues.empty());
  CHECK(closed.value().issues.front().message.find("missing") != std::string::npos);
}

TEST_CASE("SMF decode separates source-event and repaired-score event ceilings") {
  using namespace seam;
  const auto bytes = fileWithTrack({
      0U, 0x90U, 60U, 100U, // Source event 1: note-on.
      0x83U, 0x60U, 0xffU, 0x2fU, 0U}); // Source event 2: EOT at tick 480.
  interchange::SmfLimits inputBound;
  inputBound.maximumEvents = 2U;
  const auto decoded = interchange::decodeSmf(bytes, inputBound); CHECK(decoded);
  CHECK(decoded.value().notes.size() == 1U);
  CHECK(decoded.value().notes.front().start == time::Tick{0});
  CHECK(decoded.value().notes.front().duration == time::Tick{480});
  CHECK(decoded.value().issues.size() == 1U);
  CHECK(decoded.value().issues.front().message.find("Note-off was missing") != std::string::npos);
  CHECK(interchange::encodeSmf(decoded.value(), inputBound));

  auto serializedBound = inputBound;
  serializedBound.maximumSerializedEvents = 2U;
  const auto overOutputBudget = interchange::decodeSmf(bytes, serializedBound);
  CHECK(!overOutputBudget);
  CHECK(overOutputBudget.error().message.find("serialized event count") != std::string::npos);
}

TEST_CASE("SMF decoder reports discarded channel controls including running status") {
  using namespace seam;
  for (const auto rawStatus : {0xa2U, 0xb2U, 0xc2U, 0xd2U, 0xe2U}) {
    const auto status = static_cast<std::uint8_t>(rawStatus);
    std::vector<std::uint8_t> track{0U, status, 64U};
    const bool twoBytes = (status & 0xf0U) != 0xc0U && (status & 0xf0U) != 0xd0U;
    if (twoBytes) track.push_back(32U);
    track.insert(track.end(), {1U, 65U}); // Same status, next tick.
    if (twoBytes) track.push_back(33U);
    track.insert(track.end(), {0U, 0xffU, 0x2fU, 0U});
    const auto result = interchange::decodeSmf(fileWithTrack(track)); CHECK(result);
    CHECK(result.value().issues.size() == 2U);
    for (const auto& issue : result.value().issues) {
      CHECK(issue.severity == interchange::SmfIssueSeverity::Loss);
      CHECK(issue.message.find("channel 3") != std::string::npos);
    }
    CHECK(result.value().issues.front().tick == time::Tick{0});
    CHECK(result.value().issues.back().tick == time::Tick{1});
  }
}

TEST_CASE("SMF decoder rejects hostile lengths, SMPTE division and malformed VLQ before allocation") {
  using namespace seam;
  auto truncated = fileWithTrack({0U, 0xffU, 0x2fU, 0U});
  truncated[18U] = 0xffU; // MTrk length high byte; exceeds remaining input.
  CHECK(!interchange::decodeSmf(truncated));
  CHECK(!interchange::decodeSmf(fileWithTrack({0U, 0xffU, 0x2fU, 0U}, 0xe728U)));
  CHECK(!interchange::decodeSmf(fileWithTrack({0x80U, 0x80U, 0x80U, 0x80U, 0U})));
  auto oversized = fileWithTrack({0U, 0xffU, 0x01U, 0x7fU});
  interchange::SmfLimits limits; limits.maximumBytes = 8U;
  CHECK(!interchange::decodeSmf(oversized, limits));
}

TEST_CASE("SMF decoder enforces the note budget before retaining more active notes") {
  using namespace seam;
  const auto bytes = fileWithTrack({
      0U, 0x90U, 60U, 100U,
      0U, 0x90U, 62U, 100U,
      // This malformed End-of-Track event must not be reached after the
      // second unclosed note already exceeds the one-note admission limit.
      0U, 0xffU, 0x2fU, 1U, 0U});
  interchange::SmfLimits limits;
  limits.maximumNotes = 1U;
  const auto decoded = interchange::decodeSmf(bytes, limits);
  CHECK(!decoded);
  CHECK(decoded.error().code == core::ErrorCode::InvalidArgument);
  CHECK(decoded.error().message == "SMF note count exceeds bounds");
}

TEST_CASE("SMF encoder preserves source state and rejects invalid score fields") {
  using namespace seam;
  interchange::SmfScore score;
  score.ppq = 480U;
  score.notes.push_back({time::Tick{-1}, time::Tick{10}, 60U, 100U, 0U});
  const auto before = score;
  CHECK(!interchange::encodeSmf(score));
  CHECK(score == before);
  score.notes.front() = {time::Tick{0}, time::Tick{10}, 128U, 100U, 0U};
  CHECK(!interchange::encodeSmf(score));
  score.notes.front() = {time::Tick{0}, time::Tick{10}, 60U, 100U, 0U};
  score.texts = {{time::Tick{0}, std::string{"a\0b", 3U}, true}};
  const auto nulText = interchange::encodeSmf(score);
  CHECK(!nulText);
  CHECK(nulText.error().message.find("contains NUL") != std::string::npos);
}

TEST_CASE("SMF encoder enforces cumulative wire event budget before allocation") {
  using namespace seam;
  interchange::SmfScore score;
  score.tracks = {{"Lead"}};
  score.tempos = {{time::Tick{0}, 120.0}};
  score.meters = {{time::Tick{0}, 4U, 2U}};
  score.texts = {{time::Tick{0}, "la", true}};
  score.notes = {{time::Tick{0}, time::Tick{480}, 60U, 100U, 0U}};
  interchange::SmfLimits exact;
  exact.maximumSerializedEvents = 7U; // EOT + note-on/off + lyric + tempo + meter + track name.
  const auto encoded = interchange::encodeSmf(score, exact); CHECK(encoded);
  const auto decoded = interchange::decodeSmf(encoded.value(), exact); CHECK(decoded);
  CHECK(decoded.value().notes == score.notes);
  CHECK(decoded.value().texts == score.texts);
  CHECK(decoded.value().issues.empty());

  auto exceeded = exact;
  exceeded.maximumSerializedEvents = 6U;
  const auto refused = interchange::encodeSmf(score, exceeded);
  CHECK(!refused);
  CHECK(refused.error().message.find("serialized event count") != std::string::npos);
}

TEST_CASE("SMF project export refuses unrepresentable PPQ and preserves the SMF boundary") {
  using namespace seam;
  application::ProjectFactory factory{699000U};
  const auto makeProject = [&](time::Ppq ppq) {
    domain::Project project{domain::ProjectId{static_cast<std::uint64_t>(ppq)},
        "PPQ boundary", ppq};
    const auto track = factory.addVocalTrack(project, "Lead");
    const auto regionId = factory.addRegion(project, track, "Phrase",
        time::Tick{0}, time::Tick{960});
    auto* region = project.findRegion(regionId);
    CHECK(region != nullptr);
    auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U, U"la");
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    CHECK(project.validate());
    return std::pair{std::move(project), std::pair{track, regionId}};
  };
  auto [maximum, maximumIds] = makeProject(32767);
  CHECK(interchange::exportSmfProject(maximum, maximumIds.first, maximumIds.second));
  CHECK(interchange::exportSmfProject(maximum));
  auto [tooLarge, tooLargeIds] = makeProject(32768);
  const auto selected = interchange::exportSmfProject(
      tooLarge, tooLargeIds.first, tooLargeIds.second);
  CHECK(!selected);
  CHECK(selected.error().code == core::ErrorCode::Unsupported);
  CHECK(selected.error().message.find("PPQ") != std::string::npos);
  const auto whole = interchange::exportSmfProject(tooLarge);
  CHECK(!whole);
  CHECK(whole.error().code == core::ErrorCode::Unsupported);
}

TEST_CASE("SMF project export refuses nested repeated pitches and conflicting onset lyrics") {
  using namespace seam;
  application::ProjectFactory factory{698000U};
  auto project = factory.createProject("Unrepresentable MIDI identities");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  const auto firstId = factory.addRegion(project, trackId, "A",
      time::Tick{0}, time::Tick{960});
  const auto secondId = factory.addRegion(project, trackId, "B",
      time::Tick{100}, time::Tick{960});
  const auto addNote = [&](domain::RegionId regionId, std::uint8_t key,
                           std::u32string text, std::int64_t start,
                           std::int64_t duration) {
    auto* region = project.findRegion(regionId);
    CHECK(region != nullptr);
    auto [lyric, note] = factory.makeNote(time::Tick{start}, time::Tick{duration},
        key, std::move(text));
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  };
  addNote(firstId, 60U, U"la", 0, 500);
  addNote(secondId, 60U, U"oh", 0, 100);
  CHECK(project.validate());
  auto nested = interchange::exportSmfProject(project);
  CHECK(!nested);
  CHECK(nested.error().code == core::ErrorCode::Unsupported);
  CHECK(nested.error().message.find("nested overlaps") != std::string::npos);

  auto conflicting = factory.createProject("Conflicting lyric identity");
  const auto conflictTrack = factory.addVocalTrack(conflicting, "Lead");
  const auto left = factory.addRegion(conflicting, conflictTrack, "A",
      time::Tick{0}, time::Tick{960});
  const auto right = factory.addRegion(conflicting, conflictTrack, "B",
      time::Tick{0}, time::Tick{960});
  const auto addAtZero = [&](domain::RegionId regionId, std::uint8_t key,
                             std::u32string text) {
    auto* region = conflicting.findRegion(regionId);
    CHECK(region != nullptr);
    auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{240},
        key, std::move(text));
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  };
  addAtZero(left, 60U, U"la");
  addAtZero(right, 64U, U"do");
  CHECK(conflicting.validate());
  const auto refused = interchange::exportSmfProject(conflicting);
  CHECK(!refused);
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("different lyric surfaces") != std::string::npos);

  auto chord = factory.createProject("Shared onset lyric identity");
  const auto chordTrack = factory.addVocalTrack(chord, "Lead");
  const auto chordRegion = factory.addRegion(chord, chordTrack, "A",
      time::Tick{0}, time::Tick{960});
  auto* target = chord.findRegion(chordRegion);
  auto [firstLyric, firstNote] = factory.makeNote(
      time::Tick{0}, time::Tick{240}, 60U, U"la");
  auto [sameLyric, secondNote] = factory.makeNote(
      time::Tick{0}, time::Tick{240}, 64U, U"la");
  target->lyrics.push_back(std::move(firstLyric));
  target->lyrics.push_back(std::move(sameLyric));
  target->notes.push_back(std::move(firstNote));
  target->notes.push_back(std::move(secondNote));
  CHECK(chord.validate());
  const auto score = interchange::exportSmfProject(chord); CHECK(score);
  CHECK(std::count_if(score.value().texts.begin(), score.value().texts.end(),
      [](const auto& text) { return text.lyric; }) == 1);
  const auto bytes = interchange::encodeSmf(score.value()); CHECK(bytes);
  const auto decoded = interchange::decodeSmf(bytes.value()); CHECK(decoded);
  CHECK(decoded.value().issues.empty());
}

TEST_CASE("SMF whole-project export spends shared text budget on lyrics before track names") {
  using namespace seam;
  application::ProjectFactory factory{697000U};
  auto project = factory.createProject("Shared text budget");
  const auto addVoice = [&](std::string name, std::uint8_t key, std::u32string text) {
    const auto trackId = factory.addVocalTrack(project, std::move(name));
    const auto regionId = factory.addRegion(project, trackId, "Phrase",
        time::Tick{0}, time::Tick{960});
    auto* region = project.findRegion(regionId);
    auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{240},
        key, std::move(text));
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  };
  addVoice("Lead", 60U, U"la");
  addVoice("Harmony", 64U, U"do");
  CHECK(project.validate());
  interchange::SmfLimits limits;
  limits.maximumTextBytes = 4U;
  const auto exported = interchange::exportSmfProject(project, limits); CHECK(exported);
  CHECK(exported.value().texts.size() == 2U);
  CHECK(exported.value().texts[0].text == "la");
  CHECK(exported.value().texts[1].text == "do");
  CHECK(exported.value().tracks[0].name.empty());
  CHECK(exported.value().tracks[1].name.empty());
  CHECK(std::count_if(exported.value().issues.begin(), exported.value().issues.end(),
      [](const auto& issue) {
        return issue.message.find("Track name") != std::string::npos;
      }) == 2);
  CHECK(interchange::encodeSmf(exported.value(), limits));
}

TEST_CASE("SMF whole-project export admits names against actual remaining wire slots") {
  using namespace seam;
  application::ProjectFactory factory{696000U};
  auto project = factory.createProject("Track-name event budget");
  const auto addVoice = [&](std::string name, std::uint8_t key,
                            std::u32string lyricText) {
    const auto trackId = factory.addVocalTrack(project, std::move(name));
    const auto regionId = factory.addRegion(project, trackId, "Phrase",
        time::Tick{0}, time::Tick{960});
    auto* region = project.findRegion(regionId);
    auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{240},
        key, std::move(lyricText));
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  };
  addVoice("Lead", 60U, U"la");
  addVoice("Harmony", 64U, U"do");
  CHECK(project.validate());

  for (const auto& [budget, expectedNames] : {
           std::pair<std::size_t, std::size_t>{10U, 0U}, {11U, 1U}, {12U, 2U}}) {
    interchange::SmfLimits limits;
    limits.maximumSerializedEvents = budget;
    const auto score = interchange::exportSmfProject(project, limits); CHECK(score);
    const auto names = static_cast<std::size_t>(std::count_if(
        score.value().tracks.begin(), score.value().tracks.end(),
        [](const auto& track) { return !track.name.empty(); }));
    CHECK(names == expectedNames);
    CHECK(score.value().texts.size() == 2U);
    CHECK(score.value().validate(limits));
    CHECK(interchange::encodeSmf(score.value(), limits));
  }
}

TEST_CASE("SMF project conversion creates an inert unsaved project and reports SEAM-only losses") {
  using namespace seam;
  interchange::SmfScore score;
  score.ppq = 480U;
  score.tempos = {{time::Tick{0}, 128.0}};
  score.meters = {{time::Tick{0}, 4U, 2U}};
  score.texts = {{time::Tick{0}, "hello", true}};
  score.notes = {{time::Tick{0}, time::Tick{480}, 60U, 100U, 0U}};
  const auto bytes = interchange::encodeSmf(score); CHECK(bytes);
  application::ProjectFactory factory{700000U};
  const auto draft = interchange::importSmfProject(bytes.value(), factory,
      interchange::SmfImportRequest{.projectName = "Imported", .trackName = "Lead", .regionName = "Verse", .language = domain::Language::English});
  CHECK(draft);
  CHECK(draft.value().project.name() == "Imported");
  CHECK(draft.value().project.vocalTracks().size() == 1U);
  CHECK(draft.value().project.vocalTracks().front().regions.front().notes.size() == 1U);
  CHECK(draft.value().project.vocalTracks().front().regions.front().lyrics.front().surface == U"hello");
  CHECK(draft.value().project.tempoMap().bpmAt(time::Tick{0}) == 128.0);
  CHECK(draft.value().project.meterMap().meterAt(time::Tick{0}).denominator == 4U);

  auto project = draft.value().project;
  auto& region = project.vocalTracks().front().regions.front();
  CHECK(project.vocalTracks().front().name == "Lead");
  region.phonemeOverrides.push_back({.key = {region.notes.front().id, 0U}, .symbol = "eh0"});
  const auto exported = interchange::exportSmfProject(project,
      project.vocalTracks().front().id, region.id); CHECK(exported);
  CHECK(exported.value().notes.size() == 1U);
  CHECK(exported.value().tracks.size() == 1U);
  CHECK(exported.value().tracks.front().name == "Lead");
  CHECK(!exported.value().issues.empty());
  CHECK(exported.value().issues.front().severity == interchange::SmfIssueSeverity::Loss);
}

TEST_CASE("SMF import preserves musical time across 480 960 and 1920 PPQ") {
  using namespace seam;
  for (const auto sourcePpq : {480U, 960U, 1920U}) {
    const auto quarter = static_cast<std::int64_t>(sourcePpq);
    interchange::SmfScore score;
    score.ppq = static_cast<std::uint16_t>(sourcePpq);
    score.tempos = {{time::Tick{0}, 120.0}, {time::Tick{quarter * 2}, 60.0}};
    score.meters = {{time::Tick{0}, 4U, 2U}, {time::Tick{quarter * 4}, 3U, 2U}};
    score.texts = {{time::Tick{quarter}, "first", true},
                   {time::Tick{quarter * 4}, "second", true}};
    score.notes = {{time::Tick{quarter}, time::Tick{quarter}, 60U, 100U, 0U},
                   {time::Tick{quarter * 4}, time::Tick{quarter * 2}, 62U, 100U, 0U}};
    const auto bytes = interchange::encodeSmf(score); CHECK(bytes);
    const auto before = bytes.value();
    application::ProjectFactory factory{710000U};
    const auto imported = interchange::importSmfProject(bytes.value(), factory); CHECK(imported);
    const auto& project = imported.value().project;
    const auto& region = project.vocalTracks().front().regions.front();
    CHECK(project.ppq() == 960);
    CHECK(region.notes[0].startTick == time::Tick{960});
    CHECK(region.notes[0].durationTick == time::Tick{960});
    CHECK(region.notes[1].startTick == time::Tick{3840});
    CHECK(region.notes[1].durationTick == time::Tick{1920});
    CHECK(region.durationTick == time::Tick{5760});
    CHECK(region.lyrics[0].surface == U"first");
    CHECK(region.lyrics[1].surface == U"second");
    CHECK(project.tempoMap().events()[1].tick == time::Tick{1920});
    CHECK(project.meterMap().events()[1].tick == time::Tick{3840});
    CHECK(project.meterMap().meterAt(time::Tick{3840}).numerator == 3U);
    CHECK(std::abs(project.tempoMap().secondsAt(region.notes[0].startTick) - 0.5) < 1e-12);
    CHECK(std::abs(project.tempoMap().secondsAt(region.durationTick) - 5.0) < 1e-12);
    CHECK(imported.value().score.ppq == score.ppq);
    CHECK(imported.value().score.notes == score.notes);
    CHECK(imported.value().score.issues.empty());
    CHECK(bytes.value() == before);
    const auto exported = interchange::exportSmfProject(project,
        project.vocalTracks().front().id, region.id); CHECK(exported);
    CHECK(exported.value().ppq == 960U);
    CHECK(exported.value().tracks.size() == 1U);
    CHECK(exported.value().tracks.front().name == project.vocalTracks().front().name);
    CHECK(exported.value().notes[1].start == time::Tick{3840});
    CHECK(exported.value().notes[1].duration == time::Tick{1920});
  }
}

TEST_CASE("SMF import rounds shared absolute endpoints once and discloses quantization") {
  using namespace seam;
  interchange::SmfScore score;
  score.ppq = 7U;
  score.tempos = {{time::Tick{0}, 120.0}, {time::Tick{2}, 60.0}};
  score.meters = {{time::Tick{0}, 4U, 2U}, {time::Tick{7}, 3U, 2U}};
  score.texts = {{time::Tick{1}, "one", true}, {time::Tick{4}, "two", true}};
  score.notes = {{time::Tick{1}, time::Tick{3}, 60U, 100U, 0U},
                 {time::Tick{4}, time::Tick{3}, 62U, 100U, 0U}};
  const auto bytes = interchange::encodeSmf(score); CHECK(bytes);
  application::ProjectFactory factory{720000U};
  const auto imported = interchange::importSmfProject(bytes.value(), factory); CHECK(imported);
  const auto& project = imported.value().project;
  const auto& region = project.vocalTracks().front().regions.front();
  CHECK(region.notes[0].startTick == time::Tick{137});
  CHECK(region.notes[0].durationTick == time::Tick{412});
  CHECK(region.notes[1].startTick == time::Tick{549});
  CHECK(region.notes[1].durationTick == time::Tick{411});
  CHECK(region.notes[0].startTick + region.notes[0].durationTick == region.notes[1].startTick);
  CHECK(region.notes[1].startTick + region.notes[1].durationTick == time::Tick{960});
  CHECK(region.durationTick == time::Tick{960});
  CHECK(project.tempoMap().events()[1].tick == time::Tick{274});
  CHECK(project.meterMap().events()[1].tick == time::Tick{960});
  CHECK(region.lyrics[0].surface == U"one");
  CHECK(region.lyrics[1].surface == U"two");
  CHECK(imported.value().score.ppq == 7U);
  CHECK(imported.value().score.notes == score.notes);
  CHECK(imported.value().score.issues.size() == 3U);
  for (const auto& issue : imported.value().score.issues) {
    CHECK(issue.severity == interchange::SmfIssueSeverity::Warning);
    CHECK(issue.message.find("rounded") != std::string::npos);
  }
}

TEST_CASE("SMF import refuses rounding collisions instead of erasing timeline events") {
  using namespace seam;
  for (const int family : {0, 1, 2, 3}) {
    interchange::SmfScore score;
    score.ppq = 1920U;
    score.notes = {{time::Tick{0}, time::Tick{1920}, 60U, 100U, 0U}};
    if (family == 0) score.notes = {{time::Tick{1}, time::Tick{1}, 60U, 100U, 0U}};
    if (family == 1) score.tempos = {{time::Tick{1}, 120.0}, {time::Tick{2}, 90.0}};
    if (family == 2) score.meters = {{time::Tick{1}, 4U, 2U}, {time::Tick{2}, 3U, 2U}};
    if (family == 3) score.texts = {{time::Tick{1}, "one", true}, {time::Tick{2}, "two", true}};
    const auto bytes = interchange::encodeSmf(score); CHECK(bytes);
    application::ProjectFactory factory{730000U};
    const auto next = factory.nextIdValue();
    const auto imported = interchange::importSmfProject(bytes.value(), factory);
    CHECK(!imported);
    CHECK(imported.error().code == core::ErrorCode::Unsupported);
    CHECK(imported.error().message.find("collid") != std::string::npos);
    CHECK(factory.nextIdValue() == next);
  }
}

TEST_CASE("SMF import rounds exact half ticks upward without stretching note endpoints") {
  using namespace seam;
  interchange::SmfScore score;
  score.ppq = 1920U;
  score.notes = {{time::Tick{1}, time::Tick{2}, 60U, 100U, 0U}};
  score.texts = {{time::Tick{1}, "half", true}};
  const auto bytes = interchange::encodeSmf(score); CHECK(bytes);
  application::ProjectFactory factory{735000U};
  const auto imported = interchange::importSmfProject(bytes.value(), factory); CHECK(imported);
  const auto& region = imported.value().project.vocalTracks().front().regions.front();
  CHECK(region.notes.front().startTick == time::Tick{1});
  CHECK(region.notes.front().durationTick == time::Tick{1});
  CHECK(region.lyrics.front().surface == U"half");
  CHECK(imported.value().score.issues.size() == 2U);
  CHECK(imported.value().score.issues[0].tick == time::Tick{1});
  CHECK(imported.value().score.issues[1].tick == time::Tick{3});
}

TEST_CASE("SMF import checks scaled bounds before allocating project identities") {
  using namespace seam;
  interchange::SmfScore score;
  score.ppq = 480U;
  score.notes = {{time::Tick{480}, time::Tick{480}, 60U, 100U, 0U}};
  const auto bytes = interchange::encodeSmf(score); CHECK(bytes);
  interchange::SmfLimits limits;
  limits.maximumTick = 1920;
  application::ProjectFactory exactFactory{740000U};
  const auto exact = interchange::importSmfProject(bytes.value(), exactFactory, {}, limits);
  CHECK(exact);
  CHECK(exact.value().project.vocalTracks().front().regions.front().durationTick == time::Tick{1920});
  limits.maximumTick = 1919;
  application::ProjectFactory rejectedFactory{750000U};
  const auto next = rejectedFactory.nextIdValue();
  const auto rejected = interchange::importSmfProject(bytes.value(), rejectedFactory, {}, limits);
  CHECK(!rejected);
  CHECK(rejected.error().code == core::ErrorCode::InvalidArgument);
  CHECK(rejected.error().message.find("scaled") != std::string::npos);
  CHECK(rejectedFactory.nextIdValue() == next);
}

TEST_CASE("SMF import scales maximum VLQ positions without 32 bit overflow") {
  using namespace seam;
  interchange::SmfScore score;
  score.ppq = 1U;
  score.notes = {{time::Tick{0x0fffffff}, time::Tick{1}, 60U, 100U, 0U}};
  const auto bytes = interchange::encodeSmf(score); CHECK(bytes);
  application::ProjectFactory factory{760000U};
  const auto imported = interchange::importSmfProject(bytes.value(), factory); CHECK(imported);
  const auto& region = imported.value().project.vocalTracks().front().regions.front();
  CHECK(region.notes.front().startTick == time::Tick{257698036800LL});
  CHECK(region.notes.front().durationTick == time::Tick{960});
  CHECK(region.durationTick == time::Tick{257698037760LL});
  interchange::SmfLimits limits;
  limits.maximumTick = 0xffffffffLL;
  application::ProjectFactory limitedFactory{770000U};
  const auto next = limitedFactory.nextIdValue();
  const auto rejected = interchange::importSmfProject(bytes.value(), limitedFactory, {}, limits);
  CHECK(!rejected);
  CHECK(rejected.error().code == core::ErrorCode::InvalidArgument);
  CHECK(limitedFactory.nextIdValue() == next);
}

TEST_CASE("SMF scaled bounds also cover tempo meter and text without notes") {
  using namespace seam;
  for (const int family : {0, 1, 2}) {
    interchange::SmfScore score;
    score.ppq = 480U;
    if (family == 0) score.tempos = {{time::Tick{1000}, 120.0}};
    if (family == 1) score.meters = {{time::Tick{1000}, 4U, 2U}};
    if (family == 2) score.texts = {{time::Tick{1000}, "later", true}};
    const auto bytes = interchange::encodeSmf(score); CHECK(bytes);
    interchange::SmfLimits limits;
    limits.maximumTick = 1999;
    application::ProjectFactory factory{780000U};
    const auto next = factory.nextIdValue();
    const auto rejected = interchange::importSmfProject(bytes.value(), factory, {}, limits);
    CHECK(!rejected);
    CHECK(rejected.error().code == core::ErrorCode::InvalidArgument);
    CHECK(rejected.error().message.find("scaled") != std::string::npos);
    CHECK(factory.nextIdValue() == next);
  }
}

TEST_CASE("SMF import reports nondefault note velocities and channel assignments") {
  using namespace seam;
  interchange::SmfScore score;
  score.ppq = 480U;
  score.notes = {{time::Tick{0}, time::Tick{480}, 60U, 100U, 0U},
                 {time::Tick{480}, time::Tick{480}, 62U, 80U, 2U},
                 {time::Tick{960}, time::Tick{480}, 64U, 120U, 7U}};
  score.texts = {{time::Tick{0}, "a", true}, {time::Tick{480}, "b", true},
                 {time::Tick{960}, "c", true}};
  const auto bytes = interchange::encodeSmf(score); CHECK(bytes);
  application::ProjectFactory factory{790000U};
  const auto imported = interchange::importSmfProject(bytes.value(), factory); CHECK(imported);
  CHECK(imported.value().score.notes == score.notes);
  const auto& issues = imported.value().score.issues;
  CHECK(issues.size() == 2U);
  for (const auto& issue : issues) {
    CHECK(issue.severity == interchange::SmfIssueSeverity::Loss);
    CHECK(issue.tick == time::Tick{480});
    CHECK(issue.message.find("2 ") != std::string::npos);
    CHECK(issue.message.find("480..960 at 480 PPQ") != std::string::npos);
  }
  CHECK(issues[0].message.find("velocities") != std::string::npos);
  CHECK(issues[1].message.find("channel assignments") != std::string::npos);
  const auto& project = imported.value().project;
  const auto& track = project.vocalTracks().front();
  const auto exported = interchange::exportSmfProject(project, track.id, track.regions.front().id);
  CHECK(exported);
  CHECK(exported.value().notes.size() == 3U);
  for (const auto& note : exported.value().notes) {
    CHECK(note.velocity == 100U);
    CHECK(note.channel == 0U);
  }
}

TEST_CASE("SMF import reports nonlyric unmatched and extra lyric events separately") {
  using namespace seam;
  interchange::SmfScore score;
  score.ppq = 480U;
  score.notes = {{time::Tick{0}, time::Tick{480}, 60U, 100U, 0U},
                 {time::Tick{480}, time::Tick{480}, 62U, 100U, 0U}};
  score.texts = {{time::Tick{0}, "one", true}, {time::Tick{0}, "two", true},
                 {time::Tick{0}, "tri", true}, {time::Tick{120}, "unused", true},
                 {time::Tick{240}, "marker", false}, {time::Tick{480}, "end", true},
                 {time::Tick{600}, "annotation", false}, {time::Tick{720}, "orphan", true}};
  const auto bytes = interchange::encodeSmf(score); CHECK(bytes);
  const auto decoded = interchange::decodeSmf(bytes.value()); CHECK(decoded);
  CHECK(decoded.value().issues.empty()); // Codec retains these; project cannot.
  application::ProjectFactory factory{800000U};
  const auto imported = interchange::importSmfProject(bytes.value(), factory); CHECK(imported);
  CHECK(imported.value().score.texts == decoded.value().texts);
  const auto& region = imported.value().project.vocalTracks().front().regions.front();
  CHECK(region.lyrics[0].surface == U"one");
  CHECK(region.lyrics[1].surface == U"end");
  const auto& issues = imported.value().score.issues;
  CHECK(issues.size() == 3U);
  for (const auto& issue : issues) {
    CHECK(issue.severity == interchange::SmfIssueSeverity::Loss);
    CHECK(issue.message.find("2 ") != std::string::npos);
    CHECK(issue.message.find("at 480 PPQ") != std::string::npos);
  }
  CHECK(issues[0].tick == time::Tick{240});
  CHECK(issues[0].message.find("non-lyric text events") != std::string::npos);
  CHECK(issues[1].tick == time::Tick{120});
  CHECK(issues[1].message.find("unambiguous matching note onset") != std::string::npos);
  CHECK(issues[2].tick == time::Tick{0});
  CHECK(issues[2].message.find("extra lyric events") != std::string::npos);
}

TEST_CASE("SMF text compatibility losses preserve notes and strip lyric terminators") {
  using namespace seam;
  const std::vector<std::uint8_t> noteTail{
      0U, 0x90U, 60U, 100U,
      0x83U, 0x60U, 0x80U, 60U, 0U,
      0U, 0xffU, 0x2fU, 0U};
  const auto withMetaEvents = [&](std::uint8_t type,
                                  const std::vector<std::vector<std::uint8_t>>& texts) {
    std::vector<std::uint8_t> track;
    for (const auto& text : texts) {
      track.insert(track.end(), {0U, 0xffU, type,
          static_cast<std::uint8_t>(text.size())});
      track.insert(track.end(), text.begin(), text.end());
    }
    track.insert(track.end(), noteTail.begin(), noteTail.end());
    return fileWithTrack(std::move(track));
  };
  const auto withMeta = [&](std::uint8_t type,
                            const std::vector<std::uint8_t>& text) {
    return interchange::decodeSmf(withMetaEvents(type, {text}));
  };

  const auto nulComment = withMeta(0x01U, {'C', 'm', 't', 0U});
  CHECK(nulComment);
  CHECK(nulComment.value().notes.size() == 1U);
  CHECK(nulComment.value().texts.empty());
  CHECK(nulComment.value().issues.size() == 1U);
  CHECK(nulComment.value().issues.front().severity == interchange::SmfIssueSeverity::Loss);

  const auto legacyComment = withMeta(0x01U, {0x82U, 0xa0U});
  CHECK(legacyComment);
  CHECK(legacyComment.value().notes.size() == 1U);
  CHECK(legacyComment.value().texts.empty());
  CHECK(legacyComment.value().issues.front().message.find("not NUL-free UTF-8") != std::string::npos);

  const auto terminatedLyric = withMeta(0x05U, {'l', 'a', 0U});
  CHECK(terminatedLyric);
  CHECK(terminatedLyric.value().notes.size() == 1U);
  CHECK(terminatedLyric.value().texts.size() == 1U);
  CHECK(terminatedLyric.value().texts.front().text == "la");
  CHECK(terminatedLyric.value().issues.size() == 1U);
  CHECK(terminatedLyric.value().issues.front().severity == interchange::SmfIssueSeverity::Warning);
  CHECK(terminatedLyric.value().issues.front().message.find(
      "1 trailing NUL lyric terminator was removed") != std::string::npos);

  const auto nulOnlyLyric = withMeta(0x05U, {0U});
  CHECK(nulOnlyLyric);
  CHECK(nulOnlyLyric.value().notes.size() == 1U);
  CHECK(nulOnlyLyric.value().texts.empty());
  CHECK(nulOnlyLyric.value().issues.size() == 1U);
  CHECK(nulOnlyLyric.value().issues.front().severity == interchange::SmfIssueSeverity::Loss);
  CHECK(nulOnlyLyric.value().issues.front().message.find("empty after") != std::string::npos);

  const auto emptyLyric = withMeta(0x05U, {});
  CHECK(emptyLyric);
  CHECK(emptyLyric.value().notes.size() == 1U);
  CHECK(emptyLyric.value().texts.empty());
  CHECK(emptyLyric.value().issues.size() == 1U);
  CHECK(emptyLyric.value().issues.front().severity == interchange::SmfIssueSeverity::Loss);
  CHECK(emptyLyric.value().issues.front().message.find(
      "was empty or became empty after removing a trailing NUL") != std::string::npos);

  for (const auto& doubled : {std::vector<std::uint8_t>{'l', 'a', 0U, 0U},
                              std::vector<std::uint8_t>{'a', 0U, 'b', 0U},
                              std::vector<std::uint8_t>{0x82U, 0xa0U, 0U}}) {
    const auto discarded = withMeta(0x05U, doubled);
    CHECK(discarded);
    CHECK(discarded.value().notes.size() == 1U);
    CHECK(discarded.value().texts.empty());
    CHECK(discarded.value().issues.size() == 1U);
    CHECK(discarded.value().issues.front().severity == interchange::SmfIssueSeverity::Loss);
  }

  const auto legacyLyric = withMeta(0x05U, {0x82U, 0xa0U});
  CHECK(legacyLyric);
  CHECK(legacyLyric.value().notes.size() == 1U);
  CHECK(legacyLyric.value().texts.empty());
  CHECK(legacyLyric.value().issues.front().severity == interchange::SmfIssueSeverity::Loss);

  const auto repeated = interchange::decodeSmf(withMetaEvents(0x05U,
      {{'l', 'a', 0U}, {'l', 'a', 0U}, {0x82U, 0xa0U}, {0x82U, 0xa0U}}));
  CHECK(repeated);
  CHECK(repeated.value().issues.size() == 2U);
  CHECK(repeated.value().issues[0].severity == interchange::SmfIssueSeverity::Warning);
  CHECK(repeated.value().issues[0].message.find("2 trailing NUL lyric terminators") != std::string::npos);
  CHECK(repeated.value().issues[1].severity == interchange::SmfIssueSeverity::Loss);
  CHECK(repeated.value().issues[1].message.find("2 lyric payloads") != std::string::npos);

  const std::vector<std::uint8_t> firstTrack{
      0U, 0xffU, 0x05U, 3U, 'l', 'a', 0U,
      0x78U, 0xffU, 0x05U, 3U, 'l', 'a', 0U,
      0U, 0x90U, 60U, 100U, 0x83U, 0x60U, 0x80U, 60U, 0U,
      0U, 0xffU, 0x2fU, 0U};
  const std::vector<std::uint8_t> secondTrack{
      0x81U, 0x70U, 0xffU, 0x05U, 3U, 'l', 'a', 0U,
      0x81U, 0x70U, 0xffU, 0x05U, 3U, 'l', 'a', 0U,
      0U, 0x90U, 62U, 100U, 0x83U, 0x60U, 0x80U, 62U, 0U,
      0U, 0xffU, 0x2fU, 0U};
  const auto byTrack = interchange::decodeSmf(
      fileWithTracks({firstTrack, secondTrack}));
  CHECK(byTrack);
  CHECK(byTrack.value().notes.size() == 2U);
  CHECK(byTrack.value().issues.size() == 2U);
  CHECK(byTrack.value().issues[0].tick == time::Tick{0});
  CHECK(byTrack.value().issues[0].message.find(
      "source track 1; source ticks 0..120 at 480 PPQ") != std::string::npos);
  CHECK(byTrack.value().issues[1].tick == time::Tick{240});
  CHECK(byTrack.value().issues[1].message.find(
      "source track 2; source ticks 240..480 at 480 PPQ") != std::string::npos);

  application::ProjectFactory factory{800000U};
  const auto imported = interchange::importSmfProject(
      withMetaEvents(0x05U, {{0x82U, 0xa0U}}), factory);
  CHECK(imported);
  CHECK(imported.value().project.vocalTracks().front().regions.front().notes.size() == 1U);
  CHECK(imported.value().project.vocalTracks().front().regions.front().lyrics.size() == 1U);
  CHECK(imported.value().project.vocalTracks().front().regions.front().lyrics.front().surface.empty());
  CHECK(std::any_of(imported.value().score.issues.begin(), imported.value().score.issues.end(),
      [](const auto& issue) {
        return issue.message.find("no matched lyric events") != std::string::npos;
      }));
}

TEST_CASE("SMF Type-1 track identity and names survive decode encode and project import") {
  using namespace seam;
  const std::vector<std::uint8_t> first{
      0U, 0xffU, 0x03U, 4U, 'L', 'e', 'a', 'd',
      0U, 0xffU, 0x05U, 4U, 'h', 'e', 'l', 'o',
      0U, 0x90U, 60U, 100U, 0x83U, 0x60U, 0x80U, 60U, 0U,
      0U, 0xffU, 0x2fU, 0U};
  const std::vector<std::uint8_t> second{
      0U, 0xffU, 0x03U, 4U, 'B', 'a', 's', 's',
      0x83U, 0x60U, 0xffU, 0x05U, 4U, 'w', 'o', 'r', 'd',
      0U, 0x90U, 64U, 100U, 0x83U, 0x60U, 0x80U, 64U, 0U,
      0U, 0xffU, 0x2fU, 0U};
  auto bytes = fileWithTrack(first, 480U, 1U, 2U);
  bytes.insert(bytes.end(), {'M', 'T', 'r', 'k'});
  u32(bytes, static_cast<std::uint32_t>(second.size()));
  bytes.insert(bytes.end(), second.begin(), second.end());
  const auto decoded = interchange::decodeSmf(bytes); CHECK(decoded);
  CHECK(decoded.value().notes.size() == 2U);
  CHECK(decoded.value().tracks.size() == 2U);
  CHECK(decoded.value().tracks[0].name == "Lead");
  CHECK(decoded.value().tracks[1].name == "Bass");
  CHECK(decoded.value().notes[0].track == 0U);
  CHECK(decoded.value().notes[1].track == 1U);
  CHECK(decoded.value().texts.size() == 2U);
  CHECK(decoded.value().texts[0].track == 0U);
  CHECK(decoded.value().texts[1].track == 1U);
  CHECK(decoded.value().issues.empty());
  const auto encoded = interchange::encodeSmf(decoded.value()); CHECK(encoded);
  const auto roundTrip = interchange::decodeSmf(encoded.value()); CHECK(roundTrip);
  CHECK(roundTrip.value().tracks == decoded.value().tracks);
  CHECK(roundTrip.value().notes == decoded.value().notes);
  CHECK(roundTrip.value().texts == decoded.value().texts);
  application::ProjectFactory factory{810000U};
  const auto imported = interchange::importSmfProject(bytes, factory); CHECK(imported);
  CHECK(imported.value().project.vocalTracks().size() == 2U);
  CHECK(imported.value().project.vocalTracks()[0].name == "Lead");
  CHECK(imported.value().project.vocalTracks()[1].name == "Bass");
  CHECK(imported.value().project.vocalTracks()[0].regions.front().notes.size() == 1U);
  CHECK(imported.value().project.vocalTracks()[1].regions.front().notes.size() == 1U);
  CHECK(imported.value().project.vocalTracks()[0].regions.front().lyrics.front().surface == U"helo");
  CHECK(imported.value().project.vocalTracks()[1].regions.front().lyrics.front().surface == U"word");
  CHECK(std::none_of(imported.value().score.issues.begin(), imported.value().score.issues.end(),
      [](const interchange::SmfIssue& issue) {
        return issue.severity == interchange::SmfIssueSeverity::Warning &&
            issue.message.find("no matched lyric events") != std::string::npos;
      }));
}

TEST_CASE("SMF legacy non-UTF-8 track names are discarded without rejecting notes") {
  using namespace seam;
  const auto bytes = fileWithTrack({
      0U, 0xffU, 0x03U, 2U, 0x82U, 0xa0U, // Shift-JIS "あ".
      0U, 0x90U, 60U, 100U, 0x83U, 0x60U, 0x80U, 60U, 0U,
      0U, 0xffU, 0x2fU, 0U});
  const auto decoded = interchange::decodeSmf(bytes); CHECK(decoded);
  CHECK(decoded.value().notes.size() == 1U);
  CHECK(decoded.value().tracks.size() == 1U);
  CHECK(decoded.value().tracks.front().name.empty());
  CHECK(decoded.value().issues.size() == 1U);
  CHECK(decoded.value().issues.front().severity == interchange::SmfIssueSeverity::Loss);
  CHECK(decoded.value().issues.front().message.find("Track name") != std::string::npos);
}

TEST_CASE("SMF lyrics on a separate note-free track pair only when mapping is unambiguous") {
  using namespace seam;
  const auto bytes = fileWithTracks({
      {0U, 0xffU, 0x05U, 2U, 'l', 'a', 0U, 0xffU, 0x2fU, 0U},
      {0U, 0x90U, 60U, 100U, 0x83U, 0x60U, 0x80U, 60U, 0U,
       0U, 0xffU, 0x2fU, 0U}});
  application::ProjectFactory factory{815000U};
  const auto imported = interchange::importSmfProject(bytes, factory); CHECK(imported);
  CHECK(imported.value().project.vocalTracks().size() == 1U);
  const auto& region = imported.value().project.vocalTracks().front().regions.front();
  CHECK(region.notes.size() == 1U);
  CHECK(region.lyrics.front().surface == U"la");
  CHECK(std::any_of(imported.value().score.issues.begin(), imported.value().score.issues.end(),
      [](const interchange::SmfIssue& issue) {
        return issue.severity == interchange::SmfIssueSeverity::Warning &&
            issue.message.find("only note-bearing track") != std::string::npos;
      }));

  const auto competing = fileWithTracks({
      {0U, 0xffU, 0x05U, 5U, 'w', 'r', 'o', 'n', 'g',
       0U, 0xffU, 0x2fU, 0U},
      {0U, 0xffU, 0x05U, 5U, 'r', 'i', 'g', 'h', 't',
       0U, 0x90U, 60U, 100U, 0x83U, 0x60U, 0x80U, 60U, 0U,
       0U, 0xffU, 0x2fU, 0U}});
  application::ProjectFactory precedenceFactory{815100U};
  const auto precedence = interchange::importSmfProject(competing, precedenceFactory);
  CHECK(precedence);
  CHECK(precedence.value().project.vocalTracks().front().regions.front().lyrics.front().surface == U"right");
  CHECK(std::any_of(precedence.value().score.issues.begin(), precedence.value().score.issues.end(),
      [](const interchange::SmfIssue& issue) {
        return issue.severity == interchange::SmfIssueSeverity::Loss &&
            issue.message.find("extra lyric events") != std::string::npos;
      }));
}

TEST_CASE("SMF conductor-only track and duplicate track names remain distinct from singer tracks") {
  using namespace seam;
  const std::vector<std::uint8_t> conductor{
      0U, 0xffU, 0x51U, 3U, 7U, 0xa1U, 0x20U,
      0U, 0xffU, 0x05U, 9U, 'a', 'm', 'b', 'i', 'g', 'u', 'o', 'u', 's',
      0U, 0xffU, 0x2fU, 0U};
  const auto makeNamedNoteTrack = [](std::uint8_t key) {
    return std::vector<std::uint8_t>{
        0U, 0xffU, 0x03U, 4U, 'P', 'a', 'r', 't',
        0U, 0x90U, key, 100U, 0x83U, 0x60U, 0x80U, key, 0U,
        0U, 0xffU, 0x2fU, 0U};
  };
  const auto bytes = fileWithTracks({conductor, makeNamedNoteTrack(60U), makeNamedNoteTrack(64U)});
  const auto decoded = interchange::decodeSmf(bytes); CHECK(decoded);
  CHECK(decoded.value().tracks.size() == 3U);
  CHECK(decoded.value().notes.size() == 2U);
  CHECK(decoded.value().notes[0].track == 1U);
  CHECK(decoded.value().notes[1].track == 2U);
  application::ProjectFactory factory{816000U};
  const auto imported = interchange::importSmfProject(bytes, factory); CHECK(imported);
  CHECK(imported.value().project.tempoMap().events().size() == 1U);
  CHECK(imported.value().project.vocalTracks().size() == 2U);
  CHECK(imported.value().project.vocalTracks()[0].name == "Part");
  CHECK(imported.value().project.vocalTracks()[1].name == "Part");
  CHECK(std::all_of(imported.value().project.vocalTracks().begin(),
      imported.value().project.vocalTracks().end(), [](const domain::VocalTrack& track) {
        return track.regions.front().lyrics.front().surface != U"ambiguous";
      }));
  CHECK(std::any_of(imported.value().score.issues.begin(), imported.value().score.issues.end(),
      [](const interchange::SmfIssue& issue) {
        return issue.severity == interchange::SmfIssueSeverity::Loss &&
            issue.message.find("no unambiguous matching note onset") != std::string::npos;
      }));
  const auto encoded = interchange::encodeSmf(decoded.value()); CHECK(encoded);
  const std::array<std::uint8_t, 4U> marker{'M', 'T', 'r', 'k'};
  const auto secondTrack = std::search(encoded.value().begin() + 14,
      encoded.value().end(), marker.begin(), marker.end());
  CHECK(secondTrack != encoded.value().end());
  const auto thirdTrack = std::search(secondTrack + 8,
      encoded.value().end(), marker.begin(), marker.end());
  CHECK(thirdTrack != encoded.value().end());
  const auto firstEventOffset = static_cast<std::size_t>(thirdTrack - encoded.value().begin()) + 8U;
  CHECK(encoded.value()[firstEventOffset] == 0U);
  CHECK(encoded.value()[firstEventOffset + 1U] == 0xffU);
  CHECK(encoded.value()[firstEventOffset + 2U] == 0x03U);
}

TEST_CASE("SMF lyric ordering is stable and lyric-free percussion tracks are disclosed") {
  using namespace seam;
  interchange::SmfScore score;
  score.texts = {{time::Tick{0}, "z-first", true, 0U},
                 {time::Tick{0}, "a-second", true, 0U}};
  score.notes = {{time::Tick{0}, time::Tick{480}, 60U, 100U, 0U}};
  const auto encoded = interchange::encodeSmf(score); CHECK(encoded);
  const auto decoded = interchange::decodeSmf(encoded.value()); CHECK(decoded);
  CHECK(decoded.value().texts == score.texts);
  application::ProjectFactory vocalFactory{817000U};
  const auto vocal = interchange::importSmfProject(encoded.value(), vocalFactory); CHECK(vocal);
  CHECK(vocal.value().project.vocalTracks().front().regions.front().lyrics.front().surface == U"z-first");

  const auto percussionBytes = fileWithTrack({
      0U, 0x99U, 36U, 100U, 0x83U, 0x60U, 0x89U, 36U, 0U,
      0U, 0xffU, 0x2fU, 0U});
  application::ProjectFactory percussionFactory{818000U};
  const auto percussion = interchange::importSmfProject(percussionBytes, percussionFactory);
  CHECK(percussion);
  CHECK(percussion.value().project.vocalTracks().front().regions.front().notes.size() == 1U);
  CHECK(std::any_of(percussion.value().score.issues.begin(), percussion.value().score.issues.end(),
      [](const interchange::SmfIssue& issue) {
        return issue.severity == interchange::SmfIssueSeverity::Warning &&
            issue.message.find("MIDI channel 10 percussion") != std::string::npos;
      }));
  CHECK(std::any_of(percussion.value().score.issues.begin(), percussion.value().score.issues.end(),
      [](const interchange::SmfIssue& issue) {
        return issue.severity == interchange::SmfIssueSeverity::Warning &&
            issue.message.find("no matched lyric events") != std::string::npos;
      }));
}

TEST_CASE("SMF source track names are bounded when admitted as SEAM project tracks") {
  using namespace seam;
  std::vector<std::uint8_t> track{0U, 0xffU, 0x03U, 0x82U, 0x01U}; // 257 bytes.
  track.insert(track.end(), 257U, static_cast<std::uint8_t>('x'));
  track.insert(track.end(), {0U, 0x90U, 60U, 100U, 0x83U, 0x60U,
      0x80U, 60U, 0U, 0U, 0xffU, 0x2fU, 0U});
  const auto bytes = fileWithTrack(std::move(track));
  application::ProjectFactory factory{819000U};
  const auto imported = interchange::importSmfProject(bytes, factory); CHECK(imported);
  const auto& name = imported.value().project.vocalTracks().front().name;
  CHECK(name.size() <= 256U);
  CHECK(std::any_of(imported.value().score.issues.begin(), imported.value().score.issues.end(),
      [](const interchange::SmfIssue& issue) {
        return issue.severity == interchange::SmfIssueSeverity::Loss &&
            issue.message.find("name exceeded 256") != std::string::npos;
      }));
}

TEST_CASE("SMF project export omits invalid or over-budget SEAM track names with a loss") {
  using namespace seam;
  for (const auto& [name, textBudget] : {
           std::pair{std::string{"Lead\0Aux", 8U}, 1024U},
           std::pair{std::string{"A"}, 2U}}) {
    application::ProjectFactory factory{825000U};
    auto project = factory.createProject("Export name policy");
    const auto trackId = factory.addVocalTrack(project, name);
    const auto regionId = factory.addRegion(project, trackId, "Phrase",
        time::Tick{0}, time::Tick{960});
    auto* target = project.findRegion(regionId); CHECK(target);
    auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{480},
        60U, U"la");
    target->lyrics.push_back(std::move(lyric));
    target->notes.push_back(std::move(note));
    CHECK(project.validate());
    interchange::SmfLimits limits;
    limits.maximumTextBytes = textBudget;
    const auto exported = interchange::exportSmfProject(project, trackId,
        regionId, limits); CHECK(exported);
    CHECK(exported.value().tracks.size() == 1U);
    CHECK(exported.value().tracks.front().name.empty());
    CHECK(exported.value().issues.size() == 1U);
    CHECK(exported.value().issues.front().severity == interchange::SmfIssueSeverity::Loss);
    CHECK(exported.value().issues.front().message.find("track name") != std::string::npos);
    const auto bytes = interchange::encodeSmf(exported.value(), limits); CHECK(bytes);
    const auto decoded = interchange::decodeSmf(bytes.value(), limits); CHECK(decoded);
    CHECK(decoded.value().notes.size() == 1U);
  }
}

TEST_CASE("SMF selected-region export omits a name when its serialized event slot is unavailable") {
  using namespace seam;
  application::ProjectFactory factory{695000U};
  auto project = factory.createProject("Selected region event budget");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, trackId, "Phrase",
      time::Tick{0}, time::Tick{960});
  auto* region = project.findRegion(regionId);
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{240}, 60U, U"la");
  region->lyrics.push_back(std::move(lyric));
  region->notes.push_back(std::move(note));
  CHECK(project.validate());

  interchange::SmfLimits limits;
  limits.maximumSerializedEvents = 6U; // Required score fits; optional track name does not.
  const auto score = interchange::exportSmfProject(project, trackId, regionId,
      limits);
  CHECK(score);
  CHECK(score.value().tracks.size() == 1U);
  CHECK(score.value().tracks.front().name.empty());
  CHECK(score.value().notes.size() == 1U);
  CHECK(score.value().texts.size() == 1U);
  CHECK(score.value().issues.size() == 1U);
  CHECK(score.value().issues.front().message.find("track name") != std::string::npos);
  CHECK(interchange::encodeSmf(score.value(), limits));
}

TEST_CASE("SMF project export omits NUL-bearing lyric text without losing notes") {
  using namespace seam;
  application::ProjectFactory factory{826000U};
  auto project = factory.createProject("NUL lyric export");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, trackId, "Phrase",
      time::Tick{0}, time::Tick{960});
  auto* target = project.findRegion(regionId); CHECK(target);
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U,
      std::u32string{U"la\0oh", 5U});
  target->lyrics.push_back(std::move(lyric));
  target->notes.push_back(std::move(note));
  CHECK(project.validate());
  const auto exported = interchange::exportSmfProject(project, trackId, regionId);
  CHECK(exported);
  CHECK(exported.value().notes.size() == 1U);
  CHECK(exported.value().texts.empty());
  CHECK(exported.value().issues.size() == 1U);
  CHECK(exported.value().issues.front().severity == interchange::SmfIssueSeverity::Loss);
  CHECK(exported.value().issues.front().message.find("lyric surfaces contained NUL") != std::string::npos);
  const auto encoded = interchange::encodeSmf(exported.value()); CHECK(encoded);
  const auto decoded = interchange::decodeSmf(encoded.value()); CHECK(decoded);
  CHECK(decoded.value().notes.size() == 1U);
  CHECK(decoded.value().texts.empty());
}

TEST_CASE("SMF whole-project export preserves lyric surface and reports separate reading loss") {
  using namespace seam;
  application::ProjectFactory factory{826100U};
  auto project = factory.createProject("Reading hint export");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, trackId, "Phrase",
      time::Tick{0}, time::Tick{960});
  auto* target = project.findRegion(regionId); CHECK(target);
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{480}, 60U,
      U"今日", domain::Language::Japanese);
  lyric.readingHint = U"きょう";
  target->lyrics.push_back(std::move(lyric));
  target->notes.push_back(std::move(note));

  const auto exported = interchange::exportSmfProject(project); CHECK(exported);
  CHECK(exported.value().notes.size() == 1U);
  CHECK(exported.value().texts.size() == 1U);
  CHECK(exported.value().texts.front().text == "今日");
  CHECK(std::any_of(exported.value().issues.begin(), exported.value().issues.end(),
      [](const auto& issue) {
        return issue.severity == interchange::SmfIssueSeverity::Loss &&
            issue.message.find("SEAM-only track, region, note, or performance settings") !=
                std::string::npos;
      }));
}

TEST_CASE("SMF import combines loss families with source coordinate rounding warnings") {
  using namespace seam;
  interchange::SmfScore score;
  score.ppq = 7U;
  score.notes = {{time::Tick{1}, time::Tick{6}, 60U, 80U, 3U}};
  score.texts = {{time::Tick{1}, "sung", true}, {time::Tick{2}, "unused", true}};
  const auto bytes = interchange::encodeSmf(score); CHECK(bytes);
  application::ProjectFactory factory{820000U};
  const auto imported = interchange::importSmfProject(bytes.value(), factory); CHECK(imported);
  CHECK(imported.value().score.ppq == 7U);
  CHECK(imported.value().score.notes == score.notes);
  const auto& issues = imported.value().score.issues;
  CHECK(issues.size() == 5U);
  CHECK(std::count_if(issues.begin(), issues.end(), [](const auto& issue) {
    return issue.severity == interchange::SmfIssueSeverity::Warning;
  }) == 2);
  for (const auto& issue : issues) {
    CHECK(issue.tick == time::Tick{1} || issue.tick == time::Tick{2});
    CHECK(issue.message.find("7 PPQ") != std::string::npos);
  }
  const auto& note = imported.value().project.vocalTracks().front().regions.front().notes.front();
  CHECK(note.startTick == time::Tick{137});
  CHECK(note.durationTick == time::Tick{823});
}

TEST_CASE("SMF import refuses an overflowing combined diagnostic report before allocating IDs") {
  using namespace seam;
  const std::vector<std::uint8_t> track{
      0U, 0xb0U, 1U, 64U,
      1U, 0x91U, 60U, 80U,
      0U, 0xffU, 0x05U, 1U, 'a',
      0U, 0xffU, 0x05U, 1U, 'b',
      1U, 0xffU, 0x01U, 1U, 'x',
      1U, 0xffU, 0x05U, 1U, 'u',
      4U, 0x81U, 60U, 0U,
      0U, 0xffU, 0x2fU, 0U};
  const auto bytes = fileWithTrack(track, 7U);
  for (const std::size_t budget : {8U, 9U}) {
    interchange::SmfLimits limits;
    limits.maximumEvents = budget;
    const auto decoded = interchange::decodeSmf(bytes, limits); CHECK(decoded);
    CHECK(decoded.value().issues.size() == 1U);
    application::ProjectFactory factory{830000U};
    const auto next = factory.nextIdValue();
    const auto imported = interchange::importSmfProject(bytes, factory, {}, limits);
    if (budget == 8U) {
      CHECK(!imported);
      CHECK(imported.error().code == core::ErrorCode::Unsupported);
      CHECK(imported.error().message.find("diagnostic") != std::string::npos);
      CHECK(factory.nextIdValue() == next);
    } else {
      CHECK(imported);
      CHECK(imported.value().score.issues.size() == 9U);
    }
  }
}
