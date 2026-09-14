#pragma once

#include "seam/domain/project.hpp"
#include "seam/ui/geometry.hpp"

#include <array>

namespace seam::native_ui {

// How much of the character dock the current width can carry. The dock is secondary artwork beside a
// minimum musical workspace, so it collapses in a fixed order: the portrait goes first, then the whole
// dock. It never squeezes the timeline, and it never drops the identity and status indicators while
// still drawing itself -- a cropped face and a clipped name are worse than not drawing the dock.
inline constexpr double kCharacterDockPortraitMinimumWidth{140.0};
inline constexpr double kCharacterDockCompactMinimumWidth{88.0};

enum class CharacterDockPresentation { Hidden, Compact, Full };

[[nodiscard]] CharacterDockPresentation resolveCharacterDockPresentation(
    double dockWidth,
    double portraitMinimumWidth = kCharacterDockPortraitMinimumWidth) noexcept;

struct EditorFrameLayoutInput final {
  double logicalWidth{0.0};
  double logicalHeight{0.0};
  double toolbarHeight{0.0};
  double rulerHeight{0.0};
  double statusHeight{0.0};
  double keyboardWidth{0.0};
  double minimumTimelineWidth{0.0};
  double dockWidth{0.0};
  double bottomInset{0.0};
  double pianoBottom{0.0};
  double phonemeHeight{0.0};
  double unitHeight{0.0};
  double seamHeight{0.0};
  double pitchHeight{0.0};
  bool dockVisible{false};
};

struct EditorFrameLayout final {
  ui::Rect toolbar;
  ui::Rect ruler;
  ui::Rect timeline;
  ui::Rect keyboard;
  ui::Rect phonemeLane;
  ui::Rect unitLane;
  ui::Rect seamLane;
  ui::Rect pitchLane;
  ui::Rect status;
  ui::Rect dock;
  double editorRight{0.0};
};

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

[[nodiscard]] EditorFrameLayout buildEditorFrameLayout(
    const EditorFrameLayoutInput& input) noexcept;

}
