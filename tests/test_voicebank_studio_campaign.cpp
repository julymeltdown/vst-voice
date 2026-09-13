// Native Studio generation-campaign orchestration.
//
// The producer, recipe and audio here are synthetic fixtures: they prove the
// plan / advance / cancel / resume route through the repository owner, not that a
// useful singer or a qualified resource was produced.
#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/native_ui/voicebank_studio.hpp"
#include "seam/authoring/generation_campaign.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank_production/project_codec.hpp"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace seam;
namespace production = voicebank_production;
using Controller = native_ui::VoicebankStudioController;
using Phase = Controller::GenerationCampaignProgress::Phase;

core::Result<void> drain(Controller& controller) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{60};
  while (controller.proceduralImportBusy()) {
    auto result = controller.pollProceduralCandidateImport();
    if (!result) return result;
    if (std::chrono::steady_clock::now() >= deadline)
      throw seam::test::Failure{"Studio campaign worker did not finish"};
    if (controller.proceduralImportBusy()) std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return core::success();
}

voice_design::VoiceRecipe campaignRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "studio-campaign";
  recipe.poses = {{"a", "neutral", 0.0, {{700.0, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  recipe.frications = {{"s", "neutral", {.seed = 42U}}};
  auto softPose = recipe.poses.front();
  softPose.style = "soft";
  recipe.poses.push_back(softPose);
  auto softNoise = recipe.frications.front();
  softNoise.style = "soft";
  recipe.frications.push_back(softNoise);
  return recipe;
}

struct Fixture final {
  std::filesystem::path root{test::support::temporaryDirectory("studio-campaign")};
  std::filesystem::path workspace{root / "producer"};
  std::filesystem::path recipePath{root / "recipe.json"};
  production::VoicebankProductionProject project;
  Controller controller;

  explicit Fixture() {
    const auto license = root / "fixture-license.txt";
    CHECK(core::durableAtomicWriteTextNew(license, "Synthetic native campaign test only; no singer or Beta qualification."));
    const auto licenseHash = core::sha256File(license); CHECK(licenseHash);
    project = {.projectId = "native-campaign", .inventoryId = "fixture", .inventorySha256 = std::string(64U, 'a'),
        .selectedSourceStrategyId = "fixture", .licenseLocator = license.string(),
        .licenseSha256 = licenseHash.value(), .immutableAssetRoot = "assets"};
    project.schemaVersion = production::kProductionStyleSchemaVersion;
    project.language = "ja";
    project.sourceStrategies = {{.id = "fixture", .kind = production::SourceStrategyKind::ProceduralSynthesis,
        .rights = production::Feasibility::Pass, .coverage = production::Feasibility::Pass, .listening = production::Feasibility::Pass,
        .permissions = {true, true, true, true}, .licenseLocator = license.string(), .licenseSha256 = licenseHash.value(),
        .evidenceState = "SYNTHETIC_TEST_ONLY"}};
    project.operators = {{"producer", "PRODUCER"}};
    project.unitAssignments = {{.coverageKey = "cv:s:a", .pitchLayer = 69, .promptId = "prompt-sa",
        .plannedTakeId = "take-sa", .style = "neutral"}};
    auto soft = project.unitAssignments.front();
    soft.style = "soft"; soft.plannedTakeId = "take-sa-soft"; soft.promptId = "prompt-sa-soft";
    project.unitAssignments.push_back(soft);
    production::ProductionProjectRepository repository{workspace};
    CHECK(repository.initialize(project, {.action = "create", .subjectId = project.projectId,
        .operatorId = "producer", .occurredAtUtc = "2026-09-14T00:00:00Z"}));
    CHECK(voice_design::saveVoiceRecipeFile(recipePath, campaignRecipe()));
    CHECK(controller.openProductionProject(workspace, project.inventorySha256, "producer"));
    const auto opened = drain(controller);
    if (!opened) throw seam::test::Failure{"Studio workspace open failed: " + opened.error().message};
    if (!controller.productionProject()) throw seam::test::Failure{"Studio workspace did not adopt the producer"};
  }
};

}  // namespace

TEST_CASE("a Studio campaign is planned into a new directory and refuses to replace one") {
  Fixture fixture;
  const auto destination = fixture.root / "campaign-planned";
  CHECK(fixture.controller.beginGenerationCampaignPlan(fixture.recipePath,
      {"take-sa", "take-sa-soft"}, destination, 1U));
  const auto drained = drain(fixture.controller); CHECK(drained);
  const auto progress = fixture.controller.generationCampaignProgress(); CHECK(progress);
  CHECK(progress->phase == Phase::Planned);
  CHECK(progress->totalBatches == 2U);
  CHECK(progress->completedBatches == 0U);
  CHECK(fixture.controller.generationCampaignPath() == destination / "campaign.json");
  CHECK(fixture.controller.generationCampaignSha256().size() == 64U);
  const auto bytes = core::readTextFileLimited(destination / "campaign.json", 32U * 1024U * 1024U); CHECK(bytes);
  CHECK(core::sha256Hex(bytes.value()) == fixture.controller.generationCampaignSha256());
  const auto parsed = formats::parseJson(bytes.value()); CHECK(parsed);
  CHECK(parsed.value().find("status")->asString() == "PLANNED_UNPREPARED");
  CHECK(parsed.value().find("batchCount")->asInt64() == 2);
  // Planning never renders or collects, and the producer state is untouched.
  CHECK(fixture.controller.productionProject()->takes.empty());
  production::ProductionProjectRepository repository{fixture.workspace};
  const auto durable = repository.recover(); CHECK(durable);
  CHECK(durable.value().takes.empty());
  CHECK(durable.value().lastDurableGeneration == fixture.project.lastDurableGeneration);
  // The published definition is never replaced: a second plan into the same
  // directory is refused, and a stored campaign is advanced or resumed instead.
  const auto replay = fixture.controller.beginGenerationCampaignPlan(fixture.recipePath,
      {"take-sa"}, destination, 1U);
  CHECK(!replay);
  CHECK(replay.error().code == core::ErrorCode::Conflict);
  CHECK(fixture.controller.beginGenerationCampaignPlan({}, {"take-sa"}, fixture.root / "empty", 1U).error().code ==
      core::ErrorCode::InvalidArgument);
  CHECK(fixture.controller.beginGenerationCampaignPlan(fixture.recipePath, {}, fixture.root / "empty", 1U).error().code ==
      core::ErrorCode::InvalidArgument);
  CHECK(!std::filesystem::exists(fixture.root / "empty"));
}

TEST_CASE("campaign advancement commits every batch and adopts the recovered producer") {
  Fixture fixture;
  const auto destination = fixture.root / "campaign-run";
  CHECK(fixture.controller.beginGenerationCampaignPlan(fixture.recipePath,
      {"take-sa", "take-sa-soft"}, destination, 1U));
  CHECK(drain(fixture.controller));
  const auto path = fixture.controller.generationCampaignPath();
  const auto sha = fixture.controller.generationCampaignSha256();
  production::ProductionProjectRepository repository{fixture.workspace};
  const auto plannedState = repository.recover(); CHECK(plannedState);
  const auto beforeGeneration = plannedState.value().lastDurableGeneration;
  CHECK(fixture.controller.beginGenerationCampaignAdvance(path, sha, "2026-09-14T00:00:01Z"));
  const auto advanced = drain(fixture.controller); CHECK(advanced);
  const auto progress = fixture.controller.generationCampaignProgress(); CHECK(progress);
  CHECK(progress->phase == Phase::Complete);
  CHECK(progress->completedBatches == 2U);
  CHECK(progress->totalBatches == 2U);
  // The controller adopted the committed producer, and every collected take is
  // unapproved marker-review material rather than a finished resource.
  CHECK(fixture.controller.productionProject());
  CHECK(fixture.controller.productionProject()->takes.size() == 2U);
  for (const auto& take : fixture.controller.productionProject()->takes) {
    CHECK(take.state == production::UnitQueueState::MarkerReview);
  }
  CHECK(fixture.controller.status().find("CAMPAIGN COLLECTED") != std::string::npos);
  const auto durable = repository.recover(); CHECK(durable);
  CHECK(durable.value().takes.size() == 2U);
  CHECK(durable.value().lastDurableGeneration == beforeGeneration + 2U);
  // Advancing a completed campaign is idempotent: the same receipts verify and
  // no further generation is committed.
  CHECK(fixture.controller.beginGenerationCampaignAdvance(path, sha, "2026-09-14T00:00:05Z"));
  CHECK(drain(fixture.controller));
  const auto repeated = repository.recover(); CHECK(repeated);
  CHECK(repeated.value().takes.size() == 2U);
  CHECK(repeated.value().lastDurableGeneration == durable.value().lastDurableGeneration);
}

TEST_CASE("a cancelled campaign keeps its retained batches and resumes from its own receipts") {
  Fixture fixture;
  const auto destination = fixture.root / "campaign-cancel";
  CHECK(fixture.controller.beginGenerationCampaignPlan(fixture.recipePath,
      {"take-sa", "take-sa-soft"}, destination, 1U));
  CHECK(drain(fixture.controller));
  const auto path = fixture.controller.generationCampaignPath();
  const auto sha = fixture.controller.generationCampaignSha256();
  CHECK(fixture.controller.beginGenerationCampaignAdvance(path, sha, "2026-09-14T00:00:02Z"));
  // The stop request may land before or after the first batch commits; both are
  // legal, so the test asserts the contract rather than a race outcome.
  fixture.controller.cancelGenerationCampaign();
  const auto stopped = drain(fixture.controller);
  const auto progress = fixture.controller.generationCampaignProgress(); CHECK(progress);
  CHECK(progress->totalBatches == 2U);
  CHECK(progress->completedBatches <= progress->totalBatches);
  if (!stopped) {
    CHECK(progress->phase == Phase::Cancelled);
    CHECK(stopped.error().code == core::ErrorCode::Conflict);
    // Retained batches stay committed, the identity is unchanged, and resuming
    // continues from the receipts instead of re-planning.
    production::ProductionProjectRepository repository{fixture.workspace};
    const auto retained = repository.recover(); CHECK(retained);
    CHECK(retained.value().takes.size() == progress->completedBatches);
    CHECK(fixture.controller.generationCampaignPath() == path);
    CHECK(fixture.controller.generationCampaignSha256() == sha);
    CHECK(fixture.controller.beginGenerationCampaignResume("2026-09-14T00:00:03Z"));
    const auto resumed = drain(fixture.controller); CHECK(resumed);
    const auto completed = fixture.controller.generationCampaignProgress(); CHECK(completed);
    CHECK(completed->phase == Phase::Complete);
    CHECK(completed->completedBatches == 2U);
    const auto after = repository.recover(); CHECK(after);
    CHECK(after.value().takes.size() == 2U);
  } else {
    CHECK(progress->phase == Phase::Complete);
    CHECK(progress->completedBatches == 2U);
  }
  // A campaign identity is required to resume, and a wrong digest is refused by
  // the service rather than advancing a different definition.
  Controller fresh;
  const auto noIdentity = fresh.beginGenerationCampaignResume();
  CHECK(!noIdentity);
  CHECK(noIdentity.error().code == core::ErrorCode::InvalidState);
  CHECK(fixture.controller.beginGenerationCampaignAdvance(path, std::string(64U, '0'), "2026-09-14T00:00:04Z"));
  const auto refused = drain(fixture.controller);
  CHECK(!refused);
  CHECK(refused.error().code == core::ErrorCode::Conflict);
  const auto failed = fixture.controller.generationCampaignProgress(); CHECK(failed);
  CHECK(failed->phase == Phase::Failed);
}
