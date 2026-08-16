#include "seam/rendering/phrase_segmenter.hpp"

#include "seam/core/stable_hash.hpp"

#include <algorithm>

namespace seam::rendering {
namespace {

std::string makeId(const domain::VocalRegion& region,
                   time::Tick start,
                   time::Tick end,
                   const std::vector<domain::NoteId>& noteIds) {
  core::StableHash64 hash;
  hash.add(region.id.value());
  hash.add(start.value());
  hash.add(end.value());
  for (const auto noteId : noteIds) hash.add(noteId.value());
  return "phrase-" + hash.hex();
}

}  // namespace

core::Result<std::vector<PhraseSegment>> PhraseSegmenter::segment(
    const domain::VocalRegion& region,
    PhraseSegmentationConfig config) const {
  const auto regionValidation = region.validate();
  if (!regionValidation) {
    return core::Result<std::vector<PhraseSegment>>{regionValidation.error()};
  }
  if (config.splitRest.value() < 0 || config.maximumDuration.value() <= 0) {
    return core::failure<std::vector<PhraseSegment>>(
        core::ErrorCode::InvalidArgument,
        "Phrase segmentation configuration is invalid");
  }
  std::vector<const domain::Note*> notes;
  notes.reserve(region.notes.size());
  for (const auto& note : region.notes) notes.push_back(&note);
  std::stable_sort(notes.begin(), notes.end(), [](const auto* lhs, const auto* rhs) {
    if (lhs->startTick == rhs->startTick) return lhs->id < rhs->id;
    return lhs->startTick < rhs->startTick;
  });
  if (notes.empty()) return std::vector<PhraseSegment>{};

  std::vector<PhraseSegment> result;
  time::Tick currentStart = notes.front()->startTick;
  time::Tick currentEnd = notes.front()->endTick();
  std::vector<domain::NoteId> currentNotes{notes.front()->id};

  const auto flush = [&]() {
    result.push_back(PhraseSegment{
        .id = makeId(region, currentStart, currentEnd, currentNotes),
        .regionId = region.id,
        .startTick = currentStart,
        .endTick = currentEnd,
        .noteIds = currentNotes,
    });
  };

  for (std::size_t index = 1; index < notes.size(); ++index) {
    const auto* note = notes[index];
    const auto gap = note->startTick - currentEnd;
    const auto proposedEnd = std::max(currentEnd, note->endTick());
    const bool restSplit = gap >= config.splitRest && gap.value() > 0;
    const bool durationSplit = proposedEnd - currentStart > config.maximumDuration;
    if (restSplit || durationSplit) {
      flush();
      currentStart = note->startTick;
      currentEnd = note->endTick();
      currentNotes.clear();
      currentNotes.push_back(note->id);
      continue;
    }
    currentEnd = proposedEnd;
    currentNotes.push_back(note->id);
  }
  flush();
  return result;
}

std::vector<std::string> DirtyPhraseInvalidator::affected(
    const std::vector<PhraseSegment>& segments,
    time::Tick changedStart,
    time::Tick changedEnd,
    bool includeNeighbors) const {
  if (changedEnd < changedStart) std::swap(changedStart, changedEnd);
  std::vector<std::size_t> indices;
  for (std::size_t index = 0; index < segments.size(); ++index) {
    const auto& segment = segments[index];
    if (segment.endTick >= changedStart && segment.startTick <= changedEnd) {
      indices.push_back(index);
    }
  }
  if (includeNeighbors && !indices.empty()) {
    const auto first = indices.front();
    const auto last = indices.back();
    if (first > 0) indices.push_back(first - 1U);
    if (last + 1U < segments.size()) indices.push_back(last + 1U);
  }
  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
  std::vector<std::string> result;
  result.reserve(indices.size());
  for (const auto index : indices) result.push_back(segments[index].id);
  return result;
}

}  // namespace seam::rendering
