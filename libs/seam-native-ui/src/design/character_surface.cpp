#include "seam/native_ui/design/character_surface.hpp"
#include "seam/native_ui/design/shell_strings.hpp"
#include "seam/native_ui/paint/display_list.hpp"

#include "seam/native_ui/diagnostic_presentation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <system_error>
#include <utility>
#include <vector>

namespace seam::native_ui::design {
namespace {

using paint::Canvas2D;
using paint::Path;
using paint::StrokeStyle;

constexpr double kPi = std::numbers::pi;

// A nearest-sampled, source-over blit of one PixelSurface into the frame, with an optional shape mask
// and source rectangle. The pixel surfaces the character package decodes are PPM files the vector
// canvas cannot address, so they reach the frame through the raster canvas the shell already paints
// into; the look's own PNG artwork uses the vector path, where its alpha and its circular clip are
// the backend's own work. Samples are straight-alpha, matching Color::bgra and PixelSurface::loadPpm.
enum class Mask : std::uint8_t { None, Circle, Rounded };

bool maskCovers(Mask mask, double u, double v, double radiusFraction) noexcept {
  switch (mask) {
    case Mask::None: return true;
    case Mask::Circle: {
      const auto du = u * 2.0 - 1.0;
      const auto dv = v * 2.0 - 1.0;
      return du * du + dv * dv <= 1.0;
    }
    case Mask::Rounded: {
      const auto r = std::clamp(radiusFraction, 0.0, 0.5);
      const auto dx = std::max(0.0, std::max(r - u, u - (1.0 - r)));
      const auto dy = std::max(0.0, std::max(r - v, v - (1.0 - r)));
      return dx * dx + dy * dy <= r * r;
    }
  }
  return true;
}

// The destination is where the pixels land; the maskBounds is the shape the mask is inscribed in.
// They differ whenever the artwork is contained in the shape rather than stretched to it: a 320x480
// portrait inside a square ring must be cut by the ring's circle, not by its own narrower box, or the
// face is clipped into a vertical ellipse.
void blit(RasterCanvas& canvas, ui::Rect destination, ui::Rect maskBounds, const PixelSurface& image,
          Mask mask, double cornerFraction, ui::Rect source, double opacity) {
  if (destination.width <= 0.0 || destination.height <= 0.0 || image.width() == 0U ||
      image.height() == 0U)
    return;
  const auto alpha = std::clamp(opacity, 0.0, 1.0);
  if (alpha <= 0.0) return;
  auto& frame = canvas.surface();
  if (frame.width() == 0U || frame.height() == 0U) return;
  const auto scale = canvas.scale();
  const auto sourceX = std::clamp(source.x, 0.0, static_cast<double>(image.width()));
  const auto sourceY = std::clamp(source.y, 0.0, static_cast<double>(image.height()));
  const auto sourceWidth = std::clamp(source.width, 0.0,
                                      static_cast<double>(image.width()) - sourceX);
  const auto sourceHeight = std::clamp(source.height, 0.0,
                                       static_cast<double>(image.height()) - sourceY);
  if (sourceWidth <= 0.0 || sourceHeight <= 0.0) return;
  const auto left = static_cast<std::int32_t>(std::lround(destination.x * scale));
  const auto top = static_cast<std::int32_t>(std::lround(destination.y * scale));
  const auto width = static_cast<std::int32_t>(std::lround(destination.width * scale));
  const auto height = static_cast<std::int32_t>(std::lround(destination.height * scale));
  if (width <= 0 || height <= 0) return;
  const auto pixels = image.pixels();
  const auto target = frame.width();
  const auto targetRows = frame.height();
  auto framePixels = frame.pixels();
  const auto maskWidth = maskBounds.width > 0.0 ? maskBounds.width : destination.width;
  const auto maskHeight = maskBounds.height > 0.0 ? maskBounds.height : destination.height;
  // Everything that depends only on the column (or only on the source alpha) is worked out once per
  // blit rather than once per pixel; the arithmetic is the same, so the pixels are too.
  std::vector<std::size_t> columnImageX(static_cast<std::size_t>(width));
  std::vector<double> columnMaskU(static_cast<std::size_t>(width));
  for (auto column = 0; column < width; ++column) {
    const auto sourceU = (static_cast<double>(column) + 0.5) / static_cast<double>(width);
    columnMaskU[static_cast<std::size_t>(column)] =
        (destination.x - maskBounds.x + (static_cast<double>(column) + 0.5) / scale) / maskWidth;
    columnImageX[static_cast<std::size_t>(column)] = std::min<std::size_t>(
        image.width() - 1U, static_cast<std::size_t>(sourceX + sourceU * sourceWidth));
  }
  std::array<std::uint32_t, 256U> blendedFor{};
  for (std::uint32_t sourceAlpha = 0U; sourceAlpha < 256U; ++sourceAlpha)
    blendedFor[sourceAlpha] = static_cast<std::uint32_t>(
        std::lround(alpha * static_cast<double>(sourceAlpha)));
  const auto firstColumn = std::max(0, -left);
  const auto endColumn = std::min(width, static_cast<std::int32_t>(target) - left);
  for (auto row = 0; row < height; ++row) {
    const auto frameY = top + row;
    if (frameY < 0 || frameY >= static_cast<std::int32_t>(targetRows)) continue;
    const auto sourceV = (static_cast<double>(row) + 0.5) / static_cast<double>(height);
    const auto imageY = std::min<std::size_t>(
        image.height() - 1U,
        static_cast<std::size_t>(sourceY + sourceV * sourceHeight));
    // The mask is sampled in the shape's own space, so an inset portrait is cut by the shape.
    const auto maskV =
        (destination.y - maskBounds.y + (static_cast<double>(row) + 0.5) / scale) / maskHeight;
    const auto* sourceRow = pixels.data() + imageY * image.width();
    auto* frameRow = framePixels.data() + static_cast<std::size_t>(frameY) * target;
    for (auto column = firstColumn; column < endColumn; ++column) {
      const auto frameX = left + column;
      if (!maskCovers(mask, columnMaskU[static_cast<std::size_t>(column)], maskV, cornerFraction))
        continue;
      const auto sample = sourceRow[columnImageX[static_cast<std::size_t>(column)]];
      const auto sourceAlpha = static_cast<std::uint32_t>((sample >> 24U) & 0xFFU);
      if (sourceAlpha == 0U) continue;
      const auto blended = blendedFor[sourceAlpha];
      if (blended == 0U) continue;
      const auto inverse = 255U - std::min(255U, blended);
      auto& pixel = frameRow[static_cast<std::size_t>(frameX)];
      const auto destinationBlue = pixel & 0xFFU;
      const auto destinationGreen = (pixel >> 8U) & 0xFFU;
      const auto destinationRed = (pixel >> 16U) & 0xFFU;
      const auto sourceBlue = sample & 0xFFU;
      const auto sourceGreen = (sample >> 8U) & 0xFFU;
      const auto sourceRed = (sample >> 16U) & 0xFFU;
      const auto blue = (sourceBlue * blended + destinationBlue * inverse + 127U) / 255U;
      const auto green = (sourceGreen * blended + destinationGreen * inverse + 127U) / 255U;
      const auto red = (sourceRed * blended + destinationRed * inverse + 127U) / 255U;
      pixel = blue | (green << 8U) | (red << 16U) | 0xFF000000U;
    }
  }
}

// Contain-fit: the source aspect inside the destination, centred. A package portrait is 320x480 and
// a look portrait is square, so neither is stretched to the other's shape.
ui::Rect fitInside(ui::Rect destination, double aspect) noexcept {
  if (destination.width <= 0.0 || destination.height <= 0.0 || !(aspect > 0.0)) return {};
  auto width = destination.width;
  auto height = width / aspect;
  if (height > destination.height) {
    height = destination.height;
    width = height * aspect;
  }
  return {destination.x + (destination.width - width) * 0.5,
          destination.y + (destination.height - height) * 0.5, width, height};
}

// Cover: scale until the shape is covered, then keep the top of the frame centred horizontally.
ui::Rect coverKeepingTop(ui::Rect destination, double aspect) noexcept {
  if (destination.width <= 0.0 || destination.height <= 0.0 || !(aspect > 0.0)) return {};
  auto height = destination.height;
  auto width = height * aspect;
  if (width < destination.width) {
    width = destination.width;
    height = width / aspect;
  }
  return {destination.x + (destination.width - width) * 0.5, destination.y, width, height};
}

// The head region of a source frame, as a square at the top centre: what a 40-point toast crop shows.
// The share is the one assumption this file makes about a figure's proportions, and it is the
// standing-figure convention the package's assets follow (a 320x480 frame keeps the head and a raised
// hand in its top two fifths). A wide frame is cropped to the square rather than squashed.
inline constexpr double kHeadCropHeightShare = 0.42;

ui::Rect headRegionOf(std::uint32_t width, std::uint32_t height) noexcept {
  const auto side = std::min(static_cast<double>(height) * kHeadCropHeightShare,
                             static_cast<double>(width));
  return {(static_cast<double>(width) - side) * 0.5, 0.0, side, side};
}

Color stateTint(const DesignTokens& tokens, CharacterState state) noexcept {
  if (state == CharacterState::Warning) return tokens.color.warning;
  if (state == CharacterState::Error) return tokens.color.error;
  return tokens.color.accent;
}

// The eyelid of the idle blink. The development turnaround declares no eye anchor, so the lid is a
// band across the portrait's upper third rather than a per-eye cut-out: an approximation that is
// visible as motion and claims nothing about the face it cannot know.
void paintBlinkLid(Canvas2D& canvas, ui::Rect portrait, double blink, Color ink) {
  if (blink <= 0.0 || portrait.height <= 0.0) return;
  const auto lidHeight = std::max(1.0, portrait.height * 0.016 * std::clamp(blink, 0.0, 1.0));
  const auto y = portrait.y + portrait.height * 0.40 - lidHeight * 0.5;
  canvas.fill(Path::rect({portrait.x + portrait.width * 0.16, y, portrait.width * 0.68, lidHeight}),
              withAlpha(ink, 0.75 * std::clamp(blink, 0.0, 1.0)));
}

}  // namespace

std::string_view characterStateName(CharacterState state) noexcept {
  switch (state) {
    case CharacterState::Idle: return tr(Str::Idle);
    case CharacterState::Listening: return tr(Str::Listening);
    case CharacterState::Singing: return tr(Str::Singing);
    case CharacterState::Rendering: return tr(Str::Rendering);
    case CharacterState::Complete: return tr(Str::Complete);
    case CharacterState::Warning: return tr(Str::Warning);
    case CharacterState::Error: return tr(Str::Error);
  }
  return tr(Str::Idle);
}

character::State characterPackageState(CharacterState state) noexcept {
  switch (state) {
    case CharacterState::Idle:
    case CharacterState::Listening: return character::State::Neutral;
    case CharacterState::Singing: return character::State::Focused;
    case CharacterState::Rendering: return character::State::Rendering;
    case CharacterState::Complete: return character::State::Complete;
    case CharacterState::Warning: return character::State::Warning;
    case CharacterState::Error: return character::State::Error;
  }
  return character::State::Neutral;
}

CharacterSurfaceInput characterSurfaceInput(const EditorSceneState& state,
                                            std::optional<float> auditionLevel) noexcept {
  CharacterSurfaceInput input;
  input.voiceIdentity = state.voiceIdentity.state;
  input.render = state.renderStatus.state;
  input.playing = state.playing;
  input.auditionLevel = auditionLevel;
  if (const auto& performance = state.characterPerformance; performance.has_value()) {
    input.performing = performance->performing;
    input.energy = performance->energy;
    input.mouth = performance->mouth;
    input.audibleStale = performance->audibleStale;
  }
  input.audibleStale = input.audibleStale || state.renderStatus.audibleAudioStale;
  for (const auto& diagnostic : state.diagnostics) {
    if (diagnostic.code == "BANK_MISSING") {
      input.bankMissing = true;
      break;
    }
  }
  // The identity resolver owns the completion dwell: it reports Complete only for a genuine
  // transition to Ready for the publication on screen, and only while the transport is idle. Reading
  // its verdict is what keeps a bare Ready or a fraction of 1.0 from claiming a completion.
  input.completeDwell = state.voiceIdentity.state == VoiceIdentityState::Complete;
  if (!std::isfinite(input.energy) || input.energy < 0.0F) input.energy = 0.0F;
  return input;
}

CharacterState resolveCharacterState(const CharacterSurfaceInput& input) noexcept {
  if (input.render == RenderStatusState::Failed || input.bankMissing ||
      input.voiceIdentity == VoiceIdentityState::Error)
    return CharacterState::Error;
  if (input.voiceIdentity == VoiceIdentityState::Warning || input.audibleStale ||
      input.render == RenderStatusState::Stale)
    return CharacterState::Warning;
  // A phrase that is actually performing at the playhead is what the creator is hearing, so it
  // outranks a render in flight and a listening audition.
  if (input.playing && input.performing) return CharacterState::Singing;
  if (input.auditionLevel.has_value()) return CharacterState::Listening;
  if (input.render == RenderStatusState::Rendering) return CharacterState::Rendering;
  if (input.completeDwell) return CharacterState::Complete;
  return CharacterState::Idle;
}

bool characterStateAnimates(CharacterState state) noexcept {
  return state == CharacterState::Idle || state == CharacterState::Singing ||
         state == CharacterState::Rendering;
}

bool characterMotionShown(CharacterState state, bool figureDrawn, bool ringDrawn) noexcept {
  switch (state) {
    case CharacterState::Idle:
    case CharacterState::Singing: return figureDrawn;
    case CharacterState::Rendering: return ringDrawn;
    case CharacterState::Listening:
    case CharacterState::Complete:
    case CharacterState::Warning:
    case CharacterState::Error: return false;
  }
  return false;
}

std::filesystem::path characterStateAssetPath(const character::Package& package,
                                              CharacterState state) {
  return package.assetPath(characterPackageState(state));
}

std::filesystem::path characterMouthAssetPath(const character::Package& package,
                                              character::MouthShape shape) {
  return package.mouthAssetPath(shape);
}

CharacterArtworkChoice characterArtworkChoice(const character::Package* package,
                                              CharacterState state) {
  if (package == nullptr) return {};
  const auto path = characterStateAssetPath(*package, state);
  return path.empty() ? CharacterArtworkChoice{} : CharacterArtworkChoice{true, path};
}

CharacterArtworkChoice characterMouthChoice(const character::Package* package,
                                            character::MouthShape shape) {
  if (package == nullptr || !package->manifest.declaresPerformance())
    return CharacterArtworkChoice{};
  const auto path = characterMouthAssetPath(*package, shape);
  return path.empty() ? CharacterArtworkChoice{} : CharacterArtworkChoice{true, path};
}

std::optional<Color> keyCornerColor(PixelSurface& surface) noexcept {
  if (surface.width() == 0U || surface.height() == 0U) return std::nullopt;
  auto pixels = surface.pixels();
  constexpr std::uint32_t kColorMask = 0x00FFFFFFU;
  const auto key = pixels.front() & kColorMask;
  const auto bottomLeft = static_cast<std::size_t>(surface.height() - 1U) * surface.width();
  const auto bottomRight = bottomLeft + surface.width() - 1U;
  if ((pixels[surface.width() - 1U] & kColorMask) != key ||
      (pixels[bottomLeft] & kColorMask) != key || (pixels[bottomRight] & kColorMask) != key)
    return std::nullopt;
  Color keyed{static_cast<std::uint8_t>((key >> 16U) & 0xFFU),
              static_cast<std::uint8_t>((key >> 8U) & 0xFFU),
              static_cast<std::uint8_t>(key & 0xFFU), 255U};
  // The keyed pixels keep their color and drop to zero alpha, so a straight-alpha source-over
  // contributes nothing: the sprite composites as transparent without inventing a matte color.
  for (auto& pixel : pixels) {
    if ((pixel & kColorMask) == key) pixel &= kColorMask;
  }
  return keyed;
}

StagePlacement resolveStage(const StageInput& input, double stageAspect) noexcept {
  StagePlacement placement;
  if (!input.fullRack || input.highContrast || input.laneExpanded) return placement;
  if (!(stageAspect > 0.0) || !std::isfinite(stageAspect)) return placement;
  const auto height = input.grid.height * kStageHeightFraction;
  const auto width = height * stageAspect;
  // The figure sits behind the notes, so a roll that cannot hold it at its bottom-right anchor keeps
  // it off rather than drawing it over them: a figure under 120 points tall is a smear, and one that
  // plus its inset does not fit the roll would have to be clipped or pushed off its anchor. The
  // breakpoint rule is not repeated here: outside the full rack the caller already keeps it off.
  if (height < 120.0 || height > input.grid.height || width < 48.0 ||
      width + kStageAnchorInset > input.grid.width)
    return placement;
  placement.bounds = {input.grid.right() - width - kStageAnchorInset,
                      input.grid.bottom() - height, width, height};
  placement.shown = true;
  placement.targetOpacity = input.pointerInside || input.noteIntersects ? kStageOpacityCrowded
                                                                       : kStageOpacityRest;
  return placement;
}

void StageFade::reset() noexcept {
  opacity_ = kStageOpacityRest;
  from_ = opacity_;
  target_ = opacity_;
  fading_ = false;
}

double StageFade::advance(const StagePlacement& placement,
                          std::chrono::steady_clock::time_point now, bool reduceMotion) noexcept {
  if (!placement.shown) {
    fading_ = false;
    opacity_ = 0.0;
    from_ = 0.0;
    target_ = 0.0;
    return opacity_;
  }
  if (reduceMotion || kStageFadeSeconds <= 0.0) {
    fading_ = false;
    from_ = placement.targetOpacity;
    opacity_ = placement.targetOpacity;
    target_ = placement.targetOpacity;
    return opacity_;
  }
  if (!fading_ && std::abs(placement.targetOpacity - target_) > 1e-9) {
    // A new target (rest to crowded, or the reverse) starts its own 180 ms fade from wherever the
    // opacity is now, so an interrupted fade continues rather than snapping.
    from_ = opacity_;
    target_ = placement.targetOpacity;
    startedAt_ = now;
    fading_ = true;
  }
  if (!fading_) return opacity_;
  const auto elapsed = std::chrono::duration<double>(now - startedAt_).count();
  if (elapsed >= kStageFadeSeconds) {
    opacity_ = target_;
    fading_ = false;
    return opacity_;
  }
  const auto t = std::clamp(elapsed / kStageFadeSeconds, 0.0, 1.0);
  const auto eased = t * t * (3.0 - 2.0 * t);
  opacity_ = from_ + (target_ - from_) * eased;
  return opacity_;
}

std::optional<std::string_view> emptyProjectPrompt(std::size_t noteCount) noexcept {
  if (noteCount > 0U) return std::nullopt;
  return kEmptyProjectPrompt;
}

std::optional<CharacterToast> characterErrorToast(const SingLayout& layout,
                                                const CharacterSurfaceInput& input,
                                                std::string_view diagnostic,
                                                std::optional<ui::Rect> diagnosticsToast) {
  const auto missing = input.bankMissing;
  const auto failed = input.render == RenderStatusState::Failed;
  if (!missing && !failed) return std::nullopt;
  const auto region = layout.lane;
  constexpr double kPose = 40.0;
  constexpr double kPadding = 12.0;
  constexpr double kHeight = 56.0;
  const auto width = std::min(440.0, region.width - 32.0);
  if (width < kPose + 120.0 || region.height <= 0.0) return std::nullopt;
  // The floor is the status bar, or the diagnostics toast's row when one shows, which the toast
  // stacks above. The ceiling is the lane's tab strip, whose tabs and review opener stay reachable.
  const auto stacked = diagnosticsToast.has_value() && diagnosticsToast->height > 0.0;
  const auto bottom = stacked ? diagnosticsToast->y - 8.0 : layout.status.y - 12.0;
  const auto ceiling =
      (layout.laneTabs.height > 0.0 ? layout.laneTabs.bottom() : region.y) + 4.0;
  if (bottom - kHeight < ceiling) return std::nullopt;
  CharacterToast toast;
  toast.bounds = {region.x + 16.0, bottom - kHeight, width, kHeight};
  toast.pose = {toast.bounds.x + kPadding, toast.bounds.y + (kHeight - kPose) * 0.5, kPose, kPose};
  toast.title = missing ? std::string{tr(Str::VoicebankNeedsAttention)}
                        : std::string{tr(Str::RenderDidNotComplete)};
  toast.reason = std::string{diagnostic};
  return toast;
}

CharacterAnimator::CharacterAnimator(std::uint64_t seed) noexcept
    : random_(seed == 0U ? 0x2545F4914F6CDD1DULL : seed) {
  intervalSeconds_ = drawInterval();
}

double CharacterAnimator::drawInterval() noexcept {
  // SplitMix64: one seeded round per blink, reproducible across runs and platforms without the
  // standard library's own generator state, so a capture and a re-run agree. The high 53 bits are
  // exactly the mantissa of a double, so the unit value is uniform over [0, 1) with no bias.
  random_ += 0x9E3779B97F4A7C15ULL;
  auto value = random_;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  value ^= value >> 31U;
  constexpr double kMantissa = 1.0 / 9007199254740992.0;  // 2^-53
  const auto unit = static_cast<double>(value >> 11U) * kMantissa;
  return 4.0 + 3.0 * unit;
}

double CharacterAnimator::secondsUntilBlink(std::chrono::steady_clock::time_point now) const
    noexcept {
  if (!scheduled_) return 0.0;
  return std::max(0.0, std::chrono::duration<double>(blinkAt_ - now).count());
}

bool CharacterAnimator::advance(CharacterState state,
                                std::chrono::steady_clock::time_point now, bool reduceMotion) {
  const auto previous = motion_;
  if (reduceMotion || !characterStateAnimates(state)) {
    motion_ = Motion{};
    moving_ = false;
    scheduled_ = false;
    return false;
  }
  motion_ = Motion{};
  if (state == CharacterState::Idle || state == CharacterState::Singing) {
    const auto seconds = std::chrono::duration<double>(now.time_since_epoch()).count();
    motion_.breath =
        kBreathAmplitude * std::sin(2.0 * kPi * kBreathHertz * seconds);
    if (state == CharacterState::Idle) {
      if (!scheduled_) {
        intervalSeconds_ = drawInterval();
        blinkAt_ = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                             std::chrono::duration<double>(intervalSeconds_));
        scheduled_ = true;
      } else if (now >= blinkAt_) {
        const auto intoBlink = std::chrono::duration<double>(now - blinkAt_).count();
        if (intoBlink >= kBlinkSeconds) {
          intervalSeconds_ = drawInterval();
          blinkAt_ = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                               std::chrono::duration<double>(intervalSeconds_));
        } else {
          const auto t = std::clamp(intoBlink / kBlinkSeconds, 0.0, 1.0);
          motion_.blink = t < 0.5 ? t * 2.0 : (1.0 - t) * 2.0;
        }
      }
    }
  }
  if (state == CharacterState::Rendering) {
    const auto seconds = std::chrono::duration<double>(now.time_since_epoch()).count();
    const auto turns = seconds / kSpinnerSecondsPerTurn;
    motion_.spinner = turns - std::floor(turns);
  }
  moving_ = true;
  return motion_.blink != previous.blink || motion_.breath != previous.breath ||
         motion_.spinner != previous.spinner;
}

ui::Rect paintCharacterPortrait(CharacterCanvas canvas, ui::Rect destination, bool circular,
                                const PixelSurface* packagePortrait,
                                const paint::Image* lookPortrait, double opacity, PortraitFit fit,
                                double drift) {
  if (destination.width <= 0.0 || destination.height <= 0.0) return {};
  // The drift moves what is drawn, never the shape it is drawn inside.
  auto artwork = destination;
  artwork.y += drift;
  if (packagePortrait != nullptr && packagePortrait->width() > 0U &&
      packagePortrait->height() > 0U) {
    // The package's own PPM portrait, fitted to the destination and masked to the ring's shape. A
    // cover fit with the top edge kept is what a round ring wants; containment is what a pose wants.
    const auto source = fit == PortraitFit::HeadSquare
                            ? headRegionOf(packagePortrait->width(), packagePortrait->height())
                            : ui::Rect{0.0, 0.0, static_cast<double>(packagePortrait->width()),
                                       static_cast<double>(packagePortrait->height())};
    const auto aspect = source.height > 0.0 ? source.width / source.height : 1.0;
    const auto fitted = fit == PortraitFit::CoverTop ? coverKeepingTop(artwork, aspect)
                                                     : fitInside(artwork, aspect);
    if (fitted.width <= 0.0) return {};
    // The mask is the shape being drawn into, so a contained portrait is still cut by the ring.
    blit(canvas.raster, fitted, destination, *packagePortrait,
         circular ? Mask::Circle : Mask::Rounded,
         circular ? 0.0 : 0.16, source, opacity);
    return fitted;
  }
  if (lookPortrait == nullptr) return {};
  const auto aspect = static_cast<double>(lookPortrait->width()) /
                      static_cast<double>(lookPortrait->height());
  const auto fitted = fit == PortraitFit::CoverTop ? coverKeepingTop(artwork, aspect)
                                                   : fitInside(artwork, aspect);
  auto& vector = canvas.vector;
  vector.save();
  vector.setAlpha(std::clamp(opacity, 0.0, 1.0));
  if (circular) {
    const auto center = ui::Point{destination.x + destination.width * 0.5,
                                  destination.y + destination.height * 0.5};
    vector.clipPath(Path::circle(center, std::min(destination.width, destination.height) * 0.5));
  } else {
    vector.clipPath(Path::roundedRect(fitted, std::min(fitted.width, fitted.height) * 0.16));
  }
  vector.drawImage(*lookPortrait, fitted);
  vector.restore();
  return fitted;
}

void paintCharacterMouth(CharacterCanvas canvas, ui::Rect portraitBounds,
                         const character::MouthPlacement& placement, const PixelSurface& sprite,
                         double opacity) {
  if (portraitBounds.width <= 0.0 || portraitBounds.height <= 0.0 || sprite.width() == 0U ||
      sprite.height() == 0U)
    return;
  const ui::Rect destination{portraitBounds.x + placement.x * portraitBounds.width,
                             portraitBounds.y + placement.y * portraitBounds.height,
                             placement.width * portraitBounds.width,
                             placement.height * portraitBounds.height};
  // The whole sprite is drawn at its declared placement: the manifest's normalized rectangle is the
  // sprite's own bounds inside the portrait, so no part of it is cropped away.
  blit(canvas.raster, destination, destination, sprite, Mask::None, 0.0,
       {0.0, 0.0, static_cast<double>(sprite.width()), static_cast<double>(sprite.height())},
       opacity);
}

namespace {

// The ring's geometry, shared by its still base and its live part.
struct RingGeometry final {
  bool scene{false};
  ui::Point center{};
  double tickInner{0.0};
  double tickOuter{0.0};
  double portraitRadius{0.0};
  Color tint{};
  bool alarming{false};
};

RingGeometry ringGeometry(const DesignTokens& tokens, const SingerRingSpec& spec) {
  const auto& r = spec.bounds;
  RingGeometry g;
  g.scene = tokens.mode == DesignMode::Scene;
  g.center = {r.x + r.width * 0.5, r.y + r.height * 0.5};
  const auto radius = std::min(r.width, r.height) * 0.5;
  const auto tickLength = g.scene ? 9.0 : 7.0;
  g.tickOuter = radius;
  g.tickInner = radius - tickLength;
  g.portraitRadius = std::max(2.0, g.tickInner - (g.scene ? 5.0 : 4.0));
  g.tint = stateTint(tokens, spec.state);
  g.alarming = spec.state == CharacterState::Warning || spec.state == CharacterState::Error;
  return g;
}

// Scene cycles the look's four segment colours three ticks at a time; Emo draws one accent, and an
// alarming state recolors the ring amber or red over either look.
std::size_t tickColorGroup(const RingGeometry& g, std::size_t tick) noexcept {
  return g.scene && !g.alarming ? (tick / 3U) % 4U : 0U;
}

Color tickColor(const DesignTokens& tokens, const RingGeometry& g, std::size_t group) noexcept {
  if (g.alarming) return g.tint;
  return g.scene ? tokens.trackColors[group] : tokens.color.accent;
}

std::pair<ui::Point, ui::Point> tickEnds(const RingGeometry& g, std::size_t tick) {
  const auto angle = -kPi * 0.5 +
                     static_cast<double>(tick) * 2.0 * kPi / static_cast<double>(kSingerRingTicks);
  return {{g.center.x + std::cos(angle) * g.tickInner, g.center.y + std::sin(angle) * g.tickInner},
          {g.center.x + std::cos(angle) * g.tickOuter, g.center.y + std::sin(angle) * g.tickOuter}};
}

void addTick(Path& path, const RingGeometry& g, std::size_t tick) {
  const auto [inner, outer] = tickEnds(g, tick);
  path.moveTo(inner).lineTo(outer);
}

ui::Rect tickBounds(const RingGeometry& g, std::size_t tick, double reach) {
  const auto [inner, outer] = tickEnds(g, tick);
  const auto left = std::min(inner.x, outer.x) - reach;
  const auto top = std::min(inner.y, outer.y) - reach;
  return {left, top, std::abs(outer.x - inner.x) + 2.0 * reach,
          std::abs(outer.y - inner.y) + 2.0 * reach};
}

// A glow reaches about its radius; the sprite keeps twice that, so none of it is cut.
constexpr double kGlowSpriteReach = 2.0;

// Rasterizes draw (logical coordinates, as on the frame) into the sprite covering logical.
bool renderGlowSprite(GlowSprite& sprite, ui::Rect logical, double scale,
                      const std::function<void(paint::Canvas2D&)>& draw) {
  sprite.ready = false;
  const auto x0 = static_cast<std::int32_t>(std::floor(logical.x * scale));
  const auto y0 = static_cast<std::int32_t>(std::floor(logical.y * scale));
  const auto x1 = static_cast<std::int32_t>(std::ceil(logical.right() * scale));
  const auto y1 = static_cast<std::int32_t>(std::ceil(logical.bottom() * scale));
  if (x1 <= x0 || y1 <= y0) return false;
  if (!sprite.pixels.resize(static_cast<std::uint32_t>(x1 - x0),
                            static_cast<std::uint32_t>(y1 - y0)))
    return false;
  std::fill(sprite.pixels.pixels().begin(), sprite.pixels.pixels().end(), 0U);
  auto canvas = paint::makeCanvas(sprite.pixels, scale);
  if (!canvas) return false;
  canvas->translate(-static_cast<double>(x0) / scale, -static_cast<double>(y0) / scale);
  draw(*canvas);
  canvas->flush();
  sprite.x = x0;
  sprite.y = y0;
  sprite.ready = true;
  return true;
}

// Premultiplied source-over onto the opaque frame.
void compositeGlowSprite(RasterCanvas& raster, const GlowSprite& sprite) {
  auto& frame = raster.surface();
  const auto frameWidth = static_cast<std::int32_t>(frame.width());
  const auto frameHeight = static_cast<std::int32_t>(frame.height());
  const auto width = static_cast<std::int32_t>(sprite.pixels.width());
  const auto height = static_cast<std::int32_t>(sprite.pixels.height());
  const auto source = sprite.pixels.pixels();
  auto target = frame.pixels();
  const auto firstColumn = std::max(0, -sprite.x);
  const auto endColumn = std::min(width, frameWidth - sprite.x);
  for (auto row = std::max(0, -sprite.y); row < height && sprite.y + row < frameHeight; ++row) {
    const auto* from = source.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(width);
    auto* to = target.data() +
               static_cast<std::size_t>(sprite.y + row) * static_cast<std::size_t>(frameWidth) +
               static_cast<std::size_t>(sprite.x);
    for (auto column = firstColumn; column < endColumn; ++column) {
      const auto s = from[column];
      const auto alpha = s >> 24U;
      if (alpha == 0U) continue;
      auto& d = to[column];
      if (alpha == 255U) {
        d = s;
        continue;
      }
      const auto inverse = 255U - alpha;
      const auto channel = [&](std::uint32_t shift) {
        const auto value = ((s >> shift) & 0xFFU) + (((d >> shift) & 0xFFU) * inverse + 127U) / 255U;
        return std::min(value, 255U) << shift;
      };
      d = channel(0U) | channel(8U) | channel(16U) | 0xFF000000U;
    }
  }
}

}  // namespace

GlowSprite& RingGlowCache::tick(std::uint64_t key, std::size_t index) {
  if (key != tickKey_) {
    for (auto& sprite : ticks_) sprite.ready = false;
    tickKey_ = key;
  }
  return ticks_[index % kSingerRingTicks];
}

GlowSprite& RingGlowCache::outline(std::uint64_t key) {
  if (key != outlineKey_) {
    outline_.ready = false;
    outlineKey_ = key;
  }
  return outline_;
}

std::size_t RingGlowCache::bytes() const noexcept {
  std::size_t total = outline_.pixels.pixels().size_bytes();
  for (const auto& sprite : ticks_) total += sprite.pixels.pixels().size_bytes();
  return total;
}

void paintSingerRingBase(paint::Canvas2D& vector, const DesignTokens& tokens,
                         const SingerRingSpec& spec) {
  const auto& r = spec.bounds;
  if (r.width <= 0.0 || r.height <= 0.0) return;
  const auto g = ringGeometry(tokens, spec);
  vector.fill(Path::circle(g.center, g.portraitRadius),
              paint::RadialGradient{g.center, g.portraitRadius,
                                    {{0.0, tokens.color.surfaceRaised},
                                     {1.0, tokens.color.surfaceSunken}}});
  // Every tick, unlit: the live part lights some of them over this, whatever the level.
  std::array<Path, 4U> unlit;
  for (std::size_t i = 0U; i < kSingerRingTicks; ++i) addTick(unlit[tickColorGroup(g, i)], g, i);
  for (std::size_t group = 0U; group < unlit.size(); ++group) {
    if (unlit[group].empty()) continue;
    vector.stroke(unlit[group], withAlpha(tickColor(tokens, g, group), 0.16),
                  StrokeStyle{g.scene ? 2.6 : 1.2, false});
  }
}

bool paintSingerRing(CharacterCanvas canvas, const DesignTokens& tokens,
                     const SingerRingSpec& spec) {
  paintSingerRingBase(canvas.vector, tokens, spec);
  return paintSingerRingLive(canvas, tokens, spec);
}

bool paintSingerRingLive(CharacterCanvas canvas, const DesignTokens& tokens,
                         const SingerRingSpec& spec) {
  const auto& r = spec.bounds;
  if (r.width <= 0.0 || r.height <= 0.0) return false;
  auto& vector = canvas.vector;
  const auto g = ringGeometry(tokens, spec);
  const auto center = g.center;
  const auto portraitRadius = g.portraitRadius;
  const auto tint = g.tint;
  // The idle breathing moves the figure inside its ring rather than moving the ring, so the ring, the
  // ticks and the state colour stay exactly where the layout put them.
  const ui::Rect portraitBox{center.x - portraitRadius, center.y - portraitRadius,
                             portraitRadius * 2.0, portraitRadius * 2.0};
  const auto litFraction = std::clamp(spec.lit, 0.0, 1.0);
  const auto litCount = static_cast<std::size_t>(
      std::lround(litFraction * static_cast<double>(kSingerRingTicks)));
  auto rotationTicks = static_cast<std::ptrdiff_t>(std::lround(
      (spec.rotation - std::floor(spec.rotation)) * static_cast<double>(kSingerRingTicks)));
  rotationTicks %= static_cast<std::ptrdiff_t>(kSingerRingTicks);
  if (rotationTicks < 0) rotationTicks += static_cast<std::ptrdiff_t>(kSingerRingTicks);
  // The lit ticks, one path and one glow per colour: a glow per tick made the ring the costliest
  // thing a playback frame drew.
  std::array<Path, 4U> lit;
  for (std::size_t i = 0U; i < kSingerRingTicks; ++i) {
    const auto index = (static_cast<std::ptrdiff_t>(i) + rotationTicks) %
                       static_cast<std::ptrdiff_t>(kSingerRingTicks);
    if (index < static_cast<std::ptrdiff_t>(litCount)) addTick(lit[tickColorGroup(g, i)], g, i);
  }
  const auto tickWidth = g.scene ? 2.6 : 1.2;
  const auto scale = canvas.raster.scale();
  const auto strokeTick = [&](paint::Canvas2D& target, std::size_t i) {
    const auto color = tickColor(tokens, g, tickColorGroup(g, i));
    Path tick;
    addTick(tick, g, i);
    target.save();
    target.setGlow(withAlpha(color, 0.9), 5.0);
    target.stroke(tick, color, StrokeStyle{tickWidth, false});
    target.restore();
  };
  if (spec.glows != nullptr) {
    // Each lit tick with its own glow, as a sprite drawn once for this ring and composited.
    vector.flush();
    paint::ContentHash key;
    key.add(scale).add(g.center).add(g.tickInner).add(g.tickOuter).add(tickWidth);
    for (std::size_t group = 0U; group < 4U; ++group) key.add(tickColor(tokens, g, group));
    for (std::size_t i = 0U; i < kSingerRingTicks; ++i) {
      const auto index = (static_cast<std::ptrdiff_t>(i) + rotationTicks) %
                         static_cast<std::ptrdiff_t>(kSingerRingTicks);
      if (index >= static_cast<std::ptrdiff_t>(litCount)) continue;
      auto& sprite = spec.glows->tick(key.value(), i);
      if (!sprite.ready) {
        const auto reach = tickWidth * 0.5 + 5.0 * kGlowSpriteReach;
        static_cast<void>(renderGlowSprite(sprite, tickBounds(g, i, reach), scale,
                                           [&](paint::Canvas2D& target) { strokeTick(target, i); }));
      }
      if (sprite.ready) {
        compositeGlowSprite(canvas.raster, sprite);
      } else {
        strokeTick(vector, i);
        vector.flush();
      }
    }
  } else {
    for (std::size_t group = 0U; group < lit.size(); ++group) {
      if (lit[group].empty()) continue;
      const auto color = tickColor(tokens, g, group);
      vector.save();
      vector.setGlow(withAlpha(color, 0.9), 5.0);
      vector.stroke(lit[group], color, StrokeStyle{tickWidth, false});
      vector.restore();
    }
  }
  // The portrait is drawn after the vector work is on the surface, because the raster front writes
  // pixels directly and would otherwise be overdrawn by a later vector pass.
  vector.flush();
  const auto fittedPortrait =
      paintCharacterPortrait(canvas, portraitBox, true, spec.packagePortrait, spec.lookPortrait,
                             spec.portraitOpacity, PortraitFit::CoverTop, spec.breath);
  // The singing mouth goes on the face the portrait actually occupies, not on the ring's circle: the
  // placement is normalized within the portrait frame, which is the rectangle the artwork filled.
  if (spec.mouthSprite != nullptr && spec.mouthPlacement.has_value() && fittedPortrait.width > 0.0)
    paintCharacterMouth(canvas, fittedPortrait, *spec.mouthPlacement, *spec.mouthSprite,
                        spec.mouthOpacity);
  // The blink is drawn after the artwork and inside the ring, on the figure's own rectangle rather
  // than on the ring's circle.
  if (spec.blink > 0.0 && fittedPortrait.width > 0.0)
    paintBlinkLid(canvas.vector, fittedPortrait, spec.blink, tokens.color.textPrimary);
  const auto strokeOutline = [&](paint::Canvas2D& target) {
    target.save();
    target.setGlow(withAlpha(tint, 0.9), 10.0);
    target.stroke(Path::circle(center, portraitRadius + 1.0), withAlpha(tint, 0.85),
                  StrokeStyle{1.6});
    target.restore();
  };
  if (spec.glows != nullptr) {
    vector.flush();
    auto& sprite = spec.glows->outline(
        paint::ContentHash{}.add(scale).add(center).add(portraitRadius).add(tint).value());
    if (!sprite.ready) {
      const auto reach = portraitRadius + 1.0 + 0.8 + 10.0 * kGlowSpriteReach;
      static_cast<void>(renderGlowSprite(
          sprite, {center.x - reach, center.y - reach, reach * 2.0, reach * 2.0}, scale,
          strokeOutline));
    }
    if (sprite.ready) {
      compositeGlowSprite(canvas.raster, sprite);
      return characterMotionShown(spec.state, fittedPortrait.width > 0.0, true);
    }
  }
  strokeOutline(vector);
  return characterMotionShown(spec.state, fittedPortrait.width > 0.0, true);
}

bool paintCharacterAvatar(CharacterCanvas canvas, const DesignTokens& tokens, ui::Rect bounds,
                          CharacterState state, const PixelSurface* packagePortrait,
                          const paint::Image* lookPortrait, double opacity, double blink,
                          double breath) {
  if (bounds.width <= 0.0 || bounds.height <= 0.0) return false;
  auto& vector = canvas.vector;
  const auto tint = stateTint(tokens, state);
  const ui::Point center{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
  const auto radius = std::min(bounds.width, bounds.height) * 0.5;
  const auto inner = std::max(1.0, radius - kSingerRingThickness - 1.0);
  vector.fill(Path::circle(center, inner), tokens.color.surfaceSunken);
  vector.flush();
  const ui::Rect portraitBox{center.x - inner, center.y - inner, inner * 2.0, inner * 2.0};
  const auto figure = paintCharacterPortrait(canvas, portraitBox, true, packagePortrait,
                                             lookPortrait, opacity, PortraitFit::CoverTop, breath);
  // As in the ring, the lid closes over a figure: an empty circle has no eyes to blink.
  const auto figureDrawn = figure.width > 0.0;
  if (blink > 0.0 && figureDrawn) paintBlinkLid(vector, portraitBox, blink, tokens.color.textPrimary);
  vector.save();
  vector.setGlow(withAlpha(tint, 0.8), 5.0);
  vector.stroke(Path::circle(center, inner + 1.0), withAlpha(tint, 0.9),
                StrokeStyle{kSingerRingThickness});
  vector.restore();
  return characterMotionShown(state, figureDrawn, false);
}

void paintStageFigure(CharacterCanvas canvas, ui::Rect clip, const StagePlacement& placement,
                      double opacity, const paint::Image& stage) {
  if (!placement.shown || opacity <= 0.0 || placement.bounds.width <= 0.0 ||
      placement.bounds.height <= 0.0 || stage.width() == 0U || stage.height() == 0U)
    return;
  auto& vector = canvas.vector;
  vector.save();
  vector.clipRect(clip);
  vector.drawImage(stage, placement.bounds, opacity);
  vector.restore();
}

void paintEmptyProject(CharacterCanvas canvas, const DesignTokens& tokens,
                       const SingLayout& layout, const PixelSurface* packagePortrait,
                       const paint::Image* lookPortrait) {
  const auto& grid = layout.grid;
  if (grid.width <= 0.0 || grid.height <= 0.0) return;
  // The seated pose sits above the line, both centred on the roll: the grid still owns the double
  // click that writes the first note, so the pose is never a hit target and carries no actions.
  const auto poseHeight = std::min(grid.height * 0.42, 208.0);
  const auto centerX = grid.x + grid.width * 0.5;
  const ui::Rect poseBox{centerX - poseHeight * 0.5, grid.y + grid.height * 0.42 - poseHeight,
                         poseHeight, poseHeight};
  canvas.vector.flush();
  static_cast<void>(
      paintCharacterPortrait(canvas, poseBox, false, packagePortrait, lookPortrait, 0.42));
  const auto cx = centerX;
  const auto cy = grid.y + grid.height * 0.42;
  canvas.vector.text({cx - 200.0, cy + 6.0, 400.0, 22.0}, kEmptyProjectPrompt,
                     paint::TextStyle{paint::FontRole::Ui, tokens.type.body, 0.0,
                                      paint::TextAlign::Center, false},
                     tokens.color.textSecondary);
}

void paintCharacterToast(CharacterCanvas canvas, const DesignTokens& tokens,
                         const CharacterToast& toast, const PixelSurface* packagePortrait,
                         const paint::Image* lookPortrait) {
  auto& vector = canvas.vector;
  const auto& bounds = toast.bounds;
  if (bounds.width <= 0.0 || bounds.height <= 0.0) return;
  vector.save();
  vector.setGlow(withAlpha(tokens.color.error, 0.35), 14.0);
  vector.fill(Path::roundedRect(bounds, tokens.shape.card),
              withAlpha(tokens.color.surfaceRaised, 0.97));
  vector.restore();
  vector.stroke(Path::roundedRect(bounds, tokens.shape.card),
                withAlpha(tokens.color.error, 0.85), StrokeStyle{1.0});
  vector.fill(Path::capsule({bounds.x, bounds.y + 10.0, 3.0, bounds.height - 20.0}),
              tokens.color.error);
  vector.flush();
  // The toast shows the head-in-hand crop the plan asks for, so the source is the frame's head square.
  static_cast<void>(paintCharacterPortrait(canvas, toast.pose, false, packagePortrait, lookPortrait,
                                           1.0, PortraitFit::HeadSquare));
  const auto textX = toast.pose.right() + 12.0;
  const auto textWidth = bounds.right() - 14.0 - textX;
  vector.text({textX, bounds.y + 9.0, textWidth, 18.0}, toast.title,
              paint::TextStyle{paint::FontRole::UiSemibold, tokens.type.label, 0.6,
                               paint::TextAlign::Left, true},
              tokens.color.error);
  vector.text({textX, bounds.y + 29.0, textWidth, 18.0}, toast.reason,
              paint::TextStyle{paint::FontRole::Ui, tokens.type.smallLabel, 0.0,
                               paint::TextAlign::Left, false},
              tokens.color.textSecondary);
}

core::Result<void> CharacterSurface::loadPackage(const std::filesystem::path& packageRoot) {
  clearPackage();
  if (packageRoot.empty()) return core::success();
  auto package = character::loadPackage(packageRoot);
  if (!package) {
    // A refused package is absent, not half-loaded, and its reason is kept for the host to report.
    packageError_ = package.error().message;
    return core::Result<void>{package.error()};
  }
  package_ = std::move(package.value());
  return core::success();
}

void CharacterSurface::clearPackage() noexcept {
  package_.reset();
  packageError_.clear();
  portraits_.clear();
  mouths_.clear();
  placement_.reset();
}

const PixelSurface* CharacterSurface::portrait(CharacterState state) const {
  if (!package_.has_value()) return nullptr;
  const auto iterator = portraits_.find(state);
  if (iterator != portraits_.end())
    return iterator->second.has_value() ? &*iterator->second : nullptr;
  std::optional<PixelSurface> decoded;
  const auto path = characterStateAssetPath(*package_, state);
  if (!path.empty()) {
    auto loaded = PixelSurface::loadPpm(path);
    if (loaded) decoded = std::move(loaded.value());
  }
  // A state the artwork cannot decode stays absent, so the next frame does not re-read the file and
  // the caller falls back to the look's portrait.
  auto [slot, inserted] = portraits_.emplace(state, std::move(decoded));
  static_cast<void>(inserted);
  return slot->second.has_value() ? &*slot->second : nullptr;
}

const PixelSurface* CharacterSurface::mouth(character::MouthShape shape) const {
  if (!package_.has_value() || !package_->manifest.declaresPerformance()) return nullptr;
  const auto iterator = mouths_.find(shape);
  if (iterator != mouths_.end())
    return iterator->second.has_value() ? &*iterator->second : nullptr;
  std::optional<PixelSurface> decoded;
  const auto path = characterMouthAssetPath(*package_, shape);
  if (!path.empty()) {
    auto loaded = PixelSurface::loadPpm(path);
    if (loaded) {
      auto surface = std::move(loaded.value());
      if (package_->manifest.mouthOverlayPlacement().has_value())
        static_cast<void>(keyCornerColor(surface));
      decoded = std::move(surface);
    }
  }
  auto [slot, inserted] = mouths_.emplace(shape, std::move(decoded));
  static_cast<void>(inserted);
  return slot->second.has_value() ? &*slot->second : nullptr;
}

std::optional<character::MouthPlacement> CharacterSurface::mouthPlacement() const noexcept {
  if (!package_.has_value()) return std::nullopt;
  return package_->manifest.mouthOverlayPlacement();
}

}  // namespace seam::native_ui::design
