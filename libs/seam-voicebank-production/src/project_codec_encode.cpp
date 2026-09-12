#include "seam/voicebank_production/project_codec.hpp"

#include "seam/formats/json_value.hpp"

namespace seam::voicebank_production {
namespace {

using Array = formats::JsonValue::Array;
using Object = formats::JsonValue::Object;

formats::JsonValue encodeStrategy(const SourceStrategyAssessment& value) {
  return Object{
      {"id", value.id},
      {"kind", toString(value.kind)},
      {"rights", toString(value.rights)},
      {"coverage", toString(value.coverage)},
      {"listening", toString(value.listening)},
      {"permissions", Object{
          {"sourceUse", value.permissions.sourceUse},
          {"transformation", value.permissions.transformation},
          {"singingBankRedistribution", value.permissions.singingBankRedistribution},
          {"commercialRenders", value.permissions.commercialRenders},
      }},
      {"licenseLocator", value.licenseLocator},
      {"licenseSha256", value.licenseSha256},
      {"evidenceState", value.evidenceState},
  };
}

formats::JsonValue encodeAsset(const AssetRecord& value) {
  return Object{
      {"sha256", value.sha256},
      {"relativePath", value.relativePath},
      {"byteSize", static_cast<std::int64_t>(value.byteSize)},
      {"kind", toString(value.kind)},
  };
}

formats::JsonValue encodeRevision(const DerivedRevision& value) {
  Object parameters;
  for (const auto& [key, item] : value.parameters) parameters.emplace(key, item);
  return Object{
      {"revisionId", value.revisionId},
      {"inputSha256", value.inputSha256},
      {"outputSha256", value.outputSha256},
      {"operation", toString(value.operation)},
      {"operationVersion", value.operationVersion},
      {"parameters", std::move(parameters)},
      {"operatorId", value.operatorId},
      {"performedAtUtc", value.performedAtUtc},
  };
}

formats::JsonValue encodeMetadataRevision(const MetadataRevision& value) {
  Object values;
  for (const auto& [key, item] : value.values) values.emplace(key, item);
  return Object{
      {"revisionId", value.revisionId},
      {"takeId", value.takeId},
      {"rawAssetSha256", value.rawAssetSha256},
      {"kind", value.kind},
      {"values", std::move(values)},
      {"operatorId", value.operatorId},
      {"performedAtUtc", value.performedAtUtc},
  };
}

formats::JsonValue encodeTake(const TakeRecord& value, std::int64_t schema) {
  Array revisions;
  for (const auto& id : value.derivedRevisionIds) revisions.emplace_back(id);
  Object object{
      {"takeId", value.takeId},
      {"promptId", value.promptId},
      {"coverageKey", value.coverageKey},
      {"pitchLayer", static_cast<std::int64_t>(value.pitchLayer)},
      {"rawAssetSha256", value.rawAssetSha256},
      {"derivedRevisionIds", std::move(revisions)},
      {"supersedesTakeId", value.supersedesTakeId},
      {"state", toString(value.state)},
  };
  if (schema >= 2) object.emplace("sourceBindingId", value.sourceBindingId);
  if (schema >= kProductionStyleSchemaVersion) object.emplace("style", value.style);
  return object;
}

formats::JsonValue encodeSourceBinding(const TakeSourceBinding& value) {
  return Object{{"id", value.id}, {"takeId", value.takeId}, {"rawAssetSha256", value.rawAssetSha256},
      {"strategy", encodeStrategy(value.strategy)}, {"importerId", value.importerId},
      {"importedAtUtc", value.importedAtUtc}, {"licenseSnapshotPath", value.licenseSnapshotPath}};
}

formats::JsonValue encodeAssignment(const UnitAssignment& value, std::int64_t schema) {
  Object object{
      {"coverageKey", value.coverageKey},
      {"pitchLayer", static_cast<std::int64_t>(value.pitchLayer)},
      {"promptId", value.promptId},
      {"plannedTakeId", value.plannedTakeId},
      {"takeId", value.takeId},
      {"state", toString(value.state)},
      {"markerReviewed", value.markerReviewed},
      {"pitchReviewed", value.pitchReviewed},
  };
  if (schema >= kProductionStyleSchemaVersion) object.emplace("style", value.style);
  return object;
}

template <typename Value, typename Encoder>
Array encodeArray(const std::vector<Value>& values, Encoder encoder) {
  Array result;
  result.reserve(values.size());
  for (const auto& value : values) result.push_back(encoder(value));
  return result;
}

}

std::string encodeProductionProject(
    const VoicebankProductionProject& project) {
  auto operators = encodeArray(project.operators, [](const OperatorRecord& value) {
    return formats::JsonValue{Object{{"operatorId", value.operatorId}, {"role", value.role}}};
  });
  auto reviews = encodeArray(project.reviews, [](const ReviewRecord& value) {
    return formats::JsonValue{Object{
        {"reviewId", value.reviewId}, {"takeId", value.takeId},
        {"reviewerId", value.reviewerId}, {"result", value.result},
        {"reviewedAtUtc", value.reviewedAtUtc},
    }};
  });
  Object object{
      {"format", kProductionProjectFormat},
      {"schemaVersion", project.schemaVersion},
      {"projectId", project.projectId},
      {"inventoryId", project.inventoryId},
      {"inventorySha256", project.inventorySha256},
      {"selectedSourceStrategyId", project.selectedSourceStrategyId},
      {"licenseLocator", project.licenseLocator},
      {"licenseSha256", project.licenseSha256},
      {"immutableAssetRoot", project.immutableAssetRoot},
      {"sourceStrategies", encodeArray(project.sourceStrategies, encodeStrategy)},
      {"assets", encodeArray(project.assets, encodeAsset)},
      {"takes", encodeArray(project.takes, [&](const auto& value) { return encodeTake(value, project.schemaVersion); })},
      {"derivedRevisions", encodeArray(project.derivedRevisions, encodeRevision)},
      {"metadataRevisions", encodeArray(project.metadataRevisions, encodeMetadataRevision)},
      {"unitAssignments", encodeArray(project.unitAssignments, [&](const auto& value) { return encodeAssignment(value, project.schemaVersion); })},
      {"operators", std::move(operators)},
      {"reviews", std::move(reviews)},
      {"lastDurableGeneration", static_cast<std::int64_t>(project.lastDurableGeneration)},
  };
  if (project.schemaVersion >= kProductionStyleSchemaVersion) object.emplace("language", project.language);
  if (project.schemaVersion >= 2) {
    object.emplace("lifecycle", toString(project.lifecycle));
    object.emplace("sourceBindings", encodeArray(project.sourceBindings, encodeSourceBinding));
  }
  if (project.schemaVersion >= kProductionAssessmentSchemaVersion) {
    object.emplace("sourceQualityAssessments",encodeArray(project.sourceQualityAssessments,[](const SourceQualityAssessment& value) {
      return formats::JsonValue{Object{{"id",value.id},{"strategyId",value.strategyId},{"policySha256",value.policySha256},
          {"materialSha256",value.materialSha256},{"evidenceSha256",value.evidenceSha256},{"reviewerId",value.reviewerId},
          {"reviewedAtUtc",value.reviewedAtUtc},{"coverage",toString(value.coverage)},{"listening",toString(value.listening)}}};
    }));
  }
  return formats::stringifyJson(formats::JsonValue{std::move(object)}, true) + "\n";
}

}
