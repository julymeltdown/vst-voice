#pragma once

#include "seam/core/result.hpp"
#include "seam/synthesis/pitch_curve.hpp"
#include "seam/synthesis/raw_renderer.hpp"
#include "seam/voicebank/voicebank.hpp"
#include "seam/voicebank/wav.hpp"

#include <cstdint>
#include <stop_token>

namespace seam::synthesis {

struct PsolaRenderParameters final {
  float sourcePitchResidual{0.35F};
  float additionalGainDb{0.0F};
  PitchCurve pitchCurve;
};

class ClassicPsolaRenderer final {
public:
  [[nodiscard]] core::Result<RenderedUnit> render(
      const voicebank::Unit& unit,
      const voicebank::AudioBuffer& source,
      std::uint32_t outputSampleRate,
      time::SampleFrame outputFrames,
      std::int32_t targetMidi,
      const PsolaRenderParameters& parameters = {},
      std::stop_token stopToken = {}) const;
};

}  // namespace seam::synthesis
