#include "seam/voicebank_production/candidate_publication.hpp"
#include "candidate_publication_internal.hpp"
#include "repository_history_internal.hpp"

#include "seam/core/exclusive_file_lock.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank/content_identity.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/validator.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <set>
#include <system_error>
#if defined(__APPLE__)
#include <stdio.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#endif

namespace seam::voicebank_production {
namespace candidate_publication_internal {
using J = formats::JsonValue;
constexpr std::size_t kMaximumUnits = 4096U;
constexpr std::uint64_t kMaximumCandidateAudioBytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumManifestBytes = 32ULL * 1024ULL * 1024ULL;
std::atomic<std::uint64_t> stagingSequence{0U};

bool isDigest(std::string_view value) {
  return value.size() == 64U && std::all_of(value.begin(), value.end(),
      [](char character) { return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'); });
}

core::Result<void> cancelled(std::stop_token stop) {
  return stop.stop_requested() ? core::failure(core::ErrorCode::Conflict, "Sample candidate publication cancelled") : core::success();
}

core::Result<std::filesystem::path> realDirectory(const std::filesystem::path& path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status))
    return core::failure<std::filesystem::path>(core::ErrorCode::Conflict, "Candidate directory must be an existing real directory", path.string());
  auto canonical = std::filesystem::canonical(path, error);
  if (error) return core::failure<std::filesystem::path>(core::ErrorCode::IoError, "Cannot resolve candidate directory", error.message());
  return canonical;
}

core::Result<void> absentDestination(const std::filesystem::path& path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error == std::errc::no_such_file_or_directory || (!error && status.type() == std::filesystem::file_type::not_found))
    return core::success();
  return core::failure(core::ErrorCode::Conflict, "Candidate destination already exists or cannot be inspected", path.string());
}

core::Result<const TakeRecord*> selectedTake(const VoicebankProductionProject& project,
                                            const SampleCandidateUnitBinding& binding) {
  const auto take = std::find_if(project.takes.begin(), project.takes.end(),
      [&](const auto& value) { return value.takeId == binding.takeId; });
  if (take == project.takes.end()) return core::failure<const TakeRecord*>(core::ErrorCode::NotFound, "Candidate take is missing", binding.takeId);
  std::string effectiveAudio = take->rawAssetSha256;
  for (const auto& revisionId : take->derivedRevisionIds) {
    const auto revision = std::find_if(project.derivedRevisions.begin(), project.derivedRevisions.end(),
        [&](const auto& value) { return value.revisionId == revisionId; });
    if (revision == project.derivedRevisions.end() || revision->inputSha256 != effectiveAudio)
      return core::failure<const TakeRecord*>(core::ErrorCode::Conflict, "Candidate processing chain is incomplete", binding.takeId);
    effectiveAudio = revision->outputSha256;
  }
  if (!isDigest(binding.audioSha256) || binding.audioSha256 != effectiveAudio)
    return core::failure<const TakeRecord*>(core::ErrorCode::Conflict, "Candidate audio is not the current take revision", binding.takeId);
  return &*take;
}

std::string coverageKey(const voicebank::Unit& unit) {
  std::string result{unit.kind == voicebank::UnitKind::Glottal ? "glottal-attack" : voicebank::unitKindName(unit.kind)};
  for (const auto& phone : unit.phones) result += ":" + phone;
  return result;
}

bool reviewMetadata(std::string_view kind) {
  return kind == "sample-candidate-review-v1" || kind == kSampleCandidateReviewKind;
}

std::string reviewBasis(const VoicebankProductionProject& project, std::string_view takeId) {
  auto basis = project;
  basis.lastDurableGeneration = 0U;
  basis.reviews.clear();
  basis.operators.clear();
  if (basis.schemaVersion >= 2) basis.lifecycle = ProductionLifecycle::Experimental;
  std::erase_if(basis.metadataRevisions, [&](const auto& value) {
    return reviewMetadata(value.kind) || (!takeId.empty() && value.takeId != takeId);
  });
  if (!takeId.empty()) {
    std::erase_if(basis.takes, [&](const auto& value) { return value.takeId != takeId; });
    std::erase_if(basis.unitAssignments, [&](const auto& value) { return value.takeId != takeId; });
    std::set<std::string> revisions, assets;
    for (const auto& take : basis.takes) {
      revisions.insert(take.derivedRevisionIds.begin(), take.derivedRevisionIds.end());
      assets.insert(take.rawAssetSha256);
    }
    std::erase_if(basis.derivedRevisions, [&](const auto& value) { return !revisions.contains(value.revisionId); });
    for (const auto& revision : basis.derivedRevisions) { assets.insert(revision.inputSha256); assets.insert(revision.outputSha256); }
    std::erase_if(basis.assets, [&](const auto& value) { return !assets.contains(value.sha256); });
    std::set<std::string> bindings, strategies;
    for (const auto& take : basis.takes) if (!take.sourceBindingId.empty()) bindings.insert(take.sourceBindingId);
    std::erase_if(basis.sourceBindings, [&](const auto& source) { return !bindings.contains(source.id); });
    for (const auto& source : basis.sourceBindings) strategies.insert(source.strategy.id);
    if (basis.schemaVersion >= 2 && !strategies.empty()) {
      basis.selectedSourceStrategyId.clear();
      basis.licenseLocator.clear(); basis.licenseSha256.clear();
      std::erase_if(basis.sourceStrategies, [&](const auto& strategy) { return !strategies.contains(strategy.id); });
      std::erase_if(basis.sourceQualityAssessments,[&](const auto& assessment) { return !strategies.contains(assessment.strategyId); });
    } else {
      std::erase_if(basis.sourceStrategies, [&](const auto& strategy) { return strategy.id != basis.selectedSourceStrategyId; });
    }
  }
  for (auto& take : basis.takes) take.state = UnitQueueState::MarkerReview;
  for (auto& assignment : basis.unitAssignments) {
    assignment.state = assignment.takeId.empty() ? UnitQueueState::Missing : UnitQueueState::MarkerReview;
    assignment.markerReviewed = false;
    assignment.pitchReviewed = false;
  }
  return core::sha256Hex("sample-candidate-review-basis-v2\n" + encodeProductionProject(basis));
}

core::Result<std::string> boundedManifest(const voicebank::Manifest& manifest) {
  const auto encoded = voicebank::ManifestJsonCodec{}.encode(manifest);
  if (!encoded) return encoded;
  if (encoded.value().size() > kMaximumManifestBytes)
    return core::failure<std::string>(core::ErrorCode::Unsupported, "Candidate manifest exceeds the installed decoder byte limit");
  const auto decoded = voicebank::ManifestJsonCodec{}.decode(encoded.value());
  if (!decoded) return core::Result<std::string>{decoded.error()};
  if (decoded.value() != manifest)
    return core::failure<std::string>(core::ErrorCode::Conflict, "Candidate manifest cannot reopen with its exact typed values");
  return encoded;
}

// The current TakeRecord does not retain its importer. Recover attribution from
// the first durable generation containing the take and the hash-bound journal
// for that generation, including a batch's actor. Do not infer it from roles.
core::Result<std::map<std::string, OriginAttribution>> collectOriginAttribution(
    const std::filesystem::path& workspace, const VoicebankProductionProject& project,
    const std::vector<SampleCandidateUnitBinding>& bindings, std::stop_token stop) {
  using Output = std::map<std::string, OriginAttribution>;
  std::map<std::string, std::string> requested;
  for (const auto& binding : bindings) {
    const auto take = selectedTake(project, binding);
    if (!take) return core::Result<Output>{take.error()};
    requested.emplace(binding.takeId, take.value()->rawAssetSha256);
  }
  std::map<std::uint64_t, std::filesystem::path> generations;
  std::error_code error;
  std::size_t inspectedEntries = 0U;
  for (std::filesystem::directory_iterator iterator{workspace / "generations", error}, end;
       !error && iterator != end; iterator.increment(error)) {
    if (++inspectedEntries > 65536U)
      return core::failure<Output>(core::ErrorCode::Unsupported, "Candidate origin history exceeds its record limit");
    const auto name = iterator->path().filename().string();
    if (name.size() != 25U || !name.ends_with(".json"))
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate origin history contains an unrecognized generation");
    std::uint64_t generation = 0U;
    const auto parsed = std::from_chars(name.data(), name.data() + 20U, generation);
    if (parsed.ec != std::errc{} || parsed.ptr != name.data() + 20U)
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate origin generation filename is invalid");
    if (generation <= project.lastDurableGeneration) generations.emplace(generation, iterator->path());
  }
  if (error || generations.empty() || generations.begin()->first != 1U)
    return core::failure<Output>(core::ErrorCode::Conflict, "Candidate original import attribution is unavailable");
  Output origins;
  std::uint64_t historyBytes = 0U;
  std::uint64_t expectedGeneration = 1U;
  constexpr std::uint64_t maximumHistoryBytes = 256ULL * 1024ULL * 1024ULL;
  for (const auto& [generation, path] : generations) {
    const auto active = cancelled(stop);
    if (!active) return core::Result<Output>{active.error()};
    const auto bytes = core::readTextFileLimited(path, std::min<std::uint64_t>(64ULL * 1024ULL * 1024ULL, maximumHistoryBytes - historyBytes));
    if (!bytes) return core::Result<Output>{bytes.error()};
    historyBytes += static_cast<std::uint64_t>(bytes.value().size());
    const auto snapshot = decodeProductionProject(bytes.value());
    if (!snapshot || snapshot.value().projectId != project.projectId || snapshot.value().lastDurableGeneration != generation)
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate origin snapshot identity is invalid");
    const auto journalBytes = core::readTextFileLimited(workspace / "journal" / path.filename(),
        std::min<std::uint64_t>(1024ULL * 1024ULL, maximumHistoryBytes - historyBytes));
    if (!journalBytes) return core::Result<Output>{journalBytes.error()};
    historyBytes += static_cast<std::uint64_t>(journalBytes.value().size());
    const auto journal = formats::parseJson(journalBytes.value());
    if (!journal || !journal.value().isObject())
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate origin journal is malformed");
    const auto& record = journal.value();
    const auto textEquals = [&](std::string_view key, std::string_view expected) {
      const auto* value = record.find(key);
      return value && value->isString() && value->asString() == expected;
    };
    const auto* schema = record.find("schemaVersion");
    const auto* eventGeneration = record.find("generation");
    const auto* actor = record.find("operatorId");
    const auto* action = record.find("action");
    const auto* subject = record.find("subjectId");
    const auto* occurred = record.find("occurredAtUtc");
    if (!textEquals("format", "com.project-seam.voicebank-production-journal-event") ||
        !schema || !schema->isInteger() || schema->asInt64() != 1 ||
        !eventGeneration || !eventGeneration->isInteger() || eventGeneration->asInt64() != static_cast<std::int64_t>(generation) ||
        !textEquals("projectSha256", core::sha256Hex(bytes.value())) ||
        !actor || !actor->isString() || !action || !action->isString() ||
        !isProductionJournalAction(action->asString()) || !subject || !subject->isString() || subject->asString().empty() ||
        !occurred || !occurred->isString() || !isProductionUtcTimestamp(occurred->asString()) ||
        std::none_of(snapshot.value().operators.begin(), snapshot.value().operators.end(),
            [&](const auto& value) { return value.operatorId == actor->asString(); }))
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate origin journal is not bound to its generation");
    if (generation == 1U && (action->asString() != "create" || subject->asString() != project.projectId || !snapshot.value().takes.empty()))
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate origin requires the original empty producer generation");
    const auto ancestry = history_internal::verifyAncestry(workspace, journal.value(), project.projectId, generation);
    if (!ancestry) return core::Result<Output>{ancestry.error()};
    if ((!ancestry.value().recorded && generation != expectedGeneration) ||
        (ancestry.value().recorded && ancestry.value().parentGeneration != expectedGeneration - 1U))
      return core::failure<Output>(core::ErrorCode::Conflict,
          "Candidate original import attribution requires contiguous verified generation history or certified aborted writes",
          "Missing generation " + std::to_string(expectedGeneration));
    expectedGeneration = generation + 1U;
    for (const auto& take : snapshot.value().takes) {
      const auto expected = requested.find(take.takeId);
      if (expected == requested.end() || origins.contains(take.takeId)) continue;
      const bool batch = action->asString() == "import-generated-batch";
      const bool direct = (action->asString() == "import" || action->asString() == "import-procedural" || action->asString() == "retake") &&
          textEquals("subjectId", take.takeId);
      if (take.rawAssetSha256 != expected->second || (!direct && !batch))
        return core::failure<Output>(core::ErrorCode::Conflict, "Candidate take has no verified original import event", take.takeId);
      const auto captured = std::find_if(project.sourceBindings.begin(), project.sourceBindings.end(),
          [&](const auto& source) { return source.takeId == take.takeId; });
      if (captured != project.sourceBindings.end()) {
        const auto historical = std::find_if(snapshot.value().sourceBindings.begin(), snapshot.value().sourceBindings.end(),
            [&](const auto& source) { return source.id == take.sourceBindingId; });
        if (historical == snapshot.value().sourceBindings.end() || *historical != *captured ||
            captured->importerId != actor->asString() || captured->importedAtUtc != occurred->asString())
          return core::failure<Output>(core::ErrorCode::Conflict, "Captured source attribution differs from its original import journal", take.takeId);
      }
      origins.emplace(take.takeId, OriginAttribution{actor->asString(), generation, core::sha256Hex(journalBytes.value())});
    }
    if (origins.size() == requested.size()) return origins;
  }
  return core::failure<Output>(core::ErrorCode::Conflict, "Candidate original import attribution is incomplete");
}

core::Result<void> validateIndependentReviewer(const VoicebankProductionProject& project,
    const SampleCandidateUnitBinding& binding, std::string_view reviewerId, const OriginAttribution& origin) {
  const auto take = selectedTake(project, binding);
  if (!take) return core::Result<void>{take.error()};
  const auto reviewer = std::find_if(project.operators.begin(), project.operators.end(),
      [&](const auto& value) { return value.operatorId == reviewerId && value.role == "REVIEWER"; });
  if (reviewer == project.operators.end())
    return core::failure(core::ErrorCode::Conflict, "Candidate reviewer is not a registered reviewer", binding.takeId);
  if (origin.actor == reviewerId)
    return core::failure(core::ErrorCode::Conflict, "Candidate raw importer cannot independently review the same take", binding.takeId);
  for (const auto& revision : project.derivedRevisions)
    if (revision.operatorId == reviewerId && std::find(take.value()->derivedRevisionIds.begin(),
        take.value()->derivedRevisionIds.end(), revision.revisionId) != take.value()->derivedRevisionIds.end())
      return core::failure(core::ErrorCode::Conflict, "Candidate producer cannot review their own processing", binding.takeId);
  for (const auto& revision : project.metadataRevisions)
    if (revision.takeId == binding.takeId && !reviewMetadata(revision.kind) && revision.operatorId == reviewerId)
      return core::failure(core::ErrorCode::Conflict, "Candidate annotator cannot review their own annotations", binding.takeId);
  return core::success();
}

core::Result<void> validateReviewedBinding(const VoicebankProductionProject& project,
    const voicebank::Manifest& manifest, const SampleCandidateUnitBinding& binding) {
  const auto take = selectedTake(project, binding);
  if (!take) return core::Result<void>{take.error()};
  const auto* unit = manifest.findUnit(binding.unitId);
  if (project.schemaVersion >= kProductionStyleSchemaVersion &&
      (project.language != (manifest.language == domain::Language::Japanese ? "ja" : manifest.language == domain::Language::English ? "en" : "ko") ||
       !unit || unit->style != take.value()->style))
    return core::failure(core::ErrorCode::Conflict, "Candidate language or style differs from its producer take", binding.unitId);
  if (!unit || !unit->enabled || unit->rootMidi != take.value()->pitchLayer ||
      coverageKey(*unit) != take.value()->coverageKey ||
      unit->audioPath.generic_string() != "audio/" + binding.audioSha256 + ".wav")
    return core::failure(core::ErrorCode::Conflict, "Candidate unit differs from its take, pitch, or content-addressed audio", binding.unitId);
  const auto assignment = std::find_if(project.unitAssignments.begin(), project.unitAssignments.end(),
      [&](const auto& value) { return value.takeId == binding.takeId; });
  if (assignment == project.unitAssignments.end() || assignment->state != UnitQueueState::Approved ||
      take.value()->state != UnitQueueState::Approved || !assignment->markerReviewed || !assignment->pitchReviewed)
    return core::failure(core::ErrorCode::Conflict, "Candidate take lacks completed marker and pitch review", binding.takeId);
  const auto review = std::find_if(project.reviews.begin(), project.reviews.end(),
      [&](const auto& value) { return value.reviewId == binding.reviewId; });
  const auto metadata = std::find_if(project.metadataRevisions.begin(), project.metadataRevisions.end(),
      [&](const auto& value) { return value.revisionId == binding.reviewMetadataRevisionId; });
  const auto latestReview = std::find_if(project.reviews.rbegin(), project.reviews.rend(),
      [&](const auto& value) { return value.takeId == binding.takeId; });
  const auto latestBinding = std::find_if(project.metadataRevisions.rbegin(), project.metadataRevisions.rend(),
      [&](const auto& value) { return value.takeId == binding.takeId && value.kind == kSampleCandidateReviewKind; });
  if (review == project.reviews.end() || review->takeId != binding.takeId || review->result != "PASS" ||
      latestReview == project.reviews.rend() || latestReview->reviewId != binding.reviewId ||
      metadata == project.metadataRevisions.end() || metadata->takeId != binding.takeId ||
      latestBinding == project.metadataRevisions.rend() || latestBinding->revisionId != binding.reviewMetadataRevisionId ||
      metadata->kind != kSampleCandidateReviewKind || metadata->rawAssetSha256 != take.value()->rawAssetSha256 ||
      metadata->operatorId != review->reviewerId || metadata->performedAtUtc != review->reviewedAtUtc)
    return core::failure(core::ErrorCode::Conflict, "Candidate requires a durable independently attributed review binding", binding.takeId);
  const auto expected = sampleCandidateReviewValues(project, manifest, binding);
  if (!expected) return core::Result<void>{expected.error()};
  if (metadata->values != expected.value())
    return core::failure(core::ErrorCode::Conflict, "Candidate review is stale for its audio, markers, pitch, source, or policy", binding.takeId);
  return core::success();
}

core::Result<void> syncDirectory(const std::filesystem::path& path) {
#if defined(_WIN32)
  static_cast<void>(path);
  return core::success();
#else
  const int descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (descriptor < 0) return core::failure(core::ErrorCode::IoError, "Cannot open candidate directory for sync", path.string());
  const int synced = ::fsync(descriptor);
  ::close(descriptor);
  return synced == 0 ? core::success() : core::failure(core::ErrorCode::IoError, "Cannot sync candidate directory", path.string());
#endif
}

struct NativeDirectoryIdentity final {
  std::filesystem::path path;
#if defined(_WIN32)
  HANDLE handle{INVALID_HANDLE_VALUE};
  std::uint64_t volume{}, file{};
  ~NativeDirectoryIdentity() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
#else
  int descriptor{-1};
  dev_t device{};
  ino_t inode{};
  ~NativeDirectoryIdentity() { if (descriptor >= 0) ::close(descriptor); }
#endif
};

core::Result<DirectoryIdentity> captureDirectoryIdentity(const std::filesystem::path& path) {
  auto native = std::make_shared<NativeDirectoryIdentity>();
  native->path = path.lexically_normal();
#if defined(_WIN32)
  native->handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  BY_HANDLE_FILE_INFORMATION info{};
  if (native->handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(native->handle, &info) ||
      (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
      (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    return core::failure<DirectoryIdentity>(core::ErrorCode::Conflict, "Publication directory is missing, symbolic or unavailable", path.string());
  native->volume = info.dwVolumeSerialNumber;
  native->file = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32U) | info.nFileIndexLow;
#else
  native->descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  struct stat info{};
  if (native->descriptor < 0 || ::fstat(native->descriptor, &info) != 0 || !S_ISDIR(info.st_mode))
    return core::failure<DirectoryIdentity>(core::ErrorCode::Conflict, "Publication directory is missing, symbolic or unavailable", path.string());
  native->device = info.st_dev;
  native->inode = info.st_ino;
#endif
  return DirectoryIdentity{std::move(native)};
}

core::Result<void> validateDirectoryIdentity(const std::filesystem::path& path, const DirectoryIdentity& expected) {
  if (!expected.native || path.lexically_normal() != expected.native->path)
    return core::failure(core::ErrorCode::Conflict, "Publication directory does not match its captured pathname", path.string());
  const auto current = captureDirectoryIdentity(path);
  if (!current) return core::Result<void>{current.error()};
#if defined(_WIN32)
  const bool same = current.value().native->volume == expected.native->volume && current.value().native->file == expected.native->file;
#else
  const bool same = current.value().native->device == expected.native->device && current.value().native->inode == expected.native->inode;
#endif
  return same ? core::success() : core::failure(core::ErrorCode::Conflict,
      "Publication directory was replaced; replacement-owned data is preserved", path.string());
}

void cleanupOwnedDirectory(const std::filesystem::path& stage,
    const DirectoryIdentity& parentIdentity, const DirectoryIdentity& stageIdentity) noexcept {
  try {
    // In particular, never remove a replacement real directory or a symlink
    // occupying the old stage name after a failed identity check.
    if (!validateDirectoryIdentity(stage.parent_path(), parentIdentity) ||
        !validateDirectoryIdentity(stage, stageIdentity)) return;
    std::error_code error;
    std::filesystem::remove_all(stage, error);
  } catch (...) {
    // Conservatively retain provisional files when cleanup cannot prove ownership.
  }
}

core::Result<void> publishNewDirectory(const std::filesystem::path& stage, const std::filesystem::path& destination,
    const DirectoryIdentity& parentIdentity, const DirectoryIdentity& stageIdentity) {
  if (stage.parent_path() != destination.parent_path())
    return core::failure(core::ErrorCode::Conflict, "Directory publication must stay within the captured parent");
  auto checked = validateDirectoryIdentity(stage.parent_path(), parentIdentity);
  if (!checked) return checked;
  checked = validateDirectoryIdentity(stage, stageIdentity);
  if (!checked) return checked;
#if defined(_WIN32)
  if (!MoveFileExW(stage.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
    return core::failure(core::ErrorCode::IoError, "Cannot publish new candidate directory", std::to_string(GetLastError()));
#elif defined(__APPLE__)
  if (::renameatx_np(parentIdentity.native->descriptor, stage.filename().c_str(),
      parentIdentity.native->descriptor, destination.filename().c_str(), RENAME_EXCL) != 0)
    return core::failure(core::ErrorCode::Conflict, "Cannot publish candidate without replacing an existing destination", destination.string());
#elif defined(__linux__)
  if (::syscall(SYS_renameat2, parentIdentity.native->descriptor, stage.filename().c_str(),
      parentIdentity.native->descriptor, destination.filename().c_str(), 1U) != 0)
    return core::failure(core::ErrorCode::Conflict, "Cannot publish candidate without replacing an existing destination", destination.string());
#else
  static_cast<void>(stage);
  static_cast<void>(destination);
  return core::failure(core::ErrorCode::Unsupported, "Atomic create-new candidate directories are unsupported on this platform");
#endif
  return core::success();
}

struct StagingCleanup final {
  std::filesystem::path path;
  DirectoryIdentity parentIdentity, stageIdentity;
  ~StagingCleanup() { cleanupOwnedDirectory(path, parentIdentity, stageIdentity); }
};
}  // namespace candidate_publication_internal
using namespace candidate_publication_internal;

core::Result<std::map<std::string, std::string, std::less<>>> sampleCandidateReviewValues(
    const VoicebankProductionProject& project, const voicebank::Manifest& manifest,
    const SampleCandidateUnitBinding& binding) {
  using Values = std::map<std::string, std::string, std::less<>>;
  const auto valid = validateProductionProject(project);
  if (!valid) return core::Result<Values>{valid.error()};
  const auto* unit = manifest.findUnit(binding.unitId);
  if (binding.reviewId.empty() || binding.reviewMetadataRevisionId.empty() || !unit)
    return core::failure<Values>(core::ErrorCode::InvalidArgument, "Candidate review identity is incomplete");
  auto reviewedManifest = manifest;
  reviewedManifest.units = {*unit};
  reviewedManifest.styles = {unit->style};
  const auto encoded = boundedManifest(reviewedManifest);
  if (!encoded) return core::Result<Values>{encoded.error()};
  const auto take = selectedTake(project, binding);
  if (!take) return core::Result<Values>{take.error()};
  return Values{{"unitId", binding.unitId}, {"reviewId", binding.reviewId},
      {"audioSha256", binding.audioSha256}, {"unitManifestSha256", core::sha256Hex(encoded.value())},
      {"reviewBasisSha256", reviewBasis(project, binding.takeId)}};
}

core::Result<PublishedSampleCandidate> publishSampleCandidate(
    const std::filesystem::path& repositoryRoot, const VoicebankProductionProject& project,
    const SampleCandidateRequest& request, const std::filesystem::path& destination,
    const CandidatePublicationOptions& options, std::stop_token stop) {
  using Output = PublishedSampleCandidate;
  auto checkpoint = cancelled(stop);
  if (!checkpoint) return core::Result<Output>{checkpoint.error()};
  if (!destination.is_absolute() || destination != destination.lexically_normal() ||
      destination.filename().empty() || destination.filename() == "." || destination.filename() == ".." ||
      destination.filename().string().front() == '.' ||
      destination == destination.root_path() || options.maximumAudioBytes == 0U ||
      options.maximumAudioBytes > kMaximumCandidateAudioBytes)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Candidate destination or resource budget is invalid");
  const auto workspace = realDirectory(repositoryRoot);
  const auto parent = realDirectory(destination.parent_path());
  if (!workspace) return core::Result<Output>{workspace.error()};
  if (!parent) return core::Result<Output>{parent.error()};
  const auto finalPath = parent.value() / destination.filename();
  const auto relativeToWorkspace = finalPath.lexically_relative(workspace.value());
  if (!relativeToWorkspace.empty() && *relativeToWorkspace.begin() != "..")
    return core::failure<Output>(core::ErrorCode::Conflict, "Candidates must publish outside the producer workspace");
  core::ExclusiveFileLock workspaceLock;
  checkpoint = workspaceLock.acquire(workspace.value() / ".writer.lock");
  if (!checkpoint) return core::Result<Output>{checkpoint.error()};
  ProductionProjectRepository repository{workspace.value()};
  checkpoint = repository.verify(project);
  if (!checkpoint) return core::Result<Output>{checkpoint.error()};
  const auto sourceJson = encodeProductionProject(project);
  if (request.expectedGeneration != project.lastDurableGeneration ||
      !isDigest(request.expectedProjectSha256) || core::sha256Hex(sourceJson) != request.expectedProjectSha256)
    return core::failure<Output>(core::ErrorCode::Conflict, "Candidate request does not match the current durable producer generation");
  const auto manifestJson = boundedManifest(request.manifest);
  if (!manifestJson) return core::Result<Output>{manifestJson.error()};
  if (request.manifest.styles.size() != 1U)
    return core::failure<Output>(core::ErrorCode::Unsupported, "Multi-style candidate publication requires style-owned producer assignments");
  if (request.units.empty() || request.units.size() > kMaximumUnits ||
      request.units.size() != request.manifest.units.size() || request.units.size() != project.unitAssignments.size())
    return core::failure<Output>(core::ErrorCode::Conflict, "Candidate must cover every producer assignment and manifest unit exactly once");
  const auto origins = collectOriginAttribution(workspace.value(), project, request.units, stop);
  if (!origins) return core::Result<Output>{origins.error()};
  std::set<std::string> units, takes;
  std::map<std::string, const AssetRecord*> audioAssets;
  for (const auto& binding : request.units) {
    if (!units.insert(binding.unitId).second || !takes.insert(binding.takeId).second)
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate unit or take binding is duplicated");
    checkpoint = requireTakeSourceQualification(project, binding.takeId);
    if (!checkpoint) return core::Result<Output>{checkpoint.error()};
    checkpoint = validateReviewedBinding(project, request.manifest, binding);
    if (!checkpoint) return core::Result<Output>{checkpoint.error()};
    const auto reviewer = std::find_if(project.reviews.begin(), project.reviews.end(),
        [&](const auto& value) { return value.reviewId == binding.reviewId; });
    if (reviewer == project.reviews.end()) return core::failure<Output>(core::ErrorCode::Conflict, "Candidate review is missing");
    checkpoint = validateIndependentReviewer(project, binding, reviewer->reviewerId, origins.value().at(binding.takeId));
    if (!checkpoint) return core::Result<Output>{checkpoint.error()};
    const auto asset = std::find_if(project.assets.begin(), project.assets.end(), [&](const auto& value) { return value.sha256 == binding.audioSha256; });
    if (asset == project.assets.end()) return core::failure<Output>(core::ErrorCode::NotFound, "Candidate immutable audio asset is missing");
    audioAssets.emplace(binding.audioSha256, &*asset);
  }
  core::ExclusiveFileLock destinationLock;
  checkpoint = destinationLock.acquire(parent.value() / ".seam-candidate-writer.lock");
  if (!checkpoint) return core::Result<Output>{checkpoint.error()};
  const auto parentIdentity = captureDirectoryIdentity(parent.value());
  if (!parentIdentity) return core::Result<Output>{parentIdentity.error()};
  checkpoint = absentDestination(finalPath);
  if (!checkpoint) return core::Result<Output>{checkpoint.error()};
#if defined(_WIN32)
  const auto processId = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
  const auto processId = static_cast<std::uint64_t>(::getpid());
#endif
  const auto stage = parent.value() / (".seam-candidate-" + std::to_string(processId) + "-" + std::to_string(stagingSequence.fetch_add(1U)));
  std::error_code error;
  if (!std::filesystem::create_directory(stage, error) || error)
    return core::failure<Output>(core::ErrorCode::Conflict, "Cannot create private candidate staging directory", stage.string());
  const auto stageIdentity = captureDirectoryIdentity(stage);
  if (!stageIdentity) return core::Result<Output>{stageIdentity.error()};
  StagingCleanup cleanup{stage, parentIdentity.value(), stageIdentity.value()};
  std::filesystem::permissions(stage, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, error);
  if (error) return core::failure<Output>(core::ErrorCode::IoError, "Cannot restrict candidate staging directory", error.message());
  std::uint64_t totalBytes = 0U;
  for (const auto& [digest, asset] : audioAssets) {
    checkpoint = cancelled(stop);
    if (!checkpoint) return core::Result<Output>{checkpoint.error()};
    const auto bytes = core::readFileBytesLimited(repository.assetPath(*asset), options.maximumAudioBytes - totalBytes);
    if (!bytes) return core::Result<Output>{bytes.error()};
    if (bytes.value().size() != asset->byteSize || core::sha256Hex(bytes.value()) != digest)
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate audio changed while staging", digest);
    totalBytes += static_cast<std::uint64_t>(bytes.value().size());
    const auto output = stage / "audio" / (digest + ".wav");
    checkpoint = core::durableAtomicWriteNew(output, bytes.value());
    if (!checkpoint) return core::Result<Output>{checkpoint.error()};
    const auto decoded = voicebank::readWav(output);
    if (!decoded) return core::Result<Output>{decoded.error()};
    if (decoded.value().channels != 1U || decoded.value().sampleRate != request.manifest.expectedSampleRate ||
        !std::all_of(decoded.value().interleaved.begin(), decoded.value().interleaved.end(), [](float value) { return std::isfinite(value); }) ||
        voicebank::analyzeAudio(decoded.value().interleaved).rms <= 1e-4)
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate audio must be finite non-silent mono at its declared rate", digest);
  }
  if (options.faultInjector) {
    checkpoint = options.faultInjector(CandidatePublicationStage::AudioStaged);
    if (!checkpoint) return core::Result<Output>{checkpoint.error()};
  }
  checkpoint = core::durableAtomicWriteTextNew(stage / "manifest.json", manifestJson.value());
  if (!checkpoint) return core::Result<Output>{checkpoint.error()};
  const auto validation = voicebank::BankValidator{}.validate(request.manifest, stage);
  if (!validation.ok()) return core::failure<Output>(core::ErrorCode::Conflict, "Candidate manifest/audio validation failed");
  const auto contentHash = voicebank::computeVoicebankContentHash(request.manifest, stage);
  if (!contentHash) return core::Result<Output>{contentHash.error()};
  const auto firstTake = selectedTake(project, request.units.front());
  if (!firstTake) return core::Result<Output>{firstTake.error()};
  const auto firstSource = std::find_if(project.sourceBindings.begin(), project.sourceBindings.end(),
      [&](const auto& value) { return value.id == firstTake.value()->sourceBindingId; });
  if (firstSource == project.sourceBindings.end())
    return core::failure<Output>(core::ErrorCode::Conflict, "Candidate has no captured source evidence");
  const auto primaryLicenseSha256 = firstSource->strategy.licenseSha256;
  const auto license = core::readFileBytesLimited(workspace.value() / firstSource->licenseSnapshotPath, 4ULL * 1024ULL * 1024ULL);
  if (!license) return core::Result<Output>{license.error()};
  if (core::sha256Hex(license.value()) != primaryLicenseSha256)
    return core::failure<Output>(core::ErrorCode::Conflict, "Candidate source evidence changed while staging");
  checkpoint = core::durableAtomicWriteNew(stage / "source-license.txt", license.value());
  if (!checkpoint) return core::Result<Output>{checkpoint.error()};
  checkpoint = core::durableAtomicWriteTextNew(stage / "provenance" / "production.json", sourceJson);
  if (!checkpoint) return core::Result<Output>{checkpoint.error()};
  std::map<std::string, std::string> sourceEvidenceHashes;
  std::uint64_t sourceEvidenceBytes = 0U;
  for (const auto& binding : project.sourceBindings) {
    const auto relative = "provenance/" + binding.licenseSnapshotPath;
    if (sourceEvidenceHashes.contains(relative)) continue;
    const auto bytes = core::readFileBytesLimited(workspace.value() / binding.licenseSnapshotPath,
        std::min<std::uint64_t>(4ULL * 1024ULL * 1024ULL, 64ULL * 1024ULL * 1024ULL - sourceEvidenceBytes));
    if (!bytes) return core::Result<Output>{bytes.error()};
    sourceEvidenceBytes += static_cast<std::uint64_t>(bytes.value().size());
    if (core::sha256Hex(bytes.value()) != binding.strategy.licenseSha256)
      return core::failure<Output>(core::ErrorCode::Conflict, "Captured source evidence changed while publishing", binding.id);
    checkpoint = core::durableAtomicWriteNew(stage / relative, bytes.value());
    if (!checkpoint) return core::Result<Output>{checkpoint.error()};
    sourceEvidenceHashes.emplace(relative, binding.strategy.licenseSha256);
  }
  // Retain the exact history (including certified journal-only attempts) that
  // proved original attribution. A hole alone is never an abort certificate.
  // Keeping only the import journal digest would prevent an offline consumer
  // from distinguishing an original import from a later surviving batch.
  for (const auto& assessment : project.sourceQualityAssessments) {
    const auto relative = "provenance/source-evidence/" + assessment.evidenceSha256 + ".quality.txt";
    if (sourceEvidenceHashes.contains(relative)) continue;
    const auto bytes = core::readFileBytesLimited(workspace.value()/"source-evidence"/(assessment.evidenceSha256+".quality.txt"),
        std::min<std::uint64_t>(4ULL*1024ULL*1024ULL,64ULL*1024ULL*1024ULL-sourceEvidenceBytes));
    if (!bytes) return core::Result<Output>{bytes.error()};
    sourceEvidenceBytes += bytes.value().size();
    if (core::sha256Hex(bytes.value()) != assessment.evidenceSha256)
      return core::failure<Output>(core::ErrorCode::Conflict,"Source quality evidence changed during candidate publication");
    checkpoint = core::durableAtomicWriteNew(stage/relative,bytes.value());
    if (!checkpoint) return core::Result<Output>{checkpoint.error()};
    sourceEvidenceHashes.emplace(relative,assessment.evidenceSha256);
  }
  std::uint64_t lastOriginGeneration = project.sourceQualityAssessments.empty() ? 0U : project.lastDurableGeneration;
  for (const auto& [takeId, origin] : origins.value()) {
    static_cast<void>(takeId);
    lastOriginGeneration = std::max(lastOriginGeneration, origin.generation);
  }
  if (lastOriginGeneration == 0U || lastOriginGeneration > 65536U)
    return core::failure<Output>(core::ErrorCode::Unsupported, "Candidate origin history exceeds its retained record limit");
  J::Array originHistory;
  std::uint64_t historyBytes = 0U;
  std::map<std::string, std::string> historyHashes;
  std::map<std::uint64_t, history_internal::AbortedGeneration> abortedHistory;
  for (std::uint64_t generation = 1U; generation <= lastOriginGeneration; ++generation) {
    checkpoint = cancelled(stop);
    if (!checkpoint) return core::Result<Output>{checkpoint.error()};
    const auto filename = history_internal::generationFilename(generation);
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(workspace.value() / "generations" / filename, statusError);
    if (statusError == std::errc::no_such_file_or_directory ||
        (!statusError && status.type() == std::filesystem::file_type::not_found)) continue;
    if (statusError || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate history snapshot is unsafe");
    const auto journalBytes = core::readTextFileLimited(workspace.value() / "journal" / filename, 1024U * 1024U);
    if (!journalBytes) return core::Result<Output>{journalBytes.error()};
    const auto journal = formats::parseJson(journalBytes.value());
    if (!journal) return core::Result<Output>{journal.error()};
    const auto ancestry = history_internal::verifyAncestry(workspace.value(), journal.value(), project.projectId, generation);
    if (!ancestry) return core::Result<Output>{ancestry.error()};
    for (const auto& aborted : ancestry.value().aborted)
      if (!abortedHistory.emplace(aborted.generation, aborted).second)
        return core::failure<Output>(core::ErrorCode::Conflict, "Candidate history repeats aborted-write evidence");
  }
  for (std::uint64_t generation = 1U; generation <= lastOriginGeneration; ++generation) {
    checkpoint = cancelled(stop);
    if (!checkpoint) return core::Result<Output>{checkpoint.error()};
    auto filename = std::to_string(generation);
    filename.insert(0U, 20U - filename.size(), '0');
    filename += ".json";
    if (const auto aborted = abortedHistory.find(generation); aborted != abortedHistory.end()) {
      const auto journalBytes = core::readFileBytesLimited(workspace.value() / "journal" / filename,
          std::min<std::uint64_t>(1024ULL * 1024ULL, 256ULL * 1024ULL * 1024ULL - historyBytes));
      if (!journalBytes) return core::Result<Output>{journalBytes.error()};
      historyBytes += static_cast<std::uint64_t>(journalBytes.value().size());
      const auto journalHash = core::sha256Hex(journalBytes.value());
      if (journalHash != aborted->second.journalSha256 || journalBytes.value().size() != aborted->second.journalBytes)
        return core::failure<Output>(core::ErrorCode::Conflict, "Aborted-write evidence changed while being retained");
      const auto journalPath = "provenance/history/journal/" + filename;
      checkpoint = core::durableAtomicWriteNew(stage / journalPath, journalBytes.value());
      if (!checkpoint) return core::Result<Output>{checkpoint.error()};
      historyHashes.emplace(journalPath, journalHash);
      originHistory.emplace_back(J::Object{{"generation", static_cast<std::int64_t>(generation)},
          {"entryType", "aborted-write"}, {"journalPath", journalPath}, {"journalSha256", journalHash}});
      continue;
    }
    const auto generationBytes = core::readFileBytesLimited(workspace.value() / "generations" / filename,
        std::min<std::uint64_t>(64ULL * 1024ULL * 1024ULL, 256ULL * 1024ULL * 1024ULL - historyBytes));
    if (!generationBytes) return core::Result<Output>{generationBytes.error()};
    historyBytes += static_cast<std::uint64_t>(generationBytes.value().size());
    const auto journalBytes = core::readFileBytesLimited(workspace.value() / "journal" / filename,
        std::min<std::uint64_t>(1024ULL * 1024ULL, 256ULL * 1024ULL * 1024ULL - historyBytes));
    if (!journalBytes) return core::Result<Output>{journalBytes.error()};
    historyBytes += static_cast<std::uint64_t>(journalBytes.value().size());
    const auto generationHash = core::sha256Hex(generationBytes.value());
    const auto journalHash = core::sha256Hex(journalBytes.value());
    const auto journal = formats::parseJson(std::string_view{reinterpret_cast<const char*>(journalBytes.value().data()), journalBytes.value().size()});
    if (!journal || !journal.value().isObject() || !journal.value().find("projectSha256") ||
        !journal.value().find("projectSha256")->isString() || journal.value().find("projectSha256")->asString() != generationHash)
      return core::failure<Output>(core::ErrorCode::Conflict, "Origin history changed while being retained");
    for (const auto& [takeId, origin] : origins.value()) {
      static_cast<void>(takeId);
      if (origin.generation == generation && origin.journalSha256 != journalHash)
        return core::failure<Output>(core::ErrorCode::Conflict, "Original import journal changed while being retained");
    }
    const auto generationPath = "provenance/history/generations/" + filename;
    const auto journalPath = "provenance/history/journal/" + filename;
    checkpoint = core::durableAtomicWriteNew(stage / generationPath, generationBytes.value());
    if (!checkpoint) return core::Result<Output>{checkpoint.error()};
    checkpoint = core::durableAtomicWriteNew(stage / journalPath, journalBytes.value());
    if (!checkpoint) return core::Result<Output>{checkpoint.error()};
    historyHashes.emplace(generationPath, generationHash);
    historyHashes.emplace(journalPath, journalHash);
    originHistory.emplace_back(J::Object{{"generation", static_cast<std::int64_t>(generation)},
        {"generationPath", generationPath}, {"generationSha256", generationHash},
        {"journalPath", journalPath}, {"journalSha256", journalHash}});
  }
  const auto retainedOrigins = collectOriginAttribution(stage / "provenance" / "history", project, request.units, stop);
  if (!retainedOrigins) return core::Result<Output>{retainedOrigins.error()};
  for (const auto& [takeId, original] : origins.value()) {
    const auto retained = retainedOrigins.value().find(takeId);
    if (retained == retainedOrigins.value().end() || retained->second.actor != original.actor ||
        retained->second.generation != original.generation || retained->second.journalSha256 != original.journalSha256)
      return core::failure<Output>(core::ErrorCode::Conflict, "Retained origin history does not reproduce original attribution", takeId);
  }
  J::Array bindings;
  for (const auto& binding : request.units) bindings.emplace_back(J::Object{
      {"unitId", binding.unitId}, {"takeId", binding.takeId}, {"audioSha256", binding.audioSha256},
      {"reviewId", binding.reviewId}, {"reviewMetadataRevisionId", binding.reviewMetadataRevisionId},
      {"originOperatorId", origins.value().at(binding.takeId).actor},
      {"originGeneration", static_cast<std::int64_t>(origins.value().at(binding.takeId).generation)},
      {"originJournalSha256", origins.value().at(binding.takeId).journalSha256}});
  const auto descriptor = formats::stringifyJson(J{J::Object{
      {"format", "com.project-seam.resource-candidate"}, {"schemaVersion", std::int64_t{1}},
      {"resourceKind", "sample"}, {"status", "REVIEWED_CANDIDATE"}, {"releaseEligible", false}, {"evidenceScope", "engineering"},
      {"sourceProjectSha256", request.expectedProjectSha256},
      {"sourceGeneration", static_cast<std::int64_t>(project.lastDurableGeneration)},
      {"inventorySha256", project.inventorySha256}, {"licenseSha256", primaryLicenseSha256},
      {"manifestSha256", core::sha256Hex(manifestJson.value())}, {"contentSha256", contentHash.value()},
      {"originHistory", std::move(originHistory)},
      {"unitBindings", std::move(bindings)}}}, true) + "\n";
  checkpoint = core::durableAtomicWriteTextNew(stage / "candidate.json", descriptor);
  if (!checkpoint) return core::Result<Output>{checkpoint.error()};
  for (const auto& directory : {stage / "audio", stage / "provenance" / "source-evidence",
      stage / "provenance" / "history" / "generations", stage / "provenance" / "history" / "journal",
      stage / "provenance" / "history", stage / "provenance", stage}) {
    checkpoint = syncDirectory(directory);
    if (!checkpoint) return core::Result<Output>{checkpoint.error()};
  }
  if (options.faultInjector) {
    checkpoint = options.faultInjector(CandidatePublicationStage::BeforeCommit);
    if (!checkpoint) return core::Result<Output>{checkpoint.error()};
  }
  checkpoint = cancelled(stop);
  if (!checkpoint) return core::Result<Output>{checkpoint.error()};
  checkpoint = validateDirectoryIdentity(parent.value(), parentIdentity.value());
  if (!checkpoint) return core::Result<Output>{checkpoint.error()};
  checkpoint = validateDirectoryIdentity(stage, stageIdentity.value());
  if (!checkpoint) return core::Result<Output>{checkpoint.error()};
  checkpoint = repository.verify(project);
  if (!checkpoint) return core::Result<Output>{checkpoint.error()};
  const auto finalContent = voicebank::computeVoicebankContentHash(request.manifest, stage);
  if (!finalContent || finalContent.value() != contentHash.value())
    return core::failure<Output>(core::ErrorCode::Conflict, "Candidate staged content changed before publication");
  const auto reopened = voicebank::ManifestJsonCodec{}.load(stage / "manifest.json");
  if (!reopened || reopened.value() != request.manifest)
    return core::failure<Output>(core::ErrorCode::Conflict, "Candidate staged manifest does not reopen with its reviewed values");
  for (const auto& [relative, expectedHash] : std::vector<std::pair<std::string, std::string>>{
      {"manifest.json", core::sha256Hex(manifestJson.value())},
      {"candidate.json", core::sha256Hex(descriptor)},
      {"source-license.txt", primaryLicenseSha256},
      {"provenance/production.json", request.expectedProjectSha256}}) {
    const auto actual = core::sha256File(stage / relative);
    if (!actual || actual.value() != expectedHash)
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate staged metadata changed before publication", relative);
  }
  for (const auto& [relative, expectedHash] : sourceEvidenceHashes) {
    const auto actual = core::sha256File(stage / relative, 4ULL * 1024ULL * 1024ULL);
    if (!actual || actual.value() != expectedHash)
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate retained source evidence changed before publication", relative);
  }
  for (const auto& [relative, expectedHash] : historyHashes) {
    const auto actual = core::sha256File(stage / relative, 64ULL * 1024ULL * 1024ULL);
    if (!actual || actual.value() != expectedHash)
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate retained origin history changed before publication", relative);
  }
  checkpoint = cancelled(stop);
  if (!checkpoint) return core::Result<Output>{checkpoint.error()};
  checkpoint = publishNewDirectory(stage, finalPath, parentIdentity.value(), stageIdentity.value());
  if (!checkpoint) return core::Result<Output>{checkpoint.error()};
  Output output{finalPath, core::sha256Hex(manifestJson.value()), contentHash.value(),
      core::sha256Hex(descriptor), project.lastDurableGeneration, false, true, {}};
  checkpoint = options.faultInjector ? options.faultInjector(CandidatePublicationStage::AfterCommitBeforeParentSync) : core::success();
  if (checkpoint) checkpoint = syncDirectory(parent.value());
  if (!checkpoint) {
    output.durabilityConfirmed = false;
    output.diagnostic = "Candidate is committed but parent-directory durability is uncertain. Inspect the returned destination and exact manifest/content/candidate hashes before retrying; do not regenerate or overwrite it. " + checkpoint.error().message;
  }
  return output;
}

}  // namespace seam::voicebank_production
