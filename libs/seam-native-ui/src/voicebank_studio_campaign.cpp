// Generation-campaign orchestration for the native Studio controller.
//
// The CLI already drives these services; this file gives the native controller the
// same plan / advance / cancel / resume path over the same authoring and
// repository owners. The controller never edits campaign JSON or producer state:
// planning publishes one immutable definition into a new directory, and every
// advancement commits at most one bounded batch, so a run is a loop of service
// calls that the user can stop between batches without losing committed material.
#include "seam/native_ui/voicebank_studio.hpp"

#include "voicebank_studio_production_support.hpp"

#include "seam/authoring/generation_campaign.hpp"
#include "seam/authoring/inventory_generation.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank_production/project_codec.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <set>
#include <string>
#include <utility>

namespace seam::native_ui {
namespace {

using Phase = VoicebankStudioController::GenerationCampaignProgress::Phase;

// One atomic carries a coherent snapshot: phase, completed/total batches and
// completed/total outputs in the current batch. Each count is bounded by the
// campaign admission limit (16384 jobs), so 15 bits is sufficient.
constexpr std::uint64_t kCampaignCountMask = (1ULL << 15U) - 1ULL;
constexpr unsigned kCampaignPhaseShift = 60U;
constexpr unsigned kCampaignTotalBatchesShift = 45U;
constexpr unsigned kCampaignCompletedBatchesShift = 30U;
constexpr unsigned kCampaignTotalOutputsShift = 15U;
static_assert(16384U <= kCampaignCountMask);

std::uint64_t packCampaignProgress(Phase phase, std::size_t totalBatches, std::size_t completedBatches,
    std::size_t completedOutputs = 0U, std::size_t totalOutputs = 0U) noexcept {
  return (static_cast<std::uint64_t>(phase) << kCampaignPhaseShift) |
      ((static_cast<std::uint64_t>(totalBatches) & kCampaignCountMask) << kCampaignTotalBatchesShift) |
      ((static_cast<std::uint64_t>(completedBatches) & kCampaignCountMask) << kCampaignCompletedBatchesShift) |
      ((static_cast<std::uint64_t>(totalOutputs) & kCampaignCountMask) << kCampaignTotalOutputsShift) |
      (static_cast<std::uint64_t>(completedOutputs) & kCampaignCountMask);
}

std::string campaignCountStatus(std::string_view prefix, std::size_t completed,
    std::size_t total, std::string_view suffix) {
  return std::string{prefix} + " " + std::to_string(completed) + "/" +
      std::to_string(total) + " BATCH(ES) " + std::string{suffix};
}

bool missingPath(const std::filesystem::file_status& status, const std::error_code& error) {
  return error == std::errc::no_such_file_or_directory ||
      (!error && status.type() == std::filesystem::file_type::not_found);
}

authoring::GenerationJobInspection incompleteJob(std::string message) {
  return {.state = authoring::GenerationJobInspectionState::Incomplete,
      .diagnostic = std::move(message)};
}

struct ExpectedCampaignJob final {
  std::string jobId, renderContentHash;
  voicebank_production::GenerationImportExpectation expectation;
};

core::Result<ExpectedCampaignJob> expectedCampaignJob(const formats::JsonValue& row,
    const voicebank_production::GenerationRequestJob& queued,
    const voicebank_production::VoicebankProductionProject& producer,
    const synthesis::ProceduralSingerResource& resource) {
  using Output = ExpectedCampaignJob;
  const auto invalid = [](std::string message) {
    return core::failure<Output>(core::ErrorCode::Conflict, std::move(message));
  };
  auto score = authoring::buildInventoryGenerationScore(producer, queued.takeId);
  if (!score) return core::Result<Output>{score.error()};
  const auto scoreJson = formats::ProjectJsonCodec{}.encode(score.value().project);
  const auto* frozenScore = row.find("scoreJson");
  const auto* frozenTemplate = row.find("templateIdentity");
  if (!scoreJson || frozenScore == nullptr || !frozenScore->isString() ||
      scoreJson.value() != frozenScore->asString() || frozenTemplate == nullptr || !frozenTemplate->isString() ||
      score.value().templateIdentity != frozenTemplate->asString() ||
      authoring::campaignJobId(score.value().templateIdentity, queued.takeId) != queued.jobId)
    return invalid("Prepared generation score differs from the frozen campaign row");
  const auto assignmentCount = std::count_if(producer.unitAssignments.begin(), producer.unitAssignments.end(),
      [&](const auto& item) { return item.plannedTakeId == queued.takeId; });
  if (assignmentCount != 1)
    return invalid("Campaign job does not identify exactly one frozen producer assignment");
  const auto assignment = std::find_if(producer.unitAssignments.begin(), producer.unitAssignments.end(),
      [&](const auto& item) { return item.plannedTakeId == queued.takeId; });
  if (assignment->style != queued.style || assignment->coverageKey != queued.coverageKey ||
      assignment->pitchLayer != queued.pitchLayer)
    return invalid("Campaign job differs from its frozen producer assignment");
  auto* track = score.value().project.findVocalTrack(score.value().trackId);
  if (track == nullptr) return invalid("Campaign score has no expected vocal track");
  track->proceduralRecipe = domain::ProceduralRecipeReference{
      resource.identity, "recipe.json", assignment->style};
  const auto sampleRate = static_cast<std::uint32_t>(score.value().project.settings().sampleRate);
  const auto snapshot = rendering::RenderSnapshotFactory{}.createProcedural(score.value().project,
      resource, score.value().trackId, score.value().regionId, 0U,
      rendering::RenderQuality::Final, sampleRate, assignment->style);
  if (!snapshot) return core::Result<Output>{snapshot.error()};
  const auto notes = snapshot.value().compiledPerformance->notes();
  if (notes.empty()) return invalid("Frozen campaign score contains no renderable notes");
  const voicebank_production::RawTakeInput take{.takeId = queued.takeId,
      .promptId = assignment->promptId, .coverageKey = assignment->coverageKey,
      .pitchLayer = assignment->pitchLayer, .supersedesTakeId = assignment->takeId,
      .style = assignment->style};
  const auto expectation = voicebank_production::captureGenerationImportExpectation(producer, take,
      resource, assignment->style, snapshot.value().contentHash, snapshot.value().sampleRate,
      notes.back().endFrame - notes.front().startFrame);
  if (!expectation) return core::Result<Output>{expectation.error()};
  if (queued.frameCount != expectation.value().frameCount)
    return invalid("Prepared job frame count differs from the queued campaign request");
  return Output{queued.jobId, snapshot.value().contentHash, expectation.value()};
}

std::string generationJournalName(std::uint64_t generation) {
  const auto decimal = std::to_string(generation);
  if (decimal.size() > 20U) return {};
  return std::string(20U - decimal.size(), '0') + decimal + ".json";
}

core::Result<voicebank_production::VoicebankProductionProject> recoverCampaignProducerBeforeBatch(
    const voicebank_production::GenerationRequestRecord& record,
    const formats::JsonValue& plan,
    const voicebank_production::VoicebankProductionProject& initialProducer,
    const synthesis::ProceduralSingerResource& resource,
    const std::filesystem::path& campaignRoot,
    const std::filesystem::path& workspaceRoot,
    std::size_t targetBatch,
    std::stop_token stop) {
  using Output = voicebank_production::VoicebankProductionProject;
  const auto fail = [](std::string message) {
    return core::failure<Output>(core::ErrorCode::Conflict, std::move(message));
  };
  auto producer = initialProducer;
  const auto* rows = plan.find("jobs");
  if (rows == nullptr || !rows->isArray()) return fail("Campaign job rows are unavailable");
  voicebank_production::ProductionProjectRepository repository{workspaceRoot};
  for (std::size_t batchIndex = 0U; batchIndex < targetBatch; ++batchIndex) {
    if (stop.stop_requested()) return fail("Generation output inspection cancelled");
    if (producer.lastDurableGeneration >= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
      return fail("Producer generation counter cannot advance through campaign history");
    const auto generation = producer.lastDurableGeneration + 1U;
    const auto batchDirectory = campaignRoot / ("batch-" + std::to_string(batchIndex));
    const auto receiptPath = batchDirectory / "collection.json";
    std::error_code error;
    const auto batchStatus = std::filesystem::symlink_status(batchDirectory, error);
    if (missingPath(batchStatus, error) || error || std::filesystem::is_symlink(batchStatus) ||
        !std::filesystem::is_directory(batchStatus))
      return fail("A preceding campaign batch is missing or unsafe");
    error.clear();
    const auto receiptStatus = std::filesystem::symlink_status(receiptPath, error);
    if (missingPath(receiptStatus, error) || error || std::filesystem::is_symlink(receiptStatus) ||
        !std::filesystem::is_regular_file(receiptStatus))
      return fail("A preceding campaign batch has no safe collection receipt");
    const auto receiptBytes = core::readTextFileLimited(receiptPath, 1024U * 1024U);
    if (!receiptBytes) return core::Result<Output>{receiptBytes.error()};
    const auto receipt = formats::parseJson(receiptBytes.value());
    if (!receipt || !receipt.value().isObject()) return fail("A preceding campaign collection receipt is malformed");
    const auto* originalHash = receipt.value().find("originalProducerSha256");
    const auto* committedHash = receipt.value().find("committedProducerSha256");
    const auto* committedGeneration = receipt.value().find("committedGeneration");
    if (receipt.value().asObject().size() != 6U ||
        !receipt.value().find("formatId") || !receipt.value().find("formatId")->isString() ||
        receipt.value().find("formatId")->asString() != "com.project-seam.generation-batch-collection" ||
        !receipt.value().find("schemaVersion") || !receipt.value().find("schemaVersion")->isInteger() ||
        receipt.value().find("schemaVersion")->asInt64() != 1 ||
        originalHash == nullptr || !originalHash->isString() || committedHash == nullptr || !committedHash->isString() ||
        committedGeneration == nullptr || !committedGeneration->isString())
      return fail("A preceding campaign receipt has an unsupported shape");
    const auto producerBytes = voicebank_production::encodeProductionProject(producer);
    const auto beforeHash = core::sha256Hex(producerBytes);
    if (originalHash->asString() != beforeHash || committedGeneration->asString() != std::to_string(generation))
      return fail("A preceding campaign receipt does not continue the frozen producer history");
    const auto after = repository.recoverGeneration(generation, committedHash->asString());
    if (!after) return core::Result<Output>{after.error()};

    const auto journalName = generationJournalName(generation);
    if (journalName.empty()) return fail("Producer journal generation is outside its supported range");
    const auto journalPath = workspaceRoot / "journal" / journalName;
    error.clear();
    const auto journalStatus = std::filesystem::symlink_status(journalPath, error);
    if (missingPath(journalStatus, error) || error || std::filesystem::is_symlink(journalStatus) ||
        !std::filesystem::is_regular_file(journalStatus))
      return fail("A preceding campaign producer journal event is missing or unsafe");
    const auto journalBytes = core::readTextFileLimited(journalPath, 1024U * 1024U);
    if (!journalBytes) return core::Result<Output>{journalBytes.error()};
    const auto journal = formats::parseJson(journalBytes.value());
    if (!journal || !journal.value().isObject()) return fail("A preceding campaign producer journal event is malformed");
    const auto* action = journal.value().find("action");
    const auto* journalGeneration = journal.value().find("generation");
    const auto* journalHash = journal.value().find("projectSha256");
    if (action == nullptr || !action->isString() || action->asString() != "import-generated-batch" ||
        journalGeneration == nullptr || !journalGeneration->isInteger() ||
        journalGeneration->asInt64() != static_cast<std::int64_t>(generation) ||
        journalHash == nullptr || !journalHash->isString() || journalHash->asString() != committedHash->asString())
      return fail("A preceding campaign producer state is not one batch collection");

    formats::JsonValue::Array expectedTakes;
    std::set<std::string> expectedTakeIds;
    const auto& planRows = rows->asArray();
    for (std::size_t index = 0U; index < planRows.size(); ++index) {
      if (stop.stop_requested()) return fail("Generation output inspection cancelled");
      const auto& job = record.request.jobs[index];
      if (job.batchIndex != static_cast<std::int64_t>(batchIndex)) continue;
      const auto expected = expectedCampaignJob(planRows[index], job, producer, resource);
      if (!expected) return core::Result<Output>{expected.error()};
      const auto expectationBytes = voicebank_production::encodeGenerationImportExpectation(
          expected.value().expectation);
      if (!expectationBytes) return core::Result<Output>{expectationBytes.error()};
      const auto collected = repository.findCollectedGeneration(
          expected.value().expectation, generation, committedHash->asString());
      if (!collected) return core::Result<Output>{collected.error()};
      if (!collected.value() || !collected.value()->active ||
          (collected.value()->state != voicebank_production::UnitQueueState::MarkerReview &&
           collected.value()->state != voicebank_production::UnitQueueState::Rejected))
        return fail("A preceding campaign take is not present as unreviewed request-bound material");
      expectedTakeIds.insert(job.takeId);
      expectedTakes.emplace_back(formats::JsonValue::Object{
          {"takeId", collected.value()->takeId}, {"audioSha256", collected.value()->audioSha256},
          {"expectationSha256", core::sha256Hex(expectationBytes.value())}});
    }
    if (expectedTakeIds.empty()) return fail("A preceding campaign batch contains no request jobs");
    std::set<std::string> introducedTakeIds;
    for (const auto& take : after.value().takes) {
      if (std::none_of(producer.takes.begin(), producer.takes.end(),
          [&](const auto& previous) { return previous.takeId == take.takeId; }))
        introducedTakeIds.insert(take.takeId);
    }
    if (introducedTakeIds != expectedTakeIds) return fail("A preceding producer generation introduced unexpected takes");
    const auto canonicalReceipt = formats::stringifyJson(formats::JsonValue::Object{
        {"formatId", "com.project-seam.generation-batch-collection"}, {"schemaVersion", std::int64_t{1}},
        {"originalProducerSha256", beforeHash}, {"committedProducerSha256", committedHash->asString()},
        {"committedGeneration", std::to_string(generation)}, {"takes", std::move(expectedTakes)}}, true);
    if (canonicalReceipt != receiptBytes.value()) return fail("A preceding campaign receipt differs from request-bound producer history");
    producer = after.value();
  }
  return producer;
}

core::Result<VoicebankStudioController::GenerationRequestOutputInspectionPage>
inspectRequestOutputPage(const voicebank_production::GenerationRequestRecord& record,
    const std::filesystem::path& definitionPath,
    const std::filesystem::path& workspaceRoot,
    std::size_t firstJob, std::size_t maximumJobs, std::stop_token stop) {
  using Page = VoicebankStudioController::GenerationRequestOutputInspectionPage;
  using State = authoring::GenerationJobInspectionState;
  const auto fail = [](std::string message) {
    return core::failure<Page>(core::ErrorCode::Conflict, std::move(message));
  };
  if (stop.stop_requested()) return fail("Generation output inspection cancelled");
  if (record.request.requestId.size() != 64U || record.requestSha256.size() != 64U ||
      definitionPath.empty() || maximumJobs == 0U || maximumJobs > 16U ||
      workspaceRoot.empty() || record.request.jobs.empty() || firstJob >= record.request.jobs.size())
    return core::failure<Page>(core::ErrorCode::InvalidArgument,
        "Generation output inspection request or page bounds are invalid");

  const auto encodedRequest = voicebank_production::encodeGenerationRequest(record.request);
  if (!encodedRequest || core::sha256Hex(encodedRequest.value()) != record.requestSha256)
    return fail("Queued generation request does not match its verified request digest");

  std::error_code error;
  const auto definitionStatus = std::filesystem::symlink_status(definitionPath, error);
  if (missingPath(definitionStatus, error) || error || std::filesystem::is_symlink(definitionStatus) ||
      !std::filesystem::is_regular_file(definitionStatus))
    return fail("Campaign definition is missing or is not a regular non-symlink file");
  const auto definition = core::readTextFileLimited(definitionPath, 32U * 1024U * 1024U);
  if (!definition) return core::Result<Page>{definition.error()};
  const auto campaign = authoring::VerifiedGenerationCampaign::admit(
      definition.value(), record.request.requestId, stop);
  if (!campaign) return fail("Campaign definition does not match the immutable queued request: " +
      campaign.error().message);
  const auto& plan = campaign.value().plan();
  const auto* initialJson = plan.find("initialProducerJson");
  const auto* initialHash = plan.find("initialProducerSha256");
  const auto* recipeJson = plan.find("recipeJson");
  const auto* recipeHash = plan.find("recipeSha256");
  const auto* planJobs = plan.find("jobs");
  if (initialJson == nullptr || !initialJson->isString() || initialHash == nullptr || !initialHash->isString() ||
      recipeJson == nullptr || !recipeJson->isString() || recipeHash == nullptr || !recipeHash->isString() ||
      planJobs == nullptr || !planJobs->isArray() || planJobs->asArray().size() != record.request.jobs.size())
    return fail("Admitted campaign has incomplete request-bound producer, recipe, or job data");

  const auto initial = voicebank_production::decodeProductionProject(initialJson->asString());
  if (!initial || core::sha256Hex(initialJson->asString()) != initialHash->asString() ||
      initialHash->asString() != record.request.expectedProjectSha256 ||
      initial.value().lastDurableGeneration > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
      static_cast<std::int64_t>(initial.value().lastDurableGeneration) != record.request.expectedGeneration ||
      initial.value().language != record.request.language)
    return fail("Campaign producer snapshot differs from the immutable request identity");

  const auto recipe = voice_design::decodeVoiceRecipe(recipeJson->asString());
  if (!recipe) return fail("Campaign recipe cannot be decoded: " + recipe.error().message);
  const auto resource = voice_design::freezeVoiceRecipeResource(recipe.value(), stop);
  if (!resource) return fail("Campaign recipe resource is invalid: " + resource.error().message);
  const auto& identity = resource.value().identity;
  if (identity.id != record.request.recipeId || identity.version != record.request.recipeVersion ||
      identity.contentHash != record.request.recipeHash || identity.contentHash != recipeHash->asString())
    return fail("Campaign recipe identity differs from the immutable request");

  const auto integer = [&](std::string_view key) -> std::optional<std::int64_t> {
    const auto* value = plan.find(key);
    if (value == nullptr || !value->isInteger()) return std::nullopt;
    return value->asInt64();
  };
  const auto budgetMaximumJobs = integer("maximumJobs");
  const auto maximumFrames = integer("maximumFrames");
  const auto maximumBytes = integer("maximumEstimatedBytes");
  const auto batchMaximumJobs = integer("batchMaximumJobs");
  const auto batchMaximumFrames = integer("batchMaximumFrames");
  if (!budgetMaximumJobs || !maximumFrames || !maximumBytes || !batchMaximumJobs || !batchMaximumFrames ||
      record.request.budget != voicebank_production::GenerationRequestBudget{
          *budgetMaximumJobs, *maximumFrames, *maximumBytes, *batchMaximumJobs, *batchMaximumFrames})
    return fail("Campaign resource budget differs from the immutable request");

  for (std::size_t index = 0U; index < planJobs->asArray().size(); ++index) {
    if (stop.stop_requested()) return fail("Generation output inspection cancelled");
    const auto& row = planJobs->asArray()[index];
    const auto& queued = record.request.jobs[index];
    const auto fieldString = [&](std::string_view key) -> const std::string* {
      const auto* value = row.find(key);
      return value != nullptr && value->isString() ? &value->asString() : nullptr;
    };
    const auto fieldInteger = [&](std::string_view key) -> std::optional<std::int64_t> {
      const auto* value = row.find(key);
      return value != nullptr && value->isInteger() ? std::optional<std::int64_t>{value->asInt64()} : std::nullopt;
    };
    const auto* takeId = fieldString("takeId");
    const auto* style = fieldString("style");
    const auto* coverageKey = fieldString("coverageKey");
    const auto* templateIdentity = fieldString("templateIdentity");
    const auto pitchLayer = fieldInteger("pitchLayer");
    const auto frameCount = fieldInteger("frameCount");
    const auto batchIndex = fieldInteger("batchIndex");
    if (takeId == nullptr || style == nullptr || coverageKey == nullptr || templateIdentity == nullptr ||
        !pitchLayer || !frameCount || !batchIndex || *batchIndex < 0 ||
        queued.takeId != *takeId || queued.style != *style || queued.coverageKey != *coverageKey ||
        queued.pitchLayer != *pitchLayer || queued.frameCount != *frameCount || queued.batchIndex != *batchIndex ||
        queued.jobId != authoring::campaignJobId(*templateIdentity, *takeId))
      return fail("Campaign job rows differ from the immutable request queue");
  }

  const auto first = firstJob;
  const auto end = first + std::min(maximumJobs, record.request.jobs.size() - first);
  Page page{.requestId = record.request.requestId, .requestSha256 = record.requestSha256,
      .firstJob = first, .jobs = {}};
  page.jobs.reserve(end - first);
  const auto absoluteDefinition = std::filesystem::absolute(definitionPath, error);
  if (error) return fail("Campaign definition path cannot be resolved");
  const auto campaignRoot = absoluteDefinition.parent_path().lexically_normal();
  for (std::size_t index = first; index < end; ++index) {
    if (stop.stop_requested()) return fail("Generation output inspection cancelled");
    const auto& queued = record.request.jobs[index];
    const auto batchDirectory = campaignRoot / ("batch-" + std::to_string(queued.batchIndex));
    const auto jobDirectory = batchDirectory / queued.jobId;
    error.clear();
    const auto batchStatus = std::filesystem::symlink_status(batchDirectory, error);
    if (missingPath(batchStatus, error)) {
      page.jobs.push_back({.state = State::NotPrepared,
          .diagnostic = "Campaign batch has not been prepared."});
      continue;
    }
    if (error || std::filesystem::is_symlink(batchStatus) || !std::filesystem::is_directory(batchStatus)) {
      page.jobs.push_back(incompleteJob("Campaign batch path is unsafe or unreadable."));
      continue;
    }
    error.clear();
    const auto jobStatus = std::filesystem::symlink_status(jobDirectory, error);
    if (missingPath(jobStatus, error)) {
      page.jobs.push_back({.state = State::NotPrepared,
          .diagnostic = "Generation job has not been prepared."});
      continue;
    }
    if (error || std::filesystem::is_symlink(jobStatus) || !std::filesystem::is_directory(jobStatus)) {
      page.jobs.push_back(incompleteJob("Generation job path is unsafe or unreadable."));
      continue;
    }

    bool unsafePackage = false;
    bool missingPackage = false;
    for (const auto* name : {"job.json", "expectation.json", "project.json", "recipe.json"}) {
      error.clear();
      const auto packageStatus = std::filesystem::symlink_status(jobDirectory / name, error);
      if (missingPath(packageStatus, error)) { missingPackage = true; break; }
      if (error || std::filesystem::is_symlink(packageStatus) || !std::filesystem::is_regular_file(packageStatus)) {
        unsafePackage = true;
        break;
      }
    }
    if (unsafePackage) {
      page.jobs.push_back(incompleteJob("Generation job package contains an unsafe file."));
      continue;
    }
    if (missingPackage) {
      const auto evidence = authoring::inspectGenerationJobOutputReadOnly(
          jobDirectory, core::sha256Hex(std::string_view{}), stop);
      page.jobs.push_back(evidence ? evidence.value() : incompleteJob(evidence.error().message));
      continue;
    }
    const auto manifestSha = core::sha256File(jobDirectory / "job.json");
    if (!manifestSha) {
      page.jobs.push_back(incompleteJob("Generation job manifest cannot be hashed."));
      continue;
    }
    const auto* row = &planJobs->asArray()[index];
    const auto producerForBatch = recoverCampaignProducerBeforeBatch(record, plan, initial.value(),
        resource.value(), campaignRoot, workspaceRoot,
        static_cast<std::size_t>(queued.batchIndex), stop);
    if (!producerForBatch) {
      if (stop.stop_requested()) return fail("Generation output inspection cancelled");
      page.jobs.push_back(incompleteJob("Preceding campaign history cannot be verified: " +
          producerForBatch.error().message));
      continue;
    }
    const auto loadedJob = authoring::loadGenerationJob(jobDirectory, manifestSha.value());
    if (!loadedJob) {
      page.jobs.push_back(incompleteJob("Generation job package failed integrity validation: " +
          loadedJob.error().message));
      continue;
    }
    const auto expected = expectedCampaignJob(*row, queued, producerForBatch.value(), resource.value());
    if (!expected) {
      page.jobs.push_back(incompleteJob("Campaign import expectation cannot be reconstructed: " +
          expected.error().message));
      continue;
    }
    if (loadedJob.value().jobId != expected.value().jobId) {
      page.jobs.push_back(incompleteJob("Prepared job ID differs from the campaign row."));
      continue;
    }
    if (loadedJob.value().snapshot.contentHash != expected.value().renderContentHash) {
      page.jobs.push_back(incompleteJob("Prepared render snapshot differs from the campaign score and recipe."));
      continue;
    }
    if (loadedJob.value().expectation != expected.value().expectation) {
      page.jobs.push_back(incompleteJob("Prepared import expectation differs from the frozen producer assignment."));
      continue;
    }
    const auto evidence = authoring::inspectGenerationJobOutputReadOnly(jobDirectory, manifestSha.value(), stop);
    if (!evidence) {
      if (stop.stop_requested()) return fail("Generation output inspection cancelled");
      page.jobs.push_back(incompleteJob(evidence.error().message));
    } else {
      page.jobs.push_back(evidence.value());
    }
  }
  return page;
}

}  // namespace

std::optional<VoicebankStudioController::GenerationCampaignProgress>
VoicebankStudioController::generationCampaignProgress() const noexcept {
  if (!generationCampaignProgress_) return std::nullopt;
  const auto value = generationCampaignProgress_->load(std::memory_order_relaxed);
  return GenerationCampaignProgress{
      static_cast<GenerationCampaignProgress::Phase>(value >> kCampaignPhaseShift),
      static_cast<std::size_t>((value >> kCampaignCompletedBatchesShift) & kCampaignCountMask),
      static_cast<std::size_t>((value >> kCampaignTotalBatchesShift) & kCampaignCountMask),
      static_cast<std::size_t>(value & kCampaignCountMask),
      static_cast<std::size_t>((value >> kCampaignTotalOutputsShift) & kCampaignCountMask)};
}

core::Result<void> VoicebankStudioController::beginGenerationCampaignPlan(
    std::filesystem::path recipePath, std::vector<std::string> plannedTakeIds,
    std::filesystem::path destination, std::size_t maximumJobsPerBatch) {
  using Outcome = GenerationCampaignOutcome;
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Production worker is busy");
  if (!productionProject_ || !productionRepository_)
    return core::failure(core::ErrorCode::InvalidState, "Campaign planning requires a producer workspace");
  if (recipePath.empty())
    return core::failure(core::ErrorCode::InvalidArgument, "Campaign planning requires a recipe file");
  if (plannedTakeIds.empty() || plannedTakeIds.size() > 16384U)
    return core::failure(core::ErrorCode::InvalidArgument, "Select 1 to 16384 planned take ids");
  if (destination.empty())
    return core::failure(core::ErrorCode::InvalidArgument, "Campaign planning requires a new output directory");
  if (maximumJobsPerBatch > 16384U)
    return core::failure(core::ErrorCode::InvalidArgument, "Campaign batch job budget is invalid");
  const authoring::GenerationCampaignLimits limits = maximumJobsPerBatch == 0U
      ? authoring::GenerationCampaignLimits{}
      : authoring::GenerationCampaignLimits{.batch = {.maximumJobs = maximumJobsPerBatch}};
  std::error_code probe;
  if (std::filesystem::exists(destination, probe))
    return core::failure(core::ErrorCode::Conflict,
        "Campaign directory must be new; an existing campaign is resumed, never replaced");
  auto captured = *productionProject_;
  const auto root = productionWorkspaceRoot_;
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token();
  statusBeforeImport_ = status_;
  auto progress = std::make_shared<std::atomic<std::uint64_t>>(
      packCampaignProgress(GenerationCampaignProgress::Phase::Planning, 0U, 0U));
  generationCampaignProgress_ = progress;
  try {
    campaignWork_ = std::async(std::launch::async,
        [captured = std::move(captured), root, recipePath = std::move(recipePath),
         takes = std::move(plannedTakeIds), destination = std::move(destination), limits, stop, progress]() mutable
        -> core::Result<Outcome> {
      const auto cancelled = [] { return core::failure<Outcome>(core::ErrorCode::Conflict,
          "Campaign planning cancelled before publication"); };
      const auto recipe = voice_design::loadVoiceRecipeResource(recipePath, {}, stop);
      if (!recipe) return core::Result<Outcome>{recipe.error()};
      if (stop.stop_requested()) return cancelled();
      const auto planned = authoring::planGenerationCampaign(captured, takes, recipe.value(), limits, stop);
      if (!planned) return core::Result<Outcome>{planned.error()};
      const auto parsed = formats::parseJson(planned.value());
      if (!parsed) return core::Result<Outcome>{parsed.error()};
      const auto* batchCount = parsed.value().find("batchCount");
      const auto* initialHash = parsed.value().find("initialProducerSha256");
      if (batchCount == nullptr || !batchCount->isInteger() || batchCount->asInt64() < 0 ||
          initialHash == nullptr || !initialHash->isString())
        return core::failure<Outcome>(core::ErrorCode::InvariantViolation,
            "Planned campaign definition is incomplete");
      // Planning binds the durable state it was computed from. A workspace whose
      // producer moved on in the meantime must be re-planned instead of publishing
      // a definition that no longer matches its own repository.
      voicebank_production::ProductionProjectRepository repository{root};
      const auto durable = repository.recover();
      if (!durable) return core::Result<Outcome>{durable.error()};
      if (core::sha256Hex(voicebank_production::encodeProductionProject(durable.value())) != initialHash->asString())
        return core::failure<Outcome>(core::ErrorCode::Conflict, "Campaign initial producer state is stale");
      if (stop.stop_requested()) return cancelled();
      std::error_code error;
      if (!std::filesystem::create_directory(destination, error) || error)
        return core::failure<Outcome>(core::ErrorCode::Conflict,
            "Campaign directory must be new with an existing parent");
      const auto saved = core::durableAtomicWriteTextNew(destination / "campaign.json", planned.value());
      if (!saved) return core::Result<Outcome>{saved.error()};
      const auto total = static_cast<std::size_t>(batchCount->asInt64());
      progress->store(packCampaignProgress(GenerationCampaignProgress::Phase::Planned, total, 0U),
          std::memory_order_relaxed);
      return Outcome{.campaignPath = destination / "campaign.json",
          .campaignSha256 = core::sha256Hex(planned.value()),
          .completedBatches = 0U, .totalBatches = total,
          .producer = std::move(captured), .adoptedProducer = false,
          .status = campaignCountStatus("CAMPAIGN PLANNED", 0U, total, "/ NOT GENERATED")};
    });
  } catch (const std::exception& error) {
    return core::failure(core::ErrorCode::Internal, "Unable to start campaign planning worker", error.what());
  }
  status_ = "PLANNING CAMPAIGN / ESC CANCEL";
  return core::success();
}

core::Result<void> VoicebankStudioController::beginGenerationCampaignAdvance(
    std::filesystem::path campaignPath, std::string campaignSha256, std::string occurredAtUtc) {
  using Outcome = GenerationCampaignOutcome;
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Production worker is busy");
  if (!productionProject_ || !productionRepository_)
    return core::failure(core::ErrorCode::InvalidState, "Campaign advancement requires a producer workspace");
  if (campaignPath.empty() || campaignSha256.size() != 64U)
    return core::failure(core::ErrorCode::InvalidArgument, "Campaign identity is invalid");
  const bool fixedTimestamp = !occurredAtUtc.empty();
  if (fixedTimestamp && !voicebank_production::isProductionUtcTimestamp(occurredAtUtc))
    return core::failure(core::ErrorCode::InvalidArgument, "Campaign timestamp is invalid");
  const auto root = productionWorkspaceRoot_;
  const auto operatorId = productionOperatorId_;
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token();
  statusBeforeImport_ = status_;
  auto progress = std::make_shared<std::atomic<std::uint64_t>>(
      packCampaignProgress(GenerationCampaignProgress::Phase::Advancing, 0U, 0U));
  generationCampaignProgress_ = progress;
  try {
    campaignWork_ = std::async(std::launch::async,
        [campaignPath = std::move(campaignPath), campaignSha256 = std::move(campaignSha256),
         occurredAtUtc = std::move(occurredAtUtc), fixedTimestamp, root, operatorId, stop, progress]() mutable
        -> core::Result<Outcome> {
      voicebank_production::ProductionProjectRepository repository{root};
      // The batch count comes from the admitted definition, not from the first
      // advancement result, so a cancel that lands before the first batch still
      // reports the real campaign size instead of "0 of 0".
      const auto failure = [&](core::ErrorCode code, std::string message) {
        progress->store(packCampaignProgress(GenerationCampaignProgress::Phase::Failed, 0U, 0U),
            std::memory_order_relaxed);
        return core::failure<Outcome>(code, std::move(message));
      };
      const auto bytes = core::readTextFileLimited(campaignPath, 32U * 1024U * 1024U);
      if (!bytes) return core::Result<Outcome>{bytes.error()};
      // Admission is a bounded, side-effect-free verification, so it runs without
      // the stop token: the campaign size must be reportable even when the user
      // cancels during startup. The advancement loop below still honours the stop
      // before every batch it would render or commit.
      const auto admitted = authoring::VerifiedGenerationCampaign::admit(bytes.value(), campaignSha256, {});
      if (!admitted) return failure(core::ErrorCode::Conflict, admitted.error().message);
      const auto* batchCount = admitted.value().plan().find("batchCount");
      if (batchCount == nullptr || !batchCount->isInteger() || batchCount->asInt64() < 0)
        return failure(core::ErrorCode::InvariantViolation, "Campaign definition has no batch count");
      std::size_t total = static_cast<std::size_t>(batchCount->asInt64());
      std::size_t completed = 0U;
      const auto cancelled = [&] {
        const auto previous = progress->load(std::memory_order_relaxed);
        const auto completedOutputs = static_cast<std::size_t>(previous & kCampaignCountMask);
        const auto totalOutputs = static_cast<std::size_t>((previous >> kCampaignTotalOutputsShift) & kCampaignCountMask);
        progress->store(packCampaignProgress(GenerationCampaignProgress::Phase::Cancelled,
            total, completed, completedOutputs, totalOutputs), std::memory_order_relaxed);
        return core::failure<Outcome>(core::ErrorCode::Conflict,
            "Campaign advancement cancelled; retained work may be resumed");
      };
      // The definition was already admitted above, so a stop that arrived while
      // this worker was starting still reports the real campaign size and stays
      // resumable instead of looking like a failure with no batches.
      if (stop.stop_requested()) return cancelled();
      progress->store(packCampaignProgress(GenerationCampaignProgress::Phase::Advancing, total, completed),
          std::memory_order_relaxed);
      while (true) {
        if (stop.stop_requested()) return cancelled();
        const auto stamp = fixedTimestamp ? occurredAtUtc
                                          : voicebank_studio_internal::currentUtcTimestamp();
        // The same producer operation the CLI runs: a fresh campaign is submitted to the
        // workspace and stale or exhausted requests keep their durable terminal outcome.
        progress->store(packCampaignProgress(GenerationCampaignProgress::Phase::Advancing,
            total, completed), std::memory_order_relaxed);
        const auto advanced = authoring::advanceGenerationRequest(root, campaignPath,
            campaignSha256, operatorId, stamp, stop,
            {.jobProgress = [progress, &total](std::size_t batchIndex, std::size_t done, std::size_t outputs) {
              const auto phase = done >= outputs ? GenerationCampaignProgress::Phase::Collecting
                                                  : GenerationCampaignProgress::Phase::Rendering;
              progress->store(packCampaignProgress(phase, total, batchIndex, done, outputs),
                  std::memory_order_relaxed);
            }});
        if (!advanced) {
          const auto previous = progress->load(std::memory_order_relaxed);
          progress->store(packCampaignProgress(GenerationCampaignProgress::Phase::Failed, total, completed,
              static_cast<std::size_t>(previous & kCampaignCountMask),
              static_cast<std::size_t>((previous >> kCampaignTotalOutputsShift) & kCampaignCountMask)),
              std::memory_order_relaxed);
          return core::Result<Outcome>{advanced.error()};
        }
        using Advance = authoring::CampaignAdvanceOutcome;
        const auto& report = advanced.value();
        completed = report.completedBatches;
        total = report.totalBatches;
        if (report.outcome == Advance::Cancelled) return cancelled();
        if (report.outcome == Advance::Stale || report.outcome == Advance::BudgetExhausted) {
          const auto previous = progress->load(std::memory_order_relaxed);
          progress->store(packCampaignProgress(GenerationCampaignProgress::Phase::Failed, total, completed,
              static_cast<std::size_t>(previous & kCampaignCountMask),
              static_cast<std::size_t>((previous >> kCampaignTotalOutputsShift) & kCampaignCountMask)),
              std::memory_order_relaxed);
          return core::failure<Outcome>(core::ErrorCode::Conflict,
              "Campaign " + authoring::toString(report.outcome) + ": " + report.detail);
        }
        const bool complete = report.outcome == Advance::Completed;
        progress->store(packCampaignProgress(complete ? GenerationCampaignProgress::Phase::Complete
                                                     : GenerationCampaignProgress::Phase::Advancing,
            total, completed), std::memory_order_relaxed);
        if (complete) break;
      }
      const auto durable = repository.recover();
      if (!durable) {
        progress->store(packCampaignProgress(GenerationCampaignProgress::Phase::Failed, total, completed),
            std::memory_order_relaxed);
        return core::Result<Outcome>{durable.error()};
      }
      return Outcome{.campaignPath = campaignPath, .campaignSha256 = campaignSha256,
          .completedBatches = completed, .totalBatches = total,
          .producer = std::move(durable).value(), .adoptedProducer = true,
          .status = campaignCountStatus("CAMPAIGN COLLECTED", completed, total, "/ NOT REVIEWED")};
    });
  } catch (const std::exception& error) {
    return core::failure(core::ErrorCode::Internal, "Unable to start campaign advancement worker", error.what());
  }
  status_ = "ADVANCING CAMPAIGN / ESC CANCEL";
  return core::success();
}

core::Result<void> VoicebankStudioController::beginGenerationCampaignPreflight(
    std::filesystem::path campaignPath, std::string campaignSha256) {
  using Outcome = GenerationCampaignOutcome;
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Production worker is busy");
  if (!productionProject_ || !productionRepository_)
    return core::failure(core::ErrorCode::InvalidState, "Campaign preflight requires a producer workspace");
  if (campaignPath.empty() || campaignSha256.size() != 64U)
    return core::failure(core::ErrorCode::InvalidArgument, "Campaign identity is invalid");
  const auto root = campaignPath.parent_path();
  if (root.empty())
    return core::failure(core::ErrorCode::InvalidArgument, "Campaign definition has no containing directory");
  // The held-out phrases are rendered into their own new directory beside the definition, under the
  // same rule the campaign itself follows: an existing preflight is verified, never replaced.
  const auto directory = root / "preflight";
  const auto workspace = productionWorkspaceRoot_;
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token();
  statusBeforeImport_ = status_;
  auto progress = std::make_shared<std::atomic<std::uint64_t>>(
      packCampaignProgress(GenerationCampaignProgress::Phase::Rendering, 0U, 0U));
  generationCampaignProgress_ = progress;
  try {
    campaignWork_ = std::async(std::launch::async,
        [campaignPath = std::move(campaignPath), campaignSha256 = std::move(campaignSha256),
         directory = std::move(directory), root, workspace, stop]() mutable -> core::Result<Outcome> {
      const auto bytes = core::readTextFileLimited(campaignPath, 32U * 1024U * 1024U);
      if (!bytes) return core::Result<Outcome>{bytes.error()};
      // A preflight already committed for this exact definition is admitted as it stands; the
      // phrases were rendered once and their report is the evidence a campaign may advance on.
      std::error_code probe;
      if (std::filesystem::exists(directory, probe)) {
        const auto existing = authoring::verifyCampaignPreflight(root, bytes.value(), campaignSha256, stop);
        if (existing) {
          return Outcome{.campaignPath = campaignPath, .campaignSha256 = campaignSha256,
              .producer = {}, .adoptedProducer = false,
              .status = "CAMPAIGN PREFLIGHT ALREADY ADMITTED / NOT GENERATED"};
        }
        // An interrupted or failed attempt leaves a directory the campaign can never advance on.
        // It is preserved under its own name rather than deleted, so a retry is possible without
        // destroying the phrases that were rendered before the interruption.
        bool preserved = false;
        for (std::size_t attempt = 1U; attempt <= 64U; ++attempt) {
          const auto spent = root / ("preflight-attempt-" + std::to_string(attempt));
          std::error_code probeSpent;
          if (std::filesystem::exists(spent, probeSpent)) continue;
          std::error_code renameError;
          std::filesystem::rename(directory, spent, renameError);
          preserved = !renameError;
          break;
        }
        if (!preserved)
          return core::failure<Outcome>(core::ErrorCode::Conflict,
              "An incomplete preflight is retained at preflight/ and no free attempt name is available to retry without deleting it",
              directory.string());
      }
      auto report = authoring::runInventoryPreflight(bytes.value(), campaignSha256, directory, {}, stop);
      if (!report) return core::Result<Outcome>{report.error()};
      // The producer is not touched by a preflight, so the durable state is re-read only to confirm
      // this session's workspace still matches what the campaign definition was planned from.
      voicebank_production::ProductionProjectRepository repository{workspace};
      const auto durable = repository.recover();
      if (!durable) return core::Result<Outcome>{durable.error()};
      const bool passed = report.value().passed;
      auto outcome = Outcome{.campaignPath = campaignPath, .campaignSha256 = campaignSha256,
          .producer = std::move(durable).value(), .adoptedProducer = false,
          .status = passed ? "CAMPAIGN PREFLIGHT PASSED / NOT GENERATED / NOT REVIEWED"
                           : "CAMPAIGN PREFLIGHT FAILED / " + std::to_string(report.value().defective) +
                                 " OF " + std::to_string(report.value().phrases.size()) +
                                 " PHRASES REFUSED / CAMPAIGN CANNOT ADVANCE",
          .preflightReport = std::move(report.value())};
      return outcome;
    });
  } catch (const std::exception& error) {
    return core::failure(core::ErrorCode::Internal, "Unable to start campaign preflight worker", error.what());
  }
  status_ = "RENDERING HELD-OUT PREFLIGHT PHRASES / ESC CANCEL";
  return core::success();
}

core::Result<void> VoicebankStudioController::beginGenerationCampaignResume(std::string occurredAtUtc) {
  if (campaignPath_.empty() || campaignSha256_.empty())
    return core::failure(core::ErrorCode::InvalidState,
        "No campaign identity is recorded; plan a campaign or advance one explicitly");
  return beginGenerationCampaignAdvance(campaignPath_, campaignSha256_, std::move(occurredAtUtc));
}

core::Result<void> VoicebankStudioController::refreshGenerationRequests() {
  if (proceduralImportBusy())
    return core::failure(core::ErrorCode::Conflict, "Production worker is busy");
  if (!productionProject_ || !productionRepository_ || productionWorkspaceRoot_.empty())
    return core::failure(core::ErrorCode::InvalidState, "Open a producer workspace before reading its generation queue");
  const auto root = productionWorkspaceRoot_;
  const auto epoch = productionSessionEpoch_;
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token();
  generationRequestsEpoch_ = epoch;
  generationRequestQueueStatus_ = "READING REQUEST QUEUE";
  try {
    generationRequestsWork_ = std::async(std::launch::async, [root, stop]()
        -> core::Result<std::vector<voicebank_production::GenerationRequestRecord>> {
      return voicebank_production::GenerationRequestRegistry{root}.list(stop);
    });
  } catch (const std::exception& error) {
    generationRequestQueueStatus_ = "REQUEST QUEUE FAILED";
    return core::failure(core::ErrorCode::Internal, "Unable to start generation request listing", error.what());
  }
  return core::success();
}

core::Result<void> VoicebankStudioController::beginGenerationRequestOutputInspection(
    std::string_view requestId, std::size_t firstJob, std::size_t maximumJobs,
    std::filesystem::path definitionPath) {
  if (proceduralImportBusy())
    return core::failure(core::ErrorCode::Conflict, "Production worker is busy");
  if (!productionProject_ || generationRequestsEpoch_ != productionSessionEpoch_)
    return core::failure(core::ErrorCode::InvalidState,
        "Refresh the generation queue for the current producer workspace first");
  if (maximumJobs == 0U || maximumJobs > 16U)
    return core::failure(core::ErrorCode::InvalidArgument,
        "Inspect between 1 and 16 generation jobs at a time");
  const auto found = std::find_if(generationRequests_.begin(), generationRequests_.end(),
      [requestId](const auto& record) { return record.request.requestId == requestId; });
  if (found == generationRequests_.end())
    return core::failure(core::ErrorCode::NotFound,
        "Generation request is not in the current queue snapshot");
  if (definitionPath.empty()) definitionPath = found->request.definitionLocator;
  if (definitionPath.empty())
    return core::failure(core::ErrorCode::NotFound,
        "Generation request has no campaign definition locator; locate the definition file");
  if (firstJob >= found->request.jobs.size())
    return core::failure(core::ErrorCode::InvalidArgument,
        "Generation output inspection page starts outside the request");
  const auto record = *found;
  const auto sessionEpoch = productionSessionEpoch_;
  const auto workspaceRoot = productionWorkspaceRoot_;
  if (sessionEpoch == 0U || generationRequestOutputInspectionEpoch_ ==
      std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::Conflict,
        "Generation output inspection epoch is unavailable");
  generationRequestOutputInspectionEpoch_ = sessionEpoch;
  generationRequestOutputInspectionPage_.reset();
  const auto last = firstJob + std::min(maximumJobs, record.request.jobs.size() - firstJob);
  generationRequestOutputInspectionStatus_ = "INSPECTING JOBS " + std::to_string(firstJob + 1U) +
      "-" + std::to_string(last) + "/" + std::to_string(record.request.jobs.size()) + " · READ-ONLY";
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token();
  try {
    generationRequestOutputInspectionWork_ = std::async(std::launch::async,
        [record, definitionPath = std::move(definitionPath), workspaceRoot, firstJob, maximumJobs, stop]() mutable {
      return inspectRequestOutputPage(record, definitionPath, workspaceRoot, firstJob, maximumJobs, stop);
    });
  } catch (const std::exception& error) {
    generationRequestOutputInspectionEpoch_ = 0U;
    generationRequestOutputInspectionStatus_ = "JOB OUTPUT INSPECTION FAILED";
    return core::failure(core::ErrorCode::Internal,
        "Unable to start generation output inspection", error.what());
  }
  status_ = generationRequestOutputInspectionStatus_;
  return core::success();
}

core::Result<void> VoicebankStudioController::cancelGenerationRequestOutputInspection() {
  if (!generationRequestOutputInspectionWork_.valid()) return core::success();
  generationRequestOutputInspectionEpoch_ = 0U;
  generationRequestOutputInspectionStatus_ = "CANCELLING JOB OUTPUT INSPECTION";
  proceduralImportStop_.request_stop();
  return core::success();
}

core::Result<void> VoicebankStudioController::beginGenerationRequestResume(
    std::string_view requestId, std::string occurredAtUtc) {
  if (proceduralImportBusy())
    return core::failure(core::ErrorCode::Conflict, "Production worker is busy");
  if (!productionProject_ || generationRequestsEpoch_ != productionSessionEpoch_)
    return core::failure(core::ErrorCode::InvalidState, "Refresh the generation queue for the current producer workspace first");
  const auto found = std::find_if(generationRequests_.begin(), generationRequests_.end(),
      [requestId](const auto& record) { return record.request.requestId == requestId; });
  if (found == generationRequests_.end())
    return core::failure(core::ErrorCode::NotFound, "Generation request is not in the current queue snapshot");
  if (found->terminal)
    return core::failure(core::ErrorCode::InvalidState,
        "Generation request is terminal: " + voicebank_production::toString(found->terminal->outcome));
  if (found->request.definitionLocator.empty())
    return core::failure(core::ErrorCode::NotFound, "Generation request has no campaign definition locator; locate the definition file");

  return beginGenerationRequestResumeFromDefinition(requestId,
      std::filesystem::path{found->request.definitionLocator}, std::move(occurredAtUtc));
}

core::Result<void> VoicebankStudioController::beginGenerationRequestResumeFromDefinition(
    std::string_view requestId, std::filesystem::path definitionPath, std::string occurredAtUtc) {
  if (proceduralImportBusy())
    return core::failure(core::ErrorCode::Conflict, "Production worker is busy");
  if (!productionProject_ || generationRequestsEpoch_ != productionSessionEpoch_)
    return core::failure(core::ErrorCode::InvalidState, "Refresh the generation queue for the current producer workspace first");
  const auto found = std::find_if(generationRequests_.begin(), generationRequests_.end(),
      [requestId](const auto& record) { return record.request.requestId == requestId; });
  if (found == generationRequests_.end())
    return core::failure(core::ErrorCode::NotFound, "Generation request is not in the current queue snapshot");
  if (found->terminal)
    return core::failure(core::ErrorCode::InvalidState,
        "Generation request is terminal: " + voicebank_production::toString(found->terminal->outcome));
  if (definitionPath.empty())
    return core::failure(core::ErrorCode::InvalidArgument, "Select a generation campaign definition file");

  // The stored locator or a user-selected replacement is only a path. The campaign
  // worker first reads and admits the bytes against this request ID; it does not
  // call the producer writer until that immutable-content check succeeds.
  return beginGenerationCampaignAdvance(std::move(definitionPath),
      found->request.requestId, std::move(occurredAtUtc));
}

void VoicebankStudioController::cancelGenerationCampaign() noexcept {
  proceduralImportStop_.request_stop();
}

}  // namespace seam::native_ui
