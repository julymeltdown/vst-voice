#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/formats/project_json.hpp"

#include <filesystem>

TEST_CASE("internal JSON parser handles nested UTF-8 data") {
  const auto parsed = seam::formats::parseJson(
      R"({"name":"seam","enabled":true,"values":[1,2,3],"text":"あ"})");
  CHECK(parsed);
  CHECK(parsed.value().find("name")->asString() == "seam");
  CHECK(parsed.value().find("enabled")->asBool());
  CHECK(parsed.value().find("values")->asArray().size() == 3);
  CHECK(!seam::formats::parseJson(R"({"broken": [1,})"));
}

TEST_CASE("internal JSON parser accepts finite decimal and exponent numbers") {
  const auto decimal = seam::formats::parseJson("-0.125");
  const auto exponent = seam::formats::parseJson("1.25e2");

  CHECK(decimal);
  CHECK(exponent);
  CHECK_NEAR(decimal.value().asNumber(), -0.125, 1e-12);
  CHECK_NEAR(exponent.value().asNumber(), 125.0, 1e-12);
  CHECK(!seam::formats::parseJson("1e9999"));
}

TEST_CASE("JSON arrays relocate nested object values without dangling storage") {
  seam::formats::JsonValue::Array values;
  for (std::int64_t index = 0; index < 64; ++index) {
    values.emplace_back(seam::formats::JsonValue::Object{
        {"index", seam::formats::JsonValue{index}},
        {"nested", seam::formats::JsonValue::Object{
                       {"value", seam::formats::JsonValue{index + 1}}}},
    });
  }

  const seam::formats::JsonValue root{values};
  CHECK(root.asArray().size() == 64U);
  CHECK(root.asArray().back().find("index")->asInt64() == 63);
}

TEST_CASE("project JSON round trip preserves the canonical model") {
  seam::application::ProjectFactory factory{100};
  auto project = factory.createProject("Serialization fixture");
  project.settings().characterDisplay = seam::domain::CharacterDisplayMode::Off;
  project.settings().snapGrid = seam::time::Tick{120};
  CHECK(project.tempoMap().addOrReplace(seam::time::Tick{3840}, 92.5));
  CHECK(project.meterMap().addOrReplace(seam::time::Tick{3840}, 3, 4));
  const auto trackId = factory.addVocalTrack(project, "Main");
  auto* track = project.findVocalTrack(trackId);
  track->voicebank = {"voice.test", "1.0.0", "abc"};
  track->character = {"character.test", "1.0.0"};
  const auto regionId = factory.addRegion(
      project, trackId, "Verse", seam::time::Tick{0}, seam::time::Tick{15360});
  auto [lyric, note] = factory.makeNote(
      seam::time::Tick{960}, seam::time::Tick{720}, 64, U"あ");
  project.findRegion(regionId)->lyrics.push_back(lyric);
  project.findRegion(regionId)->notes.push_back(note);
  project.findRegion(regionId)->phonemeOverrides.push_back(
      seam::domain::PhonemeOverride{
          .key = seam::domain::PhonemeKey{note.id, 0},
          .symbol = std::string{"y"},
          .timing = seam::domain::PhonemeTiming{
              .startOffset = seam::time::Microseconds{-42000},
              .endOffset = seam::time::Microseconds{0},
          },
          .locked = true,
      });
  project.findRegion(regionId)->unitSelectionOverrides.push_back(
      seam::domain::UnitSelectionOverride{
          .startKey = seam::domain::PhonemeKey{note.id, 0},
          .tokenCount = 1,
          .unitId = "unit.alt.02",
          .renderer = seam::domain::UnitRendererKind::ClassicPsola,
          .loopPrint = 0.35F,
          .sourcePitchResidual = 0.72F,
          .locked = true,
      });
  project.findRegion(regionId)->seamOverrides.push_back(
      seam::domain::SeamOverride{
          .incomingStartKey = seam::domain::PhonemeKey{note.id, 0},
          .seamAmount = 0.82F,
          .overlap = seam::time::Microseconds{9000},
          .phaseReset = 0.6F,
          .envelopeBlend = 0.15F,
          .curve = seam::domain::SeamCurve::HardCharacter,
          .locked = true,
      });
  CHECK(project.findRegion(regionId)->pitchAutomation.upsert(
      seam::domain::PitchAutomationPoint{
          .tick = seam::time::Tick{1080},
          .cents = 22.0F,
          .interpolation = seam::domain::CurveInterpolation::Smooth,
      }));

  seam::formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(project);
  CHECK(encoded);
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value() == project);
}

namespace {

// Rewrites one already-encoded project so a schema test can describe an older
// file without hand-maintaining a second full fixture.
void replaceSchemaVersion(std::string& json,std::string_view replacement) {
  const auto key=json.find("\"schemaVersion\"");
  const auto colon=json.find(':',key);
  auto begin=colon+1U;
  while (begin<json.size() && json[begin]==' ') ++begin;
  auto end=begin;
  while (end<json.size() && json[end]>='0' && json[end]<='9') ++end;
  json.replace(begin,end-begin,replacement);
}

void replaceNeuralMemberWithNull(std::string& json) {
  const auto key=json.find("\"neuralResource\"");
  const auto colon=json.find(':',key);
  auto begin=colon+1U;
  while (begin<json.size() && json[begin]==' ') ++begin;
  auto end=begin;
  if (begin<json.size() && json[begin]=='{') {
    int depth=0;
    for (;end<json.size();++end) {
      if (json[end]=='{') ++depth;
      else if (json[end]=='}' && --depth==0) {++end; break;}
    }
  } else {
    while (end<json.size() && json[end]!=',' && json[end]!='}') ++end;
    while (end>begin && json[end-1]==' ') --end;
  }
  json.replace(begin,end-begin,"null");
}

}  // namespace

TEST_CASE("project JSON persists one singer selection per track and migrates older schemas") {
  seam::application::ProjectFactory factory{120};
  auto project = factory.createProject("Neural selection fixture");
  const auto trackId = factory.addVocalTrack(project, "Neural");
  auto* track = project.findVocalTrack(trackId);
  track->neuralResource = seam::domain::NeuralResourceReference{
      {seam::domain::SingerResourceKind::Neural, "neural.bank.test", "1.0.0", std::string(64U, 'a')}};
  CHECK(project.validate());
  seam::formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(project);
  CHECK(encoded);
  CHECK(encoded.value().find("\"neuralResource\"") != std::string::npos);
  CHECK(encoded.value().find("neural.bank.test") != std::string::npos);
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value() == project);
  const auto& saved = decoded.value().vocalTracks().front().neuralResource;
  CHECK(saved.has_value());
  CHECK(saved->resource.id == "neural.bank.test");
  CHECK(saved->resource.kind == seam::domain::SingerResourceKind::Neural);
  // A schema 9 file has no neural member. It migrates to "no neural selection"
  // instead of inventing a voice or refusing the project.
  auto older = encoded.value();
  replaceSchemaVersion(older, "9");
  replaceNeuralMemberWithNull(older);
  const auto migrated = codec.decode(older);
  CHECK(migrated);
  CHECK(!migrated.value().vocalTracks().front().neuralResource.has_value());
  CHECK(migrated.value().vocalTracks().front().proceduralRecipe.has_value() == false);
  // The same old schema must not smuggle a new selection it cannot describe.
  auto smuggled = encoded.value();
  replaceSchemaVersion(smuggled, "9");
  CHECK(!codec.decode(smuggled));
  // One track selects one singer family, and the reference kind must match.
  auto conflicting = project;
  conflicting.findVocalTrack(trackId)->proceduralRecipe = seam::domain::ProceduralRecipeReference{
      {seam::domain::SingerResourceKind::Procedural, "recipe", "1", std::string(64U, 'b')},
      "recipe.json", "neutral"};
  CHECK(!conflicting.validate());
  auto wrongKind = project;
  wrongKind.findVocalTrack(trackId)->neuralResource->resource.kind = seam::domain::SingerResourceKind::Procedural;
  CHECK(!wrongKind.validate());
  auto invalidIdentity = project;
  invalidIdentity.findVocalTrack(trackId)->neuralResource->resource.contentHash.clear();
  CHECK(!invalidIdentity.validate());
}

TEST_CASE("project JSON persists and validates the bounce timing authority") {
  seam::application::ProjectFactory factory{260};
  auto project = factory.createProject("Bounce authority fixture");
  CHECK(project.settings().bounceTimingAuthority ==
        seam::domain::BounceTimingAuthority::FixedAudio);
  seam::formats::ProjectJsonCodec codec;
  const auto fixed = codec.encode(project);
  CHECK(fixed);
  if (!fixed) return;
  CHECK(fixed.value().find("\"bounceTimingAuthority\": \"fixed-audio\"") !=
        std::string::npos);
  const auto fixedDecoded = codec.decode(fixed.value());
  CHECK(fixedDecoded);
  if (fixedDecoded) CHECK(fixedDecoded.value() == project);

  project.settings().bounceTimingAuthority = seam::domain::BounceTimingAuthority::FollowHost;
  const auto follow = codec.encode(project);
  CHECK(follow);
  if (!follow) return;
  CHECK(follow.value().find("\"bounceTimingAuthority\": \"follow-host\"") !=
        std::string::npos);
  const auto followDecoded = codec.decode(follow.value());
  CHECK(followDecoded);
  if (!followDecoded) return;
  CHECK(followDecoded.value() == project);
  CHECK(followDecoded.value().settings().bounceTimingAuthority ==
        seam::domain::BounceTimingAuthority::FollowHost);

  // A project written before this choice existed meant its own tempo map, and is read as such.
  auto legacy = seam::formats::parseJson(follow.value());
  CHECK(legacy);
  if (!legacy) return;
  legacy.value().asObject()["schemaVersion"] = seam::formats::JsonValue{std::int64_t{10}};
  legacy.value().find("settings")->asObject().erase("bounceTimingAuthority");
  const auto legacyDecoded = codec.decode(seam::formats::stringifyJson(legacy.value()));
  CHECK(legacyDecoded);
  if (legacyDecoded) {
    CHECK(legacyDecoded.value().settings().bounceTimingAuthority ==
          seam::domain::BounceTimingAuthority::FixedAudio);
  }

  // A current project that omits the choice, or names one this build does not implement, is
  // refused instead of being quietly rendered as Fixed Audio.
  auto missing = seam::formats::parseJson(fixed.value());
  CHECK(missing);
  if (!missing) return;
  missing.value().find("settings")->asObject().erase("bounceTimingAuthority");
  CHECK(!codec.decode(seam::formats::stringifyJson(missing.value())));

  auto unknown = seam::formats::parseJson(fixed.value());
  CHECK(unknown);
  if (!unknown) return;
  unknown.value().find("settings")->asObject()["bounceTimingAuthority"] =
      seam::formats::JsonValue{"listen-to-the-room"};
  const auto rejected = codec.decode(seam::formats::stringifyJson(unknown.value()));
  CHECK(!rejected);
  if (!rejected) CHECK(rejected.error().code == seam::core::ErrorCode::ParseError);
}

TEST_CASE("project decoder rejects an unsupported schema") {
  seam::formats::ProjectJsonCodec codec;
  const auto decoded = codec.decode(
      R"({"formatId":"com.project-seam.project","schemaVersion":99,"projectId":"1","name":"x","ppq":960,"tempoMap":[],"meterMap":[],"settings":{},"vocalTracks":[],"audioTracks":[]})");
  CHECK(!decoded);
  CHECK(decoded.error().code == seam::core::ErrorCode::Unsupported);
}

TEST_CASE("project decoder migrates schema one regions without phoneme overrides") {
  constexpr auto legacy = R"({
    "formatId":"com.project-seam.project","schemaVersion":1,
    "projectId":"1","name":"Schema one fixture","ppq":960,
    "tempoMap":[{"tick":0,"bpm":120}],
    "meterMap":[{"tick":0,"numerator":4,"denominator":4}],
    "settings":{"sampleRate":48000,"characterDisplay":"minimal","snapEnabled":true,"snapGrid":240},
    "vocalTracks":[{"id":"2","name":"Track","voicebank":{"id":"","version":"","contentHash":""},
      "character":{"id":"","version":""},"gainDb":0,"muted":false,
      "regions":[{"id":"3","name":"Region","startTick":0,"durationTick":15360,
        "lyrics":[{"id":"4","surface":"あ","language":"ja"}],
        "notes":[{"id":"5","startTick":0,"durationTick":960,"midiKey":60,"lyricId":"4","articulation":"normal"}]}]}],
    "audioTracks":[]})";
  seam::formats::ProjectJsonCodec codec;
  const auto decoded = codec.decode(legacy);
  CHECK(decoded);
  const auto& track = decoded.value().vocalTracks().front();
  const auto& region = track.regions.front();
  CHECK(region.phonemeOverrides.empty());
  CHECK(region.unitSelectionOverrides.empty());
  CHECK(region.pitchAutomation.points().empty());
  CHECK(region.dynamicsAutomation.points().empty());
  CHECK(!region.notes.front().vibrato.enabled);
  CHECK(!region.notes.front().phoneticHint.has_value());
  CHECK(track.styleSelection.origin == seam::domain::VoiceStyleOrigin::LegacyNeedsExactBankResolution);
  CHECK(track.styleSelection.styleId.empty());
  const auto upgraded = codec.encode(decoded.value());
  CHECK(upgraded);
  CHECK(codec.decode(upgraded.value()).value() == decoded.value());
}

TEST_CASE("project JSON path I/O rejects symlink and non-file targets") {
  const auto root = seam::test::support::temporaryDirectory("project-paths");
  seam::application::ProjectFactory factory{700U};
  auto project = factory.createProject("Project path boundary");
  const auto track = factory.addVocalTrack(project, "Voice");
  static_cast<void>(factory.addRegion(project, track, "Region",
                                      seam::time::Tick{0},
                                      seam::time::Tick{3840}));
  seam::formats::ProjectJsonCodec codec;
  const auto outside = root / "outside.seam";
  CHECK(codec.save(project, outside));
  const auto link = root / "project-link.seam";
  std::error_code error;
  std::filesystem::create_symlink(outside, link, error);
  CHECK(!error);
  CHECK(!codec.load(link));
  CHECK(!codec.save(project, link));
  CHECK(codec.load(outside));

  const auto directory = root / "project-directory";
  std::filesystem::create_directories(directory);
  CHECK(!codec.load(directory));
  CHECK(!codec.save(project, directory));

  const auto target = root / "target.seam";
  CHECK(codec.save(project, target));
  const auto backupSource = root / "backup-outside.seam";
  CHECK(codec.save(project, backupSource));
  const auto backupLink = target.string() + ".bak";
  std::filesystem::create_symlink(backupSource, backupLink, error);
  CHECK(!error);
  CHECK(!codec.save(project, target));
  CHECK(std::filesystem::is_symlink(backupLink));
  CHECK(codec.load(backupSource));
}
