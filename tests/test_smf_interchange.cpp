#include "test_framework.hpp"
#include "seam/interchange/smf_codec.hpp"
#include "seam/interchange/smf_project_conversion.hpp"
#include "seam/application/project_factory.hpp"

#include <algorithm>
#include <cstdint>
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
}

TEST_CASE("SMF codec round-trips deterministic PPQ notes, tempo, meter and lyric text") {
  using namespace seam;
  interchange::SmfScore score;
  score.ppq = 480U;
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
  region.phonemeOverrides.push_back({.key = {region.notes.front().id, 0U}, .symbol = "eh0"});
  const auto exported = interchange::exportSmfProject(project,
      project.vocalTracks().front().id, region.id); CHECK(exported);
  CHECK(exported.value().notes.size() == 1U);
  CHECK(!exported.value().issues.empty());
  CHECK(exported.value().issues.front().severity == interchange::SmfIssueSeverity::Loss);
}
