// Which of a producer's own declared classes its recipe can actually prepare, and
// which phones nothing covers. Synthetic fixtures: this proves the coverage route,
// not that a useful singer or a qualified resource was produced.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/inventory_coverage.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
#include <spawn.h>
#include <sys/wait.h>
#include <cerrno>
extern char** environ;
#endif

namespace {
using namespace seam;
namespace production = voicebank_production;

#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
int runVoicebankCli(std::vector<std::string> arguments) {
  arguments.insert(arguments.begin(), SEAM_TEST_VOICEBANK_CLI);
  std::vector<char*> argv;
  for (auto& argument : arguments) argv.push_back(argument.data());
  argv.push_back(nullptr);
  pid_t process{};
  if (posix_spawn(&process, SEAM_TEST_VOICEBANK_CLI, nullptr, nullptr, argv.data(), environ) != 0) return -1;
  int status = 0;
  pid_t waited;
  do { waited = waitpid(process, &status, 0); } while (waited < 0 && errno == EINTR);
  if (waited != process) return -1;
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
}
#endif

voice_design::VoiceRecipe coverageRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "coverage-recipe";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}},
                  {"i", "neutral", 0.0, {{300.0, 70.0, 0.0}, {2300.0, 120.0, -3.0}, {3200.0, 170.0, -6.0}}}};
  recipe.frications = {{"s", "neutral", {.seed = 41U}}};
  return recipe;
}

struct Fixture final {
  std::filesystem::path root{test::support::temporaryDirectory("inventory-coverage")};
  std::filesystem::path workspace{root / "producer"};
  std::filesystem::path recipePath{root / "recipe.json"};
  production::VoicebankProductionProject project;
  production::ProductionProjectRepository repository{workspace};
  synthesis::ProceduralSingerResource resource;

  explicit Fixture(std::vector<std::string> coverageKeys) {
    const auto license = root / "fixture-license.txt";
    CHECK(core::durableAtomicWriteTextNew(license, "Synthetic coverage test only; no singer or Beta qualification."));
    const auto licenseHash = core::sha256File(license); CHECK(licenseHash);
    project = {.projectId = "coverage", .inventoryId = "fixture", .inventorySha256 = std::string(64U, 'a'),
        .selectedSourceStrategyId = "fixture", .licenseLocator = license.string(),
        .licenseSha256 = licenseHash.value(), .immutableAssetRoot = "assets"};
    project.schemaVersion = production::kProductionStyleSchemaVersion;
    project.language = "ja";
    project.sourceStrategies = {{.id = "fixture", .kind = production::SourceStrategyKind::ProceduralSynthesis,
        .rights = production::Feasibility::Pass, .coverage = production::Feasibility::Pass,
        .listening = production::Feasibility::Pass, .permissions = {true, true, true, true},
        .licenseLocator = license.string(), .licenseSha256 = licenseHash.value(),
        .evidenceState = "SYNTHETIC_TEST_ONLY"}};
    project.operators = {{"producer", "PRODUCER"}};
    for (std::size_t index = 0U; index < coverageKeys.size(); ++index) {
      project.unitAssignments.push_back({.coverageKey = coverageKeys[index], .pitchLayer = 60, .promptId = "prompt-" + std::to_string(index),
          .plannedTakeId = "take-" + std::to_string(index), .style = "neutral"});
    }
    CHECK(repository.initialize(project, {.action = "create", .subjectId = project.projectId,
        .operatorId = "producer", .occurredAtUtc = "2026-09-14T00:00:00Z"}));
    CHECK(voice_design::saveVoiceRecipeFile(recipePath, coverageRecipe()));
    const auto frozen = voice_design::freezeVoiceRecipeResource(coverageRecipe());
    CHECK(frozen);
    resource = frozen.value();
  }
};

}  // namespace

TEST_CASE("coverage reports every declared class the recipe can prepare") {
  Fixture fixture({"cv:s:a", "sustain:a", "sustain:i"});
  const auto report = authoring::inspectInventoryCoverage(fixture.project, fixture.resource);
  CHECK(report);
  if (!report) return;
  CHECK(report.value().complete);
  CHECK(report.value().assignments == 3U);
  CHECK(report.value().prepared == 3U);
  CHECK(report.value().refused == 0U);
  CHECK(report.value().phones == 3U);   // a, i, s
  CHECK(report.value().kinds == 2U);    // cv, sustain
  CHECK(report.value().missingPhones.empty());
  CHECK(report.value().missingKinds.empty());
  CHECK(report.value().entries.size() == 3U);
  for (const auto& entry : report.value().entries) {
    CHECK(entry.status == "PREPARED");
    CHECK(entry.frameCount > 0U);
  }
  const auto parsed = formats::parseJson(report.value().json); CHECK(parsed);
  CHECK(parsed.value().find("status")->asString() == "COMPLETE");
  CHECK(parsed.value().find("releaseEligible")->asBool() == false);
  CHECK(parsed.value().find("recipeSha256")->asString() == fixture.resource.identity.contentHash);
  // Inspection compiles, it does not render or collect.
  const auto recovered = fixture.repository.recover(); CHECK(recovered);
  CHECK(recovered.value().takes.empty());
  const auto tight = authoring::inspectInventoryCoverage(fixture.project, fixture.resource, {.maximumAssignments = 2U});
  CHECK(!tight);
}

TEST_CASE("coverage names the class a recipe cannot prepare instead of dropping it") {
  // The voiced affricate じ has no model, so its class cannot be prepared while the
  // rest of the inventory can. The report keeps the failure rather than skipping it.
  Fixture fixture({"cv:s:a", "cv:j:a"});
  const auto report = authoring::inspectInventoryCoverage(fixture.project, fixture.resource);
  CHECK(report);
  if (!report) return;
  CHECK(!report.value().complete);
  CHECK(report.value().assignments == 2U);
  CHECK(report.value().prepared == 1U);
  CHECK(report.value().refused == 1U);
  CHECK(report.value().refusedClasses.size() == 1U);
  CHECK(report.value().refusedClasses.front() == "cv:j:a");
  CHECK(report.value().missingPhones.size() == 1U);
  CHECK(report.value().missingPhones.front() == "j");
  CHECK(report.value().missingKinds.empty());
  const auto refused = std::find_if(report.value().entries.begin(), report.value().entries.end(),
      [](const auto& entry) { return entry.status == "REFUSED"; });
  CHECK(refused != report.value().entries.end());
  if (refused != report.value().entries.end()) {
    CHECK(refused->coverageKey == "cv:j:a");
    CHECK(refused->detail.find("j") != std::string::npos);
    CHECK(refused->frameCount == 0U);
  }
}

#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
TEST_CASE("the CLI retains a coverage report for an incomplete inventory") {
  Fixture fixture({"cv:s:a", "cv:j:a"});
  const auto reportPath = fixture.root / "coverage.json";
  CHECK(runVoicebankCli({"inspect-generation-coverage", fixture.workspace.string(),
      fixture.recipePath.string(), reportPath.string()}) == 0);
  const auto bytes = core::readTextFileLimited(reportPath, 32U * 1024U * 1024U); CHECK(bytes);
  const auto parsed = formats::parseJson(bytes.value()); CHECK(parsed);
  CHECK(parsed.value().find("status")->asString() == "INCOMPLETE");
  CHECK(parsed.value().find("refused")->asInt64() == 1);
  // The report is evidence, so it is published once and never silently replaced.
  CHECK(runVoicebankCli({"inspect-generation-coverage", fixture.workspace.string(),
      fixture.recipePath.string(), reportPath.string()}) != 0);
}
#endif
