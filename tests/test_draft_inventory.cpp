#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank_production/draft_inventory.hpp"
#include "seam/voicebank_production/repository.hpp"

#include <algorithm>
#include <stop_token>

namespace {
namespace production = seam::voicebank_production;

// Golden values printed by tools/voicebank_script_generator/draft_inventory.py
// generate_draft_inventory() for the same profiles.
constexpr std::string_view kDefaultInventorySha256{"8d1bdce97ecc9b0bde96004bfb393958bfe43b8c7087138c9154cad2fd64e481"};
constexpr std::string_view kDefaultScriptSha256{"b65d2b42d1381367204e3b7223f3ca640d98dc1412391983623bfb6b2a43c1d4"};
constexpr std::string_view kDefaultFirstIdentity{"c6eea096f83ec23ee63a29980504370c9c18da78e9312e56641ce5ce77be91a8"};
constexpr std::string_view kCustomInventorySha256{"a521b59c2d6af295d5e77ed0a3ae2bb5b0ee55d8caa46f581e93110006a5c6ad"};
constexpr std::string_view kCustomScriptSha256{"f45f1f73c4cbd16b5a5a5e25210a16c5604843e3061590e5b9453180d70a49ec"};
// generate_draft_inventory({"profileId": "starter-voice", "includeKinds": ["sustain"], "pitchLayers": [60, 66]})
constexpr std::string_view kStarterInventorySha256{"cbec43927a6b01c2b16a7fc99942f91a927d3ca3c717b7d09f28592196a3fbc4"};
constexpr std::string_view kStarterScriptSha256{"19c99748b9647994bfddef612925b15e7c015870ff799b8a7030679310fbf2c3"};
constexpr std::string_view kCustomProfile{
    R"({"profileId":"custom \u2014 voice","supportedStyles":["soft, \"airy\"","\u660e\u308b\u3044"],)"
    R"("vowels":["a","o"],"consonants":["k","sh"],"specialPhones":["N","br"],)"
    R"("includeKinds":["sustain","breath","cv","vv"],"pitchLayers":[64,57],)"
    R"("requestedRange":{"minMidi":55,"maxMidi":70},"alternateTakes":1,"sessionBlockSize":3})"};

production::DraftInventoryProfile defaultProfile() {
  production::DraftInventoryProfile profile;
  profile.profileId = "studio-default";
  return profile;
}

std::string replaceFirst(std::string text, std::string_view from, std::string_view to) {
  const auto found = text.find(from);
  CHECK(found != std::string::npos);
  if (found != std::string::npos) text.replace(found, from.size(), to);
  return text;
}

std::vector<std::string> entries(const std::filesystem::path& directory) {
  std::vector<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator{directory})
    names.push_back(entry.path().filename().string());
  std::sort(names.begin(), names.end());
  return names;
}
}  // namespace

TEST_CASE("draft inventories match the external generator byte for byte") {
  const auto generated = production::generateDraftInventory(defaultProfile());
  CHECK(generated);
  if (!generated) return;
  const auto& inventory = generated.value();
  CHECK(inventory.units.size() == 2052U);
  CHECK(inventory.requiredCoverage.size() == 342U);
  CHECK(inventory.inventorySha256 == kDefaultInventorySha256);
  CHECK(inventory.scriptSha256 == kDefaultScriptSha256);
  CHECK(seam::core::sha256Hex(inventory.operatorCsv) == inventory.scriptSha256);
  const auto& first = inventory.units.front();
  CHECK(first.assignmentId == kDefaultFirstIdentity);
  CHECK(first.promptId == "P00001-" + std::string{kDefaultFirstIdentity});
  CHECK(first.takeId == first.promptId + "-t01");
  CHECK(first.filename == "takes/s7e2372f4115c43ba/p60/" + first.takeId + ".wav");
  CHECK(first.coverageKey == "sustain:a");
  CHECK(first.sessionBlock == 1);
  // Each alternate gets its own prompt; both alternates share one assignment.
  CHECK(inventory.units[1].promptId == "P00002-" + std::string{kDefaultFirstIdentity});
  CHECK(inventory.units[1].takeId == inventory.units[1].promptId + "-t02");
  CHECK(inventory.units[24].sessionBlock == 2);

  const auto profile = production::parseDraftInventoryProfile(kCustomProfile);
  CHECK(profile);
  if (!profile) return;
  CHECK((profile.value().pitchLayers == std::vector<std::int64_t>{64, 57}));
  const auto custom = production::generateDraftInventory(profile.value());
  CHECK(custom);
  if (!custom) return;
  CHECK(custom.value().units.size() == 36U);
  CHECK(custom.value().inventorySha256 == kCustomInventorySha256);
  CHECK(custom.value().scriptSha256 == kCustomScriptSha256);
  // csv QUOTE_MINIMAL: a style with a comma and quotes is quoted with doubled quotes.
  const auto secondLine = custom.value().operatorCsv.substr(custom.value().operatorCsv.find('\n') + 1U);
  CHECK(secondLine.starts_with("P00001-482a084067c3db13c3bed2cfb7a6fb107f72e8f69915c173456dd50ba266c3a3,"
      "P00001-482a084067c3db13c3bed2cfb7a6fb107f72e8f69915c173456dd50ba266c3a3-t01,ja,"
      "\"soft, \"\"airy\"\"\",sustain,a,a,64,1,"));
}

TEST_CASE("a loaded draft inventory must be its profile's exact output") {
  const auto generated = production::generateDraftInventory(defaultProfile());
  CHECK(generated);
  if (!generated) return;
  const auto& document = generated.value().documentJson;
  const auto loaded = production::loadDraftInventory(document);
  CHECK(loaded);
  if (loaded) {
    CHECK(loaded.value().inventorySha256 == kDefaultInventorySha256);
    CHECK(loaded.value().units.size() == 2052U);
  }
  const auto refused = [](const std::string& text) {
    const auto result = production::loadDraftInventory(text);
    CHECK(!result);
    return result ? std::string{} : result.error().message + " " + result.error().context;
  };
  // A moved take path is refused even though the document still parses.
  CHECK(refused(replaceFirst(document, "takes/s7e2372f4115c43ba/p60/", "takes/elsewhere/p60/")).find("units") !=
      std::string::npos);
  // Rehashing an edit cannot make it the profile's output.
  auto rehashed = replaceFirst(document, "\"alternateTakes\": 2", "\"alternateTakes\": 1");
  rehashed = replaceFirst(rehashed, std::string{kDefaultInventorySha256}, std::string(64U, 'a'));
  static_cast<void>(refused(rehashed));
  // Python keeps 60.0 distinct from 60, so no numeric normalization may admit it.
  CHECK(refused(replaceFirst(document, "\"pitchLayer\": 60", "\"pitchLayer\": 60.0")).find("non-integer") !=
      std::string::npos);
  CHECK(refused(replaceFirst(document, "\"alternateTakes\": 2", "\"alternateTakes\": 2, \"extra\": true")).find(
      "unknown fields") != std::string::npos);
  CHECK(refused(replaceFirst(document, "\"sessionBlockSize\": 24,", "")).find("sessionBlockSize") != std::string::npos);
  static_cast<void>(refused("[]"));
  const auto rangeTest = production::parseDraftInventoryProfile(
      R"({"profileId":"x","rangeTest":{"method":"m","minMidi":60,"maxMidi":72,"result":"PASS"}})");
  CHECK(!rangeTest);
  if (!rangeTest) CHECK(rangeTest.error().message.find("not draft authority") != std::string::npos);
}

TEST_CASE("draft inventory profiles keep the generator's bounds") {
  const auto refused = [](production::DraftInventoryProfile profile, std::string_view expected) {
    const auto result = production::generateDraftInventory(profile);
    CHECK(!result);
    if (!result) CHECK(result.error().message.find(expected) != std::string::npos);
  };
  auto profile = defaultProfile();
  profile.profileId.clear();
  refused(profile, "profileId");
  profile = defaultProfile(); profile.profileId = std::string(129U, 'x');
  refused(profile, "profileId");
  profile = defaultProfile(); profile.profileId = std::string{"bad\ncontrol"};
  refused(profile, "profileId");
  profile = defaultProfile(); profile.profileId = std::string{"\xC0\xAF"};
  refused(profile, "profileId");
  profile = defaultProfile(); profile.language = "en";
  refused(profile, "Japanese rules");
  profile = defaultProfile(); profile.vowels = {"a", "a"};
  refused(profile, "duplicate");
  profile = defaultProfile(); profile.consonants.push_back("k:y");
  refused(profile, "unsupported phonetic symbol");
  profile = defaultProfile(); profile.supportedStyles.assign(17U, "style");
  refused(profile, "supportedStyles");
  profile = defaultProfile(); profile.includeKinds = {"sustain", "chant"};
  refused(profile, "unsupported kinds");
  profile = defaultProfile(); profile.pitchLayers = {60};
  refused(profile, "pitchLayers");
  profile = defaultProfile(); profile.pitchLayers = {60, 60};
  refused(profile, "pitchLayers");
  profile = defaultProfile(); profile.pitchLayers = {60, 74};
  refused(profile, "requested, unassessed range");
  profile = defaultProfile(); profile.requestedMinMidi = 72; profile.requestedMaxMidi = 60;
  refused(profile, "requestedRange");
  profile = defaultProfile(); profile.alternateTakes = 9;
  refused(profile, "alternateTakes");
  profile = defaultProfile(); profile.sessionBlockSize = 0;
  refused(profile, "sessionBlockSize");
  profile = defaultProfile(); profile.vowels = {"a"}; profile.includeKinds = {"vv"};
  refused(profile, "at least one required sequence");
  profile = defaultProfile();
  profile.supportedStyles = {"a", "b", "c", "d", "e", "f", "g", "h"};
  refused(profile, "exceeds 16384 units");
  CHECK(!production::parseDraftInventoryProfile(R"({"profileId":"x","pitchLayers":[60,true]})"));
  CHECK(!production::parseDraftInventoryProfile(R"({"profileId":"x","alternateTakes":2.0})"));
  CHECK(!production::parseDraftInventoryProfile(R"({"profileId":"x","requestedRange":{"minMidi":60}})"));
}

TEST_CASE("a new producer workspace is published once with its inventory and script") {
  const auto parent = seam::test::support::temporaryDirectory("draft-producer-workspace");
  const auto destination = parent / "first-voice";
  const auto created = production::createDraftProducerWorkspace(destination, defaultProfile(),
      "first-voice", "producer", "2026-09-29T00:00:00Z");
  CHECK(created);
  if (!created) return;
  const auto& result = created.value();
  // The result names the resolved folder (on macOS /var resolves to /private/var).
  CHECK(result.root == std::filesystem::canonical(destination));
  CHECK(result.producerRoot == result.root / "producer");
  CHECK(result.inventorySha256 == kDefaultInventorySha256);
  CHECK(result.scriptSha256 == kDefaultScriptSha256);
  CHECK(result.units == 2052U);
  CHECK(result.assignments == 1026U);
  CHECK(result.durabilityConfirmed);
  CHECK(entries(parent) == std::vector<std::string>{"first-voice"});
  CHECK((entries(destination) == std::vector<std::string>{"inventory.json", "producer", "recording-script.csv"}));
  const auto script = seam::core::sha256File(result.scriptPath);
  CHECK(script && script.value() == kDefaultScriptSha256);

  const auto reopened = production::ProductionProjectRepository{result.producerRoot}.recover();
  CHECK(reopened);
  if (reopened) {
    const auto& project = reopened.value();
    CHECK(project.schemaVersion == production::kProductionStyleSchemaVersion);
    CHECK(project.language == "ja");
    CHECK(project.projectId == "first-voice");
    CHECK(project.inventoryId == "studio-default");
    CHECK(project.inventorySha256 == kDefaultInventorySha256);
    CHECK(project.lastDurableGeneration == result.generation);
    CHECK(project.operators.size() == 1U);
    CHECK(project.operators.front().operatorId == "producer");
    CHECK(project.operators.front().role == "PRODUCER");
    CHECK(project.unitAssignments.size() == 1026U);
    CHECK(std::all_of(project.unitAssignments.begin(), project.unitAssignments.end(), [](const auto& row) {
      return row.state == production::UnitQueueState::Missing && row.takeId.empty() && !row.markerReviewed &&
          !row.pitchReviewed && row.style == "neutral" && row.plannedTakeId.ends_with("-t01");
    }));
    CHECK(project.takes.empty());
    CHECK(project.sourceStrategies.empty());
    CHECK(project.reviews.empty());
  }
  const auto read = production::readDraftProducerWorkspaceInventory(destination);
  CHECK(read);
  if (read) {
    CHECK(read.value().producerRoot == result.producerRoot);
    CHECK(read.value().inventory.inventorySha256 == kDefaultInventorySha256);
  }

  // Creation never replaces an existing folder, and a refusal changes nothing.
  const auto inventoryBefore = seam::core::sha256File(result.inventoryPath);
  const auto again = production::createDraftProducerWorkspace(destination, defaultProfile(),
      "first-voice", "producer", "2026-09-29T00:00:01Z");
  CHECK(!again);
  const auto inventoryAfter = seam::core::sha256File(result.inventoryPath);
  CHECK(inventoryBefore && inventoryAfter && inventoryBefore.value() == inventoryAfter.value());
  CHECK(entries(parent) == std::vector<std::string>{"first-voice"});
}

TEST_CASE("refused producer workspace creation leaves no folder or staging behind") {
  const auto parent = seam::test::support::temporaryDirectory("draft-producer-refusal");
  auto invalid = defaultProfile();
  invalid.pitchLayers = {60};
  CHECK(!production::createDraftProducerWorkspace(parent / "bad-profile", invalid, "voice", "producer",
      "2026-09-29T00:00:00Z"));
  CHECK(!production::createDraftProducerWorkspace(std::filesystem::path{"relative-voice"}, defaultProfile(),
      "voice", "producer", "2026-09-29T00:00:00Z"));
  CHECK(!production::createDraftProducerWorkspace(parent / ".hidden", defaultProfile(), "voice", "producer",
      "2026-09-29T00:00:00Z"));
  CHECK(!production::createDraftProducerWorkspace(parent / "missing-parent" / "voice", defaultProfile(),
      "voice", "producer", "2026-09-29T00:00:00Z"));
  CHECK(!production::createDraftProducerWorkspace(parent / "no-producer", defaultProfile(), "voice", "",
      "2026-09-29T00:00:00Z"));
  CHECK(!production::createDraftProducerWorkspace(parent / "bad-time", defaultProfile(), "voice", "producer",
      "yesterday"));
  std::stop_source stop;
  stop.request_stop();
  CHECK(!production::createDraftProducerWorkspace(parent / "cancelled", defaultProfile(), "voice", "producer",
      "2026-09-29T00:00:00Z", stop.get_token()));
  CHECK(entries(parent).empty());
  CHECK(!production::readDraftProducerWorkspaceInventory(parent / "cancelled"));
}

TEST_CASE("named inventory presets are the full default draft and the Python vowel starter") {
  const auto full = production::generateDraftInventory(
      production::draftInventoryPresetProfile(production::DraftInventoryPreset::JapaneseFull, "studio-default"));
  CHECK(full);
  if (full) CHECK(full.value().inventorySha256 == kDefaultInventorySha256);
  const auto profile =
      production::draftInventoryPresetProfile(production::DraftInventoryPreset::JapaneseVowelStarter, "starter-voice");
  CHECK((profile.includeKinds == std::vector<std::string>{"sustain"}));
  CHECK((profile.pitchLayers == std::vector<std::int64_t>{60, 66}));
  const auto starter = production::generateDraftInventory(profile);
  CHECK(starter);
  if (!starter) return;
  CHECK(starter.value().inventorySha256 == kStarterInventorySha256);
  CHECK(starter.value().scriptSha256 == kStarterScriptSha256);
  CHECK((starter.value().requiredCoverage ==
         std::vector<std::string>{"sustain:a", "sustain:i", "sustain:u", "sustain:e", "sustain:o"}));
  CHECK(starter.value().units.size() == 20U);
  const auto project = production::makeDraftProducerProject(starter.value(), "starter-voice", "producer");
  CHECK(project);
  if (!project) return;
  // One assignment per vowel and pitch layer; the two alternates share it.
  CHECK(project.value().unitAssignments.size() == 10U);
  CHECK(std::all_of(project.value().unitAssignments.begin(), project.value().unitAssignments.end(),
      [](const auto& row) { return row.pitchLayer == 60 || row.pitchLayer == 66; }));
  const auto reloaded = production::loadDraftInventory(starter.value().documentJson);
  CHECK(reloaded);
  if (reloaded) CHECK(reloaded.value().inventorySha256 == kStarterInventorySha256);
}
