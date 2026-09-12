#include "seam/voicebank_production/repository.hpp"
#include "repository_source_internal.hpp"
#include "repository_history_internal.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank/asset_path.hpp"

#include <algorithm>
#include <limits>

namespace seam::voicebank_production {
namespace history_internal {
namespace {
using J = formats::JsonValue;
constexpr std::uint64_t kMaximumAborts = 1024U;
constexpr std::uint64_t kMaximumJournalBytes = 1024U * 1024U;
constexpr std::uint64_t kMaximumAbortedJournalBytes = 16ULL * 1024ULL * 1024ULL;
bool digest(std::string_view value) {
  return value.size() == 64U && std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
bool fields(const J& object, std::initializer_list<std::string_view> names) {
  return object.isObject() && object.asObject().size() == names.size() &&
      std::all_of(names.begin(), names.end(), [&](auto name) { return object.find(name); });
}
core::Result<void> absentSnapshot(const std::filesystem::path& root, std::uint64_t generation) {
  const auto path = root / "generations" / generationFilename(generation);
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error == std::errc::no_such_file_or_directory || (!error && status.type() == std::filesystem::file_type::not_found))
    return core::success();
  return core::failure(core::ErrorCode::Conflict,
      "An aborted write must not replace an existing generation snapshot", path.string());
}
}  // namespace

std::string generationFilename(std::uint64_t generation) {
  auto name = std::to_string(generation);
  name.insert(0U, 20U - name.size(), '0');
  return name + ".json";
}

core::Result<Ancestry> verifyAncestry(const std::filesystem::path& root,
    const J& journal, std::string_view projectId, std::uint64_t generation) {
  const auto* ancestry = journal.find("ancestry");
  if (!ancestry) return Ancestry{};  // Legacy bytes remain a separate, contiguous contract.
  const auto invalid = [] { return core::failure<Ancestry>(core::ErrorCode::Conflict,
      "Production journal ancestry or aborted-write evidence is invalid"); };
  if (!fields(*ancestry, {"format", "parentGeneration", "parentProjectSha256", "parentJournalSha256", "abortedGenerations"}))
    return invalid();
  const auto* format = ancestry->find("format");
  const auto* parent = ancestry->find("parentGeneration");
  const auto* projectHash = ancestry->find("parentProjectSha256");
  const auto* journalHash = ancestry->find("parentJournalSha256");
  const auto* aborted = ancestry->find("abortedGenerations");
  if (!format->isString() || format->asString() != "parent-and-aborted-journals-v1" ||
      !parent->isInteger() || parent->asInt64() < 0 || !projectHash->isString() || !journalHash->isString() ||
      !aborted->isArray() || aborted->asArray().size() > kMaximumAborts || generation == 0U)
    return invalid();
  Ancestry result{true, static_cast<std::uint64_t>(parent->asInt64()), {}};
  if (result.parentGeneration >= generation || generation - result.parentGeneration - 1U != aborted->asArray().size())
    return invalid();
  if (result.parentGeneration == 0U) {
    if (generation != 1U || !projectHash->asString().empty() || !journalHash->asString().empty()) return invalid();
  } else {
    if (!digest(projectHash->asString()) || !digest(journalHash->asString())) return invalid();
    const auto name = generationFilename(result.parentGeneration);
    const auto parentBytes = core::readTextFileLimited(root / "generations" / name, 64ULL * 1024ULL * 1024ULL);
    const auto parentJournal = core::readTextFileLimited(root / "journal" / name, kMaximumJournalBytes);
    if (!parentBytes || !parentJournal || core::sha256Hex(parentBytes.value()) != projectHash->asString() ||
        core::sha256Hex(parentJournal.value()) != journalHash->asString()) return invalid();
    const auto parentProject = decodeProductionProject(parentBytes.value());
    if (!parentProject || parentProject.value().projectId != projectId ||
        parentProject.value().lastDurableGeneration != result.parentGeneration) return invalid();
  }
  std::uint64_t totalAbortedBytes = 0U;
  for (const auto& entry : aborted->asArray()) {
    if (!fields(entry, {"generation", "journalSha256", "journalBytes"})) return invalid();
    const auto* number = entry.find("generation");
    const auto* hash = entry.find("journalSha256");
    const auto* bytes = entry.find("journalBytes");
    const auto expected = result.parentGeneration + result.aborted.size() + 1U;
    if (!number->isInteger() || number->asInt64() <= 0 || static_cast<std::uint64_t>(number->asInt64()) != expected ||
        !hash->isString() || !digest(hash->asString()) || !bytes->isInteger() || bytes->asInt64() < 0 ||
        static_cast<std::uint64_t>(bytes->asInt64()) > kMaximumJournalBytes ||
        static_cast<std::uint64_t>(bytes->asInt64()) > kMaximumAbortedJournalBytes - totalAbortedBytes) return invalid();
    const auto absent = absentSnapshot(root, expected);
    if (!absent) return core::Result<Ancestry>{absent.error()};
    const auto original = core::readTextFileLimited(root / "journal" / generationFilename(expected), kMaximumJournalBytes);
    if (!original || original.value().size() != static_cast<std::uint64_t>(bytes->asInt64()) ||
        core::sha256Hex(original.value()) != hash->asString()) return invalid();
    totalAbortedBytes += static_cast<std::uint64_t>(bytes->asInt64());
    result.aborted.push_back({expected, hash->asString(), static_cast<std::uint64_t>(bytes->asInt64())});
  }
  return result;
}

core::Result<J> captureAncestry(const std::filesystem::path& root, std::string_view projectId,
    std::uint64_t parentGeneration, std::uint64_t generation) {
  if (generation <= parentGeneration || generation - parentGeneration - 1U > kMaximumAborts ||
      generation > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    return core::failure<J>(core::ErrorCode::Conflict, "Production aborted-write recovery exceeds its bounded history contract");
  std::string parentProjectHash, parentJournalHash;
  if (parentGeneration != 0U) {
    const auto name = generationFilename(parentGeneration);
    const auto project = core::readTextFileLimited(root / "generations" / name, 64ULL * 1024ULL * 1024ULL);
    const auto journal = core::readTextFileLimited(root / "journal" / name, kMaximumJournalBytes);
    if (!project) return core::Result<J>{project.error()};
    if (!journal) return core::Result<J>{journal.error()};
    parentProjectHash = core::sha256Hex(project.value()); parentJournalHash = core::sha256Hex(journal.value());
  }
  J::Array aborted;
  if (generation > parentGeneration + 1U) {
    // A durable pointer to a now-missing newer generation proves data loss,
    // not an interrupted write. Never turn that evidence into an abort receipt.
    const auto pointer = core::readTextFileLimited(root / "project.json", 64ULL * 1024ULL * 1024ULL);
    if (!pointer) return core::failure<J>(core::ErrorCode::Conflict,
        "Aborted-write recovery requires an inspectable previous published pointer");
    const auto decoded = decodeProductionProject(pointer.value());
    if (!decoded || decoded.value().projectId != projectId)
      return core::failure<J>(core::ErrorCode::Conflict, "Aborted-write recovery cannot establish the previous published pointer");
    if (decoded.value().lastDurableGeneration > parentGeneration)
      return core::failure<J>(core::ErrorCode::Conflict, "A missing published generation cannot be classified as an aborted write");
  }
  std::uint64_t totalAbortedBytes = 0U;
  for (auto number = parentGeneration + 1U; number < generation; ++number) {
    const auto absent = absentSnapshot(root, number);
    if (!absent) return core::Result<J>{absent.error()};
    const auto original = core::readTextFileLimited(root / "journal" / generationFilename(number), kMaximumJournalBytes);
    if (!original) return core::Result<J>{original.error()};
    if (original.value().size() > kMaximumAbortedJournalBytes - totalAbortedBytes)
      return core::failure<J>(core::ErrorCode::Unsupported, "Aborted-write evidence exceeds its aggregate byte limit");
    totalAbortedBytes += static_cast<std::uint64_t>(original.value().size());
    const auto parsed = formats::parseJson(original.value());
    if (parsed) {
      // A complete legacy journal lacks a parent binding, so a missing snapshot
      // cannot safely be distinguished from deleted committed history.
      const auto* recordedGeneration = parsed.value().find("generation");
      const auto attempt = verifyAncestry(root, parsed.value(), projectId, number);
      if (!recordedGeneration || !recordedGeneration->isInteger() ||
          recordedGeneration->asInt64() != static_cast<std::int64_t>(number) ||
          !attempt || !attempt.value().recorded || attempt.value().parentGeneration != parentGeneration)
        return core::failure<J>(core::ErrorCode::Conflict,
            "A complete journal without verified attempt ancestry cannot be classified as an aborted write");
    }
    aborted.emplace_back(J::Object{{"generation", static_cast<std::int64_t>(number)},
        {"journalSha256", core::sha256Hex(original.value())},
        {"journalBytes", static_cast<std::int64_t>(original.value().size())}});
  }
  return J{J::Object{{"format", "parent-and-aborted-journals-v1"},
      {"parentGeneration", static_cast<std::int64_t>(parentGeneration)},
      {"parentProjectSha256", parentProjectHash}, {"parentJournalSha256", parentJournalHash},
      {"abortedGenerations", std::move(aborted)}}};
}
}  // namespace history_internal

namespace source_internal {
core::Result<void> verifySnapshots(const std::filesystem::path& root, const VoicebankProductionProject& project) {
  for (const auto& assessment : project.sourceQualityAssessments) {
    const auto resolved = voicebank::resolveBankAsset(root,"source-evidence/"+assessment.evidenceSha256+".quality.txt");
    if (!resolved) return core::Result<void>{resolved.error()};
    const auto digest = core::sha256File(resolved.value(),4ULL*1024ULL*1024ULL);
    if (!digest || digest.value() != assessment.evidenceSha256)
      return core::failure(core::ErrorCode::Conflict,"Retained source assessment evidence changed or is missing",assessment.id);
  }
  for (const auto& binding : project.sourceBindings) {
    if (binding.strategy.licenseSha256.size() != 64U ||
        binding.licenseSnapshotPath != "source-evidence/" + binding.strategy.licenseSha256 + ".txt")
      return core::failure(core::ErrorCode::Conflict, "Captured source evidence path is invalid");
    const auto resolved = voicebank::resolveBankAsset(root, binding.licenseSnapshotPath);
    if (!resolved) return core::Result<void>{resolved.error()};
    const auto digest = core::sha256File(resolved.value(), 4ULL * 1024ULL * 1024ULL);
    if (!digest || digest.value() != binding.strategy.licenseSha256)
      return core::failure(core::ErrorCode::Conflict, "Captured source evidence changed or is missing", binding.id);
  }
  return core::success();
}

core::Result<void> preserveBindings(const VoicebankProductionProject& current, const VoicebankProductionProject& proposed) {
  if (proposed.sourceQualityAssessments.size() < current.sourceQualityAssessments.size() ||
      !std::equal(current.sourceQualityAssessments.begin(),current.sourceQualityAssessments.end(),proposed.sourceQualityAssessments.begin()))
    return core::failure(core::ErrorCode::Conflict,"Source quality history is append-only and immutable");
  if (current.schemaVersion >= 2 && proposed.schemaVersion < current.schemaVersion)
    return core::failure(core::ErrorCode::Conflict, "Source-aware producer generations cannot be downgraded");
  for (const auto& binding : current.sourceBindings) {
    const auto found = std::find_if(proposed.sourceBindings.begin(), proposed.sourceBindings.end(),
        [&](const auto& value) { return value.id == binding.id; });
    if (found == proposed.sourceBindings.end() || *found != binding)
      return core::failure(core::ErrorCode::Conflict, "Captured take source bindings are immutable", binding.id);
  }
  for (const auto& take : current.takes) {
    if (take.sourceBindingId.empty()) continue;
    const auto found = std::find_if(proposed.takes.begin(), proposed.takes.end(), [&](const auto& value) { return value.takeId == take.takeId; });
    if (found == proposed.takes.end() || found->sourceBindingId != take.sourceBindingId || found->rawAssetSha256 != take.rawAssetSha256)
      return core::failure(core::ErrorCode::Conflict, "Existing take source ownership cannot be reassigned", take.takeId);
  }
  return core::success();
}
}  // namespace source_internal

core::Result<void> ProductionProjectRepository::verify(
    const VoicebankProductionProject& project) const {
  return verifyGeneration(project, true);
}

core::Result<void> ProductionProjectRepository::verifyGeneration(
    const VoicebankProductionProject& project,
    bool requireCurrentPointer) const {
  auto valid = validateProductionProject(project);
  if (!valid) return valid;
  if (project.schemaVersion == 1) {
    auto licenseDigest = core::sha256File(project.licenseLocator);
    if (!licenseDigest) return core::Result<void>{licenseDigest.error()};
    if (licenseDigest.value() != project.licenseSha256)
      return core::failure(core::ErrorCode::InvariantViolation, "Production source-license digest changed");
  } else {
    const auto evidence = source_internal::verifySnapshots(root_, project);
    if (!evidence) return evidence;
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
  const auto ancestry = history_internal::verifyAncestry(root_, parsed.value(), project.projectId, project.lastDurableGeneration);
  if (!ancestry) return core::Result<void>{ancestry.error()};
  return core::success();
}

}
