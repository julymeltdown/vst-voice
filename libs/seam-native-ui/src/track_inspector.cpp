#include "seam/native_ui/track_inspector.hpp"

#include <algorithm>

namespace seam::native_ui {

namespace {

// The channels the inspector offers rows for, in the order the lane walks them. Every channel is
// considered; which ones are shown is decided by the caller's track, not by this list.
constexpr ui::ExpressionChannel kInspectorChannels[ui::kExpressionChannelCount]{
    ui::ExpressionChannel::Formant, ui::ExpressionChannel::Breathiness,
    ui::ExpressionChannel::Tension, ui::ExpressionChannel::Airiness,
    ui::ExpressionChannel::Gender, ui::ExpressionChannel::Growl};

constexpr std::size_t kMaximumInspectorExpressionRows = 3U;

}  // namespace

TrackInspectorSnapshot TrackInspectorModel::snapshot(
    const domain::Project& project, domain::TrackId trackId,
    std::optional<time::Tick> playhead) noexcept {
  if (const auto* track = project.findVocalTrack(trackId); track != nullptr) {
    TrackInspectorSnapshot snapshot{
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
    // The rows describe the track's channels, so they are built for the whole track rather than for the
    // region the playhead happens to be in. A creator comparing two phrases must not see a channel
    // disappear because they selected the other one.
    for (const auto channel : kInspectorChannels) {
      const auto descriptor = ui::describeExpressionChannel(channel);
      std::size_t stored = 0U;
      float atPlayhead = descriptor.neutral;
      for (const auto& region : track->regions) {
        const auto points = ui::readExpressionPoints(region, channel);
        stored += points.size();
        if (!playhead.has_value() || points.empty()) continue;
        // The lane reads the playhead value against the region the playhead is inside, so the row does
        // the same rather than averaging across regions a creator is not looking at.
        const auto start = region.startTick;
        const auto end = region.startTick + region.durationTick;
        if (*playhead < start || *playhead > end) continue;
        const auto tick = *playhead - start;
        const auto after = std::lower_bound(points.begin(), points.end(), tick,
            [](const ui::ExpressionPoint& point, time::Tick value) { return point.tick < value; });
        if (after == points.begin()) atPlayhead = after->amount;
        else if (after == points.end()) atPlayhead = points.back().amount;
        else if (after->tick == tick) atPlayhead = after->amount;
        else {
          const auto before = after - 1;
          const auto span = (after->tick - before->tick).value();
          atPlayhead = span <= 0 ? after->amount
              : static_cast<float>(static_cast<double>(before->amount)
                  + static_cast<double>(after->amount - before->amount)
                      * static_cast<double>((tick - before->tick).value())
                      / static_cast<double>(span));
        }
      }
      const auto allowed = ui::validateExpressionCarrier(project, trackId, channel);
      // A row earns its place by being something the creator has touched or something the singer will
      // not take. A refusal is shown even with an empty curve, because a creator whose singer cannot
      // apply a channel needs to know that before they draw on it, not after the draw is dropped.
      if (stored == 0U && allowed.hasValue()) continue;
      TrackInspectorExpressionRow row;
      row.channel = channel;
      row.label = std::string{descriptor.label};
      row.unit = std::string{descriptor.unit};
      row.valueAtPlayhead = atPlayhead;
      row.storedPoints = stored;
      if (!allowed) row.refusal = allowed.error().message;
      snapshot.expressionRows.push_back(std::move(row));
      if (snapshot.expressionRows.size() >= kMaximumInspectorExpressionRows) break;
    }
    return snapshot;
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
