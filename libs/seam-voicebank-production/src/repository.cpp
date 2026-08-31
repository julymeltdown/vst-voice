#include "seam/voicebank_production/repository.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank_production/project_codec.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <iomanip>
#include <optional>
#include <sstream>
#include <system_error>

namespace seam::voicebank_production {
namespace {

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

core::Result<void> verifyLicense(const VoicebankProductionProject& project) {
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
  constexpr std::array<const char*, 4U> directories{
      "assets", "generations", "journal", "staging"};
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
    VoicebankProductionProject& project, const ProductionJournalEvent& event) {
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
  auto license = verifyLicense(project);
  if (!license) return license;
  auto prepared = prepareWorkspace(root_, false);
  if (!prepared) return prepared;
  auto next = project;
  const auto occupied = std::max(highestGeneration(root_ / "generations"),
                                 highestGeneration(root_ / "journal"));
  next.lastDurableGeneration =
      std::max(project.lastDurableGeneration, occupied) + 1U;
  auto valid = validateProductionProject(next);
  if (!valid) return valid;
  const auto projectText = encodeProductionProject(next);
  const auto projectDigest = core::sha256Hex(projectText);
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
      }}, true) + "\n";
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
