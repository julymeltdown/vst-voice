#pragma once
#include "seam/rendering/render_snapshot.hpp"
#include "seam/authoring/export_service.hpp"
#include "seam/voicebank_production/repository.hpp"

namespace seam::authoring {
struct PreparedGenerationJob final {
  std::string jobId, manifestSha256;
  rendering::RenderSnapshot snapshot;
  voicebank_production::GenerationImportExpectation expectation;
};
struct GenerationJobOutput final {
  std::filesystem::path metadataPath, audioPath;
  std::string audioSha256;
  bool reused{false};
};
struct GenerationJobReference final { std::filesystem::path directory; std::string manifestSha256; };
[[nodiscard]] core::Result<GenerationJobReference> loadGenerationJobReference(const std::filesystem::path& path);
struct GenerationBatchLimits final {
  std::size_t maximumJobs{64U};
  std::uint64_t maximumFrames{32ULL * 1024ULL * 1024ULL};
};
[[nodiscard]] core::Result<std::vector<GenerationJobReference>> loadGenerationBatch(
    const std::filesystem::path& path, std::string_view expectedSha256);
// Publishes a new batch manifest after shared admission checks. Keeps original
// job digests/producer expectations; does not generate or collect material.
[[nodiscard]] core::Result<std::string> saveGenerationBatch(
    const std::filesystem::path& path, std::span<const GenerationJobReference> jobs,
    GenerationBatchLimits limits = {}, std::stop_token stopToken = {}, std::string_view expectedProducerSha256 = {});
// Verifies frozen inputs and batch admission, not the output files.
[[nodiscard]] core::Result<std::vector<voicebank_production::GeneratedCandidateInput>> inspectGenerationBatch(
    std::span<const GenerationJobReference> jobs, GenerationBatchLimits limits = {}, std::stop_token stopToken = {});
[[nodiscard]] core::Result<std::vector<GenerationJobOutput>> runGenerationBatch(
    std::span<const GenerationJobReference> jobs, GenerationBatchLimits limits = {},
    std::stop_token stopToken = {}, std::function<void(std::size_t, std::size_t)> progress = {});
[[nodiscard]] core::Result<GenerationJobOutput> runGenerationJob(
    const std::filesystem::path& directory, std::string_view expectedManifestSha256,
    std::stop_token stopToken = {},
    std::function<bool(ExportPublicationPhase)> publicationFaultInjector = {});
// May recover journal-owned publication, but never generates missing output.
[[nodiscard]] core::Result<GenerationJobOutput> verifyGenerationJobOutput(
    const std::filesystem::path& directory, std::string_view expectedManifestSha256, std::stop_token stopToken = {});
// Publication of the manifest is last. An incomplete directory is not a job.
[[nodiscard]] core::Result<PreparedGenerationJob> prepareGenerationJob(
    const std::filesystem::path& directory, std::string jobId,
    const rendering::RenderSnapshot& snapshot,
    const voicebank_production::VoicebankProductionProject& producer,
    const voicebank_production::RawTakeInput& take);
// Shared saved-score entry point for CLI and native producers. Does not mutate or
// reserve a producer assignment; the frozen expectation governs later collection.
struct GenerationRecipeSelection final {
  synthesis::ProceduralSingerResource resource;
  std::string style;
};
[[nodiscard]] core::Result<PreparedGenerationJob> prepareGenerationJobFromScore(
    const std::filesystem::path& directory, std::string jobId,
    const std::filesystem::path& scorePath, domain::TrackId trackId, domain::RegionId regionId,
    const voicebank_production::VoicebankProductionProject& producer, std::string_view plannedTakeId,
    std::stop_token stopToken = {}, std::string_view expectedScoreSha256 = {},
    std::optional<GenerationRecipeSelection> selectedRecipe = std::nullopt);
[[nodiscard]] core::Result<PreparedGenerationJob> loadGenerationJob(
    const std::filesystem::path& directory, std::string_view expectedManifestSha256);
}
