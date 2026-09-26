#pragma once

#include "seam/character/character.hpp"
#include "seam/native_ui/design/design_tokens.hpp"
#include "seam/native_ui/design/sing_layout.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/native_ui/render_status_panel.hpp"
#include "seam/native_ui/voice_identity.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace seam::native_ui::design {

// The protagonist's operating states (docs/design/SEAM_UI_REDESIGN_CODE_PLAN_2026-09-25.md section
// 8.3). These are the shell's own vocabulary, distinct from the character package's six asset states
// and from the editor's render and identity states: every one of them maps from a real read model the
// shell already holds, and nothing here is inferred from a timer, a percentage or a guess.
enum class CharacterState : std::uint8_t {
  Idle,
  Listening,
  Singing,
  Rendering,
  Complete,
  Warning,
  Error,
};

[[nodiscard]] std::string_view characterStateName(CharacterState state) noexcept;

// The package state whose artwork a shell state draws. Listening and Idle share the neutral asset
// because the package has no listening asset of its own: the listening pose is a shell-side choice
// between two state portraits it does have, never a fabricated package state.
[[nodiscard]] character::State characterPackageState(CharacterState state) noexcept;

// Every measured input the character reacts to, each one already computed by the shell. There is no
// timer, no animation phase and no render percentage here: those are presentation.
struct CharacterSurfaceInput final {
  VoiceIdentityState voiceIdentity{VoiceIdentityState::Missing};
  RenderStatusState render{RenderStatusState::Idle};
  // The publication the render status describes is audible but no longer matches the project. The
  // dock keeps drawing what is heard and says it has gone stale.
  bool audibleStale{false};
  bool playing{false};
  // The published phrase bound to the selected singer. performing is false when no phrase is bound,
  // which is not the same claim as a phrase of silence.
  bool performing{false};
  float energy{0.0F};
  character::MouthShape mouth{character::MouthShape::Closed};
  // The measured peak of the audition the output device last played, when one plays at all. The
  // measured level is both the "an audition plays" signal and the listening ring's lit count.
  std::optional<float> auditionLevel{};
  // The reason the render could not use its exact voicebank, when the project reports one.
  bool bankMissing{false};
  // A genuine transition to Ready for the current publication, still inside its dwell. Set from the
  // identity resolver's own dwell, never from a bare Ready or from a fraction of 1.0.
  bool completeDwell{false};
};

// The inputs above, read from the scene state the shell already built.
[[nodiscard]] CharacterSurfaceInput characterSurfaceInput(
    const EditorSceneState& state, std::optional<float> auditionLevel) noexcept;

// Section 8.3's table, in one place. Order of precedence is deliberate and is the order in which the
// claims are specific: a failed render or a missing voicebank outranks everything; stale audio
// outranks singing because the phrasing on screen no longer describes the project; an audible phrase
// at the playhead outranks a render in flight because it is what the creator is hearing; and a
// transition dwell outranks the idle rest.
[[nodiscard]] CharacterState resolveCharacterState(const CharacterSurfaceInput& input) noexcept;

// Whether this state carries motion of its own, so a caller knows whether to keep ticking at all. A
// held pose (listening, complete, warning, error) animates nothing and schedules no frame.
[[nodiscard]] bool characterStateAnimates(CharacterState state) noexcept;

// ---- artwork ------------------------------------------------------------------------------------

// The package declares one asset per state, and Manifest::assetFor already falls back to the
// package's own default state for a state it does not name. An empty path means there is no package
// art to draw, and the caller draws the look's own portrait instead of claiming a package state.
[[nodiscard]] std::filesystem::path characterStateAssetPath(const character::Package& package,
                                                            CharacterState state);

// Empty for a status-only package, which is the whole point of the schema: a presentation that asks a
// status-only package for a mouth gets nothing rather than a guess.
[[nodiscard]] std::filesystem::path characterMouthAssetPath(const character::Package& package,
                                                            character::MouthShape shape);

// Which side the artwork comes from. Exactly one is used, never a mixture of a package state and the
// look's portrait, so a screen never shows half a turnaround beside half a fallback.
struct CharacterArtworkChoice final {
  bool fromPackage{false};
  std::filesystem::path path;
};

[[nodiscard]] CharacterArtworkChoice characterArtworkChoice(const character::Package* package,
                                                            CharacterState state);
[[nodiscard]] CharacterArtworkChoice characterMouthChoice(const character::Package* package,
                                                          character::MouthShape shape);

// The corner color a placed mouth sprite keys out, read from the sprite's own bytes. Nothing (and no
// modification) when the four corners disagree: a sprite that is not a flat-background overlay is
// refused rather than keyed against an arbitrary color. Keyed pixels become fully transparent
// premultiplied black, which is the layout this canvas and CoreGraphics both expect.
[[nodiscard]] std::optional<Color> keyCornerColor(PixelSurface& surface) noexcept;

// ---- the Stage figure (section 8.4) -------------------------------------------------------------

// Height of the Stage figure as a share of the roll, and its two opacities, from section 8.4.
inline constexpr double kStageHeightFraction = 0.78;
inline constexpr double kStageOpacityRest = 0.16;
inline constexpr double kStageOpacityCrowded = 0.06;
inline constexpr double kStageFadeSeconds = 0.180;
// The distance the figure is held off the roll's right and bottom edges.
inline constexpr double kStageAnchorInset = 24.0;

struct StageInput final {
  // The full rack is where the Stage belongs; the compact presentations keep it off (section 3.4).
  bool fullRack{false};
  bool highContrast{false};
  // An expanded technical lane gives its height back to the roll and takes the Stage with it.
  bool laneExpanded{false};
  ui::Rect grid;
  bool pointerInside{false};
  bool noteIntersects{false};
};

struct StagePlacement final {
  bool shown{false};
  ui::Rect bounds;
  double targetOpacity{0.0};
};

// Whether the Stage shows at all, at the rectangle it occupies: bottom-right anchored, 78% of the
// roll's height, its aspect taken from the artwork. Off in High Contrast, off with an expanded lane,
// off outside the full rack, and off when the roll is too small to hold a figure behind the notes.
[[nodiscard]] StagePlacement resolveStage(const StageInput& input, double stageAspect) noexcept;

// The 180 ms fade between the two opacities. Reduce Motion makes the change immediate; the state and
// the opacity it settles on are unchanged.
class StageFade final {
public:
  // Returns the opacity to draw this frame for the placement's target.
  double advance(const StagePlacement& placement, std::chrono::steady_clock::time_point now,
                 bool reduceMotion) noexcept;
  [[nodiscard]] double opacity() const noexcept { return opacity_; }
  [[nodiscard]] bool fading() const noexcept { return fading_; }
  void reset() noexcept;

private:
  double opacity_{kStageOpacityRest};
  double from_{kStageOpacityRest};
  double target_{kStageOpacityRest};
  bool fading_{false};
  std::chrono::steady_clock::time_point startedAt_{};
};

// ---- the empty project and the error toast ------------------------------------------------------

// The seated pose's line, drawn only for a region that genuinely has no notes: a scrolled-away note
// is a different situation and the grid says so instead.
inline constexpr std::string_view kEmptyProjectPrompt =
    "Double-click the grid to write the first note.";

[[nodiscard]] std::optional<std::string_view> emptyProjectPrompt(std::size_t noteCount) noexcept;

// The error toast above the status bar: a 40-point head-in-hand crop and the reason the status line
// already carries. It appears for a failed render and for a missing voicebank, and for nothing else.
// The recovery action stays reachable in the SINGER card, which this rectangle never covers.
struct CharacterToast final {
  ui::Rect bounds;
  ui::Rect pose;
  std::string title;
  std::string reason;
};

[[nodiscard]] std::optional<CharacterToast> characterErrorToast(
    const SingLayout& layout, const CharacterSurfaceInput& input, std::string_view diagnostic);

// ---- the animator (section 8.3) -----------------------------------------------------------------

// Idle motion: a blink on a seeded interval in [4, 7] seconds, a 0.25 Hz breathing drift of at most
// 2 points, and the render spinner's phase. It is driven by the existing injectable UI clock, never
// by a timer of its own, and it reports a repaint only when a painted quantity actually moved, so an
// idle window that cannot animate requests no frames at all.
class CharacterAnimator final {
public:
  struct Motion final {
    // 0 open, 1 closed. Always 0 under Reduce Motion.
    double blink{0.0};
    // Points of vertical drift, within [-2, 2]. Always 0 under Reduce Motion.
    double breath{0.0};
    // Turns in [0, 1), where the render spinner starts. Always 0 under Reduce Motion.
    double spinner{0.0};
  };

  static constexpr double kBreathAmplitude = 2.0;
  static constexpr double kBreathHertz = 0.25;
  static constexpr double kBlinkSeconds = 0.12;
  static constexpr double kSpinnerSecondsPerTurn = 1.2;

  explicit CharacterAnimator(std::uint64_t seed = 0x5EA3C0FFEEULL) noexcept;

  // Advances to now for state. Reduce Motion forces every quantity to rest and reports no change,
  // whatever the state. Returns true only when something the caller paints moved.
  bool advance(CharacterState state, std::chrono::steady_clock::time_point now, bool reduceMotion);

  [[nodiscard]] const Motion& motion() const noexcept { return motion_; }
  // True while this animator is still producing motion, so a caller keeps scheduling frames.
  [[nodiscard]] bool moving() const noexcept { return moving_; }
  // The interval the current blink was drawn from, in [4, 7] seconds and reproducible from the seed.
  [[nodiscard]] double blinkIntervalSeconds() const noexcept { return intervalSeconds_; }
  [[nodiscard]] double secondsUntilBlink(std::chrono::steady_clock::time_point now) const noexcept;

private:
  [[nodiscard]] double drawInterval() noexcept;

  std::uint64_t random_;
  double intervalSeconds_{5.0};
  std::chrono::steady_clock::time_point blinkAt_{};
  bool scheduled_{false};
  Motion motion_;
  bool moving_{false};
};

// ---- painting -----------------------------------------------------------------------------------

// The singer ring: a circular portrait inside a ring of exactly 64 ticks. Emo draws thin ticks with a
// lit count from the real energy; Scene draws segments cycling the look's colors. The ring tints amber
// for a warning and red for an error, over either look.
inline constexpr std::size_t kSingerRingTicks = 64U;
inline constexpr double kSingerRingThickness{2.0};

// The two drawing fronts of one frame. Both write into the same pixel surface: the vector canvas
// carries the look's PNG artwork with its own alpha, masking and filtering, and the raster canvas
// carries the character package's decoded PPM portraits and keyed mouth sprites, which the vector
// backend cannot address. The documented contract between them is flush(): the vector canvas is
// flushed before the raster one draws, so neither sees a half-finished frame.
struct CharacterCanvas final {
  paint::Canvas2D& vector;
  RasterCanvas& raster;
};

struct SingerRingSpec final {
  // The outer square of the ring; the portrait is inscribed within it.
  ui::Rect bounds;
  CharacterState state{CharacterState::Idle};
  // The lit count as a fraction of the 64 ticks: the singer's measured energy while performing, the
  // render fraction while rendering, the measured audition level in VOICE. Nothing lights at 0.
  double lit{0.0};
  // Where the render spinner has turned to, in turns. Used only while rendering.
  double rotation{0.0};
  // The portrait: a package state asset when one is loaded, else the look's own portrait.
  const PixelSurface* packagePortrait{nullptr};
  const paint::Image* lookPortrait{nullptr};
  double portraitOpacity{1.0};
  // While singing, the mouth the published performance reports, when the package declares one at its
  // normalized placement on the portrait. Nothing for a status-only package or for any state that is
  // not singing: an undeclared shape is never drawn.
  const PixelSurface* mouthSprite{nullptr};
  std::optional<character::MouthPlacement> mouthPlacement{};
  double mouthOpacity{1.0};
  // The idle breathing drift in points: the figure moves inside its fixed ring, so the ring stays put
  // and the portrait is what breathes. Zero under Reduce Motion and in every state that does not
  // breathe.
  double breath{0.0};
  // The idle blink, 0 open and 1 closed. Zero under Reduce Motion, and zero in every state that does
  // not blink; the lid is drawn over the portrait, inside the ring.
  double blink{0.0};
};

void paintSingerRing(CharacterCanvas canvas, const DesignTokens& tokens,
                     const SingerRingSpec& spec);

// A portrait into a circle (circular = true) or a rounded square, preferring the package's decoded
// state asset and drawing the look's portrait when there is no package art. Drawing nothing when
// neither exists, so an absent package leaves an empty ring rather than a fabricated face.
//
// The fit is part of the artwork's contract, because the package's assets and the look's are framed
// differently. Contain shows the whole frame; it is what a pose wants. CoverTop scales until the shape
// is covered and keeps the top edge, which is what a round portrait ring wants when the state asset is
// a full-body figure: containing one in a circle leaves two bars of background beside the figure.
// HeadSquare draws only the top square of the source, which is what a small head crop wants.
//
// The manifest declares no focal point, so neither fit knows where the face is. CoverTop and
// HeadSquare both assume a standing figure whose head is at the top, centred. That assumption belongs
// here, in one place, and is the first thing that should read a declared anchor when packages carry
// one; until then the shell shows the declared artwork and claims no more than the assumption.
enum class PortraitFit : std::uint8_t { Contain, CoverTop, HeadSquare };

// Returns the rectangle the portrait actually filled inside the destination: what the declared mouth
// placement is measured against, and what a caller needs to place anything else on the face.
// The optional drift moves the artwork inside the destination without moving the destination's own
// shape, which is what the breathing drift needs: the figure moves and the ring's circle does not.
[[nodiscard]] ui::Rect paintCharacterPortrait(
    CharacterCanvas canvas, ui::Rect destination, bool circular,
    const PixelSurface* packagePortrait, const paint::Image* lookPortrait, double opacity,
    PortraitFit fit = PortraitFit::Contain, double drift = 0.0);

// A declared mouth sprite at its normalized placement inside the portrait rectangle. The sprite was
// already keyed by keyCornerColor, so its background composites away.
void paintCharacterMouth(CharacterCanvas canvas, ui::Rect portraitBounds,
                         const character::MouthPlacement& placement, const PixelSurface& sprite,
                         double opacity);

// The 28-point header avatar: a circle with a state ring, so the header carries the same state the
// SINGER card spells out.
inline constexpr double kHeaderAvatarSize = 28.0;

void paintCharacterAvatar(CharacterCanvas canvas, const DesignTokens& tokens, ui::Rect bounds,
                          CharacterState state, const PixelSurface* packagePortrait,
                          const paint::Image* lookPortrait, double opacity,
                          double blink = 0.0, double breath = 0.0);

// The Stage figure into the roll: drawn before the notes and curves so it sits below them.
void paintStageFigure(CharacterCanvas canvas, ui::Rect clip, const StagePlacement& placement,
                      double opacity, const paint::Image& stage);

// The seated pose and the prompt, centred in the roll, for a region that genuinely has no notes.
void paintEmptyProject(CharacterCanvas canvas, const DesignTokens& tokens,
                       const SingLayout& layout, const PixelSurface* packagePortrait,
                       const paint::Image* lookPortrait);

// The error toast above the status bar: the 40-point head-in-hand crop, its title and the reason.
void paintCharacterToast(CharacterCanvas canvas, const DesignTokens& tokens,
                         const CharacterToast& toast, const PixelSurface* packagePortrait,
                         const paint::Image* lookPortrait);

// ---- the loaded artwork -------------------------------------------------------------------------

// The character artwork the shell draws: the package's per-state portraits and mouth sprites when a
// validated package is loaded, and the look's own portrait and stage otherwise. A package that fails
// to load is treated as absent and the reason is kept, so nothing is drawn from a package the loader
// refused. Decoding is lazy and per asset, because the fidelity review budgets active character art
// and an inventory that is never shown must not be decoded.
class CharacterSurface final {
public:
  [[nodiscard]] core::Result<void> loadPackage(const std::filesystem::path& packageRoot);
  void clearPackage() noexcept;

  [[nodiscard]] bool packageLoaded() const noexcept { return package_.has_value(); }
  [[nodiscard]] const character::Package* package() const noexcept {
    return package_.has_value() ? &*package_ : nullptr;
  }
  [[nodiscard]] bool declaresPerformance() const noexcept {
    return package_.has_value() && package_->manifest.declaresPerformance();
  }
  // Why a package root was refused, so a host can report it; empty when the last load succeeded.
  [[nodiscard]] const std::string& packageError() const noexcept { return packageError_; }

  // The look's own artwork, used whenever the package has none for what is being drawn.
  void setLookArtwork(std::shared_ptr<const paint::Image> portrait,
                      std::shared_ptr<const paint::Image> stage) {
    lookPortrait_ = std::move(portrait);
    lookStage_ = std::move(stage);
  }
  [[nodiscard]] const std::shared_ptr<const paint::Image>& lookPortrait() const noexcept {
    return lookPortrait_;
  }
  [[nodiscard]] const std::shared_ptr<const paint::Image>& lookStage() const noexcept {
    return lookStage_;
  }

  // The decoded package portrait for a state, or nothing when there is no package art for it. The
  // decode is cached per state; a state the artwork cannot decode stays absent rather than re-read
  // on every frame.
  [[nodiscard]] const PixelSurface* portrait(CharacterState state) const;
  // The decoded, keyed mouth sprite for a shape, or nothing for a package that declares no
  // performance assets.
  [[nodiscard]] const PixelSurface* mouth(character::MouthShape shape) const;
  [[nodiscard]] std::optional<character::MouthPlacement> mouthPlacement() const noexcept;

private:
  std::optional<character::Package> package_;
  std::string packageError_;
  std::shared_ptr<const paint::Image> lookPortrait_;
  std::shared_ptr<const paint::Image> lookStage_;
  mutable std::map<CharacterState, std::optional<PixelSurface>> portraits_;
  mutable std::map<character::MouthShape, std::optional<PixelSurface>> mouths_;
  mutable std::optional<character::MouthPlacement> placement_;
};

}  // namespace seam::native_ui::design
