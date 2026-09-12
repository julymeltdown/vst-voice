#pragma once
#include "seam/authoring/inventory_generation.hpp"
#include "seam/formats/json_value.hpp"
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
[[nodiscard]] core::Result<CampaignAdvanceResult> advanceGenerationCampaign(
    const voicebank_production::ProductionProjectRepository& repository,
    const std::filesystem::path& campaignPath, std::string_view campaignSha256,
    std::string operatorId, std::string occurredAtUtc, std::stop_token stop = {},
    std::function<bool()> interruptBeforeReceipt = {});
}
