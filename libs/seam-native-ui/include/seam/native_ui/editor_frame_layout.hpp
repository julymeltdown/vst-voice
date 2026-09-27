#pragma once

#include "seam/domain/project.hpp"
#include "seam/ui/geometry.hpp"

#include <array>

namespace seam::native_ui {

// How the four technical lanes (phoneme, unit, seam, pitch) share the space under the piano roll in
// the controller's input geometry, from each lane's saved presentation and whether it has content.
struct TechnicalLaneLayoutInput final {
  std::array<domain::TechnicalLanePresentation, domain::kTechnicalLaneCount> presentation;
  std::array<bool, 4U> populated{};
  std::array<double, 4U> previewHeights{};
  double contentTop{0.0};
  double contentBottom{0.0};
};

struct TechnicalLaneHeights final {
  std::array<double, 4U> values{};
  double pianoBottom{0.0};
};

[[nodiscard]] TechnicalLaneHeights resolveTechnicalLaneHeights(
    const TechnicalLaneLayoutInput& input) noexcept;

}
