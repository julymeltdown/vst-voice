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

struct SpectralRenderParameters final {
  std::size_t fftSize{1024};
  std::size_t hopSize{256};
  float formantFollow{0.45F};
  float phaseReset{0.70F};
  float additionalGainDb{0.0F};
  PitchCurve pitchCurve;
};

class SpectralClassicRenderer final {
public:
  [[nodiscard]] core::Result<RenderedUnit> render(
      const voicebank::Unit& unit,
      const voicebank::AudioBuffer& source,
      std::uint32_t outputSampleRate,
      time::SampleFrame outputFrames,
      std::int32_t targetMidi,
      const SpectralRenderParameters& parameters = {},
      std::stop_token stopToken = {}) const;
};

}  // namespace seam::synthesis
