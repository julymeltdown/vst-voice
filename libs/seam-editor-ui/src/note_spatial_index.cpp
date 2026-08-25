#include "seam/ui/note_spatial_index.hpp"

#include <algorithm>

namespace seam::ui {

void NoteSpatialIndex::rebuild(const domain::Project& project) {
  notes_.clear();
  prefixMaximumEnd_.clear();
  notes_.reserve(project.noteCount());
  for (const auto& track : project.vocalTracks()) {
    for (const auto& region : track.regions) {
      for (const auto& note : region.notes) {
        notes_.push_back(IndexedNote{
            .noteId = note.id,
            .regionId = region.id,
            .absoluteStart = region.startTick + note.startTick,
            .absoluteEnd = region.startTick + note.endTick(),
            .midiKey = note.midiKey,
        });
      }
    }
  }
  std::stable_sort(notes_.begin(), notes_.end(), [](const IndexedNote& lhs, const IndexedNote& rhs) {
    if (lhs.absoluteStart == rhs.absoluteStart) {
      return lhs.noteId < rhs.noteId;
    }
    return lhs.absoluteStart < rhs.absoluteStart;
  });

  prefixMaximumEnd_.resize(notes_.size());
  for (std::size_t index = 0; index < notes_.size(); ++index) {
    const auto& absoluteEnd = notes_[index].absoluteEnd;
    prefixMaximumEnd_[index] = index == 0
        ? absoluteEnd
        : std::max(prefixMaximumEnd_[index - 1], absoluteEnd);
  }
}

std::vector<IndexedNote> NoteSpatialIndex::query(
    time::Tick start,
    time::Tick end,
    std::int32_t lowestMidi,
    std::int32_t highestMidi) const {
  if (end < start) {
    std::swap(start, end);
  }
  if (highestMidi < lowestMidi) {
    std::swap(lowestMidi, highestMidi);
  }
  lowestMidi = std::clamp(lowestMidi, 0, 127);
  highestMidi = std::clamp(highestMidi, 0, 127);

  const auto limit = std::lower_bound(notes_.begin(), notes_.end(), end,
      [](const IndexedNote& note, time::Tick tick) { return note.absoluteStart < tick; });
  const auto limitOffset = std::distance(notes_.begin(), limit);
  const auto first = std::upper_bound(
      prefixMaximumEnd_.begin(), prefixMaximumEnd_.begin() + limitOffset, start);
  const auto firstOffset = std::distance(prefixMaximumEnd_.begin(), first);
  std::vector<IndexedNote> result;
  result.reserve(static_cast<std::size_t>(limitOffset - firstOffset));
  for (auto iterator = notes_.begin() + firstOffset; iterator != limit; ++iterator) {
    if (iterator->absoluteEnd <= start) {
      continue;
    }
    if (iterator->midiKey < lowestMidi || iterator->midiKey > highestMidi) {
      continue;
    }
    result.push_back(*iterator);
  }
  return result;
}

}  // namespace seam::ui
