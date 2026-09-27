#include "seam/native_ui/editor_frame_layout.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace seam::native_ui {

TechnicalLaneHeights resolveTechnicalLaneHeights(
    const TechnicalLaneLayoutInput& input) noexcept {
  TechnicalLaneHeights result;
  const auto available = std::max(0.0, input.contentBottom - input.contentTop);
  bool expanded = false;
  for (std::size_t index = 0U; index < result.values.size(); ++index) {
    const auto& presentation = input.presentation[index];
    const auto mode = presentation.mode == domain::TechnicalLaneMode::Auto
                          ? (input.populated[index]
                                 ? domain::TechnicalLaneMode::Preview
                                 : domain::TechnicalLaneMode::Collapsed)
                          : presentation.mode;
    expanded = expanded || mode == domain::TechnicalLaneMode::Expanded;
    result.values[index] = mode == domain::TechnicalLaneMode::Collapsed
                               ? 20.0
                               : mode == domain::TechnicalLaneMode::Expanded
                                     ? std::clamp(presentation.expandedHeight, 96.0, 640.0)
                                     : std::clamp(input.previewHeights[index], 30.0, 42.0);
  }
  const auto desired = std::accumulate(result.values.begin(), result.values.end(), 0.0);
  const auto laneBudget = expanded ? std::max(0.0, available - 52.0)
                                   : available * 0.28;
  if (desired > laneBudget && desired > 0.0) {
    const auto scale = laneBudget / desired;
    for (auto& value : result.values) value = std::max(1.0, value * scale);
  }
  const auto used = std::accumulate(result.values.begin(), result.values.end(), 0.0);
  result.pianoBottom = std::max(input.contentTop, input.contentBottom - used);
  return result;
}

}
