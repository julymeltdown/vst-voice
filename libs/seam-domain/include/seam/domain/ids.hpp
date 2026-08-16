#pragma once

#include "seam/core/id.hpp"

namespace seam::domain {

struct ProjectTag;
struct TrackTag;
struct RegionTag;
struct NoteTag;
struct LyricTag;
struct AudioClipTag;
struct BusTag;

using ProjectId = core::Id<ProjectTag>;
using TrackId = core::Id<TrackTag>;
using RegionId = core::Id<RegionTag>;
using NoteId = core::Id<NoteTag>;
using LyricTokenId = core::Id<LyricTag>;
using AudioClipId = core::Id<AudioClipTag>;
using BusId = core::Id<BusTag>;

}  // namespace seam::domain
