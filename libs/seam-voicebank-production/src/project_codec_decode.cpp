#include "project_codec_internal.hpp"

#include "seam/voicebank_production/project_codec.hpp"

#include <limits>

namespace seam::voicebank_production {
namespace {

template <typename T, typename Decoder>
core::Result<std::vector<T>> decodeArray(
    const formats::JsonValue& root, std::string_view key, Decoder decoder) {
  const auto* value = root.find(key);
  if (value == nullptr || !value->isArray()) {
    return core::failure<std::vector<T>>(
        core::ErrorCode::ParseError, std::string{key} + " must be an array");
  }
  std::vector<T> result;
  result.reserve(value->asArray().size());
  for (const auto& item : value->asArray()) {
    auto decoded = decoder(item);
    if (!decoded) return core::Result<std::vector<T>>{decoded.error()};
    result.push_back(std::move(decoded).value());
  }
  return result;
}

core::Result<OperatorRecord> decodeOperator(const formats::JsonValue& value) {
  OperatorRecord result;
  if (!value.isObject() ||
      !codec_internal::readString(value, "operatorId", result.operatorId) ||
      !codec_internal::readString(value, "role", result.role)) {
    return core::failure<OperatorRecord>(
        core::ErrorCode::ParseError, "Operator record is invalid");
  }
  return result;
}

core::Result<ReviewRecord> decodeReview(const formats::JsonValue& value) {
  ReviewRecord result;
  if (!value.isObject() ||
      !codec_internal::readString(value, "reviewId", result.reviewId) ||
      !codec_internal::readString(value, "takeId", result.takeId) ||
      !codec_internal::readString(value, "reviewerId", result.reviewerId) ||
      !codec_internal::readString(value, "result", result.result) ||
      !codec_internal::readString(value, "reviewedAtUtc", result.reviewedAtUtc)) {
    return core::failure<ReviewRecord>(
        core::ErrorCode::ParseError, "Review record is invalid");
  }
  return result;
}

}

core::Result<VoicebankProductionProject> decodeProductionProject(
    std::string_view json) {
  auto parsed = formats::parseJson(
      json, formats::JsonParseLimits{
                .maximumInputBytes = 64U * 1024U * 1024U,
                .maximumDepth = 24U,
                .maximumNodes = 1'000'000U,
                .maximumStringBytes = 8U * 1024U * 1024U,
                .maximumCollectionEntries = 500'000U,
            });
  if (!parsed) return core::Result<VoicebankProductionProject>{parsed.error()};
  if (!parsed.value().isObject()) {
    return core::failure<VoicebankProductionProject>(
        core::ErrorCode::ParseError, "Production project root must be an object");
  }
  VoicebankProductionProject project;
  std::string format;
  std::int64_t generation = 0;
  if (!codec_internal::readString(parsed.value(), "format", format) ||
      !codec_internal::readInteger(parsed.value(), "schemaVersion", project.schemaVersion) ||
      !codec_internal::readString(parsed.value(), "projectId", project.projectId) ||
      !codec_internal::readString(parsed.value(), "inventoryId", project.inventoryId) ||
      !codec_internal::readString(parsed.value(), "inventorySha256", project.inventorySha256) ||
      !codec_internal::readString(parsed.value(), "selectedSourceStrategyId", project.selectedSourceStrategyId) ||
      !codec_internal::readString(parsed.value(), "licenseLocator", project.licenseLocator) ||
      !codec_internal::readString(parsed.value(), "licenseSha256", project.licenseSha256) ||
      !codec_internal::readString(parsed.value(), "immutableAssetRoot", project.immutableAssetRoot) ||
      !codec_internal::readInteger(parsed.value(), "lastDurableGeneration", generation) ||
      generation < 0 || format != kProductionProjectFormat) {
    return core::failure<VoicebankProductionProject>(
        core::ErrorCode::ParseError, "Production project header is invalid");
  }
  auto strategies = decodeArray<SourceStrategyAssessment>(
      parsed.value(), "sourceStrategies", codec_internal::decodeStrategy);
  auto assets = decodeArray<AssetRecord>(
      parsed.value(), "assets", codec_internal::decodeAsset);
  auto takes = decodeArray<TakeRecord>(
      parsed.value(), "takes", codec_internal::decodeTake);
  auto revisions = decodeArray<DerivedRevision>(
      parsed.value(), "derivedRevisions", codec_internal::decodeRevision);
  auto metadataRevisions = decodeArray<MetadataRevision>(
      parsed.value(), "metadataRevisions", codec_internal::decodeMetadataRevision);
  auto assignments = decodeArray<UnitAssignment>(
      parsed.value(), "unitAssignments", codec_internal::decodeAssignment);
  auto operators = decodeArray<OperatorRecord>(
      parsed.value(), "operators", decodeOperator);
  auto reviews = decodeArray<ReviewRecord>(
      parsed.value(), "reviews", decodeReview);
  if (!strategies) return core::Result<VoicebankProductionProject>{strategies.error()};
  if (!assets) return core::Result<VoicebankProductionProject>{assets.error()};
  if (!takes) return core::Result<VoicebankProductionProject>{takes.error()};
  if (!revisions) return core::Result<VoicebankProductionProject>{revisions.error()};
  if (!metadataRevisions) return core::Result<VoicebankProductionProject>{metadataRevisions.error()};
  if (!assignments) return core::Result<VoicebankProductionProject>{assignments.error()};
  if (!operators) return core::Result<VoicebankProductionProject>{operators.error()};
  if (!reviews) return core::Result<VoicebankProductionProject>{reviews.error()};
  project.sourceStrategies = std::move(strategies).value();
  project.assets = std::move(assets).value();
  project.takes = std::move(takes).value();
  project.derivedRevisions = std::move(revisions).value();
  project.metadataRevisions = std::move(metadataRevisions).value();
  project.unitAssignments = std::move(assignments).value();
  project.operators = std::move(operators).value();
  project.reviews = std::move(reviews).value();
  project.lastDurableGeneration = static_cast<std::uint64_t>(generation);
  auto valid = validateProductionProject(project);
  if (!valid) return core::Result<VoicebankProductionProject>{valid.error()};
  return project;
}

}
