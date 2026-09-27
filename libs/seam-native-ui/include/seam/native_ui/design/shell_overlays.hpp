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
#include "seam/native_ui/design/shell_strings.hpp"
#include "seam/native_ui/design/design_tokens.hpp"
#include "seam/native_ui/design/sing_layout.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <functional>

namespace seam::native_ui::design {

enum class OverlayKind : std::uint8_t {
  None,
  SampleMicroscope,
  PhonemeReview,
  TimeMap,
  RecoverySupport,
  OverlapDetail,
  Diagnostics,
  ReplacementReview,
  AudioSettings,
  VoicebankBrowser,
  TextField,
  SingerMenu,
  About,
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
  // The full value the control stands for (a row's complete text, a device's kind, a field's text
  // as typed), published even where the painted label elides it, and its longer description. Empty
  // means the controller's node for the same id supplies them.
  std::string value{};
  std::string description{};
  // A text field: published as editable (SetFocus and EditText), its value the text as typed.
  bool editable{false};
  // A stepped setting (sample rate, buffer size, channels): Increment and Decrement step it.
  bool adjustable{false};
};

// A pointer gesture an overlay started inside its own plot. The plot is a controller model measured
// in the classic layout; the gesture keeps the mapping it started with (the classic plot and the
// card rectangle it was drawn in), so every move and the release reach the controller in the
// geometry the press used, whatever happens to the card meanwhile.
struct OverlayGesture final {
  ui::Rect source;
  ui::Rect destination;
};
struct OverlayPress final {
  bool handled{false};
  core::Result<void> result{core::success()};
  std::optional<OverlayGesture> gesture;
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
  // Escape's first step, for a surface with an inner page it returns from before closing (the
  // microscope's details). Returns true when it stepped back and the surface stays open.
  [[nodiscard]] virtual bool back(NativeEditorController& controller) const {
    static_cast<void>(controller);
    return false;
  }
  // Called once when the shell starts presenting the overlay, so a presentation-only position (a
  // popover's page) starts from the top each time it opens.
  virtual void presented() const {}
  // Called instead of presented() when the overlay is shown again because a field it opened over
  // itself (a review's draft field) closed, so the place it was showing is kept.
  virtual void resumed() const {}
  // A press the overlay handles itself before its controls are hit-tested (a plot it forwards to
  // the controller). Unhandled by default.
  [[nodiscard]] virtual OverlayPress press(NativeEditorController& controller,
                                           const EditorSceneState& state,
                                           const SingLayout& layout, ui::Rect panel,
                                           const PointerEvent& event) const {
    static_cast<void>(controller);
    static_cast<void>(state);
    static_cast<void>(layout);
    static_cast<void>(panel);
    static_cast<void>(event);
    return {};
  }
  // A move (release false) or the release of a gesture press() started.
  virtual core::Result<void> drag(NativeEditorController& controller, const OverlayGesture& gesture,
                                  const PointerEvent& event, bool release) const {
    static_cast<void>(controller);
    static_cast<void>(gesture);
    static_cast<void>(event);
    static_cast<void>(release);
    return core::success();
  }
  // A scroll over the card. Returns true when the overlay used it; the shell absorbs it either way.
  virtual bool scroll(NativeEditorController& controller, const EditorSceneState& state,
                      const SingLayout& layout, ui::Rect panel, ui::Point anchor, double deltaX,
                      double deltaY, InputModifiers modifiers) const {
    static_cast<void>(controller);
    static_cast<void>(state);
    static_cast<void>(layout);
    static_cast<void>(panel);
    static_cast<void>(anchor);
    static_cast<void>(deltaX);
    static_cast<void>(deltaY);
    static_cast<void>(modifiers);
    return false;
  }
  // A value an assistive client set on one of the overlay's editable controls (a text field).
  virtual core::Result<void> setValue(NativeEditorController& controller, std::string_view id,
                                      std::string_view value) const {
    static_cast<void>(controller);
    static_cast<void>(id);
    static_cast<void>(value);
    return core::failure(core::ErrorCode::Unsupported, tr(Str::ThisControlHasNoEditableValue));
  }
};

[[nodiscard]] std::unique_ptr<ShellOverlay> makeSampleMicroscopeOverlay();
[[nodiscard]] std::unique_ptr<ShellOverlay> makePhonemeReviewOverlay();
[[nodiscard]] std::unique_ptr<ShellOverlay> makeTimeMapOverlay();
[[nodiscard]] std::unique_ptr<ShellOverlay> makeRecoverySupportOverlay();
[[nodiscard]] std::unique_ptr<ShellOverlay> makeOverlapDetailOverlay();
[[nodiscard]] std::unique_ptr<ShellOverlay> makeDiagnosticsOverlay();
// The surfaces that were the last to hand the frame to the classic painter: the replacement review
// (and every review the controller shows in that panel: find, cleanup, vibrato, dynamics, style,
// Japanese reading), the audio settings, the voice browser, and the classic-only text fields
// (tempo or meter from the transport, phone hint, find/replace, review draft fields, renames).
[[nodiscard]] std::unique_ptr<ShellOverlay> makeReplacementReviewOverlay();
[[nodiscard]] std::unique_ptr<ShellOverlay> makeAudioSettingsOverlay();
[[nodiscard]] std::unique_ptr<ShellOverlay> makeVoicebankBrowserOverlay();
[[nodiscard]] std::unique_ptr<ShellOverlay> makeTextFieldOverlay();
// The SINGER card's overflow menu: a popover anchored to the card's ⋯ button listing the
// controller's own singer commands (the replacement review, the dynamics, vibrato and style
// inspectors, Japanese reading, phoneme review, the voice browser and the voicebank installer and
// rescan). Each item runs that public command and nothing else; an item the controller refused is
// disabled with the controller's reason until the menu opens again.
[[nodiscard]] std::unique_ptr<ShellOverlay> makeSingerMenuOverlay();
// The ids of the singer menu's items, in menu order, and the shell id of the button that opens it.
inline constexpr std::string_view kSingerMenuButtonId{"shell.singer-menu"};
[[nodiscard]] std::vector<std::string> singerMenuItemIds();

// The About sheet (the app menu's "About Project SEAM"): the current mode's splash key art with the
// product name, version and build over the art's scrimmed left clear area, and a Close button. The
// art is read through the callback at paint time, so a mode switch while it is open shows the new
// mode's art. The shell holds whether it is open; Close, Escape and a press outside close it.
[[nodiscard]] std::unique_ptr<ShellOverlay> makeAboutOverlay(
    std::function<const paint::Image*()> art);
inline constexpr std::string_view kAboutCloseId{"shell.overlay.about.close"};
// The art's rectangle inside the About card.
[[nodiscard]] ui::Rect aboutArtBounds(ui::Rect panel) noexcept;

// Where a classic-only text field sits in the shell, for a request anchored to it. The shell moves
// the host's text input client there (translateTextInput), and the overlay that draws the field
// lays it out from the same function, so the IME candidate window, the painted field, its hit
// rectangle and its accessible bounds are one rectangle.
struct TextFieldPlacement final {
  ui::Rect panel;
  ui::Rect input;
  ui::Rect cancel;
};
// The time map's card in a slot, and the event field inside that card.
[[nodiscard]] ui::Rect timeMapPanelBounds(ui::Rect slot);
[[nodiscard]] TextFieldPlacement timeMapFieldPlacement(ui::Rect timeMapPanel);
// Every other anchor: the transport's tempo or meter field under the transport display, and the
// bounded field (hint, find/replace, draft fields, renames) as a field card at the top of the body.
[[nodiscard]] TextFieldPlacement textFieldPlacement(TextInputAnchor anchor, const SingLayout& layout);

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
