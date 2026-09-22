#include "seam/authoring/interchange_service.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"

#include <algorithm>
#include <cctype>
#include <span>
#include <string_view>
#include <system_error>

namespace seam::authoring {
namespace {

core::Result<std::filesystem::path> normalizedPath(
    const std::filesystem::path& path) {
  if (path.empty()) return core::failure<std::filesystem::path>(core::ErrorCode::InvalidArgument, "Interchange path cannot be empty");
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error && error != std::errc::no_such_file_or_directory) return core::failure<std::filesystem::path>(core::ErrorCode::IoError, "Unable to inspect interchange path", error.message());
  if (!error && status.type() == std::filesystem::file_type::symlink) return core::failure<std::filesystem::path>(core::ErrorCode::Conflict, "Interchange path cannot be a symbolic link", path.string());
  const auto absolute = std::filesystem::absolute(path, error);
  if (error) return core::failure<std::filesystem::path>(core::ErrorCode::IoError, "Unable to resolve interchange path", error.message());
  const auto normalized = absolute.lexically_normal();
  // Parent-path policy: canonicalize the parent chain so intermediate
  // symlinks resolve deterministically to the real directory instead of
  // silently redirecting a held path. The leaf itself is still rejected
  // when it is a symlink (checked above and again by the no-follow open),
  // and the recorded sourcePath is the canonical real location.
  const auto parent = normalized.parent_path();
  std::error_code canonicalError;
  const auto canonicalParent = parent.empty()
      ? normalized
      : std::filesystem::weakly_canonical(parent, canonicalError);
  if (canonicalError) {
    return core::failure<std::filesystem::path>(core::ErrorCode::IoError,
        "Unable to canonicalize interchange parent", canonicalError.message());
  }
  const auto resolved = parent.empty()
      ? normalized
      : (canonicalParent / normalized.filename()).lexically_normal();
  // Re-check the resolved leaf: canonicalization must not smuggle a symlink
  // back into the admitted path.
  std::error_code resolvedError;
  const auto resolvedStatus = std::filesystem::symlink_status(resolved, resolvedError);
  if (!resolvedError && resolvedStatus.type() == std::filesystem::file_type::symlink) {
    return core::failure<std::filesystem::path>(core::ErrorCode::Conflict,
        "Interchange path cannot be a symbolic link", resolved.string());
  }
  return resolved;
}

std::string lowerExtension(const std::filesystem::path& path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
  return extension;
}

core::Result<void> admitReportSize(std::size_t current, std::size_t additional) {
  constexpr std::size_t maximumIssues = 4'096U;
  if (current > maximumIssues || additional > maximumIssues - current)
    return core::failure(core::ErrorCode::Unsupported,
        "Interchange diagnostic report exceeds its capacity; conversion would conceal losses");
  return core::success();
}

core::Result<void> appendUstx(std::vector<InterchangeIssue>& output,
                            const std::vector<interchange::UstxIssue>& issues) {
  const auto admitted = admitReportSize(output.size(), issues.size());
  if (!admitted) return admitted;
  for (const auto& item : issues) output.push_back({InterchangeFormat::Ustx,
      item.severity == interchange::UstxIssueSeverity::Loss,
      item.path, item.message});
  return core::success();
}

core::Result<void> appendSmf(std::vector<InterchangeIssue>& output,
                           const std::vector<interchange::SmfIssue>& issues) {
  const auto admitted = admitReportSize(output.size(), issues.size());
  if (!admitted) return admitted;
  for (const auto& item : issues) output.push_back({InterchangeFormat::Smf,
      item.severity == interchange::SmfIssueSeverity::Loss,
      "tick:" + std::to_string(item.tick.value()), item.message});
  return core::success();
}

}  // namespace

core::Result<InterchangeImportDraft> InterchangeService::importFile(
    const std::filesystem::path& source, application::ProjectFactory& factory,
    InterchangeImportRequest request, interchange::UstxLimits ustxLimits,
    interchange::SmfLimits smfLimits,
    const core::HeldReadFaultInjector& readFaultInjector) const {
  using Output = InterchangeImportDraft;
  auto path = normalizedPath(source);
  if (!path) return core::Result<Output>{path.error()};
  const auto extension = lowerExtension(path.value());
  if (extension == ".ustx") request.format = InterchangeFormat::Ustx;
  else if (extension == ".mid" || extension == ".midi") request.format = InterchangeFormat::Smf;
  else return core::failure<Output>(core::ErrorCode::Unsupported, "Unsupported interchange file extension", extension);
  const auto maximumBytes = request.format == InterchangeFormat::Ustx ? ustxLimits.maximumInputBytes : smfLimits.maximumBytes;
  const auto sourceBytes = core::readFileBytesLimited(path.value(), maximumBytes, readFaultInjector);
  if (!sourceBytes) return core::Result<Output>{sourceBytes.error()};
  const auto sourceHash = core::sha256Hex(std::span<const std::byte>{sourceBytes.value().data(), sourceBytes.value().size()});
  if (request.format == InterchangeFormat::Ustx) {
    const auto view = std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(sourceBytes.value().data()), sourceBytes.value().size()};
    auto imported = interchange::importUstxProject(view, factory,
        interchange::UstxImportRequest{.projectName = std::move(request.projectName), .voicebankId = std::move(request.voicebankId), .voicebankVersion = std::move(request.voicebankVersion), .voicebankContentHash = std::move(request.voicebankContentHash), .characterId = std::move(request.characterId), .characterVersion = std::move(request.characterVersion), .language = request.language}, ustxLimits);
    if (!imported) return core::Result<Output>{imported.error()};
    auto importedValue = std::move(imported).value();
    Output result{std::move(importedValue.project), InterchangeFormat::Ustx,
                  path.value(), sourceHash, {}};
    const auto report = appendUstx(result.issues, importedValue.issues);
    if (!report) return core::Result<Output>{report.error()};
    return result;
  }
  const auto view = std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(sourceBytes.value().data()), sourceBytes.value().size()};
  auto imported = interchange::importSmfProject(view, factory,
      interchange::SmfImportRequest{.projectName = std::move(request.projectName), .trackName = "MIDI Track", .regionName = "MIDI Phrase", .language = request.language}, smfLimits);
  if (!imported) return core::Result<Output>{imported.error()};
  auto importedValue = std::move(imported).value();
  Output result{std::move(importedValue.project), InterchangeFormat::Smf,
                path.value(), sourceHash, {}};
  const auto report = appendSmf(result.issues, importedValue.score.issues);
  if (!report) return core::Result<Output>{report.error()};
  return result;
}

core::Result<InterchangeExportReceipt> InterchangeService::exportFile(
    const domain::Project& project, InterchangeExportRequest request,
    interchange::UstxLimits ustxLimits, interchange::SmfLimits smfLimits) const {
  using Output = InterchangeExportReceipt;
  auto path = normalizedPath(request.destination);
  if (!path) return core::Result<Output>{path.error()};
  if (request.destination.empty()) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Interchange destination cannot be empty");
  std::vector<InterchangeIssue> issues;
  if (request.format == InterchangeFormat::Ustx) {
    auto exported = interchange::exportUstxProject(project, ustxLimits);
    if (!exported) return core::Result<Output>{exported.error()};
    const auto report = appendUstx(issues, exported.value().issues);
    if (!report) return core::Result<Output>{report.error()};
    const auto written = core::durableAtomicWriteNew(path.value(), std::as_bytes(std::span{exported.value().bytes.data(), exported.value().bytes.size()}));
    if (!written) return core::Result<Output>{written.error()};
    return Output{InterchangeFormat::Ustx, path.value(), core::sha256Hex(std::span<const std::byte>{reinterpret_cast<const std::byte*>(exported.value().bytes.data()), exported.value().bytes.size()}), std::move(issues)};
  }
  domain::TrackId trackId{};
  domain::RegionId regionId{};
  if (request.trackId.has_value()) trackId = *request.trackId;
  else if (!project.vocalTracks().empty()) trackId = project.vocalTracks().front().id;
  if (request.regionId.has_value()) regionId = *request.regionId;
  else if (const auto* track = project.findVocalTrack(trackId); track && !track->regions.empty()) regionId = track->regions.front().id;
  if (!trackId.valid() || !regionId.valid()) return core::failure<Output>(core::ErrorCode::InvalidArgument, "SMF export requires a vocal track and region");
  auto exported = interchange::exportSmfProject(project, trackId, regionId, smfLimits);
  if (!exported) return core::Result<Output>{exported.error()};
  const auto report = appendSmf(issues, exported.value().issues);
  if (!report) return core::Result<Output>{report.error()};
  auto encoded = interchange::encodeSmf(exported.value(), smfLimits);
  if (!encoded) return core::Result<Output>{encoded.error()};
  const auto written = core::durableAtomicWriteNew(path.value(), std::as_bytes(std::span{encoded.value().data(), encoded.value().size()}));
  if (!written) return core::Result<Output>{written.error()};
  return Output{InterchangeFormat::Smf, path.value(), core::sha256Hex(std::span<const std::byte>{reinterpret_cast<const std::byte*>(encoded.value().data()), encoded.value().size()}), std::move(issues)};
}

}  // namespace seam::authoring
