#pragma once

// Re-homed overlays (docs/design/SEAM_UI_REDESIGN_CODE_PLAN_2026-09-25.md section 7.6). Each
// surface that used to hand the whole frame back to the classic painter is a panel or popover
// inside the shell: the shell draws it with the design tokens and the card material it already
// uses, the covered score is neither published nor editable while it is open, and Escape closes it.
//
// One snapshot drives all three uses. An overlay lays its controls out once (`controls`), and the
// shell paints, hit-tests and publishes exactly those rectangles, so a control's hit rectangle is
// its painted rectangle. Each control keeps the id and actions of the controller node it re-homes,
// so its real command still reaches the same controller code the classic painter called.

#include "seam/native_ui/accessibility_tree.hpp"
#include "seam/native_ui/design/design_tokens.hpp"
#include "seam/native_ui/design/sing_layout.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace seam::native_ui::design {

enum class OverlayKind : std::uint8_t {
  None,
  SampleMicroscope,
  PhonemeReview,
  TimeMap,
  RecoverySupport,
  OverlapDetail,
  Diagnostics,
};

// One control of a re-homed overlay: the controller node it re-homes and the rectangle the shell
// paints it in. `bounds` is the painted rectangle and the hit rectangle, in shell coordinates, and
// `id` is the controller's own node id, so the shell publishes the controller's role, name, value
// and actions at the shell's rectangle and the control's command is the controller's own.
struct OverlayControl final {
  // The controller node this control re-homes, when one exists with the same id (Close, Details,
  // the time-map rows and actions, the recovery items). An overlay's own row that the controller
  // lists without its own node (an overlap member) keeps this id too, so `perform` can address it.
  std::string id;
  ui::Rect bounds;
  // What the shell publishes. The role and name are the overlay's own statement about the control,
  // so a surface whose controller node the classic tree hides (a support item, which the legacy
  // tree only lists when the dock leaves room) is still published with a real role. Where the
  // controller does publish the id, its value, description and extra actions are merged in.
  std::string name;
  SemanticRole role{SemanticRole::Button};
  bool enabled{true};
  bool selected{false};
  bool activatable{true};
};

class ShellOverlay {
public:
  virtual ~ShellOverlay() = default;

  [[nodiscard]] virtual OverlayKind kind() const noexcept = 0;
  // "shell.overlay.microscope." and friends: every id the overlay publishes starts with it.
  [[nodiscard]] virtual std::string_view idPrefix() const noexcept = 0;
  // True while the controller's model asks for this surface at all (before layout decisions).
  [[nodiscard]] virtual bool wanted(const NativeEditorController& controller,
                                    const EditorSceneState& state) const noexcept = 0;
  // The card the overlay paints, in shell coordinates. Empty when the window cannot hold it.
  [[nodiscard]] virtual ui::Rect panel(const NativeEditorController& controller,
                                       const EditorSceneState& state, const SingLayout& layout,
                                       ui::Rect slot) const = 0;
  [[nodiscard]] virtual std::string title(const NativeEditorController& controller,
                                          const EditorSceneState& state) const = 0;
  // Every control, in Tab order, at the rectangles the shell paints and hit-tests.
  [[nodiscard]] virtual std::vector<OverlayControl> controls(
      const NativeEditorController& controller, const EditorSceneState& state,
      const SingLayout& layout, ui::Rect panel) const = 0;
  // The +N badge rectangle of a note drawn with an overlap indicator, in shell coordinates, or an
  // empty rectangle. The shell derives it from the notes it is painting, so the overlap popover
  // anchors to the badge the creator actually clicked. `noteId` selects the group's own note.
  [[nodiscard]] virtual ui::Rect badge(const NativeEditorController& controller,
                                       const EditorSceneState& state,
                                       const SingLayout& layout) const {
    static_cast<void>(controller);
    static_cast<void>(state);
    static_cast<void>(layout);
    return {};
  }
  // The id of the control that opened this surface, so closing it returns focus there (Escape and
  // the shell's own close both restore the same control). Empty when the opener is not a node the
  // shell publishes: the microscope is opened from a note in the covered score, so focus is left
  // cleared rather than pointed at something that is no longer in the tree.
  [[nodiscard]] virtual std::string openerId(const NativeEditorController& controller,
                                            const EditorSceneState& state) const {
    static_cast<void>(controller);
    static_cast<void>(state);
    return {};
  }
  // Paints the panel's content and its control chrome. The shell has already drawn the scrim, the
  // card and the title; every control in `controls` is drawn at its own rectangle.
  virtual void paint(paint::Canvas2D& c, const DesignTokens& tokens,
                     const NativeEditorController& controller, const EditorSceneState& state,
                     const SingLayout& layout, ui::Rect panel,
                     const std::vector<OverlayControl>& controls) const = 0;
  // Performs one of the overlay's own nodes. SetFocus is handled by the shell.
  virtual core::Result<void> perform(NativeEditorController& controller, std::string_view id,
                                     SemanticAction action) const = 0;
  // A plain key while one of this overlay's controls holds focus. Returns true when consumed; the
  // shell stops every other plain key anyway, so nothing reaches the covered score.
  virtual bool key(NativeEditorController& controller, std::string_view focusedId,
                   const KeyEvent& event) const = 0;
  // Closes the surface through the controller's own command, so the state the classic painter read
  // changes exactly as it did when its close control ran.
  [[nodiscard]] virtual core::Result<void> close(NativeEditorController& controller) const = 0;
};

[[nodiscard]] std::unique_ptr<ShellOverlay> makeSampleMicroscopeOverlay();
[[nodiscard]] std::unique_ptr<ShellOverlay> makePhonemeReviewOverlay();
[[nodiscard]] std::unique_ptr<ShellOverlay> makeTimeMapOverlay();
[[nodiscard]] std::unique_ptr<ShellOverlay> makeRecoverySupportOverlay();
[[nodiscard]] std::unique_ptr<ShellOverlay> makeOverlapDetailOverlay();
[[nodiscard]] std::unique_ptr<ShellOverlay> makeDiagnosticsOverlay();

// The node an overlay re-homes, searched through the whole tree (controller nodes nest), or null
// when it is not published right now: the surface closed, or its state changed under it.
[[nodiscard]] const SemanticNode* overlayControllerNode(const AccessibilityTree& tree,
                                                        std::string_view id) noexcept;
// The controller node whose id ends with `suffix`, or null. The time map's ids carry a
// per-interaction prefix, so its rows and actions are re-homed by their stable suffix.
[[nodiscard]] const SemanticNode* overlayControllerNodeEndingWith(const AccessibilityTree& tree,
                                                                  std::string_view suffix) noexcept;

// Paints the scrim, the card in the design material and the title, then the overlay's own content.
// Returns the painted panel rectangle (empty when the window cannot hold one).
ui::Rect paintShellOverlay(const ShellOverlay& overlay, paint::Canvas2D& c, const DesignTokens& t,
                           const NativeEditorController& controller,
                           const EditorSceneState& state, const SingLayout& layout,
                           ui::Rect slot);

// A standard control rectangle's skin, shared by the overlays so every surface's buttons, rows and
// fields look like the shell's own. `role` is the published role, which picks the treatment.
void paintOverlayControl(paint::Canvas2D& c, const DesignTokens& t, ui::Rect r, std::string_view label,
                         SemanticRole role, bool enabled, bool selected, bool focused);

}  // namespace seam::native_ui::design
