#pragma once

#include "seam/core/id.hpp"

namespace seam::domain {

struct ProjectTag;
struct TrackTag;
struct RegionTag;
struct NoteTag;
struct LyricTag;
struct AudioClipTag;

using ProjectId = core::Id<ProjectTag>;
using TrackId = core::Id<TrackTag>;
using RegionId = core::Id<RegionTag>;
using NoteId = core::Id<NoteTag>;
using LyricTokenId = core::Id<LyricTag>;
using AudioClipId = core::Id<AudioClipTag>;

}  // namespace seam::domain
