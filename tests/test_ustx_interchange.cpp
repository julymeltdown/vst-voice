#include "test_framework.hpp"
#include "seam/core/finite_decimal.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/interchange/ustx_codec.hpp"
#include "seam/interchange/ustx_project_conversion.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/synthesis/performance_compiler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <locale>
#include <numbers>
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

// Independent oracle translation of OpenUtau 83e02c7e MusicMath.cs:123-150.
// Keep this separate from the importer's subdivision and composition helpers.
double upstreamSineWeight(std::string_view shape, double t) {
  if (shape == "i") return 1.0 - std::cos(t * std::numbers::pi / 2.0);
  if (shape == "o") return std::sin(t * std::numbers::pi / 2.0);
  return (1.0 - std::cos(t * std::numbers::pi)) / 2.0;
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

TEST_CASE("USTX floating scalars preserve decimal grammar and range rejection") {
  const auto withPan = [](std::string_view value) {
    std::string source{fixture()};
    source.replace(source.find("pan: 0.25"), std::string_view{"pan: 0.25"}.size(),
                   "pan: " + std::string{value});
    return seam::interchange::decodeUstx(bytes(source));
  };
  for (const auto value : {"0.25", ".25", "2.5e-1", "2.5E-1", "-0.25", "0.", "-0.0", "0e-9999"}) {
    CHECK(withPan(value));
  }
  for (const auto value : {"+0.25", "1e9999", "1e-9999", "0.25junk", "1e", "--1", "0x1p-2", "nan", "NAN", "NaN(payload)", "inf", "INFINITY", ".inf"}) {
    CHECK(!withPan(value));
  }
}

TEST_CASE("portable finite decimal parser bounds float and double without mutating rejected output") {
  const auto exercise = []<typename Floating>() {
    for (const auto token : {"", " ", " 1", "1 ", "+1", "nan", "inf", "0x1p0", "1e9999", "1e-9999", "1.2.3", "1e", "--1"}) {
      Floating value{7};
      CHECK(!seam::core::parseFiniteDecimal(token, value));
      CHECK(value == Floating{7});
    }
    Floating value{};
    CHECK(seam::core::parseFiniteDecimal("2.5e-1", value));
    CHECK(value == Floating{0.25});
    CHECK(seam::core::parseFiniteDecimal("-0.0", value));
    CHECK(std::signbit(value));
  };
  exercise.operator()<float>();
  exercise.operator()<double>();
  float small{};
  double large{};
  CHECK(!seam::core::parseFiniteDecimal("1e40", small));
  CHECK(seam::core::parseFiniteDecimal("1e40", large));
}

TEST_CASE("USTX decimal decoding is independent of the global numeric locale") {
  struct CommaDecimal final : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
  };
  struct RestoreLocale final {
    std::locale previous{std::locale()};
    ~RestoreLocale() { std::locale::global(previous); }
  } restore;
  std::locale::global(std::locale{std::locale::classic(), new CommaDecimal});
  const auto decoded = seam::interchange::decodeUstx(bytes(fixture()));
  CHECK(decoded);
  CHECK(decoded.value().tracks.front().pan == 0.25);
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

TEST_CASE("USTX unsupported spline pitch retains its explicit smooth approximation") {
  // OpenUtau UNote.cs:446-465 names sp as Spline, never Step.
  // RenderPhrase.cs:331-348 uses CubicSplineSegment or a sine interpolation
  // fallback; SEAM's cubic smoothstep is only an explicitly lossy approximation.
  for (const auto shape : {"sp"}) {
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

TEST_CASE("USTX import reports unsupported spline cross-note portamento") {
  std::string source{negativePitchFixture()};
  source.replace(source.find("{x: 5, y: 0"), std::string{"{x: 5, y: 0"}.size(), "{x: 5, y: 10");
  source.replace(source.find("shape: io"), std::string{"shape: io"}.size(), "shape: sp");
  const auto notes = source.find("    notes:\n") + std::string{"    notes:\n"}.size();
  source.insert(notes, "      - {position: 0, duration: 120, tone: 60, lyric: \"la\"}\n");
  seam::application::ProjectFactory factory{1000000U};
  const auto imported = seam::interchange::importUstxProject(bytes(source), factory);
  CHECK(imported);
  CHECK(imported.value().project.vocalTracks().front().regions.front().performance.ownership.empty());
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

TEST_CASE("USTX spline rejection considers neighbors even when segment endpoints are equal") {
  auto source = linearPortamentoFixture();
  const auto points = std::string{"[{x: -125, y: 99, shape: l}, {x: 125, y: 0, shape: l}]"};
  source.replace(source.find(points), points.size(),
      "[{x: -125, y: 99, shape: sp}, {x: 0, y: -35, shape: l}, {x: 125, y: 0, shape: l}]");
  // The snapped first and middle pitches both equal6025c, but the third is
  //6375c. Pinned SplineInterpolate.cs:14-21 yields m0=0,m1=175 and a midpoint
  //6003.125c: equal segment endpoints do NOT imply a flat Catmull-Rom spline.
  seam::application::ProjectFactory factory{1140000U};
  const auto imported = seam::interchange::importUstxProject(bytes(source), factory);
  CHECK(imported);
  CHECK(imported.value().project.vocalTracks().front().regions.front().performance.ownership.empty());
  CHECK(hasLossAt(imported.value().issues, "ustx.voice_parts[0].notes[1].pitch.composition"));
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

TEST_CASE("USTX sine portamento matches independent OpenUtau quarter-point pitches") {
  // OpenUtau 83e02c7e MusicMath.cs:123-150. These constants are the
  // quarter/mid/three-quarter values of its sine easing functions, not values
  // obtained from the imported SEAM curve or a SEAM export/reimport cycle.
  struct Oracle final { const char* shape; double quarter; double middle; double threeQuarter; };
  for (const auto& oracle : std::vector<Oracle>{{"io", 0.1464466094067262, 0.5, 0.8535533905932737},
           {"i", 0.0761204674887133, 0.2928932188134524, 0.6173165676349102},
           {"o", 0.3826834323650898, 0.7071067811865475, 0.9238795325112867}}) {
    auto source = linearPortamentoFixture();
    source.replace(source.find("shape: l"), std::string{"shape: l"}.size(), "shape: " + std::string{oracle.shape});
    source.replace(source.find("lyric: \"a\""), std::string{"lyric: \"a\""}.size(), "lyric: \"あ\"");
    source.replace(source.find("lyric: \"i\""), std::string{"lyric: \"i\""}.size(), "lyric: \"ー\"");
    seam::application::ProjectFactory factory{1100000U};
    const auto imported = seam::interchange::importUstxProject(bytes(source), factory);
    CHECK(imported);
    const auto& project = imported.value().project;
    const auto& region = project.vocalTracks().front().regions.front();
    CHECK(region.performance.ownership.size() == 1U);
    CHECK(hasLossAt(imported.value().issues, "ustx.voice_parts[0].pitch.approximation"));
    CHECK(std::any_of(imported.value().issues.begin(), imported.value().issues.end(), [](const auto& issue) {
      return issue.path == "ustx.voice_parts[0].pitch.approximation" &&
          issue.message.find("0.25-cent") != std::string::npos &&
          issue.message.find("960 PPQ") != std::string::npos;
    }));
    CHECK(!hasLossAt(imported.value().issues, "ustx.voice_parts[0].notes[1].pitch[0]"));
    const auto pronunciation = seam::phonemizer::resolveJapanesePronunciation(region);
    CHECK(pronunciation);
    const auto compiled = seam::synthesis::compileScorePerformance(project, region, 48000U,
        pronunciation.value().pronunciation.tokens);
    CHECK(compiled);
    for (const auto& [tick, factor] : std::vector<std::pair<std::int64_t, double>>{
             {840, oracle.quarter}, {960, oracle.middle}, {1080, oracle.threeQuarter}}) {
      const auto sample = compiled.value().inspectAt(project.tempoMap().sampleFrameAt(seam::time::Tick{tick}, 48000.0));
      CHECK(sample.scoreFrequencyHz.has_value());
      CHECK_NEAR(6900.0 + 1200.0 * std::log2(*sample.scoreFrequencyHz / 440.0), 6025.0 + 350.0 * factor, 0.251);
    }
  }
}

TEST_CASE("USTX sine composition stays bounded across tempo and continuation sample boundaries") {
  for (const auto shape : {"io", "i", "o"}) {
    for (const auto varyingTempo : {false, true}) {
      auto source = linearPortamentoFixture();
      source.replace(source.find("shape: l"), std::string{"shape: l"}.size(), "shape: " + std::string{shape});
      source.replace(source.find("lyric: \"a\""), std::string{"lyric: \"a\""}.size(), "lyric: \"あ\"");
      source.replace(source.find("lyric: \"i\""), std::string{"lyric: \"i\""}.size(), "lyric: \"ー\"");
      if (varyingTempo)
        source.replace(source.find("tempos: [{position: 0, bpm: 120}]"), std::string{"tempos: [{position: 0, bpm: 120}]"}.size(),
                       "tempos: [{position: 0, bpm: 120}, {position: 480, bpm: 240}]");
      seam::application::ProjectFactory factory{1110000U};
      const auto imported = seam::interchange::importUstxProject(bytes(source), factory);
      CHECK(imported);
      const auto& project = imported.value().project;
      const auto& region = project.vocalTracks().front().regions.front();
      const auto pronunciation = seam::phonemizer::resolveJapanesePronunciation(region);
      CHECK(pronunciation);
      // Endpoints are converted through the tempo axis FIRST: -125ms is
      // canonical720, +125ms is1200 at120bpm, or1440 after the240bpm change.
      const std::int64_t end = varyingTempo ? 1440 : 1200;
      const auto expected = [&](std::int64_t tick) {
        return 6025.0 + 350.0 * upstreamSineWeight(shape, static_cast<double>(tick - 720) / static_cast<double>(end - 720));
      };
      for (const auto rate : {8000U, 44100U, 48000U, 192000U}) {
        const auto compiled = seam::synthesis::compileScorePerformance(project, region, rate,
            pronunciation.value().pronunciation.tokens);
        CHECK(compiled);
        CHECK(!compiled.value().notes().back().reattack);
        for (std::int64_t tick = 720; tick <= end; ++tick) {
          const auto sample = compiled.value().at(project.tempoMap().sampleFrameAt(seam::time::Tick{tick}, rate));
          CHECK(sample.scoreFrequencyHz.has_value());
          CHECK_NEAR(6900.0 + 1200.0 * std::log2(*sample.scoreFrequencyHz / 440.0), expected(tick), 0.251);
        }
        const auto last = compiled.value().at(compiled.value().notes().back().startFrame - 1);
        CHECK(last.scoreFrequencyHz.has_value());
        CHECK_NEAR(6900.0 + 1200.0 * std::log2(*last.scoreFrequencyHz / 440.0), expected(959), 0.251);
      }
    }
  }
}

TEST_CASE("USTX simultaneous nonlinear pickups share the total interpolation error budget") {
  for (const auto shape : {"io", "i", "o"}) {
    auto document = seam::interchange::decodeUstx(bytes(linearPortamentoFixture()));
    CHECK(document);
    auto& part = document.value().parts.front();
    part.notes.clear();
    part.duration = seam::time::Tick{3840};
    // Eight nonoverlapping score notes, but eight overlapping pitch pickups.
    // Their identical +100c ramps add; a per-curve0.25c budget would permit
    // eight times the promised error and fails this independent dense oracle.
    for (std::int64_t index = 0; index < 8; ++index) {
      const double offset = -500.0 * static_cast<double>(index);
      seam::interchange::UstxNote note;
      note.position = seam::time::Tick{index * 480};
      note.lyric = "あ";
      note.snapFirst = false;
      note.pitch = {{offset, 0.0, shape}, {250.0 + offset, 10.0, "l"}};
      part.notes.push_back(std::move(note));
    }
    const auto source = seam::interchange::encodeUstx(document.value());
    CHECK(source);
    seam::application::ProjectFactory factory{1120000U};
    const auto imported = seam::interchange::importUstxProject(source.value(), factory);
    CHECK(imported);
    const auto& project = imported.value().project;
    const auto& region = project.vocalTracks().front().regions.front();
    CHECK(region.performance.ownership.size() == 1U);
    const auto compiled = seam::synthesis::compileScorePerformance(project, region, 48000U);
    CHECK(compiled);
    for (std::int64_t tick = 0; tick <= 480; ++tick) {
      const auto expected = 6000.0 + 800.0 * upstreamSineWeight(shape, static_cast<double>(tick) / 480.0);
      const auto sample = compiled.value().at(project.tempoMap().sampleFrameAt(seam::time::Tick{tick}, 48000.0));
      CHECK(sample.scoreFrequencyHz.has_value());
      CHECK_NEAR(6900.0 + 1200.0 * std::log2(*sample.scoreFrequencyHz / 440.0), expected, 0.251);
    }
    CHECK_NEAR(region.pitchAutomation.valueAt(seam::time::Tick{959}), 800.0, 0.001);
    CHECK_NEAR(region.pitchAutomation.valueAt(seam::time::Tick{960}), 700.0, 0.001);
  }
}

TEST_CASE("USTX steep sine segments preserve every representable tick without claiming sub-tick exactness") {
  for (const auto shape : {"io", "i", "o"}) {
    auto source = linearPortamentoFixture();
    source.replace(source.find("shape: l"), std::string{"shape: l"}.size(), "shape: " + std::string{shape});
    source.replace(source.find("x: -125"), std::string{"x: -125"}.size(), "x: -1");
    source.replace(source.find("x: 125"), std::string{"x: 125"}.size(), "x: 1");
    seam::application::ProjectFactory factory{1150000U};
    const auto imported = seam::interchange::importUstxProject(bytes(source), factory);
    CHECK(imported);
    const auto& pitch = imported.value().project.vocalTracks().front().regions.front().pitchAutomation;
    for (std::int64_t tick = 958; tick <= 962; ++tick) {
      const auto expected = 6025.0 + 350.0 * upstreamSineWeight(shape, static_cast<double>(tick - 958) / 4.0);
      CHECK_NEAR((tick < 960 ? 6000.0 : 6400.0) + pitch.valueAt(seam::time::Tick{tick}), expected, 0.001);
    }
    CHECK(hasLossAt(imported.value().issues, "ustx.voice_parts[0].pitch.approximation"));
    CHECK(hasLossAt(imported.value().issues, "ustx.voice_parts[0].pitch.tick_grid"));
  }
}

TEST_CASE("USTX nonlinear linearization rejects point exhaustion without expanding a long song") {
  auto source = linearPortamentoFixture();
  source.replace(source.find("shape: l"), std::string{"shape: l"}.size(), "shape: io");
  seam::application::ProjectFactory factory{1130000U};
  seam::interchange::UstxLimits limits;
  limits.maximumCurvePoints = 16U;
  const auto exhausted = seam::interchange::importUstxProject(bytes(source), factory, {}, limits);
  CHECK(!exhausted);
  CHECK(exhausted.error().message.find("point/work budget") != std::string::npos);
  auto document = seam::interchange::decodeUstx(bytes(source));
  CHECK(document);
  auto& part = document.value().parts.front();
  part.duration = seam::time::Tick{960'000'000};
  part.notes.front().duration = seam::time::Tick{480'000'000};
  part.notes.back().position = seam::time::Tick{480'000'000};
  part.notes.back().duration = seam::time::Tick{480'000'000};
  const auto encoded = seam::interchange::encodeUstx(document.value());
  CHECK(encoded);
  const auto sparse = seam::interchange::importUstxProject(encoded.value(), factory);
  CHECK(sparse);
  CHECK(sparse.value().project.vocalTracks().front().regions.front().pitchAutomation.points().size() < 128U);
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
  CHECK(polyphonic.value().project.vocalTracks().front().regions.front().performance.ownership.empty());
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

TEST_CASE("USTX authored continuation pitch is not combined with an automatic melisma glide") {
  auto source = linearPortamentoFixture();
  source.replace(source.find("lyric: \"a\""), std::string{"lyric: \"a\""}.size(), "lyric: \"あ\"");
  source.replace(source.find("lyric: \"i\""), std::string{"lyric: \"i\""}.size(), "lyric: \"ー\"");
  seam::application::ProjectFactory factory{1090000U};
  const auto imported = seam::interchange::importUstxProject(bytes(source), factory);
  CHECK(imported);
  const auto& project = imported.value().project;
  const auto& region = project.vocalTracks().front().regions.front();
  CHECK(region.performance.ownership.size() == 1U);
  const auto& ownership = region.performance.ownership.front();
  CHECK(ownership.channel == seam::domain::PerformanceChannel::Pitch);
  CHECK(ownership.mode == seam::domain::ManualPerformanceMode::Replace);
  CHECK((std::get<seam::domain::PerformanceTimeRange>(ownership.scope) ==
      seam::domain::PerformanceTimeRange{seam::time::Tick{0}, region.durationTick}));
  const seam::formats::ProjectJsonCodec codec;
  const auto saved = codec.encode(project);
  CHECK(saved);
  const auto reopened = codec.decode(saved.value());
  CHECK(reopened);
  CHECK(reopened.value().vocalTracks().front().regions.front().performance == region.performance);
  // OpenUtau 83e02c7e: UNote.cs:108-114 snaps the first point to 6025c;
  // RenderPhrase.cs:301-352 composes its linear ramp to 6375c over source
  // ticks360..600. There is no additional automatic glide at note onset.
  for (const auto* candidate : {&project, &reopened.value()}) {
    const auto& candidateRegion = candidate->vocalTracks().front().regions.front();
    const auto pronunciation = seam::phonemizer::resolveJapanesePronunciation(candidateRegion);
    CHECK(pronunciation);
    for (const auto rate : {8000U, 44100U, 48000U, 192000U}) {
      const auto compiled = seam::synthesis::compileScorePerformance(*candidate, candidateRegion, rate,
          pronunciation.value().pronunciation.tokens);
      CHECK(compiled);
      const auto& continuation = compiled.value().notes().back();
      CHECK(!continuation.reattack);
      CHECK(continuation.transitionFromMidi == 60U);
      for (const auto& [tick, expected] : std::vector<std::pair<std::int64_t, double>>{
               {960, 6200.0}, {984, 6217.5}, {1008, 6235.0}, {1200, 6375.0}}) {
        const auto frame = candidate->tempoMap().sampleFrameAt(seam::time::Tick{tick}, rate);
        const auto sample = compiled.value().inspectAt(frame);
        CHECK(sample.scoreFrequencyHz.has_value());
        CHECK(compiled.value().at(frame).scoreFrequencyHz == sample.scoreFrequencyHz);
        CHECK_NEAR(6900.0 + 1200.0 * std::log2(*sample.scoreFrequencyHz / 440.0), expected, 1e-3);
        CHECK(!sample.reattack);
      }
    }
  }
  const auto exported = seam::interchange::exportUstxProject(project);
  CHECK(exported);
  CHECK(hasLossAt(exported.value().issues, "project.vocalTracks.regions[0]"));
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

  // Explicit additive ownership retains native glide. Only Replace owns the
  // absolute score-base transition, for its exact note or time-range scope.
  region->performance.ownership = {{seam::domain::PerformanceChannel::Pitch,
      region->notes.back().id, seam::domain::ManualPerformanceMode::PitchOffset, {}}};
  const auto additive = seam::synthesis::compileScorePerformance(project, *region, 48000U,
      pronunciation.value().pronunciation.tokens);
  CHECK(additive);
  CHECK_NEAR(*additive.value().inspectAt(continuation.startFrame).scoreFrequencyHz, *onset.scoreFrequencyHz, 1e-5);
  region->performance.ownership.front().mode = seam::domain::ManualPerformanceMode::Replace;
  const auto manual = seam::synthesis::compileScorePerformance(project, *region, 48000U,
      pronunciation.value().pronunciation.tokens);
  CHECK(manual);
  const auto replaced = manual.value().inspectAt(continuation.startFrame);
  CHECK(!replaced.reattack);
  CHECK_NEAR(*replaced.scoreFrequencyHz, *settled.scoreFrequencyHz, 1e-5);
  CHECK_NEAR(replaced.dynamicsGain, onset.dynamicsGain, 1e-6);
  CHECK_NEAR(replaced.articulationGain, onset.articulationGain, 1e-6);
  region->performance.ownership.front().scope = seam::domain::PerformanceTimeRange{
      seam::time::Tick{965}, seam::time::Tick{970}};
  const auto scoped = seam::synthesis::compileScorePerformance(project, *region, 48000U,
      pronunciation.value().pronunciation.tokens);
  CHECK(scoped);
  const auto begin = project.tempoMap().sampleFrameAt(region->startTick + seam::time::Tick{965}, 48000.0);
  const auto end = project.tempoMap().sampleFrameAt(region->startTick + seam::time::Tick{970}, 48000.0);
  CHECK_NEAR(*scoped.value().inspectAt(begin - 1).scoreFrequencyHz, *linked.value().inspectAt(begin - 1).scoreFrequencyHz, 1e-5);
  CHECK_NEAR(*scoped.value().inspectAt(begin).scoreFrequencyHz, *settled.scoreFrequencyHz, 1e-5);
  CHECK_NEAR(*scoped.value().inspectAt(end - 1).scoreFrequencyHz, *settled.scoreFrequencyHz, 1e-5);
  CHECK_NEAR(*scoped.value().inspectAt(end).scoreFrequencyHz, *linked.value().inspectAt(end).scoreFrequencyHz, 1e-5);
}
