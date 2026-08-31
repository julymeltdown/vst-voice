#pragma once

#include "seam/core/result.hpp"
#include "seam/voicebank_production/asset_store.hpp"
#include "seam/voicebank_production/operations.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace seam::voicebank_production {

struct RawTakeInput final {
  std::string takeId;
  std::string promptId;
  std::string coverageKey;
  std::int32_t pitchLayer{0};
  std::string supersedesTakeId;
  UnitQueueState initialState{UnitQueueState::MarkerReview};
  std::optional<ReviewRecord> review;
};

struct StagedOperation final {
  std::string stagingId;
  std::string inputSha256;
  std::string outputSha256;
  std::filesystem::path path;
  OperationRequest request;
};

struct ExportedU57Inputs final {
  std::filesystem::path briefPath;
  std::filesystem::path candidateTemplatePath;
  std::string status;
};

class ProductionProjectRepository final {
public:
  explicit ProductionProjectRepository(std::filesystem::path root);

  [[nodiscard]] core::Result<void> initialize(
      VoicebankProductionProject& project,
      const ProductionJournalEvent& event);
  [[nodiscard]] core::Result<void> save(
      VoicebankProductionProject& project,
      const ProductionJournalEvent& event);
  [[nodiscard]] core::Result<VoicebankProductionProject> recover() const;
  [[nodiscard]] core::Result<AssetRecord> importRaw(
      VoicebankProductionProject& project,
      const std::filesystem::path& source,
      const RawTakeInput& take,
      const ProductionJournalEvent& event);
  [[nodiscard]] core::Result<StagedOperation> stageOperation(
      const AssetRecord& input,
      const OperationRequest& request,
      std::string stagingId) const;
  [[nodiscard]] core::Result<DerivedRevision> commitStaged(
      VoicebankProductionProject& project,
      const StagedOperation& staged,
      std::string revisionId,
      std::string operatorId,
      std::string performedAtUtc);
  [[nodiscard]] core::Result<void> recordMetadataRevision(
      VoicebankProductionProject& project,
      MetadataRevision revision,
      const ProductionJournalEvent& event);
  [[nodiscard]] std::vector<std::filesystem::path> inspectStaged(
      const VoicebankProductionProject& project) const;
  [[nodiscard]] core::Result<void> verify(
      const VoicebankProductionProject& project) const;
  [[nodiscard]] core::Result<ExportedU57Inputs> exportU57Inputs(
      VoicebankProductionProject& project,
      const std::filesystem::path& destination,
      const ProductionJournalEvent& event);
  [[nodiscard]] std::filesystem::path assetPath(
      const AssetRecord& asset) const;

private:
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
