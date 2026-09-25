#include "seam/voicebank_production/project_codec.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank_production/candidate_markers.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
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
  const bool legacy = project.schemaVersion == 1;
  if (!legacy && project.schemaVersion != kProductionProjectSchemaVersion && project.schemaVersion != kProductionAssessmentSchemaVersion && project.schemaVersion != kProductionStyleSchemaVersion) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Production project schema is unsupported");
  }
  const bool styleOwned = project.schemaVersion >= kProductionStyleSchemaVersion;
  if (styleOwned ? (project.language != "ja" && project.language != "en" && project.language != "ko") : !project.language.empty())
    return invalid("Production language does not match its schema");
  const auto validStyle = [styleOwned](const std::string& style) {
    return styleOwned ? (!style.empty() && style.size() <= 128U &&
        std::none_of(style.begin(), style.end(), [](unsigned char c) { return c < 32U || c == 127U; })) : style.empty();
  };
  if (project.projectId.empty() || project.immutableAssetRoot.empty() ||
      (legacy && (project.inventoryId.empty() || project.selectedSourceStrategyId.empty()))) {
    return invalid("Production project identity is incomplete");
  }
  if ((legacy && (!isDigest(project.inventorySha256) || !isDigest(project.licenseSha256))) ||
      (!legacy && ((!project.inventorySha256.empty() && !isDigest(project.inventorySha256)) ||
          (!project.licenseSha256.empty() && !isDigest(project.licenseSha256)) ||
          (project.inventoryId.empty() != project.inventorySha256.empty()) ||
          (!project.unitAssignments.empty() && project.inventoryId.empty())))) {
    return invalid("Production project hashes are invalid");
  }
  if ((legacy && (project.lifecycle != ProductionLifecycle::LegacyUnclassified || !project.sourceBindings.empty())) ||
      (!legacy && (project.lifecycle == ProductionLifecycle::LegacyUnclassified || toString(project.lifecycle).empty())))
    return invalid("Production lifecycle does not match its schema");
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
                 [](const auto& value) -> const auto& { return value.operatorId; }) ||
      !uniqueIds(project.sourceBindings,
                 [](const auto& value) -> const auto& { return value.id; })) {
    return invalid("Production project contains missing or duplicate identifiers");
  }
  if (legacy && !selectedStrategyReady(project)) {
    return invalid("Selected source strategy is not rights, coverage, and listening feasible");
  }
  const auto selectedStrategy = std::find_if(
      project.sourceStrategies.begin(), project.sourceStrategies.end(),
      [&project](const SourceStrategyAssessment& value) {
        return value.id == project.selectedSourceStrategyId;
      });
  if ((!project.selectedSourceStrategyId.empty() && (selectedStrategy == project.sourceStrategies.end() ||
          selectedStrategy->licenseLocator != project.licenseLocator || selectedStrategy->licenseSha256 != project.licenseSha256)) ||
      (project.selectedSourceStrategyId.empty() && (!project.licenseLocator.empty() || !project.licenseSha256.empty()))) {
    return invalid("Selected source strategy license is not bound to the project");
  }
  for (const auto& strategy : project.sourceStrategies) {
    if (toString(strategy.kind).empty() || toString(strategy.rights).empty() || toString(strategy.coverage).empty() ||
        toString(strategy.listening).empty() || (!strategy.licenseSha256.empty() &&
            (!isDigest(strategy.licenseSha256) || strategy.licenseLocator.empty())))
      return invalid("Production source strategy has invalid enum or evidence fields");
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
  if ((project.schemaVersion < kProductionAssessmentSchemaVersion && !project.sourceQualityAssessments.empty()) ||
      project.sourceQualityAssessments.size() > 1024U || !uniqueIds(project.sourceQualityAssessments,[](const auto& row) -> const auto& { return row.id; }))
    return invalid("Source quality history does not match its schema or bounds");
  for (const auto& row : project.sourceQualityAssessments) {
    if (row.id.size() > 128U || !isDigest(row.policySha256) || !isDigest(row.materialSha256) || !isDigest(row.evidenceSha256) ||
        !isProductionUtcTimestamp(row.reviewedAtUtc) || toString(row.coverage).empty() || toString(row.listening).empty() ||
        std::none_of(project.sourceStrategies.begin(),project.sourceStrategies.end(),[&](const auto& source) { return source.id == row.strategyId; }) ||
        std::none_of(project.operators.begin(),project.operators.end(),[&](const auto& actor) { return actor.operatorId == row.reviewerId && actor.role == "REVIEWER"; }))
      return invalid("Source quality assessment identity, evidence, or reviewer is invalid");
  }
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
  std::set<std::string> sourceOwnedTakes;
  for (const auto& source : project.sourceBindings) {
    const auto take = std::find_if(project.takes.begin(), project.takes.end(), [&](const auto& value) { return value.takeId == source.takeId; });
    if (take == project.takes.end() || take->sourceBindingId != source.id || take->rawAssetSha256 != source.rawAssetSha256 ||
        !isDigest(source.rawAssetSha256) || !assetDigests.contains(source.rawAssetSha256) ||
        source.strategy.id.empty() || toString(source.strategy.kind).empty() ||
        source.strategy.rights != Feasibility::Pass || !source.strategy.permissions.sourceUse || !source.strategy.permissions.transformation ||
        toString(source.strategy.coverage).empty() || toString(source.strategy.listening).empty() ||
        source.strategy.licenseLocator.empty() || !isDigest(source.strategy.licenseSha256) ||
        !operatorExists(source.importerId) || !isProductionUtcTimestamp(source.importedAtUtc) ||
        source.licenseSnapshotPath != "source-evidence/" + source.strategy.licenseSha256 + ".txt" ||
        !sourceOwnedTakes.insert(source.takeId).second)
      return invalid("Captured source binding is not an authorized immutable ingress record for exactly one take");
  }
  for (const auto& take : project.takes) {
    if ((legacy && !take.sourceBindingId.empty()) || (!take.sourceBindingId.empty() && !sourceOwnedTakes.contains(take.takeId)))
      return invalid("Take references an unavailable source binding or source-aware legacy field");
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
        !validStyle(take.style) || (styleOwned && (take.pitchLayer < 24 || take.pitchLayer > 96)) ||
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
          superseded->style != take.style ||
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
  std::set<std::string> editedCandidates;
  for (const auto& revision : project.metadataRevisions) {
    if (revision.kind == "candidate-marker-edit") editedCandidates.insert(revision.takeId);
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
    if (revision.kind == "procedural-lineage") {
      const auto& values = revision.values;
      const bool generated = values.contains("generationExpectationSha256");
      if (values.size() != (generated ? 7U : 6U) || (generated && !isDigest(values.at("generationExpectationSha256"))))
        return invalid("Procedural lineage has an invalid shape");
      for (const auto* key : {"candidateMetadata", "candidateMetadataSha256", "recipeJson", "recipeHash", "renderContentHash", "approval"})
        if (!values.contains(key)) return invalid("Procedural lineage is incomplete");
      if (values.at("approval") != "unapproved" || values.at("candidateMetadata").size() > 4U * 1024U * 1024U ||
          values.at("recipeJson").size() > 512U * 1024U || !isDigest(values.at("renderContentHash")) ||
          core::sha256Hex(values.at("candidateMetadata")) != values.at("candidateMetadataSha256") ||
          core::sha256Hex(values.at("recipeJson")) != values.at("recipeHash")) return invalid("Procedural lineage bytes or status differ from their recorded identity");
      const auto metadata = formats::parseJson(values.at("candidateMetadata"), {.maximumInputBytes = 4U * 1024U * 1024U,
          .maximumDepth = 4U, .maximumNodes = 100000U, .maximumStringBytes = 256U, .maximumCollectionEntries = 16384U});
      if (!metadata || !metadata.value().isObject()) return invalid("Procedural lineage metadata is invalid");
      for (const auto& [key, expected] : {std::pair{"audioSha256", revision.rawAssetSha256},
          std::pair{"recipeHash", values.at("recipeHash")}, std::pair{"renderContentHash", values.at("renderContentHash")},
          std::pair{"approval", std::string{"unapproved"}}}) {
        const auto* value = metadata.value().find(key);
        if (!value || !value->isString() || value->asString() != expected) return invalid("Procedural lineage metadata is not bound to its raw asset and recipe");
      }
    } else if (revision.kind == "dry-take-inspection.v1") {
      const auto& values = revision.values;
      if (values.size() != 2U || !values.contains("evidenceJson") ||
          !values.contains("evidenceSha256") ||
          !isDigest(values.at("evidenceSha256")) ||
          core::sha256Hex(values.at("evidenceJson")) != values.at("evidenceSha256"))
        return invalid("Dry-take technical evidence digest is invalid");
      const auto evidence = formats::parseJson(values.at("evidenceJson"),
          {.maximumInputBytes = 65536U, .maximumDepth = 6U,
           .maximumNodes = 128U, .maximumStringBytes = 4096U,
           .maximumCollectionEntries = 32U});
      if (!evidence || !evidence.value().isObject())
        return invalid("Dry-take technical evidence JSON is invalid");
      const auto& object = evidence.value().asObject();
      constexpr std::array<std::string_view, 14U> evidenceFields{
          "schemaVersion", "inspectorId", "inspectorVersion", "takeSha256",
          "sampleRate", "channels", "bitsPerSample", "expectedRootMidi",
          "analyzedRootMidi", "peak", "rms", "dcOffset", "status", "quality"};
      if (object.size() != evidenceFields.size() ||
          std::any_of(evidenceFields.begin(), evidenceFields.end(), [&](std::string_view key) {
            return !evidence.value().find(key);
          })) return invalid("Dry-take technical evidence fields are incomplete or unknown");
      const auto* schema = evidence.value().find("schemaVersion");
      const auto* inspector = evidence.value().find("inspectorId");
      const auto* inspectorVersion = evidence.value().find("inspectorVersion");
      const auto* takeHash = evidence.value().find("takeSha256");
      const auto* sampleRate = evidence.value().find("sampleRate");
      const auto* channels = evidence.value().find("channels");
      const auto* bits = evidence.value().find("bitsPerSample");
      const auto* expectedMidi = evidence.value().find("expectedRootMidi");
      const auto* analyzedMidi = evidence.value().find("analyzedRootMidi");
      const auto* peak = evidence.value().find("peak");
      const auto* rms = evidence.value().find("rms");
      const auto* dc = evidence.value().find("dcOffset");
      const auto* status = evidence.value().find("status");
      const auto* quality = evidence.value().find("quality");
      constexpr auto maximumMeasuredMagnitude =
          static_cast<double>(std::numeric_limits<float>::max());
      constexpr std::array<std::string_view, 6U> qualityFields{
          "formatValid", "finite", "clippingFree", "silenceFree",
          "dcOffsetFree", "rootPitchValid"};
      if (!schema->isInteger() || schema->asInt64() != 1 ||
          !inspector->isString() || inspector->asString() != "seam.dry-take-inspector" ||
          !inspectorVersion->isString() || inspectorVersion->asString() != "1" ||
          !takeHash->isString() || takeHash->asString() != revision.rawAssetSha256 ||
          !sampleRate->isInteger() || sampleRate->asInt64() < 8000 || sampleRate->asInt64() > 384000 ||
          !channels->isInteger() || channels->asInt64() < 1 || channels->asInt64() > 8 ||
          !bits->isInteger() || bits->asInt64() < 8 || bits->asInt64() > 32 ||
          !expectedMidi->isInteger() || expectedMidi->asInt64() < 0 || expectedMidi->asInt64() > 127 ||
          (!analyzedMidi->isNull() && (!analyzedMidi->isInteger() || analyzedMidi->asInt64() < 0 || analyzedMidi->asInt64() > 127)) ||
          !peak->isNumber() || !rms->isNumber() || !dc->isNumber() ||
          !std::isfinite(peak->asNumber()) || !std::isfinite(rms->asNumber()) || !std::isfinite(dc->asNumber()) ||
          peak->asNumber() < 0.0 || peak->asNumber() > maximumMeasuredMagnitude ||
          rms->asNumber() < 0.0 || rms->asNumber() > maximumMeasuredMagnitude ||
          dc->asNumber() < -maximumMeasuredMagnitude || dc->asNumber() > maximumMeasuredMagnitude ||
          !status->isString() ||
          !quality->isObject() || quality->asObject().size() != qualityFields.size() ||
          std::any_of(qualityFields.begin(), qualityFields.end(), [&](std::string_view key) {
            const auto* value = quality->find(key);
            return value == nullptr || !value->isBool();
          })) return invalid("Dry-take technical evidence values are invalid");
      const bool allSignalChecksPassed = std::all_of(
          qualityFields.begin(), qualityFields.end(), [&](std::string_view key) {
            return quality->find(key)->asBool();
          });
      const auto expectedStatus = allSignalChecksPassed
          ? "SIGNAL_CHECKS_PASSED" : "SIGNAL_CHECKS_NEED_REVIEW";
      if (status->asString() != expectedStatus)
        return invalid("Dry-take technical status does not match its measured checks");
    }
  }
  for (const auto& takeId : editedCandidates) {
    const auto resolved = resolveCandidateMarkers(project, takeId);
    if (!resolved) return invalid(resolved.error().message);
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
  std::set<ProductionUnitIdentity> assignments;
  for (const auto& assignment : project.unitAssignments) {
    if (assignment.coverageKey.empty() || assignment.promptId.empty() ||
        !validStyle(assignment.style) || (styleOwned && (assignment.pitchLayer < 24 || assignment.pitchLayer > 96)) ||
        assignment.plannedTakeId.empty() ||
        !assignments.insert({project.language, assignment.style, assignment.coverageKey, assignment.pitchLayer}).second) {
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
        take->style != assignment.style ||
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
  if (project.lifecycle == ProductionLifecycle::Qualified) {
    if (project.unitAssignments.empty()) return invalid("Qualified producer has no required units");
    for (const auto& assignment : project.unitAssignments) {
      if (assignment.state != UnitQueueState::Approved || !assignment.markerReviewed || !assignment.pitchReviewed)
        return invalid("Qualified producer requires complete accepted unit review");
      const auto source = requireTakeSourceQualification(project, assignment.takeId);
      if (!source) return invalid(source.error().message);
      const auto latest = std::find_if(project.reviews.rbegin(), project.reviews.rend(),
          [&](const auto& review) { return review.takeId == assignment.takeId; });
      if (latest == project.reviews.rend() || latest->result != "PASS")
        return invalid("Qualified producer lacks an explicit accepted review");
    }
  }
  return core::success();
}

}
