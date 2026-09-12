#pragma once
#include "seam/synthesis/source_phoneme_alignment.hpp"
#include "seam/synthesis/timing_solver.hpp"
#include <stop_token>

namespace seam::synthesis {
struct SourceTargetLandmark final {
  time::SampleFrame sourceFrame{0};
  time::SampleFrame targetFrame{0};
  friend bool operator==(const SourceTargetLandmark&, const SourceTargetLandmark&) = default;
};
struct SourceVoicingSpan final {
  time::SampleFrame start{0};
  time::SampleFrame end{0};
  std::optional<bool> voiced{};
};
struct SourceTargetMap final {
  // Endpoints are exclusive audio/target ends; interior knots are authored
  // source landmarks paired with compiled nuclei or explicit boundaries.
  std::vector<SourceTargetLandmark> knots;
  std::vector<SourceVoicingSpan> voicing{};
  [[nodiscard]] core::Result<void> validate(time::SampleFrame sourceFrames) const;
  // Require a validated map; clamp outside its endpoints. Fractional frames
  // preserve source timing independently of output pitch pulse placement.
  [[nodiscard]] double sourceAt(double targetFrame) const noexcept;
  [[nodiscard]] double targetAt(double sourceFrame) const noexcept;
  [[nodiscard]] std::optional<bool> voicedAtSource(double sourceFrame) const noexcept;
};
[[nodiscard]] core::Result<SourceTargetMap> compileSourceTargetMap(
    const SourcePhonemeAlignment& alignment, const voicebank::Unit& unit,
    const TimedUnitPlacement& placement, std::string_view verifiedAudioSha256,
    time::SampleFrame decodedFrames);
// Marker-only short CV/sustain mapping; never substitutes for interior-phone
// alignment. Caller must establish that the selected span has one nucleus.
[[nodiscard]] core::Result<SourceTargetMap> compileShortUnitMarkerMap(
    const voicebank::Unit& unit, time::SampleFrame destinationStart,
    time::SampleFrame vowelFrame, time::SampleFrame destinationEnd,
    std::uint32_t sourceRate, std::uint32_t outputRate, time::SampleFrame decodedFrames);
struct MappedSourceAudio final {
  time::SampleFrame startFrame{0};
  std::vector<float> samples;
};
// Deterministic linear interpolation between authored source/target knots.
// This baseline changes local playback rate; it is not pitch-preserving DSP.
[[nodiscard]] core::Result<MappedSourceAudio> applySourceTargetMap(
    std::span<const float> source, const SourceTargetMap& map,
    std::stop_token stopToken = {});
// Explicit raw-alignment path: validates audio binding, compiles every anchor,
// applies the map and unit gain. Does not promise pitch-preserving time stretch.
[[nodiscard]] core::Result<MappedSourceAudio> renderAlignedRawUnit(
    std::span<const float> source, const SourcePhonemeAlignment& alignment,
    const voicebank::Unit& unit, const TimedUnitPlacement& placement,
    std::string_view verifiedAudioSha256, std::stop_token stopToken = {});
}
