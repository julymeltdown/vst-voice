#pragma once

#include "seam/domain/project.hpp"
#include "seam/formats/json_value.hpp"

#include <filesystem>
#include <string>

namespace seam::formats {

// What an encoding is for. The project file records the renderer that last produced this project's
// audio, but that record is not an acoustic input: it says which code made a sound, not what the
// sound is. Acoustic identity computation therefore excludes it, so recording provenance cannot
// change the identity that decides cache reuse. One encoder serves both callers, so the two cannot
// drift into disagreeing about anything else.
struct ProjectJsonEncodeOptions final {
  // True when writing a document for a reader: the file keeps the recorded renderer. False when
  // deriving an identity for audio: a stamp must not perturb the identity it is only describing.
  bool includeRendererProvenance{true};
};

class ProjectJsonCodec final {
public:
  // Schema 20 adds a nullable lyric reading hint, separate from visible text
  // and note-level phonetic phone hints.
  static constexpr std::int32_t kSchemaVersion = 20;

  [[nodiscard]] core::Result<std::string> encode(const domain::Project& project) const;
  [[nodiscard]] core::Result<std::string> encode(
      const domain::Project& project, ProjectJsonEncodeOptions options) const;
  [[nodiscard]] core::Result<domain::Project> decode(std::string_view json) const;
  [[nodiscard]] core::Result<void> save(const domain::Project& project,
                                        const std::filesystem::path& path) const;
  [[nodiscard]] core::Result<domain::Project> load(const std::filesystem::path& path) const;
};

}  // namespace seam::formats
