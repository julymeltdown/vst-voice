#pragma once
#include "seam/voicebank/acoustic_analysis.hpp"
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

// Replaces a map's voicing with what the stored analysis measured.
//
// Why this is needed: a renderer asks voicedAtSource(...) == false in order to
// skip a sample, and voicedAtSource answers nullopt when no span covers the
// frame. Unknown therefore reads as "not unvoiced", so a map with no voicing at
// all -- which is what compileShortUnitMarkerMap produced -- makes an unvoiced
// consonant look voiced. The measured spans are clipped to the map's source
// extent and made contiguous, because the map requires coverage from its first
// knot to its last.
//
// Returns false and leaves the map untouched when the analysis covers no part of
// the range, so a caller never gets a partially described voicing that would
// reintroduce the same unknown-means-voiced default.
[[nodiscard]] bool applyMeasuredVoicing(
    SourceTargetMap& map, const voicebank::AcousticAnalysis& analysis,
    time::SampleFrame begin, time::SampleFrame end);
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
