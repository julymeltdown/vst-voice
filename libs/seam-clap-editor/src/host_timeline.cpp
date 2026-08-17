#include "seam/clap_editor/host_timeline.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace seam::clap_editor {
namespace {

double loopPosition(double value, double start, double end) noexcept {
  if (!std::isfinite(value) || !std::isfinite(start) || !std::isfinite(end) ||
      end <= start || value < start) {
    return value;
  }
  return start + std::fmod(value - start, end - start);
}

}  // namespace

HostFramePosition HostTimelineMapper::map(
    const HostTimelineState& state,
    double projectOffsetSeconds,
    double defaultTempo,
    double sampleRate,
    std::uint32_t frameOffset) noexcept {
  HostFramePosition result;
  if (!state.playing || !std::isfinite(sampleRate) || sampleRate < 8000.0 ||
      sampleRate > 384000.0 || !std::isfinite(projectOffsetSeconds) ||
      projectOffsetSeconds < 0.0 || !std::isfinite(defaultTempo) ||
      defaultTempo <= 0.0) {
    return result;
  }

  const auto frameSeconds = static_cast<double>(frameOffset) / sampleRate;
  double hostSeconds = 0.0;
  if (state.hasSeconds && std::isfinite(state.seconds)) {
    hostSeconds = state.seconds + frameSeconds;
    if (state.loopActive && state.loopHasSeconds) {
      hostSeconds = loopPosition(hostSeconds, state.loopStartSeconds,
                                 state.loopEndSeconds);
    }
  } else if (state.hasBeats && std::isfinite(state.beats)) {
    const auto tempo = state.hasTempo && std::isfinite(state.tempo) &&
                               state.tempo > 0.0
                           ? state.tempo
                           : defaultTempo;
    auto beats = state.beats + frameSeconds * tempo / 60.0;
    if (state.loopActive && state.loopHasBeats) {
      beats = loopPosition(beats, state.loopStartBeats, state.loopEndBeats);
    }
    hostSeconds = beats * 60.0 / tempo;
  } else {
    hostSeconds = frameSeconds;
  }

  const auto sourceSeconds = hostSeconds - projectOffsetSeconds;
  result.hostSeconds = hostSeconds;
  if (!std::isfinite(sourceSeconds) || sourceSeconds < 0.0) return result;
  const auto frame = static_cast<long double>(sourceSeconds) * sampleRate;
  if (frame < 0.0L ||
      frame > static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
    return result;
  }
  result.audible = true;
  result.sourceFrame = static_cast<std::uint64_t>(frame);
  return result;
}

HostFramePosition HostTimelineMapper::map(
    const HostTimelineState& state,
    const domain::Project& project,
    double sampleRate,
    std::uint32_t frameOffset) noexcept {
  return map(state,
             project.tempoMap().secondsAt(project.settings().hostStartOffsetTick),
             project.tempoMap().bpmAt(time::Tick{0}), sampleRate, frameOffset);
}

}  // namespace seam::clap_editor
