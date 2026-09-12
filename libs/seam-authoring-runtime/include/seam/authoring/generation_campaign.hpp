#pragma once
#include "seam/authoring/inventory_generation.hpp"

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
struct PreparedCampaignBatch final {
  std::vector<GenerationJobReference> jobs;
  std::string batchSha256;
};
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
[[nodiscard]] core::Result<CampaignAdvanceResult> advanceGenerationCampaign(
    const voicebank_production::ProductionProjectRepository& repository,
    const std::filesystem::path& campaignPath, std::string_view campaignSha256,
    std::string operatorId, std::string occurredAtUtc, std::stop_token stop = {},
    std::function<bool()> interruptBeforeReceipt = {});
}
