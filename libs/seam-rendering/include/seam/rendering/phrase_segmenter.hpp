#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"

#include <string>
#include <vector>

namespace seam::rendering {

struct PhraseSegment final {
  std::string id;
  domain::RegionId regionId;
  time::Tick startTick;
  time::Tick endTick;
  std::vector<domain::NoteId> noteIds;

  [[nodiscard]] time::Tick duration() const noexcept { return endTick - startTick; }
  friend bool operator==(const PhraseSegment&, const PhraseSegment&) = default;
};

struct PhraseSegmentationConfig final {
  time::Tick splitRest{time::Tick{time::kDefaultPpq}};
  time::Tick maximumDuration{time::Tick{time::kDefaultPpq * 16}};
};

class PhraseSegmenter final {
public:
  [[nodiscard]] core::Result<std::vector<PhraseSegment>> segment(
      const domain::VocalRegion& region,
      PhraseSegmentationConfig config = {}) const;
};

class DirtyPhraseInvalidator final {
public:
  [[nodiscard]] std::vector<std::string> affected(
      const std::vector<PhraseSegment>& segments,
      time::Tick changedStart,
      time::Tick changedEnd,
      bool includeNeighbors = true) const;
};

}  // namespace seam::rendering
