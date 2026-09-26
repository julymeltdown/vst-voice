#pragma once

#include "seam/native_ui/accessibility_tree.hpp"
#include "seam/native_ui/design/character_surface.hpp"
#include "seam/native_ui/design/design_tokens.hpp"
#include "seam/native_ui/design/shell_workspace.hpp"
#include "seam/native_ui/design/sing_layout.hpp"
#include "seam/native_ui/design/voice_workspace.hpp"
#include "seam/native_ui/design/shell_overlays.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/region_envelope.hpp"

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
  // Reduce Motion drops blink, breathing, the render spinner and the Stage fade, and makes a state
  // change immediate. The state itself is unchanged: a screen that reduces motion still says what
  // the singer is doing. It is the shell's own preference, alongside the look and the contrast, and
  // it is the same setting the host already publishes to the editor through
  // platform::AccessibilityPreferences, so both surfaces agree.
  bool reduceMotion{false};
  bool shellEnabled{true};
};

[[nodiscard]] DesignPreferences loadDesignPreferences();
void saveDesignPreferences(const DesignPreferences& preferences);

struct ModeAssets final {
  std::shared_ptr<const paint::Image> portrait;
  std::shared_ptr<const paint::Image> stage;
  std::shared_ptr<const paint::Image> wordmark;
};

// The shell's workspaces. VOICE, TUNE, MIX and EXPORT cover the score with their own body; the
// voice browser stays behind the singer card's "Change voice".
enum class Workspace : std::uint8_t { Sing, Voice, Tune, Mix, Export };

// What an Export Set will write, as the host computed it. Nothing here is a guess by the shell.
struct ShellExportPlan final {
  std::uint32_t sampleRate{0U};
  std::uint8_t channels{0U};
  std::string format;
  bool master{false};
  bool stems{false};
  bool asksAboutPackaging{false};
};

// Host commands the shell can run. A host that cannot export from the editor (a plug-in, whose DAW
// owns rendering and files) leaves exportSet empty and states why in exportUnavailable.
struct ShellHostActions final {
  std::function<core::Result<void>()> exportSet;
  std::function<std::optional<ShellExportPlan>()> exportPlan;
  std::string exportUnavailable{"This host does not export from the editor"};
  // The host's live export worker state; the shell also reads the editor's current progress.
  std::function<bool()> exportBusy;
  // The selected region's own rendered audio for the notes, or why there is none. A host without
  // one leaves it empty and the grid says so.
  std::function<RegionWaveform()> regionWaveform;
  // True for a modified key this host itself handles as an application command (save, quit, undo).
  // While EXPORT hides the score only these pass on; every other modified key stops at the shell,
  // so a shortcut the host does not implement can never fall through to a note-editing command.
  std::function<bool(const KeyEvent&)> applicationShortcut;
  // The VOICE workspace's Voice Designer session, dialogs and audition output. A host without a
  // designer (a plug-in) leaves it empty and VOICE says voice design runs in the standalone app.
  ShellVoiceHost voice;
};

// Finds assets/ui-design next to a bundle, in an explicit override, or in the source tree for
// development builds. Returns an empty path when no asset directory exists.
[[nodiscard]] std::filesystem::path locateDesignAssets(
    const std::filesystem::path& bundleResources = {});

// Finds the character package: an explicit override, character-01 beside the design assets, or
// character-01 in the source tree. Returns an empty path when no package exists, and the shell then
// draws the look's own portrait and claims no package state.
[[nodiscard]] std::filesystem::path locateCharacterAssets(
    const std::filesystem::path& designAssets = {},
    const std::filesystem::path& bundleResources = {});

// The in-note waveform is drawn in columns of this width, phased from the note's left edge.
inline constexpr double kNoteWaveformColumn = 2.0;
// Calls column(x0, x1) for each waveform column of a note rectangle that lies inside
// [visibleLeft, visibleRight) and returns how many there were. The work is bounded by the visible
// span, however long the note is at the current zoom.
std::size_t noteWaveformColumns(ui::Rect note, double visibleLeft, double visibleRight,
                                const std::function<void(double, double)>& column);

// The status bar's left message: the audio device, else the most important diagnostic, else the
// render note. A failed render always names its reason, because "Render did not complete" alone
// gives the user nothing to act on. The painter elides it at the bar's width.
enum class StatusTone : std::uint8_t { Normal, Warning };
struct StatusMessage final {
  std::string text;
  StatusTone tone{StatusTone::Normal};
};
[[nodiscard]] StatusMessage singStatusMessage(const EditorSceneState& state);

// The SING workspace shell for the EMO and SCENE designs. It paints around the existing editing
// engine: pointer events inside the musical grid are translated into the legacy controller's
// coordinates, so note creation, selection, lyric entry and vibrato editing keep their existing
// behavior and undo history. Any legacy modal surface (voice browser, audio settings, reviews,
// tempo/meter entry, microscope) is shown by the legacy painter until it is re-homed. The surfaces
// section 7.6 of the redesign plan lists as re-homed (sample microscope, phoneme review, time map,
// recovery/support, overlap detail, diagnostics and the export progress strip) are painted here as
// panel and popover surfaces by shell_overlays.hpp; the voice browser, the audio settings, the
// replacement review and the classic-only text inputs still hand the frame to the legacy painter.
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
  // The compact rack's singer inspector (rail and drawer presentations only).
  [[nodiscard]] bool inspectorOpen() const noexcept { return layout_.inspectorOpen; }
  // The re-homed overlay the shell would present now, or None. Read from the controller's own
  // state and the window, so a host that never paints still sees which surface is up.
  [[nodiscard]] OverlayKind overlayKind(const NativeEditorController& controller) const;
  // The rectangle a re-homed overlay's card may occupy: the body right of the musical axis, never
  // over the header. Empty when the window is too small to place one.
  [[nodiscard]] ui::Rect overlaySlot(const NativeEditorController& controller,
                                     const EditorSceneState& state) const noexcept;
  // True while the shell paints a re-homed overlay and covers the score with it.
  [[nodiscard]] bool overlayPresented(const NativeEditorController& controller) const;
  // The DIAGNOSTICS popover is a presentation the shell owns (the controller folds diagnostics into
  // the status bar); this opens and closes it.
  [[nodiscard]] bool diagnosticsOpen() const noexcept { return diagnosticsOpen_; }
  void setDiagnosticsOpen(bool open);
  // Whether the last SING frame painted the Stage figure; §3.4 keeps it off without the full rack.
  [[nodiscard]] bool lastFrameShowedStage() const noexcept { return stageShown_; }
  // Where the last SING frame drew the Stage figure, or nothing when it drew none. The figure is
  // decorative, so this exists for evidence and tests rather than for input: nothing routes to it.
  [[nodiscard]] std::optional<ui::Rect> lastFrameStageBounds() const noexcept {
    return stagePlacement_.has_value() && stagePlacement_->shown
               ? std::optional<ui::Rect>{stagePlacement_->bounds}
               : std::nullopt;
  }
  [[nodiscard]] std::optional<std::size_t> lastOffscreenHint() const noexcept { return offscreenHint_; }
  [[nodiscard]] static bool legacySurfaceRequired(const EditorSceneState& state) noexcept;
  // The overlays this shell re-homes. A surface listed here is painted inside the shell, so it is
  // not one of the states that still hands the frame to the classic painter.
  [[nodiscard]] static bool rehomedSurface(OverlayKind kind) noexcept;

  void setMode(DesignMode mode, bool persist = true);
  // Turns motion down. Like the look and the contrast it is an application preference, so the shell
  // keeps painting the same state with the animation dropped.
  void setReduceMotion(bool reduceMotion, bool persist = true);
  void setEnabled(bool enabled, bool persist = true);
  // Enables or disables the shell while a controller is attached: gestures are cancelled and the
  // controller's input geometry is returned to the classic editor before the switch.
  void setEnabled(NativeEditorController& controller, bool enabled);
  void setRepaintCallback(std::function<void()> callback) { repaint_ = std::move(callback); }
  void setHostActions(ShellHostActions actions) {
    hostActions_ = std::move(actions);
    voice_->setHost(hostActions_.voice);
  }
  // The host's own character package directory, when it has one it would rather use than the one
  // beside the design assets. Loading it is optional: an empty path or a refused package leaves the
  // shell on the look's own portrait alone, and packageError() says why if a package was refused.
  void setCharacterPackage(const std::filesystem::path& packageRoot);
  [[nodiscard]] bool characterPackageLoaded() const noexcept {
    return character_.packageLoaded();
  }
  [[nodiscard]] const std::string& characterPackageError() const noexcept {
    return character_.packageError();
  }
  // Set by the VOICE workspace while an audition plays, so the protagonist can listen to it: the
  // measured peak of the audition the output device last played, or nothing when nothing plays. The
  // shell reads it once per frame and clears it when VOICE is not the visible workspace.
  void setAuditionLevel(std::optional<float> level) noexcept { auditionLevel_ = level; }
  // The clock the character animation reads. The host's injectable UI clock is the intended source,
  // so a frozen clock freezes the blink, the breathing and the Stage fade along with everything else;
  // it defaults to the steady clock for a host that injects nothing.
  void setUiClock(std::function<std::chrono::steady_clock::time_point()> clock) {
    uiClock_ = std::move(clock);
  }
  [[nodiscard]] Workspace workspace() const noexcept { return workspace_; }
  // The VOICE, TUNE or MIX body while it is shown, else null.
  [[nodiscard]] ShellWorkspace* bodyWorkspace() const noexcept;
  // Undo and redo belong to the Voice Designer while VOICE is shown: the editor's history is not
  // on screen there. Returns the designer's result then, and nothing in any other workspace.
  [[nodiscard]] std::optional<core::Result<void>> routeUndo(bool redo);
  // The rectangle a covering workspace (TUNE, MIX, EXPORT) owns, in shell coordinates.
  [[nodiscard]] ui::Rect workspaceArea() const noexcept { return exportArea(); }
  // Switches the visible workspace. Gestures and a note-grid lyric field are abandoned first: the
  // grid they belong to is no longer on screen.
  void setWorkspace(NativeEditorController& controller, Workspace workspace);
  // The Export workspace's run button, in shell coordinates (empty unless that workspace shows).
  [[nodiscard]] ui::Rect exportRunButton() const noexcept;
  // The export-progress segment in the status bar: the strip the classic painter drew full width,
  // now a segment that names the attempt and carries the cancel action. Empty when no export has
  // ever reported files.
  [[nodiscard]] ui::Rect exportStatusSegment(const EditorSceneState& state) const noexcept;
  [[nodiscard]] bool assetsLoaded(DesignMode mode) const noexcept;
  // The protagonist's real state for the last painted frame, from the read models the shell holds.
  [[nodiscard]] CharacterState characterState() const noexcept { return characterState_; }

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
  // Host boundary for editor elements while the shell presents. An action or value reaches the
  // editor only for an element the shell publishes right now: a retained host element for the
  // covered score (EXPORT) or for a control the layout removed is refused, whatever flags it kept.
  core::Result<void> dispatchController(NativeEditorController& controller, std::string_view id,
                                        SemanticAction action);
  core::Result<void> setControllerValue(NativeEditorController& controller, std::string_view id,
                                        std::string_view value);
  // Live export state (the host's worker, the editor's current progress), never a painted cache.
  [[nodiscard]] bool exportBusy(const NativeEditorController& controller) const;

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
  // The protagonist's artwork for a state: the package's decoded portrait when the package has one,
  // else nothing (the caller then draws the look's portrait). Never a mixture of the two.
  [[nodiscard]] const PixelSurface* characterPortrait(CharacterState state) const;
  // The declared mouth sprite for a shape, keyed and ready to draw, or nothing.
  [[nodiscard]] const PixelSurface* characterMouth(character::MouthShape shape) const;
  // The package's listening-state portrait as vector artwork, for the VOICE hero's own ring, which
  // draws through the vector front and not the raster one. Empty when no package is loaded, so the
  // hero keeps the look's portrait. Decoded once and held.
  [[nodiscard]] std::shared_ptr<const paint::Image> lookListeningPortrait() const;
  // Both drawing fronts of the current frame, for the character painters that need the raster one for
  // the package's PPM art. The shell's own canvas is the vector front.
  [[nodiscard]] CharacterCanvas characterCanvas(paint::Canvas2D& vector) const;
  [[nodiscard]] bool reduceMotion() const noexcept { return preferences_.reduceMotion; }
  // Whether any technical lane is expanded. The Stage and the roll's height both read this one
  // answer, so they cannot disagree.
  [[nodiscard]] static bool laneExpanded(const EditorSceneState& state) noexcept;
  // Records the pointer in shell space. A move that changes only which side of the Stage the pointer
  // is on repaints for the Stage's own fade and nothing else.
  void notePointer(ui::Point point);
  // Requests the next frame only while the character is still moving or the Stage is still fading.
  void scheduleAnimationRepaint();
  void ensureBackground(const RasterCanvas& canvas, const DesignTokens& tokens);
  void paintBackground(paint::Canvas2D& c, const DesignTokens& t) const;
  void paintHeader(paint::Canvas2D& c, const DesignTokens& t, const EditorSceneState& state,
                   time::Tick playhead) const;
  void paintWorkspaceMenu(paint::Canvas2D& c, const DesignTokens& t) const;
  void paintEditor(paint::Canvas2D& c, const DesignTokens& t, ui::PianoRollModel& model,
                   const EditorSceneState& state) const;
  void paintLane(paint::Canvas2D& c, const DesignTokens& t, const ui::PianoRollModel& model,
                 const EditorSceneState& state) const;
  void paintRack(paint::Canvas2D& c, const DesignTokens& t, const EditorSceneState& state) const;
  void paintKnobs(paint::Canvas2D& c, const DesignTokens& t, const EditorSceneState& state) const;
  void paintInspector(paint::Canvas2D& c, const DesignTokens& t, const EditorSceneState& state) const;
  // Opens or closes the compact inspector and re-solves the layout at once, so hit-testing and
  // semantics follow before the next paint. Closing returns shell focus that was inside the
  // inspector to its button.
  void setInspectorOpen(NativeEditorController& controller, bool open);
  void setWorkspaceMenuOpen(NativeEditorController& controller, bool open);
  // Header tab and menu row order: SING, VOICE, TUNE, MIX, EXPORT.
  [[nodiscard]] bool tabSelected(std::size_t tab) const noexcept;
  void openTab(NativeEditorController& controller, std::size_t tab);
  [[nodiscard]] bool knobsShown() const noexcept {
    return layout_.rack == RackPresentation::Full || layout_.inspectorOpen;
  }
  void paintStatus(paint::Canvas2D& c, const DesignTokens& t, const EditorSceneState& state) const;
  // A re-homed overlay: the scrim, card and controls, over everything else in the body. `controls`
  // is the same list its semantics publish, so paint and hit-test share one layout.
  void paintOverlay(paint::Canvas2D& c, const DesignTokens& t,
                    const NativeEditorController& controller, const EditorSceneState& state,
                    const ShellOverlay& overlay) const;
  [[nodiscard]] std::vector<SemanticNode> overlaySemantics(
      const NativeEditorController& controller, const EditorSceneState& state) const;
  [[nodiscard]] const ShellOverlay* activeOverlay(const NativeEditorController& controller) const;
  // The diagnostics toast the shell stacks above the status bar, and its popover opener. Both are
  // derived from the status bar's own rectangle, so they move with it.
  [[nodiscard]] ui::Rect diagnosticsToastBounds() const noexcept;
  [[nodiscard]] ui::Rect diagnosticsOpenButton() const noexcept;
  // The +N overlap badges in the grid, exactly as painted, paired with the group they open. The
  // shell makes each one a hit target, so the overlap popover opens from the badge it anchors to.
  [[nodiscard]] std::vector<std::pair<std::size_t, ui::Rect>> overlapBadges(
      const NativeEditorController& controller) const;
  // True when `id` is one of the presented overlay's own controls (its card id or a control it lays
  // out). Such an id belongs to the overlay even when it is also a controller id, so its action is
  // routed to the overlay instead of the controller's own dispatch.
  [[nodiscard]] bool overlayPublishes(const NativeEditorController& controller,
                                      std::string_view id) const;
  // Closes the presented overlay through its own command. The DIAGNOSTICS popover is the shell's
  // own presentation, so this also drops the flag that shows it.
  core::Result<void> closeOverlay(NativeEditorController& controller, const ShellOverlay& overlay);
  void paintExport(paint::Canvas2D& c, const DesignTokens& t, const EditorSceneState& state) const;
  [[nodiscard]] ui::Rect exportArea() const noexcept;
  [[nodiscard]] core::Result<void> runExportSet(NativeEditorController& controller);
  [[nodiscard]] bool inMusicalArea(ui::Point point) const noexcept;
  [[nodiscard]] bool inEditableLane(ui::Point point) const noexcept;
  [[nodiscard]] PointerEvent translated(const PointerEvent& event, ForwardArea area) const noexcept;
  [[nodiscard]] NativeEditorController::HostedGeometry hostedGeometry() const noexcept;
  void applyGeometry(NativeEditorController& controller);
  void frameNotesIfNeeded(NativeEditorController& controller, double previousGridHeight);
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
  // The re-homed overlays, one instance each: an overlay holds no state between frames, so the
  // shell can rebuild its presentation from the controller alone.
  std::unique_ptr<ShellOverlay> microscopeOverlay_{makeSampleMicroscopeOverlay()};
  std::unique_ptr<ShellOverlay> phonemeOverlay_{makePhonemeReviewOverlay()};
  std::unique_ptr<ShellOverlay> timeMapOverlay_{makeTimeMapOverlay()};
  std::unique_ptr<ShellOverlay> supportOverlay_{makeRecoverySupportOverlay()};
  std::unique_ptr<ShellOverlay> overlapOverlay_{makeOverlapDetailOverlay()};
  std::unique_ptr<ShellOverlay> diagnosticsOverlay_{makeDiagnosticsOverlay()};
  bool diagnosticsOpen_{false};
  bool inspectorWanted_{false};
  bool workspaceMenuOpen_{false};
  // The character artwork and its animation, both driven by the read models above.
  CharacterSurface character_;
  // The raster front of the frame being painted. Set for the duration of paint() and cleared after,
  // because the character package's PPM artwork reaches the frame through this canvas and the vector
  // front cannot address it. Null outside paint, which is a programmer error to draw through. The
  // pointer is mutable because the painters are const member functions of a const shell while the
  // pixels they write are the frame's, not the shell's.
  mutable RasterCanvas* raster_{nullptr};
  mutable CharacterAnimator animator_;
  mutable CharacterAnimator::Motion motion_{};
  std::optional<float> auditionLevel_{};
  CharacterState characterState_{CharacterState::Idle};
  // The frame's own clock reading, taken once so every part of one frame animates against the same
  // instant. The injectable UI clock is the source, so a test's frozen clock freezes all of it.
  std::chrono::steady_clock::time_point frameNow_{};
  mutable StageFade stageFade_;
  mutable std::optional<StagePlacement> stagePlacement_{};
  // The last shell-space pointer position the shell saw, so the Stage knows whether the pointer is
  // inside its bounds. Nothing until a pointer event arrives, which is the truth: a shell that has
  // never seen the pointer cannot claim it rests on the figure.
  std::optional<ui::Point> pointerPosition_;
  // The pointer position the Stage's last painted frame was evaluated against, so a change repaints
  // the Stage and only the Stage.
  mutable std::optional<ui::Point> stagePointerAt_;
  // The package's listening-state portrait as vector artwork, decoded once on first use and keyed by
  // the path it came from, so a package reload does not keep showing the previous pose.
  mutable std::shared_ptr<const paint::Image> listeningPortrait_;
  mutable std::filesystem::path listeningPortraitPath_;
  std::uint64_t controllerSerial_{0U};
  std::optional<domain::RegionId> framedRegion_;
  std::optional<std::int32_t> framedTopMidi_;
  std::size_t lastFramingNoteCount_{0U};
  mutable bool stageShown_{false};
  double scrollAccumulator_{0.0};
  AccessibilityTree semantics_;
  std::string semanticFocus_;
  std::string semanticFocusBaseline_;
  std::function<void()> repaint_;
  // The character animation's clock. Empty means the steady clock, so a host that injects nothing
  // still animates and a test that freezes the clock freezes the character with it.
  std::function<std::chrono::steady_clock::time_point()> uiClock_;
  ShellHostActions hostActions_;
  Workspace workspace_{Workspace::Sing};
  std::unique_ptr<ShellWorkspace> tune_{makeTuneWorkspace()};
  std::unique_ptr<ShellWorkspace> mix_{makeMixWorkspace()};
  std::unique_ptr<VoiceWorkspace> voice_{makeVoiceWorkspace()};
  // A pointer gesture that started inside the TUNE or MIX body.
  bool bodyGesture_{false};
  // Export progress from the last painted state, so the run button can refuse while one runs.
  bool exportRunning_{false};
  // What this frame drew inside the notes (or why it drew nothing); accessibility reports it.
  RegionWaveform waveform_;

  PixelSurface background_;
  double backgroundScale_{0.0};
  DesignMode backgroundMode_{DesignMode::Emo};
  bool backgroundValid_{false};
};

}  // namespace seam::native_ui::design
