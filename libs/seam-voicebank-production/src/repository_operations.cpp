#include "seam/voicebank_production/repository.hpp"
#include "operation_staging_internal.hpp"

#include "seam/core/sha256.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/take_inspection_receipt.hpp"

#include <algorithm>
#include <set>
#include <system_error>

namespace seam::voicebank_production {
namespace {

core::Result<ProductionCommitReceipt> confirmMutation(
    ProductionProjectRepository& repository, VoicebankProductionProject& project,
    const VoicebankProductionProject& original, const core::Result<void>& saved,
    std::string_view operation) {
  ProductionCommitReceipt receipt;
  if (!saved) {
    const auto recovered = repository.recover();
    if (recovered && recovered.value().lastDurableGeneration > original.lastDurableGeneration) {
      auto comparable = recovered.value();
      comparable.lastDurableGeneration = project.lastDurableGeneration;
      if (encodeProductionProject(comparable) == encodeProductionProject(project)) {
        project = recovered.value();
        receipt.durabilityConfirmed = false;
        receipt.diagnostic = std::string{operation} +
            " is recoverably committed, but pointer durability is uncertain. Recover and inspect this generation before further work; do not repeat this operation. " + saved.error().message;
      } else {
        project = original;
        return core::failure<ProductionCommitReceipt>(core::ErrorCode::Conflict,
            std::string{operation} + " save failed and recovery found different work. Recover before retrying; no matching commit is confirmed.");
      }
    } else {
      project = original;
      return core::failure<ProductionCommitReceipt>(saved.error().code,
          std::string{operation} + " did not confirm a commit. Recover and compare the intended revision before retrying: " + saved.error().message);
    }
  }
  receipt.committedGeneration = project.lastDurableGeneration;
  receipt.committedProjectSha256 = core::sha256Hex(encodeProductionProject(project));
  return receipt;
}

}

core::Result<CommittedDerivedRevision> ProductionProjectRepository::commitStaged(
    VoicebankProductionProject& project, const StagedOperation& staged,
    std::string revisionId, std::string operatorId,
    std::string performedAtUtc, std::string_view takeId,
    std::string_view expectedParentRevisionId) {
  if (revisionId.empty() || operatorId.empty() || performedAtUtc.empty() || takeId.empty()) {
    return core::failure<CommittedDerivedRevision>(
        core::ErrorCode::InvalidArgument, "Derived revision identity is incomplete");
  }
  // Use recovery instead of the current pointer: an interrupted pointer write
  // must not prevent a caller from editing a successfully recovered generation.
  const auto durable = recover();
  if (!durable) return core::Result<CommittedDerivedRevision>{durable.error()};
  if (encodeProductionProject(durable.value()) != encodeProductionProject(project)) {
    return core::failure<CommittedDerivedRevision>(core::ErrorCode::Conflict,
        "Production edit requires the exact current durable project");
  }
  if (staged.takeId != takeId || staged.parentRevisionId != expectedParentRevisionId ||
      staged.sourceProjectSha256 != core::sha256Hex(encodeProductionProject(project))) {
    return core::failure<CommittedDerivedRevision>(core::ErrorCode::Conflict,
        "Staged operation belongs to another take, parent, or project generation");
  }
  const auto permitted = requireTakeSourceExecution(project, takeId);
  if (!permitted) return core::Result<CommittedDerivedRevision>{permitted.error()};
  auto take = std::find_if(project.takes.begin(), project.takes.end(),
      [takeId](const TakeRecord& value) { return value.takeId == takeId; });
  if (take == project.takes.end()) {
    return core::failure<CommittedDerivedRevision>(core::ErrorCode::NotFound,
        "Production edit target take is unavailable");
  }
  const std::string_view currentParent = take->derivedRevisionIds.empty()
      ? std::string_view{} : std::string_view{take->derivedRevisionIds.back()};
  if (currentParent != expectedParentRevisionId) {
    return core::failure<CommittedDerivedRevision>(core::ErrorCode::Conflict,
        "Production edit parent revision is stale");
  }
  auto currentInput = take->rawAssetSha256;
  if (!currentParent.empty()) {
    const auto parent = std::find_if(project.derivedRevisions.begin(), project.derivedRevisions.end(),
        [currentParent](const DerivedRevision& value) { return value.revisionId == currentParent; });
    if (parent == project.derivedRevisions.end()) {
      return core::failure<CommittedDerivedRevision>(core::ErrorCode::InvariantViolation,
          "Production edit parent revision is unavailable");
    }
    currentInput = parent->outputSha256;
  }
  if (currentInput != staged.inputSha256) {
    return core::failure<CommittedDerivedRevision>(core::ErrorCode::Conflict,
        "Production edit input does not match its selected take and parent");
  }
  if (std::any_of(project.derivedRevisions.begin(), project.derivedRevisions.end(),
                  [&revisionId](const DerivedRevision& value) {
                    return value.revisionId == revisionId;
                  })) {
    return core::failure<CommittedDerivedRevision>(
        core::ErrorCode::Conflict, "Derived revision identifier already exists");
  }
  const auto expectedParent = (root_ / "staging").lexically_normal();
  if (staged.path.parent_path().lexically_normal() != expectedParent) {
    return core::failure<CommittedDerivedRevision>(
        core::ErrorCode::InvalidArgument, "Staged output is outside this workspace");
  }
  auto digest = core::sha256File(staged.path);
  if (!digest) return core::Result<CommittedDerivedRevision>{digest.error()};
  if (digest.value() != staged.outputSha256) {
    return core::failure<CommittedDerivedRevision>(
        core::ErrorCode::InvariantViolation, "Staged output digest changed");
  }
  const auto inputExists = std::any_of(
      project.assets.begin(), project.assets.end(),
      [&staged](const AssetRecord& value) {
        return value.sha256 == staged.inputSha256;
      });
  if (!inputExists) {
    return core::failure<CommittedDerivedRevision>(
        core::ErrorCode::NotFound, "Derived input asset is unavailable");
  }
  const auto validatedStage = validateStagedOperation(root_, staged);
  if (!validatedStage) return core::Result<CommittedDerivedRevision>{validatedStage.error()};
  auto imported = assetStore_.importFile(staged.path, AssetKind::Derived);
  if (!imported) return core::Result<CommittedDerivedRevision>{imported.error()};
  if (imported.value().sha256 != staged.outputSha256) {
    return core::failure<CommittedDerivedRevision>(
        core::ErrorCode::InvariantViolation, "Derived asset digest changed during commit");
  }
  // Inspect the exact stored derivative under this assignment's current policy.
  // Failed outcomes remain diagnostics; this operation never grants review.
  const auto request = takeInspectionRequestFor(take->coverageKey, take->pitchLayer);
  if (!request) return core::Result<CommittedDerivedRevision>{request.error()};
  const auto inspected = voicebank::inspectTake(assetStore_.pathFor(imported.value()), request.value());
  if (!inspected) return core::Result<CommittedDerivedRevision>{inspected.error()};
  if (inspected.value().sourceSha256 != imported.value().sha256 || inspected.value().byteSize != imported.value().byteSize)
    return core::failure<CommittedDerivedRevision>(core::ErrorCode::Conflict, "Derived audio changed during inspection");
  const auto original = project;
  invalidateProductionQualification(project);
  if (std::none_of(project.assets.begin(), project.assets.end(),
                   [&imported](const AssetRecord& value) {
                     return value.sha256 == imported.value().sha256;
                   })) {
    project.assets.push_back(imported.value());
  }
  DerivedRevision revision{
      .revisionId = std::move(revisionId),
      .inputSha256 = staged.inputSha256,
      .outputSha256 = staged.outputSha256,
      .operation = staged.request.kind,
      .operationVersion = "seam-pcm-ops-1",
      .parameters = operationParameters(staged.request),
      .operatorId = std::move(operatorId),
      .performedAtUtc = std::move(performedAtUtc),
  };
  project.derivedRevisions.push_back(revision);
  take->derivedRevisionIds.push_back(revision.revisionId);
  project.metadataRevisions.push_back(makeDerivedTakeInspectionRevision(
      {{take->takeId, take->promptId, take->coverageKey, take->pitchLayer}, inspected.value()},
      take->rawAssetSha256, revision.revisionId, revision.operatorId, revision.performedAtUtc));
  // Keep immutable reviews and annotations for history. Only their current
  // applicability changes; an unrelated take's approvals must remain intact.
  if (take->state != UnitQueueState::Retake) take->state = UnitQueueState::MarkerReview;
  for (auto& assignment : project.unitAssignments) {
    if (assignment.takeId != takeId) continue;
    assignment.state = UnitQueueState::MarkerReview;
    assignment.markerReviewed = false;
    assignment.pitchReviewed = false;
  }
  auto saved = save(
      project, {.action = "transform", .subjectId = revision.revisionId,
                .operatorId = revision.operatorId,
                .occurredAtUtc = revision.performedAtUtc});
  const auto receipt = confirmMutation(*this, project, original, saved, "Audio edit");
  if (!receipt) return core::Result<CommittedDerivedRevision>{receipt.error()};
  return CommittedDerivedRevision{revision, receipt.value()};
}

core::Result<CommittedMetadataRevision> ProductionProjectRepository::recordCurrentTakeInspection(
    VoicebankProductionProject& project, std::string_view takeId,
    std::string_view expectedActiveRevisionId, std::string_view expectedProjectSha256,
    std::string producerId, std::string occurredAtUtc, std::stop_token stopToken) {
  using Output = CommittedMetadataRevision;
  if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Take inspection cancelled");
  if (core::sha256Hex(encodeProductionProject(project)) != expectedProjectSha256)
    return core::failure<Output>(core::ErrorCode::Conflict, "Take inspection requires the captured current project digest");
  if (!isProductionUtcTimestamp(occurredAtUtc) ||
      std::none_of(project.operators.begin(), project.operators.end(), [&](const auto& actor) {
        return actor.operatorId == producerId && actor.role == "PRODUCER";
      }))
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Take inspection requires a registered producer and UTC time");
  const auto verified = verify(project);
  if (!verified) return core::Result<Output>{verified.error()};
  const auto permitted = requireTakeSourceExecution(project, takeId);
  if (!permitted) return core::Result<Output>{permitted.error()};
  const auto take = std::find_if(project.takes.begin(), project.takes.end(),
      [&](const auto& value) { return value.takeId == takeId; });
  if (take == project.takes.end()) return core::failure<Output>(core::ErrorCode::NotFound, "Inspection take is unavailable");
  const std::string_view active = take->derivedRevisionIds.empty() ? std::string_view{} : take->derivedRevisionIds.back();
  if (active != expectedActiveRevisionId)
    return core::failure<Output>(core::ErrorCode::Conflict, "Take inspection requires the captured active revision");
  if (currentActiveTakeInspection(project, takeId))
    return core::failure<Output>(core::ErrorCode::Conflict, "This take already has current diagnostics; no receipt is rewritten");
  auto audioSha = take->rawAssetSha256;
  if (!active.empty()) {
    const auto revision = std::find_if(project.derivedRevisions.begin(), project.derivedRevisions.end(),
        [&](const auto& value) { return value.revisionId == active; });
    if (revision == project.derivedRevisions.end())
      return core::failure<Output>(core::ErrorCode::InvariantViolation, "Inspection active revision is unavailable");
    audioSha = revision->outputSha256;
  }
  const auto asset = std::find_if(project.assets.begin(), project.assets.end(),
      [&](const auto& value) { return value.sha256 == audioSha; });
  if (asset == project.assets.end()) return core::failure<Output>(core::ErrorCode::NotFound, "Inspection asset is unavailable");
  const auto request = takeInspectionRequestFor(take->coverageKey, take->pitchLayer);
  if (!request) return core::Result<Output>{request.error()};
  const auto inspected = voicebank::inspectTake(assetStore_.pathFor(*asset), request.value(), stopToken);
  if (!inspected) return core::Result<Output>{inspected.error()};
  if (inspected.value().sourceSha256 != audioSha || inspected.value().byteSize != asset->byteSize)
    return core::failure<Output>(core::ErrorCode::Conflict, "Stored audio changed during take inspection");
  if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Take inspection cancelled before commit");
  TakeInspectionEvidence evidence{{take->takeId, take->promptId, take->coverageKey, take->pitchLayer}, inspected.value()};
  auto revision = active.empty() ? makeTakeInspectionRevision(evidence, producerId, occurredAtUtc) :
      makeDerivedTakeInspectionRevision(evidence, take->rawAssetSha256, std::string{active}, producerId, occurredAtUtc);
  const ProductionJournalEvent event{"marker", revision.revisionId, producerId, occurredAtUtc};
  // The existing durable annotation writer rechecks the generation and makes this
  // diagnostic a review-basis change, preserving history and lowering affected approvals.
  return recordMetadataRevisionBound(project, std::move(revision), event);
}

core::Result<CommittedMetadataRevision> ProductionProjectRepository::recordMetadataRevision(
    VoicebankProductionProject& project, MetadataRevision revision,
    const ProductionJournalEvent& event) {
  if (revision.kind == kTakeInspectionRevisionKind || revision.kind == kDerivedTakeInspectionRevisionKind)
    return core::failure<CommittedMetadataRevision>(core::ErrorCode::InvalidArgument,
        "Diagnostic receipts require the stored-audio inspection path, not an annotation");
  return recordMetadataRevisionBound(project, std::move(revision), event);
}

core::Result<CommittedMetadataRevision> ProductionProjectRepository::recordMetadataRevisionBound(
    VoicebankProductionProject& project, MetadataRevision revision,
    const ProductionJournalEvent& event) {
  if (revision.revisionId.empty() || revision.takeId.empty() ||
      revision.kind.empty() || revision.values.empty() ||
      revision.operatorId.empty() || revision.performedAtUtc.empty() ||
      event.action != "marker" || event.subjectId != revision.revisionId ||
      event.operatorId != revision.operatorId || event.occurredAtUtc != revision.performedAtUtc) {
    return core::failure<CommittedMetadataRevision>(core::ErrorCode::InvalidArgument,
                         "Metadata revision or journal event is invalid");
  }
  if (revision.kind == "sample-candidate-review-v1" || revision.kind == "sample-candidate-review-v2")
    return core::failure<CommittedMetadataRevision>(core::ErrorCode::InvalidArgument,
        "Review material is recorded only by an independent review decision, not as an annotation");
  const auto durable = recover();
  if (!durable) return core::Result<CommittedMetadataRevision>{durable.error()};
  if (encodeProductionProject(durable.value()) != encodeProductionProject(project))
    return core::failure<CommittedMetadataRevision>(core::ErrorCode::Conflict,
        "Metadata edit requires the exact current durable project");
  const auto take = std::find_if(
      project.takes.begin(), project.takes.end(),
      [&revision](const TakeRecord& value) { return value.takeId == revision.takeId; });
  if (take == project.takes.end() ||
      take->rawAssetSha256 != revision.rawAssetSha256) {
    return core::failure<CommittedMetadataRevision>(core::ErrorCode::InvariantViolation,
                         "Metadata revision is not bound to its raw take");
  }
  if (std::any_of(project.metadataRevisions.begin(),
                  project.metadataRevisions.end(),
                  [&revision](const MetadataRevision& value) {
                    return value.revisionId == revision.revisionId;
                  })) {
    return core::failure<CommittedMetadataRevision>(core::ErrorCode::Conflict,
                         "Metadata revision identifier already exists");
  }
  const auto original = project;
  {
    const auto assignment = std::find_if(project.unitAssignments.begin(), project.unitAssignments.end(),
        [&](const auto& value) { return value.takeId == revision.takeId; });
    if (revision.kind == "candidate-marker-edit" && assignment == project.unitAssignments.end()) return core::failure<CommittedMetadataRevision>(
            core::ErrorCode::Conflict, "Candidate marker edits require the active take and matching journal attribution");
    invalidateProductionQualification(project);
    if (assignment != project.unitAssignments.end()) {
      assignment->markerReviewed = false;
      assignment->pitchReviewed = false;
      assignment->state = UnitQueueState::MarkerReview;
      take->state = UnitQueueState::MarkerReview;
    }
  }
  project.metadataRevisions.push_back(revision);
  auto saved = save(project, event);
  const auto receipt = confirmMutation(*this, project, original, saved, "Metadata edit");
  if (!receipt) return core::Result<CommittedMetadataRevision>{receipt.error()};
  return CommittedMetadataRevision{std::move(revision), receipt.value()};
}

std::vector<std::filesystem::path>
ProductionProjectRepository::inspectStaged(
    const VoicebankProductionProject& project) const {
  std::vector<std::filesystem::path> outputs;
  std::set<std::string, std::less<>> committedDigests;
  for (const auto& revision : project.derivedRevisions) {
    committedDigests.insert(revision.outputSha256);
  }
  std::error_code error;
  for (std::filesystem::directory_iterator iterator{root_ / "staging", error}, end;
       !error && iterator != end; iterator.increment(error)) {
    if (!iterator->is_regular_file(error) || error ||
        iterator->path().extension() != ".wav") {
      continue;
    }
    const auto digest = core::sha256File(iterator->path());
    if (!digest || committedDigests.find(digest.value()) == committedDigests.end()) {
      outputs.push_back(iterator->path());
    }
  }
  std::sort(outputs.begin(), outputs.end());
  return outputs;
}

std::filesystem::path ProductionProjectRepository::assetPath(
    const AssetRecord& asset) const {
  return assetStore_.pathFor(asset);
}

}
