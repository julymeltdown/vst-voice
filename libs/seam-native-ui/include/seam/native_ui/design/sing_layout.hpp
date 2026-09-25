#pragma once

#include "seam/ui/geometry.hpp"

#include <array>

namespace seam::native_ui::design {

// How much of the singer rack the width can carry. The approved composition gives the singer a
// full rack; narrower hosts keep the musical grid and collapse the rack to a rail before any text
// shrinks.
enum class RackPresentation { Full, Rail };

// One geometry snapshot for the SING workspace. Paint, pointer routing and native text input read
// the same rectangles. At 1600x900 logical points it reproduces
// docs/design/ui-fidelity-contract-v1.json exactly.
struct SingLayout final {
  double width{0.0};
  double height{0.0};
  RackPresentation rack{RackPresentation::Full};
  bool compactHeader{false};
  bool workspaceLabelsVisible{true};
  bool outputMeterVisible{true};

  ui::Rect header, wordmark, workspaceTabs, modeSwitch, transport, outputMeter, settings;
  ui::Rect editor, tools, ruler, keyboard, grid;
  ui::Rect lane, laneTabs, lanePlot, laneTimePlot;
  ui::Rect rackArea, singer, portraitRing, expression, style;
  ui::Rect status;

  // Tool strip controls inside tools.
  ui::Rect classicToggle, trackLabel, gridLabel;
  // Five workspace tabs, the six expression knob slots, lane tab slots.
  std::array<ui::Rect, 5U> workspaceTab{};
  std::array<ui::Rect, 6U> knob{};
  // Transport internals.
  ui::Rect playButton, positionReadout, tempoReadout, meterReadout;
  // Singer card actions.
  ui::Rect singerChange;
};

inline constexpr double kSingEdge = 16.0;
inline constexpr double kSingGap = 12.0;
inline constexpr double kSingRackGap = 16.0;
inline constexpr double kSingStatusHeight = 28.0;
inline constexpr double kSingMinimumTimeline = 480.0;

[[nodiscard]] double singRackWidth(double width) noexcept;
[[nodiscard]] SingLayout solveSingLayout(double width, double height) noexcept;

}  // namespace seam::native_ui::design
