#pragma once

#include "seam/core/result.hpp"
#include "seam/synthesis/raw_renderer.hpp"
#include "seam/synthesis/seam_composer.hpp"
#include "seam/synthesis/timing_solver.hpp"
#include "seam/voicebank/voicebank.hpp"

#include <filesystem>
#include <vector>

namespace seam::synthesis {

struct RenderedPlacementInfo final {
  std::string unitId;
  time::SampleFrame requestedStart{0};
  time::SampleFrame alignedStart{0};
  time::SampleFrame frameCount{0};
  time::SampleFrame vowelOnset{0};
};

struct PhraseRenderResult final {
  PhraseAudio audio;
  std::vector<RenderedPlacementInfo> placements;
};

class RawPhraseRenderer final {
public:
  [[nodiscard]] core::Result<PhraseRenderResult> render(
      const voicebank::Manifest& manifest,
      const std::filesystem::path& bankRoot,
      const TimingPlan& timing,
      std::uint32_t outputSampleRate,
      RawRenderParameters renderParameters = {},
      SeamSettings seamSettings = {}) const;
};

}  // namespace seam::synthesis
