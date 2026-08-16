#include "seam/rendering/render_snapshot.hpp"

#include "seam/core/stable_hash.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/voicebank/manifest_json.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_set>

namespace seam::rendering {
namespace {

domain::VocalRegion extractPhraseRegion(const domain::VocalRegion& source,
                                         const PhraseSegment& segment) {
  std::unordered_set<domain::NoteId> noteIds;
  noteIds.reserve(segment.noteIds.size());
  for (const auto noteId : segment.noteIds) noteIds.insert(noteId);

  domain::VocalRegion result{
      .id = source.id,
      .name = source.name,
      .startTick = source.startTick,
      .durationTick = source.durationTick,
      .lyrics = {},
      .notes = {},
      .phonemeOverrides = {},
      .unitSelectionOverrides = {},
      .seamOverrides = {},
      .pitchAutomation = {},
  };
  std::unordered_set<domain::LyricTokenId> lyricIds;
  for (const auto& note : source.notes) {
    if (!noteIds.contains(note.id)) continue;
    result.notes.push_back(note);
    lyricIds.insert(note.lyricTokenId);
  }
  for (const auto& lyric : source.lyrics) {
    if (lyricIds.contains(lyric.id)) result.lyrics.push_back(lyric);
  }
  for (const auto& value : source.phonemeOverrides) {
    if (noteIds.contains(value.key.noteId)) result.phonemeOverrides.push_back(value);
  }
  for (const auto& value : source.unitSelectionOverrides) {
    if (noteIds.contains(value.startKey.noteId)) {
      result.unitSelectionOverrides.push_back(value);
    }
  }
  for (const auto& value : source.seamOverrides) {
    if (noteIds.contains(value.incomingStartKey.noteId)) {
      result.seamOverrides.push_back(value);
    }
  }

  // Preserve the interpolation anchors immediately surrounding the phrase, in
  // addition to every point inside it. This keeps valueAt() identical inside
  // the phrase without hashing automation that cannot affect this render.
  const auto& points = source.pitchAutomation.points();
  const domain::PitchAutomationPoint* previous = nullptr;
  const domain::PitchAutomationPoint* next = nullptr;
  for (const auto& point : points) {
    if (point.tick <= segment.startTick) previous = &point;
    if (point.tick >= segment.endTick && next == nullptr) next = &point;
    if (point.tick >= segment.startTick && point.tick <= segment.endTick) {
      static_cast<void>(result.pitchAutomation.upsert(point));
    }
  }
  if (previous != nullptr) static_cast<void>(result.pitchAutomation.upsert(*previous));
  if (next != nullptr) static_cast<void>(result.pitchAutomation.upsert(*next));
  result.sortNotes();
  return result;
}

core::Result<domain::Project> extractPhraseProject(
    const domain::Project& source,
    domain::TrackId trackId,
    const PhraseSegment& segment) {
  const auto* sourceTrack = source.findVocalTrack(trackId);
  const auto* sourceRegion = sourceTrack == nullptr
      ? nullptr
      : sourceTrack->findRegion(segment.regionId);
  if (sourceTrack == nullptr || sourceRegion == nullptr) {
    return core::failure<domain::Project>(
        core::ErrorCode::NotFound,
        "Render snapshot phrase track or region was not found");
  }

  // Build a canonical audio-only slice instead of copying presentation state.
  // This keeps cache keys stable when the project title, character panel, snap
  // settings, unrelated tracks, or tempo events after this phrase change.
  domain::Project result{domain::ProjectId{1}, "Render snapshot", source.ppq()};
  result.settings().sampleRate = source.settings().sampleRate;
  result.settings().characterDisplay = domain::CharacterDisplayMode::Off;
  result.settings().snapEnabled = false;
  result.settings().snapGrid = time::Tick{source.ppq()};

  const auto absolutePhraseEnd = sourceRegion->startTick + segment.endTick;
  for (const auto& event : source.tempoMap().events()) {
    if (event.tick > absolutePhraseEnd) break;
    const auto inserted = result.tempoMap().addOrReplace(event.tick, event.bpm);
    if (!inserted) return core::Result<domain::Project>{inserted.error()};
  }
  for (const auto& event : source.meterMap().events()) {
    if (event.tick > absolutePhraseEnd) break;
    const auto inserted = result.meterMap().addOrReplace(
        event.tick, event.numerator, event.denominator);
    if (!inserted) return core::Result<domain::Project>{inserted.error()};
  }

  auto track = *sourceTrack;
  track.name = "Render track";
  track.character = {};
  track.muted = false;
  track.solo = false;
  track.regions.clear();
  auto phraseRegion = extractPhraseRegion(*sourceRegion, segment);
  phraseRegion.name = "Render phrase";
  track.regions.push_back(std::move(phraseRegion));
  result.vocalTracks().push_back(std::move(track));
  return result;
}

}  // namespace

std::string fnv1aHex(std::string_view value) {
  core::StableHash64 hash;
  hash.addString(value);
  return hash.hex();
}

core::Result<RenderSnapshot> RenderSnapshotFactory::create(
    const domain::Project& project,
    const voicebank::Manifest& voicebankValue,
    domain::TrackId trackId,
    const PhraseSegment& segment,
    std::uint64_t revision,
    RenderQuality quality,
    std::filesystem::path bankRoot,
    std::uint32_t sampleRate,
    std::string style,
    std::string engineVersion) const {
  const auto projectValidation = project.validate();
  if (!projectValidation) return core::Result<RenderSnapshot>{projectValidation.error()};
  const auto bankValidation = voicebankValue.validate();
  if (!bankValidation) return core::Result<RenderSnapshot>{bankValidation.error()};
  const auto* track = project.findVocalTrack(trackId);
  if (track == nullptr) {
    return core::failure<RenderSnapshot>(core::ErrorCode::NotFound,
                                         "Render snapshot track was not found",
                                         trackId.toString());
  }
  const auto* region = track->findRegion(segment.regionId);
  if (region == nullptr) {
    return core::failure<RenderSnapshot>(core::ErrorCode::NotFound,
                                         "Render snapshot region was not found",
                                         segment.regionId.toString());
  }
  if (engineVersion.empty() || segment.id.empty() || segment.noteIds.empty()) {
    return core::failure<RenderSnapshot>(core::ErrorCode::InvalidArgument,
                                         "Render snapshot identity is incomplete");
  }
  for (const auto noteId : segment.noteIds) {
    if (region->findNote(noteId) == nullptr) {
      return core::failure<RenderSnapshot>(core::ErrorCode::InvariantViolation,
                                           "Render segment references a missing note",
                                           noteId.toString());
    }
  }
  if (sampleRate == 0U) {
    const auto configured = project.settings().sampleRate;
    if (configured < 8000.0 || configured > 384000.0) {
      return core::failure<RenderSnapshot>(core::ErrorCode::InvalidArgument,
                                           "Render snapshot sample rate is invalid");
    }
    sampleRate = static_cast<std::uint32_t>(configured);
  }
  if (sampleRate < 8000U || sampleRate > 384000U) {
    return core::failure<RenderSnapshot>(core::ErrorCode::InvalidArgument,
                                         "Render snapshot sample rate is unsupported");
  }
  if (style.empty()) style = voicebankValue.styles.front();
  if (std::find(voicebankValue.styles.begin(), voicebankValue.styles.end(), style) ==
      voicebankValue.styles.end()) {
    return core::failure<RenderSnapshot>(core::ErrorCode::NotFound,
                                         "Render snapshot style is not in the voicebank",
                                         style);
  }

  auto phraseProject = extractPhraseProject(project, trackId, segment);
  if (!phraseProject) {
    return core::Result<RenderSnapshot>{phraseProject.error()};
  }
  const auto phraseValidation = phraseProject.value().validate();
  if (!phraseValidation) {
    return core::Result<RenderSnapshot>{phraseValidation.error()};
  }

  formats::ProjectJsonCodec projectCodec;
  voicebank::ManifestJsonCodec bankCodec;
  auto projectJson = projectCodec.encode(phraseProject.value());
  if (!projectJson) return core::Result<RenderSnapshot>{projectJson.error()};
  auto bankJson = bankCodec.encode(voicebankValue);
  if (!bankJson) return core::Result<RenderSnapshot>{bankJson.error()};

  core::StableHash64 identity;
  identity.addString("project-seam-render-snapshot-v2");
  identity.addString(engineVersion);
  identity.add(quality);
  identity.addString(segment.id);
  identity.add(segment.startTick.value());
  identity.add(segment.endTick.value());
  identity.add(sampleRate);
  identity.addString(style);
  identity.addString(projectJson.value());
  identity.addString(bankJson.value());

  return RenderSnapshot{
      .revision = revision,
      .quality = quality,
      .engineVersion = std::move(engineVersion),
      .contentHash = identity.hex(),
      .segment = segment,
      .trackId = trackId,
      .project = std::make_shared<const domain::Project>(
          std::move(phraseProject).value()),
      .voicebank = std::make_shared<const voicebank::Manifest>(voicebankValue),
      .bankRoot = std::move(bankRoot),
      .sampleRate = sampleRate,
      .style = std::move(style),
  };
}

}  // namespace seam::rendering
