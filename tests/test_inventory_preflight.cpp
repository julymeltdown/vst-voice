// The held-out inventory preflight and the gate that refuses to multiply a phone
// class before its phrases have rendered audibly.
//
// The producer, recipe and audio are synthetic fixtures: they prove the selection,
// rendering, admission and gate route, not that a useful singer or a qualified
// resource was produced.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/generation_campaign.hpp"
#include "seam/authoring/inventory_preflight.hpp"
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

voice_design::VoiceRecipe preflightRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "preflight-recipe";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  recipe.frications = {{"s", "neutral", {.seed = 41U}}};
  return recipe;
}

struct Fixture final {
  std::filesystem::path root{test::support::temporaryDirectory("inventory-preflight")};
  std::filesystem::path workspace{root / "producer"};
  production::VoicebankProductionProject project;
  production::ProductionProjectRepository repository{workspace};
  std::string campaign;
  std::string campaignSha256;
  std::filesystem::path campaignPath{root / "campaign" / "campaign.json"};
  std::filesystem::path campaignRoot{root / "campaign"};

  explicit Fixture() {
    const auto license = root / "fixture-license.txt";
    CHECK(core::durableAtomicWriteTextNew(license, "Synthetic preflight test only; no singer or Beta qualification."));
    const auto licenseHash = core::sha256File(license); CHECK(licenseHash);
    project = {.projectId = "preflight", .inventoryId = "fixture", .inventorySha256 = std::string(64U, 'a'),
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
    project.unitAssignments = {
        {.coverageKey = "cv:s:a", .pitchLayer = 69, .promptId = "prompt-sa", .plannedTakeId = "take-sa", .style = "neutral"},
        {.coverageKey = "vc:a:s", .pitchLayer = 69, .promptId = "prompt-as", .plannedTakeId = "take-as", .style = "neutral"},
        {.coverageKey = "sustain:a", .pitchLayer = 69, .promptId = "prompt-aa", .plannedTakeId = "take-aa", .style = "neutral"}};
    CHECK(repository.initialize(project, {.action = "create", .subjectId = project.projectId,
        .operatorId = "producer", .occurredAtUtc = "2026-09-14T00:00:00Z"}));
    const auto resource = voice_design::freezeVoiceRecipeResource(preflightRecipe());
    CHECK(resource);
    const std::vector<std::string> takes{"take-sa", "take-as", "take-aa"};
    const auto planned = authoring::planGenerationCampaign(project, takes, resource.value());
    if (!planned) throw test::Failure{"Campaign planning failed: " + planned.error().message};
    campaign = planned.value();
    campaignSha256 = core::sha256Hex(campaign);
    CHECK(std::filesystem::create_directory(campaignRoot));
    CHECK(core::durableAtomicWriteTextNew(campaignPath, campaign));
  }
  [[nodiscard]] std::filesystem::path preflight(const std::string& name = "preflight") const {
    return campaignRoot / name;
  }
};

}  // namespace

TEST_CASE("a preflight selects one bounded phrase per phone and kind the campaign declares") {
  Fixture fixture;
  const auto selected = authoring::selectInventoryPreflightTakeIds(fixture.campaign, fixture.campaignSha256);
  CHECK(selected);
  if (!selected) return;
  // Every assignment is needed: cv:s:a is the only cv, vc:a:s is the only vc, and
  // sustain:a is the only sustain. The order is canonical, not mapped order.
  CHECK(selected.value().size() == 3U);
  CHECK(selected.value()[0] == "take-sa");
  CHECK(selected.value()[1] == "take-aa");
  CHECK(selected.value()[2] == "take-as");
  // A bound that cannot cover them is refused with the requirement, not truncated.
  const auto tight = authoring::selectInventoryPreflightTakeIds(fixture.campaign, fixture.campaignSha256,
      {.maximumPhrases = 1U});
  CHECK(!tight);
  if (!tight) {
    CHECK(tight.error().message.find("bound of 1") != std::string::npos);
    CHECK(tight.error().message.find("2 phones") != std::string::npos);
    CHECK(tight.error().message.find("3 kinds") != std::string::npos);
  }
  CHECK(authoring::selectInventoryPreflightTakeIds(fixture.campaign, std::string(64U, '0')).error().code ==
      core::ErrorCode::InvalidArgument);
}

TEST_CASE("a preflight renders its phrases, retains dry audio and collects nothing") {
  Fixture fixture;
  const auto report = authoring::runInventoryPreflight(fixture.campaign, fixture.campaignSha256, fixture.preflight());
  CHECK(report);
  if (!report) return;
  CHECK(report.value().passed);
  CHECK(report.value().produced == 3U);
  CHECK(report.value().defective == 0U);
  CHECK(report.value().requiredPhones == 2U);
  CHECK(report.value().requiredKinds == 3U);
  CHECK(report.value().phrases.size() == 3U);
  for (const auto& phrase : report.value().phrases) {
    CHECK(phrase.verdict == "PRODUCED");
    CHECK(phrase.peak > 0.0);
    CHECK(phrase.nonzeroFrames > 0U);
    CHECK(phrase.frameCount > 0U);
    // Every phone the coverage key declares has its own rendered gesture.
    for (const auto& phone : phrase.requiredPhones)
      CHECK(std::find(phrase.producedPhones.begin(), phrase.producedPhones.end(), phone) != phrase.producedPhones.end());
  }
  // The evidence is retained beside the report the campaign gate reads.
  const auto reportPath = fixture.preflight() / "report.json";
  const auto bytes = core::readTextFileLimited(reportPath, 32U * 1024U * 1024U); CHECK(bytes);
  CHECK(bytes.value() == report.value().json);
  CHECK(std::filesystem::exists(fixture.preflight() / "phrase-0" / "output" / "candidates"));
  CHECK(std::filesystem::exists(fixture.preflight() / "phrase-1" / "output" / "candidates"));
  CHECK(std::filesystem::exists(fixture.preflight() / "phrase-2" / "output" / "candidates"));
  // The vowel-to-coda unit renders both of its own gestures in order: the inventory's
  // own declared class is now a real, audible pair rather than a refused placement.
  CHECK(report.value().phrases[2].coverageKey == "vc:a:s");
  CHECK(report.value().phrases[2].requiredPhones == (std::vector<std::string>{"a", "s"}));
  CHECK(report.value().phrases[2].producedPhones == (std::vector<std::string>{"a", "s"}));
  CHECK(authoring::verifyInventoryPreflight(bytes.value(), fixture.campaign, fixture.campaignSha256));
  // Rendering a phrase never commits a take or advances the producer history.
  const auto recovered = fixture.repository.recover(); CHECK(recovered);
  CHECK(recovered.value().takes.empty());
  CHECK(recovered.value().lastDurableGeneration == fixture.project.lastDurableGeneration);
  // The preflight directory is published once and is never silently replaced.
  CHECK(!authoring::runInventoryPreflight(fixture.campaign, fixture.campaignSha256, fixture.preflight()));
}

TEST_CASE("a campaign cannot advance until its own preflight passes") {
  Fixture fixture;
  // No report at all: the campaign is refused before anything renders or commits.
  const auto missing = authoring::advanceGenerationCampaign(fixture.repository, fixture.campaignPath,
      fixture.campaignSha256, "producer", "2026-09-14T00:00:01Z");
  CHECK(!missing);
  if (!missing) CHECK(missing.error().message.find("preflight") != std::string::npos);
  CHECK(!std::filesystem::exists(fixture.campaignRoot / "batch-0"));
  CHECK(fixture.repository.recover().value().takes.empty());
  // A refused phrase is retained as a failing report and still cannot advance.
  const auto refused = authoring::runInventoryPreflight(fixture.campaign, fixture.campaignSha256,
      fixture.preflight(), {}, {},
      [](std::size_t index) -> std::optional<std::string> {
        return index == 1U ? std::optional<std::string>{"injected refusal for a defective class"} : std::nullopt;
      });
  CHECK(refused);
  if (!refused) return;
  CHECK(!refused.value().passed);
  CHECK(refused.value().defective == 1U);
  CHECK(refused.value().defectiveClasses.size() == 1U);
  CHECK(refused.value().phrases[1].verdict == "REFUSED");
  CHECK(refused.value().phrases[1].detail == "injected refusal for a defective class");
  CHECK(std::filesystem::exists(fixture.preflight() / "report.json"));
  const auto blocked = authoring::advanceGenerationCampaign(fixture.repository, fixture.campaignPath,
      fixture.campaignSha256, "producer", "2026-09-14T00:00:02Z");
  CHECK(!blocked);
  if (!blocked) CHECK(blocked.error().message.find("preflight") != std::string::npos);
  CHECK(!std::filesystem::exists(fixture.campaignRoot / "batch-0"));
  CHECK(fixture.repository.recover().value().takes.empty());
  // A campaign whose own phrases all render audibly does advance, and commits takes
  // that still carry no approval.
  const auto passing = authoring::runInventoryPreflight(fixture.campaign, fixture.campaignSha256, fixture.root / "clean");
  CHECK(passing);
  if (!passing) return;
  auto cleanRoot = fixture.root / "clean-campaign";
  CHECK(std::filesystem::create_directory(cleanRoot));
  CHECK(core::durableAtomicWriteTextNew(cleanRoot / "campaign.json", fixture.campaign));
  CHECK(std::filesystem::create_directory(cleanRoot / "preflight"));
  CHECK(core::durableAtomicWriteTextNew(cleanRoot / "preflight" / "report.json", passing.value().json));
  const auto advanced = authoring::advanceGenerationCampaign(fixture.repository, cleanRoot / "campaign.json",
      fixture.campaignSha256, "producer", "2026-09-14T00:00:03Z");
  CHECK(advanced);
  if (!advanced) return;
  CHECK(advanced.value().complete);
  CHECK(advanced.value().completedBatches == advanced.value().totalBatches);
  const auto collected = fixture.repository.recover(); CHECK(collected);
  CHECK(collected.value().takes.size() == 3U);
  for (const auto& take : collected.value().takes) CHECK(take.state == production::UnitQueueState::MarkerReview);
}

TEST_CASE("a preflight report is admitted only in the campaign's own canonical form") {
  Fixture fixture;
  const auto report = authoring::runInventoryPreflight(fixture.campaign, fixture.campaignSha256,
      fixture.preflight(), {}, {},
      [](std::size_t index) -> std::optional<std::string> {
        return index == 0U ? std::optional<std::string>{"injected refusal"} : std::nullopt;
      });
  CHECK(report);
  if (!report) return;
  const auto parsed = formats::parseJson(report.value().json); CHECK(parsed);
  const auto verify = [&](const formats::JsonValue& value) {
    return authoring::verifyInventoryPreflight(formats::stringifyJson(value, true), fixture.campaign, fixture.campaignSha256);
  };
  // An unedited failing report is refused.
  CHECK(!verify(parsed.value()));
  // Rewriting only the status does not admit it.
  auto statusOnly = parsed.value();
  *statusOnly.find("status") = formats::JsonValue{"PASS"};
  CHECK(!verify(statusOnly));
  // Dropping a phrase the campaign's selection requires does not admit it.
  auto dropped = parsed.value();
  dropped.find("phrases")->asArray().pop_back();
  CHECK(!verify(dropped));
  // Relabeling a phrase onto another class does not admit it.
  auto relabeled = parsed.value();
  *relabeled.find("phrases")->asArray().front().find("coverageKey") = formats::JsonValue{"cv:k:a"};
  CHECK(!verify(relabeled));
  // A report that measured a different campaign is refused before its phrases are read.
  auto otherCampaign = parsed.value();
  *otherCampaign.find("campaignSha256") = formats::JsonValue{std::string(64U, 'b')};
  CHECK(!verify(otherCampaign));
  // And a campaign directory without the report at its admitted path is refused.
  CHECK(!authoring::verifyCampaignPreflight(fixture.campaignRoot / "absent", fixture.campaign, fixture.campaignSha256));
}

#if defined(SEAM_TEST_VOICEBANK_CLI) && (defined(__APPLE__) || defined(__linux__))
TEST_CASE("the CLI preflights a campaign into the directory its gate reads") {
  Fixture fixture;
  CHECK(runVoicebankCli({"preflight-generation-campaign", fixture.campaignPath.string(), fixture.campaignSha256}) == 0);
  CHECK(std::filesystem::exists(fixture.campaignRoot / "preflight" / "report.json"));
  CHECK(authoring::verifyCampaignPreflight(fixture.campaignRoot, fixture.campaign, fixture.campaignSha256));
  // Retained evidence is never replaced in place: a second run of the same command
  // fails rather than overwriting the report a campaign was admitted on.
  CHECK(runVoicebankCli({"preflight-generation-campaign", fixture.campaignPath.string(), fixture.campaignSha256}) != 0);
  CHECK(runVoicebankCli({"preflight-generation-campaign", fixture.campaignPath.string(), std::string(64U, '0')}) != 0);
}
#endif
