#pragma once

#include "seam/core/result.hpp"
#include "seam/distribution/procedural_package.hpp"
#include "seam/domain/project.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace seam::native_ui {

// Studio's explicit return-to-song step after a Designer singer has been installed. The song editor
// could already offer the singer on its New Project screen, but only if the creator knew to go there
// and find it again. This writes a new song project whose one vocal track is bound to that exact
// installed singer, so the editor opens straight into a song that sings with the voice just made.
struct InstalledSingerSongRequest final {
  // Absolute path of a project file that must not exist yet. An existing project is never replaced,
  // even when a save panel already asked the user about replacing it.
  std::filesystem::path projectPath;
  // The style to sing with when the installed singer declares it. Otherwise the first declared style
  // is used and reported in the result, never an undeclared one.
  std::string preferredStyle;
  // The procedural engine the song editor renders with. The singer must resolve for this engine,
  // exactly as the editor's own singer picker requires, or no project is written.
  std::string renderableEngineId;
  std::uint32_t renderableEngineRevision{0U};
};

struct InstalledSingerSongProject final {
  std::filesystem::path projectPath;
  std::string projectName;
  domain::ProceduralRecipeReference singer;
  std::string projectSha256;
};

// Re-scans the given installed-singer roots, requires the singer installed by this session to still
// be present with the same content, trusted, and renderable, then publishes a new 120 BPM 4/4 48 kHz
// stereo song project bound to it. The reference is the one the editor's New Project screen records
// for the same singer. Nothing is written on refusal, and writing into an installed singer folder is
// refused because signed installations are immutable.
[[nodiscard]] core::Result<InstalledSingerSongProject> createInstalledSingerSongProject(
    const distribution::InstalledProceduralSinger& installed,
    const std::vector<distribution::ProceduralSearchRoot>& roots,
    const InstalledSingerSongRequest& request);

// A file name suggestion such as "Aoi Song.seam" that is safe to offer in a save panel: path
// separators and control characters become hyphens, leading dots are removed so the file is not
// hidden, and the stem is bounded without splitting a UTF-8 sequence.
[[nodiscard]] std::string suggestedSongProjectFileName(std::string_view singerDisplayName);

}  // namespace seam::native_ui
