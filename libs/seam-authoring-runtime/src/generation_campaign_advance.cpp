#include "seam/authoring/generation_campaign.hpp"
#include "seam/authoring/generation_batch_collection.hpp"
#include "seam/authoring/inventory_preflight.hpp"
#include "seam/voicebank_production/generation_request.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/exclusive_file_lock.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include <algorithm>
#include <utility>

namespace seam::authoring {
namespace {
namespace production = voicebank_production;
using Outcome = CampaignAdvanceOutcome;

std::string producerHash(const production::VoicebankProductionProject& producer) {
  return core::sha256Hex(production::encodeProductionProject(producer));
}

Outcome fromTerminal(production::GenerationRequestOutcome outcome) {
  switch (outcome) {
    case production::GenerationRequestOutcome::Completed: return Outcome::Completed;
    case production::GenerationRequestOutcome::Stale: return Outcome::Stale;
    case production::GenerationRequestOutcome::BudgetExhausted: return Outcome::BudgetExhausted;
  }
  return Outcome::Stale;
}

// The immutable request a verified campaign stands for: its initial producer, frozen recipe, every
// row's job ID, identity, duration and batch, and the campaign's own admitted budgets.
core::Result<production::GenerationRequest> campaignRequest(const VerifiedGenerationCampaign& campaign,
    const std::filesystem::path& campaignPath, std::string operatorId, std::string occurredAtUtc, std::stop_token stop) {
  using Output = production::GenerationRequest;
  const auto& plan = campaign.plan();
  const auto initial = production::decodeProductionProject(plan.find("initialProducerJson")->asString());
  if (!initial) return core::Result<Output>{initial.error()};
  const auto recipe = voice_design::decodeVoiceRecipe(plan.find("recipeJson")->asString());
  if (!recipe) return core::Result<Output>{recipe.error()};
  const auto resource = voice_design::freezeVoiceRecipeResource(recipe.value(), stop);
  if (!resource) return core::Result<Output>{resource.error()};
  const auto integer = [&](const char* key) { return plan.find(key)->asInt64(); };
  Output request{.requestId = std::string{campaign.sha256()},
      .definitionLocator = std::filesystem::absolute(campaignPath).lexically_normal().string(),
      .expectedGeneration = static_cast<std::int64_t>(initial.value().lastDurableGeneration),
      .expectedProjectSha256 = plan.find("initialProducerSha256")->asString(), .language = initial.value().language,
      .recipeId = resource.value().identity.id, .recipeVersion = resource.value().identity.version,
      .recipeHash = resource.value().identity.contentHash,
      .budget = {.maximumJobs = integer("maximumJobs"), .maximumFrames = integer("maximumFrames"),
          .maximumBytes = integer("maximumEstimatedBytes"), .batchMaximumJobs = integer("batchMaximumJobs"),
          .batchMaximumFrames = integer("batchMaximumFrames")},
      .jobs = {}, .submittedBy = std::move(operatorId), .submittedAtUtc = std::move(occurredAtUtc)};
  for (const auto& row : plan.find("jobs")->asArray()) {
    const auto& takeId = row.find("takeId")->asString();
    request.jobs.push_back({.jobId = campaignJobId(row.find("templateIdentity")->asString(), takeId), .takeId = takeId,
        .style = row.find("style")->asString(), .coverageKey = row.find("coverageKey")->asString(),
        .pitchLayer = static_cast<std::int32_t>(row.find("pitchLayer")->asInt64()),
        .frameCount = row.find("frameCount")->asInt64(), .batchIndex = row.find("batchIndex")->asInt64()});
  }
  return request;
}

// One advancement over the campaign's durable receipts. With a registry, a fresh campaign is
// submitted and every terminal outcome becomes the request's retained record; without one (the
// legacy entry point) nothing outside the campaign directory and the producer is written.
core::Result<CampaignAdvanceReport> advanceCampaign(const production::ProductionProjectRepository& repository,
    const production::GenerationRequestRegistry* registry, const std::filesystem::path& campaignPath,
    std::string_view campaignSha256, const std::string& operatorId, const std::string& occurredAtUtc,
    std::stop_token stop, const CampaignAdvanceOptions& options) {
  using Output = CampaignAdvanceReport;
  const auto fail = [](std::string message) { return core::failure<Output>(core::ErrorCode::Conflict, std::move(message)); };
  const std::string cancelledDetail = "Campaign advancement cancelled; retained work may be resumed";
  Output report;
  report.requestId = std::string{campaignSha256};
  // Every returned outcome carries the durable producer state it left behind.
  const auto finish = [&](Outcome outcome, std::size_t completed, std::string detail) -> core::Result<Output> {
    report.outcome = outcome;
    report.completedBatches = completed;
    report.detail = std::move(detail);
    const auto current = repository.recover();
    if (!current) return core::Result<Output>{current.error()};
    report.producerGeneration = current.value().lastDurableGeneration;
    report.producerSha256 = producerHash(current.value());
    return report;
  };
  const auto bytes = core::readTextFileLimited(campaignPath, 32U * 1024U * 1024U);
  if (!bytes) return core::Result<Output>{bytes.error()};
  const auto verified = VerifiedGenerationCampaign::admit(bytes.value(), campaignSha256, stop);
  if (!verified) {
    if (stop.stop_requested()) return finish(Outcome::Cancelled, 0U, cancelledDetail);
    return core::Result<Output>{verified.error()};
  }
  const auto& plan = verified.value().plan();
  report.totalBatches = static_cast<std::size_t>(plan.find("batchCount")->asInt64());
  const auto initialHash = plan.find("initialProducerSha256")->asString();
  const auto initial = production::decodeProductionProject(plan.find("initialProducerJson")->asString());
  if (!initial) return core::Result<Output>{initial.error()};
  auto before = repository.recoverGeneration(initial.value().lastDurableGeneration, initialHash);
  if (!before) return core::Result<Output>{before.error()};
  if (!production::isProductionUtcTimestamp(occurredAtUtc) || std::none_of(before.value().operators.begin(),
      before.value().operators.end(), [&](const auto& entry) { return entry.operatorId == operatorId; }))
    return fail("Campaign operator or timestamp is invalid");
  std::optional<production::GenerationRequestRecord> record;
  if (registry != nullptr) {
    const auto found = registry->find(campaignSha256, stop);
    if (!found) {
      if (stop.stop_requested()) return finish(Outcome::Cancelled, 0U, cancelledDetail);
      return core::Result<Output>{found.error()};
    }
    record = found.value();
    if (record && record->terminal) {
      // A terminal request is never worked again: every retry returns the retained outcome.
      const auto& terminal = *record->terminal;
      report.registered = report.terminalRecorded = true;
      report.retainedBytes = static_cast<std::uint64_t>(terminal.retainedBytes);
      return finish(fromTerminal(terminal.outcome), static_cast<std::size_t>(terminal.completedBatches), terminal.detail);
    }
  }
  const auto root = std::filesystem::absolute(campaignPath).parent_path();
  // A campaign may not multiply a phone class across the bank until the held-out
  // phrases that exercise every class it declares have rendered audibly.
  const auto preflight = verifyCampaignPreflight(root, bytes.value(), campaignSha256, stop);
  if (!preflight) {
    if (stop.stop_requested()) return finish(Outcome::Cancelled, 0U, cancelledDetail);
    return fail("Campaign preflight is not admitted: " + preflight.error().message);
  }
  if (registry != nullptr && !record) {
    // Only a campaign whose producer is still at its initial state can be submitted. A campaign
    // advanced before submission existed keeps working unregistered, as it always did.
    const auto current = repository.recover();
    if (!current) return core::Result<Output>{current.error()};
    if (producerHash(current.value()) == initialHash) {
      const auto request = campaignRequest(verified.value(), campaignPath, operatorId, occurredAtUtc, stop);
      const auto submitted = request ? registry->submit(request.value(), stop)
                                     : core::Result<production::GenerationRequestRecord>{request.error()};
      if (!submitted) {
        if (stop.stop_requested()) return finish(Outcome::Cancelled, 0U, cancelledDetail);
        return core::Result<Output>{submitted.error()};
      }
      record = submitted.value();
    }
  }
  report.registered = record.has_value();
  // Terminal outcomes of a registered request are recorded before they are reported.
  const auto conclude = [&](Outcome outcome, std::size_t completed, std::string detail, std::uint64_t retained,
                            std::optional<std::pair<std::uint64_t, std::string>> observed) -> core::Result<Output> {
    report.retainedBytes = retained;
    auto finished = finish(outcome, completed, std::move(detail));
    if (!finished || !record || outcome == Outcome::BatchCollected || outcome == Outcome::Cancelled) return finished;
    const production::GenerationRequestTerminal terminal{
        .outcome = outcome == Outcome::Completed ? production::GenerationRequestOutcome::Completed
                 : outcome == Outcome::Stale     ? production::GenerationRequestOutcome::Stale
                                                 : production::GenerationRequestOutcome::BudgetExhausted,
        .observedGeneration = static_cast<std::int64_t>(observed ? observed->first : report.producerGeneration),
        .observedProjectSha256 = observed ? observed->second : report.producerSha256,
        .completedBatches = static_cast<std::int64_t>(completed),
        .retainedBytes = outcome == Outcome::BudgetExhausted ? static_cast<std::int64_t>(retained) : 0,
        .detail = report.detail, .recordedBy = operatorId, .recordedAtUtc = occurredAtUtc};
    // The outcome has already happened, so recording it is not cancellable.
    const auto recorded = registry->recordTerminal(campaignSha256, terminal);
    if (!recorded) return core::Result<Output>{recorded.error()};
    report.terminalRecorded = true;
    return report;
  };
  const auto cancelledOr = [&](const core::Error& error, std::size_t completed) -> core::Result<Output> {
    if (stop.stop_requested()) return conclude(Outcome::Cancelled, completed, cancelledDetail, 0U, std::nullopt);
    return core::Result<Output>{error};
  };
  const auto storageLimit = static_cast<std::uint64_t>(plan.find("maximumEstimatedBytes")->asInt64());
  // The measured retained size when it exceeds the admitted budget, otherwise nothing.
  const auto exhausted = [&]() -> core::Result<std::optional<std::uint64_t>> {
    const auto usage = measureCampaignStorage(root, 262144U, stop);
    if (!usage) return core::Result<std::optional<std::uint64_t>>{usage.error()};
    if (usage.value().logicalBytes > storageLimit) return std::optional<std::uint64_t>{usage.value().logicalBytes};
    return std::optional<std::uint64_t>{};
  };
  const auto overBudget = [&](std::size_t completed, std::uint64_t retained) {
    return conclude(Outcome::BudgetExhausted, completed, "Campaign retains " + std::to_string(retained) +
        " bytes, above its admitted " + std::to_string(storageLimit), retained, std::nullopt);
  };
  core::ExclusiveFileLock lock;
  const auto locked = lock.acquire(root / ".campaign-advance.lock");
  if (!locked) return core::Result<Output>{locked.error()};
  const auto total = report.totalBatches;
  const GenerationBatchLimits limits{static_cast<std::size_t>(plan.find("batchMaximumJobs")->asInt64()),
      static_cast<std::uint64_t>(plan.find("batchMaximumFrames")->asInt64())};
  std::optional<production::ProductionCommitReceipt> predecessor;
  for (std::size_t index = 0U; index < total; ++index) {
    if (stop.stop_requested()) return conclude(Outcome::Cancelled, index, cancelledDetail, 0U, std::nullopt);
    const auto directory = root / ("batch-" + std::to_string(index));
    const auto receipt = directory / "collection.json";
    std::error_code error;
    const bool hasReceipt = std::filesystem::exists(receipt, error);
    if (error) return fail("Cannot inspect campaign collection receipt");
    const auto beforeHash = producerHash(before.value());
    if (!hasReceipt) {
      const auto over = exhausted();
      if (!over) return cancelledOr(over.error(), index);
      if (over.value()) return overBudget(index, *over.value());
      const auto current = repository.recover();
      if (!current) return core::Result<Output>{current.error()};
      const bool retainedInputs = std::filesystem::is_regular_file(directory / "batch-inputs.json", error);
      if (error == std::errc::no_such_file_or_directory) error.clear();
      if (error) return fail("Cannot inspect campaign preparation inputs");
      // One advanced generation can be the batch's commit-before-receipt window.
      // Recognition below must prove it; no other external change is adopted.
      if (producerHash(current.value()) != beforeHash && (!retainedInputs ||
          current.value().lastDurableGeneration <= before.value().lastDurableGeneration ||
          current.value().lastDurableGeneration - before.value().lastDurableGeneration != 1U))
        return conclude(Outcome::Stale, index, "Producer changed outside the campaign's expected transition", 0U, std::nullopt);
    }
    const auto prepared = prepareGenerationCampaignBatch(verified.value(), index, before.value(), directory, predecessor, stop);
    if (!prepared) return cancelledOr(prepared.error(), index);
    if (hasReceipt) {
      const auto after = loadVerifiedGenerationBatchReceipt(repository, before.value(), prepared.value().jobs, receipt, limits, stop);
      if (!after) return cancelledOr(after.error(), index);
      before = after;
      predecessor = production::ProductionCommitReceipt{after.value().lastDurableGeneration, producerHash(after.value()), true, {}};
      continue;
    }
    const auto preparedOver = exhausted();
    if (!preparedOver) return cancelledOr(preparedOver.error(), index);
    if (preparedOver.value()) return overBudget(index, *preparedOver.value());
    const auto inputs = inspectGenerationBatch(prepared.value().jobs, limits, stop);
    if (!inputs) return cancelledOr(inputs.error(), index);
    const auto collectedCount = [&]() -> core::Result<std::size_t> {
      const auto current = repository.recover();
      if (!current) return core::Result<std::size_t>{current.error()};
      std::size_t count = 0U;
      for (const auto& input : inputs.value()) {
        const auto found = repository.findCollectedGeneration(input.expectation);
        if (found) {
          if (found.value()) ++count;
          continue;
        }
        // Another result already holds this take ID, so this request's output can never be
        // collected for it. The state comparison that follows reports that as staleness.
        const bool occupied = found.error().code == core::ErrorCode::Conflict &&
            std::any_of(current.value().takes.begin(), current.value().takes.end(),
                        [&](const auto& take) { return take.takeId == input.expectation.takeId; });
        if (!occupied) return core::Result<std::size_t>{found.error()};
      }
      return count;
    };
    const auto recognized = collectedCount();
    if (!recognized) return core::Result<Output>{recognized.error()};
    if (recognized.value() != 0U && recognized.value() != prepared.value().jobs.size())
      return fail("Partially collected campaign batch needs reconciliation");
    if (recognized.value() == 0U) {
      const auto current = repository.recover();
      if (!current) return core::Result<Output>{current.error()};
      if (producerHash(current.value()) != beforeHash)
        return conclude(Outcome::Stale, index, "Producer changed before campaign rendering", 0U, std::nullopt);
      const auto rendered = runGenerationBatch(prepared.value().jobs, limits, stop,
          [&](std::size_t done, std::size_t outputs) {
            if (options.renderProgress) options.renderProgress(done, outputs);
            if (options.jobProgress) options.jobProgress(index, done, outputs);
          });
      if (!rendered) return cancelledOr(rendered.error(), index);
      const auto renderedOver = exhausted();
      if (!renderedOver) return cancelledOr(renderedOver.error(), index);
      if (renderedOver.value()) return overBudget(index, *renderedOver.value());
    }
    const auto collected = collectGenerationBatchWithReceipt(repository, before.value(), prepared.value().jobs, receipt,
        {"import-generated-batch", prepared.value().batchSha256, operatorId, occurredAtUtc}, limits, stop,
        options.interruptBeforeReceipt);
    if (!collected) {
      // Classify from durable state: a producer that moved without this batch is stale, a
      // committed batch waits for its receipt, and anything else is a stop or a fault.
      const auto current = repository.recover();
      if (!current) return core::Result<Output>{current.error()};
      if (producerHash(current.value()) != beforeHash) {
        const auto committed = collectedCount();
        if (!committed) return core::Result<Output>{committed.error()};
        if (committed.value() == 0U)
          return conclude(Outcome::Stale, index, "Producer changed before batch collection", 0U, std::nullopt);
        return core::Result<Output>{collected.error()};
      }
      return cancelledOr(collected.error(), index);
    }
    const auto completed = index + 1U;
    if (completed == total)
      return conclude(Outcome::Completed, total, "Every campaign batch is collected as unreviewed material", 0U,
          std::pair<std::uint64_t, std::string>{collected.value().committedGeneration, collected.value().committedProjectSha256});
    const auto collectedOver = exhausted();
    if (!collectedOver) return cancelledOr(collectedOver.error(), completed);
    if (collectedOver.value()) return overBudget(completed, *collectedOver.value());
    return conclude(Outcome::BatchCollected, completed, "Campaign batch collected as unreviewed material", 0U, std::nullopt);
  }
  // Every receipt verified: the campaign completed, whatever reviews or edits followed it.
  return conclude(Outcome::Completed, total, "Every campaign batch is collected as unreviewed material", 0U,
      std::pair<std::uint64_t, std::string>{before.value().lastDurableGeneration, producerHash(before.value())});
}
}  // namespace

std::string toString(CampaignAdvanceOutcome value) {
  switch (value) {
    case Outcome::BatchCollected: return "BATCH_COLLECTED";
    case Outcome::Completed: return "COMPLETED";
    case Outcome::Cancelled: return "CANCELLED";
    case Outcome::Stale: return "STALE";
    case Outcome::BudgetExhausted: return "BUDGET_EXHAUSTED";
  }
  return "CANCELLED";
}

core::Result<production::GenerationRequestRecord> submitGenerationCampaign(
    const std::filesystem::path& workspace, const std::filesystem::path& campaignPath,
    std::string_view campaignSha256, std::string operatorId, std::string occurredAtUtc, std::stop_token stop) {
  using Output = production::GenerationRequestRecord;
  const auto bytes = core::readTextFileLimited(campaignPath, 32U * 1024U * 1024U);
  if (!bytes) return core::Result<Output>{bytes.error()};
  const auto verified = VerifiedGenerationCampaign::admit(bytes.value(), campaignSha256, stop);
  if (!verified) return core::Result<Output>{verified.error()};
  const auto request = campaignRequest(verified.value(), campaignPath, std::move(operatorId), std::move(occurredAtUtc), stop);
  if (!request) return core::Result<Output>{request.error()};
  return production::GenerationRequestRegistry{workspace}.submit(request.value(), stop);
}

core::Result<CampaignAdvanceReport> advanceGenerationRequest(
    const std::filesystem::path& workspace, const std::filesystem::path& campaignPath,
    std::string_view campaignSha256, std::string operatorId, std::string occurredAtUtc,
    std::stop_token stop, CampaignAdvanceOptions options) {
  const production::ProductionProjectRepository repository{workspace};
  const production::GenerationRequestRegistry registry{workspace};
  return advanceCampaign(repository, &registry, campaignPath, campaignSha256, operatorId, occurredAtUtc, stop, options);
}

core::Result<CampaignAdvanceResult> advanceGenerationCampaign(
    const voicebank_production::ProductionProjectRepository& repository,
    const std::filesystem::path& campaignPath, std::string_view campaignSha256,
    std::string operatorId, std::string occurredAtUtc, std::stop_token stop,
    std::function<bool()> interruptBeforeReceipt) {
  const CampaignAdvanceOptions options{.interruptBeforeReceipt = std::move(interruptBeforeReceipt), .renderProgress = {}};
  const auto report = advanceCampaign(repository, nullptr, campaignPath, campaignSha256, operatorId, occurredAtUtc, stop, options);
  if (!report) return core::Result<CampaignAdvanceResult>{report.error()};
  const auto& value = report.value();
  if (value.outcome != Outcome::BatchCollected && value.outcome != Outcome::Completed)
    return core::failure<CampaignAdvanceResult>(core::ErrorCode::Conflict, value.detail);
  return CampaignAdvanceResult{value.completedBatches, value.totalBatches, value.producerSha256,
                               value.outcome == Outcome::Completed};
}
}
