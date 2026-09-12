#include "seam/application/harmony_commands.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>

namespace seam::application {
namespace {

constexpr std::size_t kMaximumHarmonyNotes = 4'096U;

core::Result<void> validateRange(const domain::VocalRegion& region,
                                 const std::vector<domain::Note>& notes) {
  if (notes.size() > kMaximumHarmonyNotes) return core::failure(core::ErrorCode::InvalidArgument, "Harmony exceeds the note bound");
  std::unordered_set<domain::NoteId> ids;
  for (const auto& note : notes) {
    const auto valid = note.validate(); if (!valid) return valid;
    if (!ids.insert(note.id).second || note.endTick() > region.durationTick) return core::failure(core::ErrorCode::InvariantViolation, "Harmony note IDs or range are invalid");
  }
  return core::success();
}

}  // namespace

core::Result<HarmonyDraft> prepareHarmony(
    const domain::Project& project, ProjectFactory& factory,
    HarmonyRequest request) {
  using Output = HarmonyDraft;
  if (!request.regionId.valid() || request.intervalSemitones == 0 ||
      request.intervalSemitones < -48 || request.intervalSemitones > 48 ||
      request.minimumMidi > request.maximumMidi) {
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Harmony interval or MIDI range is invalid");
  }
  const auto* region = project.findRegion(request.regionId);
  if (region == nullptr) return core::failure<Output>(core::ErrorCode::NotFound, "Harmony region was not found");
  std::vector<const domain::Note*> selected;
  if (request.sourceNotes.empty()) {
    selected.reserve(region->notes.size());
    for (const auto& note : region->notes) selected.push_back(&note);
  } else {
    std::unordered_set<domain::NoteId> seen;
    for (const auto id : request.sourceNotes) {
      if (!seen.insert(id).second) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Harmony source notes repeat an ID");
      const auto* note = region->findNote(id);
      if (note == nullptr) return core::failure<Output>(core::ErrorCode::NotFound, "Harmony source note was not found", id.toString());
      selected.push_back(note);
    }
  }
  if (selected.empty() || selected.size() > kMaximumHarmonyNotes) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Harmony source selection is empty or oversized");
  Output result{.regionId = request.regionId, .expectedNotes = region->notes, .lyrics = {}, .notes = {}};
  result.lyrics.reserve(selected.size()); result.notes.reserve(selected.size());
  std::unordered_set<domain::NoteId> generatedIds;
  for (const auto* sourceNote : selected) {
    const auto* sourceLyric = region->findLyric(sourceNote->lyricTokenId);
    if (sourceLyric == nullptr) return core::failure<Output>(core::ErrorCode::InvariantViolation, "Harmony source note has no lyric");
    const auto target = static_cast<std::int32_t>(sourceNote->midiKey) + request.intervalSemitones;
    if (target < request.minimumMidi || target > request.maximumMidi || target < 0 || target > 127) return core::failure<Output>(core::ErrorCode::Conflict, "Harmony interval leaves the MIDI range", sourceNote->id.toString());
    auto [lyric, note] = factory.makeNote(sourceNote->startTick, sourceNote->durationTick,
        static_cast<std::uint8_t>(target), sourceLyric->surface, sourceLyric->language);
    note.articulation = sourceNote->articulation;
    note.vibrato = sourceNote->vibrato;
    if (!generatedIds.insert(note.id).second) return core::failure<Output>(core::ErrorCode::Conflict, "Harmony generated duplicate note identity");
    result.lyrics.push_back(std::move(lyric)); result.notes.push_back(std::move(note));
  }
  const auto valid = validateRange(*region, result.notes); if (!valid) return core::Result<Output>{valid.error()};
  return result;
}

AddHarmonyCommand::AddHarmonyCommand(
    domain::RegionId regionId, std::vector<domain::Note> expectedNotes,
    std::vector<domain::LyricToken> lyrics, std::vector<domain::Note> notes)
    : regionId_(regionId), expectedNotes_(std::move(expectedNotes)),
      lyrics_(std::move(lyrics)), notes_(std::move(notes)) {}

CommandImpact AddHarmonyCommand::impact() const {
  CommandImpact result{.scope = CommandAudioImpact::PhraseAudio,
                       .projectWide = false, .trackIds = {},
                       .regionIds = {regionId_}, .noteIds = {}, .lyricIds = {}};
  result.noteIds.reserve(notes_.size()); result.lyricIds.reserve(lyrics_.size());
  for (const auto& note : notes_) result.noteIds.push_back(note.id);
  for (const auto& lyric : lyrics_) result.lyricIds.push_back(lyric.id);
  return result;
}

core::Result<void> AddHarmonyCommand::apply(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) return core::failure(core::ErrorCode::NotFound, "Harmony region was not found");
  if (!applied_ && region->notes != expectedNotes_) return core::failure(core::ErrorCode::Conflict, "Harmony source changed before acceptance");
  const auto valid = validateRange(*region, notes_); if (!valid) return valid;
  if (lyrics_.size() != notes_.size() || notes_.empty()) return core::failure(core::ErrorCode::InvalidArgument, "Harmony payload is empty or mismatched");
  std::set<domain::NoteId> existingNotes;
  std::set<domain::LyricTokenId> existingLyrics;
  for (const auto& note : region->notes) existingNotes.insert(note.id);
  for (const auto& lyric : region->lyrics) existingLyrics.insert(lyric.id);
  for (const auto& note : notes_) if (existingNotes.contains(note.id)) return core::failure(core::ErrorCode::Conflict, "Harmony note identity is already present", note.id.toString());
  for (const auto& lyric : lyrics_) if (existingLyrics.contains(lyric.id)) return core::failure(core::ErrorCode::Conflict, "Harmony lyric identity is already present", lyric.id.toString());
  auto staged = *region;
  staged.lyrics.insert(staged.lyrics.end(), lyrics_.begin(), lyrics_.end());
  staged.notes.insert(staged.notes.end(), notes_.begin(), notes_.end());
  staged.sortNotes();
  const auto stagedValid = staged.validate(); if (!stagedValid) return stagedValid;
  *region = std::move(staged); applied_ = true;
  return core::success();
}

core::Result<void> AddHarmonyCommand::revert(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (region == nullptr || !applied_) return core::failure(core::ErrorCode::Conflict, "Harmony has no accepted state to undo");
  auto staged = *region;
  const auto noteEnd = std::remove_if(staged.notes.begin(), staged.notes.end(),
      [&](const auto& note) { return std::any_of(notes_.begin(), notes_.end(), [&](const auto& added) { return added.id == note.id; }); });
  if (noteEnd == staged.notes.end()) return core::failure(core::ErrorCode::Conflict, "Harmony notes changed before undo");
  staged.notes.erase(noteEnd, staged.notes.end());
  const auto lyricEnd = std::remove_if(staged.lyrics.begin(), staged.lyrics.end(),
      [&](const auto& lyric) { return std::any_of(lyrics_.begin(), lyrics_.end(), [&](const auto& added) { return added.id == lyric.id; }); });
  if (lyricEnd == staged.lyrics.end()) return core::failure(core::ErrorCode::Conflict, "Harmony lyrics changed before undo");
  staged.lyrics.erase(lyricEnd, staged.lyrics.end());
  const auto valid = staged.validate(); if (!valid) return valid;
  *region = std::move(staged); applied_ = false;
  return core::success();
}

}  // namespace seam::application
