#pragma once

#include "seam/voicebank/voicebank.hpp"

namespace seam::voicebank {

enum class MarkerKind {
  AudioOffset,
  ConsonantEnd,
  VowelOnset,
  StableStart,
  LoopStart,
  LoopEnd,
  ReleaseStart,
  AudioEnd,
};

class MarkerEditor final {
public:
  [[nodiscard]] static core::Result<UnitMarkers> set(
      UnitMarkers markers,
      MarkerKind marker,
      time::SampleFrame value,
      time::SampleFrame totalFrames);
  [[nodiscard]] static UnitMarkers normalize(
      UnitMarkers markers,
      time::SampleFrame totalFrames) noexcept;
};

}  // namespace seam::voicebank
