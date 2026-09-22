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
  // Original source PPQ/positions, with conversion diagnostics appended using
  // source-coordinate ticks. The project uses SEAM's default PPQ instead.
  SmfScore score;
};

// Builds an inert unsaved SEAM project from a decoded SMF score. IDs are
// allocated by the caller's factory; no current EditorSession is touched.
// Absolute positions are rescaled with checked rational arithmetic, rounding
// to nearest tick (half up) with explicit warnings. Distinct positions that
// collide after rounding are refused, never coalesced or stretched. Tick limits
// apply to both source and normalized project coordinates. Timing admission
// completes before project IDs are allocated; lyrics keep source-note pairing.
// Unsupported velocity/channel values and unused text/lyric events are disclosed
// by exact-count, source-tick-range loss summaries. Velocity 100/channel 1 are
// the neutral defaults retained by the fixed-value SMF export path.
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
