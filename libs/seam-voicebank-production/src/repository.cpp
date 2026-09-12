#include "seam/voicebank_production/repository.hpp"
#include "repository_source_internal.hpp"
#include "repository_history_internal.hpp"
#include "seam/voicebank_production/source_assessment.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank_production/project_codec.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <system_error>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace seam::voicebank_production {
namespace {

// Keep the lock file: unlinking it could allow another writer to lock a different
// inode while an existing writer still holds this one. Closing releases the OS lock.
class WorkspaceWriter final {
public:
  WorkspaceWriter() = default;
  WorkspaceWriter(const WorkspaceWriter&) = delete;
  WorkspaceWriter& operator=(const WorkspaceWriter&) = delete;
  ~WorkspaceWriter() {
#if defined(_WIN32)
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
    if (descriptor_ >= 0) close(descriptor_);
#endif
  }
  core::Result<void> acquire(const std::filesystem::path& path) {
#if defined(_WIN32)
    handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    if (handle_ == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle_, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
      return core::failure(core::ErrorCode::Conflict, "Production writer lock is busy or unsafe");
#else
    descriptor_ = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    struct stat info{};
    if (descriptor_ < 0 || fstat(descriptor_, &info) != 0 || !S_ISREG(info.st_mode) ||
        flock(descriptor_, LOCK_EX | LOCK_NB) != 0)
      return core::failure(core::ErrorCode::Conflict, "Production writer lock is busy or unsafe");
#endif
    return core::success();
  }
private:
#if defined(_WIN32)
  HANDLE handle_{INVALID_HANDLE_VALUE};
#else
  int descriptor_{-1};
#endif
};

std::string generationName(std::uint64_t generation) {
  std::ostringstream stream;
  stream << std::setw(20) << std::setfill('0') << generation << ".json";
  return stream.str();
}

core::Result<void> writeImmutableText(
    const std::filesystem::path& path, const std::string& text) {
  std::error_code error;
  if (std::filesystem::exists(path, error)) {
    if (error) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to inspect immutable record", error.message());
    }
    auto existing = core::readTextFileLimited(path, 64U * 1024U * 1024U);
    if (!existing) return core::Result<void>{existing.error()};
    if (existing.value() == text) return core::success();
    return core::failure(core::ErrorCode::Conflict,
                         "Immutable record already exists with different content",
                         path.string());
  }
  return core::durableAtomicWriteText(path, text);
}

std::uint64_t highestGeneration(const std::filesystem::path& directory) {
  std::error_code error;
  std::uint64_t highest = 0U;
  for (std::filesystem::directory_iterator iterator{directory, error}, end;
       !error && iterator != end; iterator.increment(error)) {
    if (!iterator->is_regular_file(error) || error) continue;
    const auto stem = iterator->path().stem().string();
    std::uint64_t value = 0U;
    const auto parsed = std::from_chars(stem.data(), stem.data() + stem.size(), value);
    if (parsed.ec == std::errc{} && parsed.ptr == stem.data() + stem.size()) {
      highest = std::max(highest, value);
    }
  }
  return highest;
}

core::Result<void> verifyLicense(const std::filesystem::path& root, const VoicebankProductionProject& project) {
  if (project.schemaVersion >= 2) return source_internal::verifySnapshots(root, project);
  auto digest = core::sha256File(project.licenseLocator);
  if (!digest) return core::Result<void>{digest.error()};
  if (digest.value() != project.licenseSha256) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Production source-license digest changed");
  }
  return core::success();
}

core::Result<void> prepareWorkspace(
    const std::filesystem::path& root, bool requireEmpty) {
  if (root.empty() || root == root.root_path()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Production workspace root is invalid");
  }
  std::error_code error;
  const auto status = std::filesystem::symlink_status(root, error);
  if (error == std::errc::no_such_file_or_directory ||
      status.type() == std::filesystem::file_type::not_found) {
    if (requireEmpty) return core::success();
    error.clear();
    std::filesystem::create_directories(root, error);
    if (error) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to create production workspace",
                           error.message());
    }
  } else if (error || std::filesystem::is_symlink(status) ||
             !std::filesystem::is_directory(status)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Production workspace root must be a real directory",
                         root.string());
  } else if (requireEmpty) {
    const std::filesystem::directory_iterator iterator{root, error};
    if (error) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to inspect production workspace",
                           error.message());
    }
    if (iterator != std::filesystem::directory_iterator{}) {
      return core::failure(core::ErrorCode::Conflict,
                           "Production workspace is already initialized",
                           root.string());
    }
    return core::success();
  }
  constexpr std::array<const char*, 5U> directories{
      "assets", "generations", "journal", "staging", "source-evidence"};
  for (const auto* name : directories) {
    const auto directory = root / name;
    std::filesystem::create_directories(directory, error);
    const auto directoryStatus =
        std::filesystem::symlink_status(directory, error);
    if (error || std::filesystem::is_symlink(directoryStatus) ||
        !std::filesystem::is_directory(directoryStatus)) {
      return core::failure(core::ErrorCode::Conflict,
                           "Production workspace directory is unsafe",
                           directory.string());
    }
  }
  return core::success();
}

}

ProductionProjectRepository::ProductionProjectRepository(
    std::filesystem::path root)
    : root_(std::move(root)), assetStore_(root_ / "assets") {}

core::Result<void> ProductionProjectRepository::initialize(
    VoicebankProductionProject& project, const ProductionJournalEvent& event) {
  auto prepared = prepareWorkspace(root_, true);
  if (!prepared) return prepared;
  project.lastDurableGeneration = 0U;
  return save(project, event);
}

core::Result<void> ProductionProjectRepository::save(
    VoicebankProductionProject& project, const ProductionJournalEvent& event,
    std::stop_token stopToken) {
  if (stopToken.stop_requested()) return core::failure(
      core::ErrorCode::Conflict, "Production save cancelled before commit");
  if (!isProductionJournalAction(event.action) || event.subjectId.empty() ||
      event.operatorId.empty() ||
      !isProductionUtcTimestamp(event.occurredAtUtc) ||
      std::none_of(project.operators.begin(), project.operators.end(),
                   [&event](const OperatorRecord& value) {
                     return value.operatorId == event.operatorId;
                   })) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Production journal event is invalid");
  }
  auto license = verifyLicense(root_, project);
  if (!license) return license;
  auto prepared = prepareWorkspace(root_, false);
  if (!prepared) return prepared;
  WorkspaceWriter writer;
  const auto locked = writer.acquire(root_ / ".writer.lock");
  if (!locked) return locked;
  auto next = project;
  const auto occupied = std::max(highestGeneration(root_ / "generations"),
                                 highestGeneration(root_ / "journal"));
  if (occupied > 0U) {
    const auto current = recover();
    if (!current) return core::Result<void>{current.error()};
    if (current.value().projectId != project.projectId ||
        current.value().lastDurableGeneration != project.lastDurableGeneration) {
      return core::failure(core::ErrorCode::Conflict,
          "Production writer is stale; recover the current generation before saving");
    }
    const auto preserved = source_internal::preserveBindings(current.value(), project);
    if (!preserved) return preserved;
    if (project.sourceQualityAssessments.size() != current.value().sourceQualityAssessments.size()) {
      const auto& assessment = project.sourceQualityAssessments.back();
      const auto reviewer = validateSourceQualityReviewer(current.value(),assessment);
      if (!reviewer) return reviewer;
      const auto material = sourceQualityMaterialIdentity(current.value(),assessment.strategyId);
      const auto strategy = std::find_if(current.value().sourceStrategies.begin(),current.value().sourceStrategies.end(),
          [&](const auto& row) { return row.id == assessment.strategyId; });
      if (project.sourceQualityAssessments.size() != current.value().sourceQualityAssessments.size()+1U ||
          event.action != "source-quality-assessment" || event.subjectId != assessment.id || event.operatorId != assessment.reviewerId ||
          event.occurredAtUtc != assessment.reviewedAtUtc || !material || material.value() != assessment.materialSha256 ||
          strategy == current.value().sourceStrategies.end() || sourceQualityPolicyIdentity(*strategy) != assessment.policySha256)
        return core::failure(core::ErrorCode::Conflict,"Source assessment has no matching current reviewer journal transition");
    } else if (event.action == "source-quality-assessment") {
      return core::failure(core::ErrorCode::Conflict,"Source assessment event must append exactly one new decision");
    }
  } else if (project.lastDurableGeneration != 0U) {
    return core::failure(core::ErrorCode::Conflict, "Production writer has no matching durable base");
  }
  if (occupied == 0U && !project.sourceQualityAssessments.empty())
    return core::failure(core::ErrorCode::Conflict,"A new producer cannot start with preapproved source assessment history");
  if (occupied >= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return core::failure(core::ErrorCode::Conflict, "Production generation counter is exhausted");
  }
  next.lastDurableGeneration =
      std::max(project.lastDurableGeneration, occupied) + 1U;
  auto valid = validateProductionProject(next);
  if (!valid) return valid;
  const auto projectText = encodeProductionProject(next);
  if (projectText.size() > 64U * 1024U * 1024U) return core::failure(core::ErrorCode::InvalidArgument,
      "Production generation exceeds its recoverable serialized byte limit");
  const auto projectDigest = core::sha256Hex(projectText);
  const auto ancestry = history_internal::captureAncestry(root_, project.projectId,
      project.lastDurableGeneration, next.lastDurableGeneration);
  if (!ancestry) return core::Result<void>{ancestry.error()};
  const auto journalText = formats::stringifyJson(
      formats::JsonValue{formats::JsonValue::Object{
          {"format", "com.project-seam.voicebank-production-journal-event"},
          {"schemaVersion", std::int64_t{1}},
          {"generation", static_cast<std::int64_t>(next.lastDurableGeneration)},
          {"projectSha256", projectDigest},
          {"action", event.action},
          {"subjectId", event.subjectId},
          {"operatorId", event.operatorId},
          {"occurredAtUtc", event.occurredAtUtc},
          {"ancestry", ancestry.value()},
      }}, true) + "\n";
  if (journalText.size() > 1024U * 1024U) return core::failure(core::ErrorCode::InvalidArgument,
      "Production journal exceeds its recoverable serialized byte limit");
  // Final cancellation boundary. Once publication begins, finish the durable
  // protocol and report its actual result, even if cancellation arrives later.
  if (stopToken.stop_requested()) return core::failure(
      core::ErrorCode::Conflict, "Production save cancelled before commit");
  auto journalWrite = writeImmutableText(
      journalPath(next.lastDurableGeneration), journalText);
  if (!journalWrite) return journalWrite;
  auto generationWrite = writeImmutableText(
      generationPath(next.lastDurableGeneration), projectText);
  if (!generationWrite) return generationWrite;
  auto pointerWrite = core::durableAtomicWriteText(root_ / "project.json", projectText);
  if (!pointerWrite) return pointerWrite;
  project = std::move(next);
  return core::success();
}

core::Result<void> ProductionProjectRepository::reconcileCurrentPointer(
    std::uint64_t expectedGeneration, std::string_view expectedProjectSha256, std::stop_token stopToken) const {
  if (stopToken.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Pointer reconciliation cancelled");
  if (expectedGeneration == 0U || expectedProjectSha256.size() != 64U)
    return core::failure(core::ErrorCode::InvalidArgument, "Pointer reconciliation requires exact generation and hash");
  WorkspaceWriter writer;
  const auto locked = writer.acquire(root_ / ".writer.lock");
  if (!locked) return locked;
  // Recovery may fall back from a damaged latest generation. Never turn that
  // fallback into an implicit rollback of a newer occupied journal/generation.
  if (std::max(highestGeneration(root_ / "generations"), highestGeneration(root_ / "journal")) != expectedGeneration)
    return core::failure(core::ErrorCode::Conflict, "Pointer reconciliation would replace newer or incomplete work");
  const auto recovered = recover();
  if (!recovered) return core::Result<void>{recovered.error()};
  const auto bytes = encodeProductionProject(recovered.value());
  if (recovered.value().lastDurableGeneration != expectedGeneration || core::sha256Hex(bytes) != expectedProjectSha256)
    return core::failure(core::ErrorCode::Conflict, "Pointer reconciliation state is stale or mismatched");
  if (stopToken.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Pointer reconciliation cancelled before publication");
  return core::durableAtomicWriteText(root_ / "project.json", bytes);
}

core::Result<VoicebankProductionProject> ProductionProjectRepository::recoverGeneration(
    std::uint64_t generation, std::string_view expectedProjectSha256) const {
  const auto invalid = [] { return core::failure<VoicebankProductionProject>(core::ErrorCode::Conflict,
      "Requested historical producer generation is missing, unsafe or hash-mismatched"); };
  if (generation == 0U || expectedProjectSha256.size() != 64U) return invalid();
  std::error_code error;
  const auto status = std::filesystem::symlink_status(generationPath(generation), error);
  if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) return invalid();
  const auto bytes = core::readTextFileLimited(generationPath(generation), 64U * 1024U * 1024U);
  if (!bytes || core::sha256Hex(bytes.value()) != expectedProjectSha256) return invalid();
  const auto project = decodeProductionProject(bytes.value());
  if (!project || project.value().lastDurableGeneration != generation) return invalid();
  const auto verified = verifyGeneration(project.value(), false);
  if (!verified) return core::Result<VoicebankProductionProject>{verified.error()};
  return project.value();
}

core::Result<VoicebankProductionProject>
ProductionProjectRepository::recover() const {
  const auto directory = root_ / "generations";
  std::error_code error;
  if (!std::filesystem::is_directory(directory, error)) {
    return core::failure<VoicebankProductionProject>(
        core::ErrorCode::NotFound, "Production generations are unavailable");
  }
  std::vector<std::uint64_t> generations;
  for (std::filesystem::directory_iterator iterator{directory, error}, end;
       !error && iterator != end; iterator.increment(error)) {
    if (!iterator->is_regular_file(error) || error) continue;
    const auto stem = iterator->path().stem().string();
    std::uint64_t value = 0U;
    const auto parsed = std::from_chars(stem.data(), stem.data() + stem.size(), value);
    if (parsed.ec == std::errc{} && parsed.ptr == stem.data() + stem.size()) {
      generations.push_back(value);
    }
  }
  std::sort(generations.rbegin(), generations.rend());
  std::optional<core::Error> lastError;
  for (const auto generation : generations) {
    auto text = core::readTextFileLimited(generationPath(generation), 64U * 1024U * 1024U);
    if (!text) {
      lastError = text.error();
      continue;
    }
    auto decoded = decodeProductionProject(text.value());
    if (!decoded) {
      lastError = decoded.error();
      continue;
    }
    if (decoded.value().lastDurableGeneration != generation) {
      lastError = core::Error{
          core::ErrorCode::InvariantViolation,
          "Production generation number does not match its filename",
          generationPath(generation).string()};
      continue;
    }
    auto verified = verifyGeneration(decoded.value(), false);
    if (verified) return decoded;
    lastError = verified.error();
  }
  if (lastError.has_value()) {
    return core::Result<VoicebankProductionProject>{*lastError};
  }
  return core::failure<VoicebankProductionProject>(
      core::ErrorCode::InvariantViolation,
      "No valid durable production generation could be recovered");
}

std::filesystem::path ProductionProjectRepository::generationPath(
    std::uint64_t generation) const {
  return root_ / "generations" / generationName(generation);
}

std::filesystem::path ProductionProjectRepository::journalPath(
    std::uint64_t generation) const {
  return root_ / "journal" / generationName(generation);
}

}
