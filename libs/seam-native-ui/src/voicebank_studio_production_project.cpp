#include "seam/native_ui/voicebank_studio.hpp"

#include "voicebank_studio_production_support.hpp"

#include "seam/core/sha256.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace seam::native_ui {

core::Result<void> VoicebankStudioController::openProductionProject(
    const std::filesystem::path& workspaceRoot,
    std::string_view expectedInventorySha256,
    std::string operatorId) {
  if (workspaceRoot.empty() || expectedInventorySha256.empty() ||
      operatorId.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Production workspace, inventory hash, and operator are required");
  }
  auto repository = std::make_unique<
      voicebank_production::ProductionProjectRepository>(workspaceRoot);
  auto recovered = repository->recover();
  if (!recovered) return core::Result<void>{recovered.error()};
  if (recovered.value().inventorySha256 != expectedInventorySha256) {
    return core::failure(core::ErrorCode::Conflict,
                         "Production project inventory digest does not match");
  }
  stagedRecoveryCandidateCount_ =
      repository->inspectStaged(recovered.value()).size();
  productionProject_ = std::move(recovered.value());
  productionRepository_ = std::move(repository);
  productionWorkspaceRoot_ =
      std::filesystem::absolute(workspaceRoot).lexically_normal();
  productionOperatorId_ = std::move(operatorId);
  if (manifest_.units.empty()) selectedIndex_ = 0U;
  status_ = stagedRecoveryCandidateCount_ == 0U
                ? "PRODUCTION RECOVERED" : "RECOVERY CANDIDATES";
  return core::success();
}

core::Result<void> VoicebankStudioController::saveProductionProject() {
  if (!productionRepository_ || !productionProject_) {
    return core::failure(core::ErrorCode::Conflict,
                         "Voicebank Studio has no production project");
  }
  const auto occurredAt = voicebank_studio_internal::currentUtcTimestamp();
  auto saved = productionRepository_->save(
      *productionProject_,
      {.action = "save", .subjectId = productionProject_->projectId,
       .operatorId = productionOperatorId_, .occurredAtUtc = occurredAt});
  if (saved) status_ = "PRODUCTION SAVED";
  return saved;
}

const voicebank_production::UnitAssignment*
VoicebankStudioController::selectedProductionAssignment() const noexcept {
  if (!productionProject_) return nullptr;
  if (manifest_.units.empty()) {
    return selectedIndex_ < productionProject_->unitAssignments.size()
               ? &productionProject_->unitAssignments[selectedIndex_]
               : nullptr;
  }
  const auto* unit = selectedUnit();
  if (unit == nullptr) return nullptr;
  const auto coverage = voicebank_studio_internal::coverageKey(*unit);
  const auto assignment = std::find_if(
      productionProject_->unitAssignments.begin(),
      productionProject_->unitAssignments.end(),
      [&coverage, unit](const voicebank_production::UnitAssignment& value) {
        return value.coverageKey == coverage && value.pitchLayer == unit->rootMidi;
      });
  return assignment == productionProject_->unitAssignments.end()
             ? nullptr : &*assignment;
}

core::Result<void> VoicebankStudioController::inspectSelectedProductionTake(
    const std::filesystem::path& path) {
  const auto* assignment = selectedProductionAssignment();
  if (assignment == nullptr) {
    return core::failure(core::ErrorCode::InvalidState,
                         "No production inventory unit is selected");
  }
  return inspectTake(path, assignment->pitchLayer);
}

core::Result<void> VoicebankStudioController::importSelectedTake(
    const std::filesystem::path& takePath, std::string occurredAtUtc) {
  if (!productionRepository_ || !productionProject_) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Voicebank Studio has no production project");
  }
  const auto* selectedAssignment = selectedProductionAssignment();
  if (selectedAssignment == nullptr || !takeInspection_.has_value()) {
    return core::failure(core::ErrorCode::InvalidState,
                         "A selected production unit and inspected take are required");
  }
  if (occurredAtUtc.empty()) {
    occurredAtUtc = voicebank_studio_internal::currentUtcTimestamp();
  }
  const auto assignment = *selectedAssignment;
  const auto retake = !assignment.takeId.empty();
  const auto takeId = retake
      ? assignment.plannedTakeId + "-retake-" +
            std::to_string(productionProject_->takes.size() + 1U)
      : assignment.plannedTakeId;
  auto imported = productionRepository_->importRaw(
      *productionProject_, takePath,
      {.takeId = takeId,
       .promptId = assignment.promptId,
       .coverageKey = assignment.coverageKey,
       .pitchLayer = assignment.pitchLayer,
       .supersedesTakeId = assignment.takeId,
       .initialState = takeInspection_->accepted()
                           ? voicebank_production::UnitQueueState::MarkerReview
                           : voicebank_production::UnitQueueState::Rejected,
       .review = voicebank_production::ReviewRecord{
           .reviewId = "dry-take-" +
                       core::sha256Hex(takeId + takeInspection_->sourceSha256)
                           .substr(0U, 24U),
           .takeId = takeId,
           .reviewerId = productionOperatorId_,
           .result = takeInspection_->accepted() ? "PASS" : "REJECTED",
           .reviewedAtUtc = occurredAtUtc}},
      {.action = retake ? "retake" : "import",
       .subjectId = takeId,
       .operatorId = productionOperatorId_,
       .occurredAtUtc = std::move(occurredAtUtc)});
  if (!imported) return core::Result<void>{imported.error()};
  stagedRecoveryCandidateCount_ =
      productionRepository_->inspectStaged(*productionProject_).size();
  status_ = takeInspection_->accepted() ? "TAKE IMPORTED" : "TAKE REJECTED";
  return core::success();
}

core::Result<voicebank_production::ExportedU57Inputs>
VoicebankStudioController::exportProductionInputs(
    const std::filesystem::path& destination, std::string occurredAtUtc) {
  if (!productionRepository_ || !productionProject_) {
    return core::failure<voicebank_production::ExportedU57Inputs>(
        core::ErrorCode::InvalidState,
        "Voicebank Studio has no production project");
  }
  if (occurredAtUtc.empty()) {
    occurredAtUtc = voicebank_studio_internal::currentUtcTimestamp();
  }
  auto exported = productionRepository_->exportU57Inputs(
      *productionProject_, destination,
      {.action = "candidate-export", .subjectId = productionProject_->projectId,
       .operatorId = productionOperatorId_,
       .occurredAtUtc = std::move(occurredAtUtc)});
  if (exported) status_ = "U57 INPUTS EXPORTED";
  return exported;
}

voicebank_production::ProductionQueueSummary
VoicebankStudioController::productionQueues() const noexcept {
  return productionProject_.has_value()
             ? voicebank_production::summarizeQueues(*productionProject_)
             : voicebank_production::ProductionQueueSummary{};
}

std::optional<voicebank_production::UnitQueueState>
VoicebankStudioController::productionStateForUnit(
    const voicebank::Unit& unit) const noexcept {
  if (!productionProject_) return std::nullopt;
  const auto coverage = voicebank_studio_internal::coverageKey(unit);
  const auto assignment = std::find_if(
      productionProject_->unitAssignments.begin(),
      productionProject_->unitAssignments.end(),
      [&coverage, &unit](const voicebank_production::UnitAssignment& value) {
        return value.coverageKey == coverage && value.pitchLayer == unit.rootMidi;
      });
  return assignment == productionProject_->unitAssignments.end()
             ? std::nullopt : std::optional{assignment->state};
}

}
