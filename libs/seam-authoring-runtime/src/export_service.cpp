#include "seam/authoring/export_service.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"

#include <chrono>
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
  }
  return "unknown";
}

core::Result<ExportResult> ExportService::exportProject(
    const domain::Project& project,
    std::span<const rendering::TrackVoicebankSource> voicebanks,
    domain::TrackId activeTrack, domain::RegionId activeRegion,
    std::uint64_t revision, const std::filesystem::path& destination,
    voicebank::WavSampleFormat format, std::stop_token stopToken) const {
  rendering::ProductionProjectRenderer renderer;
  const auto rendered = renderer.render(
      project, voicebanks, activeTrack, activeRegion, revision,
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
  auto cleaned = removeTree(staging);
  if (!cleaned) return core::Result<ExportResult>{cleaned.error()};
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
  if (std::filesystem::exists(destination, error) && !error) {
    std::filesystem::rename(destination, backup, error);
    if (error) {
      static_cast<void>(removeTree(staging));
      return core::failure<ExportResult>(core::ErrorCode::IoError,
                                         "Unable to preserve previous export",
                                         error.message());
    }
  }
  std::filesystem::rename(stagedMaster, destination, error);
  if (error) {
    if (std::filesystem::exists(backup)) std::filesystem::rename(backup, destination);
    static_cast<void>(removeTree(staging));
    return core::failure<ExportResult>(core::ErrorCode::IoError,
                                       "Unable to publish exported master",
                                       error.message());
  }
  if (std::filesystem::exists(receiptPath, error) && !error) {
    std::filesystem::rename(receiptPath, receiptBackup, error);
    if (error) return core::failure<ExportResult>(core::ErrorCode::IoError, "Unable to preserve previous export receipt", error.message());
  }
  std::filesystem::rename(stagedReceipt, receiptPath, error);
  if (error) {
    static_cast<void>(removeTree(staging));
    return core::failure<ExportResult>(core::ErrorCode::IoError,
                                       "Unable to publish export receipt",
                                       error.message());
  }
  static_cast<void>(removeTree(staging));
  result.state = ExportState::Committed;
  result.receiptPath = receiptPath;
  return result;
}

}
