#pragma once

#include "seam/core/result.hpp"
#include "seam/voicebank/voicebank.hpp"
#include "seam/voicebank/wav.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace seam::synthesis {

struct RawRenderParameters final {
  float loopPrint{1.0F};
  float additionalGainDb{0.0F};
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
      RawRenderParameters parameters = {}) const;
};

}  // namespace seam::synthesis
