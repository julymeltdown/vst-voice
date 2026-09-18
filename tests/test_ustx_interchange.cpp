#include "test_framework.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/interchange/ustx_codec.hpp"
#include "seam/interchange/ustx_project_conversion.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/synthesis/performance_compiler.hpp"

#include <algorithm>
#include <cmath>
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

std::string fixtureWithVibrato(std::string_view fields) {
  std::string result{fixture()};
  const auto begin = result.find("vibrato: {");
  const auto end = result.find('}', begin);
  result.replace(begin, end - begin + 1U, "vibrato: {" + std::string{fields} + "}");
  return result;
}

const char* negativePitchFixture() {
  // The note's position/duration/tone and +/-5 ms, zero-Y pitch pair follow
  // OpenUtau 83e02c7e, OpenUtau.Test/Core/USTx/UstxYamlTest.cs:15-19,30-39.
  // A nonzero part offset verifies that X stays relative to the note.
  return R"USTX(ustx_version: "0.9"
time_signatures: [{bar_position: 0, beat_per_bar: 4, beat_unit: 4}]
tempos: [{position: 0, bpm: 120}]
tracks: [{track_name: Lead}]
voice_parts:
  - track_no: 0
    position: 960
    duration: 480
    notes:
      - position: 120
        duration: 60
        tone: 42
        lyric: "あ"
        pitch:
          data: [{x: -5, y: 0, shape: io}, {x: 5, y: 0, shape: io}]
          snap_first: false
)USTX";
}

bool hasLossAt(const std::vector<seam::interchange::UstxIssue>& issues,
               std::string_view path) {
  return std::any_of(issues.begin(), issues.end(), [&](const auto& issue) {
    return issue.severity == seam::interchange::UstxIssueSeverity::Loss && issue.path == path;
  });
}

std::string linearPortamentoFixture() {
  return R"USTX(ustx_version: "0.9"
time_signatures: [{bar_position: 0, beat_per_bar: 4, beat_unit: 4}]
tempos: [{position: 0, bpm: 120}]
tracks: [{track_name: Lead}]
voice_parts:
  - track_no: 0
    position: 0
    duration: 960
    notes:
      - {position: 0, duration: 480, tone: 60, tuning: 25, lyric: "a"}
      - position: 480
        duration: 480
        tone: 64
        tuning: -25
        lyric: "i"
        pitch:
          data: [{x: -125, y: 99, shape: l}, {x: 125, y: 0, shape: l}]
          snap_first: true
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

// Independent wire-format oracle: OpenUtau 83e02c7e4a4d9ea5fca72806b2aa27c5382be015,
// OpenUtau.Core/Ustx/UNote.cs, UVibrato fields and Evaluate (lines 291-389).
// depth is cents, shift is a percentage of one period; unlike PitchPoint.Y,
// depth is NOT tenths of a semitone. Import and export are tested separately
// because reciprocal unit mistakes can pass a SEAM-only round trip.
TEST_CASE("USTX vibrato import preserves OpenUtau cents and cycle percentages") {
  const auto source = fixtureWithVibrato(
      "length: 60, period: 175, depth: 25, in: 10, out: 20, shift: 25, drift: 0, vol_link: 0");
  seam::application::ProjectFactory factory{930000U};
  const auto imported = seam::interchange::importUstxProject(bytes(source), factory);
  CHECK(imported);
  const auto& vibrato = imported.value().project.vocalTracks().front().regions.front().notes.front().vibrato;
  CHECK(vibrato.enabled);
  CHECK_NEAR(vibrato.startFraction, 0.4, 1e-6);
  CHECK_NEAR(vibrato.fadeInFraction, 0.1, 1e-6);
  CHECK_NEAR(vibrato.fadeOutFraction, 0.2, 1e-6);
  CHECK_NEAR(vibrato.depthCents, 25.0, 1e-6);
  CHECK_NEAR(vibrato.periodMilliseconds, 175.0, 1e-6);
  CHECK_NEAR(vibrato.phaseTurns, 0.25, 1e-6);
  CHECK(!hasLossAt(imported.value().issues, "ustx.voice_parts[0].notes[0].vibrato.depth"));
  CHECK(!hasLossAt(imported.value().issues, "ustx.voice_parts[0].notes[0].vibrato.shift"));

  const auto fullCycle = fixtureWithVibrato("length: 100, depth: 200, shift: 100");
  const auto importedCycle = seam::interchange::importUstxProject(bytes(fullCycle), factory);
  CHECK(importedCycle);
  const auto& cycle = importedCycle.value().project.vocalTracks().front().regions.front().notes.front().vibrato;
  CHECK_NEAR(cycle.depthCents, 200.0, 1e-6);
  CHECK_NEAR(cycle.phaseTurns, 0.0, 1e-6);
}

TEST_CASE("USTX vibrato export writes upstream units without requiring SEAM reimport") {
  seam::application::ProjectFactory factory{940000U};
  auto project = factory.createProject("Vibrato export units");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, trackId, "Verse", seam::time::Tick{0}, seam::time::Tick{960});
  auto* region = project.findRegion(regionId);
  CHECK(region != nullptr);
  auto [lyric, note] = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{960}, 60U, U"あ");
  note.vibrato = {.enabled = true, .startFraction = 0.5F, .fadeInFraction = 0.25F,
                  .fadeOutFraction = 0.125F, .depthCents = 75.0F,
                  .periodMilliseconds = 200.0F, .phaseTurns = 0.75F};
  region->lyrics.push_back(lyric);
  region->notes.push_back(note);
  const auto exported = seam::interchange::exportUstxProject(project);
  CHECK(exported);
  const auto wire = seam::interchange::decodeUstx(exported.value().bytes);
  CHECK(wire);
  const auto& vibrato = wire.value().parts.front().notes.front().vibrato;
  CHECK_NEAR(vibrato.length, 50.0, 1e-6);
  CHECK_NEAR(vibrato.period, 200.0, 1e-6);
  CHECK_NEAR(vibrato.depth, 75.0, 1e-6);
  CHECK_NEAR(vibrato.fadeIn, 25.0, 1e-6);
  CHECK_NEAR(vibrato.fadeOut, 12.5, 1e-6);
  CHECK_NEAR(vibrato.shift, 75.0, 1e-6);

  // OpenUtau clamps active depth to at least 5 cents. Preserve visibility of
  // that change when a valid SEAM vibrato is smaller than the upstream range.
  region->notes.front().vibrato.depthCents = 2.0F;
  const auto quietExport = seam::interchange::exportUstxProject(project);
  CHECK(quietExport);
  const auto quietWire = seam::interchange::decodeUstx(quietExport.value().bytes);
  CHECK(quietWire);
  CHECK_NEAR(quietWire.value().parts.front().notes.front().vibrato.depth, 5.0, 1e-6);
  CHECK(hasLossAt(quietExport.value().issues, "project.vocalTracks[0].regions[0].notes[0].vibrato.depth"));
}

TEST_CASE("USTX import reports vibrato changes and unsupported modulation") {
  const auto source = fixtureWithVibrato(
      "length: 60, period: 1000, depth: 1000, shift: -25, drift: 20, vol_link: 30");
  seam::application::ProjectFactory factory{950000U};
  const auto imported = seam::interchange::importUstxProject(bytes(source), factory);
  CHECK(imported);
  const auto& vibrato = imported.value().project.vocalTracks().front().regions.front().notes.front().vibrato;
  CHECK_NEAR(vibrato.depthCents, 200.0, 1e-6);
  CHECK_NEAR(vibrato.periodMilliseconds, 500.0, 1e-6);
  CHECK_NEAR(vibrato.phaseTurns, 0.0, 1e-6);
  for (const auto field : {"depth", "period", "shift", "drift", "vol_link"}) {
    CHECK(hasLossAt(imported.value().issues,
                    "ustx.voice_parts[0].notes[0].vibrato." + std::string{field}));
  }
}

TEST_CASE("USTX spline pitch is continuous and approximated shapes report loss") {
  // OpenUtau UNote.cs:446-465 names sp as Spline, never Step.
  // RenderPhrase.cs:331-348 uses CubicSplineSegment or a sine interpolation
  // fallback; SEAM's cubic smoothstep is only an explicitly lossy approximation.
  for (const auto shape : {"sp", "io", "i", "o"}) {
    std::string source{fixture()};
    const auto start = source.find("shape: l");
    source.replace(start, std::string{"shape: l"}.size(), "shape: " + std::string{shape});
    seam::application::ProjectFactory factory{960000U};
    const auto imported = seam::interchange::importUstxProject(bytes(source), factory);
    CHECK(imported);
    const auto& pitch = imported.value().project.vocalTracks().front().regions.front().pitchAutomation;
    CHECK(pitch.points().front().interpolation == seam::domain::CurveInterpolation::Smooth);
    CHECK_NEAR(pitch.valueAt(seam::time::Tick{240}), 25.0, 1e-6);
    CHECK(hasLossAt(imported.value().issues, "ustx.voice_parts[0].notes[0].pitch[0].shape"));
  }
}

TEST_CASE("USTX export never encodes a SEAM step as upstream spline") {
  seam::application::ProjectFactory factory{970000U};
  auto project = factory.createProject("Step export loss");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, trackId, "Verse", seam::time::Tick{0}, seam::time::Tick{960});
  auto* region = project.findRegion(regionId);
  CHECK(region != nullptr);
  auto [lyric, note] = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{960}, 60U, U"あ");
  region->lyrics.push_back(lyric);
  region->notes.push_back(note);
  CHECK(region->pitchAutomation.upsert({seam::time::Tick{0}, 0.0F, seam::domain::CurveInterpolation::Step}));
  CHECK(region->pitchAutomation.upsert({seam::time::Tick{480}, 100.0F, seam::domain::CurveInterpolation::Linear}));
  const auto exported = seam::interchange::exportUstxProject(project);
  CHECK(exported);
  const auto wire = seam::interchange::decodeUstx(exported.value().bytes);
  CHECK(wire);
  CHECK(wire.value().parts.front().notes.front().pitch.front().shape == "l");
  CHECK(hasLossAt(exported.value().issues, "project.vocalTracks[0].regions[0].notes[0].pitch[0].shape"));

  CHECK(region->pitchAutomation.upsert({seam::time::Tick{0}, 0.0F, seam::domain::CurveInterpolation::Linear}));
  const auto linearExport = seam::interchange::exportUstxProject(project);
  CHECK(linearExport);
  CHECK(!hasLossAt(linearExport.value().issues, "project.vocalTracks[0].regions[0].notes[0].pitch[0].shape"));

  CHECK(region->pitchAutomation.upsert({seam::time::Tick{0}, 0.0F, seam::domain::CurveInterpolation::Smooth}));
  const auto smoothExport = seam::interchange::exportUstxProject(project);
  CHECK(smoothExport);
  const auto smoothWire = seam::interchange::decodeUstx(smoothExport.value().bytes);
  CHECK(smoothWire);
  CHECK(smoothWire.value().parts.front().notes.front().pitch.front().shape == "io");
  CHECK(hasLossAt(smoothExport.value().issues, "project.vocalTracks[0].regions[0].notes[0].pitch[0].shape"));
}

TEST_CASE("USTX codec preserves the upstream negative pitch offset fixture with signed bounds") {
  const auto decoded = seam::interchange::decodeUstx(bytes(negativePitchFixture()));
  CHECK(decoded);
  const auto& pitch = decoded.value().parts.front().notes.front().pitch;
  CHECK(pitch.size() == 2U);
  CHECK_NEAR(pitch.front().offsetMilliseconds, -5.0, 1e-6);
  CHECK_NEAR(pitch.back().offsetMilliseconds, 5.0, 1e-6);
  const auto encoded = seam::interchange::encodeUstx(decoded.value());
  CHECK(encoded);
  const auto reopened = seam::interchange::decodeUstx(encoded.value());
  CHECK(reopened);
  CHECK(reopened.value().parts.front().notes.front().pitch == pitch);

  auto atBound = decoded.value();
  atBound.parts.front().notes.front().pitch.front().offsetMilliseconds = -86'400'000.0;
  CHECK(seam::interchange::encodeUstx(atBound));
  atBound.parts.front().notes.front().pitch.front().offsetMilliseconds = -86'400'001.0;
  CHECK(!seam::interchange::encodeUstx(atBound));
  std::string hostile{negativePitchFixture()};
  hostile.replace(hostile.find("x: -5"), 5U, "x: -86400001");
  CHECK(!seam::interchange::decodeUstx(bytes(hostile)));
}

TEST_CASE("USTX import retains negative pickup pitch inside the part before the first note") {
  std::string source{negativePitchFixture()};
  const std::string original{"data: [{x: -5, y: 0, shape: io}, {x: 5, y: 0, shape: io}]"};
  source.replace(source.find(original), original.size(),
                 "data: [{x: -5, y: -10, shape: l}, {x: 5, y: 10, shape: l}]");
  seam::application::ProjectFactory factory{980000U};
  const auto imported = seam::interchange::importUstxProject(bytes(source), factory);
  CHECK(imported);
  const auto& region = imported.value().project.vocalTracks().front().regions.front();
  CHECK(region.startTick == seam::time::Tick{1920});
  CHECK(region.notes.front().startTick == seam::time::Tick{240});
  const auto& pitch = region.pitchAutomation;
  CHECK(pitch.points().size() == 2U);
  // 120 BPM / 480 PPQ: +/-5 ms rounds to +/-5 source ticks. Convert to
  // SEAM's 960 PPQ only after adding the signed offset on the tempo axis.
  CHECK(pitch.points().front().tick == seam::time::Tick{230});
  CHECK(pitch.points().back().tick == seam::time::Tick{250});
  CHECK_NEAR(pitch.valueAt(seam::time::Tick{230}), -100.0, 1e-6);
  CHECK_NEAR(pitch.valueAt(seam::time::Tick{240}), 0.0, 1e-6);
  CHECK_NEAR(pitch.valueAt(seam::time::Tick{250}), 100.0, 1e-6);
  CHECK(!hasLossAt(imported.value().issues, "ustx.voice_parts[0].notes[0].pitch[0]"));

  // The pickup is also safe after an earlier note has ended. Deliberately put
  // the earlier note last in the file to exercise order-independent detection.
  source += "      - {position: 0, duration: 100, tone: 60, lyric: \"la\"}\n";
  const auto afterRest = seam::interchange::importUstxProject(bytes(source), factory);
  CHECK(afterRest);
  const auto& restPitch = afterRest.value().project.vocalTracks().front().regions.front().pitchAutomation;
  CHECK_NEAR(restPitch.valueAt(seam::time::Tick{100}), 0.0, 1e-6);
  CHECK_NEAR(restPitch.valueAt(seam::time::Tick{230}), -100.0, 1e-6);
  CHECK_NEAR(restPitch.valueAt(seam::time::Tick{240}), 0.0, 1e-6);
  CHECK(!hasLossAt(afterRest.value().issues, "ustx.voice_parts[0].notes[0].pitch[0]"));
}

TEST_CASE("USTX import reports negative pitch beyond the part instead of clamping it") {
  for (const auto partPosition : {"960", "0"}) {
    std::string source{negativePitchFixture()};
    source.replace(source.find("position: 960"), std::string{"position: 960"}.size(),
                   "position: " + std::string{partPosition});
    source.replace(source.find("position: 120"), std::string{"position: 120"}.size(), "position: 0");
    seam::application::ProjectFactory factory{990000U};
    const auto imported = seam::interchange::importUstxProject(bytes(source), factory);
    CHECK(imported);
    const auto& pitch = imported.value().project.vocalTracks().front().regions.front().pitchAutomation;
    CHECK(pitch.points().size() == 1U);
    CHECK(pitch.points().front().tick == seam::time::Tick{10});
    CHECK(hasLossAt(imported.value().issues, "ustx.voice_parts[0].notes[0].pitch[0]"));
  }
}

TEST_CASE("USTX import reports unsupported nonlinear cross-note portamento") {
  std::string source{negativePitchFixture()};
  source.replace(source.find("{x: 5, y: 0"), std::string{"{x: 5, y: 0"}.size(), "{x: 5, y: 10");
  const auto notes = source.find("    notes:\n") + std::string{"    notes:\n"}.size();
  source.insert(notes, "      - {position: 0, duration: 120, tone: 60, lyric: \"la\"}\n");
  seam::application::ProjectFactory factory{1000000U};
  const auto imported = seam::interchange::importUstxProject(bytes(source), factory);
  CHECK(imported);
  const auto& pitch = imported.value().project.vocalTracks().front().regions.front().pitchAutomation;
  CHECK(std::none_of(pitch.points().begin(), pitch.points().end(), [](const auto& point) {
    return point.tick == seam::time::Tick{230};
  }));
  CHECK(hasLossAt(imported.value().issues, "ustx.voice_parts[0].notes[1].pitch[0]"));
  CHECK(hasLossAt(imported.value().issues, "ustx.voice_parts[0].notes[1].pitch.composition"));
  CHECK(std::any_of(imported.value().issues.begin(), imported.value().issues.end(), [](const auto& issue) {
    return issue.path == "ustx.voice_parts[0].notes[1].pitch[0]" &&
           issue.message.find("cross-note") != std::string::npos;
  }));
}

TEST_CASE("USTX export writes rest pickups as negative milliseconds on the upcoming note") {
  seam::application::ProjectFactory factory{1010000U};
  auto project = factory.createProject("Rest pickup export");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, trackId, "Verse", seam::time::Tick{960}, seam::time::Tick{1440});
  auto* region = project.findRegion(regionId);
  CHECK(region != nullptr);
  auto [lyric, note] = factory.makeNote(seam::time::Tick{480}, seam::time::Tick{480}, 60U, U"あ");
  region->lyrics.push_back(lyric);
  region->notes.push_back(note);
  CHECK(region->pitchAutomation.upsert({seam::time::Tick{240}, -100.0F, seam::domain::CurveInterpolation::Linear}));
  CHECK(region->pitchAutomation.upsert({seam::time::Tick{720}, 100.0F, seam::domain::CurveInterpolation::Linear}));
  const auto exported = seam::interchange::exportUstxProject(project);
  CHECK(exported);
  const auto wire = seam::interchange::decodeUstx(exported.value().bytes);
  CHECK(wire);
  const auto& pickup = wire.value().parts.front().notes.front().pitch;
  CHECK(pickup.size() == 2U);
  // Independent expectation at 120 BPM: 240 / 960 beats = 125 ms.
  CHECK_NEAR(pickup.front().offsetMilliseconds, -125.0, 1e-6);
  CHECK_NEAR(pickup.front().y, -10.0, 1e-6);
  CHECK_NEAR(pickup.back().offsetMilliseconds, 125.0, 1e-6);

  // A preceding note must not steal the gap point, even with unsorted notes.
  auto [previousLyric, previousNote] = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{120}, 50U, U"い");
  region->lyrics.push_back(previousLyric);
  region->notes.push_back(previousNote);
  const auto afterRest = seam::interchange::exportUstxProject(project);
  CHECK(afterRest);
  const auto restWire = seam::interchange::decodeUstx(afterRest.value().bytes);
  CHECK(restWire);
  CHECK(restWire.value().parts.front().notes.front().pitch.size() == 2U);
  CHECK_NEAR(restWire.value().parts.front().notes.front().pitch.front().offsetMilliseconds, -125.0, 1e-6);
  CHECK(restWire.value().parts.front().notes.back().pitch.empty());
  const auto reopened = seam::interchange::importUstxProject(afterRest.value().bytes, factory);
  CHECK(reopened);
  const auto& reopenedPitch = reopened.value().project.vocalTracks().front().regions.front().pitchAutomation;
  CHECK_NEAR(reopenedPitch.valueAt(seam::time::Tick{240}), -100.0, 1e-6);
  CHECK_NEAR(reopenedPitch.valueAt(seam::time::Tick{480}), 0.0, 1e-6);

  CHECK(region->pitchAutomation.upsert({seam::time::Tick{1200}, 25.0F, seam::domain::CurveInterpolation::Linear}));
  const auto tailExport = seam::interchange::exportUstxProject(project);
  CHECK(tailExport);
  CHECK(hasLossAt(tailExport.value().issues, "project.vocalTracks[0].regions[0].pitch[2]"));

  auto [simultaneousLyric, simultaneousNote] = factory.makeNote(seam::time::Tick{480}, seam::time::Tick{480}, 64U, U"う");
  region->lyrics.push_back(simultaneousLyric);
  region->notes.push_back(simultaneousNote);
  const auto ambiguousExport = seam::interchange::exportUstxProject(project);
  CHECK(ambiguousExport);
  CHECK(hasLossAt(ambiguousExport.value().issues, "project.vocalTracks[0].regions[0].pitch[0]"));
  const auto ambiguousWire = seam::interchange::decodeUstx(ambiguousExport.value().bytes);
  CHECK(ambiguousWire);
  for (const auto& exportedNote : ambiguousWire.value().parts.front().notes) {
    CHECK(std::none_of(exportedNote.pitch.begin(), exportedNote.pitch.end(), [](const auto& point) {
      return point.offsetMilliseconds < 0.0;
    }));
  }
}

// Oracle: OpenUtau 83e02c7e UNote.cs:35-36,108-114 replaces snapped Y from
// adjusted neighboring tones. RenderPhrase.cs:304-307,339-352 adds each note
// curve's absolute pitch minus the appropriate previous/current adjusted base.
// These expected pitches are independent numeric samples, not SEAM round trips.
TEST_CASE("USTX linear cross-note portamento preserves adjusted absolute pitch and snap_first") {
  seam::application::ProjectFactory factory{1020000U};
  const auto imported = seam::interchange::importUstxProject(bytes(linearPortamentoFixture()), factory);
  CHECK(imported);
  const auto& region = imported.value().project.vocalTracks().front().regions.front();
  const auto& pitch = region.pitchAutomation;
  CHECK_NEAR(6000.0 + pitch.valueAt(seam::time::Tick{720}), 6025.0, 1e-4);
  CHECK_NEAR(6000.0 + pitch.valueAt(seam::time::Tick{840}), 6112.5, 1e-4);
  CHECK_NEAR(6000.0 + pitch.valueAt(seam::time::Tick{959}), 6199.270833333, 1e-3);
  CHECK_NEAR(6400.0 + pitch.valueAt(seam::time::Tick{960}), 6200.0, 1e-4);
  CHECK_NEAR(6400.0 + pitch.valueAt(seam::time::Tick{1080}), 6287.5, 1e-4);
  CHECK_NEAR(6400.0 + pitch.valueAt(seam::time::Tick{1200}), 6375.0, 1e-4);
  CHECK_NEAR(6400.0 + pitch.valueAt(seam::time::Tick{1500}), 6375.0, 1e-4);
  CHECK(!hasLossAt(imported.value().issues, "ustx.voice_parts[0].notes[1].pitch[0]"));
  CHECK(hasLossAt(imported.value().issues, "ustx.voice_parts[0].pitch.tick_grid"));

  const auto compiled = seam::synthesis::compileScorePerformance(imported.value().project, region, 48000U);
  CHECK(compiled);
  for (const auto& [tick, expected] : std::vector<std::pair<std::int64_t, double>>{
           {720, 6025.0}, {840, 6112.5}, {959, 6199.270833333},
           {960, 6200.0}, {1080, 6287.5}, {1200, 6375.0}}) {
    const auto frame = imported.value().project.tempoMap().sampleFrameAt(seam::time::Tick{tick}, 48000.0);
    const auto sample = compiled.value().inspectAt(frame);
    CHECK(sample.scoreFrequencyHz.has_value());
    CHECK_NEAR(6900.0 + 1200.0 * std::log2(*sample.scoreFrequencyHz / 440.0), expected, 1e-3);
  }
  const auto boundary = imported.value().project.tempoMap().sampleFrameAt(seam::time::Tick{960}, 48000.0);
  const auto lastPreviousFrame = compiled.value().inspectAt(boundary - 1);
  CHECK(lastPreviousFrame.noteId == region.notes.front().id);
  CHECK(lastPreviousFrame.scoreFrequencyHz.has_value());
  // Nearest-tick rounding must not apply the incoming note's -200c offset
  // while the previous note's MIDI60 base is still active on this frame.
  CHECK_NEAR(6900.0 + 1200.0 * std::log2(*lastPreviousFrame.scoreFrequencyHz / 440.0), 6199.270833333, 1e-3);

  auto unsnapped = linearPortamentoFixture();
  unsnapped.replace(unsnapped.find("y: 99"), std::string{"y: 99"}.size(), "y: -20");
  unsnapped.replace(unsnapped.find("snap_first: true"), std::string{"snap_first: true"}.size(), "snap_first: false");
  const auto manual = seam::interchange::importUstxProject(bytes(unsnapped), factory);
  CHECK(manual);
  const auto& manualPitch = manual.value().project.vocalTracks().front().regions.front().pitchAutomation;
  CHECK_NEAR(6000.0 + manualPitch.valueAt(seam::time::Tick{720}), 6175.0, 1e-4);
  CHECK_NEAR(6400.0 + manualPitch.valueAt(seam::time::Tick{960}), 6275.0, 1e-4);
}

TEST_CASE("USTX cross-note composition adds preceding pitch contributions instead of replacing them") {
  auto source = linearPortamentoFixture();
  const std::string oldNote{"      - {position: 0, duration: 480, tone: 60, tuning: 25, lyric: \"a\"}"};
  source.replace(source.find(oldNote), oldNote.size(),
      "      - position: 0\n        duration: 480\n        tone: 60\n        tuning: 25\n        lyric: \"a\"\n"
      "        pitch: {data: [{x: 0, y: 0, shape: l}, {x: 500, y: 4, shape: l}], snap_first: false}");
  seam::application::ProjectFactory factory{1030000U};
  const auto imported = seam::interchange::importUstxProject(bytes(source), factory);
  CHECK(imported);
  const auto& pitch = imported.value().project.vocalTracks().front().regions.front().pitchAutomation;
  // At source tick420, preceding +35c combines with incoming +87.5c on 6025c.
  CHECK_NEAR(6000.0 + pitch.valueAt(seam::time::Tick{840}), 6147.5, 1e-4);
  CHECK_NEAR(6000.0 + pitch.valueAt(seam::time::Tick{720}), 6055.0, 1e-4);
  // OpenUtau contribution intervals exclude their final endpoint: the first
  // note's +40c contribution ends as the second score note begins.
  CHECK_NEAR(6400.0 + pitch.valueAt(seam::time::Tick{960}), 6200.0, 1e-4);
}

TEST_CASE("USTX snap_first resets to the current adjusted tone after a gap") {
  auto source = linearPortamentoFixture();
  source.replace(source.find("duration: 480, tone: 60"), std::string{"duration: 480, tone: 60"}.size(), "duration: 120, tone: 60");
  source.replace(source.find("{x: 125, y: 0"), std::string{"{x: 125, y: 0"}.size(), "{x: 125, y: 10");
  seam::application::ProjectFactory factory{1040000U};
  const auto imported = seam::interchange::importUstxProject(bytes(source), factory);
  CHECK(imported);
  const auto& pitch = imported.value().project.vocalTracks().front().regions.front().pitchAutomation;
  CHECK_NEAR(6400.0 + pitch.valueAt(seam::time::Tick{960}), 6425.0, 1e-4);
  CHECK_NEAR(6400.0 + pitch.valueAt(seam::time::Tick{1200}), 6475.0, 1e-4);
  CHECK_NEAR(6000.0 + pitch.valueAt(seam::time::Tick{120}), 6025.0, 1e-4);

  const std::string firstNote{"      - {position: 0, duration: 120, tone: 60, tuning: 25, lyric: \"a\"}\n"};
  source.erase(source.find(firstNote), firstNote.size());
  const auto first = seam::interchange::importUstxProject(bytes(source), factory);
  CHECK(first);
  CHECK_NEAR(6400.0 + first.value().project.vocalTracks().front().regions.front().pitchAutomation.valueAt(seam::time::Tick{960}), 6425.0, 1e-4);
}

TEST_CASE("USTX portamento millisecond endpoints follow the full tempo axis before interpolation") {
  auto source = linearPortamentoFixture();
  source.replace(source.find("tempos: [{position: 0, bpm: 120}]"), std::string{"tempos: [{position: 0, bpm: 120}]"}.size(),
                 "tempos: [{position: 0, bpm: 120}, {position: 480, bpm: 240}]");
  source.replace(source.find("tuning: 25"), std::string{"tuning: 25"}.size(), "tuning: 0");
  source.replace(source.find("tuning: -25"), std::string{"tuning: -25"}.size(), "tuning: 0");
  seam::application::ProjectFactory factory{1050000U};
  const auto imported = seam::interchange::importUstxProject(bytes(source), factory);
  CHECK(imported);
  const auto& pitch = imported.value().project.vocalTracks().front().regions.front().pitchAutomation;
  // -125 ms maps to source tick360, +125 ms to720. OpenUtau interpolates
  // between those converted tick positions, not linearly in wall-clock time.
  CHECK_NEAR(6000.0 + pitch.valueAt(seam::time::Tick{840}), 6066.6666667, 1e-3);
  CHECK_NEAR(6400.0 + pitch.valueAt(seam::time::Tick{960}), 6133.3333333, 1e-3);
  CHECK_NEAR(6400.0 + pitch.valueAt(seam::time::Tick{1200}), 6266.6666667, 1e-3);
  CHECK_NEAR(6400.0 + pitch.valueAt(seam::time::Tick{1440}), 6400.0, 1e-3);
  const auto& region = imported.value().project.vocalTracks().front().regions.front();
  const auto compiled = seam::synthesis::compileScorePerformance(imported.value().project, region, 48000U);
  CHECK(compiled);
  for (const auto& [tick, expected] : std::vector<std::pair<std::int64_t, double>>{
           {840, 6066.6666667}, {960, 6133.3333333}, {1200, 6266.6666667}, {1440, 6400.0}}) {
    const auto sample = compiled.value().inspectAt(imported.value().project.tempoMap().sampleFrameAt(seam::time::Tick{tick}, 48000.0));
    CHECK(sample.scoreFrequencyHz.has_value());
    CHECK_NEAR(6900.0 + 1200.0 * std::log2(*sample.scoreFrequencyHz / 440.0), expected, 1e-3);
  }
}

TEST_CASE("USTX pitch composition keeps explicit polyphonic losses and bounded sparse output") {
  auto overlapping = linearPortamentoFixture();
  overlapping.replace(overlapping.find("duration: 480, tone: 60"), std::string{"duration: 480, tone: 60"}.size(), "duration: 600, tone: 60");
  seam::application::ProjectFactory factory{1060000U};
  const auto polyphonic = seam::interchange::importUstxProject(bytes(overlapping), factory);
  CHECK(polyphonic);
  CHECK(hasLossAt(polyphonic.value().issues, "ustx.voice_parts[0].pitch.composition"));
  CHECK(hasLossAt(polyphonic.value().issues, "ustx.voice_parts[0].notes[1].pitch[0]"));

  seam::interchange::UstxLimits limits;
  limits.maximumCurvePoints = 4U;
  CHECK(!seam::interchange::importUstxProject(bytes(linearPortamentoFixture()), factory, {}, limits));
  auto longScore = seam::interchange::decodeUstx(bytes(linearPortamentoFixture()));
  CHECK(longScore);
  auto& part = longScore.value().parts.front();
  part.duration = seam::time::Tick{960'000'000};
  part.notes.front().duration = seam::time::Tick{480'000'000};
  part.notes.back().position = seam::time::Tick{480'000'000};
  part.notes.back().duration = seam::time::Tick{480'000'000};
  const auto encoded = seam::interchange::encodeUstx(longScore.value());
  CHECK(encoded);
  const auto sparse = seam::interchange::importUstxProject(encoded.value(), factory);
  CHECK(sparse);
  CHECK(sparse.value().project.vocalTracks().front().regions.front().pitchAutomation.points().size() < 16U);
}

TEST_CASE("USTX pitch endpoint half-ticks follow OpenUtau ties-to-even rounding") {
  // TimeAxis.cs:222-230 uses Math.Round; absolute tick1080.5 becomes1080.
  std::string source{negativePitchFixture()};
  source.replace(source.find("position: 120"), std::string{"position: 120"}.size(), "position: 121");
  source.replace(source.find("x: -5"), std::string{"x: -5"}.size(), "x: -0.5208333333333334");
  seam::application::ProjectFactory factory{1070000U};
  const auto imported = seam::interchange::importUstxProject(bytes(source), factory);
  CHECK(imported);
  CHECK(imported.value().project.vocalTracks().front().regions.front().pitchAutomation.points().front().tick == seam::time::Tick{240});
}

TEST_CASE("pitch boundary ownership preserves ordinary controls rests and melisma transitions") {
  seam::application::ProjectFactory factory{1080000U};
  auto project = factory.createProject("Pitch boundary sampling");
  const auto trackId = factory.addVocalTrack(project, "Lead");
  const auto regionId = factory.addRegion(project, trackId, "Verse", seam::time::Tick{960}, seam::time::Tick{2880});
  auto* region = project.findRegion(regionId);
  CHECK(region != nullptr);
  for (const auto key : {60U, 64U}) {
    auto [lyric, note] = factory.makeNote(seam::time::Tick{key == 60U ? 0 : 960}, seam::time::Tick{960},
                                        static_cast<std::uint8_t>(key), U"あ", seam::domain::Language::Japanese);
    region->lyrics.push_back(lyric);
    region->notes.push_back(note);
  }
  CHECK(region->pitchAutomation.upsert({seam::time::Tick{0}, 50.0F, seam::domain::CurveInterpolation::Step}));
  CHECK(region->pitchAutomation.upsert({seam::time::Tick{960}, -50.0F, seam::domain::CurveInterpolation::Step}));
  CHECK(region->dynamicsAutomation.replacePoints({{seam::time::Tick{0}, 0.2F}, {seam::time::Tick{960}, 0.8F}}));
  for (const auto rate : {8000U, 44100U, 48000U, 192000U}) {
    const auto compiled = seam::synthesis::compileScorePerformance(project, *region, rate);
    CHECK(compiled);
    const auto& first = compiled.value().notes().front();
    const auto& second = compiled.value().notes().back();
    const auto last = compiled.value().inspectAt(first.endFrame - 1);
    const auto next = compiled.value().inspectAt(second.startFrame);
    CHECK(last.noteId == first.id);
    CHECK(next.noteId == second.id);
    CHECK(last.scoreFrequencyHz.has_value());
    CHECK(next.scoreFrequencyHz.has_value());
    CHECK_NEAR(6900.0 + 1200.0 * std::log2(*last.scoreFrequencyHz / 440.0), 6050.0, 1e-4);
    CHECK_NEAR(6900.0 + 1200.0 * std::log2(*next.scoreFrequencyHz / 440.0), 6350.0, 1e-4);
    // Dynamics is still sampled on its existing rounded region tick.
    CHECK_NEAR(last.dynamicsGain, 0.8, 1e-6);
    CHECK(!compiled.value().inspectAt(first.startFrame - 1).scoreFrequencyHz);
    CHECK(!compiled.value().inspectAt(second.endFrame).scoreFrequencyHz);
  }

  region->lyrics.back().surface = U"ー";
  region->notes.back().articulation = seam::domain::NoteArticulation::Legato;
  const auto pronunciation = seam::phonemizer::resolveJapanesePronunciation(*region);
  CHECK(pronunciation);
  const auto linked = seam::synthesis::compileScorePerformance(project, *region, 48000U,
      pronunciation.value().pronunciation.tokens);
  CHECK(linked);
  const auto& continuation = linked.value().notes().back();
  CHECK(!continuation.reattack);
  CHECK(continuation.transitionFromMidi == 60U);
  const auto onset = linked.value().inspectAt(continuation.startFrame);
  const auto settled = linked.value().inspectAt(continuation.transitionEndFrame);
  CHECK(onset.scoreFrequencyHz.has_value());
  CHECK(settled.scoreFrequencyHz.has_value());
  // Ordinary authored offsets still combine with the existing automatic
  // continuation glide; this boundary fix does not disable or restart it.
  CHECK_NEAR(6900.0 + 1200.0 * std::log2(*onset.scoreFrequencyHz / 440.0), 5950.0, 1e-4);
  CHECK_NEAR(6900.0 + 1200.0 * std::log2(*settled.scoreFrequencyHz / 440.0), 6350.0, 1e-4);
}
