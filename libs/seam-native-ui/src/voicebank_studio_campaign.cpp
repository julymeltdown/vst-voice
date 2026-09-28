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
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank_production/project_codec.hpp"

#include <algorithm>
#include <exception>
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
