#include "seam/voicebank_production/source_assessment.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/core/file_io.hpp"
#include "seam/voicebank/asset_path.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include <algorithm>
#include <map>

namespace seam::voicebank_production {
using J = formats::JsonValue;
std::string sourceQualityPolicyIdentity(const SourceStrategyAssessment& source) {
  return core::sha256Hex(formats::stringifyJson(J{J::Object{
      {"format","seam-source-quality-policy-v1"},{"id",source.id},{"kind",toString(source.kind)},
      {"rights",toString(source.rights)},{"licenseLocator",source.licenseLocator},{"licenseSha256",source.licenseSha256},
      {"sourceUse",source.permissions.sourceUse},{"transformation",source.permissions.transformation},
      {"singingBankRedistribution",source.permissions.singingBankRedistribution},{"commercialRenders",source.permissions.commercialRenders}}},false));
}

core::Result<std::string> sourceQualityMaterialIdentity(const VoicebankProductionProject& project, std::string_view strategyId) {
  std::map<std::string,J> rows;
  std::map<std::string_view,const TakeRecord*,std::less<>> takes;
  std::map<std::string_view,const TakeSourceBinding*,std::less<>> bindings;
  std::map<std::string_view,const DerivedRevision*,std::less<>> revisions;
  for (const auto& value : project.takes) takes.emplace(value.takeId,&value);
  for (const auto& value : project.sourceBindings) bindings.emplace(value.id,&value);
  for (const auto& value : project.derivedRevisions) revisions.emplace(value.revisionId,&value);
  for (const auto& assignment : project.unitAssignments) {
    if (assignment.takeId.empty()) continue;
    const auto foundTake = takes.find(assignment.takeId);
    if (foundTake == takes.end()) return core::failure<std::string>(core::ErrorCode::Conflict,"Assessment assignment has no take");
    const auto* take = foundTake->second;
    const auto foundBinding = bindings.find(take->sourceBindingId);
    if (foundBinding == bindings.end() || foundBinding->second->strategy.id != strategyId) continue;
    const auto* binding = foundBinding->second;
    std::string audio = take->rawAssetSha256, parent;
    J::Array editors;
    for (const auto& id : take->derivedRevisionIds) {
      const auto foundRevision = revisions.find(id);
      if (foundRevision == revisions.end() || foundRevision->second->inputSha256 != audio)
        return core::failure<std::string>(core::ErrorCode::Conflict,"Assessed take has a broken processing chain");
      const auto* revision = foundRevision->second;
      audio = revision->outputSha256; parent = revision->revisionId; editors.emplace_back(revision->operatorId);
    }
    J::Object materialRow{{"takeId",take->takeId},{"sourceBindingId",binding->id},
        {"coverageKey",assignment.coverageKey},{"pitchLayer",static_cast<std::int64_t>(assignment.pitchLayer)},
        {"promptId",assignment.promptId},{"rawSha256",take->rawAssetSha256},{"effectiveSha256",audio},
        {"parentRevisionId",parent},{"importerId",binding->importerId},{"editors",std::move(editors)}};
    if (project.schemaVersion >= kProductionStyleSchemaVersion) {
      materialRow.emplace("language", project.language);
      materialRow.emplace("style", assignment.style);
    }
    if (!rows.emplace(take->takeId, std::move(materialRow)).second)
      return core::failure<std::string>(core::ErrorCode::Conflict,"Assessed take appears in multiple assignments");
  }
  if (rows.empty()) return core::failure<std::string>(core::ErrorCode::InvalidState,"Source quality assessment requires active source-owned audio");
  J::Array material; for (auto& [id,row] : rows) { static_cast<void>(id); material.push_back(std::move(row)); }
  return core::sha256Hex(formats::stringifyJson(J{J::Object{{"format","seam-source-quality-material-v1"},
      {"projectId",project.projectId},{"inventoryId",project.inventoryId},{"inventorySha256",project.inventorySha256},
      {"strategyId",std::string{strategyId}},{"takes",std::move(material)}}},false));
}

core::Result<void> requireCurrentSourceQualityAssessment(const VoicebankProductionProject& project, std::string_view strategyId) {
  const auto assessment = std::find_if(project.sourceQualityAssessments.rbegin(),project.sourceQualityAssessments.rend(),
      [&](const auto& value) { return value.strategyId == strategyId; });
  // Historical schemas/policies retain their previous admission behavior; a
  // recorded assessment must never silently fall back to those assertions.
  if (assessment == project.sourceQualityAssessments.rend()) {
    if (project.schemaVersion >= kProductionStyleSchemaVersion)
      return core::failure(core::ErrorCode::Conflict, "Style-owned source requires an explicit current quality assessment");
    return core::success();
  }
  const auto source = std::find_if(project.sourceStrategies.begin(),project.sourceStrategies.end(),[&](const auto& value) { return value.id == strategyId; });
  const auto material = sourceQualityMaterialIdentity(project,strategyId);
  if (source == project.sourceStrategies.end() || !material || assessment->policySha256 != sourceQualityPolicyIdentity(*source) ||
      assessment->materialSha256 != material.value() || assessment->coverage != Feasibility::Pass || assessment->listening != Feasibility::Pass ||
      source->coverage != assessment->coverage || source->listening != assessment->listening)
    return core::failure(core::ErrorCode::Conflict,"Source quality assessment is stale or not passing; record an explicit reassessment",std::string{strategyId});
  return core::success();
}

core::Result<void> validateSourceQualityReviewer(const VoicebankProductionProject& project, const SourceQualityAssessment& assessment) {
  if (std::none_of(project.operators.begin(),project.operators.end(),[&](const auto& actor) {
      return actor.operatorId == assessment.reviewerId && actor.role == "REVIEWER";
  })) return core::failure(core::ErrorCode::Conflict,"Source quality decision requires a registered reviewer");
  for (const auto& binding : project.sourceBindings) {
    if (binding.strategy.id != assessment.strategyId) continue;
    const bool active = std::any_of(project.unitAssignments.begin(),project.unitAssignments.end(),[&](const auto& row) { return row.takeId == binding.takeId; });
    if (!active) continue;
    if (binding.importerId == assessment.reviewerId)
      return core::failure(core::ErrorCode::Conflict,"Source quality reviewer cannot review their imported material");
    const auto take = std::find_if(project.takes.begin(),project.takes.end(),[&](const auto& row) { return row.takeId == binding.takeId; });
    if (take == project.takes.end()) return core::failure(core::ErrorCode::Conflict,"Assessed source take is missing");
    for (const auto& id : take->derivedRevisionIds) {
      const auto revision = std::find_if(project.derivedRevisions.begin(),project.derivedRevisions.end(),[&](const auto& row) { return row.revisionId == id; });
      if (revision != project.derivedRevisions.end() && revision->operatorId == assessment.reviewerId)
        return core::failure(core::ErrorCode::Conflict,"Source quality reviewer cannot review their processed material");
    }
  }
  return core::success();
}

core::Result<ProductionCommitReceipt> ProductionProjectRepository::recordSourceQualityAssessment(
    VoicebankProductionProject& project, const SourceQualityAssessment& assessment,
    const std::filesystem::path& evidenceFile, std::string_view expectedProjectSha256, std::stop_token stop) {
  using Output = ProductionCommitReceipt;
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,"Source assessment cancelled");
  const auto current = verify(project); if (!current) return core::Result<Output>{current.error()};
  if (project.schemaVersion < 2 || core::sha256Hex(encodeProductionProject(project)) != expectedProjectSha256)
    return core::failure<Output>(core::ErrorCode::Conflict,"Source assessment requires its exact source-aware producer snapshot");
  const auto source = std::find_if(project.sourceStrategies.begin(),project.sourceStrategies.end(),[&](const auto& value) { return value.id == assessment.strategyId; });
  const auto material = sourceQualityMaterialIdentity(project,assessment.strategyId);
  if (source == project.sourceStrategies.end() || !material || assessment.policySha256 != sourceQualityPolicyIdentity(*source) ||
      assessment.materialSha256 != material.value())
    return core::failure<Output>(core::ErrorCode::Conflict,"Source assessment policy or audio material changed");
  if (std::any_of(project.sourceQualityAssessments.begin(),project.sourceQualityAssessments.end(),[&](const auto& row) { return row.id == assessment.id; }))
    return core::failure<Output>(core::ErrorCode::Conflict,"Source assessment ID is already recorded");
  const auto reviewer = validateSourceQualityReviewer(project,assessment); if (!reviewer) return core::Result<Output>{reviewer.error()};
  auto draft = project;
  draft.schemaVersion = std::max(draft.schemaVersion, kProductionAssessmentSchemaVersion);
  draft.sourceQualityAssessments.push_back(assessment);
  auto& assessed = draft.sourceStrategies[static_cast<std::size_t>(source-project.sourceStrategies.begin())];
  assessed.coverage = assessment.coverage; assessed.listening = assessment.listening;
  assessed.evidenceState = "RECORDED_SOURCE_QUALITY_ASSESSMENT:" + assessment.id;
  invalidateProductionQualification(draft);
  for (auto& assignment : draft.unitAssignments) {
    const auto binding = std::find_if(draft.sourceBindings.begin(),draft.sourceBindings.end(),[&](const auto& row) {
      return row.takeId == assignment.takeId && row.strategy.id == assessment.strategyId;
    });
    if (binding != draft.sourceBindings.end() && assignment.state == UnitQueueState::Approved) {
      assignment.state = UnitQueueState::MarkerReview; assignment.markerReviewed = false; assignment.pitchReviewed = false;
      for (auto& take : draft.takes) if (take.takeId == assignment.takeId) take.state = UnitQueueState::MarkerReview;
    }
  }
  const auto valid = validateProductionProject(draft); if (!valid) return core::Result<Output>{valid.error()};
  auto bytes = core::readFileBytesLimited(evidenceFile,4ULL*1024ULL*1024ULL);
  if (!bytes || bytes.value().empty() || core::sha256Hex(bytes.value()) != assessment.evidenceSha256)
    return core::failure<Output>(core::ErrorCode::Conflict,"Source assessment evidence is missing, empty, oversized or changed");
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,"Source assessment cancelled before evidence retention");
  const auto directory = root_ / "source-evidence";
  std::error_code error;
  if (!std::filesystem::exists(directory,error)) std::filesystem::create_directory(directory,error);
  const auto directoryStatus = std::filesystem::symlink_status(directory,error);
  if (error || !std::filesystem::is_directory(directoryStatus) || std::filesystem::is_symlink(directoryStatus))
    return core::failure<Output>(core::ErrorCode::Conflict,"Source evidence directory is unsafe");
  const auto retained = directory / (assessment.evidenceSha256 + ".quality.txt");
  const auto written = core::durableAtomicWriteNew(retained,bytes.value());
  if (!written) {
    const auto resolved = voicebank::resolveBankAsset(root_,"source-evidence/"+assessment.evidenceSha256+".quality.txt");
    const auto existing = resolved ? core::readFileBytesLimited(resolved.value(),4ULL*1024ULL*1024ULL) : core::Result<std::vector<std::byte>>{resolved.error()};
    if (!existing || existing.value() != bytes.value()) return core::Result<Output>{written.error()};
  }
  const ProductionJournalEvent event{"source-quality-assessment",assessment.id,assessment.reviewerId,assessment.reviewedAtUtc};
  const auto saved = save(draft,event,stop);
  bool durable = true; std::string diagnostic;
  if (!saved) {
    const auto recovered = recover();
    if (!recovered || recovered.value().lastDurableGeneration <= project.lastDurableGeneration)
      return core::Result<Output>{saved.error()};
    auto comparison = recovered.value(); comparison.lastDurableGeneration = draft.lastDurableGeneration;
    if (encodeProductionProject(comparison) != encodeProductionProject(draft))
      return core::failure<Output>(core::ErrorCode::Conflict,"Assessment commit was not confirmed; recovery found different work");
    draft = recovered.value(); durable = false;
    diagnostic = "Exact assessment is recoverably committed; inspect before further work and do not repeat. " + saved.error().message;
  }
  project = std::move(draft);
  return Output{project.lastDurableGeneration,core::sha256Hex(encodeProductionProject(project)),durable,std::move(diagnostic)};
}
}
