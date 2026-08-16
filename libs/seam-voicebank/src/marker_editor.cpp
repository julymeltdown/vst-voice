#include "seam/voicebank/marker_editor.hpp"

#include <algorithm>

namespace seam::voicebank {

UnitMarkers MarkerEditor::normalize(UnitMarkers markers,
                                    time::SampleFrame totalFrames) noexcept {
  totalFrames = std::max<time::SampleFrame>(1, totalFrames);
  markers.audioOffset = std::clamp(markers.audioOffset,
                                   time::SampleFrame{0}, totalFrames - 1);
  markers.audioEnd = std::clamp(markers.audioEnd,
                                markers.audioOffset + 1, totalFrames);
  markers.consonantEnd = std::clamp(markers.consonantEnd,
                                    markers.audioOffset, markers.audioEnd);
  markers.vowelOnset = std::clamp(markers.vowelOnset,
                                  markers.consonantEnd, markers.audioEnd);
  markers.stableStart = std::clamp(markers.stableStart,
                                   markers.vowelOnset, markers.audioEnd);
  if (markers.loopStart.has_value() || markers.loopEnd.has_value()) {
    if (markers.stableStart >= markers.audioEnd) {
      markers.loopStart.reset();
      markers.loopEnd.reset();
    } else {
      auto start = markers.loopStart.value_or(markers.stableStart);
      auto end = markers.loopEnd.value_or(std::min(markers.audioEnd, start + 1));
      start = std::clamp(start, markers.stableStart, markers.audioEnd - 1);
      end = std::clamp(end, start + 1, markers.audioEnd);
      markers.loopStart = start;
      markers.loopEnd = end;
    }
  }
  if (markers.releaseStart.has_value()) {
    const auto minimum = markers.loopEnd.value_or(markers.stableStart);
    if (minimum >= markers.audioEnd) {
      markers.releaseStart.reset();
    } else {
      markers.releaseStart = std::clamp(*markers.releaseStart,
                                        minimum, markers.audioEnd - 1);
    }
  }
  return markers;
}

core::Result<UnitMarkers> MarkerEditor::set(UnitMarkers markers,
                                             MarkerKind marker,
                                             time::SampleFrame value,
                                             time::SampleFrame totalFrames) {
  if (totalFrames <= 0) {
    return core::failure<UnitMarkers>(core::ErrorCode::InvalidArgument,
                                      "Marker editor requires a non-empty sample");
  }
  switch (marker) {
    case MarkerKind::AudioOffset: markers.audioOffset = value; break;
    case MarkerKind::ConsonantEnd: markers.consonantEnd = value; break;
    case MarkerKind::VowelOnset: markers.vowelOnset = value; break;
    case MarkerKind::StableStart: markers.stableStart = value; break;
    case MarkerKind::LoopStart: markers.loopStart = value; break;
    case MarkerKind::LoopEnd: markers.loopEnd = value; break;
    case MarkerKind::ReleaseStart: markers.releaseStart = value; break;
    case MarkerKind::AudioEnd: markers.audioEnd = value; break;
  }
  markers = normalize(markers, totalFrames);
  const auto validation = markers.validate(totalFrames);
  if (!validation) return core::Result<UnitMarkers>{validation.error()};
  return markers;
}

}  // namespace seam::voicebank
