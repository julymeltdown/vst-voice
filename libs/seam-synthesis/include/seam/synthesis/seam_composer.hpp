#pragma once

#include "seam/core/result.hpp"
#include "seam/synthesis/raw_renderer.hpp"

#include <span>
#include <vector>

namespace seam::synthesis {

struct PlacedRenderedUnit final {
  time::SampleFrame destinationStart{0};
  RenderedUnit unit;
};

struct SeamSettings final {
  float seamAmount{0.7F};
  std::uint32_t sampleRate{48000};
};

struct PhraseAudio final {
  time::SampleFrame startFrame{0};
  std::vector<float> samples;
};

class SeamComposer final {
public:
  [[nodiscard]] core::Result<PhraseAudio> compose(
      std::span<const PlacedRenderedUnit> units,
      SeamSettings settings = {}) const;
};

}  // namespace seam::synthesis
