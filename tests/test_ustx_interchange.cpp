#include "test_framework.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/interchange/ustx_codec.hpp"
#include "seam/interchange/ustx_project_conversion.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> bytes(std::string_view text) {
  return {text.begin(), text.end()};
}

const char* fixture() {
  return R"USTX(ustx_version: "0.9"
name: "Native USTX fixture"
comment: "bounded"
output_dir: Vocal
cache_dir: UCache
expressions: {}
exp_selectors: [dyn, pitd, clr, eng, vel, vol, atk, dec, gen, bre]
exp_primary: 0
exp_secondary: 1
key: 0
time_signatures:
  - {bar_position: 0, beat_per_bar: 4, beat_unit: 4}
  - {bar_position: 2, beat_per_bar: 3, beat_unit: 4}
tempos:
  - {position: 0, bpm: 120}
  - {position: 480, bpm: 150}
tracks:
  - singer: "fixture-singer"
    track_name: Lead
    track_color: Blue
    mute: false
    solo: false
    volume: -3
    pan: 0.25
    track_expressions: []
    voice_color_names: [""]
voice_parts:
  - name: Verse
    comment: ""
    track_no: 0
    position: 0
    duration: 960
    curves: []
    notes:
      - position: 0
        duration: 480
        tone: 60
        lyric: "あ"
        pitch:
          data:
            - {x: 0, y: 0, shape: l}
            - {x: 250, y: 5, shape: io}
          snap_first: true
        vibrato: {length: 60, period: 175, depth: 25, in: 10, out: 10, shift: 0, drift: 0, vol_link: 0}
        tuning: 0
        phoneme_expressions: []
        phoneme_overrides: []
      - position: 480
        duration: 480
        tone: 62
        lyric: "い"
        pitch:
          data:
            - {x: 0, y: 0, shape: l}
          snap_first: true
        vibrato: {length: 0, period: 175, depth: 25, in: 10, out: 10, shift: 0, drift: 0, vol_link: 0}
        tuning: 0
        phoneme_expressions: []
        phoneme_overrides: []
)USTX";
}

}  // namespace

TEST_CASE("native USTX decoder parses bounded flow and block YAML") {
  const auto decoded = seam::interchange::decodeUstx(bytes(fixture()));
  CHECK(decoded);
  CHECK(decoded.value().version == "0.9");
  CHECK(decoded.value().name == "Native USTX fixture");
  CHECK(decoded.value().tempos.size() == 2U);
  CHECK(decoded.value().meters.size() == 2U);
  CHECK(decoded.value().tracks.size() == 1U);
  CHECK(decoded.value().parts.size() == 1U);
  CHECK(decoded.value().parts.front().notes.size() == 2U);
  CHECK(decoded.value().parts.front().notes.front().pitch.size() == 2U);
  CHECK(decoded.value().parts.front().notes.front().hasVibrato);
  CHECK(!decoded.value().issues.empty()); // snap_first/phoneme details are explicit losses or warnings.
}

TEST_CASE("native USTX decoder rejects aliases, duplicate keys, documents and hostile depth") {
  using seam::interchange::decodeUstx;
  CHECK(!decodeUstx(bytes("ustx_version: '0.9'\nustx_version: '0.9'\n")));
  CHECK(!decodeUstx(bytes("ustx_version: '0.9'\na: &anchor 1\n")));
  CHECK(!decodeUstx(bytes("---\nustx_version: '0.9'\n")));
  CHECK(!decodeUstx(bytes("ustx_version: '0.9'\nname: [*missing]\n")));
  seam::interchange::UstxLimits limits;
  limits.maximumInputBytes = 8U;
  CHECK(!decodeUstx(bytes(fixture()), limits));
  limits = {};
  limits.maximumNodes = 8U;
  CHECK(!decodeUstx(bytes(fixture()), limits));
}

TEST_CASE("USTX import maps tempo meter pitch vibrato and track identity without source mutation") {
  seam::application::ProjectFactory factory{910000U};
  const auto imported = seam::interchange::importUstxProject(
      bytes(fixture()), factory,
      seam::interchange::UstxImportRequest{.projectName = "Imported USTX",
                                           .voicebankId = "fixture-bank",
                                           .voicebankVersion = "1.0.0",
                                           .voicebankContentHash = "",
                                           .characterId = "fixture-character",
                                           .characterVersion = "1.0.0",
                                           .language = seam::domain::Language::Japanese});
  CHECK(imported);
  CHECK(imported.value().project.name() == "Imported USTX");
  CHECK(imported.value().project.vocalTracks().size() == 1U);
  const auto& track = imported.value().project.vocalTracks().front();
  CHECK(track.voicebank.id == "fixture-bank");
  CHECK(track.pan == 0.25F);
  CHECK(track.regions.size() == 1U);
  const auto& region = track.regions.front();
  CHECK(region.startTick == seam::time::Tick{0});
  CHECK(region.durationTick == seam::time::Tick{1920});
  CHECK(region.notes.size() == 2U);
  CHECK(region.notes.front().startTick == seam::time::Tick{0});
  CHECK(region.notes.back().startTick == seam::time::Tick{960});
  CHECK(region.notes.front().vibrato.enabled);
  CHECK(!region.pitchAutomation.points().empty());
  CHECK(imported.value().project.tempoMap().bpmAt(seam::time::Tick{960}) == 150.0);
  CHECK(imported.value().project.meterMap().meterAt(seam::time::Tick{7680}).numerator == 3U);
}

TEST_CASE("USTX project export is deterministic and reports lossy fields") {
  seam::application::ProjectFactory factory{920000U};
  auto project = factory.createProject("Export USTX");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, trackId, "Verse", seam::time::Tick{0}, seam::time::Tick{1920});
  auto* region = project.findRegion(regionId);
  CHECK(region != nullptr);
  auto [lyric, note] = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{960}, 60U, U"あ");
  note.vibrato = {.enabled = true, .startFraction = 0.4F, .fadeInFraction = 0.1F,
                  .fadeOutFraction = 0.1F, .depthCents = 100.0F,
                  .periodMilliseconds = 175.0F, .phaseTurns = 0.25F};
  region->lyrics.push_back(lyric);
  region->notes.push_back(note);
  CHECK(region->pitchAutomation.upsert({seam::time::Tick{0}, 0.0F, seam::domain::CurveInterpolation::Linear}));
  CHECK(region->pitchAutomation.upsert({seam::time::Tick{480}, 50.0F, seam::domain::CurveInterpolation::Smooth}));
  const auto exported = seam::interchange::exportUstxProject(project);
  CHECK(exported);
  CHECK(!exported.value().bytes.empty());
  CHECK(!exported.value().issues.empty());
  const auto second = seam::interchange::exportUstxProject(project);
  CHECK(second);
  CHECK(exported.value().bytes == second.value().bytes);
  const auto reopened = seam::interchange::decodeUstx(exported.value().bytes);
  CHECK(reopened);
  CHECK(reopened.value().tracks.size() == 1U);
  CHECK(reopened.value().parts.front().notes.front().pitch.size() == 2U);
  CHECK(reopened.value().parts.front().notes.front().hasVibrato);
}
