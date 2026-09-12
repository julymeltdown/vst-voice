#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace seam::voicebank_production {

inline constexpr std::int64_t kProductionProjectSchemaVersion = 2;
inline constexpr std::int64_t kProductionAssessmentSchemaVersion = 3;
inline constexpr const char* kProductionProjectFormat =
    "com.project-seam.voicebank-production";

enum class SourceStrategyKind { HumanRecording, ProceduralSynthesis, TtsDerived };
enum class ProductionLifecycle { LegacyUnclassified, Draft, Experimental, Qualified };
enum class Feasibility { Pass, Blocked, NotAssessed };
enum class AssetKind { Raw, Derived };
enum class UnitQueueState {
  Missing,
  Rejected,
  Retake,
  MarkerReview,
  PitchReview,
  Approved,
};
enum class OperationKind {
  ChannelSelect,
  Downmix,
  Resample,
  RemoveDc,
  NormalizeGain,
  Trim,
  Segment,
};

struct RightsPermissions final {
  bool sourceUse{false};
  bool transformation{false};
  bool singingBankRedistribution{false};
  bool commercialRenders{false};
  friend bool operator==(const RightsPermissions&, const RightsPermissions&) = default;
};

struct SourceStrategyAssessment final {
  std::string id;
  SourceStrategyKind kind{SourceStrategyKind::HumanRecording};
  Feasibility rights{Feasibility::NotAssessed};
  Feasibility coverage{Feasibility::NotAssessed};
  Feasibility listening{Feasibility::NotAssessed};
  RightsPermissions permissions;
  std::string licenseLocator;
  std::string licenseSha256;
  std::string evidenceState;
  friend bool operator==(const SourceStrategyAssessment&, const SourceStrategyAssessment&) = default;
};

// One immutable ingress binding per take, independent of shared audio blobs.
// The original locator remains attribution; a hash-addressed evidence copy
// makes the captured policy inspectable after the selected strategy changes.
struct TakeSourceBinding final {
  std::string id;
  std::string takeId;
  std::string rawAssetSha256;
  SourceStrategyAssessment strategy;
  std::string importerId;
  std::string importedAtUtc;
  std::string licenseSnapshotPath;
  friend bool operator==(const TakeSourceBinding&, const TakeSourceBinding&) = default;
};

// Append-only reviewer decisions. They never change captured ingress rights.
struct SourceQualityAssessment final {
  std::string id, strategyId, policySha256, materialSha256;
  std::string evidenceSha256, reviewerId, reviewedAtUtc;
  Feasibility coverage{Feasibility::NotAssessed};
  Feasibility listening{Feasibility::NotAssessed};
  friend bool operator==(const SourceQualityAssessment&, const SourceQualityAssessment&) = default;
};

struct AssetRecord {
  std::string sha256;
  std::string relativePath;
  std::uint64_t byteSize{0U};
  AssetKind kind{AssetKind::Raw};
};

struct DerivedRevision {
  std::string revisionId;
  std::string inputSha256;
  std::string outputSha256;
  OperationKind operation{OperationKind::Downmix};
  std::string operationVersion{"seam-pcm-ops-1"};
  std::map<std::string, std::string, std::less<>> parameters;
  std::string operatorId;
  std::string performedAtUtc;
};

struct MetadataRevision {
  std::string revisionId;
  std::string takeId;
  std::string rawAssetSha256;
  std::string kind;
  std::map<std::string, std::string, std::less<>> values;
  std::string operatorId;
  std::string performedAtUtc;
};

struct TakeRecord final {
  std::string takeId;
  std::string promptId;
  std::string coverageKey;
  std::int32_t pitchLayer{0};
  std::string rawAssetSha256;
  std::vector<std::string> derivedRevisionIds;
  std::string supersedesTakeId;
  UnitQueueState state{UnitQueueState::MarkerReview};
  // Empty means unknown legacy/unattributed origin, never implicit ownership
  // by the project's currently selected source strategy.
  std::string sourceBindingId;
};

struct UnitAssignment final {
  std::string coverageKey;
  std::int32_t pitchLayer{0};
  std::string promptId;
  std::string plannedTakeId;
  std::string takeId;
  UnitQueueState state{UnitQueueState::Missing};
  bool markerReviewed{false};
  bool pitchReviewed{false};
};

struct OperatorRecord final {
  std::string operatorId;
  std::string role;
};

struct ReviewRecord final {
  std::string reviewId;
  std::string takeId;
  std::string reviewerId;
  std::string result;
  std::string reviewedAtUtc;
};

struct ProductionJournalEvent final {
  std::string action;
  std::string subjectId;
  std::string operatorId;
  std::string occurredAtUtc;
};

struct VoicebankProductionProject final {
  std::int64_t schemaVersion{kProductionProjectSchemaVersion};
  std::string projectId;
  std::string inventoryId;
  std::string inventorySha256;
  std::string selectedSourceStrategyId;
  std::string licenseLocator;
  std::string licenseSha256;
  std::string immutableAssetRoot{"assets"};
  std::vector<SourceStrategyAssessment> sourceStrategies;
  std::vector<AssetRecord> assets;
  std::vector<TakeRecord> takes;
  std::vector<DerivedRevision> derivedRevisions;
  std::vector<MetadataRevision> metadataRevisions;
  std::vector<UnitAssignment> unitAssignments;
  std::vector<OperatorRecord> operators;
  std::vector<ReviewRecord> reviews;
  std::uint64_t lastDurableGeneration{0U};
  ProductionLifecycle lifecycle{ProductionLifecycle::Draft};
  std::vector<TakeSourceBinding> sourceBindings;
  std::vector<SourceQualityAssessment> sourceQualityAssessments;
};

struct ProductionQueueSummary final {
  std::size_t missing{0U};
  std::size_t rejected{0U};
  std::size_t retake{0U};
  std::size_t markerReview{0U};
  std::size_t pitchReview{0U};
  std::size_t approved{0U};
};

[[nodiscard]] ProductionQueueSummary summarizeQueues(
    const VoicebankProductionProject& project) noexcept;
[[nodiscard]] bool isProductionUtcTimestamp(std::string_view value) noexcept;
[[nodiscard]] bool isProductionJournalAction(std::string_view value) noexcept;
[[nodiscard]] bool selectedStrategyReady(
    const VoicebankProductionProject& project) noexcept;
// Execution admission never requires coverage or listening PASS. The caller
// must separately verify the retained evidence bytes through the repository.
[[nodiscard]] core::Result<void> requireSelectedSourceExecution(const VoicebankProductionProject& project);
[[nodiscard]] core::Result<void> requireTakeSourceExecution(const VoicebankProductionProject& project, std::string_view takeId);
[[nodiscard]] core::Result<void> requireTakeSourceQualification(const VoicebankProductionProject& project, std::string_view takeId);
void invalidateProductionQualification(VoicebankProductionProject& project) noexcept;
[[nodiscard]] std::string toString(ProductionLifecycle value);
[[nodiscard]] std::string toString(SourceStrategyKind value);
[[nodiscard]] std::string toString(Feasibility value);
[[nodiscard]] std::string toString(AssetKind value);
[[nodiscard]] std::string toString(UnitQueueState value);
[[nodiscard]] std::string toString(OperationKind value);

}
