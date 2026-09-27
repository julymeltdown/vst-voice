#pragma once

#include "seam/native_ui/accessibility_tree.hpp"
#include "seam/native_ui/design/shell_strings.hpp"
#include "seam/native_ui/design/character_surface.hpp"
#include "seam/native_ui/design/design_tokens.hpp"
#include "seam/native_ui/design/shell_workspace.hpp"
#include "seam/native_ui/design/sing_layout.hpp"
#include "seam/native_ui/design/voice_workspace.hpp"
#include "seam/native_ui/design/shell_overlays.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/frame_damage.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/paint/display_list.hpp"
#include "seam/native_ui/paint/layer_cache.hpp"
#include "seam/native_ui/region_envelope.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace seam::native_ui::design {

// Application preferences for the redesigned shell. They are never project data.
struct DesignPreferences final {
  DesignMode mode{DesignMode::Emo};
  // The contrast the shell paints with. When contrastFollowsSystem is set it is the system's own
  // Increase Contrast setting, re-read every frame, so turning that setting on or off in System
  // Settings changes the open editor. An explicit choice made in the app clears the flag and wins
  // over the system, for this app alone, until the user returns it to the system.
  Contrast contrast{Contrast::Standard};
  bool contrastFollowsSystem{false};
  // Reduce Motion drops blink, breathing, the render spinner and the Stage fade, and makes a state
  // change immediate. The state itself is unchanged: a screen that reduces motion still says what
  // the singer is doing. It is the shell's own preference, alongside the look and the contrast, and
  // it is the same setting the host already publishes to the editor through
  // platform::AccessibilityPreferences, so both surfaces agree.
  bool reduceMotion{false};
  // The language the shell reads in: a code from shellLanguages() ("en", "ko"). When
  // languageFollowsSystem is set it is the platform's preferred language, when the shell offers it,
  // and English otherwise; an explicit choice made in the app clears the flag and wins, for this app
  // alone, until the user returns it to the system. The default is English, so a test fixture that
  // builds its own preferences reads English whatever the machine's language is.
  std::string language{"en"};
  bool languageFollowsSystem{false};
};

[[nodiscard]] DesignPreferences loadDesignPreferences();
void saveDesignPreferences(const DesignPreferences& preferences);
// The platform's Increase Contrast accessibility setting (false where the platform has none).
[[nodiscard]] bool systemIncreaseContrast();
// The platform's first preferred language tag ("ko-KR"), or empty where the platform has none.
[[nodiscard]] std::string systemPreferredLanguage();
// Runs onChange whenever the system's accessibility display options change (Increase Contrast,
// Reduce Motion, Reduce Transparency), on the thread that posts the change. The observation ends
// when the returned token is released. Empty where the platform has no such notification.
[[nodiscard]] std::shared_ptr<void> observeSystemDisplayOptions(std::function<void()> onChange);
// Posts the platform's own display-options notification, as System Settings does (tests, and a
// host that changed an option itself). Does nothing where the platform has none.
void postSystemDisplayOptionsChanged();

struct ModeAssets final {
  std::shared_ptr<const paint::Image> portrait;
  std::shared_ptr<const paint::Image> stage;
  std::shared_ptr<const paint::Image> wordmark;
  // The 1600x1000 key art: the empty project's splash and the About sheet's art.
  std::shared_ptr<const paint::Image> splash;
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
  std::string exportUnavailable{tr(Str::ThisHostDoesNotExportFrom)};
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

// Finds the translation files: an explicit override (SEAM_L10N_ASSETS), l10n beside the design
// assets, or assets/l10n in the source tree. Empty when there are none, and the shell reads English.
[[nodiscard]] std::filesystem::path locateShellTranslations(
    const std::filesystem::path& designAssets = {});

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

// The SING workspace shell for the EMO and SCENE designs, and the one editor surface on platforms
// with the vector backend. It paints around the existing editing engine: pointer events inside the
// musical grid are translated into the controller's own coordinates, so note creation, selection,
// lyric entry and vibrato editing keep their behavior and undo history. Every modal surface the
// controller opens is presented here as a panel, popover or sheet by shell_overlays.hpp (section 7.6
// of the redesign plan): the sample microscope, phoneme review, time map, recovery/support, overlap
// detail, diagnostics, the replacement review, the audio settings, the voice browser and the text
// fields (tempo/meter, phone hint, find/replace, review draft fields, renames).
class SingShell final {
public:
  // A shell starts inactive: it paints nothing, and it reads neither the saved preferences nor the
  // design assets. Every host activates it; the no-argument form reads the saved preferences, and
  // tests pass an explicit preference set so they never observe the user's saved design mode.
  SingShell() = default;
  // A shell's own translation table is installed for lookups while it is active; dropping the
  // shell uninstalls it.
  ~SingShell();
  // The system display-options observer calls back into this shell, so it never moves.
  SingShell(SingShell&&) = delete;
  SingShell& operator=(SingShell&&) = delete;
  void activate(const std::filesystem::path& assetRoot = locateDesignAssets());
  // Test and screenshot entry point: an explicit preference set, no persistence.
  void activate(const std::filesystem::path& assetRoot, DesignPreferences preferences);

  [[nodiscard]] bool available() const noexcept { return paint::vectorBackendAvailable(); }
  [[nodiscard]] bool active() const noexcept { return active_; }
  // True when an activated shell can present: it is the editor surface whenever the platform has
  // the vector backend. There is no other editor surface to switch to.
  [[nodiscard]] bool enabled() const noexcept { return active_ && available(); }
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
  // The SINGER card's overflow menu (its ⋯ button, shell.singer-menu) is the shell's own popover
  // too. Opening it is refused while another surface is up or the card's button is not on screen;
  // it closes on Escape, a press outside, a command that ran, a workspace switch, a resize and a
  // replaced controller, and focus returns to the button.
  [[nodiscard]] bool singerMenuOpen() const noexcept { return singerMenuOpen_; }
  core::Result<void> setSingerMenuOpen(NativeEditorController& controller, bool open);
  // The About sheet (the application menu's About command): the mode's key art, name and version.
  [[nodiscard]] bool aboutOpen() const noexcept { return aboutOpen_; }
  core::Result<void> setAboutOpen(NativeEditorController& controller, bool open);
  // Whether the last SING frame painted the Stage figure; §3.4 keeps it off without the full rack.
  [[nodiscard]] bool lastFrameShowedStage() const noexcept { return stageShown_; }
  // Where the last SING frame drew the Stage figure, or nothing when it drew none. The figure is
  // decorative, so this exists for evidence and tests rather than for input: nothing routes to it.
  [[nodiscard]] std::optional<ui::Rect> lastFrameStageBounds() const noexcept {
    return stagePlacement_.has_value() && stagePlacement_->shown
               ? std::optional<ui::Rect>{stagePlacement_->bounds}
               : std::nullopt;
  }
  // Where the last frame drew the character's error toast, or nothing when it drew none.
  [[nodiscard]] std::optional<ui::Rect> lastFrameErrorToast() const noexcept {
    return errorToast_.has_value() ? std::optional<ui::Rect>{errorToast_->bounds} : std::nullopt;
  }
  [[nodiscard]] std::optional<std::size_t> lastOffscreenHint() const noexcept { return offscreenHint_; }
  // The overlays this shell re-homes: each is painted inside the shell as a sheet or inline field.
  [[nodiscard]] static bool rehomedSurface(OverlayKind kind) noexcept;

  void setMode(DesignMode mode, bool persist = true);
  [[nodiscard]] Contrast contrast() const noexcept { return preferences_.contrast; }
  [[nodiscard]] bool contrastFollowsSystem() const noexcept {
    return preferences_.contrastFollowsSystem;
  }
  // The in-app override: an explicit Standard or High that wins over the system setting.
  void setContrast(Contrast contrast, bool persist = true);
  // Drops the override, so the shell follows the system's Increase Contrast again.
  void followSystemContrast(bool persist = true);
  // Turns motion down. Like the look and the contrast it is an application preference, so the shell
  // keeps painting the same state with the animation dropped.
  void setReduceMotion(bool reduceMotion, bool persist = true);
  // The language the shell reads in (a code from shellLanguages()), and the in-app override: an
  // explicit language that wins over the system's, or following the system again. An offered
  // language whose translation file is missing or unreadable reads as English.
  [[nodiscard]] const std::string& language() const noexcept { return preferences_.language; }
  [[nodiscard]] bool languageFollowsSystem() const noexcept {
    return preferences_.languageFollowsSystem;
  }
  void setLanguage(std::string_view language, bool persist = true);
  void followSystemLanguage(bool persist = true);
  // The header's language control steps System, then each offered language, then System again.
  void cycleLanguage(int direction, bool persist = true);
  // What loading the current language's translation file reported (empty for English).
  [[nodiscard]] const ShellStringLoadReport& languageReport() const noexcept {
    return languageReport_;
  }
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
  // The Phonemes lane tab: the lane band hosts the phoneme, unit and seam lanes, top to bottom, in
  // place of an expression curve. Their gestures are the controller's own (boundary drag, unit
  // click with S/R and double-click for the microscope, seam click and the seam keys), forwarded
  // with the band geometry the shell paints. A collapsed band keeps a label strip and takes no
  // gesture; its collapsed state is the project's own lane presentation.
  struct TechnicalBands final {
    std::array<ui::Rect, 3U> band{};
    std::array<ui::Rect, 3U> toggle{};
    std::array<bool, 3U> collapsed{};
  };
  [[nodiscard]] bool technicalLanesShown() const noexcept { return technicalLane_; }
  [[nodiscard]] TechnicalBands technicalBands() const noexcept;
  core::Result<void> showTechnicalLanes(NativeEditorController& controller);
  core::Result<void> toggleTechnicalBand(NativeEditorController& controller, std::size_t band);
  // The VOICE, TUNE or MIX body while it is shown, else null.
  [[nodiscard]] ShellWorkspace* bodyWorkspace() const noexcept;
  // Undo and redo belong to the Voice Designer while VOICE is shown: the editor's history is not
  // on screen there. Returns the designer's result then, and nothing in any other workspace.
  // While a workspace body drag is in progress it returns a refusal in every workspace, so neither
  // history moves under the gesture and the application menu does not fall through to the song.
  [[nodiscard]] std::optional<core::Result<void>> routeUndo(bool redo);
  // The rectangle a covering workspace (TUNE, MIX, EXPORT) owns, in shell coordinates.
  [[nodiscard]] ui::Rect workspaceArea() const noexcept { return exportArea(); }
  // Switches the visible workspace. Gestures and a note-grid lyric field are abandoned first: the
  // grid they belong to is no longer on screen.
  void setWorkspace(NativeEditorController& controller, Workspace workspace);
  // The Export workspace's run button, in shell coordinates (empty unless that workspace shows).
  [[nodiscard]] ui::Rect exportRunButton() const noexcept;
  // The final bounce's timing choice beside the run button (empty unless EXPORT shows). It is
  // painted, hit and published only for a host that offers the choice (a plug-in).
  [[nodiscard]] ui::Rect exportBounceButton() const noexcept;
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

  // The frame pipeline of redesign plan section 10. A frame is recorded into four layers
  // (background, grid, content, dynamic); the first three are cached and rasterized again only when
  // what they draw changed, and the dynamic one (playhead, meters, the singer's ring and avatar,
  // hover, focus, gestures, menus and overlays) is drawn over them on every frame. The canvas always
  // receives the complete frame.
  //
  // What the last painted frame changed, in logical points, so a presenter can invalidate only
  // that. Everything, for a frame composed from nothing or one where a cached layer changed.
  [[nodiscard]] const FrameDamage& lastFrameDamage() const noexcept { return lastDamage_; }
  // Which layers the last frame rasterized, background to dynamic.
  [[nodiscard]] const std::array<bool, paint::kLayerCount>& lastFrameLayers() const noexcept {
    return lastLayers_;
  }
  [[nodiscard]] std::size_t layerCacheBytes() const noexcept {
    return layers_.bytes() + ringGlows_.bytes() + paint::glowSpriteCacheBytes();
  }
  // Forgets every cached layer, so the next frame is composed from nothing.
  void invalidateLayers() noexcept;
  // A host whose presenter keeps the painted surface between frames (the AppKit window and the
  // CLAP view do) says so here. A frame that changes only dynamic items then restores and redraws
  // just the damaged rectangles of that surface instead of writing all of it. The shell still
  // recognises a different surface and writes it whole.
  void setRetainedSurface(bool retained) noexcept { retainedSurface_ = retained; }
  // Abandons shell and controller gestures without committing them (capture loss, hide).
  void cancelGestures(NativeEditorController& controller);

  core::Result<void> pointerDown(NativeEditorController& controller, const PointerEvent& event);
  core::Result<void> pointerMove(NativeEditorController& controller, const PointerEvent& event);
  core::Result<void> pointerUp(NativeEditorController& controller, const PointerEvent& event);
  // Returns true when the shell consumed the scroll.
  bool scroll(NativeEditorController& controller, double deltaX, double deltaY, ui::Point anchor,
              InputModifiers modifiers);
  // Returns true when the shell consumed the key: Escape cancels a knob drag or a forwarded pointer
  // gesture without committing it, and an open overlay owns the keyboard.
  bool handleShellKey(NativeEditorController& controller, const KeyEvent& event);
  // Lyric requests come from note bounds in the shell's viewport and are moved into shell space.
  // Every other request (tempo/meter, hint, find/replace and draft fields, renames) is placed on
  // the field the shell draws for it, so the input client, the painted field, its hit rectangle
  // and its accessible bounds are one rectangle.
  [[nodiscard]] TextInputRequest translateTextInput(TextInputRequest request);
  void textInputEnded() noexcept {
    lyricInputActive_ = false;
    fieldAnchor_.reset();
  }

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
  // The overlay presented for a state the caller already derived: the frame's own state while
  // painting and building semantics, so a frame never derives the controller's state twice.
  [[nodiscard]] const ShellOverlay* activeOverlay(const NativeEditorController& controller,
                                                  const EditorSceneState& state) const;
  // Character artwork that reaches the frame through the raster front (the package's PPM portraits
  // and mouth sprites). While a frame is recorded it is deferred into the recording with a hash of
  // everything it draws and the bounds it may touch; on a canvas that draws directly it runs now.
  void characterArt(paint::Canvas2D& c, ui::Rect bounds, std::uint64_t hash,
                    std::function<void(CharacterCanvas)> draw) const;
  // Text widths for the recording canvas, from the vector backend, remembered across frames.
  [[nodiscard]] double measureText(std::string_view utf8, const paint::TextStyle& style) const;
  // Everything the background layer depends on.
  [[nodiscard]] std::uint64_t backgroundKey(const DesignTokens& tokens, const PixelSurface& surface,
                                            double scale) const noexcept;
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
  void paintBackground(paint::Canvas2D& c, const DesignTokens& t) const;
  void paintHeader(paint::Canvas2D& c, const DesignTokens& t, const EditorSceneState& state,
                   time::Tick playhead) const;
  void paintWorkspaceMenu(paint::Canvas2D& c, const DesignTokens& t) const;
  void paintEditor(paint::Canvas2D& c, const DesignTokens& t, ui::PianoRollModel& model,
                   const EditorSceneState& state) const;
  void paintLane(paint::Canvas2D& c, const DesignTokens& t, const ui::PianoRollModel& model,
                 const EditorSceneState& state) const;
  void paintTechnicalLanes(paint::Canvas2D& c, const DesignTokens& t,
                           const ui::PianoRollModel& model, const EditorSceneState& state) const;
  // Reads the project's lane presentation, which decides which technical bands are collapsed.
  void syncTechnicalBands(const NativeEditorController& controller);
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
  // A note-grid lyric field belongs to the score. When a surface is presented over the score (the
  // voice browser, audio settings, diagnostics, any re-homed overlay), the lyric is cancelled, never
  // committed, as opening a classic surface cancelled it; its input client leaves with it.
  void cancelCoveredLyric(NativeEditorController& controller);
  // Runs one of the presented overlay's controls. A singer menu item whose command ran closes the
  // menu and returns focus to its button (a surface the command opened takes it from there).
  core::Result<void> performOverlay(NativeEditorController& controller, const ShellOverlay& overlay,
                                    std::string_view id, SemanticAction action);
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
  // Closes whichever overlay is presented (workspace switch): its own close command, and an open
  // field is cancelled.
  void dismissOverlay(NativeEditorController& controller);
  core::Result<void> nudge(NativeEditorController& controller, std::size_t index, int steps);
  core::Result<void> shellPointerDown(NativeEditorController& controller, const PointerEvent& event);
  core::Result<void> performSemantic(NativeEditorController& controller, std::string_view id,
                                     SemanticAction action);
  // Shell focus remembers the controller's focus at the moment it was taken; once the controller's
  // focus moves, the shell's is dropped.
  void takeSemanticFocus(NativeEditorController& controller, std::string id);
  // After the About sheet closes (Close, the host, or setAboutOpen(false)), focus returns to the
  // control that had it when the sheet opened, exactly as Escape returns it.
  void returnFocusToOverlayOpener(NativeEditorController& controller);
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
  bool technicalLane_{false};
  std::array<bool, 3U> technicalCollapsed_{};
  bool lyricInputActive_{false};
  // The anchor of an open non-lyric field the shell moved into shell space, and the rectangle it
  // was placed at; a layout that moves that rectangle cancels the composition (as for lyrics).
  std::optional<TextInputAnchor> fieldAnchor_;
  ui::Rect fieldBounds_{};
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
  std::unique_ptr<ShellOverlay> reviewOverlay_{makeReplacementReviewOverlay()};
  std::unique_ptr<ShellOverlay> audioOverlay_{makeAudioSettingsOverlay()};
  std::unique_ptr<ShellOverlay> voicebankOverlay_{makeVoicebankBrowserOverlay()};
  std::unique_ptr<ShellOverlay> fieldOverlay_{makeTextFieldOverlay()};
  std::unique_ptr<ShellOverlay> singerMenuOverlay_{makeSingerMenuOverlay()};
  std::unique_ptr<ShellOverlay> aboutOverlay_{
      makeAboutOverlay([this] { return assets().splash.get(); })};
  // A plot gesture an overlay started (the dynamics inspector's points).
  std::optional<OverlayGesture> overlayGesture_;
  // The shell control that had focus when the presented overlay opened (MIX's Settings, VOICE's
  // browser button); Escape returns focus there when it is still published.
  std::string overlayOpener_;
  // The field the presented overlay last published (its event or text field), so a field that
  // appears inside an open card takes the keyboard once.
  std::string overlayField_;
  bool diagnosticsOpen_{false};
  bool singerMenuOpen_{false};
  bool aboutOpen_{false};
  // The overlay the last semantics rebuild presented, so the first frame of a newly opened overlay
  // gives its first control the keyboard.
  OverlayKind presentedOverlay_{OverlayKind::None};
  // The surface an open inline field was opened over (a review, for its draft field), so the
  // surface it returns to is resumed where it was rather than presented anew.
  OverlayKind fieldOpenedOver_{OverlayKind::None};
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
  // Whether a surface painted this frame showed that motion (the header avatar, the full rack's
  // ring). Only then does the frame ask for the next one; the Stage's fade asks on its own.
  mutable bool motionShown_{false};
  // The error toast the last frame's status bar painted, if any.
  mutable std::optional<CharacterToast> errorToast_{};
  std::optional<float> auditionLevel_{};
  CharacterState characterState_{CharacterState::Idle};
  // The project's character display mode for this frame (Full, Minimal, Off). Minimal keeps the
  // compact identity and drops the Stage; Off draws no character artwork anywhere, while the ring,
  // the avatar's state ring, the toast and the empty-project line still carry the singer's status.
  domain::CharacterDisplayMode characterDisplay_{domain::CharacterDisplayMode::Full};
  [[nodiscard]] const paint::Image* lookPortrait() const;
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
  // Keeps the system display-options observer alive while the shell is active: an Increase
  // Contrast change repaints an idle editor, whose next frame reads the new setting.
  std::shared_ptr<void> displayOptionsObservation_;
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
  // The translation directory found at activation, the table of the current language (none for
  // English) and the language it was loaded for.
  std::filesystem::path translations_;
  std::unique_ptr<ShellStringTable> strings_;
  std::string stringsLanguage_{"en"};
  ShellStringLoadReport languageReport_;
  void applyLanguage();

  // The frame pipeline: the cached layers, what the last frame changed and rasterized, and the
  // generation of the artwork every recorded image and portrait belongs to (a reload changes it, so
  // no cached layer outlives the artwork it drew).
  paint::LayerCache layers_;
  FrameDamage lastDamage_{FrameDamage::everything()};
  std::array<bool, paint::kLayerCount> lastLayers_{};
  mutable std::uint64_t artGeneration_{0U};
  // The singer ring's glows, drawn once and composited on every frame after.
  mutable RingGlowCache ringGlows_;
  bool retainedSurface_{false};
  PixelSurface metricsSurface_;
  std::unique_ptr<paint::Canvas2D> metrics_;
  mutable std::unordered_map<std::string, double> measureCache_;
};

}  // namespace seam::native_ui::design
