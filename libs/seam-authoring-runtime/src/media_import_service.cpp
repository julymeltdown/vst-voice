#include "seam/authoring/media_import_service.hpp"

#include "seam/rendering/streaming_pcm_source.hpp"

#include <array>
#include <cctype>
#include <fstream>
#include <utility>

namespace seam::authoring {
namespace {

bool isWavPath(const std::filesystem::path& path) {
  auto extension = path.extension().string();
  for (auto& character : extension) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return extension == ".wav";
}

core::Result<void> copyFile(const std::filesystem::path& source,
                            const std::filesystem::path& destination) {
  std::ifstream input(source, std::ios::binary);
  if (!input) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to open backing media for copy",
                         source.string());
  }
  std::ofstream output(destination, std::ios::binary | std::ios::trunc);
  if (!output) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to stage backing media copy",
                         destination.string());
  }
  std::array<char, 64U * 1024U> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = input.gcount();
    if (count > 0) {
      output.write(buffer.data(), count);
      if (!output) {
        return core::failure(core::ErrorCode::IoError,
                             "Unable to write staged backing media copy",
                             destination.string());
      }
    }
  }
  if (!input.eof()) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to read backing media during copy",
                         source.string());
  }
  output.flush();
  if (!output) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to flush staged backing media copy",
                         destination.string());
  }
  return core::success();
}

}

core::Result<MediaImportResult> MediaImportService::import(
    const MediaImportRequest& request) {
  if (!request.trackId.valid() || request.sourcePath.empty()) {
    return core::failure<MediaImportResult>(
        core::ErrorCode::InvalidArgument,
        "Media import requires a valid track and source path");
  }
  if (!isWavPath(request.sourcePath)) {
    return core::failure<MediaImportResult>(
        core::ErrorCode::Unsupported,
        "Only PCM WAV backing media is supported by External Beta");
  }
  std::error_code error;
  const auto source = std::filesystem::weakly_canonical(request.sourcePath, error);
  if (error || !std::filesystem::is_regular_file(source, error)) {
    return core::failure<MediaImportResult>(
        core::ErrorCode::NotFound,
        "Backing media source is not a regular file",
        request.sourcePath.string());
  }
  auto audio = rendering::StreamingPcmSource::open(source, 4096U);
  if (!audio) return core::Result<MediaImportResult>{audio.error()};
  const auto& info = audio.value()->info();

  const auto filename = source.filename().string();
  domain::AudioTrack track{
      .id = request.trackId,
      .name = request.trackName.empty() ? filename : request.trackName,
      .mediaPath = source.string(),
      .mediaHash = info.contentHash,
      .mediaOwnership = domain::MediaOwnership::ExternalReference,
      .originalFilename = filename,
      .sourceSampleRate = info.sampleRate,
      .sourceChannels = info.channels,
      .sourceFrameCount = info.frameCount,
      .startTick = request.startTick,
  };
  if (request.mode == MediaImportMode::Reference) {
    return MediaImportResult{.track = std::move(track), .ownedPath = {}};
  }
  if (request.projectPath.empty()) {
    return core::failure<MediaImportResult>(
        core::ErrorCode::InvalidArgument,
        "Copying backing media requires a project path");
  }

  const auto mediaRoot = request.projectPath.string() + ".media";
  const auto destinationRoot = std::filesystem::path{mediaRoot};
  std::filesystem::create_directories(destinationRoot, error);
  if (error) {
    return core::failure<MediaImportResult>(core::ErrorCode::IoError,
                                            "Unable to create project media directory",
                                            error.message());
  }
  const auto destination = destinationRoot / (info.contentHash.substr(0U, 16U) + "-" + filename);
  const auto staging = destinationRoot / (".staging-" + info.contentHash);
  std::filesystem::remove(staging, error);
  auto copied = copyFile(source, staging);
  if (!copied) {
    std::filesystem::remove(staging, error);
    return core::Result<MediaImportResult>{copied.error()};
  }
  if (std::filesystem::exists(destination, error)) {
    auto existing = rendering::StreamingPcmSource::open(destination, 4096U);
    if (!existing || existing.value()->info().contentHash != info.contentHash) {
      std::filesystem::remove(staging, error);
      return core::failure<MediaImportResult>(
          core::ErrorCode::Conflict,
          "Project-owned backing media path already contains different content",
          destination.string());
    }
    std::filesystem::remove(staging, error);
    const auto projectParent = request.projectPath.parent_path();
    const auto relative = std::filesystem::relative(destination, projectParent, error);
    track.mediaPath = (error ? destination : relative).generic_string();
    track.mediaOwnership = domain::MediaOwnership::ProjectCopy;
    return MediaImportResult{.track = std::move(track), .ownedPath = destination};
  }
  std::filesystem::rename(staging, destination, error);
  if (error) {
    std::filesystem::remove(staging, error);
    return core::failure<MediaImportResult>(core::ErrorCode::IoError,
                                            "Unable to publish project-owned media",
                                            destination.string());
  }
  const auto projectParent = request.projectPath.parent_path();
  const auto relative = std::filesystem::relative(destination, projectParent, error);
  track.mediaPath = (error ? destination : relative).generic_string();
  track.mediaOwnership = domain::MediaOwnership::ProjectCopy;
  return MediaImportResult{.track = std::move(track), .ownedPath = destination};
}

}
