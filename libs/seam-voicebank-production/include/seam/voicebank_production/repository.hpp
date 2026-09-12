#pragma once

#include "seam/core/result.hpp"
#include "seam/voicebank_production/asset_store.hpp"
#include "seam/voicebank_production/operations.hpp"
#include "seam/synthesis/singer_resource.hpp"

#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace seam::synthesis { struct ProceduralSingerResource; }
namespace seam::voicebank_production {

struct RawTakeInput final {
  std::string takeId;
  std::string promptId;
  std::string coverageKey;
  std::int32_t pitchLayer{0};
  std::string supersedesTakeId;
  UnitQueueState initialState{UnitQueueState::MarkerReview};
  std::optional<ReviewRecord> review;
  std::string style;
};

// Captured before worker generation. Integrity/staleness check, not a signature
// or source approval. The durable job layer must retain this original value.
struct GenerationImportExpectation final {
  std::string projectStateSha256;
  std::string takeId, promptId, coverageKey, supersedesTakeId;
  std::int32_t pitchLayer{0};
  std::string recipeId, recipeVersion, recipeHash, style, renderContentHash;
  std::uint32_t sampleRate{0U};
  std::int64_t frameCount{0};
  // Empty preserves the legacy expectation-v1 bytes. Style-owned workspaces
  // capture an explicit language in expectation v2.
  std::string language{};
  friend bool operator==(const GenerationImportExpectation&, const GenerationImportExpectation&) = default;
};
struct CollectedGenerationResult final {
  std::string takeId, audioSha256;
  std::uint64_t generation{0U};
  UnitQueueState state{UnitQueueState::MarkerReview};
  bool active{false};
};
struct GeneratedCandidateInput final {
  std::filesystem::path metadataPath, audioPath;
  synthesis::ProceduralSingerResource recipe;
  GenerationImportExpectation expectation;
};

[[nodiscard]] core::Result<std::string> encodeGenerationImportExpectation(const GenerationImportExpectation& expectation);
// New-file-only publication returns the digest the owning job must retain.
[[nodiscard]] core::Result<std::string> saveGenerationImportExpectation(
    const std::filesystem::path& path, const GenerationImportExpectation& expectation);
[[nodiscard]] core::Result<GenerationImportExpectation> loadGenerationImportExpectation(
    const std::filesystem::path& path, std::string_view expectedSha256);

[[nodiscard]] core::Result<GenerationImportExpectation> captureGenerationImportExpectation(
    const VoicebankProductionProject& project, const RawTakeInput& take,
    const synthesis::ProceduralSingerResource& recipe, std::string style,
    std::string renderContentHash, std::uint32_t sampleRate, std::int64_t frameCount);

struct StagedOperation final {
  std::string stagingId;
  std::string inputSha256;
  std::string outputSha256;
  std::filesystem::path path;
  OperationRequest request;
  std::string takeId;
  std::string parentRevisionId;
  std::string sourceProjectSha256;
};

struct ExportedU57Inputs final {
  std::filesystem::path briefPath;
  std::filesystem::path candidateTemplatePath;
  std::string status;
};

// Transient commit information, never serialized as source or edit history.
// A returned value means committed; pointer/directory durability may still need
// inspection before retrying or issuing the next dependent operation.
struct ProductionCommitReceipt {
  std::uint64_t committedGeneration{0U};
  std::string committedProjectSha256;
  bool durabilityConfirmed{true};
  std::string diagnostic;
};
struct CommittedDerivedRevision final : DerivedRevision, ProductionCommitReceipt {};
struct CommittedMetadataRevision final : MetadataRevision, ProductionCommitReceipt {};
struct CommittedAssetRecord final : AssetRecord, ProductionCommitReceipt {};
struct CommittedImportBatch final : ProductionCommitReceipt {
  std::vector<AssetRecord> assets;
};

class ProductionProjectRepository final {
public:
  explicit ProductionProjectRepository(std::filesystem::path root);

  [[nodiscard]] core::Result<void> initialize(
      VoicebankProductionProject& project,
      const ProductionJournalEvent& event);
  [[nodiscard]] core::Result<void> save(
      VoicebankProductionProject& project,
      const ProductionJournalEvent& event, std::stop_token stopToken = {});
  [[nodiscard]] core::Result<VoicebankProductionProject> recover() const;
  // Read a specific verified historical state without changing the current pointer.
  [[nodiscard]] core::Result<VoicebankProductionProject> recoverGeneration(
      std::uint64_t generation, std::string_view expectedProjectSha256) const;
  // Under the normal writer lock, republish only the verified latest pointer.
  // No generation/journal append or source/review changes. Exact state required.
  [[nodiscard]] core::Result<void> reconcileCurrentPointer(
      std::uint64_t expectedGeneration, std::string_view expectedProjectSha256,
      std::stop_token stopToken = {}) const;
  // Read-only recognition of a previously committed original request.
  [[nodiscard]] core::Result<std::optional<CollectedGenerationResult>> findCollectedGeneration(
      const GenerationImportExpectation& expectation, std::uint64_t generation = 0U,
      std::string_view expectedProjectSha256 = {}) const;
  [[nodiscard]] core::Result<CommittedImportBatch> importGeneratedBatch(
      VoicebankProductionProject& project, std::span<const GeneratedCandidateInput> inputs,
      const ProductionJournalEvent& event, std::uint64_t maximumFrames = 32ULL * 1024ULL * 1024ULL,
      std::stop_token stopToken = {});
  [[nodiscard]] core::Result<CommittedAssetRecord> importRaw(
      VoicebankProductionProject& project,
      const std::filesystem::path& source,
      const RawTakeInput& take,
      const ProductionJournalEvent& event);
  [[nodiscard]] core::Result<CommittedAssetRecord> importProceduralCandidate(
      VoicebankProductionProject& project, const std::filesystem::path& metadataPath,
      const std::filesystem::path& audioPath, const synthesis::ProceduralSingerResource& recipe,
      const RawTakeInput& take, const ProductionJournalEvent& event,
      std::stop_token stopToken = {}, const GenerationImportExpectation* expectation = nullptr);
  [[nodiscard]] core::Result<StagedOperation> stageOperation(
      const VoicebankProductionProject& project,
      std::string_view takeId,
      std::string_view expectedParentRevisionId,
      const OperationRequest& request,
      std::string stagingId) const;
  [[nodiscard]] core::Result<CommittedDerivedRevision> commitStaged(
      VoicebankProductionProject& project,
      const StagedOperation& staged,
      std::string revisionId,
      std::string operatorId,
      std::string performedAtUtc,
      std::string_view takeId,
      // Captured before staging. Empty identifies the raw parent, not any
      // revision with matching bytes; equal hashes do not identify ownership.
      std::string_view expectedParentRevisionId);
  [[nodiscard]] core::Result<CommittedMetadataRevision> recordMetadataRevision(
      VoicebankProductionProject& project,
      MetadataRevision revision,
      const ProductionJournalEvent& event);
  [[nodiscard]] std::vector<std::filesystem::path> inspectStaged(
      const VoicebankProductionProject& project) const;
  [[nodiscard]] core::Result<void> verify(
      const VoicebankProductionProject& project) const;
  [[nodiscard]] core::Result<ProductionCommitReceipt> recordSourceQualityAssessment(
      VoicebankProductionProject& project, const SourceQualityAssessment& assessment,
      const std::filesystem::path& evidenceFile, std::string_view expectedProjectSha256,
      std::stop_token stopToken = {});
  // Append and select a new, explicitly declared source. Never rewrites an
  // existing policy or attributes legacy takes; musical outcomes stay unassessed.
  [[nodiscard]] core::Result<ProductionCommitReceipt> registerSource(
      VoicebankProductionProject& project, const SourceStrategyAssessment& source,
      std::string_view expectedProjectSha256, std::string producerId,
      std::string occurredAtUtc, std::stop_token stopToken = {});
  [[nodiscard]] core::Result<ExportedU57Inputs> exportU57Inputs(
      VoicebankProductionProject& project,
      const std::filesystem::path& destination,
      const ProductionJournalEvent& event);
  [[nodiscard]] std::filesystem::path assetPath(
      const AssetRecord& asset) const;

private:
  [[nodiscard]] core::Result<AssetRecord> importProceduralCandidateBound(
      VoicebankProductionProject& project, const std::filesystem::path& metadataPath,
      const std::filesystem::path& audioPath, const synthesis::ProceduralSingerResource& recipe,
      const RawTakeInput& take, const ProductionJournalEvent& event, std::stop_token stopToken,
      const GenerationImportExpectation* expectation, std::string_view originalState);
  [[nodiscard]] core::Result<AssetRecord> importRawBound(
      VoicebankProductionProject& project, const std::filesystem::path& source,
      const RawTakeInput& take, const ProductionJournalEvent& event,
      std::string_view expectedDigest, const std::optional<MetadataRevision>& lineage,
      std::stop_token stopToken = {});
  [[nodiscard]] std::filesystem::path generationPath(
      std::uint64_t generation) const;
  [[nodiscard]] std::filesystem::path journalPath(
      std::uint64_t generation) const;
  [[nodiscard]] core::Result<void> verifyGeneration(
      const VoicebankProductionProject& project,
      bool requireCurrentPointer) const;

  std::filesystem::path root_;
  ImmutableAssetStore assetStore_;
};

}
