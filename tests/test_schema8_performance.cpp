#include "test_framework.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/core/file_io.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"

#include <array>

namespace {

seam::domain::Project performanceProject() {
  seam::application::ProjectFactory factory{8100U};
  auto project = factory.createProject("Schema eight performance");
  const auto trackId = factory.addVocalTrack(project, "Singer");
  const auto regionId = factory.addRegion(
      project, trackId, "Phrase", seam::time::Tick{0}, seam::time::Tick{3840});
  auto [lyric, note] = factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{960}, 60U, U"あ");
  auto* region = project.findRegion(regionId);
  region->lyrics.push_back(lyric);
  region->notes.push_back(note);
  return project;
}

seam::formats::JsonValue encodedPerformanceProject() {
  const auto encoded = seam::formats::ProjectJsonCodec{}.encode(performanceProject());
  CHECK(encoded);
  auto tree = seam::formats::parseJson(encoded.value());
  CHECK(tree);
  return std::move(tree).value();
}

seam::formats::JsonValue& trackTree(seam::formats::JsonValue& tree) {
  return tree.find("vocalTracks")->asArray().front();
}

seam::formats::JsonValue& regionTree(seam::formats::JsonValue& tree) {
  return trackTree(tree).find("regions")->asArray().front();
}

seam::formats::JsonValue& noteTree(seam::formats::JsonValue& tree) {
  return regionTree(tree).find("notes")->asArray().front();
}

seam::formats::JsonValue frozenFixture(std::string_view relative) {
  const auto source = seam::core::readTextFileLimited(
      std::filesystem::path{SEAM_SCHEMA8_SOURCE_ROOT} / relative, 1U << 20U);
  CHECK(source);
  auto tree = seam::formats::parseJson(source.value());
  CHECK(tree);
  return std::move(tree).value();
}

}

TEST_CASE("schema nine persists exact procedural selections and migrates schema eight neutrally") {
  auto project = performanceProject();
  auto& track = project.vocalTracks().front();
  track.proceduralRecipe = seam::domain::ProceduralRecipeReference{
      {seam::domain::SingerResourceKind::Procedural, "draft", "1", std::string(64U, 'a')},
      "recipes/draft.voice-recipe.json", "neutral"};
  const seam::formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(project); CHECK(encoded);
  const auto decoded = codec.decode(encoded.value()); CHECK(decoded); CHECK(decoded.value() == project);
  auto tree = seam::formats::parseJson(encoded.value()).value();
  trackTree(tree).asObject().erase("proceduralRecipe");
  CHECK(!codec.decode(seam::formats::stringifyJson(tree)));
  tree.asObject()["schemaVersion"] = seam::formats::JsonValue{std::int64_t{8}};
  const auto legacy = codec.decode(seam::formats::stringifyJson(tree)); CHECK(legacy);
  CHECK(!legacy.value().vocalTracks().front().proceduralRecipe);
  track.proceduralRecipe.reset(); CHECK(legacy.value() == project);
  tree = seam::formats::parseJson(encoded.value()).value();
  trackTree(tree).find("proceduralRecipe")->asObject()["path"] = seam::formats::JsonValue{std::string{}};
  CHECK(!codec.decode(seam::formats::stringifyJson(tree)));
  tree = seam::formats::parseJson(encoded.value()).value();
  trackTree(tree).find("proceduralRecipe")->asObject()["unexpected"] = seam::formats::JsonValue{true};
  CHECK(!codec.decode(seam::formats::stringifyJson(tree)));
  tree = seam::formats::parseJson(encoded.value()).value();
  tree.asObject()["schemaVersion"] = seam::formats::JsonValue{std::int64_t{8}};
  CHECK(!codec.decode(seam::formats::stringifyJson(tree)));
}

TEST_CASE("schema eight writer emits required performance fields and five lanes") {
  const auto tree = encodedPerformanceProject();
  CHECK(tree.find("schemaVersion")->asInt64() == seam::formats::ProjectJsonCodec::kSchemaVersion);
  const auto& track = tree.find("vocalTracks")->asArray().front();
  const auto& region = track.find("regions")->asArray().front();
  const auto& note = region.find("notes")->asArray().front();
  CHECK(note.find("vibrato") != nullptr);
  CHECK(note.find("vibrato")->isObject());
  CHECK(note.find("phoneticHint") != nullptr);
  CHECK(note.find("phoneticHint")->isNull());
  CHECK(region.find("dynamicsAutomation") != nullptr);
  CHECK(region.find("dynamicsAutomation")->isArray());
  CHECK(track.find("styleSelection") != nullptr);
  CHECK(track.find("styleSelection")->isObject());
  CHECK(tree.find("settings")->find("technicalLanes")->asArray().size() == 5U);
}

TEST_CASE("schema eight round trip preserves vibrato hint dynamics and explicit style") {
  auto project = performanceProject();
  auto& track = project.vocalTracks().front();
  auto& region = track.regions.front();
  region.notes.front().vibrato = {.enabled = true, .startFraction = 0.4F,
      .fadeInFraction = 0.2F, .fadeOutFraction = 0.3F, .depthCents = 42.0F,
      .periodMilliseconds = 155.0F, .phaseTurns = 0.25F};
  region.notes.front().phoneticHint = "k a / 한글";
  track.styleSelection = {.origin = seam::domain::VoiceStyleOrigin::Explicit, .styleId = "soft-息"};
  CHECK(region.dynamicsAutomation.replacePoints({
      {.tick = seam::time::Tick{0}, .linearGain = 0.5F},
      {.tick = seam::time::Tick{480}, .linearGain = 0.0F},
      {.tick = seam::time::Tick{960}, .linearGain = 2.0F}}));
  project.settings().technicalLanes[4] = {.mode = seam::domain::TechnicalLaneMode::Expanded, .expandedHeight = 264.0};
  const seam::formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(project);
  CHECK(encoded);
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value() == project);
}

TEST_CASE("schema eight rejects omitted performance fields instead of silently defaulting") {
  const seam::formats::ProjectJsonCodec codec;
  for (const auto field : {"vibrato", "phoneticHint", "dynamicsAutomation", "styleSelection"}) {
    auto tree = encodedPerformanceProject();
    CHECK(codec.decode(seam::formats::stringifyJson(tree)));
    auto& object = std::string_view{field} == "styleSelection" ? trackTree(tree)
        : std::string_view{field} == "dynamicsAutomation" ? regionTree(tree) : noteTree(tree);
    object.asObject().erase(field);
    const auto result = codec.decode(seam::formats::stringifyJson(tree));
    CHECK(!result);
    CHECK(result.error().code == seam::core::ErrorCode::ParseError);
  }
}

TEST_CASE("schema eight rejects invalid vibrato and malformed nullable hints") {
  using seam::formats::JsonValue;
  const seam::formats::ProjectJsonCodec codec;
  const std::array invalidVibrato{std::pair{"depthCents", -1.0}, std::pair{"depthCents", 201.0},
      std::pair{"periodMilliseconds", 0.0}, std::pair{"periodMilliseconds", 1e100},
      std::pair{"phaseTurns", 1.0}, std::pair{"fadeInFraction", 0.95}, std::pair{"startFraction", 1.1}};
  for (const auto& [field, value] : invalidVibrato) {
    auto tree = encodedPerformanceProject();
    noteTree(tree).find("vibrato")->asObject()[field] = JsonValue{value};
    CHECK(!codec.decode(seam::formats::stringifyJson(tree)));
  }
  for (const auto& value : std::array{JsonValue{true}, JsonValue{""},
           JsonValue{std::string(4097U, 'a')}, JsonValue{std::string(1U, '\xff')}}) {
    auto tree = encodedPerformanceProject();
    noteTree(tree).asObject()["phoneticHint"] = value;
    CHECK(!codec.decode(seam::formats::stringifyJson(tree)));
  }
}

TEST_CASE("schema eight rejects invalid dynamics ordering bounds and point counts") {
  using seam::formats::JsonValue;
  const auto point = [](JsonValue tick, double gain) { return JsonValue{JsonValue::Object{
      {"tick", std::move(tick)}, {"linearGain", JsonValue{gain}}}}; };
  const std::array invalid{
      JsonValue::Array{point(JsonValue{0.5}, 1.0)},
      JsonValue::Array{point(JsonValue{std::int64_t{-1}}, 1.0)},
      JsonValue::Array{point(JsonValue{std::int64_t{3841}}, 1.0)},
      JsonValue::Array{point(JsonValue{std::int64_t{0}}, -0.1)},
      JsonValue::Array{point(JsonValue{std::int64_t{0}}, 4.1)},
      JsonValue::Array{point(JsonValue{std::int64_t{0}}, 1e100)},
      JsonValue::Array{point(JsonValue{std::int64_t{0}}, 1.0), point(JsonValue{std::int64_t{0}}, 0.5)},
      JsonValue::Array{point(JsonValue{std::int64_t{2}}, 1.0), point(JsonValue{std::int64_t{1}}, 0.5)},
      JsonValue::Array(seam::domain::kMaximumDynamicsPoints + 1U, point(JsonValue{std::int64_t{0}}, 1.0))};
  for (const auto& values : invalid) {
    auto tree = encodedPerformanceProject();
    regionTree(tree).asObject()["dynamicsAutomation"] = JsonValue{values};
    CHECK(!seam::formats::ProjectJsonCodec{}.decode(seam::formats::stringifyJson(tree)));
  }
}

TEST_CASE("schema eight rejects raw numeric bounds before float rounding") {
  using seam::formats::JsonValue;
  const seam::formats::ProjectJsonCodec codec;
  for (const auto& [field, value] : std::array{
      std::pair{"depthCents", -1e-100},
      std::pair{"startFraction", 1.000000000001},
      std::pair{"periodMilliseconds", 500.000001}}) {
    auto tree = encodedPerformanceProject();
    noteTree(tree).find("vibrato")->asObject()[field] = JsonValue{value};
    CHECK(!codec.decode(seam::formats::stringifyJson(tree)));
  }
  auto fadeTree = encodedPerformanceProject();
  auto& vibrato = noteTree(fadeTree).find("vibrato")->asObject();
  vibrato["fadeInFraction"] = JsonValue{0.6};
  vibrato["fadeOutFraction"] = JsonValue{0.400000000001};
  CHECK(!codec.decode(seam::formats::stringifyJson(fadeTree)));
  auto gainTree = encodedPerformanceProject();
  regionTree(gainTree).asObject()["dynamicsAutomation"] = JsonValue::Array{
      JsonValue::Object{{"tick", JsonValue{std::int64_t{0}}},
                        {"linearGain", JsonValue{-1e-100}}}};
  CHECK(!codec.decode(seam::formats::stringifyJson(gainTree)));
}

TEST_CASE("schema eight preserves valid canonical float fades at the combined boundary") {
  auto project = performanceProject();
  auto& vibrato = project.vocalTracks().front().regions.front().notes.front().vibrato;
  vibrato.enabled = true;
  vibrato.fadeInFraction = 0.8F;
  vibrato.fadeOutFraction = 0.2F;
  CHECK(vibrato.validate());
  const seam::formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(project);
  CHECK(encoded);
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value() == project);
}

TEST_CASE("schema eight requires known style origin and consistent bounded identity") {
  using seam::formats::JsonValue;
  for (const auto& [origin, id] : std::array{
      std::pair{std::string{"unknown"}, std::string{"soft"}},
      std::pair{std::string{"explicit"}, std::string{}},
      std::pair{std::string{"unselected"}, std::string{"soft"}},
      std::pair{std::string{"legacy-needs-exact-bank-resolution"}, std::string{"soft"}},
      std::pair{std::string{"explicit"}, std::string(1025U, 's')}}) {
    auto tree = encodedPerformanceProject();
    trackTree(tree).asObject()["styleSelection"] = JsonValue{JsonValue::Object{
        {"origin", JsonValue{origin}}, {"styleId", JsonValue{id}}}};
    CHECK(!seam::formats::ProjectJsonCodec{}.decode(seam::formats::stringifyJson(tree)));
  }
}

TEST_CASE("real legacy project fixtures migrate neutrally and reencode without musical loss") {
  const seam::formats::ProjectJsonCodec codec;
  for (const auto& [path, version] : std::array{
      std::pair{"tests/fixtures/projects/schema-1-historical-writer.seam", 1},
      std::pair{"docs/phase2/evidence/phase2-demo.seam.json", 2},
      std::pair{"docs/phase3/evidence/phase3-demo.seam.json", 3},
      std::pair{"tests/fixtures/projects/schema-4-historical-writer.seam", 4},
      std::pair{"tests/fixtures/projects/schema-5-historical-writer.seam", 5},
      std::pair{"tests/fixtures/projects/schema-6-historical-writer.seam", 6},
      std::pair{"tests/fixtures/projects/schema-7-historical-writer.seam", 7},
      std::pair{"tests/fixtures/projects/schema-5-audio-track.seam", 5},
      std::pair{"tests/fixtures/projects/schema-6-media-identity.seam", 6},
      std::pair{"tests/singing_quality/corpus/original-melody.seam", 7},
      std::pair{"tests/singing_quality/corpus/unequal-rests.seam", 7}}) {
    const auto tree = frozenFixture(path);
    CHECK(tree.find("schemaVersion")->asInt64() == version);
    const auto decoded = codec.decode(seam::formats::stringifyJson(tree));
    CHECK(decoded);
    if (version == 1 || version == 4) {
      CHECK(decoded.value().noteCount() == 1U);
      const auto& legacyRegion = decoded.value().vocalTracks().front().regions.front();
      CHECK(legacyRegion.notes.front().midiKey == 64U);
      CHECK(legacyRegion.notes.front().startTick == seam::time::Tick{240});
      CHECK(legacyRegion.notes.front().durationTick == seam::time::Tick{480});
      CHECK(legacyRegion.lyrics.front().surface == U"か");
      CHECK(legacyRegion.performance.takes.empty());
      CHECK(!legacyRegion.performance.pronunciation);
      if (version == 4) {
        CHECK(legacyRegion.phonemeOverrides.size() == 1U);
        CHECK(legacyRegion.phonemeOverrides.front().timing.startOffset == -1200);
        CHECK(legacyRegion.phonemeOverrides.front().locked);
        CHECK(legacyRegion.phonemeOverrides.front().unresolved);
        CHECK(!legacyRegion.phonemeOverrides.front().sourceContextId);
        CHECK(decoded.value().routing().deviceOutputChannels == 2U);
      }
    }
    CHECK(decoded.value().settings().technicalLanes.size() == 5U);
    CHECK(decoded.value().settings().technicalLanes[4] == seam::domain::TechnicalLanePresentation{});
    for (const auto& track : decoded.value().vocalTracks()) {
      CHECK(track.styleSelection.origin == seam::domain::VoiceStyleOrigin::LegacyNeedsExactBankResolution);
      CHECK(track.styleSelection.styleId.empty());
      for (const auto& region : track.regions) {
        for (const auto& edit : region.phonemeOverrides) {
          CHECK(edit.unresolved);
          CHECK(!edit.sourceContextId);
        }
        for (const auto& edit : region.unitSelectionOverrides) CHECK(edit.unresolved);
        for (const auto& edit : region.seamOverrides) CHECK(edit.unresolved);
        CHECK(region.dynamicsAutomation.points().empty());
        for (const auto& note : region.notes) {
          CHECK(!note.vibrato.enabled);
          CHECK(!note.phoneticHint.has_value());
        }
      }
    }
    const auto upgraded = codec.encode(decoded.value());
    CHECK(upgraded);
    CHECK(codec.decode(upgraded.value()).value() == decoded.value());
  }
}

TEST_CASE("schema four migration uses frozen pre-schema-six audio metadata") {
  auto tree = frozenFixture("tests/fixtures/projects/schema-5-audio-track.seam");
  tree.asObject()["schemaVersion"] = seam::formats::JsonValue{std::int64_t{4}};
  tree.find("settings")->asObject().erase("hostStartOffsetTick");
  const auto decoded = seam::formats::ProjectJsonCodec{}.decode(seam::formats::stringifyJson(tree));
  CHECK(decoded);
  CHECK(decoded.value().audioTracks().size() == 1U);
  CHECK(decoded.value().settings().hostStartOffsetTick == seam::time::Tick{0});
}

TEST_CASE("pre binding schemas retain manual payloads unresolved and ignore invented binding claims") {
  // Characterization seeded by a genuine old-writer fixture; added controls are
  // synthetic input, not represented as historical writer output.
  auto tree = frozenFixture("tests/fixtures/projects/schema-4-historical-writer.seam");
  using Json = seam::formats::JsonValue;
  auto& region = regionTree(tree);
  const auto noteId = *noteTree(tree).find("id");
  auto& phoneme = region.find("phonemeOverrides")->asArray().front();
  phoneme.asObject()["sourceContextId"] = Json{std::string(64U, 'a')};
  phoneme.asObject()["unresolved"] = Json{false};
  Json::Object unit{
      {"noteId", noteId}, {"ordinal", Json{std::int64_t{0}}},
      {"tokenCount", Json{std::int64_t{2}}}, {"unitId", Json{std::string{"legacy-ka"}}},
      {"locked", Json{true}}, {"unresolved", Json{false}}, {"loopPrint", Json{0.6}}};
  region.asObject()["unitSelectionOverrides"] = Json{Json::Array{Json{std::move(unit)}}};
  Json::Object seam{
      {"noteId", noteId}, {"ordinal", Json{std::int64_t{1}}},
      {"curve", Json{std::string{"linear"}}}, {"locked", Json{true}},
      {"unresolved", Json{false}}, {"overlapUs", Json{std::int64_t{1200}}}};
  region.asObject()["seamOverrides"] = Json{Json::Array{Json{std::move(seam)}}};
  seam::formats::ProjectJsonCodec codec;
  const auto migrated = codec.decode(seam::formats::stringifyJson(tree));
  CHECK(migrated);
  const auto& state = migrated.value().vocalTracks().front().regions.front();
  CHECK(state.phonemeOverrides.front().unresolved);
  CHECK(!state.phonemeOverrides.front().sourceContextId);
  CHECK(state.phonemeOverrides.front().locked);
  CHECK(state.phonemeOverrides.front().timing.startOffset == -1200);
  CHECK(state.unitSelectionOverrides.front().unresolved);
  CHECK(state.unitSelectionOverrides.front().unitId == "legacy-ka");
  CHECK_NEAR(*state.unitSelectionOverrides.front().loopPrint, 0.6, 1e-6);
  CHECK(state.seamOverrides.front().unresolved);
  CHECK(state.seamOverrides.front().overlap == 1200);
  const auto resolved = seam::phonemizer::resolveJapanesePronunciation(state);
  CHECK(resolved);
  CHECK(!resolved.value().pronunciation.warnings.empty());
  for (const auto& token : resolved.value().pronunciation.tokens) CHECK(!token.locked);
  const auto saved = codec.encode(migrated.value());
  CHECK(saved);
  const auto reopened = codec.decode(saved.value());
  CHECK(reopened);
  CHECK(reopened.value() == migrated.value());
}

TEST_CASE("legacy style metadata cannot claim resolution without the exact bank") {
  auto tree = frozenFixture("tests/singing_quality/corpus/original-melody.seam");
  trackTree(tree).asObject()["styleSelection"] = seam::formats::JsonValue::Object{
      {"origin", seam::formats::JsonValue{"explicit"}},
      {"styleId", seam::formats::JsonValue{"unverified-style"}}};
  const auto decoded = seam::formats::ProjectJsonCodec{}.decode(seam::formats::stringifyJson(tree));
  CHECK(decoded);
  CHECK(decoded.value().vocalTracks().front().styleSelection.origin ==
        seam::domain::VoiceStyleOrigin::LegacyNeedsExactBankResolution);
  CHECK(decoded.value().vocalTracks().front().styleSelection.styleId.empty());
}

TEST_CASE("schema seven requires four lanes while schema eight requires five") {
  auto legacy = frozenFixture("tests/singing_quality/corpus/original-melody.seam");
  auto& lanes = legacy.find("settings")->find("technicalLanes")->asArray();
  lanes.front().asObject()["mode"] = seam::formats::JsonValue{"expanded"};
  lanes.front().asObject()["expandedHeight"] = seam::formats::JsonValue{300.0};
  const seam::formats::ProjectJsonCodec codec;
  const auto migrated = codec.decode(seam::formats::stringifyJson(legacy));
  CHECK(migrated);
  CHECK(migrated.value().settings().technicalLanes[0].expandedHeight == 300.0);
  lanes.push_back(lanes.front());
  CHECK(!codec.decode(seam::formats::stringifyJson(legacy)));
  auto current = encodedPerformanceProject();
  current.find("settings")->find("technicalLanes")->asArray().pop_back();
  CHECK(!codec.decode(seam::formats::stringifyJson(current)));
}

TEST_CASE("schema eight rejects fractional and future schema versions") {
  auto tree = encodedPerformanceProject();
  tree.asObject()["schemaVersion"] = seam::formats::JsonValue{8.5};
  const seam::formats::ProjectJsonCodec codec;
  CHECK(!codec.decode(seam::formats::stringifyJson(tree)));
  tree.asObject()["schemaVersion"] = seam::formats::JsonValue{std::int64_t{seam::formats::ProjectJsonCodec::kSchemaVersion + 1}};
  const auto future = codec.decode(seam::formats::stringifyJson(tree));
  CHECK(!future);
  CHECK(future.error().code == seam::core::ErrorCode::Unsupported);
}
