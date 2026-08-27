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

EditorFrameLayout buildEditorFrameLayout(
    const EditorFrameLayoutInput& input) noexcept {
  const auto width = std::max(0.0, input.logicalWidth);
  const auto height = std::max(0.0, input.logicalHeight);
  const auto dockWidth = input.dockVisible ? std::max(0.0, input.dockWidth) : 0.0;
  const auto editorRight = std::clamp(
      std::max(input.keyboardWidth + input.minimumTimelineWidth,
               width - dockWidth),
      0.0, width);
  const auto contentTop = std::clamp(input.toolbarHeight + input.rulerHeight,
                                     0.0, height);
  const auto statusTop = std::clamp(height - input.statusHeight, contentTop,
                                    height);
  const auto contentBottom = std::clamp(
      height - input.statusHeight - input.bottomInset, contentTop, statusTop);
  const auto pianoBottom = std::clamp(input.pianoBottom, contentTop,
                                      contentBottom);
  const auto phonemeTop = pianoBottom;
  const auto unitTop = phonemeTop + std::max(0.0, input.phonemeHeight);
  const auto seamTop = unitTop + std::max(0.0, input.unitHeight);
  const auto pitchTop = seamTop + std::max(0.0, input.seamHeight);
  return EditorFrameLayout{
      .toolbar = ui::Rect{0.0, 0.0, width, std::max(0.0, input.toolbarHeight)},
      .ruler = ui::Rect{0.0, input.toolbarHeight, editorRight,
                        std::max(0.0, input.rulerHeight)},
      .timeline = ui::Rect{input.keyboardWidth, contentTop,
                           std::max(0.0, editorRight - input.keyboardWidth),
                           std::max(0.0, pianoBottom - contentTop)},
      .keyboard = ui::Rect{0.0, contentTop, input.keyboardWidth,
                           std::max(0.0, pianoBottom - contentTop)},
      .phonemeLane = ui::Rect{0.0, phonemeTop, editorRight,
                              std::max(0.0, input.phonemeHeight)},
      .unitLane = ui::Rect{0.0, unitTop, editorRight,
                           std::max(0.0, input.unitHeight)},
      .seamLane = ui::Rect{0.0, seamTop, editorRight,
                           std::max(0.0, input.seamHeight)},
      .pitchLane = ui::Rect{0.0, pitchTop, editorRight,
                            std::max(0.0, input.pitchHeight)},
      .status = ui::Rect{0.0, statusTop, width,
                         std::max(0.0, input.statusHeight)},
      .dock = ui::Rect{editorRight, input.toolbarHeight,
                       std::max(0.0, width - editorRight),
                       std::max(0.0, contentBottom - input.toolbarHeight)},
      .editorRight = editorRight,
  };
}

}
