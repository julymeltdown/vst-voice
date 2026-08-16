#include "test_framework.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/formats/project_json.hpp"

TEST_CASE("internal JSON parser handles nested UTF-8 data") {
  const auto parsed = seam::formats::parseJson(
      R"({"name":"seam","enabled":true,"values":[1,2,3],"text":"あ"})");
  CHECK(parsed);
  CHECK(parsed.value().find("name")->asString() == "seam");
  CHECK(parsed.value().find("enabled")->asBool());
  CHECK(parsed.value().find("values")->asArray().size() == 3);
  CHECK(!seam::formats::parseJson(R"({"broken": [1,})"));
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

TEST_CASE("project decoder rejects an unsupported schema") {
  seam::formats::ProjectJsonCodec codec;
  const auto decoded = codec.decode(
      R"({"formatId":"com.project-seam.project","schemaVersion":99,"projectId":"1","name":"x","ppq":960,"tempoMap":[],"meterMap":[],"settings":{},"vocalTracks":[],"audioTracks":[]})");
  CHECK(!decoded);
  CHECK(decoded.error().code == seam::core::ErrorCode::Unsupported);
}

TEST_CASE("project decoder migrates schema one regions without phoneme overrides") {
  seam::application::ProjectFactory factory{300};
  auto project = factory.createProject("Schema one fixture");
  const auto trackId = factory.addVocalTrack(project, "Track");
  const auto regionId = factory.addRegion(
      project, trackId, "Region", seam::time::Tick{0}, seam::time::Tick{15360});
  auto [lyric, note] = factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{960}, 60, U"あ");
  project.findRegion(regionId)->lyrics.push_back(lyric);
  project.findRegion(regionId)->notes.push_back(note);

  seam::formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(project);
  CHECK(encoded);
  auto legacy = encoded.value();
  const auto schemaPosition = legacy.find("\"schemaVersion\": 4");
  CHECK(schemaPosition != std::string::npos);
  legacy.replace(schemaPosition, std::string{"\"schemaVersion\": 4"}.size(),
                 "\"schemaVersion\": 1");
  for (const auto field : {"phonemeOverrides", "unitSelectionOverrides",
                           "seamOverrides", "pitchAutomation"}) {
    const auto fragment = std::string{",\n          \""} + field + "\": []";
    const auto position = legacy.find(fragment);
    CHECK(position != std::string::npos);
    legacy.erase(position, fragment.size());
  }
  const auto decoded = codec.decode(legacy);
  CHECK(decoded);
  CHECK(decoded.value() == project);
}
