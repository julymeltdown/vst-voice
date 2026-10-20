#pragma once

// Reviews name a note the way a creator counts it: its place in the region, its bar and beat and
// its lyric. Rehearsal 2026-10-20 found the distribution, cleanup and dependency reviews listing
// raw 16-digit ids, which say nothing on screen.

#include "seam/domain/note.hpp"
#include "seam/domain/phoneme.hpp"
#include "seam/domain/project.hpp"

#include <cstddef>
#include <string>

namespace seam::native_ui::review_labels {

inline std::string note(const domain::Project& project, domain::RegionId regionId,
                        domain::NoteId noteId) {
  const auto* region = project.findRegion(regionId);
  if (region == nullptr) return "Note in a closed region";
  const domain::Note* found = nullptr;
  for (const auto& candidate : region->notes)
    if (candidate.id == noteId) found = &candidate;
  if (found == nullptr) return "Removed note";
  std::size_t earlier = 0U;
  for (const auto& candidate : region->notes)
    if (candidate.startTick < found->startTick ||
        (candidate.startTick == found->startTick && candidate.id < found->id))
      ++earlier;
  const auto position = project.meterMap().barBeatAt(region->startTick + found->startTick);
  std::string label = "Note " + std::to_string(earlier + 1U) + " (bar " +
                      std::to_string(position.bar) + ", beat " + std::to_string(position.beat);
  for (const auto& lyric : region->lyrics)
    if (lyric.id == found->lyricTokenId && !lyric.surface.empty())
      label += ", " + domain::toUtf8(lyric.surface);
  return label + ")";
}

inline std::string lyric(const domain::Project& project, domain::RegionId regionId,
                         domain::LyricTokenId lyricId) {
  if (const auto* region = project.findRegion(regionId))
    for (const auto& candidate : region->notes)
      if (candidate.lyricTokenId == lyricId) return note(project, regionId, candidate.id);
  return "Lyric without a note";
}

inline std::string sound(const domain::Project& project, domain::RegionId regionId,
                         const domain::PhonemeKey& key) {
  return note(project, regionId, key.noteId) + " sound " + std::to_string(key.ordinal + 1U);
}

}  // namespace seam::native_ui::review_labels
