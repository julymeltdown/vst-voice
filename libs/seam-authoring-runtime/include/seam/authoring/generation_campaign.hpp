#pragma once
#include "seam/authoring/inventory_generation.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank_production/generation_request.hpp"
#include <memory>

namespace seam::authoring {
struct GenerationCampaignLimits final {
  std::size_t maximumJobs{16384U};
  std::uint64_t maximumFrames{512ULL * 1024ULL * 1024ULL};
  std::uint64_t maximumEstimatedBytes{8ULL * 1024ULL * 1024ULL * 1024ULL};
  GenerationBatchLimits batch{};
};
// Pure admission and immutable definition. Compiles one template at a time but
// never renders, prepares expectations, writes files or changes producer state.
// Caller may publish the returned bytes with a create-new atomic writer.
[[nodiscard]] core::Result<std::string> planGenerationCampaign(
    const voicebank_production::VoicebankProductionProject& producer,
    std::span<const std::string> plannedTakeIds,
    const synthesis::ProceduralSingerResource& recipe,
    GenerationCampaignLimits limits = {}, std::stop_token stop = {});
// Exact canonical reconstruction; a matching external digest alone is not
// sufficient to admit altered templates, totals, batch layout or hidden fields.
[[nodiscard]] core::Result<void> verifyGenerationCampaign(
    std::string_view definition, std::string_view expectedSha256, std::stop_token stop = {});
// The stable job ID of one campaign row: the same template and take always name the same job.
[[nodiscard]] std::string campaignJobId(std::string_view templateIdentity, std::string_view takeId);
// Immutable admission capability: no public constructor from unchecked JSON.
// Copies share the owned parsed plan. Each advancement admits once, not per batch.
class VerifiedGenerationCampaign final {
public:
  [[nodiscard]] static core::Result<VerifiedGenerationCampaign> admit(
      std::string_view definition, std::string_view expectedSha256, std::stop_token stop = {});
  [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(data_); }
  [[nodiscard]] const formats::JsonValue& plan() const { return data_->plan; }
  [[nodiscard]] std::string_view sha256() const { return data_->sha256; }
private:
  struct Data { std::string sha256; formats::JsonValue plan; };
  VerifiedGenerationCampaign(std::string sha256, formats::JsonValue plan)
      : data_(std::make_shared<const Data>(Data{std::move(sha256), std::move(plan)})) {}
  std::shared_ptr<const Data> data_;
};
struct PreparedCampaignBatch final {
  std::vector<GenerationJobReference> jobs;
  std::string batchSha256;
};
[[nodiscard]] core::Result<PreparedCampaignBatch> prepareGenerationCampaignBatch(
    const VerifiedGenerationCampaign& campaign, std::size_t batchIndex,
    const voicebank_production::VoicebankProductionProject& producer,
    const std::filesystem::path& directory,
    std::optional<voicebank_production::ProductionCommitReceipt> predecessor = {},
    std::stop_token stop = {});
// Internal orchestration primitive. For later batches the caller must provide
// the preceding verified collection receipt, not an arbitrary decoded JSON value.
// Retrying requires the same original producer snapshot and predecessor receipt.
[[nodiscard]] core::Result<PreparedCampaignBatch> prepareGenerationCampaignBatch(
    std::string_view definition, std::string_view campaignSha256, std::size_t batchIndex,
    const voicebank_production::VoicebankProductionProject& producer,
    const std::filesystem::path& directory,
    std::optional<voicebank_production::ProductionCommitReceipt> predecessor = {},
    std::stop_token stop = {});
struct CampaignAdvanceResult final {
  std::size_t completedBatches{0U}, totalBatches{0U};
  std::string producerSha256;
  bool complete{false};
};
struct CampaignStorageUsage final { std::uint64_t logicalBytes{0U}; std::size_t entries{0U}; };
// Bounded, read-only scan. Counts logical sizes (including sparse files), not
// allocated disk blocks. Rejects symlinks/special files and does not delete data.
[[nodiscard]] core::Result<CampaignStorageUsage> inspectCampaignStorage(
    const std::filesystem::path& root, std::uint64_t maximumBytes,
    std::size_t maximumEntries = 262144U, std::stop_token stop = {});
// The same scan without a byte limit, so an exhausted budget is reported with its measured size.
[[nodiscard]] core::Result<CampaignStorageUsage> measureCampaignStorage(
    const std::filesystem::path& root, std::size_t maximumEntries = 262144U, std::stop_token stop = {});
// Legacy form over the typed advancement below. It neither submits the campaign to the workspace
// nor records terminal outcomes; cancellation, staleness and budget exhaustion return errors.
[[nodiscard]] core::Result<CampaignAdvanceResult> advanceGenerationCampaign(
    const voicebank_production::ProductionProjectRepository& repository,
    const std::filesystem::path& campaignPath, std::string_view campaignSha256,
    std::string operatorId, std::string occurredAtUtc, std::stop_token stop = {},
    std::function<bool()> interruptBeforeReceipt = {});

// Every way one advancement can end without a fault. Integrity and I/O faults remain errors.
enum class CampaignAdvanceOutcome { BatchCollected, Completed, Cancelled, Stale, BudgetExhausted };
[[nodiscard]] std::string toString(CampaignAdvanceOutcome value);
struct CampaignAdvanceReport final {
  CampaignAdvanceOutcome outcome{CampaignAdvanceOutcome::Cancelled};
  std::size_t completedBatches{0U}, totalBatches{0U};
  // The durable producer state after this call; unchanged by every outcome except a collection.
  std::uint64_t producerGeneration{0U};
  std::string producerSha256;
  std::string requestId;
  bool registered{false}, terminalRecorded{false};
  std::uint64_t retainedBytes{0U};
  std::string detail;
};
struct CampaignAdvanceOptions final {
  // Fault-test seams; production callers leave them empty.
  std::function<bool()> interruptBeforeReceipt;
  std::function<void(std::size_t, std::size_t)> renderProgress;
  // Studio's live progress includes the actual batch index because one advance
  // may skip already-collected receipts before rendering the next batch.
  std::function<void(std::size_t, std::size_t, std::size_t)> jobProgress;
};
// Submits the verified campaign to the producer workspace as an immutable generation request,
// bound to the campaign's initial producer generation. Requires that exact current state.
[[nodiscard]] core::Result<voicebank_production::GenerationRequestRecord> submitGenerationCampaign(
    const std::filesystem::path& workspace, const std::filesystem::path& campaignPath,
    std::string_view campaignSha256, std::string operatorId, std::string occurredAtUtc, std::stop_token stop = {});
// The shared producer operation behind the CLI and Studio. Completes at most one bounded batch:
// prepare, render (reusing verified output) and collect through the canonical writer. A fresh
// campaign is submitted first. COMPLETED, STALE and BUDGET_EXHAUSTED become the request's durable
// terminal record and are returned again, without work, on every retry; CANCELLED keeps retained
// work resumable. No outcome approves material or changes the producer except a collection.
[[nodiscard]] core::Result<CampaignAdvanceReport> advanceGenerationRequest(
    const std::filesystem::path& workspace, const std::filesystem::path& campaignPath,
    std::string_view campaignSha256, std::string operatorId, std::string occurredAtUtc,
    std::stop_token stop = {}, CampaignAdvanceOptions options = {});
}
