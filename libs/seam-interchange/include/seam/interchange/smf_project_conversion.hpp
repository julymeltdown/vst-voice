#pragma once

#include "seam/application/project_factory.hpp"
#include "seam/interchange/smf_codec.hpp"

namespace seam::interchange {

struct SmfImportRequest final {
  std::string projectName{"Imported MIDI"};
  std::string trackName{"MIDI Track"};
  std::string regionName{"MIDI Phrase"};
  domain::Language language{domain::Language::Unspecified};
};

struct SmfProjectDraft final {
  domain::Project project;
  SmfScore score;
};

// Builds an inert unsaved SEAM project from a decoded SMF score. IDs are
// allocated by the caller's factory; no current EditorSession is touched.
[[nodiscard]] core::Result<SmfProjectDraft> importSmfProject(
    std::span<const std::uint8_t> bytes, application::ProjectFactory& factory,
    SmfImportRequest request = {}, SmfLimits limits = {});

// Converts one existing SEAM track/region into a bounded score. Musical note,
// lyric, tempo and meter data are retained; unsupported SEAM-only controls are
// reported as explicit loss records in the returned score.
[[nodiscard]] core::Result<SmfScore> exportSmfProject(
    const domain::Project& project, domain::TrackId trackId,
    domain::RegionId regionId, SmfLimits limits = {});

}  // namespace seam::interchange
