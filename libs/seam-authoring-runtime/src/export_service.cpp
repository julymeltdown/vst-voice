#include "seam/authoring/export_service.hpp"

#include "seam/build/version.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include <map>
#include <unordered_set>

#include <chrono>
#include <algorithm>
#include <cctype>
#include <memory>
#include <system_error>

namespace seam::authoring {
namespace {

std::filesystem::path stagingPath(const std::filesystem::path& destination,
                                  std::uint64_t revision) {
  return destination.parent_path() /
         (".project-seam-export-" + destination.filename().string() + "-" +
          std::to_string(revision) + "-staging");
}

core::Result<void> removeTree(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::remove_all(path, error);
  if (error) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to remove failed export staging", error.message());
  }
  return core::success();
}

std::string safeStem(std::string value) {
  for (auto& character : value) {
    const auto allowed = std::isalnum(static_cast<unsigned char>(character)) ||
                         character == '-' || character == '_';
    if (!allowed) character = '_';
  }
  if (value.empty()) value = "track";
  return value;
}

std::string wavFormatName(voicebank::WavSampleFormat format) {
  switch (format) {
    case voicebank::WavSampleFormat::Pcm16: return "PCM16";
    case voicebank::WavSampleFormat::Pcm24: return "PCM24";
    case voicebank::WavSampleFormat::Float32: return "Float32";
  }
  return "Unknown";
}

std::int64_t executionDateUnixMs() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<std::int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

core::Result<ExportFileReceipt> writeRenderedFile(
    const rendering::ProjectRenderResult& rendered,
    const std::filesystem::path& path, voicebank::WavSampleFormat format) {
  if (rendered.channelCount == 0U ||
      rendered.interleaved.size() % rendered.channelCount != 0U) {
    return core::failure<ExportFileReceipt>(
        core::ErrorCode::InvariantViolation,
        "Rendered export buffer has invalid channel alignment");
  }
  auto writer = voicebank::WavStreamWriter::create(
      path, voicebank::WavOutputFormat{rendered.sampleRate,
                                      rendered.channelCount, format});
  if (!writer) return core::Result<ExportFileReceipt>{writer.error()};
  const auto blockSamples = static_cast<std::size_t>(rendered.channelCount) * 4096U;
  for (std::size_t offset = 0U; offset < rendered.interleaved.size();
       offset += blockSamples) {
    const auto count = std::min(blockSamples,
                                rendered.interleaved.size() - offset);
    auto written = writer.value()->writeFrames(
        std::span<const float>{rendered.interleaved}.subspan(offset, count));
    if (!written) return core::Result<ExportFileReceipt>{written.error()};
  }
  auto finalized = writer.value()->finalize();
  if (!finalized) return core::Result<ExportFileReceipt>{finalized.error()};
  auto digest = core::sha256File(path);
  if (!digest) return core::Result<ExportFileReceipt>{digest.error()};
  return ExportFileReceipt{
      .path = path,
      .sha256 = digest.value(),
      .frames = static_cast<std::uint64_t>(rendered.interleaved.size() /
                                            rendered.channelCount),
      .channels = rendered.channelCount,
  };
}

void notifyProgress(const std::function<void(const ExportProgress&)>& callback,
                    ExportState state, std::string output,
                    std::uint64_t completed, std::uint64_t total) {
  if (callback) {
    callback(ExportProgress{.state = state,
                            .currentOutput = std::move(output),
                            .completedFiles = completed,
                            .totalFiles = total});
  }
}

domain::Project isolatedProject(const domain::Project& project,
                                domain::TrackId target,
                                bool vocalTarget) {
  auto isolated = project;
  for (auto& track : isolated.vocalTracks()) {
    track.solo = false;
    track.muted = !vocalTarget || track.id != target;
  }
  for (auto& track : isolated.audioTracks()) {
    track.solo = false;
    track.muted = vocalTarget || track.id != target;
  }
  return isolated;
}

constexpr std::uint64_t kMaximumReceiptBytes = 4ULL * 1024ULL * 1024ULL;

struct ExportReceiptView final {
  std::string state;
  std::string transactionId;
  std::vector<std::filesystem::path> ownedPaths;
};

struct ExportJournal final {
  std::string transactionId;
  std::filesystem::path staging;
  std::filesystem::path backup;
};

std::filesystem::path journalPath(const std::filesystem::path& destination) {
  return destination.parent_path() /
         ("." + destination.filename().string() + "-export-journal.json");
}

bool safeRelativePath(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute() || path != path.lexically_normal()) {
    return false;
  }
  return std::none_of(path.begin(), path.end(), [](const auto& component) {
    return component == "." || component == ".." || component.empty();
  });
}

core::Result<ExportReceiptView> readExportReceipt(
    const std::filesystem::path& destination, bool validateFiles) {
  const auto receiptPath = destination / "receipt.json";
  auto text = core::readTextFileLimited(receiptPath, kMaximumReceiptBytes);
  if (!text) return core::Result<ExportReceiptView>{text.error()};
  auto parsed = formats::parseJson(
      text.value(), formats::JsonParseLimits{
                        .maximumInputBytes = kMaximumReceiptBytes,
                        .maximumDepth = 16U,
                        .maximumNodes = 10000U,
                        .maximumStringBytes = 1024U * 1024U,
                        .maximumCollectionEntries = 4096U,
                    });
  if (!parsed || !parsed.value().isObject()) {
    return core::failure<ExportReceiptView>(
        core::ErrorCode::Conflict, "Export receipt is not valid JSON");
  }
  const auto* state = parsed.value().find("state");
  const auto* files = parsed.value().find("files");
  const auto* transaction = parsed.value().find("transactionId");
  if (state == nullptr || !state->isString() || files == nullptr ||
      !files->isArray()) {
    return core::failure<ExportReceiptView>(
        core::ErrorCode::Conflict, "Export receipt ownership fields are invalid");
  }
  ExportReceiptView result{
      .state = state->asString(),
      .transactionId = transaction != nullptr && transaction->isString()
                           ? transaction->asString()
                           : std::string{},
      .ownedPaths = {std::filesystem::path{"receipt.json"}},
  };
  for (const auto& entry : files->asArray()) {
    const auto* pathValue = entry.isObject() ? entry.find("path") : nullptr;
    const auto* shaValue = entry.isObject() ? entry.find("sha256") : nullptr;
    if (pathValue == nullptr || !pathValue->isString() || shaValue == nullptr ||
        !shaValue->isString()) {
      return core::failure<ExportReceiptView>(
          core::ErrorCode::Conflict, "Export receipt file entry is invalid");
    }
    const std::filesystem::path relative{pathValue->asString()};
    if (!safeRelativePath(relative)) {
      return core::failure<ExportReceiptView>(
          core::ErrorCode::Conflict, "Export receipt contains an unsafe path");
    }
    if (std::find(result.ownedPaths.begin(), result.ownedPaths.end(), relative) !=
        result.ownedPaths.end()) {
      return core::failure<ExportReceiptView>(
          core::ErrorCode::Conflict, "Export receipt contains a duplicate path");
    }
    result.ownedPaths.push_back(relative);
    if (!validateFiles) continue;
    std::error_code error;
    const auto file = destination / relative;
    const auto status = std::filesystem::symlink_status(file, error);
    if (error == std::errc::no_such_file_or_directory ||
        status.type() == std::filesystem::file_type::not_found) {
      continue;
    }
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status)) {
      return core::failure<ExportReceiptView>(
          core::ErrorCode::Conflict,
          "Export receipt-owned path is not a regular file", file.string());
    }
    auto digest = core::sha256File(file);
    if (!digest || digest.value() != shaValue->asString()) {
      return core::failure<ExportReceiptView>(
          core::ErrorCode::Conflict,
          "Export receipt-owned file hash does not match", file.string());
    }
  }
  return result;
}

core::Result<void> copyUnownedFiles(
    const std::filesystem::path& destination,
    const std::filesystem::path& staging,
    const std::vector<std::filesystem::path>& ownedPaths) {
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator iterator{destination, error};
       !error && iterator != std::filesystem::recursive_directory_iterator{};
       iterator.increment(error)) {
    const auto status = iterator->symlink_status(error);
    if (error) break;
    if (std::filesystem::is_symlink(status)) {
      return core::failure(core::ErrorCode::Conflict,
                           "Export destination contains a symbolic link",
                           iterator->path().string());
    }
    if (!std::filesystem::is_regular_file(status)) continue;
    const auto relative = iterator->path().lexically_relative(destination);
    if (!safeRelativePath(relative)) {
      return core::failure(core::ErrorCode::Conflict,
                           "Export destination contains an unsafe path",
                           iterator->path().string());
    }
    if (std::find(ownedPaths.begin(), ownedPaths.end(), relative) !=
        ownedPaths.end()) {
      continue;
    }
    const auto target = staging / relative;
    if (std::filesystem::exists(target, error)) {
      return core::failure(core::ErrorCode::Conflict,
                           "Unowned export file conflicts with a new output",
                           relative.generic_string());
    }
    std::filesystem::create_directories(target.parent_path(), error);
    if (error) break;
    std::filesystem::copy_file(iterator->path(), target, error);
    if (error) break;
  }
  if (error) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to preserve an unowned export file",
                         error.message());
  }
  return core::success();
}

core::Result<void> writeJournal(
    const std::filesystem::path& path, const ExportJournal& journal,
    std::string phase) {
  return core::durableAtomicWriteText(
      path,
      formats::stringifyJson(
          formats::JsonValue{formats::JsonValue::Object{
              {"schemaVersion", formats::JsonValue{std::int64_t{1}}},
              {"phase", formats::JsonValue{std::move(phase)}},
              {"transactionId", formats::JsonValue{journal.transactionId}},
              {"staging", formats::JsonValue{journal.staging.filename().string()}},
              {"backup", formats::JsonValue{journal.backup.filename().string()}},
          }},
          true));
}

core::Result<ExportJournal> readJournal(
    const std::filesystem::path& destination) {
  auto text = core::readTextFileLimited(journalPath(destination),
                                        kMaximumReceiptBytes);
  if (!text) return core::Result<ExportJournal>{text.error()};
  auto parsed = formats::parseJson(text.value());
  const auto* transaction = parsed && parsed.value().isObject()
                                ? parsed.value().find("transactionId")
                                : nullptr;
  const auto* staging = parsed && parsed.value().isObject()
                            ? parsed.value().find("staging")
                            : nullptr;
  const auto* backup = parsed && parsed.value().isObject()
                           ? parsed.value().find("backup")
                           : nullptr;
  if (transaction == nullptr || !transaction->isString() ||
      transaction->asString().empty() || staging == nullptr ||
      !staging->isString() || backup == nullptr || !backup->isString()) {
    return core::failure<ExportJournal>(
        core::ErrorCode::Conflict, "Export transaction journal is invalid");
  }
  const std::filesystem::path stagingName{staging->asString()};
  const std::filesystem::path backupName{backup->asString()};
  const auto prefix = "." + destination.filename().string() + "-export-" +
                      transaction->asString();
  if (stagingName.has_parent_path() || backupName.has_parent_path() ||
      stagingName.string() != prefix + "-staging" ||
      backupName.string() != prefix + "-backup") {
    return core::failure<ExportJournal>(
        core::ErrorCode::Conflict,
        "Export transaction journal paths are not owned by the transaction");
  }
  return ExportJournal{
      .transactionId = transaction->asString(),
      .staging = destination.parent_path() / stagingName,
      .backup = destination.parent_path() / backupName,
  };
}

}

std::string_view exportStateName(ExportState state) noexcept {
  switch (state) {
    case ExportState::Preflight: return "preflight";
    case ExportState::Staging: return "staging";
    case ExportState::Prepared: return "prepared";
    case ExportState::Publishing: return "publishing";
    case ExportState::Committed: return "committed";
    case ExportState::Cancelled: return "cancelled";
    case ExportState::Failed: return "failed";
    case ExportState::Recovered: return "recovered";
    case ExportState::RollbackRequired: return "rollback-required";
  }
  return "unknown";
}

core::Result<ExportResult> ExportService::exportProject(
    const domain::Project& project,
    std::span<const rendering::TrackVoicebankSource> voicebanks,
    domain::TrackId activeTrack, domain::RegionId activeRegion,
    std::uint64_t revision, const std::filesystem::path& destination,
    voicebank::WavSampleFormat format, std::stop_token stopToken) const {
  std::vector<rendering::TrackSingerSource> sources;
  sources.reserve(voicebanks.size());
  for (const auto& source : voicebanks) sources.emplace_back(source);
  return exportProjectWithSources(project, sources, activeTrack, activeRegion, revision, destination, format, stopToken);
}

core::Result<ExportResult> ExportService::exportProjectWithSources(
    const domain::Project& project, std::span<const rendering::TrackSingerSource> sources,
    domain::TrackId activeTrack, domain::RegionId activeRegion, std::uint64_t revision,
    const std::filesystem::path& destination, voicebank::WavSampleFormat format, std::stop_token stopToken) const {
  rendering::ProductionProjectRenderer renderer;
  const auto rendered = renderer.renderWithSources(
      project, sources, activeTrack, activeRegion, revision,
      static_cast<std::uint32_t>(project.settings().sampleRate),
      rendering::RenderQuality::Final, {}, nullptr, stopToken);
  if (!rendered) return core::Result<ExportResult>{rendered.error()};
  return commitRendered(rendered.value(), revision, destination, format,
                        stopToken);
}

core::Result<ExportResult> ExportService::commitRendered(
    const rendering::ProjectRenderResult& rendered, std::uint64_t revision,
    const std::filesystem::path& destination, voicebank::WavSampleFormat format,
    std::stop_token stopToken) const {
  ExportResult result{.state = ExportState::Preflight,
                      .masterPath = destination,
                      .projectRevision = revision};
  if (destination.empty() || destination.filename().empty() ||
      rendered.sampleRate < 8000U || rendered.sampleRate > 384000U ||
      rendered.channelCount == 0U || rendered.channelCount > 8U ||
      rendered.interleaved.empty() ||
      rendered.interleaved.size() % rendered.channelCount != 0U) {
    return core::failure<ExportResult>(core::ErrorCode::InvalidArgument,
                                       "Export preflight rejected the output");
  }
  if (stopToken.stop_requested()) {
    result.state = ExportState::Cancelled;
    result.diagnostic = "Export cancelled before staging";
    return result;
  }
  std::error_code error;
  if (destination.has_parent_path()) {
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
      return core::failure<ExportResult>(core::ErrorCode::IoError,
                                         "Unable to create export directory",
                                         error.message());
    }
  }
  const auto staging = stagingPath(destination, revision);
  if (std::filesystem::exists(staging, error)) {
    if (error) {
      return core::failure<ExportResult>(core::ErrorCode::IoError,
                                         "Unable to inspect export staging",
                                         error.message());
    }
    return core::failure<ExportResult>(core::ErrorCode::InvalidArgument,
                                       "Export staging path already exists");
  }
  if (error) {
    return core::failure<ExportResult>(core::ErrorCode::IoError,
                                       "Unable to inspect export staging",
                                       error.message());
  }
  std::filesystem::create_directories(staging, error);
  if (error) {
    return core::failure<ExportResult>(core::ErrorCode::IoError,
                                       "Unable to create export staging",
                                       error.message());
  }
  result.state = ExportState::Staging;
  const auto stagedMaster = staging / "master.wav";
  auto writer = voicebank::WavStreamWriter::create(
      stagedMaster,
      voicebank::WavOutputFormat{rendered.sampleRate, rendered.channelCount,
                                 format});
  if (!writer) {
    static_cast<void>(removeTree(staging));
    return core::Result<ExportResult>{writer.error()};
  }
  const auto blockSamples = static_cast<std::size_t>(rendered.channelCount) * 4096U;
  for (std::size_t offset = 0U; offset < rendered.interleaved.size();
       offset += blockSamples) {
    if (stopToken.stop_requested()) {
      static_cast<void>(removeTree(staging));
      result.state = ExportState::Cancelled;
      result.diagnostic = "Export cancelled during staging";
      return result;
    }
    const auto count = std::min(blockSamples, rendered.interleaved.size() - offset);
    auto written = writer.value()->writeFrames(
        std::span<const float>{rendered.interleaved}.subspan(offset, count));
    if (!written) {
      static_cast<void>(removeTree(staging));
      return core::Result<ExportResult>{written.error()};
    }
  }
  auto finalized = writer.value()->finalize();
  if (!finalized) {
    static_cast<void>(removeTree(staging));
    return core::Result<ExportResult>{finalized.error()};
  }
  const auto digest = core::sha256File(stagedMaster);
  if (!digest) {
    static_cast<void>(removeTree(staging));
    return core::Result<ExportResult>{digest.error()};
  }
  result.state = ExportState::Prepared;
  result.masterSha256 = digest.value();
  const auto receiptText = formats::stringifyJson(
      formats::JsonValue{formats::JsonValue::Object{
          {"schemaVersion", formats::JsonValue{std::int64_t{1}}},
          {"state", formats::JsonValue{"COMMITTED"}},
          {"projectRevision", formats::JsonValue{static_cast<std::int64_t>(revision)}},
          {"sampleRate", formats::JsonValue{static_cast<std::int64_t>(rendered.sampleRate)}},
          {"channels", formats::JsonValue{static_cast<std::int64_t>(rendered.channelCount)}},
          {"formatName", formats::JsonValue{wavFormatName(format)}},
          {"renderQuality", formats::JsonValue{"Final"}},
          {"renderAbi", formats::JsonValue{std::string{build::kRenderAbiId}}},
          {"applicationBuildSha", formats::JsonValue{std::string{build::kSourceCommit}}},
          {"executionDateUnixMs", formats::JsonValue{executionDateUnixMs()}},
          {"masterSha256", formats::JsonValue{result.masterSha256}},
          {"masterFile", formats::JsonValue{"master.wav"}},
      }},
      true);
  const auto stagedReceipt = staging / "receipt.json";
  auto receiptWrite = core::durableAtomicWriteText(stagedReceipt, receiptText);
  if (!receiptWrite) {
    static_cast<void>(removeTree(staging));
    return core::Result<ExportResult>{receiptWrite.error()};
  }
  result.state = ExportState::Publishing;
  const auto backup = destination.string() + ".previous";
  const auto receiptPath = destination.string() + ".receipt.json";
  const auto receiptBackup = receiptPath + ".previous";
  bool previousMasterMoved = false;
  bool newMasterPublished = false;
  bool previousReceiptMoved = false;
  bool newReceiptPublished = false;
  const auto rollbackPublication = [&](std::string diagnostic)
      -> core::Result<ExportResult> {
    std::error_code rollbackError;
    if (newReceiptPublished) {
      std::filesystem::remove(receiptPath, rollbackError);
    }
    if (!rollbackError && previousReceiptMoved) {
      std::filesystem::rename(receiptBackup, receiptPath, rollbackError);
    }
    if (!rollbackError && newMasterPublished) {
      std::filesystem::remove(destination, rollbackError);
    }
    if (!rollbackError && previousMasterMoved) {
      std::filesystem::rename(backup, destination, rollbackError);
    }
    static_cast<void>(removeTree(staging));
    if (rollbackError) {
      result.state = ExportState::RollbackRequired;
      result.diagnostic = std::move(diagnostic) + "; rollback failed: " +
                          rollbackError.message();
      return result;
    }
    return core::failure<ExportResult>(core::ErrorCode::IoError,
                                       std::move(diagnostic));
  };
  const auto previousMasterExists = std::filesystem::exists(destination, error);
  if (error) {
    static_cast<void>(removeTree(staging));
    return core::failure<ExportResult>(core::ErrorCode::IoError,
                                       "Unable to inspect previous export",
                                       error.message());
  }
  const auto masterBackupExists = std::filesystem::exists(backup, error);
  if (error) {
    static_cast<void>(removeTree(staging));
    return core::failure<ExportResult>(core::ErrorCode::IoError,
                                       "Unable to inspect master backup path",
                                       error.message());
  }
  const auto receiptBackupExists = std::filesystem::exists(receiptBackup, error);
  if (error) {
    static_cast<void>(removeTree(staging));
    return core::failure<ExportResult>(core::ErrorCode::IoError,
                                       "Unable to inspect receipt backup path",
                                       error.message());
  }
  if (masterBackupExists || receiptBackupExists) {
    static_cast<void>(removeTree(staging));
    return core::failure<ExportResult>(core::ErrorCode::Conflict,
                                       "Export rollback backup path already exists");
  }
  if (previousMasterExists) {
    std::filesystem::rename(destination, backup, error);
    if (error) {
      static_cast<void>(removeTree(staging));
      return core::failure<ExportResult>(core::ErrorCode::IoError,
                                         "Unable to preserve previous export",
                                         error.message());
    }
    previousMasterMoved = true;
  }
  error.clear();
  std::filesystem::rename(stagedMaster, destination, error);
  if (error) {
    return rollbackPublication("Unable to publish exported master: " +
                               error.message());
  }
  newMasterPublished = true;
  error.clear();
  const auto previousReceiptExists = std::filesystem::exists(receiptPath, error);
  if (error) {
    return rollbackPublication("Unable to inspect previous export receipt: " +
                               error.message());
  }
  if (previousReceiptExists) {
    std::filesystem::rename(receiptPath, receiptBackup, error);
    if (error) {
      return rollbackPublication(
          "Unable to preserve previous export receipt: " + error.message());
    }
    previousReceiptMoved = true;
  }
  error.clear();
  std::filesystem::rename(stagedReceipt, receiptPath, error);
  if (error) {
    return rollbackPublication("Unable to publish export receipt: " +
                               error.message());
  }
  newReceiptPublished = true;
  if (previousMasterMoved) {
    error.clear();
    std::filesystem::remove(backup, error);
    if (error) {
      result.diagnostic = "Published export but could not remove owned master backup: " +
                          error.message();
    }
  }
  if (previousReceiptMoved) {
    error.clear();
    std::filesystem::remove(receiptBackup, error);
    if (error && result.diagnostic.empty()) {
      result.diagnostic = "Published export but could not remove owned receipt backup: " +
                          error.message();
    }
  }
  static_cast<void>(removeTree(staging));
  result.state = ExportState::Committed;
  result.receiptPath = receiptPath;
  return result;
}

core::Result<ExportResult> ExportService::exportSet(
    const domain::Project& project,
    std::span<const rendering::TrackVoicebankSource> voicebanks,
    domain::TrackId activeTrack, domain::RegionId activeRegion,
    std::uint64_t revision, const std::filesystem::path& destination,
    ExportSettings settings,
    std::function<void(const ExportProgress&)> progress,
    std::stop_token stopToken) const {
  std::vector<rendering::TrackSingerSource> sources;
  sources.reserve(voicebanks.size());
  for (const auto& source : voicebanks) sources.emplace_back(source);
  return exportSetWithSources(project, sources, activeTrack, activeRegion, revision,
      destination, std::move(settings), std::move(progress), stopToken);
}

core::Result<ExportResult> ExportService::exportSetWithSources(
    const domain::Project& project, std::span<const rendering::TrackSingerSource> voicebanks,
    domain::TrackId activeTrack, domain::RegionId activeRegion, std::uint64_t revision,
    const std::filesystem::path& destination, ExportSettings settings,
    std::function<void(const ExportProgress&)> progress, std::stop_token stopToken) const {
  ExportResult result{.state = ExportState::Preflight,
                      .projectRevision = revision,
                      .setPath = destination};
  notifyProgress(progress, ExportState::Preflight, {}, 0U, 0U);
  if (destination.empty() || destination.filename().empty() ||
      (!settings.includeMaster && !settings.includeStems && !settings.includeProceduralCandidates) ||
      settings.sampleRate < 8000U || settings.sampleRate > 384000U ||
      settings.channels == 0U || settings.channels > 8U) {
    return core::failure<ExportResult>(
        core::ErrorCode::InvalidArgument,
        "Export-set preflight rejected the requested settings");
  }
  const auto validation = project.validate();
  if (!validation) return core::Result<ExportResult>{validation.error()};
  std::unordered_set<domain::TrackId> sourceTracks;
  for (const auto& source : voicebanks) {
    const auto id = std::visit([](const auto& value) { return value.trackId; }, source);
    if (!project.findVocalTrack(id) || !sourceTracks.insert(id).second) return core::failure<ExportResult>(
        core::ErrorCode::Conflict, "Export sources contain an unknown or duplicate vocal track");
  }
  formats::ProjectJsonCodec projectCodec;
  const auto encodedProject = projectCodec.encode(project);
  if (!encodedProject) {
    return core::Result<ExportResult>{encodedProject.error()};
  }
  const auto projectContentHash = core::sha256Hex(encodedProject.value());
  if (std::filesystem::exists(destination) && !settings.replaceExisting) {
    return core::failure<ExportResult>(
        core::ErrorCode::Conflict,
        "Export destination already contains an export set",
        destination.string());
  }
  if (project.routing().deviceOutputChannels != settings.channels) {
    return core::failure<ExportResult>(
        core::ErrorCode::InvalidArgument,
        "Export channel count must match the project output routing");
  }
  if (stopToken.stop_requested()) {
    result.state = ExportState::Cancelled;
    result.diagnostic = "Export cancelled during preflight";
    return result;
  }

  std::vector<std::pair<domain::TrackId, bool>> stems;
  std::map<std::string, std::string> packageFiles;
  std::vector<rendering::TrackSingerSource> frozenSources;
  if (settings.includeProjectAndRecipes || settings.includeProceduralCandidates) {
    if (voicebanks.size() > project.vocalTracks().size()) return core::failure<ExportResult>(
        core::ErrorCode::InvalidArgument, "Recipe package source count exceeds project tracks");
    for (const auto& track : project.audioTracks()) {
      if (!track.mediaPath.empty() && std::filesystem::path{track.mediaPath}.is_relative()) return core::failure<ExportResult>(
          core::ErrorCode::Conflict, "Resolve relative backing media before packaging the project snapshot");
    }
    auto packaged = project;
    std::size_t recipeBytes = 0U;
    for (const auto& source : voicebanks) {
      if (stopToken.stop_requested()) return core::failure<ExportResult>(core::ErrorCode::Conflict, "Recipe packaging cancelled");
      if (std::holds_alternative<rendering::TrackVoicebankSource>(source)) { frozenSources.push_back(source); continue; }
      rendering::TrackProceduralSource frozen;
      if (const auto* value = std::get_if<rendering::TrackProceduralSource>(&source)) frozen = *value;
      else {
        const auto& file = std::get<rendering::TrackRecipeFileSource>(source);
        auto path = std::filesystem::path{file.reference.path};
        if (path.is_relative()) {
          if (!file.projectDirectory || !file.projectDirectory->is_absolute()) return core::failure<ExportResult>(
              core::ErrorCode::NotFound, "Recipe packaging requires a saved project directory");
          path = *file.projectDirectory / path;
        }
        const auto resource = voice_design::loadVoiceRecipeResource(path.lexically_normal(), file.reference.resource, stopToken);
        if (!resource) return core::Result<ExportResult>{resource.error()};
        frozen = {file.trackId, resource.value(), file.reference.style};
      }
      const auto recipe = voice_design::decodeVoiceRecipeResource(frozen.resource, stopToken);
      if (!recipe) return core::Result<ExportResult>{recipe.error()};
      const auto encoded = voice_design::encodeVoiceRecipe(recipe.value());
      if (!encoded) return core::Result<ExportResult>{encoded.error()};
      if (core::sha256Hex(encoded.value()) != frozen.resource.identity.contentHash) return core::failure<ExportResult>(
          core::ErrorCode::Conflict, "Packaged recipe identity must describe canonical recipe bytes");
      const auto relative = "recipes/" + frozen.resource.identity.contentHash + ".json";
      if (!packageFiles.contains(relative)) {
        if (encoded.value().size() > 16U * 1024U * 1024U - recipeBytes) return core::failure<ExportResult>(
            core::ErrorCode::Unsupported, "Packaged recipes exceed the 16 MiB aggregate limit");
        recipeBytes += encoded.value().size();
        packageFiles.emplace(relative, encoded.value());
      }
      auto* track = packaged.findVocalTrack(frozen.trackId);
      if (!track) return core::failure<ExportResult>(core::ErrorCode::NotFound, "Packaged recipe track is missing");
      track->proceduralRecipe = domain::ProceduralRecipeReference{frozen.resource.identity, relative, frozen.style};
      frozenSources.emplace_back(std::move(frozen));
    }
    const auto encoded = projectCodec.encode(packaged);
    for (const auto& track : project.vocalTracks()) {
      if (track.proceduralRecipe && std::none_of(frozenSources.begin(), frozenSources.end(), [&](const auto& source) {
          const auto* procedural = std::get_if<rendering::TrackProceduralSource>(&source);
          return procedural && procedural->trackId == track.id;
        })) return core::failure<ExportResult>(core::ErrorCode::NotFound, "A saved recipe is absent from the package sources");
    }
    if (!encoded) return core::Result<ExportResult>{encoded.error()};
    packageFiles.emplace("project.seam", encoded.value());
    voicebanks = frozenSources; // All outputs use the same captured recipes.
  }
  if (settings.includeStems) {
    for (const auto& track : project.vocalTracks()) {
      if (!track.muted) stems.emplace_back(track.id, true);
    }
    for (const auto& track : project.audioTracks()) {
      if (!track.muted) stems.emplace_back(track.id, false);
    }
  }
  std::vector<rendering::RenderSnapshot> candidates;
  if (settings.includeProceduralCandidates) {
    std::size_t notes = 0U;
    for (const auto& source : voicebanks) {
      const auto* procedural = std::get_if<rendering::TrackProceduralSource>(&source);
      if (!procedural) continue;
      const auto* track = project.findVocalTrack(procedural->trackId);
      if (!track) return core::failure<ExportResult>(core::ErrorCode::NotFound, "Bake track is missing");
      for (const auto& region : track->regions) {
        if (region.notes.empty()) continue;
        if (candidates.size() >= 256U || region.notes.size() > 65536U - notes) return core::failure<ExportResult>(
            core::ErrorCode::Unsupported, "Procedural bake request exceeds candidate/note bounds");
        notes += region.notes.size();
        auto snapshot = rendering::RenderSnapshotFactory{}.createProcedural(project, procedural->resource,
            track->id, region.id, revision, rendering::RenderQuality::Final, settings.sampleRate, procedural->style);
        if (!snapshot) return core::Result<ExportResult>{snapshot.error()};
        candidates.push_back(std::move(snapshot).value());
      }
    }
    if (candidates.empty()) return core::failure<ExportResult>(core::ErrorCode::InvalidArgument, "No procedural regions were requested for baking");
  }
  const auto totalFiles = static_cast<std::uint64_t>(
      (settings.includeMaster ? 1U : 0U) + stems.size() + packageFiles.size() + candidates.size() * 2U);
  if (totalFiles > 1024U) return core::failure<ExportResult>(core::ErrorCode::Unsupported, "Export file count exceeds receipt bounds");
  if (totalFiles == 0U) {
    return core::failure<ExportResult>(core::ErrorCode::InvalidArgument,
                                       "Export set contains no outputs");
  }

  const auto executedAt = executionDateUnixMs();
  const auto transactionId = core::sha256Hex(
      projectContentHash + "|" + std::to_string(revision) + "|" +
      std::to_string(executedAt)).substr(0U, 24U);
  const auto transactionPrefix =
      "." + destination.filename().string() + "-export-" + transactionId;
  const auto staging = destination.parent_path() /
                       (transactionPrefix + "-staging");
  const auto backup = destination.parent_path() /
                      (transactionPrefix + "-backup");
  const auto transactionJournal = journalPath(destination);
  std::error_code error;
  std::filesystem::create_directories(destination.parent_path(), error);
  if (error) {
    return core::failure<ExportResult>(core::ErrorCode::IoError,
                                       "Unable to create export parent",
                                       error.message());
  }
  if (std::filesystem::exists(transactionJournal, error)) {
    auto recovered = recoverSet(destination);
    if (!recovered) return core::Result<ExportResult>{recovered.error()};
  }
  error.clear();
  if (std::filesystem::exists(staging, error) ||
      std::filesystem::exists(backup, error)) {
    return core::failure<ExportResult>(
        core::ErrorCode::Conflict,
        "Export transaction path already exists and is not owned by this run");
  }
  if (error || !std::filesystem::create_directory(staging, error)) {
    return core::failure<ExportResult>(core::ErrorCode::IoError,
                                       "Unable to create export-set staging",
                                       error.message());
  }
  result.state = ExportState::Staging;
  notifyProgress(progress, result.state, {}, 0U, totalFiles);

  rendering::ProductionProjectRenderer renderer;
  const auto renderOne = [&](const domain::Project& renderProject,
                             const std::filesystem::path& relative,
                             domain::TrackId renderTrack,
                             domain::RegionId renderRegion,
                             std::uint64_t completed)
      -> core::Result<ExportFileReceipt> {
    if (stopToken.stop_requested()) {
      return core::failure<ExportFileReceipt>(core::ErrorCode::Conflict,
                                              "Export cancelled");
    }
    auto rendered = renderer.renderWithSources(
        renderProject, voicebanks, renderTrack, renderRegion, revision,
        settings.sampleRate, rendering::RenderQuality::Final, {}, nullptr,
        stopToken);
    if (!rendered) return core::Result<ExportFileReceipt>{rendered.error()};
    const auto target = staging / relative;
    std::filesystem::create_directories(target.parent_path(), error);
    if (error) {
      return core::failure<ExportFileReceipt>(core::ErrorCode::IoError,
                                              "Unable to create export output directory",
                                              error.message());
    }
    auto receipt = writeRenderedFile(rendered.value(), target, settings.format);
    if (!receipt) return receipt;
    notifyProgress(progress, ExportState::Staging, relative.generic_string(),
                   completed + 1U, totalFiles);
    return receipt;
  };

  std::uint64_t completed = 0U;
  if (settings.includeMaster) {
    auto master = renderOne(project, "master.wav", activeTrack, activeRegion,
                            completed);
    if (!master) {
      static_cast<void>(removeTree(staging));
      if (master.error().code == core::ErrorCode::Conflict &&
          stopToken.stop_requested()) {
        result.state = ExportState::Cancelled;
        result.diagnostic = "Export cancelled during rendering";
        return result;
      }
      return core::Result<ExportResult>{master.error()};
    }
    result.masterSha256 = master.value().sha256;
    result.files.push_back(master.value());
    result.masterPath = destination / "master.wav";
    ++completed;
  }
  for (const auto& [trackId, vocal] : stems) {
    const auto* track = vocal ? project.findVocalTrack(trackId) : nullptr;
    const auto trackName = track == nullptr
                               ? std::string{"audio"}
                               : track->name;
    const auto relative = std::filesystem::path{"stems"} /
        (safeStem(trackName) + "-" + trackId.toString() + ".wav");
    const auto isolated = isolatedProject(project, trackId, vocal);
    domain::RegionId stemRegion{};
    if (vocal && track != nullptr && !track->regions.empty()) {
      stemRegion = track->regions.front().id;
    }
    auto stem = renderOne(isolated, relative, vocal ? trackId : domain::TrackId{},
                          stemRegion, completed);
    if (!stem) {
      static_cast<void>(removeTree(staging));
      if (stem.error().code == core::ErrorCode::Conflict &&
          stopToken.stop_requested()) {
        result.state = ExportState::Cancelled;
        result.diagnostic = "Export cancelled during stem rendering";
        return result;
      }
      return core::Result<ExportResult>{stem.error()};
    }
    result.files.push_back(stem.value());
    ++completed;
  }

  for (const auto& candidate : candidates) {
    auto rendered = rendering::PhraseRenderPipeline{}.render(candidate, stopToken);
    if (!rendered) { static_cast<void>(removeTree(staging)); return core::Result<ExportResult>{rendered.error()}; }
    const auto prefix = std::string{"candidates/"} + candidate.trackId.toString() + "-" + candidate.segment.regionId.toString();
    const auto path = staging / (prefix + ".wav");
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) { static_cast<void>(removeTree(staging)); return core::failure<ExportResult>(core::ErrorCode::IoError, "Unable to stage candidate audio"); }
    const auto origin = rendered.value().rendered.audio.startFrame;
    rendering::ProjectRenderResult mono;
    mono.sampleRate = candidate.sampleRate; mono.channelCount = 1U;
    mono.interleaved = std::move(rendered.value().rendered.audio.samples);
    const auto audio = writeRenderedFile(mono, path, voicebank::WavSampleFormat::Float32);
    if (!audio) { static_cast<void>(removeTree(staging)); return core::Result<ExportResult>{audio.error()}; }
    const auto& resource = std::get<synthesis::ProceduralSingerResource>(candidate.resource).identity;
    const bool mixed = voice_design::requiresArticulation(candidate.phonemes->tokens);
    const bool voicedFrication=std::any_of(rendered.value().proceduralMarkers.begin(),rendered.value().proceduralMarkers.end(),
        [](const auto& marker){return marker.kind==voice_design::ProceduralGestureKind::VoicedFrication;});
    const bool nasal=std::any_of(rendered.value().proceduralMarkers.begin(),rendered.value().proceduralMarkers.end(),
        [](const auto& marker) { return marker.kind==voice_design::ProceduralGestureKind::Nasal; });
    const bool plosive=std::any_of(rendered.value().proceduralMarkers.begin(),rendered.value().proceduralMarkers.end(),
        [](const auto& marker) { return marker.kind==voice_design::ProceduralGestureKind::Plosive; });
    formats::JsonValue::Array markers;
    for (const auto& marker : rendered.value().proceduralMarkers) {
      formats::JsonValue::Object entry{
        {"key", formats::JsonValue{marker.key.toString()}}, {"phone", formats::JsonValue{marker.phone}},
        {"startFrame", formats::JsonValue{static_cast<std::int64_t>(marker.ownedSpan.start - origin)}},
        {"endFrame", formats::JsonValue{static_cast<std::int64_t>(marker.ownedSpan.end - origin)}}};
      if (mixed) entry.emplace("kind", formats::JsonValue{marker.kind == voice_design::ProceduralGestureKind::Frication ? "frication" :
          marker.kind==voice_design::ProceduralGestureKind::VoicedFrication?"voiced-frication":
          marker.kind==voice_design::ProceduralGestureKind::Plosive?"plosive":
          marker.kind==voice_design::ProceduralGestureKind::Nasal?"nasal":"oral-vowel"});
      markers.emplace_back(std::move(entry));
    }
    formats::JsonValue::Object metadataFields{
        {"formatId", formats::JsonValue{"com.project-seam.procedural-candidate"}},
        {"schemaVersion", formats::JsonValue{std::int64_t{voicedFrication ? 5 : plosive ? 4 : nasal ? 3 : mixed ? 2 : 1}}}, {"approval", formats::JsonValue{"unapproved"}},
        {"markerSemantics", formats::JsonValue{mixed ? "planned-articulated-gestures" : "planned-vowel-gestures"}},
        {"audioSha256", formats::JsonValue{audio.value().sha256}},
        {"sampleRate", formats::JsonValue{static_cast<std::int64_t>(candidate.sampleRate)}},
        {"frameCount", formats::JsonValue{static_cast<std::int64_t>(mono.interleaved.size())}},
        {"scoreOriginFrame", formats::JsonValue{static_cast<std::int64_t>(origin)}},
        {"renderContentHash", formats::JsonValue{candidate.contentHash}},
        {"renderAbi", formats::JsonValue{candidate.renderAbiId}},
        {"proceduralRevision", formats::JsonValue{static_cast<std::int64_t>(mixed ? voice_design::ArticulatedStream::algorithmRevision : voice_design::kSustainedPoseRendererRevision)}},
        {"compilerRevision", formats::JsonValue{static_cast<std::int64_t>(synthesis::kPerformanceCompilerRevision)}},
        {"recipeId", formats::JsonValue{resource.id}}, {"recipeVersion", formats::JsonValue{resource.version}},
        {"recipeHash", formats::JsonValue{resource.contentHash}}, {"style", formats::JsonValue{candidate.style}},
        {"markers", formats::JsonValue{std::move(markers)}}};
    if (mixed) {
      metadataFields.emplace("articulationPlanRevision", formats::JsonValue{static_cast<std::int64_t>(voice_design::ArticulationPlan::algorithmRevision)});
      metadataFields.emplace("fricationRevision", formats::JsonValue{static_cast<std::int64_t>(voice_design::FricationSource::algorithmRevision)});
      metadataFields.emplace("fricationStreamRevision", formats::JsonValue{static_cast<std::int64_t>(voice_design::FricationGestureStream::algorithmRevision)});
    }
    if (plosive || voicedFrication) metadataFields.emplace("plosiveRevision", formats::JsonValue{static_cast<std::int64_t>(voice_design::PlosiveSource::algorithmRevision)});
    const auto metadata = formats::stringifyJson(formats::JsonValue{std::move(metadataFields)}, true);
    const auto metadataPath = staging / (prefix + ".json");
    const auto saved = core::durableAtomicWriteTextNew(metadataPath, metadata);
    if (!saved) { static_cast<void>(removeTree(staging)); return core::Result<ExportResult>{saved.error()}; }
    result.files.push_back(audio.value());
    result.files.push_back({metadataPath, core::sha256Hex(metadata), 0U, 0U});
    completed += 2U;
    notifyProgress(progress, ExportState::Staging, prefix, completed, totalFiles);
  }
  for (const auto& [relative, bytes] : packageFiles) {
    if (stopToken.stop_requested()) {
      static_cast<void>(removeTree(staging));
      return core::failure<ExportResult>(core::ErrorCode::Conflict, "Recipe package writing cancelled");
    }
    const auto path = staging / relative;
    std::filesystem::create_directories(path.parent_path(), error);
    const auto written = error ? core::failure(core::ErrorCode::IoError, "Unable to create recipe package directory") :
        core::durableAtomicWriteTextNew(path, bytes);
    if (!written) { static_cast<void>(removeTree(staging)); return core::Result<ExportResult>{written.error()}; }
    result.files.push_back({path, core::sha256Hex(bytes), 0U, 0U});
    ++completed;
    notifyProgress(progress, ExportState::Staging, relative, completed, totalFiles);
  }
  result.state = ExportState::Prepared;
  notifyProgress(progress, result.state, {}, completed, totalFiles);
  formats::JsonValue::Array files;
  for (const auto& file : result.files) {
    const auto relative = std::filesystem::relative(file.path, staging, error);
    files.emplace_back(formats::JsonValue{formats::JsonValue::Object{
        {"path", formats::JsonValue{relative.generic_string()}},
        {"sha256", formats::JsonValue{file.sha256}},
        {"frames", formats::JsonValue{static_cast<std::int64_t>(file.frames)}},
        {"channels", formats::JsonValue{static_cast<std::int64_t>(file.channels)}},
    }});
  }
  formats::JsonValue::Array voicebankEntries;
  formats::JsonValue::Array proceduralEntries;
  for (const auto& source : voicebanks) {
    const domain::SingerResourceIdentity* identity = nullptr;
    std::string style;
    domain::TrackId trackId;
    if (const auto* procedural = std::get_if<rendering::TrackProceduralSource>(&source)) {
      identity = &procedural->resource.identity; style = procedural->style; trackId = procedural->trackId;
    } else if (const auto* file = std::get_if<rendering::TrackRecipeFileSource>(&source)) {
      identity = &file->reference.resource; style = file->reference.style; trackId = file->trackId;
    }
    if (identity) proceduralEntries.emplace_back(formats::JsonValue::Object{
        {"trackId", formats::JsonValue{trackId.toString()}}, {"id", formats::JsonValue{identity->id}},
        {"version", formats::JsonValue{identity->version}}, {"contentHash", formats::JsonValue{identity->contentHash}},
        {"style", formats::JsonValue{style}}});
  }
  for (const auto& track : project.vocalTracks()) {
    if (std::any_of(voicebanks.begin(), voicebanks.end(), [&](const auto& source) {
        return !std::holds_alternative<rendering::TrackVoicebankSource>(source) &&
            std::visit([&](const auto& value) { return value.trackId == track.id; }, source);
      })) continue;
    voicebankEntries.emplace_back(formats::JsonValue{formats::JsonValue::Object{
        {"trackId", formats::JsonValue{track.id.toString()}},
        {"id", formats::JsonValue{track.voicebank.id}},
        {"version", formats::JsonValue{track.voicebank.version}},
        {"contentHash", formats::JsonValue{track.voicebank.contentHash}},
    }});
  }
  formats::JsonValue receiptValue{formats::JsonValue::Object{
          {"schemaVersion", formats::JsonValue{std::int64_t{2}}},
          {"state", formats::JsonValue{"PREPARED"}},
          {"transactionId", formats::JsonValue{transactionId}},
          {"projectId", formats::JsonValue{project.id().toString()}},
          {"projectSchema", formats::JsonValue{std::int64_t{
              formats::ProjectJsonCodec::kSchemaVersion}}},
          {"projectContentHash", formats::JsonValue{projectContentHash}},
          {"projectRevision", formats::JsonValue{static_cast<std::int64_t>(revision)}},
          {"sampleRate", formats::JsonValue{static_cast<std::int64_t>(settings.sampleRate)}},
          {"channels", formats::JsonValue{static_cast<std::int64_t>(settings.channels)}},
          {"format", formats::JsonValue{static_cast<std::int64_t>(
                                      static_cast<int>(settings.format))}},
          {"formatName", formats::JsonValue{wavFormatName(settings.format)}},
          {"renderQuality", formats::JsonValue{"Final"}},
          {"renderAbi", formats::JsonValue{std::string{build::kRenderAbiId}}},
          {"applicationBuildSha", formats::JsonValue{std::string{build::kSourceCommit}}},
          {"executionDateUnixMs", formats::JsonValue{executedAt}},
          {"voicebanks", formats::JsonValue{std::move(voicebankEntries)}},
          {"proceduralRecipes", formats::JsonValue{std::move(proceduralEntries)}},
          {"includesProjectAndRecipes", formats::JsonValue{settings.includeProjectAndRecipes || settings.includeProceduralCandidates}},
          {"includesProceduralCandidates", formats::JsonValue{settings.includeProceduralCandidates}},
          {"files", formats::JsonValue{std::move(files)}},
      }};
  const auto receiptText = formats::stringifyJson(receiptValue, true);
  const auto stagedReceipt = staging / "receipt.json";
  auto receiptWrite = core::durableAtomicWriteText(stagedReceipt, receiptText);
  if (!receiptWrite) {
    static_cast<void>(removeTree(staging));
    return core::Result<ExportResult>{receiptWrite.error()};
  }

  result.state = ExportState::Publishing;
  notifyProgress(progress, result.state, {}, completed, totalFiles);
  const auto previousExists = std::filesystem::exists(destination, error);
  if (error) {
    static_cast<void>(removeTree(staging));
    return core::failure<ExportResult>(core::ErrorCode::IoError,
                                       "Unable to inspect export destination",
                                       error.message());
  }
  if (previousExists) {
    const auto destinationStatus =
        std::filesystem::symlink_status(destination, error);
    if (error || std::filesystem::is_symlink(destinationStatus) ||
        !std::filesystem::is_directory(destinationStatus)) {
      static_cast<void>(removeTree(staging));
      return core::failure<ExportResult>(
          core::ErrorCode::Conflict,
          "Export destination must be a regular non-symlink directory",
          destination.string());
    }
    auto previousReceipt = readExportReceipt(destination, true);
    if (!previousReceipt || previousReceipt.value().state != "COMMITTED") {
      static_cast<void>(removeTree(staging));
      return core::failure<ExportResult>(
          core::ErrorCode::Conflict,
          "Existing export ownership cannot be established from its receipt",
          destination.string());
    }
    auto copied = copyUnownedFiles(
        destination, staging, previousReceipt.value().ownedPaths);
    if (!copied) {
      static_cast<void>(removeTree(staging));
      return core::Result<ExportResult>{copied.error()};
    }
  }
  const ExportJournal journal{
      .transactionId = transactionId,
      .staging = staging,
      .backup = backup,
  };
  auto journalWrite = writeJournal(
      transactionJournal, journal, "JOURNAL_PREPARED");
  if (!journalWrite) {
    static_cast<void>(removeTree(staging));
    return core::Result<ExportResult>{journalWrite.error()};
  }
  const auto interrupted = [&](ExportPublicationPhase phase) {
    if (!settings.publicationFaultInjector ||
        !settings.publicationFaultInjector(phase)) {
      return false;
    }
    result.state = ExportState::RollbackRequired;
    result.diagnostic = "Export publication interrupted at a journalled phase";
    return true;
  };
  if (interrupted(ExportPublicationPhase::JournalPrepared)) return result;
  const auto recoverFailure = [&](std::string diagnostic)
      -> core::Result<ExportResult> {
    auto recovered = recoverSet(destination);
    if (!recovered) {
      result.state = ExportState::RollbackRequired;
      result.diagnostic = std::move(diagnostic) + "; recovery failed: " +
                          recovered.error().message;
      return result;
    }
    return core::failure<ExportResult>(core::ErrorCode::IoError,
                                       std::move(diagnostic));
  };
  if (previousExists) {
    std::filesystem::rename(destination, backup, error);
    if (error) {
      return recoverFailure("Unable to preserve previous export set: " +
                            error.message());
    }
  }
  journalWrite = writeJournal(transactionJournal, journal, "PREVIOUS_MOVED");
  if (!journalWrite) {
    return recoverFailure("Unable to journal previous export preservation");
  }
  if (interrupted(ExportPublicationPhase::PreviousMoved)) return result;
  for (auto& file : result.files) {
    file.path = destination / file.path.lexically_relative(staging);
  }
  std::filesystem::rename(staging, destination, error);
  if (error) {
    return recoverFailure("Unable to publish prepared export set: " +
                          error.message());
  }
  journalWrite = writeJournal(
      transactionJournal, journal, "DESTINATION_PUBLISHED");
  if (!journalWrite) {
    return recoverFailure("Unable to journal export publication");
  }
  if (interrupted(ExportPublicationPhase::DestinationPublished)) return result;
  auto* receiptState = receiptValue.find("state");
  if (receiptState == nullptr) {
    return recoverFailure("Prepared export receipt lost its state");
  }
  *receiptState = formats::JsonValue{"COMMITTED"};
  receiptWrite = core::durableAtomicWriteText(
      destination / "receipt.json", formats::stringifyJson(receiptValue, true));
  if (!receiptWrite) {
    return recoverFailure("Unable to commit the published export receipt");
  }
  journalWrite = writeJournal(
      transactionJournal, journal, "RECEIPT_COMMITTED");
  if (!journalWrite) {
    return recoverFailure("Unable to journal committed export receipt");
  }
  if (interrupted(ExportPublicationPhase::ReceiptCommitted)) return result;
  if (previousExists) {
    auto removed = removeTree(backup);
    if (!removed) {
      result.state = ExportState::RollbackRequired;
      result.diagnostic = removed.error().message;
      return result;
    }
  }
  journalWrite = writeJournal(transactionJournal, journal, "BACKUP_REMOVED");
  if (!journalWrite) {
    result.state = ExportState::RollbackRequired;
    result.diagnostic = journalWrite.error().message;
    return result;
  }
  if (interrupted(ExportPublicationPhase::BackupRemoved)) return result;
  std::filesystem::remove(transactionJournal, error);
  if (error) {
    result.state = ExportState::RollbackRequired;
    result.diagnostic = "Committed export journal requires cleanup: " +
                        error.message();
    return result;
  }
  result.receiptPath = destination / "receipt.json";
  result.masterPath = destination / "master.wav";
  result.state = ExportState::Committed;
  notifyProgress(progress, result.state, {}, completed, totalFiles);
  return result;
}

core::Result<ExportResult> ExportService::recoverSet(
    const std::filesystem::path& destination) const {
  if (destination.empty()) {
    return core::failure<ExportResult>(core::ErrorCode::InvalidArgument,
                                       "Export recovery destination is empty");
  }
  const auto recoveredResult = [&]() -> core::Result<ExportResult> {
    ExportResult result{.state = ExportState::Recovered,
                        .masterPath = destination / "master.wav",
                        .receiptPath = destination / "receipt.json",
                        .setPath = destination};
    std::error_code resultError;
    const auto masterStatus =
        std::filesystem::symlink_status(result.masterPath, resultError);
    if (resultError == std::errc::no_such_file_or_directory ||
        !std::filesystem::exists(masterStatus)) {
      result.masterPath.clear();
      result.receiptPath.clear();
      return result;
    }
    if (resultError || std::filesystem::is_symlink(masterStatus) ||
        !std::filesystem::is_regular_file(masterStatus)) {
      return core::failure<ExportResult>(
          core::ErrorCode::Conflict,
          "Recovered master must be a regular non-symlink file",
          result.masterPath.string());
    }
    auto digest = core::sha256File(result.masterPath);
    if (!digest) return core::Result<ExportResult>{digest.error()};
    result.masterSha256 = digest.value();
    return result;
  };
  std::error_code error;
  const auto transactionJournal = journalPath(destination);
  const auto journalStatus =
      std::filesystem::symlink_status(transactionJournal, error);
  const auto hasJournal = !error && std::filesystem::exists(journalStatus);
  if (error == std::errc::no_such_file_or_directory) error.clear();
  if (error || std::filesystem::is_symlink(journalStatus)) {
    return core::failure<ExportResult>(
        core::ErrorCode::Conflict,
        "Export transaction journal is not a regular file",
        transactionJournal.string());
  }
  auto destinationStatus = std::filesystem::symlink_status(destination, error);
  const auto destinationExists =
      !error && std::filesystem::exists(destinationStatus);
  if (error == std::errc::no_such_file_or_directory) error.clear();
  if (error || std::filesystem::is_symlink(destinationStatus) ||
      (destinationExists &&
       !std::filesystem::is_directory(destinationStatus))) {
    return core::failure<ExportResult>(
        core::ErrorCode::Conflict,
        "Export recovery destination must be a regular non-symlink directory",
        destination.string());
  }
  if (!hasJournal) {
    if (!destinationExists) {
      return core::failure<ExportResult>(
          core::ErrorCode::NotFound,
          "No export set is available for recovery");
    }
    const auto masterStatus = std::filesystem::symlink_status(
        destination / "master.wav", error);
    if (std::filesystem::is_symlink(masterStatus)) {
      return core::failure<ExportResult>(
          core::ErrorCode::Conflict,
          "Recovered master must be a regular non-symlink file",
          (destination / "master.wav").string());
    }
    auto receipt = readExportReceipt(destination, true);
    if (!receipt || receipt.value().state != "COMMITTED") {
      return core::failure<ExportResult>(
          core::ErrorCode::Conflict,
          "Export recovery requires a valid committed receipt",
          destination.string());
    }
    return recoveredResult();
  }

  auto journal = readJournal(destination);
  if (!journal) return core::Result<ExportResult>{journal.error()};
  auto committed = destinationExists
                       ? readExportReceipt(destination, true)
                       : core::failure<ExportReceiptView>(
                             core::ErrorCode::NotFound,
                             "Published export does not exist");
  if (committed && committed.value().state == "COMMITTED") {
    for (const auto& owned : {journal.value().staging,
                              journal.value().backup}) {
      const auto status = std::filesystem::symlink_status(owned, error);
      if (error == std::errc::no_such_file_or_directory ||
          !std::filesystem::exists(status)) {
        error.clear();
        continue;
      }
      if (error || std::filesystem::is_symlink(status) ||
          !std::filesystem::is_directory(status)) {
        return core::failure<ExportResult>(
            core::ErrorCode::Conflict,
            "Journal-owned export path is not a regular directory",
            owned.string());
      }
      auto removed = removeTree(owned);
      if (!removed) return core::Result<ExportResult>{removed.error()};
    }
    std::filesystem::remove(transactionJournal, error);
    if (error) {
      return core::failure<ExportResult>(
          core::ErrorCode::IoError,
          "Unable to remove reconciled export journal", error.message());
    }
    return recoveredResult();
  }

  if (destinationExists) {
    auto prepared = readExportReceipt(destination, false);
    if (!prepared || prepared.value().state != "PREPARED" ||
        prepared.value().transactionId != journal.value().transactionId) {
      return core::failure<ExportResult>(
          core::ErrorCode::Conflict,
          "Uncommitted destination is not owned by the export journal",
          destination.string());
    }
    auto removed = removeTree(destination);
    if (!removed) return core::Result<ExportResult>{removed.error()};
  }
  const auto backupStatus =
      std::filesystem::symlink_status(journal.value().backup, error);
  const auto backupExists = !error && std::filesystem::exists(backupStatus);
  if (error == std::errc::no_such_file_or_directory) error.clear();
  if (error || std::filesystem::is_symlink(backupStatus) ||
      (backupExists && !std::filesystem::is_directory(backupStatus))) {
    return core::failure<ExportResult>(
        core::ErrorCode::Conflict,
        "Journal-owned backup is not a regular directory",
        journal.value().backup.string());
  }
  if (backupExists) {
    std::filesystem::rename(journal.value().backup, destination, error);
    if (error) {
      return core::failure<ExportResult>(
          core::ErrorCode::IoError,
          "Unable to restore journal-owned export backup", error.message());
    }
  }
  const auto stagingStatus =
      std::filesystem::symlink_status(journal.value().staging, error);
  const auto stagingExists = !error && std::filesystem::exists(stagingStatus);
  if (error == std::errc::no_such_file_or_directory) error.clear();
  if (error || std::filesystem::is_symlink(stagingStatus) ||
      (stagingExists && !std::filesystem::is_directory(stagingStatus))) {
    return core::failure<ExportResult>(
        core::ErrorCode::Conflict,
        "Journal-owned staging path is not a regular directory",
        journal.value().staging.string());
  }
  if (stagingExists) {
    auto removed = removeTree(journal.value().staging);
    if (!removed) return core::Result<ExportResult>{removed.error()};
  }
  std::filesystem::remove(transactionJournal, error);
  if (error) {
    return core::failure<ExportResult>(
        core::ErrorCode::IoError,
        "Unable to remove rolled-back export journal", error.message());
  }
  if (backupExists) {
    auto receipt = readExportReceipt(destination, true);
    if (!receipt || receipt.value().state != "COMMITTED") {
      return core::failure<ExportResult>(
          core::ErrorCode::Conflict,
          "Restored export backup does not have a valid committed receipt");
    }
  }
  return recoveredResult();
}

}
