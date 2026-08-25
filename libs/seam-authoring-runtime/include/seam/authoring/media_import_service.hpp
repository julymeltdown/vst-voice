#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"

#include <filesystem>
#include <string>

namespace seam::authoring {

enum class MediaImportMode { Reference, Copy };

struct MediaImportRequest final {
  domain::TrackId trackId;
  std::string trackName;
  std::filesystem::path sourcePath;
  std::filesystem::path projectPath;
  time::Tick startTick;
  MediaImportMode mode{MediaImportMode::Reference};
};

struct MediaImportResult final {
  domain::AudioTrack track;
  std::filesystem::path ownedPath;
};

class MediaImportService final {
public:
  [[nodiscard]] static core::Result<MediaImportResult> import(
      const MediaImportRequest& request);
};

}
