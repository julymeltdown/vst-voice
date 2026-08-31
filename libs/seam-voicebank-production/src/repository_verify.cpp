#include "seam/voicebank_production/repository.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank_production/project_codec.hpp"

#include <algorithm>

namespace seam::voicebank_production {

core::Result<void> ProductionProjectRepository::verify(
    const VoicebankProductionProject& project) const {
  return verifyGeneration(project, true);
}

core::Result<void> ProductionProjectRepository::verifyGeneration(
    const VoicebankProductionProject& project,
    bool requireCurrentPointer) const {
  auto valid = validateProductionProject(project);
  if (!valid) return valid;
  auto licenseDigest = core::sha256File(project.licenseLocator);
  if (!licenseDigest) return core::Result<void>{licenseDigest.error()};
  if (licenseDigest.value() != project.licenseSha256) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Production source-license digest changed");
  }
  for (const auto& asset : project.assets) {
    auto verified = assetStore_.verify(asset);
    if (!verified) return verified;
  }
  auto generation = core::readTextFileLimited(
      generationPath(project.lastDurableGeneration), 64U * 1024U * 1024U);
  if (!generation) return core::Result<void>{generation.error()};
  auto persistedProject = decodeProductionProject(generation.value());
  if (!persistedProject ||
      encodeProductionProject(persistedProject.value()) !=
          encodeProductionProject(project)) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Production project differs from its durable generation");
  }
  if (requireCurrentPointer) {
    const auto pointer = core::readTextFileLimited(
        root_ / "project.json", 64U * 1024U * 1024U);
    if (!pointer) return core::Result<void>{pointer.error()};
    auto pointerProject = decodeProductionProject(pointer.value());
    if (!pointerProject ||
        pointerProject.value().lastDurableGeneration != project.lastDurableGeneration ||
        core::sha256Hex(generation.value()) != core::sha256Hex(pointer.value())) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Production project pointer is stale or invalid");
    }
  }
  auto journal = core::readTextFileLimited(
      journalPath(project.lastDurableGeneration), 1024U * 1024U);
  if (!journal) return core::Result<void>{journal.error()};
  auto parsed = formats::parseJson(journal.value());
  if (!parsed || !parsed.value().isObject()) {
    return core::failure(core::ErrorCode::ParseError,
                         "Production journal record is invalid");
  }
  const auto* journalGeneration = parsed.value().find("generation");
  const auto* format = parsed.value().find("format");
  const auto* schemaVersion = parsed.value().find("schemaVersion");
  const auto* journalDigest = parsed.value().find("projectSha256");
  const auto* action = parsed.value().find("action");
  const auto* subject = parsed.value().find("subjectId");
  const auto* operatorId = parsed.value().find("operatorId");
  const auto* occurred = parsed.value().find("occurredAtUtc");
  if (format == nullptr || !format->isString() ||
      format->asString() !=
          "com.project-seam.voicebank-production-journal-event" ||
      schemaVersion == nullptr || !schemaVersion->isInteger() ||
      schemaVersion->asInt64() != 1 ||
      journalGeneration == nullptr || !journalGeneration->isInteger() ||
      journalDigest == nullptr || !journalDigest->isString() ||
      action == nullptr || !action->isString() ||
      !isProductionJournalAction(action->asString()) ||
      subject == nullptr || !subject->isString() || subject->asString().empty() ||
      operatorId == nullptr || !operatorId->isString() || operatorId->asString().empty() ||
      std::none_of(project.operators.begin(), project.operators.end(),
                   [operatorId](const OperatorRecord& value) {
                     return value.operatorId == operatorId->asString();
                   }) ||
      occurred == nullptr || !occurred->isString() ||
      !isProductionUtcTimestamp(occurred->asString()) ||
      journalGeneration->asInt64() != static_cast<std::int64_t>(project.lastDurableGeneration) ||
      journalDigest->asString() != core::sha256Hex(generation.value())) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Production journal binding is invalid");
  }
  return core::success();
}

}
