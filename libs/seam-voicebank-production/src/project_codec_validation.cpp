#include "seam/voicebank_production/project_codec.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace seam::voicebank_production {
namespace {

bool isDigest(const std::string& value) {
  return value.size() == 64U && std::all_of(
      value.begin(), value.end(), [](unsigned char item) {
        return std::isxdigit(item) != 0;
      });
}

template <typename Values, typename Identifier>
bool uniqueIds(const Values& values, Identifier identifier) {
  std::set<std::string, std::less<>> ids;
  for (const auto& value : values) {
    const auto& id = identifier(value);
    if (id.empty() || !ids.insert(id).second) return false;
  }
  return true;
}

core::Result<void> invalid(std::string message) {
  return core::failure(core::ErrorCode::InvariantViolation,
                       std::move(message));
}

}

core::Result<void> validateProductionProject(
    const VoicebankProductionProject& project) {
  if (project.schemaVersion != kProductionProjectSchemaVersion) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Production project schema is unsupported");
  }
  if (project.projectId.empty() || project.inventoryId.empty() ||
      project.selectedSourceStrategyId.empty() ||
      project.immutableAssetRoot.empty()) {
    return invalid("Production project identity is incomplete");
  }
  if (!isDigest(project.inventorySha256) || !isDigest(project.licenseSha256)) {
    return invalid("Production project hashes are invalid");
  }
  const std::filesystem::path assetRoot{project.immutableAssetRoot};
  if (assetRoot.is_absolute() || assetRoot.has_parent_path() ||
      project.immutableAssetRoot == "." || project.immutableAssetRoot == "..") {
    return invalid("Immutable asset root must be a single relative directory");
  }
  if (!uniqueIds(project.sourceStrategies,
                 [](const auto& value) -> const auto& { return value.id; }) ||
      !uniqueIds(project.takes,
                 [](const auto& value) -> const auto& { return value.takeId; }) ||
      !uniqueIds(project.derivedRevisions,
                 [](const auto& value) -> const auto& { return value.revisionId; }) ||
      !uniqueIds(project.metadataRevisions,
                 [](const auto& value) -> const auto& { return value.revisionId; }) ||
      !uniqueIds(project.reviews,
                 [](const auto& value) -> const auto& { return value.reviewId; }) ||
      !uniqueIds(project.operators,
                 [](const auto& value) -> const auto& { return value.operatorId; })) {
    return invalid("Production project contains missing or duplicate identifiers");
  }
  if (!selectedStrategyReady(project)) {
    return invalid("Selected source strategy is not rights, coverage, and listening feasible");
  }
  const auto selectedStrategy = std::find_if(
      project.sourceStrategies.begin(), project.sourceStrategies.end(),
      [&project](const SourceStrategyAssessment& value) {
        return value.id == project.selectedSourceStrategyId;
      });
  if (selectedStrategy == project.sourceStrategies.end() ||
      selectedStrategy->licenseLocator != project.licenseLocator ||
      selectedStrategy->licenseSha256 != project.licenseSha256) {
    return invalid("Selected source strategy license is not bound to the project");
  }
  if (project.operators.empty() ||
      std::any_of(project.operators.begin(), project.operators.end(),
                  [](const OperatorRecord& value) {
                    return value.role.empty();
                  })) {
    return invalid("Production operator records are incomplete");
  }
  const auto operatorExists = [&project](const std::string& operatorId) {
    return std::any_of(
        project.operators.begin(), project.operators.end(),
        [&operatorId](const OperatorRecord& value) {
          return value.operatorId == operatorId;
        });
  };
  std::set<std::string, std::less<>> assetDigests;
  for (const auto& asset : project.assets) {
    const std::filesystem::path relative{asset.relativePath};
    if (!isDigest(asset.sha256) || asset.byteSize == 0U ||
        relative.empty() || relative.is_absolute()) {
      return invalid("Production asset record is invalid");
    }
    for (const auto& part : relative) {
      if (part == "." || part == "..") return invalid("Production asset path is unsafe");
    }
    if (!assetDigests.insert(asset.sha256).second) {
      return invalid("Production asset digest is duplicated");
    }
  }
  std::set<std::string, std::less<>> revisions;
  for (const auto& revision : project.derivedRevisions) {
    if (!isDigest(revision.inputSha256) || !isDigest(revision.outputSha256) ||
        revision.operationVersion != "seam-pcm-ops-1" ||
        !operatorExists(revision.operatorId) ||
        !isProductionUtcTimestamp(revision.performedAtUtc) ||
        assetDigests.find(revision.inputSha256) == assetDigests.end() ||
        assetDigests.find(revision.outputSha256) == assetDigests.end()) {
      return invalid("Derived revision chain is invalid");
    }
    revisions.insert(revision.revisionId);
  }
  std::set<std::string, std::less<>> ownedRevisions;
  for (const auto& take : project.takes) {
    if (take.promptId.empty() || take.coverageKey.empty() ||
        assetDigests.find(take.rawAssetSha256) == assetDigests.end()) {
      return invalid("Take binding is invalid");
    }
    if (!take.supersedesTakeId.empty()) {
      const auto superseded = std::find_if(
          project.takes.begin(), project.takes.end(),
          [&take](const TakeRecord& value) {
            return value.takeId == take.supersedesTakeId;
          });
      if (superseded == project.takes.end() ||
          superseded->takeId == take.takeId ||
          superseded->coverageKey != take.coverageKey ||
          superseded->pitchLayer != take.pitchLayer ||
          superseded->promptId != take.promptId) {
        return invalid("Retake chain is invalid");
      }
    }
    auto inputSha256 = take.rawAssetSha256;
    for (const auto& revisionId : take.derivedRevisionIds) {
      const auto revision = std::find_if(
          project.derivedRevisions.begin(), project.derivedRevisions.end(),
          [&revisionId](const DerivedRevision& value) {
            return value.revisionId == revisionId;
          });
      if (revision == project.derivedRevisions.end() ||
          revision->inputSha256 != inputSha256 ||
          !ownedRevisions.insert(revisionId).second) {
        return invalid("Take references an unavailable derived revision");
      }
      inputSha256 = revision->outputSha256;
    }
  }
  if (ownedRevisions.size() != revisions.size()) {
    return invalid("Derived revision is not owned by exactly one take");
  }
  for (const auto& revision : project.metadataRevisions) {
    const auto take = std::find_if(
        project.takes.begin(), project.takes.end(),
        [&revision](const TakeRecord& value) { return value.takeId == revision.takeId; });
    if (take == project.takes.end() || revision.kind.empty() ||
        revision.values.empty() || !operatorExists(revision.operatorId) ||
        !isProductionUtcTimestamp(revision.performedAtUtc) ||
        revision.rawAssetSha256 != take->rawAssetSha256 ||
        assetDigests.find(revision.rawAssetSha256) == assetDigests.end()) {
      return invalid("Metadata revision is not bound to its immutable raw take");
    }
  }
  for (const auto& review : project.reviews) {
    const auto take = std::find_if(
        project.takes.begin(), project.takes.end(),
        [&review](const TakeRecord& value) { return value.takeId == review.takeId; });
    if (take == project.takes.end() || review.reviewerId.empty() ||
        !isProductionUtcTimestamp(review.reviewedAtUtc) ||
        (review.result != "PASS" && review.result != "REJECTED")) {
      return invalid("Review record is invalid or unbound");
    }
  }
  std::set<std::pair<std::string, std::int32_t>> assignments;
  for (const auto& assignment : project.unitAssignments) {
    if (assignment.coverageKey.empty() || assignment.promptId.empty() ||
        assignment.plannedTakeId.empty() ||
        !assignments.emplace(assignment.coverageKey, assignment.pitchLayer).second) {
      return invalid("Unit assignment is missing or duplicated");
    }
    if (assignment.state == UnitQueueState::Missing) {
      if (!assignment.takeId.empty()) {
        return invalid("Missing unit assignment references a take");
      }
      continue;
    }
    const auto take = std::find_if(
        project.takes.begin(), project.takes.end(),
        [&assignment](const TakeRecord& value) {
          return value.takeId == assignment.takeId;
        });
    if (take == project.takes.end() ||
        take->coverageKey != assignment.coverageKey ||
        take->pitchLayer != assignment.pitchLayer ||
        take->promptId != assignment.promptId ||
        take->state != assignment.state) {
      return invalid("Unit assignment take binding is invalid");
    }
    if (assignment.state == UnitQueueState::Approved &&
        (assignment.takeId.empty() || !assignment.markerReviewed ||
         !assignment.pitchReviewed)) {
      return invalid("Approved unit is missing take or review evidence");
    }
  }
  for (const auto& take : project.takes) {
    const auto current = std::find_if(
        project.unitAssignments.begin(), project.unitAssignments.end(),
        [&take](const UnitAssignment& value) {
          return value.takeId == take.takeId;
        });
    if (current == project.unitAssignments.end() &&
        take.state != UnitQueueState::Retake) {
      return invalid("Take is neither current nor retained as a retake");
    }
  }
  return core::success();
}

}
