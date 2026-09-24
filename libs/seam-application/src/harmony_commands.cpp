#include "seam/application/harmony_commands.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace seam::application {
namespace {

constexpr std::size_t kMaximumHarmonyNotes = 4'096U;

core::Result<void> validateScale(const DiatonicHarmony& scale) {
  if (scale.tonicPitchClass>11U || scale.degreeOffset==0 ||
      scale.degreeOffset < -48 || scale.degreeOffset>48 ||
      scale.scaleIntervals.size()<2U || scale.scaleIntervals.size()>12U ||
      scale.scaleIntervals.front()!=0U)
    return core::failure(core::ErrorCode::InvalidArgument,"Harmony scale is invalid");
  for (std::size_t index=0U;index<scale.scaleIntervals.size();++index) {
    if (scale.scaleIntervals[index]>11U ||
        (index>0U && scale.scaleIntervals[index]<=scale.scaleIntervals[index-1U]))
      return core::failure(core::ErrorCode::InvalidArgument,
          "Harmony scale intervals must be strictly ascending pitch classes");
  }
  return core::success();
}

int floorDiv(int value,int divisor) noexcept {
  const int quotient=value/divisor;
  return value%divisor<0?quotient-1:quotient;
}

core::Result<std::int32_t> diatonicTarget(std::uint8_t midi,
    const DiatonicHarmony& scale) {
  const int relative=static_cast<int>(midi)-static_cast<int>(scale.tonicPitchClass);
  const int octave=floorDiv(relative,12);
  const int pitchClass=relative-octave*12;
  const auto found=std::find(scale.scaleIntervals.begin(),scale.scaleIntervals.end(),
      static_cast<std::uint8_t>(pitchClass));
  if (found==scale.scaleIntervals.end())
    return core::failure<std::int32_t>(core::ErrorCode::Conflict,
        "Harmony source note is outside the selected scale");
  const int count=static_cast<int>(scale.scaleIntervals.size());
  const int degree=octave*count+static_cast<int>(found-scale.scaleIntervals.begin())+
      scale.degreeOffset;
  const int targetOctave=floorDiv(degree,count);
  const int targetIndex=degree-targetOctave*count;
  return core::success(static_cast<std::int32_t>(scale.tonicPitchClass)+
      targetOctave*12+scale.scaleIntervals[static_cast<std::size_t>(targetIndex)]);
}

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
  if (!request.regionId.valid() ||
      (!request.diatonic && (request.intervalSemitones == 0 ||
      request.intervalSemitones < -48 || request.intervalSemitones > 48)) ||
      request.minimumMidi > request.maximumMidi) {
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Harmony interval or MIDI range is invalid");
  }
  if (request.diatonic) {
    const auto valid=validateScale(*request.diatonic);
    if (!valid) return core::Result<Output>{valid.error()};
  }
  const auto* region = project.findRegion(request.regionId);
  if (region == nullptr) return core::failure<Output>(core::ErrorCode::NotFound, "Harmony region was not found");
  factory.synchronizeWith(project);
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
  // Each generated note consumes a lyric ID and a note ID, and a separate
  // harmony track consumes two more IDs. Fail before a near-limit project can
  // wrap the shared document allocator.
  const auto remainingIds = 2U * selected.size() + 2U;
  if (factory.nextIdValue() == 0U ||
      factory.nextIdValue() > std::numeric_limits<std::uint64_t>::max() - remainingIds)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Harmony cannot reserve fresh identities for this project");
  std::sort(selected.begin(),selected.end(),[](const auto* left,const auto* right) {
    return left->startTick==right->startTick?left->id<right->id:left->startTick<right->startTick;
  });
  Output result{.regionId = request.regionId, .expectedNotes = region->notes, .lyrics = {}, .notes = {}};
  result.lyrics.reserve(selected.size()); result.notes.reserve(selected.size());
  std::unordered_set<domain::NoteId> generatedIds;
  std::unordered_map<domain::LyricTokenId,domain::LyricTokenId> copiedLyrics;
  for (const auto* sourceNote : selected) {
    const auto* sourceLyric = region->findLyric(sourceNote->lyricTokenId);
    if (sourceLyric == nullptr) return core::failure<Output>(core::ErrorCode::InvariantViolation, "Harmony source note has no lyric");
    const auto targetResult=request.diatonic?
        diatonicTarget(sourceNote->midiKey,*request.diatonic):
        core::success(static_cast<std::int32_t>(sourceNote->midiKey)+request.intervalSemitones);
    if (!targetResult) return core::Result<Output>{targetResult.error()};
    const auto target=targetResult.value();
    if (target < request.minimumMidi || target > request.maximumMidi || target < 0 || target > 127) return core::failure<Output>(core::ErrorCode::Conflict, "Harmony interval leaves the MIDI range", sourceNote->id.toString());
    auto [lyric, note] = factory.makeNote(sourceNote->startTick, sourceNote->durationTick,
        static_cast<std::uint8_t>(target), sourceLyric->surface, sourceLyric->language);
    note.articulation = sourceNote->articulation;
    note.slurGroup = sourceNote->slurGroup;
    note.vibrato = sourceNote->vibrato;
    note.phoneticHint = sourceNote->phoneticHint;
    const auto [mapped,inserted]=copiedLyrics.emplace(sourceLyric->id,lyric.id);
    if (!inserted) note.lyricTokenId=mapped->second;
    if (!generatedIds.insert(note.id).second) return core::failure<Output>(core::ErrorCode::Conflict, "Harmony generated duplicate note identity");
    if (inserted) result.lyrics.push_back(std::move(lyric));
    result.notes.push_back(std::move(note));
  }
  const auto valid = validateRange(*region, result.notes); if (!valid) return core::Result<Output>{valid.error()};
  return result;
}

core::Result<HarmonyTrackDraft> prepareHarmonyTrack(
    const domain::Project& project,ProjectFactory& factory,HarmonyRequest request) {
  using Output=HarmonyTrackDraft;
  const domain::VocalTrack* sourceTrack=nullptr;
  for (const auto& track:project.vocalTracks())
    if (track.findRegion(request.regionId)!=nullptr) {sourceTrack=&track; break;}
  if (sourceTrack==nullptr)
    return core::failure<Output>(core::ErrorCode::NotFound,"Harmony source track was not found");
  const auto* sourceRegion=sourceTrack->findRegion(request.regionId);
  const auto prepared=prepareHarmony(project,factory,std::move(request));
  if (!prepared) return core::Result<Output>{prepared.error()};
  auto track=*sourceTrack;
  track.id=factory.nextTrackId();
  const std::string base=sourceTrack->name+" Harmony";
  track.name=base;
  for (std::size_t suffix=2U;
       std::any_of(project.vocalTracks().begin(),project.vocalTracks().end(),
           [&](const auto& existing){return existing.name==track.name;});++suffix)
    track.name=base+" "+std::to_string(suffix);
  track.muted=false;
  track.solo=false;
  track.regions.clear();
  domain::VocalRegion region{.id=factory.nextRegionId(),
      .name=sourceRegion->name+" Harmony",.startTick=sourceRegion->startTick,
      .durationTick=sourceRegion->durationTick,.lyrics=prepared.value().lyrics,
      .notes=prepared.value().notes};
  region.sortNotes();
  track.regions.push_back(std::move(region));
  auto staged=project;
  staged.vocalTracks().push_back(track);
  const auto valid=staged.validate();
  if (!valid) return core::Result<Output>{valid.error()};
  return core::success(Output{sourceTrack->id,*sourceTrack,std::move(track)});
}

CommandImpact AddHarmonyTrackCommand::impact() const {
  CommandImpact result{.scope=CommandAudioImpact::ProjectAudio,.projectWide=true,
      .trackIds={draft_.harmonyTrack.id}};
  for (const auto& region:draft_.harmonyTrack.regions) {
    result.regionIds.push_back(region.id);
    for (const auto& note:region.notes) result.noteIds.push_back(note.id);
    for (const auto& lyric:region.lyrics) result.lyricIds.push_back(lyric.id);
  }
  return result;
}

core::Result<void> AddHarmonyTrackCommand::apply(domain::Project& project) {
  const auto* source=project.findVocalTrack(draft_.sourceTrackId);
  if (source==nullptr || *source!=draft_.expectedSourceTrack)
    return core::failure(core::ErrorCode::Conflict,"Harmony source changed before acceptance");
  if (project.findVocalTrack(draft_.harmonyTrack.id)!=nullptr ||
      std::any_of(project.vocalTracks().begin(),project.vocalTracks().end(),
          [&](const auto& track){return track.name==draft_.harmonyTrack.name;}))
    return core::failure(core::ErrorCode::Conflict,"Harmony track identity or name is already present");
  auto staged=project;
  staged.vocalTracks().push_back(draft_.harmonyTrack);
  const auto valid=staged.validate();
  if (!valid) return valid;
  project=std::move(staged);
  applied_=true;
  return core::success();
}

core::Result<void> AddHarmonyTrackCommand::revert(domain::Project& project) {
  if (!applied_) return core::failure(core::ErrorCode::Conflict,"Harmony track was not accepted");
  auto& tracks=project.vocalTracks();
  const auto found=std::find_if(tracks.begin(),tracks.end(),
      [&](const auto& track){return track.id==draft_.harmonyTrack.id;});
  if (found==tracks.end() || *found!=draft_.harmonyTrack)
    return core::failure(core::ErrorCode::Conflict,"Harmony track changed before undo");
  tracks.erase(found);
  applied_=false;
  return core::success();
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
  if (lyrics_.empty() || notes_.empty() || lyrics_.size()>notes_.size()) return core::failure(core::ErrorCode::InvalidArgument, "Harmony payload is empty or mismatched");
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
