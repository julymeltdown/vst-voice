#include "seam/rendering/phrase_segmenter.hpp"

#include "seam/core/stable_hash.hpp"
#include "seam/phonemizer/language_resolver.hpp"

#include <algorithm>
#include <unordered_map>

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

  // Cuts inside an active relationship are forbidden. Overlapping dependencies
  // naturally form larger atomic groups; consider their full extent before
  // choosing a duration split, not only the next note's end.
  std::vector<bool> joined(notes.size(), false);
  for (std::size_t i = 1U; i < notes.size(); ++i) {
    joined[i] = domain::continuesSharedLyric(*notes[i - 1U], *notes[i]);
  }
  const bool dependencies = std::any_of(region.unitSelectionOverrides.begin(), region.unitSelectionOverrides.end(), [](const auto& edit) { return !edit.unresolved; }) ||
      std::any_of(region.seamOverrides.begin(), region.seamOverrides.end(), [](const auto& edit) { return !edit.unresolved; });
  if (dependencies) {
    if (region.unitSelectionOverrides.size() > 4096U || region.seamOverrides.size() > 4096U) {
      return core::failure<std::vector<PhraseSegment>>(core::ErrorCode::InvalidArgument, "Phrase dependency count exceeds bounds");
    }
    const auto resolved = phonemizer::resolvePronunciation(region);
    if (!resolved) return core::Result<std::vector<PhraseSegment>>{resolved.error()};
    const auto& tokens = resolved.value().pronunciation.tokens;
    std::unordered_map<domain::NoteId, std::size_t> indices;
    for (std::size_t i = 0U; i < notes.size(); ++i) indices.emplace(notes[i]->id, i);
    const auto connect = [&](domain::PhonemeKey key, std::size_t count, bool seam) {
      const auto start = std::find_if(tokens.begin(), tokens.end(), [&](const auto& token) { return token.key == key; });
      if (start == tokens.end()) return false;
      auto first = start;
      auto last = start;
      if (seam) { if (first != tokens.begin()) --first; }
      else {
        if (count == 0U || count > static_cast<std::size_t>(tokens.end() - start)) return false;
        last += static_cast<std::ptrdiff_t>(count - 1U);
      }
      const auto a = indices.at(first->key.noteId);
      const auto b = indices.at(last->key.noteId);
      for (auto i = std::min(a, b) + 1U; i <= std::max(a, b); ++i) joined[i] = true;
      return true;
    };
    for (const auto& edit : region.unitSelectionOverrides) {
      if (!edit.unresolved && !connect(edit.startKey, edit.tokenCount, false)) return core::failure<std::vector<PhraseSegment>>(
          core::ErrorCode::Conflict, "Active unit span cannot be segmented", edit.startKey.toString());
    }
    for (const auto& edit : region.seamOverrides) {
      if (!edit.unresolved && !connect(edit.incomingStartKey, 1U, true)) return core::failure<std::vector<PhraseSegment>>(
          core::ErrorCode::Conflict, "Active seam cannot be segmented", edit.incomingStartKey.toString());
    }
  }
  std::vector<time::Tick> groupEnds(notes.size());
  for (std::size_t begin = 0U; begin < notes.size();) {
    auto end = begin + 1U;
    auto extent = notes[begin]->endTick();
    while (end < notes.size() && joined[end]) { extent = std::max(extent, notes[end]->endTick()); ++end; }
    if (end > begin + 1U && extent - notes[begin]->startTick > config.maximumDuration) {
      return core::failure<std::vector<PhraseSegment>>(core::ErrorCode::Conflict, "Active render relationship exceeds maximum phrase duration");
    }
    for (auto i = begin; i < end; ++i) groupEnds[i] = extent;
    begin = end;
  }

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
    const auto proposedEnd = std::max(currentEnd, groupEnds[index]);
    const bool restSplit = gap >= config.splitRest && gap.value() > 0;
    const bool durationSplit = proposedEnd - currentStart > config.maximumDuration;
    if (!joined[index] && (restSplit || durationSplit)) {
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
