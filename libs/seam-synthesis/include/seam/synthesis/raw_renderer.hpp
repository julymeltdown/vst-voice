#pragma once

#include "seam/core/result.hpp"
#include "seam/synthesis/performance_compiler.hpp"
#include "seam/synthesis/pitch_curve.hpp"
#include "seam/synthesis/source_target_map.hpp"
#include <memory>
#include "seam/voicebank/voicebank.hpp"
#include "seam/voicebank/wav.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace seam::synthesis {

struct RawRenderParameters final {
  float loopPrint{1.0F};
  float additionalGainDb{0.0F};
  std::shared_ptr<const CompiledScorePerformance> performance{};
  time::SampleFrame performanceStartFrame{0};
  std::optional<time::SampleFrame> performanceVowelFrame{};
  std::optional<SourceTargetMap> sourceMap{};
  // Raw has no pitchCurve field in the source voicebank; this curve drives a
  // bounded per-sample sustain trajectory when no compiled performance is
  // supplied. Compiled performance remains the authoritative path when set.
  PitchCurve pitchCurve;
};

struct RenderedUnit final {
  std::string unitId;
  std::vector<float> samples;
  time::SampleFrame vowelOnsetOffset{0};
};

class RawLoopRenderer final {
public:
  [[nodiscard]] core::Result<RenderedUnit> render(
      const voicebank::Unit& unit,
      const voicebank::AudioBuffer& source,
      std::uint32_t outputSampleRate,
      time::SampleFrame outputFrames,
      std::int32_t targetMidi,
      RawRenderParameters parameters = {}, std::stop_token stopToken = {}) const;
};

// Deterministic two-segment resampling around the rendered vowel landmark.
// Retains sample count; caller supplies a strictly interior target landmark.
[[nodiscard]] core::Result<void> retimeRenderedOnset(RenderedUnit& unit,
                                                   time::SampleFrame targetVowelOffset);

}  // namespace seam::synthesis
