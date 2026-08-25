#include "seam/native_ui/track_inspector.hpp"

namespace seam::native_ui {

TrackInspectorSnapshot TrackInspectorModel::snapshot(
    const domain::Project& project, domain::TrackId trackId) noexcept {
  if (const auto* track = project.findVocalTrack(trackId); track != nullptr) {
    return TrackInspectorSnapshot{
        .valid = true,
        .vocal = true,
        .trackId = track->id,
        .name = track->name,
        .gainDb = track->gainDb,
        .pan = track->pan,
        .muted = track->muted,
        .solo = track->solo,
        .voicebank = track->voicebank,
        .outputRoute = track->outputRoute,
    };
  }
  for (const auto& track : project.audioTracks()) {
    if (track.id != trackId) continue;
    return TrackInspectorSnapshot{
        .valid = true,
        .vocal = false,
        .trackId = track.id,
        .name = track.name,
        .gainDb = track.gainDb,
        .pan = track.pan,
        .muted = track.muted,
        .solo = track.solo,
        .mediaPath = track.mediaPath,
        .mediaHash = track.mediaHash,
        .mediaOwnership = track.mediaOwnership,
        .originalFilename = track.originalFilename,
        .sourceSampleRate = track.sourceSampleRate,
        .sourceChannels = track.sourceChannels,
        .sourceFrameCount = track.sourceFrameCount,
        .outputRoute = track.outputRoute,
    };
  }
  return {};
}

}
