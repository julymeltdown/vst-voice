#pragma once

#include "seam/ui/geometry.hpp"

#include <array>

namespace seam::native_ui::design {

// How much of the singer rack the width can carry (docs/design/SEAM_UI_FIDELITY_REVIEW §3.4). The
// approved composition gives the singer a full rack; narrower hosts keep the musical grid and
// collapse the rack before any text shrinks: a 56-point rail below 1100 points, and a 44-point
// drawer button below 860. In both compact presentations the portrait is a button that opens the
// singer inspector (voice, expression knobs, style) over the body, so every rack control stays
// reachable by pointer, keyboard and accessibility.
enum class RackPresentation { Full, Rail, Drawer };

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

  ui::Rect header, wordmark, workspaceTabs, workspaceMenuButton, workspaceMenu;
  // SING, VOICE, TUNE, MIX, EXPORT; below 720 pt the two looks share one row under them.
  std::array<ui::Rect, 5U> workspaceMenuRow{};
  std::array<ui::Rect, 2U> modeMenuRow{};
  ui::Rect modeSwitch, transport, outputMeter, settings;
  ui::Rect editor, tools, ruler, keyboard, grid;
  ui::Rect lane, laneTabs, lanePlot, laneTimePlot;
  ui::Rect rackArea, singer, portraitRing, expression, style;
  ui::Rect status;

  // Compact presentations: the portrait button and, while it is open, the inspector panel that
  // carries singer, singerChange, expression, knob and style (all inside inspector).
  ui::Rect inspectorButton, inspector;
  bool inspectorOpen{false};
  // Six knobs in one row instead of two when the window is too short for two rows.
  bool knobsInOneRow{false};

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
inline constexpr double kSingDrawerWidth = 860.0;
inline constexpr double kSingRailWidth = 1100.0;

[[nodiscard]] double singRackWidth(double width) noexcept;
// inspectorOpen only matters for the compact presentations; the full rack has no inspector.
[[nodiscard]] SingLayout solveSingLayout(double width, double height,
                                         bool inspectorOpen = false) noexcept;

}  // namespace seam::native_ui::design
