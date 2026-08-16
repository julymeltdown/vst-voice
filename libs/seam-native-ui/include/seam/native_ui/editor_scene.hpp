#pragma once

#include "seam/native_ui/pixel_surface.hpp"
#include "seam/ui/piano_roll_model.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace seam::native_ui {

struct EditorSceneTheme final {
  Color background{16, 15, 19, 255};
  Color toolbarTop{38, 34, 41, 255};
  Color toolbarBottom{27, 25, 30, 255};
  Color panel{24, 22, 28, 255};
  Color keyboardWhite{48, 45, 53, 255};
  Color keyboardBlack{34, 31, 39, 255};
  Color gridStrong{70, 62, 74, 255};
  Color gridWeak{41, 37, 45, 255};
  Color note{74, 56, 78, 255};
  Color noteAlternate{80, 61, 86, 255};
  Color noteSelected{139, 76, 105, 255};
  Color noteStroke{119, 96, 128, 255};
  Color noteSelectedStroke{237, 175, 205, 255};
  Color primaryText{241, 235, 242, 255};
  Color secondaryText{170, 159, 171, 255};
  Color accent{170, 77, 116, 255};
  Color playhead{98, 192, 190, 255};
  Color selection{155, 88, 124, 58};
};

struct EditorSceneState final {
  std::string projectName{"Project SEAM"};
  std::uint64_t revision{0};
  bool playing{false};
  bool dirty{false};
  bool audioDeviceOnline{false};
  std::string audioBackend{"OFFLINE"};
  std::optional<ui::Rect> boxSelection;
  std::optional<ui::Rect> lyricEditor;
  std::string compositionPreview;
  double playheadPixel{0.0};
};

struct EditorSceneLayout final {
  double toolbarHeight{64.0};
  double rulerHeight{34.0};
  double statusHeight{28.0};
  double keyboardWidth{76.0};

  [[nodiscard]] double contentTop() const noexcept {
    return toolbarHeight + rulerHeight;
  }
};

class EditorScenePainter final {
public:
  explicit EditorScenePainter(EditorSceneTheme theme = {}) noexcept
      : theme_(theme) {}

  [[nodiscard]] EditorSceneLayout layout() const noexcept { return layout_; }
  void paint(RasterCanvas& canvas, ui::PianoRollModel& model,
             const EditorSceneState& state) const noexcept;

private:
  void paintToolbar(RasterCanvas& canvas, const EditorSceneState& state) const noexcept;
  void paintGrid(RasterCanvas& canvas, const ui::PianoRollModel& model,
                 double contentBottom) const noexcept;
  void paintKeyboard(RasterCanvas& canvas, const ui::PianoRollModel& model,
                     double contentBottom) const noexcept;
  void paintNotes(RasterCanvas& canvas, const ui::PianoRollModel& model) const noexcept;
  void paintStatus(RasterCanvas& canvas, const ui::PianoRollModel& model,
                   const EditorSceneState& state) const noexcept;

  EditorSceneTheme theme_;
  EditorSceneLayout layout_;
};

}  // namespace seam::native_ui
