#pragma once

#include "seam/native_ui/design/design_tokens.hpp"
#include "seam/native_ui/design/sing_layout.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace seam::native_ui::design {

// Application preferences for the redesigned shell. They are never project data.
struct DesignPreferences final {
  DesignMode mode{DesignMode::Emo};
  Contrast contrast{Contrast::Standard};
  bool shellEnabled{true};
};

[[nodiscard]] DesignPreferences loadDesignPreferences();
void saveDesignPreferences(const DesignPreferences& preferences);

struct ModeAssets final {
  std::shared_ptr<const paint::Image> portrait;
  std::shared_ptr<const paint::Image> stage;
  std::shared_ptr<const paint::Image> wordmark;
};

// Finds assets/ui-design next to a bundle, in an explicit override, or in the source tree for
// development builds. Returns an empty path when no asset directory exists.
[[nodiscard]] std::filesystem::path locateDesignAssets(
    const std::filesystem::path& bundleResources = {});

// The SING workspace shell for the EMO and SCENE designs. It paints around the existing editing
// engine: pointer events inside the musical grid are translated into the legacy controller's
// coordinates, so note creation, selection, lyric entry and vibrato editing keep their existing
// behavior and undo history. Any legacy modal surface (voice browser, audio settings, reviews,
// tempo/meter entry, microscope) is shown by the legacy painter until it is re-homed.
class SingShell final {
public:
  // A shell starts inactive: it paints nothing and forwards all input, and it reads neither the
  // saved preferences nor the design assets. Production hosts call activate(); library tests keep
  // the classic editor and never observe the user's saved design mode.
  SingShell() = default;
  void activate(const std::filesystem::path& assetRoot = locateDesignAssets());
  // Test and screenshot entry point: an explicit preference set, no persistence.
  void activate(const std::filesystem::path& assetRoot, DesignPreferences preferences);

  [[nodiscard]] bool available() const noexcept { return paint::vectorBackendAvailable(); }
  [[nodiscard]] bool active() const noexcept { return active_; }
  [[nodiscard]] bool enabled() const noexcept { return active_ && preferences_.shellEnabled; }
  [[nodiscard]] DesignMode mode() const noexcept { return preferences_.mode; }
  [[nodiscard]] bool presentedLastFrame() const noexcept { return presented_; }
  [[nodiscard]] const SingLayout& layout() const noexcept { return layout_; }
  [[nodiscard]] static bool legacySurfaceRequired(const EditorSceneState& state) noexcept;

  void setMode(DesignMode mode, bool persist = true);
  void setEnabled(bool enabled, bool persist = true);
  void setRepaintCallback(std::function<void()> callback) { repaint_ = std::move(callback); }
  [[nodiscard]] bool assetsLoaded(DesignMode mode) const noexcept;

  // Paints the shell and returns true, or returns false so the caller paints the legacy editor.
  bool paint(RasterCanvas& canvas, ui::PianoRollModel& model, const EditorSceneState& state,
             time::Tick playhead);

  core::Result<void> pointerDown(NativeEditorController& controller, const PointerEvent& event);
  core::Result<void> pointerMove(NativeEditorController& controller, const PointerEvent& event);
  core::Result<void> pointerUp(NativeEditorController& controller, const PointerEvent& event);
  // Returns true when the shell consumed the scroll.
  bool scroll(NativeEditorController& controller, double deltaX, double deltaY, ui::Point anchor,
              InputModifiers modifiers);
  // Returns true when the key toggles the shell itself (Command-Shift-Space).
  bool handleShellKey(const KeyEvent& event);
  [[nodiscard]] TextInputRequest translateTextInput(TextInputRequest request) const noexcept;

  // Converts a rectangle published in the legacy editor's window coordinates into the shell.
  [[nodiscard]] ui::Rect fromLegacy(ui::Rect rect) const noexcept;
  [[nodiscard]] ui::Point toLegacy(ui::Point point) const noexcept;

private:
  struct KnobDrag final {
    std::size_t index{0U};
    double startY{0.0};
    int steps{0};
    bool moved{false};
  };

  void repaint() const {
    if (repaint_) repaint_();
  }
  const ModeAssets& assets() const noexcept;
  void ensureBackground(const RasterCanvas& canvas, const DesignTokens& tokens);
  void paintBackground(paint::Canvas2D& c, const DesignTokens& t) const;
  void paintHeader(paint::Canvas2D& c, const DesignTokens& t, const EditorSceneState& state,
                   time::Tick playhead) const;
  void paintEditor(paint::Canvas2D& c, const DesignTokens& t, ui::PianoRollModel& model,
                   const EditorSceneState& state) const;
  void paintLane(paint::Canvas2D& c, const DesignTokens& t, const ui::PianoRollModel& model,
                 const EditorSceneState& state) const;
  void paintRack(paint::Canvas2D& c, const DesignTokens& t, const EditorSceneState& state) const;
  void paintStatus(paint::Canvas2D& c, const DesignTokens& t, const EditorSceneState& state) const;
  [[nodiscard]] bool inMusicalArea(ui::Point point) const noexcept;
  [[nodiscard]] PointerEvent translated(const PointerEvent& event) const noexcept;
  // Tells the controller whether the shell owns the chrome around the grid this frame.
  void syncHostedGrid(NativeEditorController& controller) const noexcept;
  core::Result<void> nudge(NativeEditorController& controller, std::size_t index, int steps);

  DesignPreferences preferences_;
  bool active_{false};
  bool persist_{true};
  std::array<ModeAssets, 2U> assets_{};
  SingLayout layout_;
  double legacyContentTop_{EditorSceneLayout{}.contentTop()};
  std::int64_t ppq_{960};
  bool presented_{false};
  bool forwarding_{false};
  std::optional<KnobDrag> knobDrag_;
  std::array<bool, 6U> knobRefused_{};
  double scrollAccumulator_{0.0};
  std::function<void()> repaint_;

  PixelSurface background_;
  double backgroundScale_{0.0};
  DesignMode backgroundMode_{DesignMode::Emo};
  bool backgroundValid_{false};
};

}  // namespace seam::native_ui::design
