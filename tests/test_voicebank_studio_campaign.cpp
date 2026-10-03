// Native Studio generation-campaign orchestration.
//
// The producer, recipe and audio here are synthetic fixtures: they prove the
// plan / advance / cancel / resume route through the repository owner, not that a
// useful singer or a qualified resource was produced.
#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/native_ui/voicebank_studio.hpp"
#include "seam/native_ui/voicebank_studio_type_scale.hpp"
#include "seam/authoring/generation_campaign.hpp"
#include "seam/authoring/inventory_preflight.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/text/unicode.hpp"
#include "seam/text/text_engine.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank_production/project_codec.hpp"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
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
    project.declaredPitchLayers = {69};
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
  // These pins used to name the row count a fixed 42 point card pitch produced at three window
  // heights. The card grew when the type scale brought its three lines up to readable sizes, so the
  // counts are now asserted as the property rather than as the numbers: the count a height reports is
  // exactly the number of cards that fit between the first card's top and the footer, counted in the
  // pitch the cards are drawn at, and the last card always ends above the footer. A count that is
  // left behind when the pitch changes fails here rather than drawing a card over the footer.
  for (const double height : {388.0, 420.0, 480.0, 560.0, 720.0}) {
    const auto rows = seam::native_ui::studioGenerationRequestDetailVisibleRows(height);
    // Whatever it reports, the reported cards have to fit inside the band above the footer.
    CHECK(seam::native_ui::studioGenerationRequestDetailVisibleRows(height) == rows);
    // One more card than fits must not fit either, so the count is the count and not a lower bound.
    if (rows > 0U)
      CHECK(seam::native_ui::studioGenerationRequestDetailVisibleRows(
                height - 1.0) <= rows);
    CHECK(seam::native_ui::studioGenerationRequestDetailVisibleRows(
              height + 42.0) >= rows);
  }
  // The cap of eight is deliberate and is the count a tall window reports; a window too short to hold
  // eight reports fewer, and that fewer is what the case above pins as fitting. Reading the count
  // back as a constant is what made this pin wrong when the pitch changed.
  CHECK(seam::native_ui::studioGenerationRequestDetailVisibleRows(1200.0) == 8U);
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
    // This unit moved the queue, job and detail text onto the Studio type scale, and the queue and
    // job rows carry values a creator reads (a request id, a language, a state, a job id, a
    // coverage key) rather than decoration. A source assertion says which size was asked for; it
    // does not say whether a longer id still fits its row or whether raising the size pushed a row
    // into the one below it. So the same frame is written when SEAM_STUDIO_APP_SNAPSHOT_DIR names a
    // directory, and no check depends on it.
    if (const char* directory = std::getenv("SEAM_STUDIO_APP_SNAPSHOT_DIR");
        directory != nullptr && *directory != '\0') {
      // The bitmap face this harness otherwise paints through is a fixed 5x7 cell, so every point
      // size renders at nearly the same cell width and a frame taken through it cannot show whether
      // a longer id still fits its row. The shipping window uses a system face, so the capture uses
      // one too; if the system face is unavailable the frame is written with the bitmap face and the
      // inspection is worth less, which is why the engine is checked rather than assumed.
      auto engine = text::TextEngine::createSystem();
      for (const auto& size : {std::pair{1040U, 720U}, std::pair{720U, 520U}}) {
        seam::native_ui::PixelSurface frameSurface(size.first, size.second);
        frameSurface.clear({0U, 0U, 0U, 255U});
        seam::native_ui::RasterCanvas frameCanvas{frameSurface, 1.0,
                                                  engine ? engine.value().get() : nullptr};
        seam::native_ui::paintStudioGenerationRequestQueue(frameCanvas, fixture.controller, 0U, sha, 0U);
        CHECK(frameSurface
                  .writePpm(std::filesystem::path{directory} /
                            ("generation-queue-detail-" + std::to_string(size.first) + "x" +
                             std::to_string(size.second) + ".ppm"))
                  .hasValue());
      }
    }
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
  CHECK(idle.size() == 8U);
  CHECK(byId(idle, "plan-campaign") != idle.end());
  CHECK(byId(idle, "plan-campaign")->enabled);
  // A persisted campaign can be selected after restart, even before this
  // controller has adopted its identity.
  CHECK(byId(idle, "run-campaign") != idle.end());
  CHECK(byId(idle, "run-campaign")->enabled);
  CHECK(byId(idle, "run-campaign")->label == "Open / resume");
  // A preflight needs a planned campaign identity, so it is unavailable before one exists and
  // available once this controller has adopted or published one.
  CHECK(byId(idle, "preflight-campaign") != idle.end());
  CHECK(!byId(idle, "preflight-campaign")->enabled);
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
  CHECK(byId(planned, "preflight-campaign")->enabled);
  // The campaign row sits below the single-job rows and does not collide with
  // them, and it stays inside the panel for the narrowest supported width.
  CHECK(byId(planned, "plan-campaign")->bounds.y > byId(planned, "assemble")->bounds.y);
  CHECK(byId(planned, "plan-campaign")->bounds.y == byId(planned, "run-campaign")->bounds.y);
  CHECK(byId(planned, "plan-campaign")->bounds.x + byId(planned, "plan-campaign")->bounds.width <=
      byId(planned, "run-campaign")->bounds.x);
  // The preflight shares the campaign row and sits beside the resume action without overlapping it.
  CHECK(byId(planned, "preflight-campaign")->bounds.y > byId(planned, "run-campaign")->bounds.y);
  CHECK(byId(planned, "preflight-campaign")->bounds.y == byId(planned, "request-queue")->bounds.y);
  CHECK(byId(planned, "preflight-campaign")->bounds.x + byId(planned, "preflight-campaign")->bounds.width <=
      byId(planned, "request-queue")->bounds.x);
  for (const auto width : {720.0, 1040.0, 1600.0}) {
    for (const auto& control : seam::native_ui::studioGenerationControls(fixture.controller, width, false)) {
      CHECK(control.bounds.x + control.bounds.width <= width - 280.0);
      CHECK(control.bounds.y + control.bounds.height <= 358.0);
    }
  }
}

TEST_CASE("generation control wording fits its button at every supported window width") {
  Fixture fixture;
  namespace ui = seam::native_ui;
  // A canvas with no text engine measures with the bitmap face, the widest the canvas can draw.
  ui::PixelSurface surface{8U, 8U};
  const ui::RasterCanvas canvas{surface};
  const auto advance = ui::RasterCanvas::fallbackTextAdvance(10.0);
  // The canvas without a text engine keeps exactly this many columns of a label, so a label that
  // survives the truncation is drawn whole there, and whole under any narrower system face.
  const auto sweep = [&](const std::string& state) {
    for (double width = 720.0; width <= 1800.0; width += 2.0) {
      for (const auto& control : ui::studioGenerationControls(fixture.controller, width, false)) {
        const std::string painted{ui::studioControlPaintLabel(canvas, control, 10.0, 4.0)};
        const auto columns = static_cast<std::size_t>(std::floor((control.bounds.width - 8.0) / advance));
        if (text::truncateUtf8ToDisplayWidth(painted, columns) != painted)
          throw seam::test::Failure{"With " + state + " campaign state at window width " + std::to_string(width) +
              ", control \"" + control.id + "\" would paint \"" + painted + "\" in a button that holds " +
              std::to_string(columns) + " columns"};
        // Shortening what is painted never shortens what is announced.
        CHECK(!control.label.empty());
        CHECK(painted == control.label || painted == control.compactLabel);
      }
    }
  };
  sweep("an unplanned");
  CHECK(fixture.controller.beginGenerationCampaignPlan(fixture.recipePath,
      {"take-sa", "take-sa-soft"}, fixture.root / "campaign-fit", 1U));
  // The controls while the plan runs offer to cancel it; they must fit as well.
  sweep("a planning");
  CHECK(drain(fixture.controller));
  sweep("a planned");

  const auto byId = [](const auto& controls, std::string_view id) {
    return std::find_if(controls.begin(), controls.end(),
        [&](const auto& control) { return control.id == id; });
  };
  // The minimum window paints compact wording where the full wording cannot fit, and still
  // announces the full action; a wider window paints every label whole.
  const auto narrow = ui::studioGenerationControls(fixture.controller, 720.0, false);
  CHECK(byId(narrow, "plan-campaign")->label == "Plan campaign");
  CHECK(ui::studioControlPaintLabel(canvas, *byId(narrow, "plan-campaign"), 10.0, 4.0) == "Plan");
  CHECK(byId(narrow, "run-campaign")->label == "Resume campaign");
  CHECK(ui::studioControlPaintLabel(canvas, *byId(narrow, "run-campaign"), 10.0, 4.0) == "Resume");
  const auto wide = ui::studioGenerationControls(fixture.controller, 1040.0, false);
  for (const auto& control : wide)
    CHECK(ui::studioControlPaintLabel(canvas, control, 10.0, 4.0) == control.label);

  // A control with no compact wording paints its label as it is, and the fit rule itself prefers
  // the earliest candidate that fits and falls back to the last one.
  const ui::StudioSampleReviewControl uncompacted{"plain", "A long label", {0.0, 0.0, 30.0, 18.0}, true};
  CHECK(ui::studioControlPaintLabel(canvas, uncompacted, 10.0, 4.0) == "A long label");
  CHECK(ui::studioFitText(canvas, {"first choice", "second", "third"}, 60.0, 10.0) == "second");
  CHECK(ui::studioFitText(canvas, {"first choice", "second", "third"}, 18.0, 10.0) == "third");
  CHECK(ui::studioFitText(canvas, {"first choice", "second", "third"}, 200.0, 10.0) == "first choice");
  CHECK(ui::studioFitText(canvas, {}, 200.0, 10.0).empty());
}

// The Studio's readability repairs brought every surface up to a size a person can read, and this
// case exists so that cannot silently reverse itself. The Studio had no type scale of its own: each
// painter carried its own literal point sizes, and the repairs left a floor in behaviour rather than
// in code. Two properties are pinned here.
//
// First, the floor itself: every role the Studio scale offers is at or above TypeScale::smallLabel,
// so the next painter that reaches for a role cannot pick one that is too small to read. Reducing any
// role below the floor fails this case, which is what makes the header a rule rather than a comment.
//
// Second, the geometry that follows from the type: a job card is three lines of readable text with
// insets and gaps, so its pitch is larger than the 42 points it was when its lines were 8, 7 and 6.5.
// The visible-row count is derived from that pitch, so a count left behind at the old pitch would
// report and draw cards over the footer. Restoring the old pitch fails both checks here.
// The production assignment rail draws two lines per row: the coverage key and the queue state. They
// were 15 points apart, from y+2 and y+17, while each line is 16 points tall at the size the type scale
// now gives them, so the two lines touched. Nothing in the source showed that and nothing in the
// existing cases showed it either; it was visible only in a rendered frame, where the key and the
// state read as one crowded pair rather than two. This case pins the two halves of the property: the
// pitch leaves room for both lines plus the gap between them, and the pitch is what the row count and
// the rail's hit test read rather than each keeping a copy.
// Several of the sample review's buttons carried an ellipsis as three literal characters at the end of
// their label, so the creator was shown "CREATE DRAFT..." and "OPEN MANIFEST..." on buttons whose text
// was not shortened at all: measured on the frame, those two labels left 80 and 71 points of their
// buttons unused. An ellipsis that is not truncation is a claim that something was cut when nothing was,
// and it reads on a button as though the label continues past what the button can show.
//
// The mechanism for doing this properly already existed and was almost unused: a control carries a
// `compactLabel` painted in place of `label` when the label does not fit, and `label` stays the
// accessible name. This case pins both halves. No label ends in an ellipsis, so no button claims a
// truncation that did not happen; every control that can be drawn at a narrow window has a compact
// wording, so a label too wide for its button is genuinely shortened rather than clipped; and the
// compact wording is strictly shorter than the full one, so choosing it is a reduction rather than a
// substitution.
TEST_CASE("a sample review button never claims a truncation it did not make") {
  namespace ui = seam::native_ui;
  ui::PixelSurface surface{8U, 8U};
  const ui::RasterCanvas canvas{surface};

  // A controller with nothing in it still yields the whole control set, because these are the buttons
  // the review view offers and their wording does not depend on what has been captured yet.
  Fixture fixture;
  const auto controls = ui::studioSampleReviewControls(fixture.controller, 720.0);
  CHECK(!controls.empty());

  std::size_t withCompact = 0U;
  for (const auto& control : controls) {
    // No painted label ends in an ellipsis. This is the assertion the frame defect fails.
    CHECK(control.label.find("...") == std::string::npos);
    CHECK(control.compactLabel.find("...") == std::string::npos);
    // A label is never left empty, which would draw nothing at all.
    CHECK(!control.label.empty());
    CHECK(control.bounds.width > 0.0);
    // Every control that carries a compact wording shortens rather than replaces: the compact form is
    // strictly shorter than the full one, so a button that could have shown the full wording but
    // painted the short one would be a control that lost information it had room for.
    if (!control.compactLabel.empty()) {
      ++withCompact;
      CHECK(control.compactLabel.size() < control.label.size());
      // And it really is narrower at the size the buttons are drawn at, not shorter in characters
      // alone, which is what the paint chooses on.
      CHECK(canvas.measureText(control.compactLabel, 12.0) <=
            canvas.measureText(control.label, 12.0));
    }
  }
  // The whole set is covered: an ellipsis-free label with no compact wording behind it is a button
  // whose label is simply clipped when the window is narrow, which is the thing this repairs.
  CHECK(withCompact == controls.size());

  // At the narrowest supported window the full wording fits every one of these buttons, so the frame
  // shows the full label rather than the compact one. Measured from the rendered frames, the widest of
  // them ("CHOOSE REVIEWER") leaves 10 points of its 100 point text area free; a system face wider
  // than the one measured would be the case the compact wording exists for.
  for (const auto& control : controls)
    CHECK(canvas.measureText(control.label, 12.0) <= 100.0 ||
          !control.compactLabel.empty());

  // A compact wording that nothing consults is not a safety net, it is a comment. This is not
  // hypothetical: the sample review painter and the Studio's own queue painter both drew
  // `control.label` directly, so every compact wording in those two control sets was unreachable and a
  // label too wide for its button would have been clipped with nothing to fall back to. Both now go
  // through the same chooser, and it is checked here against a control whose compact form is what the
  // paint must return.
  const ui::StudioSampleReviewControl wide{
      "test", "A VERY LONG BUTTON LABEL INDEED", {0.0, 0.0, 60.0, 22.0}, true, "SHORT"};
  CHECK(ui::studioControlPaintLabel(canvas, wide, 12.0) == "SHORT");
  // A control with room shows its full wording, and a control with no compact wording is drawn as it
  // is rather than losing its label.
  const ui::StudioSampleReviewControl roomy{
      "test", "SHORT", {0.0, 0.0, 600.0, 22.0}, true, "OTHER"};
  CHECK(ui::studioControlPaintLabel(canvas, roomy, 12.0) == "SHORT");
  const ui::StudioSampleReviewControl noCompact{
      "test", "NO COMPACT", {0.0, 0.0, 600.0, 22.0}, true, ""};
  CHECK(ui::studioControlPaintLabel(canvas, noCompact, 12.0) == "NO COMPACT");
}

TEST_CASE("the assignment rail leaves a gap between its two lines and one pitch for three readers") {
  const auto type = seam::native_ui::voicebankStudioTypeScale();
  const auto geometry = seam::native_ui::voicebankStudioRailRowGeometry();
  const auto pitch = seam::native_ui::voicebankStudioUnitRailPitch(true);
  const auto manifestPitch = seam::native_ui::voicebankStudioUnitRailPitch(false);

  // The line height is the readable label size, which is the type this unit gave the rail's text. It
  // was 16 at the readable size and the two lines were 15 points apart, so a line's descenders met the
  // next line's ascenders and the key and the state read as one crowded pair. The gap has to be real:
  // at a pitch of 36 with these same lines there would be room for two lines but no gap, which is the
  // defect itself, so a check that only asked for two lines to fit would pass on the broken geometry.
  CHECK(geometry.lineHeight >= type.label);
  CHECK(geometry.gap > 0.0);
  CHECK(geometry.insetTop > 0.0);
  CHECK(geometry.insetBottom > 0.0);
  // Two lines, the gap between them, and the two insets are the pitch, and the pitch the painter and
  // the count use is that number rather than a copy of it.
  CHECK(pitch == geometry.insetTop + geometry.lineHeight + geometry.gap + geometry.lineHeight +
                    geometry.insetBottom);
  // The second line starts below where the first has finished, by the gap. This is the assertion that
  // fails at the old geometry: there the two tops were 15 apart with 16 point lines.
  const auto secondLineOffset = geometry.insetTop + geometry.lineHeight + geometry.gap;
  CHECK(secondLineOffset >= geometry.lineHeight);
  CHECK(secondLineOffset - geometry.lineHeight >= geometry.gap);
  // The manifest rail draws one line, so its pitch is the smaller of the two and is sized for one
  // line rather than two; if the two ever became equal the manifest rail would carry a gap it does
  // not need and the two layouts would no longer be distinguishable.
  CHECK(manifestPitch < pitch);
  CHECK(manifestPitch >= type.label);

  // The pitch the painter uses and the pitch the count and the hit test use must be one number. This
  // is checked through the count: the rows it reports at a height, drawn at the pitch, must cover the
  // viewport and must not claim a row that does not fit.
  for (const double height : {520.0, 720.0, 900.0}) {
    const auto rows = seam::native_ui::voicebankStudioUnitRailVisibleRows(height, true);
    CHECK(rows > 0U);
    CHECK(108.0 + static_cast<double>(rows) * pitch <= height + pitch);
    const auto manifestRows = seam::native_ui::voicebankStudioUnitRailVisibleRows(height, false);
    CHECK(manifestRows > 0U);
    CHECK(108.0 + static_cast<double>(manifestRows) * manifestPitch <= height + manifestPitch);
    // The manifest rail packs more rows into the same height precisely because its rows are shorter;
    // if that stopped being true the two rails would have been given the same row for the same text.
    CHECK(manifestRows >= rows);
  }
}

TEST_CASE("the Studio type scale holds a readable floor and the job card is sized from its lines") {
  const auto type = seam::native_ui::voicebankStudioTypeScale();
  const auto floor = seam::native_ui::design::TypeScale{}.smallLabel;
  CHECK(type.label >= floor);
  CHECK(type.secondary >= floor);
  CHECK(type.body >= floor);
  CHECK(type.heading >= floor);
  // A role is not allowed to be empty or negative: a zero-size role draws nothing, which is the
  // quietest possible way for a surface to lose its text.
  CHECK(type.label > 0.0);
  CHECK(type.secondary > 0.0);
  // The floor is inherited rather than restated: the Studio cannot drift away from the application
  // scale because it holds roles over it rather than its own numbers.
  CHECK(type.label == seam::native_ui::design::TypeScale{}.label);
  CHECK(type.secondary == seam::native_ui::design::TypeScale{}.smallLabel);

  // The card carries three lines, so its pitch is at least the height of those three lines plus the
  // inset above the first and the gap below the last, and it is taller than the 42 point card whose
  // three lines were 8, 7 and 6.5 point. A card drawn at readable sizes inside a pitch sized for
  // unreadable ones is the defect this unit found by reading the frame: the three lines overlapped
  // each other and the card clipped them.
  const auto pitch = seam::native_ui::studioGenerationJobCardPitch();
  CHECK(pitch > 42.0);
  const auto lineHeight = type.label * 1.25;
  CHECK(pitch >= 3.0 * lineHeight);

  // The row count has to agree with the pitch at every height the window can be, including the
  // shortest one that shows a card at all and a tall one that hits the cap. A count derived from any
  // other pitch disagrees at the boundary, which is where a card would be drawn over the footer.
  for (const double height : {388.0, 400.0, 430.0, 500.0, 600.0, 720.0, 900.0, 1200.0}) {
    const auto rows = seam::native_ui::studioGenerationRequestDetailVisibleRows(height);
    if (rows == 0U) continue;
    const auto first = seam::native_ui::studioGenerationJobCardTop();
    // The last card it reports has to end above the footer line the panel draws.
    CHECK(first + static_cast<double>(rows) * pitch <= height + pitch);
    // And one more card must not fit at the same height, or the count would be leaving a row undrawn.
    CHECK(first + static_cast<double>(rows + 1U) * pitch > first + static_cast<double>(rows) * pitch);
  }
}

TEST_CASE("word wrapping never splits a word and keeps each line inside its width") {
  namespace ui = seam::native_ui;
  ui::PixelSurface surface{8U, 8U};
  const ui::RasterCanvas canvas{surface};
  const std::string hint = "R REC / CMD/CTRL-I IMPORT / SHIFT-B BUILD";
  const auto two = ui::studioWrapWords(canvas, hint, 146.0, 6.0);
  CHECK(two.size() == 2U);
  if (two.size() == 2U) {
    CHECK(two[0] == "R REC / CMD/CTRL-I");
    CHECK(two[1] == "IMPORT / SHIFT-B BUILD");
  }
  CHECK(ui::studioWrapWords(canvas, hint, 526.0, 6.0).size() == 1U);
  CHECK(ui::studioWrapWords(canvas, "", 100.0, 6.0).empty());
  CHECK(ui::studioWrapWords(canvas, "   ", 100.0, 6.0).empty());
  // A word wider than the width stays whole on a line of its own instead of being split.
  const auto stuck = ui::studioWrapWords(canvas, "A UNBROKENIDENTIFIER B", 60.0, 6.0);
  CHECK(stuck.size() == 3U);
  if (stuck.size() == 3U) CHECK(stuck[1] == "UNBROKENIDENTIFIER");

  const auto advance = ui::RasterCanvas::fallbackTextAdvance(6.0);
  for (double width = 0.0; width <= 600.0; width += 6.0) {
    const auto lines = ui::studioWrapWords(canvas, hint, width, 6.0);
    std::string joined;
    for (const auto line : lines) {
      if (!joined.empty()) joined += ' ';
      joined += std::string{line};
      const auto columns = static_cast<std::size_t>(width / advance);
      if (text::utf8DisplayWidth(line) > columns && line.find(' ') != std::string_view::npos)
        throw seam::test::Failure{"Wrapped line \"" + std::string{line} + "\" is wider than " + std::to_string(columns) +
            " columns at width " + std::to_string(width)};
    }
    // Wrapping only chooses where the line breaks fall; it never adds, drops or splits a word.
    if (joined != hint)
      throw seam::test::Failure{"Wrapping at width " + std::to_string(width) + " changed the text to \"" + joined + "\""};
  }
}

TEST_CASE("a system text engine keeps whole wording wherever it truly fits and never truncates") {
  Fixture fixture;
  namespace ui = seam::native_ui;
  auto engine = text::TextEngine::createSystem();
  CHECK(engine);
  if (!engine) return;
  ui::PixelSurface plainSurface{8U, 8U};
  const ui::RasterCanvas plain{plainSurface};
  // Planned, so the campaign controls carry their Resume wording as well.
  CHECK(fixture.controller.beginGenerationCampaignPlan(fixture.recipePath,
      {"take-sa", "take-sa-soft"}, fixture.root / "campaign-engine", 1U));
  CHECK(drain(fixture.controller));

  for (const double scale : {1.0, 2.0}) {
    ui::PixelSurface surface{8U, 8U};
    const ui::RasterCanvas canvas{surface, scale, engine.value().get()};
    CHECK(canvas.measureText("", 10.0) == 0.0);
    // The premise of choosing against the bitmap face when no engine is installed: a system face is
    // narrower, so a label that fits the bitmap face fits the system face.
    CHECK(canvas.measureText("Plan campaign", 10.0) < plain.measureText("Plan campaign", 10.0));
    for (double width = 720.0; width <= 1800.0; width += 2.0) {
      for (const auto& control : ui::studioGenerationControls(fixture.controller, width, false)) {
        const auto painted = ui::studioControlPaintLabel(canvas, control, 10.0, 4.0);
        const auto box = control.bounds.width - 8.0;
        const auto measured = canvas.measureText(painted, 10.0);
        if (measured > box)
          throw seam::test::Failure{"At window width " + std::to_string(width) + " and scale " + std::to_string(scale) +
              ", control \"" + control.id + "\" paints \"" + std::string{painted} + "\" needing " +
              std::to_string(measured) + " px in " + std::to_string(box)};
        // Nothing is shortened that did not have to be.
        if (canvas.measureText(control.label, 10.0) <= box && painted != control.label)
          throw seam::test::Failure{"At window width " + std::to_string(width) + ", control \"" + control.id +
              "\" shortened a label that fits as \"" + std::string{painted} + "\""};
        // What the canvas measured is enough for the engine to draw it with no ellipsis, in the same
        // physical box drawText hands it.
        const auto physical = static_cast<std::uint32_t>(std::ceil(box * scale));
        const auto drawn = engine.value()->render(painted, text::TextStyle{
            .pixelHeight = static_cast<float>(10.0 * scale), .letterSpacing = 0.0F, .lineSpacing = 1.20F,
            .maximumWidth = physical, .maximumLines = 1U, .ellipsize = true});
        CHECK(drawn);
        if (drawn && drawn.value().metrics.truncated)
          throw seam::test::Failure{"The engine truncates \"" + std::string{painted} + "\" at window width " +
              std::to_string(width) + " and scale " + std::to_string(scale)};
      }
    }
  }
  // The real window keeps the full wording the bitmap face could not hold.
  const ui::RasterCanvas real{plainSurface, 2.0, engine.value().get()};
  const auto narrow = ui::studioGenerationControls(fixture.controller, 720.0, false);
  std::size_t whole = 0U;
  for (const auto& control : narrow)
    if (ui::studioControlPaintLabel(real, control, 10.0, 4.0) == control.label) ++whole;
  CHECK(whole > 0U);
}

// The wrapping draw has to break a sentence at its spaces and has to keep an unbroken token whole
// rather than replacing its tail with an ellipsis. Both were wrong with a system face loaded: the
// line broke at whatever character would not fit ("missi" then "ng"), and an identifier wider than
// its column came out as "P00001-c6eea096f83ec23e…". Neither can be seen on the built-in bitmap face,
// which is what a canvas with no engine draws, so this runs against a real engine.
TEST_CASE("wrapped text with a system engine breaks at words and keeps a long token whole") {
  namespace ui = seam::native_ui;
  auto engine = text::TextEngine::createSystem();
  CHECK(engine);
  if (!engine) return;
  const ui::Color ink{239, 233, 241, 255};
  const ui::Color background{15, 14, 18, 255};
  const auto rowsWith = [&](const std::string& text, double width, double height) {
    ui::PixelSurface surface{400U, 300U};
    surface.clear(background);
    ui::RasterCanvas canvas{surface, 1.0, engine.value().get()};
    canvas.drawTextWrapped({20.0, 20.0, width, height}, text, ink, 12.0, 16.0);
    std::vector<std::uint32_t> rows;
    for (std::uint32_t y = 0U; y < surface.height(); ++y)
      for (std::uint32_t x = 0U; x < surface.width(); ++x)
        if (surface.pixels()[static_cast<std::size_t>(y) * surface.width() + x] !=
            background.bgra()) {
          rows.push_back(y);
          break;
        }
    return rows;
  };
  // A sentence in a box that holds part of it takes more than one line.
  CHECK(rowsWith("Selected source execution evidence is missing or changed", 200.0, 60.0).size() >
        rowsWith("Selected source execution evidence is missing or changed", 600.0, 60.0).size());
  // An unbroken token wider than the column is broken across lines rather than drawn as a shorter
  // token with an ellipsis in place of its tail. In a 120 point box the token needs five lines, and
  // an ellipsized draw would show one line ending well before the right edge of the box.
  const auto token = "P00001-c6eea096f83ec23ee63a29980504370c9c18da78e9312e56641ce5ce77be91a8";
  CHECK(rowsWith(token, 120.0, 80.0).size() >= 4U);
  // Every full line of the token reaches the right edge of its box. A line that lost its tail to an
  // ellipsis stops at the width of the ellipsis plus what was kept, which is short of the edge.
  // A token whose tail was replaced by an ellipsis still draws on the same number of rows, so the
  // row count does not see it; what sees it is the ink on the last row, which an ellipsized draw
  // leaves short of the box while a broken one reaches it. The final row of the token is short, so
  // this checks the row above it, which is a full one.
  // The widest inked column of each drawn line. A broken token fills every one of its lines to the
  // right edge of the box, because the break falls where the next character will not fit and no
  // character is dropped. An ellipsized draw ends each line short of that edge, because the
  // ellipsis takes the room the dropped tail had, so the two are told apart by the width of a line
  // and not by how many lines there are.
  const auto lineWidths = [&](const std::string& text, double width, double height) {
    ui::PixelSurface surface{400U, 300U};
    surface.clear(background);
    ui::RasterCanvas canvas{surface, 1.0, engine.value().get()};
    canvas.drawTextWrapped({20.0, 20.0, width, height}, text, ink, 12.0, 16.0);
    std::vector<std::uint32_t> widths;
    std::uint32_t right = 0U;
    bool inLine = false;
    for (std::uint32_t y = 0U; y < surface.height(); ++y) {
      std::uint32_t rowRight = 0U;
      for (std::uint32_t x = 0U; x < surface.width(); ++x)
        if (surface.pixels()[static_cast<std::size_t>(y) * surface.width() + x] !=
            background.bgra())
          rowRight = x;
      if (rowRight != 0U) {
        inLine = true;
        if (rowRight > right) right = rowRight;
      } else if (inLine) {
        widths.push_back(right);
        right = 0U;
        inLine = false;
      }
    }
    if (inLine) widths.push_back(right);
    return widths;
  };
  const auto widths = lineWidths(token, 120.0, 80.0);
  CHECK(widths.size() >= 4U);
  // Every line but the last is a full one and reaches the right edge of the 120 point box, because
  // the break falls where the next character will not fit and nothing is dropped.
  for (std::size_t index = 0U; index + 1U < widths.size(); ++index)
    CHECK(widths[index] >= 20U + 100U);
}

// The unit rail cuts a long name to fit its row, and it used to do that by resizing the string to 24
// bytes. A UTF-8 character is more than one byte, so a name written in anything but ASCII lost its
// last character to a cut through the middle of it, and the creator was shown a name that was not the
// name of their unit. The cut is in display columns and says that it cut.
// The marker labels sit in a band across the top of the waveform. They were 6 point on a 9 point row
// with a width estimate of 3.8 points per column, all three numbers tuned to that size, so at 12 point
// two labels shared a row and the estimate was narrower than the text in it. These pin the size the
// labels are drawn at: the rows are tall enough for it, they do not overlap, they stay inside the
// waveform, and a label that would fall below the band is given no room rather than drawn on the
// trace. The case lives here rather than in test_native_ui because that file is only built with the
// CLAP editor plugin and so is not run in this configuration.
// The marker labels over the waveform are the one surface the readability work left pinned by
// geometry rather than by a rendered frame: no snapshot in the Studio app suite reaches the manifest
// view with a microscope, because every point in that journey a capture could be taken at is in the
// sample review view instead. This builds that view directly from a manifest with real markers and
// keeps the frames when SEAM_STUDIO_CAPTURE_DIRECTORY names a directory, so the labels can be read at
// both supported widths in the face the window uses. No check depends on it.
TEST_CASE("the manifest view with a microscope renders its marker labels at both widths") {
  const auto root = test::support::temporaryDirectory("studio-manifest-microscope");
  // A unit audio path is stored relative to the manifest, so the WAV is written beside it and named
  // relatively rather than by absolute path.
  const auto audioName = std::filesystem::path{"unit.wav"};
  const auto audio = root / audioName;
  std::vector<float> samples(24000U * 2U, 0.0F);
  for (std::size_t index = 0U; index < 24000U; ++index) {
    const auto value = (index % 400U) < 200U ? 0.4F : -0.4F;
    samples[index * 2U] = value;
    samples[index * 2U + 1U] = value;
  }
  CHECK(voicebank::writeWav(
      audio, voicebank::WavOutputFormat{.sampleRate = 48000U, .channels = 2U,
                                       .sampleFormat = voicebank::WavSampleFormat::Float32},
      samples).hasValue());
  auto unit = test::support::makeUnit("ja.original.a3.a.01", {"a"}, audio, 57,
                                     voicebank::UnitKind::Sustain, 24000U);
  unit.audioPath = audioName;
  unit.alias = "a";
  const auto manifest = test::support::makeManifest({unit});
  voicebank::ManifestJsonCodec codec;
  const auto manifestPath = root / "manifest.json";
  CHECK(codec.save(manifest, manifestPath));

  auto engine = text::TextEngine::createSystem();
  for (const auto& [width, height] : {std::pair{1100U, 720U}, std::pair{720U, 520U}}) {
    seam::native_ui::PixelSurface surface{width, height};
    seam::native_ui::RasterCanvas canvas{surface, 1.0,
                                        engine ? engine.value().get() : nullptr};
    Controller controller;
    CHECK(controller.openManifest(manifestPath, static_cast<double>(width),
                                  static_cast<double>(height)));
    CHECK(controller.selectedUnit() != nullptr);
    CHECK(!controller.microscope().markers().empty());
    seam::native_ui::VoicebankStudioScenePainter{}.paint(canvas, controller);
    // A marker line runs through the label that names it, and the label is drawn on its own backing
    // over the line. Removing that backing is invisible to every other check here: the frame still
    // looks drawn, and only reading it shows a line through the word. So the backing is checked as a
    // pixel: somewhere inside the first drawn label there is the backing colour and not the marker
    // colour, which is only true when the fill is drawn after the lines.
    const auto labels = seam::native_ui::voicebankStudioMarkerLabelBounds(
        controller.microscope().markers(), controller.microscope().waveformBounds());
    bool checked = false;
    const auto markerColour = seam::native_ui::Color{169, 79, 119, 255}.bgra();
    const auto gridColour = seam::native_ui::Color{58, 52, 64, 255}.bgra();
    for (const auto& label : labels) {
      if (label.width <= 0.0) continue;
      // The line that crosses a label belongs to a neighbouring marker, so the whole box is scanned:
      // a marker line crossing it is a line pixel, and with the backing drawn there is none.
      bool lineInside = false;
      for (std::uint32_t y = static_cast<std::uint32_t>(label.y);
           y < static_cast<std::uint32_t>(label.bottom()) && !lineInside; ++y) {
        for (std::uint32_t x = static_cast<std::uint32_t>(label.x);
             x < static_cast<std::uint32_t>(label.right()); ++x) {
          const auto pixel = surface.pixels()[static_cast<std::size_t>(y) * width + x];
          if (pixel == markerColour || pixel == gridColour) {
            lineInside = true;
            break;
          }
        }
      }
      CHECK(!lineInside);
      checked = true;
      break;
    }
    CHECK(checked);
    // The frames are kept only when a directory is named, so the case runs its checks either way.
    const char* capture = std::getenv("SEAM_STUDIO_CAPTURE_DIRECTORY");
    if (capture != nullptr && *capture != '\0') {
      std::filesystem::create_directories(capture);
      CHECK(surface.writePpm(std::filesystem::path{capture} /
                             ("manifest-microscope-" + std::to_string(width) + ".ppm")));
    }
  }
}

TEST_CASE("marker labels are rows tall enough for their type and stay inside the waveform") {
  const std::vector<seam::ui::AcousticMarkerVisual> markers{
      {seam::ui::AcousticMarkerKind::AudioOffset, "offset", 0, 272.0},
      {seam::ui::AcousticMarkerKind::ConsonantEnd, "consonant", 1, 278.0},
      {seam::ui::AcousticMarkerKind::VowelOnset, "vowel", 2, 284.0},
      {seam::ui::AcousticMarkerKind::StableStart, "stable", 3, 290.0},
      {seam::ui::AcousticMarkerKind::LoopStart, "loop-start", 4, 296.0},
      {seam::ui::AcousticMarkerKind::LoopEnd, "loop-end", 5, 302.0},
      {seam::ui::AcousticMarkerKind::ReleaseStart, "release", 6, 308.0},
      {seam::ui::AcousticMarkerKind::AudioEnd, "end", 7, 314.0},
  };
  const seam::ui::Rect waveform{270.0, 100.0, 182.0, 147.0};
  const auto labels = seam::native_ui::voicebankStudioMarkerLabelBounds(markers, waveform);
  CHECK(labels.size() == markers.size());
  std::size_t drawn = 0U;
  for (const auto& label : labels) {
    if (label.width <= 0.0) continue;  // A label with no room in the band.
    ++drawn;
    CHECK(label.x >= waveform.x);
    CHECK(label.right() <= waveform.right());
    CHECK(label.y >= waveform.y);
    // A row is at least as tall as 12 point type needs, which is what the old 7 point row was not.
    CHECK(label.height >= 14.0);
    // Labels stay inside the band rather than running down over the trace. The band holds four rows
    // at the 15 point pitch; a crowded waveform pushes a label onto a fourth row and it is still drawn
    // whole rather than cut by the edge of the band, which is what the rendered frame showed.
    CHECK(label.bottom() <= waveform.y + 62.0);
  }
  CHECK(drawn > 0U);
  for (std::size_t left = 0U; left < labels.size(); ++left) {
    if (labels[left].width <= 0.0) continue;
    for (std::size_t right = left + 1U; right < labels.size(); ++right) {
      if (labels[right].width <= 0.0) continue;
      CHECK(!labels[left].intersects(labels[right]));
    }
  }
  // A label is wide enough for its text at this size: the estimate is 7.2 points per display column
  // plus padding, so a label is never narrower than the text drawn in it.
  const std::vector<seam::ui::AcousticMarkerVisual> wide{
      {seam::ui::AcousticMarkerKind::VowelOnset, "かな", 0, 100.0},
  };
  const seam::ui::Rect narrow{0.0, 0.0, 240.0, 80.0};
  const auto one = seam::native_ui::voicebankStudioMarkerLabelBounds(wide, narrow);
  CHECK(one.size() == 1U);
  if (one.size() == 1U) {
    const auto columns = static_cast<double>(seam::text::utf8DisplayWidth(wide.front().label));
    CHECK(one.front().width >= columns * 7.2);
  }
}

TEST_CASE("a rail label cut to fit is cut by column and never through a character") {
  constexpr std::size_t kColumns = 24U;
  const auto painted = [](const std::string& label) {
    const auto truncated = text::utf8DisplayWidth(label) > kColumns;
    const auto kept = text::truncateUtf8ToDisplayWidth(
        label, truncated ? kColumns - 1U : kColumns);
    return truncated ? std::string{kept} + "…" : std::string{kept};
  };
  // A name whose 24th byte lands inside a multi byte character is the case that broke.
  // Twenty two ASCII columns followed by three three-byte characters: the 24th byte of this string
  // lands inside the first of them, which is exactly what a resize to 24 bytes did to it.
  const std::string wide = "AAAAAAAAAAAAAAAAAAAAAA\xE3\x82\xBD\xE3\x82\x93\xE3\x82\x93";
  const auto result = painted(wide);
  CHECK(text::utf8DisplayWidth(result) <= kColumns);
  // The cut says that it cut, with an ellipsis as its last three bytes.
  CHECK(result.size() >= 3U &&
        static_cast<unsigned char>(result[result.size() - 3U]) == 0xE2U &&
        static_cast<unsigned char>(result[result.size() - 2U]) == 0x80U &&
        static_cast<unsigned char>(result[result.size() - 1U]) == 0xA6U);
  // Every byte of what is painted is a whole character: the result decodes without a partial one.
  std::size_t index = 0U;
  while (index < result.size()) {
    const auto byte = static_cast<unsigned char>(result[index]);
    const auto length = byte < 0x80U ? 1U : (byte & 0xE0U) == 0xC0U   ? 2U
                        : (byte & 0xF0U) == 0xE0U ? 3U
                                                 : 4U;
    CHECK(index + length <= result.size());
    index += length;
  }
  // A short name is left exactly as it is.
  CHECK(painted("sustain:a") == "sustain:a");
}

TEST_CASE("the intake shortcut hint wraps at the minimum window instead of being cut") {
  Fixture fixture;
  namespace ui = seam::native_ui;
  const auto ink = ui::VoicebankStudioTheme{}.secondaryText.bgra();
  // The rows of the intake column that carry secondary text, and the ink in the column for a band of
  // rows. The column is laid out from the height of the rows above each one, and those wrap to a
  // different number of lines at each window width, so no row is written down here: what is pinned is
  // where the text ends, which is a property of the layout rather than of one window size.
  const auto bands = [&](std::uint32_t width, std::uint32_t height) {
    ui::PixelSurface surface{width, height};
    ui::RasterCanvas canvas{surface};
    fixture.controller.resize(static_cast<double>(width), static_cast<double>(height));
    ui::VoicebankStudioScenePainter{}.paint(canvas, fixture.controller);
    const std::uint32_t left = 294U, content = width - 256U - 270U - 48U;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> found;
    std::uint32_t start = 0U;
    bool open = false;
    for (std::uint32_t y = 72U; y < height; ++y) {
      bool any = false;
      for (std::uint32_t x = left; x < left + content && !any; ++x)
        any = surface.pixels()[static_cast<std::size_t>(y) * width + x] == ink;
      if (any && !open) { start = y; open = true; }
      if (!any && open) { found.emplace_back(start, y - 1U); open = false; }
    }
    if (open) found.emplace_back(start, height - 1U);
    return found;
  };
  const auto narrow = bands(720U, 520U);
  const auto wide = bands(1100U, 720U);
  // There is a hint at both widths, and it is the last band of secondary text in the column.
  CHECK(narrow.size() >= 4U);
  CHECK(wide.size() >= 4U);
  // The generation panel starts at 268 and used to paint over the last two shortcut rows outright,
  // so the creator could not see them at all. Nothing of the column is drawn from there down.
  constexpr std::uint32_t kGenerationPanelTop = 268U;
  CHECK(!narrow.empty() && narrow.back().second < kGenerationPanelTop);
  CHECK(!wide.empty() && wide.back().second < kGenerationPanelTop);
  // The hint is drawn at all: the band nearest the panel is the hint and it carries ink.
  CHECK(!narrow.empty() && narrow.back().first <= narrow.back().second);
}
