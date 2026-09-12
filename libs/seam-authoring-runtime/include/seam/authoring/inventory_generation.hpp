#pragma once
#include "seam/authoring/generation_job.hpp"

namespace seam::authoring {
// Template revision 1: one 960-tick note at 120 BPM, explicit inventory phones.
// Pure construction: no source approval, assignment mutation or job expectation.
struct InventoryGenerationScore final {
  domain::Project project;
  domain::TrackId trackId;
  domain::RegionId regionId;
  std::string templateIdentity;
};
[[nodiscard]] core::Result<InventoryGenerationScore> buildInventoryGenerationScore(
    const voicebank_production::VoicebankProductionProject& producer,
    std::string_view plannedTakeId);
// Invoke only immediately before the batch runs, against current producer state.
// Score and job destinations must be new. Failed preparation retains its score.
[[nodiscard]] core::Result<PreparedGenerationJob> prepareInventoryGenerationJob(
    const std::filesystem::path& scorePath, const std::filesystem::path& jobDirectory,
    const voicebank_production::VoicebankProductionProject& producer,
    std::string_view plannedTakeId, GenerationRecipeSelection recipe,
    std::stop_token stop = {});
}
