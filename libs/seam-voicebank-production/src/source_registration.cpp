#include "seam/voicebank_production/repository.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"

#include <algorithm>

namespace seam::voicebank_production {
core::Result<ProductionCommitReceipt> ProductionProjectRepository::registerSource(
    VoicebankProductionProject& project, const SourceStrategyAssessment& source,
    std::string_view expectedProjectSha256, std::string producerId,
    std::string occurredAtUtc, std::stop_token stop) {
  using Output = ProductionCommitReceipt;
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,"Source registration cancelled");
  const auto verified = verify(project); if (!verified) return core::Result<Output>{verified.error()};
  if (project.schemaVersion < 2 || core::sha256Hex(encodeProductionProject(project)) != expectedProjectSha256)
    return core::failure<Output>(core::ErrorCode::Conflict,"Source registration requires the exact source-aware producer snapshot");
  if (source.id.empty() || source.id.size() > 128U || project.sourceStrategies.size() >= 256U ||
      source.licenseLocator.empty() || source.licenseLocator.size() > 4096U ||
      source.coverage != Feasibility::NotAssessed || source.listening != Feasibility::NotAssessed ||
      !isProductionUtcTimestamp(occurredAtUtc) ||
      std::none_of(project.operators.begin(),project.operators.end(),[&](const auto& actor) {
        return actor.operatorId == producerId && actor.role == "PRODUCER";
      })) return core::failure<Output>(core::ErrorCode::InvalidArgument,"Registration requires a registered producer, bounded identity, evidence and unassessed musical outcomes");
  if (std::any_of(project.sourceStrategies.begin(),project.sourceStrategies.end(),[&](const auto& row) { return row.id == source.id; }))
    return core::failure<Output>(core::ErrorCode::Conflict,"Source ID already exists; registration never rewrites captured policy");
  auto draft = project;
  auto registered = source;
  registered.evidenceState = "PRODUCER_DECLARATION_NOT_MUSICAL_QUALIFICATION";
  draft.sourceStrategies.push_back(registered);
  draft.selectedSourceStrategyId = registered.id;
  draft.licenseLocator = registered.licenseLocator;
  draft.licenseSha256 = registered.licenseSha256;
  invalidateProductionQualification(draft);
  const auto valid = validateProductionProject(draft); if (!valid) return core::Result<Output>{valid.error()};
  const auto evidence = core::readFileBytesLimited(source.licenseLocator,4ULL*1024ULL*1024ULL);
  if (!evidence || evidence.value().empty() || core::sha256Hex(evidence.value()) != source.licenseSha256)
    return core::failure<Output>(core::ErrorCode::Conflict,"Source evidence is missing, empty, oversized or differs from the captured digest");
  // Retain the exact declaration now, before any imported material depends on it.
  const auto directory = root_ / "source-evidence";
  std::error_code error;
  const auto status = std::filesystem::symlink_status(directory,error);
  if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status))
    return core::failure<Output>(core::ErrorCode::Conflict,"Source evidence directory is unsafe");
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,"Source registration cancelled before retention");
  const auto retained = directory / (source.licenseSha256 + ".txt");
  const auto written = core::durableAtomicWriteNew(retained,evidence.value());
  if (!written) {
    const auto existing = core::readFileBytesLimited(retained,4ULL*1024ULL*1024ULL);
    if (!existing || existing.value() != evidence.value()) return core::Result<Output>{written.error()};
  }
  const auto saved = save(draft,{"source-register",source.id,std::move(producerId),std::move(occurredAtUtc)},stop);
  bool durable = true; std::string diagnostic;
  if (!saved) {
    const auto recovered = recover();
    if (!recovered || recovered.value().lastDurableGeneration <= project.lastDurableGeneration)
      return core::Result<Output>{saved.error()};
    auto comparable = recovered.value(); comparable.lastDurableGeneration = draft.lastDurableGeneration;
    if (encodeProductionProject(comparable) != encodeProductionProject(draft))
      return core::failure<Output>(core::ErrorCode::Conflict,"Source registration was not confirmed; recovery found different work");
    draft = recovered.value(); durable = false;
    diagnostic = "Exact source declaration is recoverably committed; inspect before retrying. " + saved.error().message;
  }
  project = std::move(draft);
  return Output{project.lastDurableGeneration,core::sha256Hex(encodeProductionProject(project)),durable,std::move(diagnostic)};
}
} // namespace seam::voicebank_production
