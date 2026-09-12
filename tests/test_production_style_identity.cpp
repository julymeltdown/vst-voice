#include "test_framework.hpp"
#include "seam/voicebank_production/project.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank/wav.hpp"
#include "test_support.hpp"

#include <set>

namespace production = seam::voicebank_production;

namespace {
production::VoicebankProductionProject styleDraft() {
  production::VoicebankProductionProject project;
  project.schemaVersion = production::kProductionStyleSchemaVersion;
  project.projectId = "style-test";
  project.language = "ja";
  project.inventoryId = "style-inventory-test";
  project.inventorySha256 = std::string(64U, 'a');
  project.operators = {{"producer", "PRODUCER"}};
  project.unitAssignments = {
      {.coverageKey = "sustain:a", .pitchLayer = 60, .promptId = "neutral-prompt", .plannedTakeId = "neutral-take", .style = "neutral"},
      {.coverageKey = "sustain:a", .pitchLayer = 60, .promptId = "soft-prompt", .plannedTakeId = "soft-take", .style = "soft"}};
  return project;
}
}

TEST_CASE("schema four round trips distinct styles without silently upgrading legacy workspaces") {
  const auto project = styleDraft();
  CHECK(production::validateProductionProject(project));
  const auto encoded = production::encodeProductionProject(project);
  const auto decoded = production::decodeProductionProject(encoded);
  CHECK(decoded);
  CHECK(decoded.value().language == "ja");
  CHECK(decoded.value().unitAssignments[1].style == "soft");
  CHECK(production::encodeProductionProject(decoded.value()) == encoded);
  auto invalid = project;
  invalid.unitAssignments[1].style = "neutral";
  CHECK(!production::validateProductionProject(invalid));
  invalid = project; invalid.language.clear();
  CHECK(!production::validateProductionProject(invalid));
  invalid = project; invalid.unitAssignments[0].style.clear();
  CHECK(!production::validateProductionProject(invalid));
  invalid = project; invalid.schemaVersion = 3;
  CHECK(!production::validateProductionProject(invalid));
  auto legacyBytes = encoded;
  const auto version = legacyBytes.find("\"schemaVersion\": 4");
  CHECK(version != std::string::npos);
  legacyBytes.replace(version, std::string{"\"schemaVersion\": 4"}.size(), "\"schemaVersion\": 3");
  CHECK(!production::decodeProductionProject(legacyBytes));
}

TEST_CASE("style-owned raw takes persist independently even when they share identical PCM") {
  const auto root = seam::test::support::temporaryDirectory("style-owned-import");
  auto project = styleDraft();
  const auto notice = root / "notice.txt";
  CHECK(seam::core::durableAtomicWriteText(notice, "GENERATED TEST FIXTURE ONLY; no singer qualification"));
  const auto licenseHash = seam::core::sha256File(notice); CHECK(licenseHash);
  project.selectedSourceStrategyId = "test-source";
  project.licenseLocator = notice.string(); project.licenseSha256 = licenseHash.value();
  project.sourceStrategies.push_back({.id = "test-source", .kind = production::SourceStrategyKind::ProceduralSynthesis,
      .rights = production::Feasibility::Pass,
      .permissions = {.sourceUse = true, .transformation = true},
      .licenseLocator = notice.string(), .licenseSha256 = licenseHash.value(), .evidenceState = "SYNTHETIC_TEST_ONLY"});
  production::ProductionProjectRepository repository{root / "workspace"};
  CHECK(repository.initialize(project, {"create", project.projectId, "producer", "2026-09-13T00:00:00Z"}));
  const auto wav = root / "raw.wav";
  CHECK(seam::voicebank::writeWav(wav, {.sampleRate = 48000U, .channels = 1U,
      .sampleFormat = seam::voicebank::WavSampleFormat::Pcm24}, seam::test::support::sineWave(48000U, 261.63, 0.12, 0.2F)));
  for (const auto& style : {std::string{"neutral"}, std::string{"soft"}}) {
    CHECK(repository.importRaw(project, wav,
        {.takeId = style + "-take", .promptId = style + "-prompt", .coverageKey = "sustain:a", .pitchLayer = 60, .style = style},
        {"import", style + "-take", "producer", "2026-09-13T00:01:00Z"}));
  }
  CHECK(project.takes.size() == 2U);
  CHECK(project.takes[0].rawAssetSha256 == project.takes[1].rawAssetSha256);
  CHECK(project.takes[0].style != project.takes[1].style);
  const auto recovered = repository.recover(); CHECK(recovered);
  CHECK(production::encodeProductionProject(recovered.value()) == production::encodeProductionProject(project));
  const auto before = production::encodeProductionProject(project);
  CHECK(!repository.importRaw(project, wav,
      {.takeId = "wrong-retake", .promptId = "soft-prompt", .coverageKey = "sustain:a", .pitchLayer = 60,
       .supersedesTakeId = "neutral-take", .style = "soft"},
      {"retake", "wrong-retake", "producer", "2026-09-13T00:02:00Z"}));
  CHECK(!repository.importRaw(project, wav,
      {.takeId = "style-free-take", .promptId = "soft-prompt", .coverageKey = "sustain:a", .pitchLayer = 60},
      {"import", "style-free-take", "producer", "2026-09-13T00:02:00Z"}));
  CHECK(production::encodeProductionProject(project) == before);
  CHECK(production::encodeProductionProject(repository.recover().value()) == before);
  auto relabeled = project;
  relabeled.language = "en";
  CHECK(!repository.save(relabeled, {"save", project.projectId, "producer", "2026-09-13T00:03:00Z"}));
  relabeled = project;
  relabeled.takes[0].style = "bright";
  relabeled.unitAssignments[0].style = "bright";
  CHECK(production::validateProductionProject(relabeled));
  CHECK(!repository.save(relabeled, {"save", project.projectId, "producer", "2026-09-13T00:03:00Z"}));
  CHECK(production::encodeProductionProject(repository.recover().value()) == before);
}

TEST_CASE("production assignment identity agrees with the Python draft inventory") {
  const production::ProductionUnitIdentity identity{"ja", "neutral", "sustain:a", 60};
  CHECK(production::productionUnitIdentitySha256(identity) ==
      "c6eea096f83ec23ee63a29980504370c9c18da78e9312e56641ce5ce77be91a8");
  CHECK(production::productionUnitIdentitySha256({"ja", "柔らかい \"A\"", "sustain:a", 60}) ==
      "35700829e14437ae6c67c43592a6517ea547811148bf31f460a256c42e42a17a");
}

TEST_CASE("production assignment identity retains all four axes without style slug collisions") {
  const production::ProductionUnitIdentity base{"ja", "soft bright", "sustain:a", 60};
  std::set<production::ProductionUnitIdentity> identities{base};
  std::set<std::string> hashes{production::productionUnitIdentitySha256(base)};
  for (const auto& identity : {
      production::ProductionUnitIdentity{"en", "soft bright", "sustain:a", 60},
      production::ProductionUnitIdentity{"ja", "soft-bright", "sustain:a", 60},
      production::ProductionUnitIdentity{"ja", "soft bright", "sustain:i", 60},
      production::ProductionUnitIdentity{"ja", "soft bright", "sustain:a", 66}}) {
    CHECK(identity != base);
    CHECK(identities.insert(identity).second);
    CHECK(hashes.insert(production::productionUnitIdentitySha256(identity)).second);
  }
  CHECK(identities.size() == 5U);
}
