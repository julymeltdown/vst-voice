#include "seam/native_ui/voicebank_studio.hpp"

#include "voicebank_studio_production_support.hpp"

#include "seam/core/sha256.hpp"

#include <algorithm>
#include <utility>

namespace seam::native_ui {

core::Result<voicebank_production::DerivedRevision>
VoicebankStudioController::applySelectedProductionOperation(
    const voicebank_production::OperationRequest& request,
    std::string occurredAtUtc) {
  if (!productionRepository_ || !productionProject_) {
    return core::failure<voicebank_production::DerivedRevision>(
        core::ErrorCode::InvalidState,
        "Voicebank Studio has no production project");
  }
  const auto* assignment = selectedProductionAssignment();
  if (assignment == nullptr || assignment->takeId.empty()) {
    return core::failure<voicebank_production::DerivedRevision>(
        core::ErrorCode::InvalidState,
        "Selected production unit has no imported take");
  }
  const auto take = std::find_if(
      productionProject_->takes.begin(), productionProject_->takes.end(),
      [assignment](const voicebank_production::TakeRecord& value) {
        return value.takeId == assignment->takeId;
      });
  if (take == productionProject_->takes.end()) {
    return core::failure<voicebank_production::DerivedRevision>(
        core::ErrorCode::InvariantViolation,
        "Selected production take is unavailable");
  }
  auto inputSha256 = take->rawAssetSha256;
  if (!take->derivedRevisionIds.empty()) {
    const auto latestId = take->derivedRevisionIds.back();
    const auto latest = std::find_if(
        productionProject_->derivedRevisions.begin(),
        productionProject_->derivedRevisions.end(),
        [&latestId](const voicebank_production::DerivedRevision& value) {
          return value.revisionId == latestId;
        });
    if (latest == productionProject_->derivedRevisions.end()) {
      return core::failure<voicebank_production::DerivedRevision>(
          core::ErrorCode::InvariantViolation,
          "Latest production revision is unavailable");
    }
    inputSha256 = latest->outputSha256;
  }
  const auto asset = std::find_if(
      productionProject_->assets.begin(), productionProject_->assets.end(),
      [&inputSha256](const voicebank_production::AssetRecord& value) {
        return value.sha256 == inputSha256;
      });
  if (asset == productionProject_->assets.end()) {
    return core::failure<voicebank_production::DerivedRevision>(
        core::ErrorCode::NotFound,
        "Production operation input asset is unavailable");
  }
  if (occurredAtUtc.empty()) {
    occurredAtUtc = voicebank_studio_internal::currentUtcTimestamp();
  }
  auto identity = take->takeId + inputSha256 +
                  voicebank_production::toString(request.kind) + occurredAtUtc;
  for (const auto& [key, value] :
       voicebank_production::operationParameters(request)) {
    identity += key + "=" + value + ";";
  }
  const auto stagingId = "operation-" +
                         core::sha256Hex(
                             identity + std::to_string(
                                 productionProject_->lastDurableGeneration + 1U))
                             .substr(0U, 24U);
  auto staged = productionRepository_->stageOperation(*asset, request, stagingId);
  if (!staged) {
    return core::Result<voicebank_production::DerivedRevision>{staged.error()};
  }
  const auto revisionId = "revision-" + core::sha256Hex(
      identity + staged.value().outputSha256 + std::to_string(
          productionProject_->lastDurableGeneration + 1U)).substr(0U, 24U);
  auto committed = productionRepository_->commitStaged(
      *productionProject_, staged.value(), revisionId,
      productionOperatorId_, occurredAtUtc);
  stagedRecoveryCandidateCount_ =
      productionRepository_->inspectStaged(*productionProject_).size();
  if (committed) status_ = "AUDIO OPERATION SAVED";
  return committed;
}

core::Result<void> VoicebankStudioController::persistProductionMetadata() {
  if (!productionRepository_ || !productionProject_) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Voicebank Studio has no production project");
  }
  const auto* unit = selectedUnit();
  if (unit == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "No unit is selected for metadata revision");
  }
  const auto coverage = voicebank_studio_internal::coverageKey(*unit);
  const auto assignment = std::find_if(
      productionProject_->unitAssignments.begin(),
      productionProject_->unitAssignments.end(),
      [&coverage, unit](const voicebank_production::UnitAssignment& value) {
        return value.coverageKey == coverage && value.pitchLayer == unit->rootMidi;
      });
  if (assignment == productionProject_->unitAssignments.end() ||
      assignment->takeId.empty()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Selected unit has no imported production take");
  }
  const auto take = std::find_if(
      productionProject_->takes.begin(), productionProject_->takes.end(),
      [&assignment](const voicebank_production::TakeRecord& value) {
        return value.takeId == assignment->takeId;
      });
  if (take == productionProject_->takes.end()) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Production assignment references an unavailable take");
  }
  auto values = voicebank_studio_internal::metadataValues(*unit);
  std::string identity = take->takeId + take->rawAssetSha256;
  for (const auto& [key, value] : values) identity += key + "=" + value + ";";
  identity += std::to_string(productionProject_->lastDurableGeneration + 1U);
  const auto revisionId = "metadata-" +
                          core::sha256Hex(identity).substr(0U, 24U);
  const auto occurredAt = voicebank_studio_internal::currentUtcTimestamp();
  return productionRepository_->recordMetadataRevision(
      *productionProject_,
      {.revisionId = revisionId,
       .takeId = take->takeId,
       .rawAssetSha256 = take->rawAssetSha256,
       .kind = "MARKERS_AND_PITCH",
       .values = std::move(values),
       .operatorId = productionOperatorId_,
       .performedAtUtc = occurredAt},
      {.action = "marker", .subjectId = revisionId,
       .operatorId = productionOperatorId_, .occurredAtUtc = occurredAt});
}

}
