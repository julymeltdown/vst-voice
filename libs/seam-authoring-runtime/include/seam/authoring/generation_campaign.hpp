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
}
