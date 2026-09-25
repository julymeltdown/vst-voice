#pragma once

#include "seam/native_ui/accessibility_tree.hpp"
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
  [[nodiscard]] std::optional<std::size_t> lastOffscreenHint() const noexcept { return offscreenHint_; }
  [[nodiscard]] static bool legacySurfaceRequired(const EditorSceneState& state) noexcept;

  void setMode(DesignMode mode, bool persist = true);
  void setEnabled(bool enabled, bool persist = true);
  // Enables or disables the shell while a controller is attached: gestures are cancelled and the
  // controller's input geometry is returned to the classic editor before the switch.
  void setEnabled(NativeEditorController& controller, bool enabled);
  void setRepaintCallback(std::function<void()> callback) { repaint_ = std::move(callback); }
  [[nodiscard]] bool assetsLoaded(DesignMode mode) const noexcept;

  // Frame step 1, before the host derives its scene state: chooses the surface and applies its
  // complete geometry (piano-roll viewport and the controller's hosted input geometry) so paint,
  // scene state, IME anchors and pointer routing all read one geometry. Returns true when the shell
  // will present. When it will not, the classic input geometry is restored here, even if no shell
  // input event ever runs again.
  bool prepareFrame(NativeEditorController& controller, double logicalWidth, double logicalHeight);
  // Frame step 2: paints and returns true, or returns false (restoring classic input geometry) so
  // the caller paints the legacy editor.
  bool paint(RasterCanvas& canvas, NativeEditorController& controller,
             const EditorSceneState& state, time::Tick playhead);
  // Abandons shell and controller gestures without committing them (capture loss, hide).
  void cancelGestures(NativeEditorController& controller);

  core::Result<void> pointerDown(NativeEditorController& controller, const PointerEvent& event);
  core::Result<void> pointerMove(NativeEditorController& controller, const PointerEvent& event);
  core::Result<void> pointerUp(NativeEditorController& controller, const PointerEvent& event);
  // Returns true when the shell consumed the scroll.
  bool scroll(NativeEditorController& controller, double deltaX, double deltaY, ui::Point anchor,
              InputModifiers modifiers);
  // Returns true when the shell consumed the key: Command-Shift-Space toggles the shell, and Escape
  // cancels a knob drag or a forwarded pointer gesture without committing it.
  bool handleShellKey(NativeEditorController& controller, const KeyEvent& event);
  // Lyric requests come from note bounds in the shell's viewport and are moved into shell space.
  // Every other request (tempo/meter, hint, replacement fields) belongs to a classic surface that
  // replaces the shell on the next frame and keeps its classic coordinates.
  [[nodiscard]] TextInputRequest translateTextInput(TextInputRequest request);
  void textInputEnded() noexcept { lyricInputActive_ = false; }

  // Accessibility for the presented shell, built from the same layout snapshot that paint and
  // pointer routing use. Notes and the timeline keep their controller ids (actions still reach the
  // controller); controls the shell draws get shell ids and real roles: sliders with ranges for
  // the knobs, radio buttons for EMO/SCENE, tabs for workspaces and lane channels, a progress
  // indicator for rendering. Classic-only nodes without a visual here are left out.
  void rebuildSemantics(const NativeEditorController& controller, const EditorSceneState& state);
  [[nodiscard]] const AccessibilityTree& accessibilityTree() const noexcept { return semantics_; }
  [[nodiscard]] static bool ownsSemantic(std::string_view id) noexcept {
    return id.starts_with("shell.");
  }
  // Performs an action on a shell-owned node. Controller ids are dispatched by the host.
  core::Result<void> dispatchSemantic(NativeEditorController& controller, std::string_view id,
                                      SemanticAction action);
  // Called when the host sends an action to a controller id, so shell focus follows it.
  void controllerFocusTaken() noexcept { semanticFocus_.clear(); }

  // Converts a rectangle published in the legacy editor's window coordinates into the shell.
  [[nodiscard]] ui::Rect fromLegacy(ui::Rect rect) const noexcept;
  [[nodiscard]] ui::Point toLegacy(ui::Point point) const noexcept;

private:
  // A knob gesture is bound to the document, region and playhead it started on; if any of them
  // changes before release, the release commits nothing.
  struct KnobDrag final {
    std::size_t index{0U};
    double startY{0.0};
    int steps{0};
    bool moved{false};
    std::uint64_t revision{0U};
    domain::RegionId region;
    time::Tick playhead{0};
  };
  enum class ForwardArea : std::uint8_t { None, Grid, Lane };

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
  [[nodiscard]] bool inEditableLane(ui::Point point) const noexcept;
  [[nodiscard]] PointerEvent translated(const PointerEvent& event, ForwardArea area) const noexcept;
  [[nodiscard]] NativeEditorController::HostedGeometry hostedGeometry() const noexcept;
  void applyGeometry(NativeEditorController& controller);
  void releaseSurface(NativeEditorController& controller);
  // Hands the frame to a classic surface as soon as a shell command opens one, before the repaint.
  void yieldIfModal(NativeEditorController& controller);
  core::Result<void> nudge(NativeEditorController& controller, std::size_t index, int steps);
  core::Result<void> shellPointerDown(NativeEditorController& controller, const PointerEvent& event);
  core::Result<void> performSemantic(NativeEditorController& controller, std::string_view id,
                                     SemanticAction action);
  // Shell focus remembers the controller's focus at the moment it was taken; once the controller's
  // focus moves, the shell's is dropped.
  void takeSemanticFocus(NativeEditorController& controller, std::string id);
  // Rebuilds the controller's tree and the shell's from the current state and layout.
  void refreshSemantics(NativeEditorController& controller);
  // Controller controls the shell shows at its own rectangles (not notes, the timeline or vibrato
  // handles, whose keys belong to the score editor).
  [[nodiscard]] static bool rehomedControl(std::string_view id) noexcept;

  DesignPreferences preferences_;
  bool active_{false};
  bool persist_{true};
  std::array<ModeAssets, 2U> assets_{};
  SingLayout layout_;
  double legacyContentTop_{EditorSceneLayout{}.contentTop()};
  std::int64_t ppq_{960};
  bool presented_{false};
  ForwardArea forwarding_{ForwardArea::None};
  bool laneEditable_{false};
  bool lyricInputActive_{false};
  // Direction of the last off-screen-notes hint (0 above, 1 below, 2 earlier, 3 later); exposed so
  // the hint's direction is testable without reading pixels.
  mutable std::optional<std::size_t> offscreenHint_;
  std::optional<KnobDrag> knobDrag_;
  std::array<bool, 6U> knobRefused_{};
  double scrollAccumulator_{0.0};
  AccessibilityTree semantics_;
  std::string semanticFocus_;
  std::string semanticFocusBaseline_;
  std::function<void()> repaint_;

  PixelSurface background_;
  double backgroundScale_{0.0};
  DesignMode backgroundMode_{DesignMode::Emo};
  bool backgroundValid_{false};
};

}  // namespace seam::native_ui::design
