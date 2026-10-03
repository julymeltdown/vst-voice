// U21: generated material reaches the producer only through submitted generation requests and the
// canonical repository writer.
//
// The producer, recipe, audio and review decisions are synthetic engineering fixtures. They prove
// how generated takes enter and move through the producer lifecycle, not that a useful singer, a
// qualified resource or a human listening judgment exists.
#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/authoring/generation_campaign.hpp"
#include "seam/authoring/generation_job.hpp"
#include "seam/authoring/inventory_preflight.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank_production/candidate_publication.hpp"
#include "seam/voicebank_production/generation_request.hpp"
#include "seam/voicebank_production/manifest_draft.hpp"
#include "seam/voicebank_production/project_codec.hpp"

#include <algorithm>
#include <initializer_list>
#include <stop_token>
#include <string>
#include <vector>

namespace {
using namespace seam;
namespace production = voicebank_production;
using Outcome = authoring::CampaignAdvanceOutcome;
using production::UnitQueueState;

voice_design::VoiceRecipe recipe(double firstFormantHz = 700.0) {
  voice_design::VoiceRecipe value;
  value.id = "generation-workflow";
  value.poses = {{"a", "neutral", 0.0, {{firstFormantHz, 80.0, 0.0}, {1200.0, 100.0, -3.0}, {2600.0, 140.0, -6.0}}}};
  value.frications = {{"s", "neutral", {.seed = 42U}}};
  return value;
}

struct Campaign final {
  std::filesystem::path directory, path;
  std::string sha;
};

const production::TakeRecord* findTake(const production::VoicebankProductionProject& project, std::string_view takeId) {
  const auto found = std::find_if(project.takes.begin(), project.takes.end(),
      [&](const production::TakeRecord& take) { return take.takeId == takeId; });
  return found == project.takes.end() ? nullptr : &*found;
}
const production::UnitAssignment& assignment(const production::VoicebankProductionProject& project, std::string_view plannedTakeId) {
  return *std::find_if(project.unitAssignments.begin(), project.unitAssignments.end(),
      [&](const production::UnitAssignment& row) { return row.plannedTakeId == plannedTakeId; });
}

struct Fixture final {
  std::filesystem::path root{test::support::temporaryDirectory("voice-generation-workflow")};
  std::filesystem::path workspace{root / "producer"};
  production::ProductionProjectRepository repository{workspace};
  production::GenerationRequestRegistry registry{workspace};

  explicit Fixture(std::initializer_list<std::int32_t> layers) {
    const auto license = root / "fixture-license.txt";
    CHECK(core::durableAtomicWriteTextNew(license, "Synthetic generation workflow test only; no singer or Beta qualification."));
    const auto licenseHash = core::sha256File(license);
    CHECK(licenseHash);
    production::VoicebankProductionProject project{.projectId = "generation-workflow", .inventoryId = "fixture",
        .inventorySha256 = std::string(64U, 'a'), .selectedSourceStrategyId = "fixture", .licenseLocator = license.string(),
        .licenseSha256 = licenseHash.value(), .immutableAssetRoot = "assets"};
    project.schemaVersion = production::kProductionStyleSchemaVersion;
    project.language = "ja";
    for (const auto layer : layers) project.declaredPitchLayers.push_back(layer);
    project.sourceStrategies = {{.id = "fixture", .kind = production::SourceStrategyKind::ProceduralSynthesis,
        .rights = production::Feasibility::Pass, .coverage = production::Feasibility::Pass,
        .listening = production::Feasibility::Pass, .permissions = {true, true, true, true},
        .licenseLocator = license.string(), .licenseSha256 = licenseHash.value(), .evidenceState = "SYNTHETIC_TEST_ONLY"}};
    project.operators = {{"producer", "PRODUCER"}, {"reviewer", "REVIEWER"}};
    for (const auto layer : layers)
      project.unitAssignments.push_back({.coverageKey = "cv:s:a", .pitchLayer = layer,
          .promptId = "prompt-sa-" + std::to_string(layer), .plannedTakeId = take(layer), .style = "neutral"});
    CHECK(repository.initialize(project, {.action = "create", .subjectId = project.projectId, .operatorId = "producer",
        .occurredAtUtc = "2026-09-28T00:00:00Z"}));
  }

  static std::string take(std::int32_t layer) { return "take-sa-" + std::to_string(layer); }

  [[nodiscard]] production::VoicebankProductionProject producer() const {
    auto recovered = repository.recover();
    if (!recovered) throw test::Failure{"producer recovery failed: " + recovered.error().message};
    return std::move(recovered).value();
  }
  [[nodiscard]] std::string producerBytes() const { return production::encodeProductionProject(producer()); }

  // Plans against the current producer, publishes the definition and passes its held-out preflight.
  [[nodiscard]] Campaign plan(const std::string& name, const std::vector<std::string>& takes,
                              const voice_design::VoiceRecipe& source, std::size_t perBatch) const {
    const auto resource = voice_design::freezeVoiceRecipeResource(source);
    if (!resource) throw test::Failure{"recipe freeze failed: " + resource.error().message};
    const auto planned = authoring::planGenerationCampaign(producer(), takes, resource.value(),
        {.batch = {.maximumJobs = perBatch}});
    if (!planned) throw test::Failure{"campaign plan failed: " + planned.error().message};
    Campaign campaign{root / name, root / name / "campaign.json", core::sha256Hex(planned.value())};
    CHECK(std::filesystem::create_directory(campaign.directory));
    CHECK(core::durableAtomicWriteTextNew(campaign.path, planned.value()));
    const auto report = authoring::runInventoryPreflight(planned.value(), campaign.sha, campaign.directory / "preflight");
    if (!report || !report.value().passed) throw test::Failure{"campaign preflight did not pass"};
    return campaign;
  }

  [[nodiscard]] core::Result<authoring::CampaignAdvanceReport> advance(const Campaign& campaign, std::string at,
      std::stop_token stop = {}, authoring::CampaignAdvanceOptions options = {}) const {
    return authoring::advanceGenerationRequest(workspace, campaign.path, campaign.sha, "producer", std::move(at),
                                               stop, std::move(options));
  }

  [[nodiscard]] std::vector<authoring::GenerationJobReference> jobs(const Campaign& campaign, std::size_t batch) const {
    const auto path = campaign.directory / ("batch-" + std::to_string(batch)) / "batch.json";
    const auto hash = core::sha256File(path);
    if (!hash) throw test::Failure{"campaign batch is not prepared"};
    const auto loaded = authoring::loadGenerationBatch(path, hash.value());
    if (!loaded) throw test::Failure{"campaign batch cannot be loaded: " + loaded.error().message};
    return loaded.value();
  }

  [[nodiscard]] production::GenerationRequestRecord record(const Campaign& campaign) const {
    const auto found = registry.find(campaign.sha);
    if (!found) throw test::Failure{"generation request cannot be read: " + found.error().message};
    if (!found.value()) throw test::Failure{"generation request is not submitted"};
    return *found.value();
  }
};

// Render every job, then stop before collection: output exists, the producer has not changed.
authoring::CampaignAdvanceOptions renderOnly(std::stop_source& stop) {
  return {.interruptBeforeReceipt = {}, .renderProgress = [&stop](std::size_t done, std::size_t total) {
    if (done == total) stop.request_stop();
  }};
}

}  // namespace

TEST_CASE("an interrupted generation request resumes its valid work without duplicate takes or approval") {
  Fixture fixture{60, 64, 69, 72};
  const std::vector<std::string> takes{Fixture::take(60), Fixture::take(64), Fixture::take(69), Fixture::take(72)};
  const auto campaign = fixture.plan("campaign", takes, recipe(), 2U);
  const auto initial = fixture.producerBytes();

  // The stop lands after the first job of the first batch has rendered.
  std::stop_source stop;
  const auto interrupted = fixture.advance(campaign, "2026-09-28T01:00:00Z", stop.get_token(),
      {.interruptBeforeReceipt = {}, .renderProgress = [&](std::size_t done, std::size_t) { if (done == 1U) stop.request_stop(); }});
  CHECK(interrupted);
  CHECK(interrupted.value().outcome == Outcome::Cancelled);
  CHECK(interrupted.value().registered);
  CHECK(!interrupted.value().terminalRecorded);
  CHECK(interrupted.value().completedBatches == 0U);
  CHECK(interrupted.value().totalBatches == 2U);
  CHECK(fixture.producerBytes() == initial);

  // The submitted request is the immutable campaign: its jobs, recipe and the producer it expects.
  const auto submitted = fixture.record(campaign);
  CHECK(!submitted.terminal);
  CHECK(submitted.request.jobs.size() == 4U);
  CHECK(submitted.batchCount() == 2);
  CHECK(submitted.request.expectedGeneration == 1);
  CHECK(submitted.request.expectedProjectSha256 == core::sha256Hex(initial));
  CHECK(submitted.request.recipeHash == voice_design::freezeVoiceRecipeResource(recipe()).value().identity.contentHash);
  for (const auto& job : submitted.request.jobs) CHECK(job.frameCount > 0);

  const auto batch = fixture.jobs(campaign, 0U);
  CHECK(batch.size() == 2U);
  const auto kept = authoring::verifyGenerationJobOutput(batch[0].directory, batch[0].manifestSha256);
  CHECK(kept);
  CHECK(!authoring::verifyGenerationJobOutput(batch[1].directory, batch[1].manifestSha256));
  const auto keptTime = std::filesystem::last_write_time(kept.value().audioPath);

  // Resumption reuses the verified output rather than rendering it again.
  const auto resumed = fixture.advance(campaign, "2026-09-28T01:01:00Z");
  CHECK(resumed);
  CHECK(resumed.value().outcome == Outcome::BatchCollected);
  CHECK(resumed.value().completedBatches == 1U);
  const auto reused = authoring::verifyGenerationJobOutput(batch[0].directory, batch[0].manifestSha256);
  CHECK(reused);
  CHECK(reused.value().audioSha256 == kept.value().audioSha256);
  CHECK(std::filesystem::last_write_time(reused.value().audioPath) == keptTime);
  CHECK(fixture.producer().takes.size() == 2U);

  // A commit whose receipt was never published is recognized, not collected twice.
  std::vector<std::size_t> reportedBatchIndexes;
  const auto lostReceipt = fixture.advance(campaign, "2026-09-28T01:02:00Z", {},
      {.interruptBeforeReceipt = [] { return true; }, .renderProgress = {},
       .jobProgress = [&](std::size_t batchIndex, std::size_t, std::size_t) {
         reportedBatchIndexes.push_back(batchIndex);
       }});
  CHECK(!lostReceipt);
  CHECK(!reportedBatchIndexes.empty());
  CHECK(std::all_of(reportedBatchIndexes.begin(), reportedBatchIndexes.end(), [](std::size_t batchIndex) {
    return batchIndex == 1U;
  }));
  CHECK(fixture.producer().takes.size() == 4U);
  const auto finished = fixture.advance(campaign, "2026-09-28T01:03:00Z");
  CHECK(finished);
  CHECK(finished.value().outcome == Outcome::Completed);
  CHECK(finished.value().terminalRecorded);
  CHECK(finished.value().completedBatches == 2U);

  const auto producer = fixture.producer();
  CHECK(producer.lastDurableGeneration == 3U);
  CHECK(producer.takes.size() == 4U);
  CHECK(producer.reviews.empty());
  for (const auto& id : takes) {
    CHECK(std::count_if(producer.takes.begin(), producer.takes.end(), [&](const auto& take) { return take.takeId == id; }) == 1);
    CHECK(findTake(producer, id)->state == UnitQueueState::MarkerReview);
    CHECK(assignment(producer, id).takeId == id);
  }
  const auto completed = fixture.record(campaign);
  CHECK(completed.terminal);
  CHECK(completed.terminal->outcome == production::GenerationRequestOutcome::Completed);
  CHECK(completed.terminal->observedGeneration == 3);
  CHECK(completed.terminal->completedBatches == 2);

  // A retry reports the recorded outcome and changes nothing.
  const auto finalBytes = fixture.producerBytes();
  const auto retried = fixture.advance(campaign, "2026-09-28T01:04:00Z");
  CHECK(retried);
  CHECK(retried.value().outcome == Outcome::Completed);
  CHECK(retried.value().terminalRecorded);
  CHECK(fixture.producerBytes() == finalBytes);
}

TEST_CASE("a changed recipe, manual candidate, inventory or producer generation makes pending output stale") {
  Fixture fixture{60, 64, 69, 72, 76};
  const auto a = fixture.plan("campaign-a", {Fixture::take(60), Fixture::take(64)}, recipe(), 2U);
  std::stop_source stopA;
  const auto renderedA = fixture.advance(a, "2026-09-28T02:00:00Z", stopA.get_token(), renderOnly(stopA));
  CHECK(renderedA);
  CHECK(renderedA.value().outcome == Outcome::Cancelled);
  const auto jobsA = fixture.jobs(a, 0U);
  const auto inputsA = authoring::inspectGenerationBatch(jobsA);
  CHECK(inputsA);
  for (const auto& job : jobsA) CHECK(authoring::verifyGenerationJobOutput(job.directory, job.manifestSha256));

  // Changed recipe: a new request collects its own output for one of A's assignments.
  const auto b = fixture.plan("campaign-b", {Fixture::take(60)}, recipe(650.0), 1U);
  const auto collectedB = fixture.advance(b, "2026-09-28T02:01:00Z");
  CHECK(collectedB);
  CHECK(collectedB.value().outcome == Outcome::Completed);
  const auto afterB = fixture.producerBytes();
  const auto staleA = fixture.advance(a, "2026-09-28T02:02:00Z");
  CHECK(staleA);
  CHECK(staleA.value().outcome == Outcome::Stale);
  CHECK(staleA.value().terminalRecorded);
  CHECK(fixture.producerBytes() == afterB);
  // A's rendered output is not assigned: take-sa-64 stays missing and take-sa-60 is B's.
  auto producer = fixture.producer();
  CHECK(findTake(producer, Fixture::take(64)) == nullptr);
  CHECK(assignment(producer, Fixture::take(64)).state == UnitQueueState::Missing);
  // take-sa-60 is held by B's result, which does not prove A's request; take-sa-64 has no take.
  for (const auto& input : inputsA.value()) {
    const auto found = fixture.repository.findCollectedGeneration(input.expectation);
    CHECK(!found || !found.value());
  }
  const auto inputsB = authoring::inspectGenerationBatch(fixture.jobs(b, 0U));
  CHECK(inputsB);
  CHECK(fixture.repository.findCollectedGeneration(inputsB.value().front().expectation).value());
  // The canonical writer refuses the stale results even when called directly, and keeps them on disk.
  auto writer = fixture.producer();
  CHECK(!fixture.repository.importGeneratedBatch(writer, inputsA.value(),
      {"import-generated-batch", "stale-a", "producer", "2026-09-28T02:03:00Z"}));
  CHECK(fixture.producerBytes() == afterB);
  for (const auto& job : jobsA) CHECK(authoring::verifyGenerationJobOutput(job.directory, job.manifestSha256));
  const auto retriedA = fixture.advance(a, "2026-09-28T02:04:00Z");
  CHECK(retriedA);
  CHECK(retriedA.value().outcome == Outcome::Stale);
  CHECK(retriedA.value().terminalRecorded);

  // Manual candidate: the producer imports a candidate by hand for an assignment a request is rendering.
  const auto c = fixture.plan("campaign-c", {Fixture::take(64)}, recipe(), 1U);
  std::stop_source stopC;
  const auto renderedC = fixture.advance(c, "2026-09-28T02:05:00Z", stopC.get_token(), renderOnly(stopC));
  CHECK(renderedC);
  CHECK(renderedC.value().outcome == Outcome::Cancelled);
  const auto manualIndex = inputsA.value()[0].expectation.takeId == Fixture::take(64) ? 0U : 1U;
  const auto manual = authoring::verifyGenerationJobOutput(jobsA[manualIndex].directory, jobsA[manualIndex].manifestSha256);
  CHECK(manual);
  auto manualProject = fixture.producer();
  CHECK(fixture.repository.importProceduralCandidate(manualProject, manual.value().metadataPath, manual.value().audioPath,
      voice_design::freezeVoiceRecipeResource(recipe()).value(),
      {.takeId = Fixture::take(64), .promptId = "prompt-sa-64", .coverageKey = "cv:s:a", .pitchLayer = 64, .style = "neutral"},
      {"import-procedural", Fixture::take(64), "producer", "2026-09-28T02:06:00Z"}));
  const auto afterManual = fixture.producerBytes();
  const auto staleC = fixture.advance(c, "2026-09-28T02:07:00Z");
  CHECK(staleC);
  CHECK(staleC.value().outcome == Outcome::Stale);
  CHECK(staleC.value().terminalRecorded);
  CHECK(fixture.producerBytes() == afterManual);
  producer = fixture.producer();
  CHECK(findTake(producer, Fixture::take(64))->state == UnitQueueState::MarkerReview);
  const auto inputsC = authoring::inspectGenerationBatch(fixture.jobs(c, 0U));
  CHECK(inputsC);
  const auto foundC = fixture.repository.findCollectedGeneration(inputsC.value().front().expectation);
  CHECK(!foundC || !foundC.value());

  // Inventory: a submitted request is stale once the producer's inventory changes.
  const auto d = fixture.plan("campaign-d", {Fixture::take(69)}, recipe(), 1U);
  CHECK(authoring::submitGenerationCampaign(fixture.workspace, d.path, d.sha, "producer", "2026-09-28T02:08:00Z"));
  auto revised = fixture.producer();
  revised.inventorySha256 = std::string(64U, 'b');
  CHECK(fixture.repository.save(revised, {"save", "inventory-revision", "producer", "2026-09-28T02:09:00Z"}));
  const auto afterInventory = fixture.producerBytes();
  const auto staleD = fixture.advance(d, "2026-09-28T02:10:00Z");
  CHECK(staleD);
  CHECK(staleD.value().outcome == Outcome::Stale);
  CHECK(staleD.value().terminalRecorded);
  CHECK(fixture.producerBytes() == afterInventory);
  CHECK(!std::filesystem::exists(d.directory / "batch-0"));

  // Producer generation: any later commit, even one that changes nothing else.
  const auto e = fixture.plan("campaign-e", {Fixture::take(72)}, recipe(), 1U);
  CHECK(authoring::submitGenerationCampaign(fixture.workspace, e.path, e.sha, "producer", "2026-09-28T02:11:00Z"));
  auto unchanged = fixture.producer();
  CHECK(fixture.repository.save(unchanged, {"save", "unrelated-save", "producer", "2026-09-28T02:12:00Z"}));
  const auto afterSave = fixture.producerBytes();
  const auto staleE = fixture.advance(e, "2026-09-28T02:13:00Z");
  CHECK(staleE);
  CHECK(staleE.value().outcome == Outcome::Stale);
  CHECK(staleE.value().terminalRecorded);
  CHECK(fixture.producerBytes() == afterSave);
  CHECK(!std::filesystem::exists(e.directory / "batch-0"));

  // A campaign that was never submitted cannot be submitted stale; it reports the same outcome
  // without a registry record.
  const auto f = fixture.plan("campaign-f", {Fixture::take(76)}, recipe(), 1U);
  auto moved = fixture.producer();
  CHECK(fixture.repository.save(moved, {"save", "before-first-advance", "producer", "2026-09-28T02:14:00Z"}));
  CHECK(!authoring::submitGenerationCampaign(fixture.workspace, f.path, f.sha, "producer", "2026-09-28T02:15:00Z"));
  const auto staleF = fixture.advance(f, "2026-09-28T02:16:00Z");
  CHECK(staleF);
  CHECK(staleF.value().outcome == Outcome::Stale);
  CHECK(!staleF.value().registered);
  CHECK(!staleF.value().terminalRecorded);

  const auto listed = fixture.registry.list();
  CHECK(listed);
  CHECK(listed.value().size() == 5U);
  std::size_t staleCount = 0U, completedCount = 0U;
  for (const auto& entry : listed.value()) {
    CHECK(entry.terminal);
    staleCount += entry.terminal->outcome == production::GenerationRequestOutcome::Stale ? 1U : 0U;
    completedCount += entry.terminal->outcome == production::GenerationRequestOutcome::Completed ? 1U : 0U;
  }
  CHECK(staleCount == 4U);
  CHECK(completedCount == 1U);
}

TEST_CASE("cancellation, an exhausted output budget and retry keep the previous active state") {
  Fixture fixture{60, 64, 69};
  const auto p = fixture.plan("campaign-p", {Fixture::take(60)}, recipe(), 1U);
  const auto first = fixture.advance(p, "2026-09-28T03:00:00Z");
  CHECK(first);
  CHECK(first.value().outcome == Outcome::Completed);

  const auto q = fixture.plan("campaign-q", {Fixture::take(64), Fixture::take(69)}, recipe(), 1U);
  const auto beforeQ = fixture.producerBytes();
  std::stop_source cancelled;
  cancelled.request_stop();
  const auto early = fixture.advance(q, "2026-09-28T03:01:00Z", cancelled.get_token());
  CHECK(early);
  CHECK(early.value().outcome == Outcome::Cancelled);
  CHECK(!early.value().terminalRecorded);
  CHECK(fixture.producerBytes() == beforeQ);
  CHECK(!std::filesystem::exists(q.directory / "batch-0"));

  const auto collected = fixture.advance(q, "2026-09-28T03:02:00Z");
  CHECK(collected);
  CHECK(collected.value().outcome == Outcome::BatchCollected);
  CHECK(collected.value().completedBatches == 1U);
  const auto afterFirst = fixture.producerBytes();

  // Retained worker bytes above the admitted budget stop the request before any further work.
  const auto budget = fixture.record(q).request.budget.maximumBytes;
  const auto canary = q.directory / "retained-canary";
  CHECK(core::durableAtomicWriteTextNew(canary, "x"));
  std::filesystem::resize_file(canary, static_cast<std::uintmax_t>(budget) + 1U);
  const auto exhausted = fixture.advance(q, "2026-09-28T03:03:00Z");
  CHECK(exhausted);
  CHECK(exhausted.value().outcome == Outcome::BudgetExhausted);
  CHECK(exhausted.value().terminalRecorded);
  CHECK(exhausted.value().completedBatches == 1U);
  CHECK(exhausted.value().retainedBytes > static_cast<std::uint64_t>(budget));
  CHECK(fixture.producerBytes() == afterFirst);
  CHECK(!std::filesystem::exists(q.directory / "batch-1"));
  const auto recorded = fixture.record(q);
  CHECK(recorded.terminal);
  CHECK(recorded.terminal->outcome == production::GenerationRequestOutcome::BudgetExhausted);
  CHECK(recorded.terminal->completedBatches == 1);
  CHECK(recorded.terminal->observedGeneration == 3);
  CHECK(recorded.terminal->retainedBytes == static_cast<std::int64_t>(exhausted.value().retainedBytes));

  // Releasing the space does not reopen a terminal request: a retry reports it and does nothing.
  std::filesystem::rename(canary, fixture.root / "released-canary");
  const auto retried = fixture.advance(q, "2026-09-28T03:04:00Z");
  CHECK(retried);
  CHECK(retried.value().outcome == Outcome::BudgetExhausted);
  CHECK(retried.value().terminalRecorded);
  CHECK(fixture.producerBytes() == afterFirst);
  CHECK(!std::filesystem::exists(q.directory / "batch-1"));
  const auto producer = fixture.producer();
  CHECK(findTake(producer, Fixture::take(60))->state == UnitQueueState::MarkerReview);
  CHECK(findTake(producer, Fixture::take(64))->state == UnitQueueState::MarkerReview);
  CHECK(findTake(producer, Fixture::take(69)) == nullptr);
}

TEST_CASE("the request registry admits only requests and outcomes the producer history supports") {
  Fixture fixture{60, 64};
  const auto first = fixture.plan("campaign-first", {Fixture::take(60)}, recipe(), 1U);
  CHECK(fixture.advance(first, "2026-09-28T04:00:00Z").value().outcome == Outcome::Completed);
  const auto campaign = fixture.plan("campaign", {Fixture::take(64)}, recipe(), 1U);
  const auto submitted = authoring::submitGenerationCampaign(fixture.workspace, campaign.path, campaign.sha, "producer",
                                                             "2026-09-28T04:01:00Z");
  CHECK(submitted);
  const auto again = authoring::submitGenerationCampaign(fixture.workspace, campaign.path, campaign.sha, "producer",
                                                         "2026-09-28T04:01:00Z");
  CHECK(again);
  CHECK(again.value().requestSha256 == submitted.value().requestSha256);
  CHECK(!authoring::submitGenerationCampaign(fixture.workspace, campaign.path, campaign.sha, "producer", "2026-09-28T04:01:01Z"));

  // A request binds the current producer, real assignments with the same identity and absent takes.
  auto base = submitted.value().request;
  base.requestId = std::string(64U, 'c');
  auto stale = base;
  stale.expectedGeneration = 1;
  CHECK(!fixture.registry.submit(stale));
  auto duplicate = base;
  duplicate.jobs.front().takeId = Fixture::take(60);
  duplicate.jobs.front().pitchLayer = 60;
  CHECK(!fixture.registry.submit(duplicate));
  auto moved = base;
  moved.jobs.front().pitchLayer = 60;
  CHECK(!fixture.registry.submit(moved));
  auto unknown = base;
  unknown.jobs.front().takeId = "take-unknown";
  CHECK(!fixture.registry.submit(unknown));
  auto overBudget = base;
  overBudget.budget.maximumFrames = 1;
  CHECK(!fixture.registry.submit(overBudget));
  auto stranger = base;
  stranger.submittedBy = "stranger";
  CHECK(!fixture.registry.submit(stranger));
  CHECK(fixture.registry.list().value().size() == 2U);

  // A retained request is re-verified on every read.
  const auto requestFile = fixture.workspace / "generation-requests" / campaign.sha / "request.json";
  const auto original = core::readTextFileLimited(requestFile, 1024U * 1024U);
  CHECK(original);
  auto tampered = original.value();
  const auto at = tampered.find("\"pitchLayer\": 64");
  CHECK(at != std::string::npos);
  tampered.replace(at, 16U, "\"pitchLayer\": 60");
  CHECK(core::durableAtomicWriteText(requestFile, tampered));
  CHECK(!fixture.registry.find(campaign.sha));
  CHECK(!fixture.registry.list());
  CHECK(core::durableAtomicWriteText(requestFile, original.value()));
  CHECK(fixture.registry.find(campaign.sha));

  // Terminal outcomes must follow from the history after the expected generation.
  const auto expected = fixture.producer();
  const auto expectedHash = core::sha256Hex(production::encodeProductionProject(expected));
  const auto terminal = [](production::GenerationRequestOutcome outcome, std::int64_t generation, std::string hash,
                           std::int64_t batches, std::int64_t retained) {
    return production::GenerationRequestTerminal{.outcome = outcome, .observedGeneration = generation,
        .observedProjectSha256 = std::move(hash), .completedBatches = batches, .retainedBytes = retained,
        .detail = "fixture outcome", .recordedBy = "producer", .recordedAtUtc = "2026-09-28T04:02:00Z"};
  };
  using production::GenerationRequestOutcome;
  CHECK(!fixture.registry.recordTerminal(campaign.sha, terminal(GenerationRequestOutcome::Stale, 2, expectedHash, 0, 0)));
  const auto maximum = submitted.value().request.budget.maximumBytes;
  CHECK(!fixture.registry.recordTerminal(campaign.sha,
      terminal(GenerationRequestOutcome::BudgetExhausted, 2, expectedHash, 0, maximum)));
  auto later = fixture.producer();
  CHECK(fixture.repository.save(later, {"save", "not-a-collection", "producer", "2026-09-28T04:03:00Z"}));
  const auto laterHash = fixture.producerBytes();
  const auto laterSha = core::sha256Hex(laterHash);
  // A save is not this request's collection, so it cannot complete the request.
  CHECK(!fixture.registry.recordTerminal(campaign.sha, terminal(GenerationRequestOutcome::Completed, 3, laterSha, 1, 0)));
  CHECK(!fixture.registry.recordTerminal(campaign.sha, terminal(GenerationRequestOutcome::Stale, 3, expectedHash, 0, 0)));
  const auto exhausted = terminal(GenerationRequestOutcome::BudgetExhausted, 3, laterSha, 0, maximum + 1);
  CHECK(fixture.registry.recordTerminal(campaign.sha, exhausted));
  CHECK(fixture.registry.recordTerminal(campaign.sha, exhausted));
  auto different = exhausted;
  different.retainedBytes += 1;
  CHECK(!fixture.registry.recordTerminal(campaign.sha, different));
  const auto reported = fixture.advance(campaign, "2026-09-28T04:04:00Z");
  CHECK(reported);
  CHECK(reported.value().outcome == Outcome::BudgetExhausted);
  CHECK(findTake(fixture.producer(), Fixture::take(64)) == nullptr);
}

TEST_CASE("generated takes are inspected, reviewed and edited through the recorded-material lifecycle") {
  Fixture fixture{60};
  const auto campaign = fixture.plan("campaign", {Fixture::take(60)}, recipe(), 1U);
  CHECK(fixture.advance(campaign, "2026-09-28T05:00:00Z").value().outcome == Outcome::Completed);
  auto project = fixture.producer();
  const auto* generated = findTake(project, Fixture::take(60));
  CHECK(generated != nullptr);
  CHECK(generated->state == UnitQueueState::MarkerReview);
  const auto raw = generated->rawAssetSha256;
  // Inspected: collection committed the same technical receipt as any import, not a review.
  CHECK(std::any_of(project.metadataRevisions.begin(), project.metadataRevisions.end(), [&](const auto& revision) {
    return revision.kind == "take-inspection.v2" && revision.takeId == Fixture::take(60) && revision.rawAssetSha256 == raw;
  }));

  // Reviewed: the producer who collected it may not decide; a registered independent reviewer can.
  const auto draft = production::createSampleManifestDraft(fixture.workspace, project,
      {.id = "generation.workflow", .version = "0.1.0", .displayName = "Generation workflow",
       .language = domain::Language::Japanese, .style = "neutral"}, fixture.root / "draft");
  CHECK(draft);
  const auto manifest = voicebank::ManifestJsonCodec{}.load(draft.value().root / "manifest.json");
  CHECK(manifest);
  const auto packet = production::prepareSampleCandidateReview(fixture.workspace, project, manifest.value());
  CHECK(packet);
  CHECK(!production::commitSampleCandidateReview(fixture.workspace, project, packet.value(), "producer",
      "2026-09-28T05:01:00Z", production::SampleCandidateReviewDecision::Accept));
  CHECK(production::commitSampleCandidateReview(fixture.workspace, project, packet.value(), "reviewer",
      "2026-09-28T05:02:00Z", production::SampleCandidateReviewDecision::Accept));
  project = fixture.producer();
  CHECK(findTake(project, Fixture::take(60))->state == UnitQueueState::Approved);

  // Edited: an ordinary processing edit extends the chain and returns the take to review.
  const auto staged = fixture.repository.stageOperation(project, Fixture::take(60), "",
      {.kind = production::OperationKind::NormalizeGain, .targetPeak = 0.5F}, "generated-normalized");
  CHECK(staged);
  CHECK(fixture.repository.commitStaged(project, staged.value(), "generated-revision", "producer",
                                        "2026-09-28T05:03:00Z", Fixture::take(60), ""));
  project = fixture.producer();
  const auto* edited = findTake(project, Fixture::take(60));
  CHECK(edited->derivedRevisionIds.size() == 1U);
  CHECK(edited->rawAssetSha256 == raw);
  CHECK(edited->state == UnitQueueState::MarkerReview);

  // The completed request still reports its own outcome after that lifecycle.
  const auto retried = fixture.advance(campaign, "2026-09-28T05:04:00Z");
  CHECK(retried);
  CHECK(retried.value().outcome == Outcome::Completed);
}
