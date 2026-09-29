// Native Studio generation-campaign orchestration.
//
// The producer, recipe and audio here are synthetic fixtures: they prove the
// plan / advance / cancel / resume route through the repository owner, not that a
// useful singer or a qualified resource was produced.
#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/native_ui/voicebank_studio.hpp"
#include "seam/authoring/generation_campaign.hpp"
#include "seam/authoring/inventory_preflight.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank_production/project_codec.hpp"

#include <chrono>
#include <algorithm>
#include <string>
#include <thread>
#include <utility>
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

// The gate under test: a campaign cannot advance until the held-out phrases that
// exercise every class it declares have rendered audibly beside it.
void preflightCampaign(const std::filesystem::path& path, std::string_view sha,
                       const std::filesystem::path& directory) {
  const auto bytes = core::readTextFileLimited(path, 32U * 1024U * 1024U);
  CHECK(bytes);
  if (!bytes) return;
  const auto report = authoring::runInventoryPreflight(bytes.value(), sha, directory / "preflight");
  CHECK(report);
  if (report) CHECK(report.value().passed);
}
void preflightCampaign(const Controller& controller, const std::filesystem::path& directory) {
  preflightCampaign(controller.generationCampaignPath(), controller.generationCampaignSha256(), directory);
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
  preflightCampaign(fixture.controller, destination);
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
  preflightCampaign(fixture.controller, destination);
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

TEST_CASE("a fresh Studio controller can adopt and resume a persisted campaign") {
  Fixture fixture;
  const auto destination = fixture.root / "campaign-reopen";
  CHECK(fixture.controller.beginGenerationCampaignPlan(fixture.recipePath,
      {"take-sa", "take-sa-soft"}, destination, 1U));
  CHECK(drain(fixture.controller));
  const auto path = fixture.controller.generationCampaignPath();
  const auto sha = fixture.controller.generationCampaignSha256();
  CHECK(!path.empty());
  CHECK(sha.size() == 64U);

  // A restarted process has no in-memory campaign identity. Reopen the producer,
  // then explicitly adopt the retained campaign file with its verified digest.
  Controller reopened;
  CHECK(reopened.openProductionProject(fixture.workspace, fixture.project.inventorySha256, "producer"));
  CHECK(drain(reopened));
  CHECK(reopened.generationCampaignPath().empty());
  preflightCampaign(path, sha, destination);
  CHECK(reopened.beginGenerationCampaignAdvance(path, sha, "2026-09-24T00:00:06Z"));
  CHECK(drain(reopened));
  const auto progress = reopened.generationCampaignProgress(); CHECK(progress);
  CHECK(progress->phase == Phase::Complete);
  CHECK(progress->completedBatches == 2U);
  CHECK(reopened.generationCampaignPath() == path);
  production::ProductionProjectRepository repository{fixture.workspace};
  const auto durable = repository.recover(); CHECK(durable);
  CHECK(durable.value().takes.size() == 2U);
  CHECK(std::all_of(durable.value().takes.begin(), durable.value().takes.end(), [](const auto& take) {
    return take.state == production::UnitQueueState::MarkerReview;
  }));
}

TEST_CASE("the Studio request queue discovers, hash-checks and resumes durable generation requests") {
  Fixture fixture;
  const auto destination = fixture.root / "campaign-queued";
  CHECK(fixture.controller.beginGenerationCampaignPlan(fixture.recipePath,
      {"take-sa", "take-sa-soft"}, destination, 1U));
  CHECK(drain(fixture.controller));
  const auto path = fixture.controller.generationCampaignPath();
  const auto sha = fixture.controller.generationCampaignSha256();
  CHECK(!path.empty());
  CHECK(sha.size() == 64U);
  preflightCampaign(path, sha, destination);

  const auto submitted = authoring::submitGenerationCampaign(fixture.workspace, path, sha,
      "producer", "2026-09-28T00:00:07Z");
  CHECK(submitted);
  if (!submitted) return;
  CHECK(submitted.value().request.requestId == sha);
  CHECK(!submitted.value().terminal);

  // A fresh queue load is asynchronous and read-only; it discovers the retained
  // request without opening or changing the producer project.
  const auto before = fixture.controller.productionProject()->lastDurableGeneration;
  CHECK(fixture.controller.refreshGenerationRequests());
  CHECK(fixture.controller.proceduralImportBusy());
  CHECK(drain(fixture.controller));
  CHECK(fixture.controller.generationRequests().size() == 1U);
  CHECK(fixture.controller.generationRequests().front().request.requestId == sha);
  CHECK(!fixture.controller.generationRequests().front().terminal);
  CHECK(fixture.controller.generationRequestQueueStatus().find("1 REQUEST") != std::string_view::npos);
  CHECK(fixture.controller.productionProject()->lastDurableGeneration == before);
  const auto unpreparedPage = fixture.controller.beginGenerationRequestOutputInspection(sha, 0U, 1U);
  CHECK(unpreparedPage);
  CHECK(fixture.controller.proceduralImportBusy());
  CHECK(drain(fixture.controller));
  const auto* firstEvidence = fixture.controller.generationRequestOutputInspectionPage();
  CHECK(firstEvidence != nullptr);
  if (firstEvidence != nullptr) {
    CHECK(firstEvidence->requestId == sha);
    CHECK(firstEvidence->firstJob == 0U);
    CHECK(firstEvidence->jobs.size() == 1U);
    if (!firstEvidence->jobs.empty()) CHECK(firstEvidence->jobs.front().state ==
        authoring::GenerationJobInspectionState::NotPrepared);
  }
  CHECK(fixture.controller.generationRequestOutputInspectionStatus().find("INSPECTED 1 JOB") != std::string_view::npos);
  CHECK(!std::filesystem::exists(destination / "batch-0"));
  CHECK(fixture.controller.productionProject()->lastDurableGeneration == before);
  const auto invalidInspectionPage = fixture.controller.beginGenerationRequestOutputInspection(sha, 2U, 1U);
  CHECK(!invalidInspectionPage);
  const auto oversizedInspectionPage = fixture.controller.beginGenerationRequestOutputInspection(sha, 0U, 17U);
  CHECK(!oversizedInspectionPage);
  if (!oversizedInspectionPage) CHECK(oversizedInspectionPage.error().code == core::ErrorCode::InvalidArgument);
  using JobState = seam::native_ui::StudioGenerationJobState;
  const auto& queuedRequest = fixture.controller.generationRequests().front();
  const auto activeRender = Controller::GenerationCampaignProgress{
      .phase = Phase::Rendering, .completedBatches = 0U, .totalBatches = 2U,
      .completedOutputs = 0U, .totalOutputs = 1U};
  CHECK(seam::native_ui::studioGenerationJobState(queuedRequest, 0U, sha, activeRender) == JobState::Processing);
  CHECK(seam::native_ui::studioGenerationJobState(queuedRequest, 1U, sha, activeRender) == JobState::Queued);
  const auto outputReady = Controller::GenerationCampaignProgress{
      .phase = Phase::Collecting, .completedBatches = 0U, .totalBatches = 2U,
      .completedOutputs = 1U, .totalOutputs = 1U};
  CHECK(seam::native_ui::studioGenerationJobState(queuedRequest, 0U, sha, outputReady) == JobState::OutputReady);
  const auto nextBatch = Controller::GenerationCampaignProgress{
      .phase = Phase::Advancing, .completedBatches = 1U, .totalBatches = 2U};
  CHECK(seam::native_ui::studioGenerationJobState(queuedRequest, 0U, sha, nextBatch) == JobState::Collected);
  CHECK(seam::native_ui::studioGenerationJobState(queuedRequest, 1U, sha, nextBatch) == JobState::PreparingBatch);
  CHECK(seam::native_ui::studioGenerationJobState(queuedRequest, 0U, "another-request", activeRender) == JobState::Queued);
  const auto completeProgress = Controller::GenerationCampaignProgress{
      .phase = Phase::Complete, .completedBatches = 2U, .totalBatches = 2U};
  CHECK(seam::native_ui::studioGenerationJobState(queuedRequest, 0U, sha, completeProgress) == JobState::Collected);
  const auto inconsistentCompleteProgress = Controller::GenerationCampaignProgress{
      .phase = Phase::Complete, .completedBatches = 0U, .totalBatches = 2U};
  CHECK(seam::native_ui::studioGenerationJobState(queuedRequest, 0U, sha, inconsistentCompleteProgress) ==
      JobState::NotCollected);
  const auto pendingControls = seam::native_ui::studioGenerationQueueControls(
      fixture.controller, 1040.0, 720.0, false, 0U);
  CHECK(std::any_of(pendingControls.begin(), pendingControls.end(), [&](const auto& control) {
    return control.id == "inspect-request:" + sha && control.enabled;
  }));
  CHECK(seam::native_ui::studioGenerationRequestDetailVisibleRows(280.0) == 0U);
  CHECK(seam::native_ui::studioGenerationRequestDetailVisibleRows(320.0) == 0U);
  CHECK(seam::native_ui::studioGenerationRequestDetailVisibleRows(387.0) == 0U);
  CHECK(seam::native_ui::studioGenerationRequestDetailVisibleRows(388.0) == 1U);
  CHECK(seam::native_ui::studioGenerationRequestDetailVisibleRows(480.0) == 3U);
  CHECK(seam::native_ui::studioGenerationRequestDetailVisibleRows(720.0) == 8U);
  CHECK(seam::native_ui::studioGenerationOutputEvidenceStateLabel(
      authoring::GenerationJobInspectionState::NotPrepared) == "NOT PREPARED");
  CHECK(seam::native_ui::studioGenerationOutputEvidenceStateLabel(
      authoring::GenerationJobInspectionState::Incomplete) == "INCOMPLETE");
  CHECK(seam::native_ui::studioGenerationOutputEvidenceStateLabel(
      authoring::GenerationJobInspectionState::Prepared) == "PREPARED · NO OUTPUT");
  CHECK(seam::native_ui::studioGenerationOutputEvidenceStateLabel(
      authoring::GenerationJobInspectionState::NeedsRecovery) == "RECOVERY REQUIRED");
  CHECK(seam::native_ui::studioGenerationOutputEvidenceStateLabel(
      authoring::GenerationJobInspectionState::OutputVerified) == "OUTPUT VERIFIED");
  for (const auto [width, height] : {std::pair{720.0, 480.0}, std::pair{1040.0, 720.0},
                                    std::pair{1600.0, 900.0}}) {
    const auto controls = seam::native_ui::studioGenerationQueueControls(
        fixture.controller, width, height, false, 0U);
    const auto close = std::find_if(controls.begin(), controls.end(),
        [](const auto& control) { return control.id == "queue-close"; });
    const auto refresh = std::find_if(controls.begin(), controls.end(),
        [](const auto& control) { return control.id == "queue-refresh"; });
    CHECK(close != controls.end());
    CHECK(refresh != controls.end());
    if (close != controls.end() && refresh != controls.end()) CHECK(close->bounds.right() <= refresh->bounds.x);
    for (const auto& control : controls) {
      CHECK(control.bounds.x >= 294.0);
      CHECK(control.bounds.right() <= width - 280.0);
      CHECK(control.bounds.bottom() <= height - 32.0);
    }
    const auto details = seam::native_ui::studioGenerationQueueControls(
        fixture.controller, width, height, false, 0U, sha, 0U);
    const auto back = std::find_if(details.begin(), details.end(),
        [](const auto& control) { return control.id == "request-detail-back"; });
    const auto resume = std::find_if(details.begin(), details.end(), [&](const auto& control) {
      return control.id == "request-detail-resume:" + sha;
    });
    const auto locate = std::find_if(details.begin(), details.end(), [&](const auto& control) {
      return control.id == "request-detail-locate:" + sha;
    });
    const auto inspectOutputs = std::find_if(details.begin(), details.end(), [](const auto& control) {
      return control.id == "request-detail-inspect-outputs";
    });
    CHECK(back != details.end());
    CHECK(resume != details.end());
    CHECK(locate != details.end());
    CHECK(inspectOutputs != details.end());
    if (inspectOutputs != details.end()) {
      CHECK(inspectOutputs->enabled);
      CHECK(inspectOutputs->bounds.y == 108.0);
    }
    if (back != details.end() && resume != details.end()) CHECK(back->bounds.right() <= resume->bounds.x);
    if (resume != details.end() && locate != details.end()) CHECK(resume->bounds.right() <= locate->bounds.x);
    if (inspectOutputs != details.end()) CHECK(inspectOutputs->bounds.right() <= width - 44.0);
    for (const auto& control : details) {
      CHECK(control.bounds.x >= 44.0);
      CHECK(control.bounds.right() <= width - 44.0);
      CHECK(control.bounds.bottom() <= height - 32.0);
    }
  }
  {
    seam::native_ui::PixelSurface surface(1040U, 720U);
    surface.clear({0U, 0U, 0U, 255U});
    const auto beforePaint = surface.checksum();
    seam::native_ui::RasterCanvas canvas(surface);
    seam::native_ui::paintStudioGenerationRequestQueue(canvas, fixture.controller, 0U, sha, 0U);
    CHECK(surface.checksum() != beforePaint);
    // The first job card starts below both action rows and the request metadata.
    CHECK(surface.pixels()[285U * 1040U + 50U] == (seam::native_ui::Color{35U, 30U, 40U, 255U}.bgra()));
    CHECK(surface.pixels()[245U * 1040U + 50U] != (seam::native_ui::Color{35U, 30U, 40U, 255U}.bgra()));
  }

  const auto originalBytes = core::readTextFileLimited(path, 32U * 1024U * 1024U);
  CHECK(originalBytes);
  if (!originalBytes) return;
  // A user-selected file is still only a path: byte drift must be refused before
  // the canonical request runner can mutate the producer. A moved definition can
  // then resume the same immutable request after its stored locator disappears.
  const auto tamperedPath = fixture.root / "tampered-campaign.json";
  CHECK(core::durableAtomicWriteTextNew(tamperedPath, "not the admitted campaign definition"));
  CHECK(fixture.controller.beginGenerationRequestResumeFromDefinition(
      sha, tamperedPath, "2026-09-28T00:00:08Z"));
  const auto mismatch = drain(fixture.controller);
  CHECK(!mismatch);
  CHECK(mismatch.error().code == core::ErrorCode::Conflict);
  production::ProductionProjectRepository repository{fixture.workspace};
  auto unchanged = repository.recover(); CHECK(unchanged);
  CHECK(unchanged.value().lastDurableGeneration == before);
  CHECK(unchanged.value().takes.empty());

  const auto movedDirectory = fixture.root / "moved-campaign";
  std::error_code moveCopyError;
  std::filesystem::copy(path.parent_path(), movedDirectory, std::filesystem::copy_options::recursive, moveCopyError);
  CHECK(!moveCopyError);
  const auto movedPath = movedDirectory / path.filename();
  CHECK(std::filesystem::exists(movedPath));
  const auto movedDefinition = core::readTextFileLimited(movedPath, 32U * 1024U * 1024U);
  CHECK(movedDefinition);
  if (movedDefinition) CHECK(movedDefinition.value() == originalBytes.value());
  CHECK(std::filesystem::remove(path));
  CHECK(fixture.controller.beginGenerationRequestResume(sha, "2026-09-28T00:00:09Z"));
  const auto missingLocatorResult = drain(fixture.controller);
  CHECK(!missingLocatorResult);
  const auto afterMissingLocator = repository.recover(); CHECK(afterMissingLocator);
  CHECK(afterMissingLocator.value().lastDurableGeneration == before);
  CHECK(afterMissingLocator.value().takes.empty());
  CHECK(fixture.controller.beginGenerationRequestResumeFromDefinition(
      sha, movedPath, "2026-09-28T00:00:10Z"));
  CHECK(drain(fixture.controller));
  auto completed = repository.recover(); CHECK(completed);
  CHECK(completed.value().takes.size() == 2U);
  CHECK(completed.value().lastDurableGeneration == before + 2U);
  CHECK(std::all_of(completed.value().takes.begin(), completed.value().takes.end(), [](const auto& take) {
    return take.state == production::UnitQueueState::MarkerReview;
  }));

  CHECK(fixture.controller.refreshGenerationRequests());
  CHECK(drain(fixture.controller));
  CHECK(fixture.controller.generationRequests().size() == 1U);
  CHECK(fixture.controller.generationRequests().front().terminal.has_value());
  CHECK(fixture.controller.generationRequests().front().terminal->outcome ==
      production::GenerationRequestOutcome::Completed);
  const auto& completedRequest = fixture.controller.generationRequests().front();
  CHECK(fixture.controller.beginGenerationRequestOutputInspection(sha, 0U, 8U, movedPath));
  CHECK(drain(fixture.controller));
  const auto* completedEvidence = fixture.controller.generationRequestOutputInspectionPage();
  CHECK(completedEvidence != nullptr);
  if (completedEvidence != nullptr) {
    CHECK(completedEvidence->jobs.size() == 2U);
    for (const auto& evidence : completedEvidence->jobs)
      if (evidence.state != authoring::GenerationJobInspectionState::OutputVerified)
        throw test::Failure{"Completed request output inspection was not verified: " + evidence.diagnostic};
  }
  CHECK(std::all_of(fixture.controller.productionProject()->takes.begin(),
      fixture.controller.productionProject()->takes.end(), [](const auto& take) {
        return take.state == production::UnitQueueState::MarkerReview;
      }));
  for (std::size_t index = 0U; index < completedRequest.request.jobs.size(); ++index)
    CHECK(seam::native_ui::studioGenerationJobState(completedRequest, index, sha, std::nullopt) == JobState::Collected);
  CHECK(seam::native_ui::studioGenerationJobStateLabel(JobState::Collected).find("REVIEW SEPARATE") !=
      std::string_view::npos);
  const auto terminalResume = fixture.controller.beginGenerationRequestResume(sha);
  CHECK(!terminalResume);
  CHECK(terminalResume.error().code == core::ErrorCode::InvalidState);
  const auto terminalLocatedResume = fixture.controller.beginGenerationRequestResumeFromDefinition(sha, movedPath);
  CHECK(!terminalLocatedResume);
  CHECK(terminalLocatedResume.error().code == core::ErrorCode::InvalidState);
  const auto terminalControls = seam::native_ui::studioGenerationQueueControls(
      fixture.controller, 1040.0, 720.0, false, 0U);
  CHECK(std::any_of(terminalControls.begin(), terminalControls.end(), [&](const auto& control) {
    return control.id == "inspect-request:" + sha && control.enabled;
  }));
  const auto terminalDetails = seam::native_ui::studioGenerationQueueControls(
      fixture.controller, 1040.0, 720.0, false, 0U, sha, 0U);
  CHECK(std::any_of(terminalDetails.begin(), terminalDetails.end(), [](const auto& control) {
    return control.id == "request-detail-back";
  }));
  CHECK(std::none_of(terminalDetails.begin(), terminalDetails.end(), [](const auto& control) {
    return control.id.starts_with("request-detail-resume:");
  }));
  CHECK(std::none_of(terminalDetails.begin(), terminalDetails.end(), [](const auto& control) {
    return control.id.starts_with("request-detail-locate:");
  }));
}

TEST_CASE("campaign controls appear only when the producer and identity allow them") {
  Fixture fixture;
  const auto byId = [](const auto& controls, std::string_view id) {
    return std::find_if(controls.begin(), controls.end(),
        [&](const auto& control) { return control.id == id; });
  };
  const auto idle = seam::native_ui::studioGenerationControls(fixture.controller, 1040.0, false);
  CHECK(idle.size() == 7U);
  CHECK(byId(idle, "plan-campaign") != idle.end());
  CHECK(byId(idle, "plan-campaign")->enabled);
  // A persisted campaign can be selected after restart, even before this
  // controller has adopted its identity.
  CHECK(byId(idle, "run-campaign") != idle.end());
  CHECK(byId(idle, "run-campaign")->enabled);
  CHECK(byId(idle, "run-campaign")->label == "Open / resume");
  for (const auto& control : seam::native_ui::studioGenerationControls(fixture.controller, 1040.0, true)) {
    CHECK(!control.enabled);
  }
  // Planning records the identity and changes the action to in-memory resume.
  CHECK(fixture.controller.beginGenerationCampaignPlan(fixture.recipePath,
      {"take-sa", "take-sa-soft"}, fixture.root / "campaign-controls", 1U));
  CHECK(drain(fixture.controller));
  const auto planned = seam::native_ui::studioGenerationControls(fixture.controller, 1040.0, false);
  CHECK(byId(planned, "run-campaign")->enabled);
  CHECK(byId(planned, "run-campaign")->label == "Resume campaign");
  // The campaign row sits below the single-job rows and does not collide with
  // them, and it stays inside the panel for the narrowest supported width.
  CHECK(byId(planned, "plan-campaign")->bounds.y > byId(planned, "assemble")->bounds.y);
  CHECK(byId(planned, "plan-campaign")->bounds.y == byId(planned, "run-campaign")->bounds.y);
  CHECK(byId(planned, "plan-campaign")->bounds.x + byId(planned, "plan-campaign")->bounds.width <=
      byId(planned, "run-campaign")->bounds.x);
  for (const auto width : {720.0, 1040.0, 1600.0}) {
    for (const auto& control : seam::native_ui::studioGenerationControls(fixture.controller, width, false)) {
      CHECK(control.bounds.x + control.bounds.width <= width - 280.0);
      CHECK(control.bounds.y + control.bounds.height <= 340.0);
    }
  }
}
