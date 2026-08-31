#include "seam/voicebank_production/repository.hpp"

#include <algorithm>

namespace seam::voicebank_production {

core::Result<AssetRecord> ProductionProjectRepository::importRaw(
    VoicebankProductionProject& project, const std::filesystem::path& source,
    const RawTakeInput& take, const ProductionJournalEvent& event) {
  if (take.takeId.empty() || take.promptId.empty() || take.coverageKey.empty()) {
    return core::failure<AssetRecord>(core::ErrorCode::InvalidArgument,
                                      "Raw take identity is incomplete");
  }
  const auto duplicate = std::find_if(
      project.takes.begin(), project.takes.end(),
      [&take](const TakeRecord& value) { return value.takeId == take.takeId; });
  if (duplicate != project.takes.end()) {
    return core::failure<AssetRecord>(core::ErrorCode::Conflict,
                                      "Raw take identifier already exists");
  }
  auto assignment = std::find_if(
      project.unitAssignments.begin(), project.unitAssignments.end(),
      [&take](const UnitAssignment& value) {
        return value.coverageKey == take.coverageKey &&
               value.pitchLayer == take.pitchLayer;
      });
  if (assignment == project.unitAssignments.end()) {
    return core::failure<AssetRecord>(core::ErrorCode::InvalidArgument,
                                      "Raw take is not bound to required inventory");
  }
  auto superseded = project.takes.end();
  if (!take.supersedesTakeId.empty()) {
    superseded = std::find_if(
        project.takes.begin(), project.takes.end(),
        [&take](const TakeRecord& value) {
          return value.takeId == take.supersedesTakeId;
        });
    if (superseded == project.takes.end() ||
        superseded->coverageKey != take.coverageKey ||
        superseded->pitchLayer != take.pitchLayer ||
        superseded->promptId != take.promptId || event.action != "retake") {
      return core::failure<AssetRecord>(
          core::ErrorCode::InvalidArgument,
          "Retake must supersede a matching take with a retake journal event");
    }
  } else if (!assignment->takeId.empty()) {
    return core::failure<AssetRecord>(
        core::ErrorCode::Conflict,
        "Occupied inventory assignment requires an explicit retake chain");
  }
  if (take.review.has_value() &&
      (take.review->takeId != take.takeId || take.review->reviewId.empty() ||
       take.review->reviewerId.empty() || take.review->reviewedAtUtc.empty() ||
       (take.review->result != "PASS" && take.review->result != "REJECTED") ||
       std::any_of(project.reviews.begin(), project.reviews.end(),
                   [&take](const ReviewRecord& value) {
                     return value.reviewId == take.review->reviewId;
                   }))) {
    return core::failure<AssetRecord>(
        core::ErrorCode::InvalidArgument,
        "Raw take review is invalid or duplicated");
  }
  auto imported = assetStore_.importFile(source, AssetKind::Raw);
  if (!imported) return imported;
  const auto original = project;
  if (std::none_of(project.assets.begin(), project.assets.end(),
                   [&imported](const AssetRecord& value) {
                     return value.sha256 == imported.value().sha256;
                   })) {
    project.assets.push_back(imported.value());
  }
  if (superseded != project.takes.end()) superseded->state = UnitQueueState::Retake;
  project.takes.push_back(TakeRecord{
      .takeId = take.takeId,
      .promptId = take.promptId,
      .coverageKey = take.coverageKey,
      .pitchLayer = take.pitchLayer,
      .rawAssetSha256 = imported.value().sha256,
      .supersedesTakeId = take.supersedesTakeId,
      .state = take.initialState,
  });
  if (take.review.has_value()) {
    project.reviews.push_back(*take.review);
  }
  assignment = std::find_if(
      project.unitAssignments.begin(), project.unitAssignments.end(),
      [&take](const UnitAssignment& value) {
        return value.coverageKey == take.coverageKey &&
               value.pitchLayer == take.pitchLayer;
      });
  assignment->takeId = take.takeId;
  assignment->state = take.initialState;
  auto saved = save(project, event);
  if (!saved) {
    project = original;
    return core::Result<AssetRecord>{saved.error()};
  }
  return imported;
}

}
