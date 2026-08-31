#include "project_codec_internal.hpp"

#include <limits>
#include <optional>

namespace seam::voicebank_production::codec_internal {
namespace {

template <typename T>
core::Result<T> parseFailure(std::string message) {
  return core::failure<T>(core::ErrorCode::ParseError, std::move(message));
}

std::optional<SourceStrategyKind> strategyKind(std::string_view value) {
  if (value == "HUMAN_RECORDING") return SourceStrategyKind::HumanRecording;
  if (value == "PROCEDURAL_SYNTHESIS") return SourceStrategyKind::ProceduralSynthesis;
  if (value == "TTS_DERIVED") return SourceStrategyKind::TtsDerived;
  return std::nullopt;
}

std::optional<Feasibility> feasibility(std::string_view value) {
  if (value == "PASS") return Feasibility::Pass;
  if (value == "BLOCKED") return Feasibility::Blocked;
  if (value == "NOT_ASSESSED") return Feasibility::NotAssessed;
  return std::nullopt;
}

std::optional<AssetKind> assetKind(std::string_view value) {
  if (value == "RAW") return AssetKind::Raw;
  if (value == "DERIVED") return AssetKind::Derived;
  return std::nullopt;
}

std::optional<UnitQueueState> queueState(std::string_view value) {
  if (value == "MISSING") return UnitQueueState::Missing;
  if (value == "REJECTED") return UnitQueueState::Rejected;
  if (value == "RETAKE") return UnitQueueState::Retake;
  if (value == "MARKER_REVIEW") return UnitQueueState::MarkerReview;
  if (value == "PITCH_REVIEW") return UnitQueueState::PitchReview;
  if (value == "APPROVED") return UnitQueueState::Approved;
  return std::nullopt;
}

std::optional<OperationKind> operationKind(std::string_view value) {
  if (value == "CHANNEL_SELECT") return OperationKind::ChannelSelect;
  if (value == "DOWNMIX") return OperationKind::Downmix;
  if (value == "RESAMPLE") return OperationKind::Resample;
  if (value == "REMOVE_DC") return OperationKind::RemoveDc;
  if (value == "NORMALIZE_GAIN") return OperationKind::NormalizeGain;
  if (value == "TRIM") return OperationKind::Trim;
  if (value == "SEGMENT") return OperationKind::Segment;
  return std::nullopt;
}

}

bool readString(const formats::JsonValue& object, std::string_view key,
                std::string& output) {
  const auto* value = object.find(key);
  if (value == nullptr || !value->isString()) return false;
  output = value->asString();
  return true;
}

bool readInteger(const formats::JsonValue& object, std::string_view key,
                 std::int64_t& output) {
  const auto* value = object.find(key);
  if (value == nullptr || !value->isInteger()) return false;
  output = value->asInt64();
  return true;
}

bool readBool(const formats::JsonValue& object, std::string_view key,
              bool& output) {
  const auto* value = object.find(key);
  if (value == nullptr || !value->isBool()) return false;
  output = value->asBool();
  return true;
}

core::Result<SourceStrategyAssessment> decodeStrategy(
    const formats::JsonValue& value) {
  if (!value.isObject()) return parseFailure<SourceStrategyAssessment>("Source strategy must be an object");
  SourceStrategyAssessment result;
  std::string kind;
  std::string rights;
  std::string coverage;
  std::string listening;
  const auto* permissions = value.find("permissions");
  if (!readString(value, "id", result.id) || !readString(value, "kind", kind) ||
      !readString(value, "rights", rights) || !readString(value, "coverage", coverage) ||
      !readString(value, "listening", listening) || permissions == nullptr ||
      !permissions->isObject() || !readString(value, "licenseLocator", result.licenseLocator) ||
      !readString(value, "licenseSha256", result.licenseSha256) ||
      !readString(value, "evidenceState", result.evidenceState) ||
      !readBool(*permissions, "sourceUse", result.permissions.sourceUse) ||
      !readBool(*permissions, "transformation", result.permissions.transformation) ||
      !readBool(*permissions, "singingBankRedistribution", result.permissions.singingBankRedistribution) ||
      !readBool(*permissions, "commercialRenders", result.permissions.commercialRenders)) {
    return parseFailure<SourceStrategyAssessment>("Source strategy fields are invalid");
  }
  const auto parsedKind = strategyKind(kind);
  const auto parsedRights = feasibility(rights);
  const auto parsedCoverage = feasibility(coverage);
  const auto parsedListening = feasibility(listening);
  if (!parsedKind || !parsedRights || !parsedCoverage || !parsedListening) {
    return parseFailure<SourceStrategyAssessment>("Source strategy enum is invalid");
  }
  result.kind = *parsedKind;
  result.rights = *parsedRights;
  result.coverage = *parsedCoverage;
  result.listening = *parsedListening;
  return result;
}

core::Result<AssetRecord> decodeAsset(const formats::JsonValue& value) {
  if (!value.isObject()) return parseFailure<AssetRecord>("Asset must be an object");
  AssetRecord result;
  std::string kind;
  std::int64_t size = 0;
  if (!readString(value, "sha256", result.sha256) ||
      !readString(value, "relativePath", result.relativePath) ||
      !readInteger(value, "byteSize", size) || size < 0 ||
      !readString(value, "kind", kind)) {
    return parseFailure<AssetRecord>("Asset fields are invalid");
  }
  const auto parsedKind = assetKind(kind);
  if (!parsedKind) return parseFailure<AssetRecord>("Asset kind is invalid");
  result.byteSize = static_cast<std::uint64_t>(size);
  result.kind = *parsedKind;
  return result;
}

core::Result<DerivedRevision> decodeRevision(const formats::JsonValue& value) {
  if (!value.isObject()) return parseFailure<DerivedRevision>("Derived revision must be an object");
  DerivedRevision result;
  std::string operation;
  const auto* parameters = value.find("parameters");
  if (!readString(value, "revisionId", result.revisionId) ||
      !readString(value, "inputSha256", result.inputSha256) ||
      !readString(value, "outputSha256", result.outputSha256) ||
      !readString(value, "operation", operation) ||
      !readString(value, "operationVersion", result.operationVersion) ||
      !readString(value, "operatorId", result.operatorId) ||
      !readString(value, "performedAtUtc", result.performedAtUtc) ||
      parameters == nullptr || !parameters->isObject()) {
    return parseFailure<DerivedRevision>("Derived revision fields are invalid");
  }
  const auto parsedOperation = operationKind(operation);
  if (!parsedOperation) return parseFailure<DerivedRevision>("Derived operation is invalid");
  for (const auto& [key, item] : parameters->asObject()) {
    if (!item.isString()) return parseFailure<DerivedRevision>("Derived parameter must be a string");
    result.parameters.emplace(key, item.asString());
  }
  result.operation = *parsedOperation;
  return result;
}

core::Result<MetadataRevision> decodeMetadataRevision(
    const formats::JsonValue& value) {
  if (!value.isObject()) {
    return parseFailure<MetadataRevision>("Metadata revision must be an object");
  }
  MetadataRevision result;
  const auto* values = value.find("values");
  if (!readString(value, "revisionId", result.revisionId) ||
      !readString(value, "takeId", result.takeId) ||
      !readString(value, "rawAssetSha256", result.rawAssetSha256) ||
      !readString(value, "kind", result.kind) ||
      !readString(value, "operatorId", result.operatorId) ||
      !readString(value, "performedAtUtc", result.performedAtUtc) ||
      values == nullptr || !values->isObject()) {
    return parseFailure<MetadataRevision>("Metadata revision fields are invalid");
  }
  for (const auto& [key, item] : values->asObject()) {
    if (!item.isString()) {
      return parseFailure<MetadataRevision>("Metadata revision value must be a string");
    }
    result.values.emplace(key, item.asString());
  }
  return result;
}

core::Result<TakeRecord> decodeTake(const formats::JsonValue& value) {
  if (!value.isObject()) return parseFailure<TakeRecord>("Take must be an object");
  TakeRecord result;
  std::int64_t pitch = 0;
  std::string state;
  const auto* revisions = value.find("derivedRevisionIds");
  if (!readString(value, "takeId", result.takeId) || !readString(value, "promptId", result.promptId) ||
      !readString(value, "coverageKey", result.coverageKey) || !readInteger(value, "pitchLayer", pitch) ||
      pitch < std::numeric_limits<std::int32_t>::min() || pitch > std::numeric_limits<std::int32_t>::max() ||
      !readString(value, "rawAssetSha256", result.rawAssetSha256) ||
      !readString(value, "supersedesTakeId", result.supersedesTakeId) || !readString(value, "state", state) ||
      revisions == nullptr || !revisions->isArray()) {
    return parseFailure<TakeRecord>("Take fields are invalid");
  }
  const auto parsedState = queueState(state);
  if (!parsedState) return parseFailure<TakeRecord>("Take state is invalid");
  for (const auto& id : revisions->asArray()) {
    if (!id.isString()) return parseFailure<TakeRecord>("Take revision id must be a string");
    result.derivedRevisionIds.push_back(id.asString());
  }
  result.pitchLayer = static_cast<std::int32_t>(pitch);
  result.state = *parsedState;
  return result;
}

core::Result<UnitAssignment> decodeAssignment(const formats::JsonValue& value) {
  if (!value.isObject()) return parseFailure<UnitAssignment>("Unit assignment must be an object");
  UnitAssignment result;
  std::int64_t pitch = 0;
  std::string state;
  if (!readString(value, "coverageKey", result.coverageKey) || !readInteger(value, "pitchLayer", pitch) ||
      pitch < std::numeric_limits<std::int32_t>::min() || pitch > std::numeric_limits<std::int32_t>::max() ||
      !readString(value, "promptId", result.promptId) ||
      !readString(value, "plannedTakeId", result.plannedTakeId) ||
      !readString(value, "takeId", result.takeId) || !readString(value, "state", state) ||
      !readBool(value, "markerReviewed", result.markerReviewed) ||
      !readBool(value, "pitchReviewed", result.pitchReviewed)) {
    return parseFailure<UnitAssignment>("Unit assignment fields are invalid");
  }
  const auto parsedState = queueState(state);
  if (!parsedState) return parseFailure<UnitAssignment>("Unit assignment state is invalid");
  result.pitchLayer = static_cast<std::int32_t>(pitch);
  result.state = *parsedState;
  return result;
}

}
