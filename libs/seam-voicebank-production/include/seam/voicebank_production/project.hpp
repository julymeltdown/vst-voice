#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace seam::voicebank_production {

inline constexpr std::int64_t kProductionProjectSchemaVersion = 1;
inline constexpr const char* kProductionProjectFormat =
    "com.project-seam.voicebank-production";

enum class SourceStrategyKind { HumanRecording, ProceduralSynthesis, TtsDerived };
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
};

struct AssetRecord final {
  std::string sha256;
  std::string relativePath;
  std::uint64_t byteSize{0U};
  AssetKind kind{AssetKind::Raw};
};

struct DerivedRevision final {
  std::string revisionId;
  std::string inputSha256;
  std::string outputSha256;
  OperationKind operation{OperationKind::Downmix};
  std::string operationVersion{"seam-pcm-ops-1"};
  std::map<std::string, std::string, std::less<>> parameters;
  std::string operatorId;
  std::string performedAtUtc;
};

struct MetadataRevision final {
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
[[nodiscard]] std::string toString(SourceStrategyKind value);
[[nodiscard]] std::string toString(Feasibility value);
[[nodiscard]] std::string toString(AssetKind value);
[[nodiscard]] std::string toString(UnitQueueState value);
[[nodiscard]] std::string toString(OperationKind value);

}
