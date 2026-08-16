#pragma once

#include "seam/core/result.hpp"
#include "seam/synthesis/pitch_curve.hpp"
#include "seam/synthesis/raw_renderer.hpp"
#include "seam/voicebank/voicebank.hpp"
#include "seam/voicebank/wav.hpp"

#include <cstddef>
#include <cstdint>
#include <stop_token>

namespace seam::synthesis {

// First-party deterministic granular fallback. It is deliberately unit-scoped:
// no state crosses a unit boundary, so it cannot erase the product's audible seams.
struct StretchRenderParameters final {
  std::size_t grainSize{1024};
  std::size_t hopSize{256};
  float transientPreservation{0.75F};
  float sourceDrift{0.25F};
  float additionalGainDb{0.0F};
  PitchCurve pitchCurve;
};

class StretchUnitRenderer final {
public:
  [[nodiscard]] core::Result<RenderedUnit> render(
      const voicebank::Unit& unit,
      const voicebank::AudioBuffer& source,
      std::uint32_t outputSampleRate,
      time::SampleFrame outputFrames,
      std::int32_t targetMidi,
      const StretchRenderParameters& parameters = {},
      std::stop_token stopToken = {}) const;
};

}  // namespace seam::synthesis
