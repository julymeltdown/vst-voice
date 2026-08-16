#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/render_controls.hpp"
#include "seam/synthesis/raw_renderer.hpp"

#include <optional>
#include <span>
#include <vector>

namespace seam::synthesis {

struct SeamSettings final {
  float seamAmount{0.7F};
  domain::SeamCurve curve{domain::SeamCurve::HardCharacter};
  std::uint32_t sampleRate{48000};
};

struct BoundarySeamSettings final {
  float seamAmount{0.7F};
  domain::SeamCurve curve{domain::SeamCurve::HardCharacter};
  std::optional<time::SampleFrame> maxOverlapFrames;
  float phaseReset{0.0F};
  float envelopeBlend{0.0F};
};

struct PlacedRenderedUnit final {
  time::SampleFrame destinationStart{0};
  RenderedUnit unit;
  // Applies to the boundary immediately before this incoming unit. The first
  // unit ignores this field because no incoming boundary exists.
  std::optional<BoundarySeamSettings> incomingBoundary;
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
