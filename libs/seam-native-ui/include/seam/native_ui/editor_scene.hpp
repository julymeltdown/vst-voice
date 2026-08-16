#pragma once

#include "seam/character/character.hpp"
#include "seam/domain/project.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include "seam/ui/piano_roll_model.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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
  Color accentSecondary{110, 90, 134, 255};
  Color playhead{98, 192, 190, 255};
  Color selection{155, 88, 124, 58};
  Color phoneme{70, 67, 87, 255};
  Color unit{56, 72, 78, 255};
  Color automation{186, 104, 145, 255};
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

  phonemizer::Result phonemes;
  std::vector<domain::UnitSelectionOverride> unitOverrides;
  std::vector<domain::PitchAutomationPoint> pitchAutomation;

  domain::CharacterDisplayMode characterMode{domain::CharacterDisplayMode::Minimal};
  character::State characterState{character::State::Neutral};
  std::string characterName;
  std::string characterStyle;
  const PixelSurface* characterPortrait{nullptr};
};

struct EditorSceneLayout final {
  double toolbarHeight{64.0};
  double rulerHeight{34.0};
  double statusHeight{28.0};
  double keyboardWidth{76.0};
  double phonemeLaneHeight{42.0};
  double unitLaneHeight{52.0};
  double automationLaneHeight{72.0};
  double characterDockWidth{238.0};

  [[nodiscard]] double contentTop() const noexcept {
    return toolbarHeight + rulerHeight;
  }
  [[nodiscard]] double lanesHeight() const noexcept {
    return phonemeLaneHeight + unitLaneHeight + automationLaneHeight;
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
                 double editorRight, double contentBottom) const noexcept;
  void paintKeyboard(RasterCanvas& canvas, const ui::PianoRollModel& model,
                     double contentBottom) const noexcept;
  void paintNotes(RasterCanvas& canvas,
                  const ui::PianoRollModel& model) const noexcept;
  void paintTechnicalLanes(RasterCanvas& canvas, const ui::PianoRollModel& model,
                           const EditorSceneState& state, double editorRight,
                           double pianoBottom) const noexcept;
  void paintCharacter(RasterCanvas& canvas, const EditorSceneState& state,
                      double editorRight, double contentBottom) const noexcept;
  void paintStatus(RasterCanvas& canvas, const ui::PianoRollModel& model,
                   const EditorSceneState& state) const noexcept;

  EditorSceneTheme theme_;
  EditorSceneLayout layout_;
};

}  // namespace seam::native_ui
