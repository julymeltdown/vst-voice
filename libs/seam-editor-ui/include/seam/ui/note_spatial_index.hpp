#pragma once

#include "seam/domain/project.hpp"
#include "seam/ui/geometry.hpp"
#include "seam/ui/timeline_transform.hpp"

#include <cstddef>
#include <vector>

namespace seam::ui {

struct IndexedNote final {
  domain::NoteId noteId;
  domain::RegionId regionId;
  time::Tick absoluteStart;
  time::Tick absoluteEnd;
  std::uint8_t midiKey{60};
};

class NoteSpatialIndex final {
public:
  void rebuild(const domain::Project& project);

  [[nodiscard]] std::vector<IndexedNote> query(
      time::Tick start,
      time::Tick end,
      std::int32_t lowestMidi,
      std::int32_t highestMidi) const;

  [[nodiscard]] std::size_t size() const noexcept { return notes_.size(); }

private:
  std::vector<IndexedNote> notes_;
  std::vector<time::Tick> prefixMaximumEnd_;
};

}  // namespace seam::ui
