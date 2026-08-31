#include "seam/voicebank_production/repository.hpp"

#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <system_error>

namespace seam::voicebank_production {

core::Result<ExportedU57Inputs> ProductionProjectRepository::exportU57Inputs(
    VoicebankProductionProject& project,
    const std::filesystem::path& destination,
    const ProductionJournalEvent& event) {
  if (destination.empty() || destination == destination.root_path()) {
    return core::failure<ExportedU57Inputs>(
        core::ErrorCode::InvalidArgument, "U57 export directory is invalid");
  }
  std::error_code error;
  if (std::filesystem::exists(destination, error)) {
    const auto status = std::filesystem::symlink_status(destination, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
      return core::failure<ExportedU57Inputs>(
          core::ErrorCode::Conflict,
          "U57 export destination must be a real directory",
          destination.string());
    }
  } else {
    std::filesystem::create_directories(destination, error);
    if (error) {
      return core::failure<ExportedU57Inputs>(
          core::ErrorCode::IoError, "Unable to create U57 export directory",
          error.message());
    }
  }
  auto saved = save(project, event);
  if (!saved) return core::Result<ExportedU57Inputs>{saved.error()};
  auto verified = verify(project);
  if (!verified) return core::Result<ExportedU57Inputs>{verified.error()};
  const auto selected = std::find_if(
      project.sourceStrategies.begin(), project.sourceStrategies.end(),
      [&project](const SourceStrategyAssessment& value) {
        return value.id == project.selectedSourceStrategyId;
      });
  if (selected == project.sourceStrategies.end()) {
    return core::failure<ExportedU57Inputs>(
        core::ErrorCode::InvariantViolation, "Selected strategy is unavailable");
  }
  const auto queues = summarizeQueues(project);
  const auto status = std::string{"SYNTHETIC_READY_REAL_ASSETS_REQUIRED"};
  const auto brief = formats::stringifyJson(
      formats::JsonValue{formats::JsonValue::Object{
          {"format", "com.project-seam.voicebank-production-brief"},
          {"schemaVersion", std::int64_t{1}},
          {"status", status},
          {"projectId", project.projectId},
          {"generation", static_cast<std::int64_t>(project.lastDurableGeneration)},
          {"inventoryId", project.inventoryId},
          {"inventorySha256", project.inventorySha256},
          {"selectedSourceStrategy", formats::JsonValue::Object{
              {"id", selected->id},
              {"kind", toString(selected->kind)},
              {"rights", toString(selected->rights)},
              {"coverage", toString(selected->coverage)},
              {"listening", toString(selected->listening)},
              {"evidenceState", selected->evidenceState},
              {"licenseLocator", selected->licenseLocator},
              {"licenseSha256", selected->licenseSha256},
              {"permissions", formats::JsonValue::Object{
                  {"sourceUse", selected->permissions.sourceUse},
                  {"transformation", selected->permissions.transformation},
                  {"singingBankRedistribution", selected->permissions.singingBankRedistribution},
                  {"commercialRenders", selected->permissions.commercialRenders},
              }},
          }},
          {"queues", formats::JsonValue::Object{
              {"missing", static_cast<std::int64_t>(queues.missing)},
              {"rejected", static_cast<std::int64_t>(queues.rejected)},
              {"retake", static_cast<std::int64_t>(queues.retake)},
              {"markerReview", static_cast<std::int64_t>(queues.markerReview)},
              {"pitchReview", static_cast<std::int64_t>(queues.pitchReview)},
              {"approved", static_cast<std::int64_t>(queues.approved)},
          }},
          {"realAssetGate", "U57 must replace synthetic inputs and obtain independent musical and rights approval"},
      }}, true) + "\n";
  formats::JsonValue::Array units;
  units.reserve(project.unitAssignments.size());
  for (const auto& assignment : project.unitAssignments) {
    units.emplace_back(formats::JsonValue::Object{
        {"coverageKey", assignment.coverageKey},
        {"pitchLayer", static_cast<std::int64_t>(assignment.pitchLayer)},
        {"productionTakeId", assignment.takeId},
        {"productionState", toString(assignment.state)},
        {"realAssetSha256", ""},
        {"independentRightsApproval", "NOT_RUN"},
        {"independentMusicalApproval", "NOT_RUN"},
    });
  }
  const auto candidate = formats::stringifyJson(
      formats::JsonValue{formats::JsonValue::Object{
          {"format", "com.project-seam.voicebank-u57-candidate-template"},
          {"schemaVersion", std::int64_t{1}},
          {"status", "BLOCKED"},
          {"blockedBy", formats::JsonValue::Array{
              formats::JsonValue{"REAL_LAWFUL_ASSETS"},
              formats::JsonValue{"INDEPENDENT_RIGHTS_APPROVAL"},
              formats::JsonValue{"INDEPENDENT_MUSICAL_APPROVAL"},
          }},
          {"projectId", project.projectId},
          {"inventorySha256", project.inventorySha256},
          {"units", std::move(units)},
      }}, true) + "\n";
  const auto briefPath = destination / "production-brief.json";
  const auto candidatePath = destination / "candidate-template.json";
  auto briefWrite = core::durableAtomicWriteText(briefPath, brief);
  if (!briefWrite) return core::Result<ExportedU57Inputs>{briefWrite.error()};
  auto candidateWrite = core::durableAtomicWriteText(candidatePath, candidate);
  if (!candidateWrite) return core::Result<ExportedU57Inputs>{candidateWrite.error()};
  return ExportedU57Inputs{
      .briefPath = briefPath,
      .candidateTemplatePath = candidatePath,
      .status = status,
  };
}

}
