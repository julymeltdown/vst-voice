#include "seam/native_ui/design/sing_shell.hpp"

#include "seam/native_ui/diagnostic_presentation.hpp"
#include "seam/native_ui/render_status_panel.hpp"
#include "seam/native_ui/voice_identity.hpp"
#include "seam/ui/expression_lane.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace seam::native_ui::design {
namespace {

using paint::Canvas2D;
using paint::FontRole;
using paint::LinearGradient;
using paint::Path;
using paint::RadialGradient;
using paint::StrokeStyle;
using paint::TextAlign;
using paint::TextStyle;

constexpr double kPi = std::numbers::pi;
constexpr Color kWhite{255, 255, 255, 255};
constexpr Color kBlack{0, 0, 0, 255};

std::string format(const char* pattern, double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), pattern, value);
  return buffer;
}

bool contains(ui::Rect r, ui::Point p) noexcept {
  return p.x >= r.x && p.y >= r.y && p.x < r.right() && p.y < r.bottom();
}

bool intersects(ui::Rect a, ui::Rect b) noexcept {
  return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

TextStyle style(FontRole role, double size, double tracking = 0.0,
                TextAlign align = TextAlign::Left, bool upper = false) {
  return TextStyle{role, size, tracking, align, upper};
}

// Fits a label to a width by first tightening tracking, then stepping the size down to the 10pt
// floor. Anything still too long is ellipsized by the canvas, never drawn past its box.
TextStyle fitted(Canvas2D& c, std::string_view text, TextStyle s, double width) {
  if (width <= 0.0 || c.measure(text, s) <= width) return s;
  s.tracking = std::min(s.tracking, 0.4);
  while (s.size > 10.0 && c.measure(text, s) > width) s.size = std::max(10.0, s.size - 0.5);
  return s;
}

void glassPanel(Canvas2D& c, const DesignTokens& t, ui::Rect r, double radius,
                double alpha = 0.90) {
  if (r.width <= 0.0 || r.height <= 0.0) return;
  const auto p = Path::roundedRect(r, radius);
  c.save();
  c.setAlpha(alpha);
  c.fill(p, LinearGradient{{r.x, r.y}, {r.x, r.bottom()},
                           {{0.0, t.color.surfaceRaised}, {1.0, t.color.surface}}});
  c.restore();
  c.stroke(p, withAlpha(t.color.border, 0.95), StrokeStyle{1.0});
  Path highlight;
  highlight.moveTo({r.x + radius, r.y + 1.0}).lineTo({r.right() - radius, r.y + 1.0});
  c.stroke(highlight, withAlpha(kWhite, t.light.highlightAlpha * 1.6), StrokeStyle{1.0});
}

void sunken(Canvas2D& c, const DesignTokens& t, ui::Rect r, double radius) {
  const auto p = Path::roundedRect(r, radius);
  c.fill(p, LinearGradient{{r.x, r.y}, {r.x, r.bottom()},
                           {{0.0, t.color.surfaceSunken}, {1.0, t.color.surface}}});
  c.stroke(p, withAlpha(t.color.border, 0.9), StrokeStyle{1.0});
}

void cardHeader(Canvas2D& c, const DesignTokens& t, ui::Rect card, std::string_view title,
                bool lit) {
  c.save();
  if (lit) c.setGlow(t.color.accent, 7.0);
  c.fill(Path::circle({card.x + 20.0, card.y + 20.0}, 3.5),
         lit ? t.color.accent : t.color.textDisabled);
  c.restore();
  c.text({card.x + 32.0, card.y + 10.0, card.width - 120.0, 20.0}, title,
         style(FontRole::UiSemibold, t.type.panelTitle, t.type.panelTitleTracking,
               TextAlign::Left, true),
         t.color.textPrimary);
  Path rule;
  rule.moveTo({card.x + 16.0, card.y + 38.0}).lineTo({card.right() - 16.0, card.y + 38.0});
  if (t.mode == DesignMode::Emo) {
    c.stroke(rule, withAlpha(t.color.textPrimary, 0.16), StrokeStyle{1.0, true, {5.0, 4.0}});
  } else {
    c.stroke(rule,
             LinearGradient{{card.x, 0.0}, {card.right(), 0.0},
                            {{0.0, withAlpha(t.color.accent, 0.55)},
                             {1.0, withAlpha(t.color.accentCurve, 0.35)}}},
             StrokeStyle{1.0});
  }
}

// Original 24-point icon glyphs.
enum class Icon { Sing, Voice, Tune, Mix, Export, Gear, Play, Stop };

void icon(Canvas2D& c, Icon kind, ui::Point center, double size, Color color) {
  const auto s = size / 24.0;
  const auto P = [&](double x, double y) {
    return ui::Point{center.x + (x - 12.0) * s, center.y + (y - 12.0) * s};
  };
  const StrokeStyle line{1.75 * s};
  switch (kind) {
    case Icon::Sing: {
      c.stroke(Path::capsule({P(9, 3).x, P(9, 3).y, 6 * s, 11 * s}), color, line);
      Path cup;
      cup.moveTo(P(6, 11)).cubicTo(P(6, 19), P(18, 19), P(18, 11));
      c.stroke(cup, color, line);
      Path stem;
      stem.moveTo(P(12, 17.5)).lineTo(P(12, 21)).moveTo(P(8.5, 21)).lineTo(P(15.5, 21));
      c.stroke(stem, color, line);
      break;
    }
    case Icon::Voice: {
      Path wave;
      wave.moveTo(P(2, 12)).lineTo(P(5, 12)).lineTo(P(7, 7)).lineTo(P(9.5, 17)).lineTo(P(12, 4))
          .lineTo(P(14.5, 20)).lineTo(P(17, 8)).lineTo(P(19, 14)).lineTo(P(22, 12));
      c.stroke(wave, color, line);
      break;
    }
    case Icon::Tune: {
      Path rails;
      for (const double x : {6.0, 12.0, 18.0}) rails.moveTo(P(x, 4)).lineTo(P(x, 20));
      c.stroke(rails, withAlpha(color, color.alpha / 255.0 * 0.55), line);
      for (const auto [x, y] : {std::pair{6.0, 14.0}, {12.0, 8.0}, {18.0, 16.0}})
        c.fill(Path::circle(P(x, y), 2.6 * s), color);
      break;
    }
    case Icon::Mix: {
      Path bars;
      for (const auto [x, top] : {std::pair{5.0, 8.0}, {10.0, 4.0}, {15.0, 10.0}, {20.0, 6.0}})
        bars.moveTo(P(x, 20)).lineTo(P(x, top));
      c.stroke(bars, color, line);
      break;
    }
    case Icon::Export: {
      Path tray;
      tray.moveTo(P(4, 14)).lineTo(P(4, 20)).lineTo(P(20, 20)).lineTo(P(20, 14));
      tray.moveTo(P(12, 15)).lineTo(P(12, 4)).moveTo(P(8, 8)).lineTo(P(12, 4)).lineTo(P(16, 8));
      c.stroke(tray, color, line);
      break;
    }
    case Icon::Gear: {
      c.stroke(Path::circle(center, 4.2 * s), color, line);
      Path teeth;
      for (int i = 0; i < 8; ++i) {
        const auto a = i * kPi / 4.0;
        teeth.moveTo({center.x + std::cos(a) * 7.0 * s, center.y + std::sin(a) * 7.0 * s})
            .lineTo({center.x + std::cos(a) * 10.0 * s, center.y + std::sin(a) * 10.0 * s});
      }
      c.stroke(teeth, color, StrokeStyle{2.4 * s});
      break;
    }
    case Icon::Play: {
      Path tri;
      tri.moveTo(P(8, 5)).lineTo(P(19, 12)).lineTo(P(8, 19)).close();
      c.fill(tri, color);
      break;
    }
    case Icon::Stop:
      c.fill(Path::roundedRect({P(7, 7).x, P(7, 7).y, 10 * s, 10 * s}, 1.5 * s), color);
      break;
  }
}

struct KnobModel final {
  ui::ExpressionChannelDescriptor descriptor;
  std::string label;
  double value{0.0};
  std::string refusal;
  std::size_t storedPoints{0U};
};

std::array<KnobModel, 6U> knobModels(const EditorSceneState& state) {
  std::array<KnobModel, 6U> knobs;
  static constexpr std::array<const char*, 6U> kLabels{"Formant", "Breath", "Tension",
                                                       "Air", "Gender", "Growl"};
  for (std::size_t i = 0U; i < knobs.size(); ++i) {
    const auto channel = ui::expressionChannelAt(i);
    knobs[i].descriptor = ui::describeExpressionChannel(channel);
    knobs[i].label = kLabels[i];
    knobs[i].value = knobs[i].descriptor.neutral;
    knobs[i].refusal = state.inspector.valid ? "" : "No vocal track is selected";
    for (const auto& row : state.inspector.expressionCapabilities) {
      if (row.channel != channel) continue;
      knobs[i].value = row.valueAtPlayhead;
      knobs[i].refusal = row.refusal;
      knobs[i].storedPoints = row.storedPoints;
    }
    // The knob still reads the region's edge value, but an edit "at the playhead" is refused.
    if (knobs[i].refusal.empty() && !state.playheadInsideRegion)
      knobs[i].refusal = "The playhead is outside the selected region";
  }
  return knobs;
}

// A persisted value in the unit a musician reads: semitones stay semitones, shares become percent.
std::pair<std::string, std::string> displayValue(const KnobModel& knob) {
  if (knob.descriptor.unit == "semitones") return {format("%.1f", knob.value), "ST"};
  return {format("%.1f", knob.value * 100.0), "%"};
}

std::string lowercase(std::string_view text) {
  std::string out{text};
  for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return out;
}

std::string barsBeatsTicks(time::Tick tick, std::int64_t ppq, const time::MeterEvent& meter) {
  const auto safePpq = std::max<std::int64_t>(1, ppq);
  const auto beat = std::max<std::int64_t>(1, safePpq * 4 / std::max<int>(1, meter.denominator));
  const auto bar = beat * std::max<int>(1, meter.numerator);
  const auto value = std::max<std::int64_t>(0, tick.value());
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%03lld:%lld:%03lld",
                static_cast<long long>(value / bar + 1),
                static_cast<long long>((value % bar) / beat + 1),
                static_cast<long long>(value % beat));
  return buffer;
}

}  // namespace

void SingShell::activate(const std::filesystem::path& assetRoot) {
  auto preferences = loadDesignPreferences();
  if (const char* forced = std::getenv("SEAM_UI_DESIGN"); forced != nullptr) {
    const std::string value{forced};
    if (value == "classic") preferences.shellEnabled = false;
    else {
      preferences.shellEnabled = true;
      preferences.mode = parseDesignMode(value, preferences.mode);
    }
  }
  // Captures only: open a workspace other than SING. The workspace is never a saved preference.
  if (const char* workspace = std::getenv("SEAM_UI_WORKSPACE");
      workspace != nullptr && std::string_view{workspace} == "export")
    workspace_ = Workspace::Export;
  activate(assetRoot, preferences);
  persist_ = true;
}

void SingShell::activate(const std::filesystem::path& assetRoot, DesignPreferences preferences) {
  preferences_ = preferences;
  active_ = true;
  persist_ = false;
  backgroundValid_ = false;
  if (assetRoot.empty() || !available()) return;
  for (const auto mode : {DesignMode::Emo, DesignMode::Scene}) {
    const auto folder = assetRoot / std::string{designModeName(mode)};
    auto& slot = assets_[mode == DesignMode::Scene ? 1U : 0U];
    slot.portrait = paint::loadImage(folder / "portrait.png");
    slot.stage = paint::loadImage(folder / "stage.png");
    slot.wordmark = paint::loadImage(folder / "wordmark.png");
  }
}

std::filesystem::path locateDesignAssets(const std::filesystem::path& bundleResources) {
  std::vector<std::filesystem::path> candidates;
  if (const char* root = std::getenv("SEAM_UI_ASSETS"); root != nullptr && *root != '\0')
    candidates.emplace_back(root);
  if (!bundleResources.empty()) candidates.push_back(bundleResources / "ui-design");
  if (const auto own = paint::codeBundleResources(); !own.empty())
    candidates.push_back(own / "ui-design");
#if defined(SEAM_UI_DESIGN_SOURCE_ASSETS)
  candidates.emplace_back(SEAM_UI_DESIGN_SOURCE_ASSETS);
#endif
  for (const auto& candidate : candidates) {
    std::error_code error;
    if (std::filesystem::is_regular_file(candidate / "manifest.json", error)) return candidate;
  }
  return {};
}

bool SingShell::legacySurfaceRequired(const EditorSceneState& state) noexcept {
  return state.voicebankBrowserVisible || state.audioSettings.visible ||
         state.recoverySupport.visible || state.replacementReview.visible ||
         state.timeMapVisible || state.timeMapInputActive || state.hintInputActive ||
         state.sampleMicroscope.has_value() || state.phonemeReview.visible;
}

void SingShell::setMode(DesignMode mode, bool persist) {
  preferences_.mode = mode;
  backgroundValid_ = false;
  if (persist && persist_) saveDesignPreferences(preferences_);
  repaint();
}

void SingShell::setEnabled(bool enabled, bool persist) {
  preferences_.shellEnabled = enabled;
  forwarding_ = ForwardArea::None;
  knobDrag_.reset();
  if (persist && persist_) saveDesignPreferences(preferences_);
  repaint();
}

void SingShell::setEnabled(NativeEditorController& controller, bool enabled) {
  cancelGestures(controller);
  if (!enabled) releaseSurface(controller);
  setEnabled(enabled);
}

bool SingShell::assetsLoaded(DesignMode mode) const noexcept {
  const auto& a = assets_[mode == DesignMode::Scene ? 1U : 0U];
  return a.portrait && a.stage && a.wordmark;
}

const ModeAssets& SingShell::assets() const noexcept {
  return assets_[preferences_.mode == DesignMode::Scene ? 1U : 0U];
}

ui::Rect SingShell::fromLegacy(ui::Rect rect) const noexcept {
  rect.y += layout_.grid.y - legacyContentTop_;
  return rect;
}

ui::Point SingShell::toLegacy(ui::Point point) const noexcept {
  return ui::Point{point.x, point.y - layout_.grid.y + legacyContentTop_};
}

TextInputRequest SingShell::translateTextInput(TextInputRequest request) {
  // The controller states which geometry the bounds came from; only note-grid anchors move. Any
  // other request has already replaced a lyric composition, so the lyric flag must not survive to
  // cancel the new field when its classic surface takes the frame.
  if (request.anchor != TextInputAnchor::NoteGrid) {
    lyricInputActive_ = false;
    return request;
  }
  if (!presented_) return request;
  lyricInputActive_ = true;
  request.logicalBounds = fromLegacy(request.logicalBounds);
  return request;
}

bool SingShell::inMusicalArea(ui::Point point) const noexcept {
  return workspace_ == Workspace::Sing &&
         (contains(layout_.grid, point) || contains(layout_.ruler, point));
}

bool SingShell::inEditableLane(ui::Point point) const noexcept {
  return workspace_ == Workspace::Sing && laneEditable_ && contains(layout_.laneTimePlot, point);
}

NativeEditorController::HostedGeometry SingShell::hostedGeometry() const noexcept {
  return {.pianoBottom = legacyContentTop_ + layout_.grid.height,
          .laneHeight = layout_.laneTimePlot.height};
}

PointerEvent SingShell::translated(const PointerEvent& event, ForwardArea area) const noexcept {
  auto copy = event;
  if (area == ForwardArea::Lane) {
    // The hosted lane starts at the hosted piano bottom, so lane y maps 1:1 onto it.
    copy.position.y = event.position.y - layout_.laneTimePlot.y + hostedGeometry().pianoBottom;
    return copy;
  }
  copy.position = toLegacy(event.position);
  // The shell ruler is shorter than the legacy ruler; never let a ruler point reach the legacy
  // toolbar row above it.
  const auto legacyRulerTop = legacyContentTop_ - EditorSceneLayout{}.rulerHeight;
  if (contains(layout_.ruler, event.position))
    copy.position.y = std::max(copy.position.y, legacyRulerTop + 1.0);
  return copy;
}

void SingShell::applyGeometry(NativeEditorController& controller) {
  auto& model = controller.pianoRoll();
  const ui::PianoRollViewport viewport{
      .bounds = ui::Rect{0.0, 0.0, layout_.grid.right(), layout_.grid.height},
      .keyboardWidth = layout_.grid.x,
  };
  const auto& current = model.viewport();
  if (current.bounds.x != viewport.bounds.x || current.bounds.y != viewport.bounds.y ||
      current.bounds.width != viewport.bounds.width ||
      current.bounds.height != viewport.bounds.height ||
      current.keyboardWidth != viewport.keyboardWidth) {
    model.setViewport(viewport);
    model.rebuildIndex();
  }
  controller.setHostedGrid(hostedGeometry());
}

void SingShell::cancelGestures(NativeEditorController& controller) {
  const auto hadGesture = knobDrag_.has_value() || forwarding_ != ForwardArea::None;
  knobDrag_.reset();
  forwarding_ = ForwardArea::None;
  scrollAccumulator_ = 0.0;
  controller.cancelPointerGesture();
  if (hadGesture) repaint();
}

void SingShell::releaseSurface(NativeEditorController& controller) {
  if (presented_ || controller.hostedGrid().has_value()) {
    cancelGestures(controller);
    // A lyric field anchored in shell space would be misplaced on the classic surface.
    if (lyricInputActive_) controller.cancelTextComposition();
    lyricInputActive_ = false;
  }
  // The classic surface owns keyboard focus from here on.
  semanticFocus_.clear();
  presented_ = false;
  controller.setHostedGrid(std::nullopt);
}

void SingShell::yieldIfModal(NativeEditorController& controller) {
  if (presented_ && controller.legacyModalSurfaceActive()) {
    releaseSurface(controller);
    repaint();
  }
}

bool SingShell::prepareFrame(NativeEditorController& controller, double logicalWidth,
                             double logicalHeight) {
  if (!enabled() || !available() || controller.legacyModalSurfaceActive()) {
    releaseSurface(controller);
    return false;
  }
  const auto next = solveSingLayout(logicalWidth, logicalHeight);
  const auto moved = next.grid.x != layout_.grid.x || next.grid.y != layout_.grid.y ||
                     next.grid.width != layout_.grid.width ||
                     next.grid.height != layout_.grid.height ||
                     next.laneTimePlot.y != layout_.laneTimePlot.y ||
                     next.laneTimePlot.height != layout_.laneTimePlot.height;
  if (moved || !presented_) {
    // A gesture's coordinate transform is frozen at its start; a geometry change cancels it, and
    // an open lyric field would be anchored to the old geometry.
    if (knobDrag_ || forwarding_ != ForwardArea::None) cancelGestures(controller);
    if (lyricInputActive_) {
      controller.cancelTextComposition();
      lyricInputActive_ = false;
    }
  }
  layout_ = next;
  applyGeometry(controller);
  presented_ = true;
  return true;
}

void SingShell::ensureBackground(const RasterCanvas& canvas, const DesignTokens& tokens) {
  const auto& surface = const_cast<RasterCanvas&>(canvas).surface();
  if (backgroundValid_ && background_.width() == surface.width() &&
      background_.height() == surface.height() && backgroundScale_ == canvas.scale() &&
      backgroundMode_ == preferences_.mode)
    return;
  backgroundValid_ = false;
  if (!background_.resize(surface.width(), surface.height())) return;
  background_.clear(tokens.color.canvas);
  auto c = paint::makeCanvas(background_, canvas.scale());
  if (!c) return;
  paintBackground(*c, tokens);
  c->flush();
  backgroundScale_ = canvas.scale();
  backgroundMode_ = preferences_.mode;
  backgroundValid_ = true;
}

void SingShell::paintBackground(Canvas2D& c, const DesignTokens& t) const {
  const auto W = c.width();
  const auto H = c.height();
  c.fill(Path::rect({0.0, 0.0, W, H}), t.color.canvas);
  std::mt19937 rng{t.mode == DesignMode::Emo ? 0x5EA1u : 0x5CE7u};
  const auto uniform = [&](double lo, double hi) {
    return std::uniform_real_distribution<double>{lo, hi}(rng);
  };
  const auto textureAlpha = t.light.textureAlpha;
  if (t.mode == DesignMode::Emo) {
    // Stage light from the upper left and a low red bloom behind the singer rack.
    c.fill(Path::rect({0, 0, W, H}),
           RadialGradient{{W * 0.18, H * 0.05}, W * 0.55,
                          {{0.0, withAlpha(t.color.accentDeep, 0.35)}, {1.0, withAlpha(kBlack, 0.0)}}});
    c.fill(Path::rect({0, 0, W, H}),
           RadialGradient{{W * 0.86, H * 0.42}, W * 0.40,
                          {{0.0, withAlpha(t.color.accent, 0.18)}, {1.0, withAlpha(kBlack, 0.0)}}});
    // Seeded ink threads: original procedural texture.
    if (textureAlpha > 0.0) {
      for (int i = 0; i < 170; ++i) {
        Path strand;
        const ui::Point a{uniform(-0.1, 1.1) * W, uniform(-0.1, 1.1) * H};
        const ui::Point d{uniform(-0.1, 1.1) * W, uniform(-0.1, 1.1) * H};
        strand.moveTo(a).cubicTo({uniform(0, W), uniform(0, H)}, {uniform(0, W), uniform(0, H)}, d);
        const auto red = uniform(0.0, 1.0) < 0.7;
        c.stroke(strand,
                 withAlpha(red ? t.color.texturePrimary : t.color.textureSecondary,
                           uniform(0.25, 1.0) * textureAlpha * (red ? 1.9 : 0.8)),
                 StrokeStyle{uniform(0.5, 1.4)});
      }
    }
  } else {
    c.fill(Path::rect({0, 0, W, H}),
           RadialGradient{{W * 0.12, H * 0.1}, W * 0.50,
                          {{0.0, withAlpha(t.color.accentAlt1, 0.32)}, {1.0, withAlpha(kBlack, 0.0)}}});
    c.fill(Path::rect({0, 0, W, H}),
           RadialGradient{{W * 0.92, H * 0.85}, W * 0.45,
                          {{0.0, withAlpha(t.color.accent, 0.22)}, {1.0, withAlpha(kBlack, 0.0)}}});
    if (textureAlpha > 0.0) {
      const std::array<Color, 4U> sparkle{t.color.accent, t.color.accentCurve, t.color.accentAlt1,
                                          kWhite};
      for (int i = 0; i < 1400; ++i) {
        const ui::Point p{uniform(0, W), uniform(0, H)};
        const auto color = withAlpha(sparkle[static_cast<std::size_t>(uniform(0, 3.999))],
                                     uniform(0.15, 1.0) * textureAlpha * 4.0);
        if (uniform(0.0, 1.0) < 0.82) {
          c.fill(Path::circle(p, uniform(0.35, 1.1)), color);
        } else {
          const auto r = uniform(2.0, 4.5);
          Path star;
          star.moveTo({p.x, p.y - r}).lineTo({p.x + r * 0.22, p.y - r * 0.22})
              .lineTo({p.x + r, p.y}).lineTo({p.x + r * 0.22, p.y + r * 0.22})
              .lineTo({p.x, p.y + r}).lineTo({p.x - r * 0.22, p.y + r * 0.22})
              .lineTo({p.x - r, p.y}).lineTo({p.x - r * 0.22, p.y - r * 0.22}).close();
          c.fill(star, color);
        }
      }
    }
  }

  const auto& l = layout_;
  glassPanel(c, t, l.header, t.shape.hero);
  glassPanel(c, t, l.editor, t.shape.card);
  glassPanel(c, t, l.lane, t.shape.card);
  if (l.rack == RackPresentation::Full) {
    glassPanel(c, t, l.singer, t.shape.card);
    glassPanel(c, t, l.expression, t.shape.card);
    glassPanel(c, t, l.style, t.shape.card);
    if (t.mode == DesignMode::Scene) {
      // An iridescent edge marks the singer card, the one place the character lives.
      c.save();
      c.setGlow(withAlpha(t.color.accent, 0.6), 8.0);
      c.stroke(Path::roundedRect(l.singer, t.shape.card),
               LinearGradient{{l.singer.x, l.singer.y}, {l.singer.right(), l.singer.bottom()},
                              {{0.0, t.color.accent}, {0.5, t.color.accentAlt1},
                               {1.0, t.color.accentCurve}}},
               StrokeStyle{1.4});
      c.restore();
    }
    // Signal rail: the singing chain from singer to expression to style.
    const auto x = l.rackArea.x - 8.0;
    Path rail;
    rail.moveTo({x, l.singer.y + 20.0}).lineTo({x, l.style.y + 20.0});
    c.stroke(rail, withAlpha(t.color.accent, 0.35),
             StrokeStyle{1.5, true, t.mode == DesignMode::Emo ? std::vector<double>{4.0, 3.0}
                                                              : std::vector<double>{}});
  } else {
    glassPanel(c, t, l.rackArea, t.shape.card);
  }
  glassPanel(c, t, l.status, 10.0, 0.80);
  sunken(c, t, l.transport, 10.0);

  if (t.mode == DesignMode::Emo) {
    // A torn paper seam under the header.
    Path torn;
    std::mt19937 tear{0x7EA2u};
    torn.moveTo({l.header.x + 14.0, l.header.bottom() - 3.0});
    for (double x = l.header.x + 14.0; x < l.header.right() - 14.0; x += 5.0)
      torn.lineTo({x, l.header.bottom() - 3.0 +
                          std::uniform_real_distribution<double>{-1.6, 1.6}(tear)});
    c.stroke(torn, withAlpha(t.color.textPrimary, 0.55), StrokeStyle{1.1});
  } else {
    // A checker strip along the top of the ruler.
    for (double x = l.ruler.x, i = 0; x < l.ruler.right(); x += 5.0, ++i)
      c.fill(Path::rect({x, l.ruler.y + (static_cast<int>(i) % 2 == 0 ? 0.0 : 3.0), 5.0, 3.0}),
             withAlpha(t.color.textPrimary, 0.16));
  }

  if (const auto& wordmark = assets().wordmark; wordmark) {
    const auto aspect = static_cast<double>(wordmark->width()) / wordmark->height();
    const auto height = std::min(l.wordmark.height, l.wordmark.width / aspect);
    c.drawImage(*wordmark, {l.wordmark.x, l.wordmark.y + (l.wordmark.height - height) * 0.5,
                            height * aspect, height});
  } else {
    c.text(l.wordmark, "SEAM", style(FontRole::Display, 34.0, 6.0), t.color.textPrimary);
  }
}

bool SingShell::paint(RasterCanvas& canvas, NativeEditorController& controller,
                      const EditorSceneState& state, time::Tick playhead) {
  if (!presented_ || legacySurfaceRequired(state) ||
      layout_.width != std::max(canvas.logicalWidth(), 480.0) ||
      layout_.height != std::max(canvas.logicalHeight(), 320.0)) {
    // Either prepareFrame did not run for this canvas or the state needs a classic surface.
    if (!presented_ || legacySurfaceRequired(state) ||
        !prepareFrame(controller, canvas.logicalWidth(), canvas.logicalHeight())) {
      releaseSurface(controller);
      return false;
    }
  }
  auto& model = controller.pianoRoll();
  laneEditable_ = state.expressionLabelVisible() && state.expression.refusal.empty();
  const auto& t = tokensFor(preferences_.mode, preferences_.contrast);
  ppq_ = time::Tick{model.timeline().ppq()}.value();
  ensureBackground(canvas, t);
  auto& surface = canvas.surface();
  if (backgroundValid_) {
    std::copy(background_.pixels().begin(), background_.pixels().end(), surface.pixels().begin());
  } else {
    surface.clear(t.color.canvas);
  }
  auto c = paint::makeCanvas(surface, canvas.scale());
  if (!c) {
    releaseSurface(controller);
    return false;
  }
  const auto knobs = knobModels(state);
  for (std::size_t i = 0U; i < knobs.size(); ++i) knobRefused_[i] = !knobs[i].refusal.empty();
  paintHeader(*c, t, state, playhead);
  exportRunning_ = exportBusy(controller);
  waveform_ = hostActions_.regionWaveform
                  ? hostActions_.regionWaveform()
                  : RegionWaveform{nullptr, "No waveform",
                                   "This host does not give the editor its region's audio."};
  if (waveform_.shown() && waveform_.view->key.region != model.regionId())
    waveform_ = RegionWaveform{nullptr, "Other region",
                               "The rendered audio belongs to a different region than the one shown."};
  if (workspace_ == Workspace::Export) {
    paintExport(*c, t, state);
  } else {
    paintEditor(*c, t, model, state);
    paintLane(*c, t, model, state);
  }
  paintRack(*c, t, state);
  paintStatus(*c, t, state);
  if (workspace_ == Workspace::Sing && state.focusedElementBounds.has_value()) {
    const auto focus = fromLegacy(*state.focusedElementBounds);
    if (intersects(focus, layout_.grid)) {
      c->save();
      c->setGlow(t.color.focusRing, 6.0);
      c->stroke(Path::roundedRect({focus.x - 2, focus.y - 2, focus.width + 4, focus.height + 4}, 5),
                t.color.focusRing, StrokeStyle{2.0});
      c->restore();
    }
  }
  c->flush();
  return true;
}

void SingShell::paintHeader(Canvas2D& c, const DesignTokens& t, const EditorSceneState& state,
                            time::Tick playhead) const {
  const auto& l = layout_;
  // Workspace tabs. SING and EXPORT are workspaces, VOICE opens the voice browser; TUNE and MIX are
  // not built yet and are drawn disabled.
  static constexpr std::array<Icon, 5U> kIcons{Icon::Sing, Icon::Voice, Icon::Tune, Icon::Mix,
                                               Icon::Export};
  static constexpr std::array<const char*, 5U> kNames{"Sing", "Voice", "Tune", "Mix", "Export"};
  for (std::size_t i = 0U; i < l.workspaceTab.size(); ++i) {
    const auto tab = l.workspaceTab[i];
    if (tab.width < 24.0) continue;
    const auto active = (i == 0U && workspace_ == Workspace::Sing) ||
                        (i == 4U && workspace_ == Workspace::Export);
    const auto enabled = i == 0U || i == 1U || i == 4U;
    const auto color = active    ? t.color.accent
                       : enabled ? t.color.textSecondary
                                 : withAlpha(t.color.textSecondary, 0.35);
    const auto iconCenter = ui::Point{tab.x + tab.width * 0.5,
                                      tab.y + (l.workspaceLabelsVisible ? tab.height * 0.36
                                                                        : tab.height * 0.5)};
    c.save();
    if (active) c.setGlow(t.color.accent, 8.0);
    icon(c, kIcons[i], iconCenter, 22.0, color);
    c.restore();
    if (l.workspaceLabelsVisible)
      c.text({tab.x, tab.y + tab.height * 0.62, tab.width, 16.0}, kNames[i],
             style(FontRole::UiSemibold, t.type.smallLabel, t.type.labelTracking, TextAlign::Center,
                   true),
             active ? t.color.accent
                    : withAlpha(t.color.textSecondary, enabled ? 0.9 : 0.4));
    if (active) {
      c.save();
      c.setGlow(t.color.accent, 8.0);
      c.fill(Path::capsule({tab.x + 18.0, tab.bottom() - 3.0, tab.width - 36.0, 2.5}),
             t.color.accent);
      c.restore();
    }
  }

  // Mode switch.
  const auto sw = l.modeSwitch;
  c.fill(Path::capsule(sw), withAlpha(t.color.surfaceSunken, 0.9));
  c.stroke(Path::capsule(sw), t.color.border, StrokeStyle{1.0});
  const auto half = sw.width * 0.5;
  const auto activeRect = preferences_.mode == DesignMode::Emo
                              ? ui::Rect{sw.x + 2, sw.y + 2, half - 2, sw.height - 4}
                              : ui::Rect{sw.x + half, sw.y + 2, half - 2, sw.height - 4};
  c.save();
  c.setGlow(withAlpha(t.color.accent, 0.8), 8.0);
  c.fill(Path::capsule(activeRect), t.color.accent);
  c.restore();
  const auto labelStyle = style(FontRole::UiBold, t.type.smallLabel, 1.6, TextAlign::Center, true);
  c.text({sw.x, sw.y, half, sw.height}, "Emo", labelStyle,
         preferences_.mode == DesignMode::Emo ? t.color.textOnAccent : t.color.textSecondary);
  c.text({sw.x + half, sw.y, half, sw.height}, "Scene", labelStyle,
         preferences_.mode == DesignMode::Scene ? t.color.textOnAccent : t.color.textSecondary);

  // Transport.
  const auto canPlay = state.renderStatus.hasAudibleAudio;
  c.save();
  if (state.playing) c.setGlow(t.color.accentTime, 10.0);
  c.fill(Path::circle({l.playButton.x + 16.0, l.playButton.y + 16.0}, 15.0),
         state.playing ? withAlpha(t.color.accentTime, 0.18) : withAlpha(t.color.accent, 0.14));
  c.restore();
  icon(c, state.playing ? Icon::Stop : Icon::Play,
       {l.playButton.x + 16.0 + (state.playing ? 0.0 : 1.0), l.playButton.y + 16.0}, 18.0,
       canPlay ? (state.playing ? t.color.accentTime : t.color.accent) : t.color.textDisabled);
  const auto position = barsBeatsTicks(playhead, ppq_, state.meter);
  auto readout = style(FontRole::Mono, t.type.transport, 0.5);
  // A narrow header shrinks the digits instead of truncating the time.
  // The divider sits 6pt inside the tempo cell's left edge; keep the digits clear of it.
  const auto positionWidth = l.positionReadout.width - 12.0;
  if (const auto needed = c.measure("888:8:888", readout); needed > positionWidth)
    readout = style(FontRole::Mono, std::max(12.0, t.type.transport * positionWidth / needed), 0.5);
  c.text(l.positionReadout, "888:8:888", readout, withAlpha(t.color.accentTime, 0.07));
  c.save();
  c.setGlow(withAlpha(t.color.accentTime, 0.75), 7.0);
  c.text(l.positionReadout, position, readout, t.color.accentTime);
  c.restore();
  const auto divider = [&](double x) {
    Path p;
    p.moveTo({x, l.transport.y + 9.0}).lineTo({x, l.transport.bottom() - 9.0});
    c.stroke(p, t.color.border, StrokeStyle{1.0});
  };
  divider(l.tempoReadout.x - 6.0);
  divider(l.meterReadout.x - 6.0);
  const auto small = style(FontRole::Mono, 14.0, 0.4, TextAlign::Center);
  const auto tempoColor = t.mode == DesignMode::Scene ? t.color.accentTime : t.color.textPrimary;
  const auto tempoText = format("%.0f BPM", state.tempoBpm);
  const auto meterText =
      std::to_string(state.meter.numerator) + "/" + std::to_string(state.meter.denominator);
  c.text(l.tempoReadout, tempoText, fitted(c, tempoText, small, l.tempoReadout.width - 8.0),
         tempoColor);
  c.text(l.meterReadout, meterText, fitted(c, meterText, small, l.meterReadout.width - 6.0),
         tempoColor);

  // Output meter: no measured output level is published to the editor yet, so the meter shows
  // its empty scale instead of an invented level.
  if (l.outputMeterVisible) {
    const auto m = l.outputMeter;
    c.text({m.x, m.y, 36.0, m.height}, "Out",
           style(FontRole::UiSemibold, t.type.smallLabel, 1.2, TextAlign::Left, true),
           t.color.textSecondary);
    for (int row = 0; row < 2; ++row)
      for (int i = 0; i < 20; ++i)
        c.fill(Path::roundedRect({m.x + 40.0 + i * 6.0, m.y + 12.0 + row * 12.0, 4.0, 8.0}, 1.0),
               withAlpha(i < 14 ? t.color.meterLow : (i < 18 ? t.color.meterMid : t.color.meterHigh),
                         0.22));
  }
  icon(c, Icon::Gear, {l.settings.x + 16.0, l.settings.y + 16.0}, 22.0, t.color.textSecondary);
}

std::size_t noteWaveformColumns(ui::Rect note, double visibleLeft, double visibleRight,
                                const std::function<void(double, double)>& column) {
  // Columns stay on the note's own 2pt phase (anchored at its left edge) so scrolling does not
  // shimmer, but only the part inside the visible span is walked: a long note at high zoom costs
  // what is on screen, not its full width.
  constexpr double kColumn = kNoteWaveformColumn;
  const auto left = std::max(note.x, visibleLeft);
  const auto right = std::min(note.right(), visibleRight);
  if (right <= left || note.width <= 0.0) return 0U;
  auto x = note.x + std::floor((left - note.x) / kColumn) * kColumn;
  std::size_t count = 0U;
  for (; x < right; x += kColumn) {
    column(x, std::min(note.right(), x + kColumn));
    ++count;
  }
  return count;
}

namespace {

// The note's slice of its region's rendered audio, as signed min/max columns inside the capsule.
// Ticks map to frames through the project tempo map and the audio's absolute origin frame, so a
// region placed later in the song and a tempo change draw the audio under the right note. Only
// columns inside `visible` (the grid) are computed.
void paintNoteWaveform(Canvas2D& c, const DesignTokens& t, double radius, ui::Rect b,
                       ui::Rect visible, const RegionEnvelopeView& view,
                       const time::TempoMap& tempo, time::Tick start, time::Tick end,
                       bool selected) {
  const auto& envelope = *view.envelope;
  const auto rate = static_cast<double>(view.key.sampleRate);
  if (rate <= 0.0 || envelope.peak() < 1e-4F || b.width < 4.0 || b.height < 6.0 || end <= start)
    return;
  const auto amplitude = b.height * 0.5 - 1.5;
  const auto scale = amplitude / static_cast<double>(envelope.peak());
  const auto mid = b.y + b.height * 0.5;
  const auto duration = static_cast<double>((end - start).value());
  const auto frameAt = [&](double x) {
    const auto fraction = std::clamp((x - b.x) / b.width, 0.0, 1.0);
    const time::Tick tick{start.value() + static_cast<std::int64_t>(std::llround(fraction * duration))};
    return tempo.sampleFrameAt(tick, rate) - view.key.originFrame;
  };
  // One filled outline per contiguous run of columns: the maximum edge left to right, then the
  // minimum edge back. That is a few hundred edges and no caps for a full grid, where a stroke per
  // column made the anti-aliasing rasterizer sort thousands of cap curves every frame.
  struct Column final {
    double x, top, bottom;
  };
  std::vector<Column> run;
  run.reserve(static_cast<std::size_t>(visible.width / kNoteWaveformColumn) + 4U);
  Path outline;
  bool any = false;
  const auto flush = [&] {
    if (run.empty()) return;
    const auto half = kNoteWaveformColumn * 0.5;
    outline.moveTo({run.front().x - half, run.front().top});
    for (const auto& column : run) outline.lineTo({column.x, column.top});
    outline.lineTo({run.back().x + half, run.back().top});
    outline.lineTo({run.back().x + half, run.back().bottom});
    for (auto it = run.rbegin(); it != run.rend(); ++it) outline.lineTo({it->x, it->bottom});
    outline.lineTo({run.front().x - half, run.front().bottom});
    outline.close();
    run.clear();
    any = true;
  };
  static_cast<void>(noteWaveformColumns(b, visible.x, visible.right(), [&](double x0, double x1) {
    const auto first = frameAt(x0);
    const auto last = std::max(first + 1, frameAt(x1));
    const auto pair = envelope.range(first, last);
    if (!pair.has_value()) {
      flush();
      return;
    }
    const auto top = mid - static_cast<double>(pair->maximum) * scale;
    const auto bottom = mid - static_cast<double>(pair->minimum) * scale;
    run.push_back({(x0 + x1) * 0.5, std::min(top, mid - 0.6), std::max(bottom, mid + 0.6)});
  }));
  flush();
  if (!any) return;
  // Clip to the capsule as far as it is visible. A note many screens wide at high zoom would make
  // the rasterizer build coverage for its whole outline; cutting it just outside the visible span
  // keeps its real rounded ends wherever they are on screen, and the cut edges off screen.
  const auto margin = radius + 2.0;
  const auto left = std::max(b.x, visible.x - margin);
  const auto right = std::min(b.right(), visible.right() + margin);
  if (right <= left) return;
  c.save();
  c.clipPath(Path::roundedRect({left, b.y, right - left, b.height}, radius));
  c.fill(outline, withAlpha(t.color.waveInNote, selected ? 0.9 : 0.7));
  c.restore();
}

}  // namespace

void SingShell::paintEditor(Canvas2D& c, const DesignTokens& t, ui::PianoRollModel& model,
                            const EditorSceneState& state) const {
  const auto& l = layout_;
  // Tool strip: the real track and project, and the way back to the classic editor.
  const auto chip = l.trackLabel;
  c.fill(Path::roundedRect(chip, 6.0), withAlpha(t.color.surfaceSunken, 0.85));
  c.stroke(Path::roundedRect(chip, 6.0), t.color.border, StrokeStyle{1.0});
  const auto trackName = state.inspector.valid && !state.inspector.name.empty()
                             ? state.inspector.name
                             : std::string{"No track"};
  c.text({chip.x + 10, chip.y, chip.width - 20, chip.height}, trackName,
         style(FontRole::UiSemibold, t.type.label, 0.6, TextAlign::Left, true), t.color.textPrimary);
  const auto project = (state.projectName.empty() ? std::string{"Untitled"} : state.projectName) +
                       (state.dirty ? "  \u2022 edited" : "");
  c.text({chip.right() + 14.0, chip.y, l.gridLabel.x - chip.right() - 24.0, chip.height}, project,
         style(FontRole::Ui, t.type.label), t.color.textSecondary);
  c.fill(Path::roundedRect(l.classicToggle, 6.0), withAlpha(t.color.surfaceSunken, 0.85));
  c.stroke(Path::roundedRect(l.classicToggle, 6.0), t.color.border, StrokeStyle{1.0});
  c.text(l.classicToggle, "Classic", style(FontRole::UiSemibold, t.type.smallLabel, 1.0,
                                           TextAlign::Center, true),
         t.color.textSecondary);
  // Whether the notes carry the current render's waveform, and if not, the short reason (the full
  // reason is published to accessibility).
  if (l.gridLabel.width > 0.0 && l.gridLabel.x > chip.right() + 24.0 && !waveform_.caption.empty()) {
    const auto captionStyle = style(FontRole::UiSemibold, t.type.smallLabel, 0.6, TextAlign::Right, true);
    c.text(l.gridLabel, waveform_.caption,
           fitted(c, waveform_.caption, captionStyle, l.gridLabel.width),
           waveform_.shown() ? t.color.waveInNote : t.color.textSecondary);
  }

  // Ruler and grid lines share one tick-to-x transform with the lane below.
  const auto& timeline = model.timeline();
  const auto quarter = time::Tick{timeline.ppq()};
  const auto visibleStart = timeline.pixelToTick(0.0);
  const auto visibleEnd = timeline.pixelToTick(l.grid.width);
  const auto quartersPerBar = std::max<std::int64_t>(
      1, static_cast<std::int64_t>(state.meter.numerator) * 4 /
             std::max<std::int64_t>(1, state.meter.denominator));
  c.save();
  c.clipRect(l.grid);
  c.fill(Path::rect(l.grid), withAlpha(t.color.canvas, 0.55));
  const auto& pitch = model.pitch();
  for (auto midi = pitch.topMidiKey(); midi >= 0; --midi) {
    const auto y = l.grid.y + pitch.midiToPixel(midi);
    if (y > l.grid.bottom()) break;
    const auto black = midi % 12 == 1 || midi % 12 == 3 || midi % 12 == 6 || midi % 12 == 8 ||
                       midi % 12 == 10;
    if (black) c.fill(Path::rect({l.grid.x, y, l.grid.width, pitch.rowHeight()}),
                      withAlpha(kBlack, 0.22));
    Path row;
    row.moveTo({l.grid.x, y}).lineTo({l.grid.right(), y});
    c.stroke(row, midi % 12 == 0 ? t.color.gridStrong : t.color.gridWeak,
             StrokeStyle{midi % 12 == 0 ? 1.0 : 0.6});
  }
  c.restore();
  if (quarter.value() > 0) {
    auto tick = time::Tick{(visibleStart.value() / quarter.value()) * quarter.value()};
    if (tick < visibleStart) tick += quarter;
    auto index = tick.value() / quarter.value();
    for (; tick <= visibleEnd; tick += quarter, ++index) {
      const auto x = l.grid.x + timeline.tickToPixel(tick);
      if (x < l.grid.x || x > l.grid.right()) continue;
      const auto bar = index % quartersPerBar == 0;
      Path v;
      v.moveTo({x, bar ? l.ruler.y + 4.0 : l.grid.y}).lineTo({x, l.grid.bottom()});
      c.stroke(v, bar ? t.color.gridBar : t.color.gridStrong, StrokeStyle{bar ? 1.0 : 0.7});
      if (bar)
        c.text({x + 5.0, l.ruler.y + 3.0, 60.0, 20.0}, std::to_string(index / quartersPerBar + 1),
               style(FontRole::Mono, t.type.rulerMicro + 1.0), t.color.textSecondary);
    }
  }

  // Keyboard.
  c.save();
  c.clipPath(Path::roundedRect(l.keyboard, 4.0));
  for (auto midi = pitch.topMidiKey(); midi >= 0; --midi) {
    const auto y = l.keyboard.y + pitch.midiToPixel(midi);
    if (y > l.keyboard.bottom()) break;
    const auto black = midi % 12 == 1 || midi % 12 == 3 || midi % 12 == 6 || midi % 12 == 8 ||
                       midi % 12 == 10;
    // Every row is white underneath: black keys are short keys laid over the white ones, and only
    // the B|C and E|F boundaries draw a full-width seam, as on a real keyboard.
    const auto key = ui::Rect{l.keyboard.x, y, l.keyboard.width, pitch.rowHeight()};
    c.fill(Path::rect(key), t.color.keyWhite);
    const auto degree = midi % 12;
    if (!black && (degree == 11 || degree == 4)) {
      Path edge;
      edge.moveTo({key.x, y}).lineTo({key.right(), y});
      c.stroke(edge, withAlpha(kBlack, 0.28), StrokeStyle{0.8});
    }
  }
  for (auto midi = pitch.topMidiKey(); midi >= 0; --midi) {
    const auto y = l.keyboard.y + pitch.midiToPixel(midi);
    if (y > l.keyboard.bottom()) break;
    const auto black = midi % 12 == 1 || midi % 12 == 3 || midi % 12 == 6 || midi % 12 == 8 ||
                       midi % 12 == 10;
    if (black)
      c.fill(Path::roundedRect({l.keyboard.x, y + 1.0, l.keyboard.width * 0.6, pitch.rowHeight() - 2.0},
                               2.0),
             t.color.keyBlack);
    if (midi % 12 == 0)
      c.text({l.keyboard.x, y, l.keyboard.width - 4.0, pitch.rowHeight()},
             "C" + std::to_string(midi / 12 - 1), style(FontRole::UiSemibold, 10.0, 0.0, TextAlign::Right),
             t.color.keyLabel);
  }
  c.restore();

  const auto notes = model.visibleNotes();
  // The singer stands behind the notes and fades back whenever notes share her space.
  if (const auto& stage = assets().stage; stage && l.grid.width > 520.0) {
    const auto height = l.grid.height * 0.94;
    const auto width = height * stage->width() / stage->height();
    const ui::Rect figure{l.grid.right() - width - 36.0, l.grid.bottom() - height, width, height};
    bool crowded = false;
    for (const auto& note : notes) {
      auto bounds = note.bounds;
      bounds.y += l.grid.y;
      crowded = crowded || intersects(bounds, figure);
    }
    c.save();
    c.clipRect(l.grid);
    c.drawImage(*stage, figure, crowded ? 0.06 : 0.16);
    c.setBlend(paint::Blend::Screen);
    c.drawImage(*stage, figure, crowded ? 0.05 : 0.14);
    c.restore();
  }

  const auto* region = model.project().findRegion(model.regionId());
  c.save();
  c.clipRect(l.grid);
  // Score pitch line: note targets with short glides between adjacent notes, broken at rests.
  {
    std::vector<ui::Rect> ordered;
    for (const auto& note : notes) {
      if (note.hiddenByOverlapDensity || note.overlapMemberCount > 1U) continue;
      auto bounds = note.bounds;
      bounds.y += l.grid.y;
      ordered.push_back(bounds);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
    Path line;
    bool open = false;
    for (std::size_t i = 0U; i < ordered.size(); ++i) {
      const auto& n = ordered[i];
      const auto y = n.y + n.height * 0.5;
      if (!open) {
        line.moveTo({n.x, y});
        open = true;
      }
      const auto next = i + 1U < ordered.size() ? &ordered[i + 1U] : nullptr;
      if (next != nullptr && next->x - n.right() <= 12.0 && next->x >= n.right() - 2.0) {
        const auto glide = std::min({12.0, n.width * 0.3, next->width * 0.3});
        const auto ny = next->y + next->height * 0.5;
        line.lineTo({n.right() - glide, y});
        line.cubicTo({n.right(), y}, {next->x, ny}, {next->x + glide, ny});
      } else {
        line.lineTo({n.right(), y});
        open = false;
      }
    }
    c.save();
    c.setGlow(withAlpha(t.color.pitchGlow, 0.9), 7.0);
    c.stroke(line, withAlpha(t.color.pitchCurve, 0.85), StrokeStyle{1.8});
    c.restore();
  }

  // Notes as capsules.
  std::vector<ui::Rect> capsules;
  for (const auto& note : notes) {
    if (note.hiddenByOverlapDensity) continue;
    auto b = note.bounds;
    b.y += l.grid.y + 1.5;
    b.height = std::max(3.0, b.height - 3.0);
    b.width = std::max(2.0, b.width);
    capsules.push_back(b);
    const auto radius = std::min(t.shape.note, b.height * 0.5);
    const auto p = Path::roundedRect(b, radius);
    const auto hovered = state.hoveredNote == note.noteId || state.focusedNote == note.noteId;
    // The note's own rendered audio sits inside the capsule, under its outline.
    const auto wave = [&] {
      if (!waveform_.shown() || region == nullptr) return;
      const auto* source = region->findNote(note.noteId);
      if (source == nullptr) return;
      paintNoteWaveform(c, t, radius, b, l.grid, *waveform_.view, model.project().tempoMap(),
                        region->startTick + source->startTick, region->startTick + source->endTick(),
                        note.selected);
    };
    if (note.selected) {
      c.save();
      c.setGlow(withAlpha(t.color.noteSelectedA, 0.85), 10.0);
      c.fill(p, LinearGradient{{b.x, b.y}, {b.right(), b.bottom()},
                               {{0.0, t.color.noteSelectedA}, {1.0, t.color.noteSelectedB}}});
      c.restore();
      wave();
      c.stroke(p, t.color.noteSelectedStroke, StrokeStyle{1.2});
    } else {
      c.fill(p, LinearGradient{{b.x, b.y}, {b.x, b.bottom()},
                               {{0.0, note.midiKey % 2U == 0U ? t.color.noteFillAlt : t.color.noteFill},
                                {1.0, t.color.surfaceSunken}}});
      wave();
      c.save();
      c.setGlow(withAlpha(t.color.noteStroke, 0.55), 5.0);
      c.stroke(p, withAlpha(t.color.noteStroke, hovered ? 1.0 : 0.8), StrokeStyle{hovered ? 1.6 : 1.1});
      c.restore();
    }
  }

  // Vibrato handles on a single selected note keep their existing hit geometry.
  if (state.selectedNoteCount == 1U && region != nullptr) {
    for (const auto& note : notes) {
      if (!note.selected || note.hiddenByOverlapDensity) continue;
      const auto* source = region->findNote(note.noteId);
      if (source == nullptr) continue;
      auto display = *source;
      if (state.vibratoGesturePreview && state.vibratoGesturePreview->noteId == source->id)
        display.vibrato = state.vibratoGesturePreview->value;
      auto bounds = note.bounds;
      bounds.y += l.grid.y;
      const auto handles = vibratoHandlePositions(display, region->startTick,
                                                  model.project().tempoMap(), bounds);
      if (!handles) continue;
      const auto dot = [&](std::optional<ui::Point> p, double r) {
        if (!p) return;
        c.fill(Path::circle(*p, r), t.color.focusRing);
        c.stroke(Path::circle(*p, r), t.color.canvas, StrokeStyle{1.0});
      };
      dot(handles->onset, 4.0);
      dot(handles->depth, 4.0);
      dot(handles->fadeIn, 3.0);
      dot(handles->fadeOut, 3.0);
      dot(handles->period, 3.5);
      dot(handles->phase, 3.0);
    }
  }

  // Lyric labels: allocated in priority order into free slots so that labels never overlap each
  // other or another note, and never leave the grid.
  {
    struct Candidate final {
      const ui::NoteVisual* note;
      ui::Rect bounds;
      int priority;
    };
    std::vector<Candidate> candidates;
    for (const auto& note : notes) {
      if (note.hiddenByOverlapDensity || note.lyric.empty()) continue;
      auto b = note.bounds;
      b.y += l.grid.y;
      const auto priority = note.selected ? 0 : (state.hoveredNote == note.noteId ? 1 : 2);
      candidates.push_back({&note, b, priority});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
      return a.priority != b.priority ? a.priority < b.priority : a.bounds.x < b.bounds.x;
    });
    std::vector<ui::Rect> placed;
    const auto lyricStyle = style(FontRole::UiSemibold, t.type.lyric);
    for (const auto& candidate : candidates) {
      const auto width = c.measure(candidate.note->lyric, lyricStyle) + 8.0;
      const auto b = candidate.bounds;
      const std::array<ui::Rect, 2U> slots{
          ui::Rect{b.x - 2.0, b.y - 19.0, width, 18.0},
          ui::Rect{b.x + 4.0, b.y, width, b.height}};
      for (std::size_t s = 0U; s < slots.size(); ++s) {
        const auto slot = slots[s];
        const auto inside = s == 1U;
        if (inside && (b.height < 15.0 || width > b.width - 6.0)) continue;
        if (!inside && (slot.y < l.grid.y || slot.right() > l.grid.right())) continue;
        bool free = std::none_of(placed.begin(), placed.end(),
                                 [&](const ui::Rect& other) { return intersects(slot, other); });
        if (!inside) {
          free = free && std::none_of(capsules.begin(), capsules.end(), [&](const ui::Rect& other) {
                   return intersects(slot, other);
                 });
        }
        if (!free) continue;
        placed.push_back(slot);
        const auto color = candidate.note->selected && inside ? t.color.textOnAccent
                                                              : t.color.noteText;
        c.text(slot, candidate.note->lyric, inside ? style(FontRole::UiSemibold, t.type.label)
                                                   : lyricStyle,
               t.mode == DesignMode::Scene && candidate.note->selected && inside ? kWhite : color);
        break;
      }
    }
  }

  // Overlap badges.
  for (const auto& note : notes) {
    if (!note.drawsOverlapIndicator) continue;
    auto b = note.bounds;
    b.y += l.grid.y;
    const ui::Rect badge{std::min(b.right() + 3.0, l.grid.right() - 30.0), b.y - 2.0, 28.0, 18.0};
    c.fill(Path::capsule(badge), t.color.surfaceRaised);
    c.stroke(Path::capsule(badge), t.color.focusRing, StrokeStyle{1.0});
    c.text(badge, "x" + std::to_string(note.overlapMemberCount),
           style(FontRole::UiBold, t.type.smallLabel, 0.0, TextAlign::Center), t.color.focusRing);
  }
  c.restore();

  offscreenHint_.reset();
  const auto total = model.noteCount();
  if (notes.empty() && total > 0U) {
    // Notes exist but are scrolled out of this grid: point at them instead of claiming none exist.
    // Count in all four directions; horizontal-only panning has notes left or right, not above.
    std::array<std::size_t, 4U> away{};  // above, below, earlier (left), later (right)
    for (const auto& note : model.allNotes()) {
      if (note.bounds.bottom() <= 0.0) ++away[0];
      else if (note.bounds.y >= l.grid.height) ++away[1];
      else if (note.bounds.right() <= l.grid.x) ++away[2];
      else if (note.bounds.x >= l.grid.right()) ++away[3];
    }
    const auto direction = static_cast<std::size_t>(
        std::max_element(away.begin(), away.end()) - away.begin());
    static constexpr std::array<const char*, 4U> kArrow{"↑ ", "↓ ", "← ", "→ "};
    static constexpr std::array<const char*, 4U> kWhere{" notes above", " notes below", " notes earlier",
                                                        " notes later"};
    const auto count = away[direction] > 0U ? away[direction] : total;
    const auto hint = std::string{kArrow[direction]} + std::to_string(count) + kWhere[direction];
    const auto hintStyle = style(FontRole::UiSemibold, t.type.smallLabel, 0.6, TextAlign::Center, true);
    const auto chipWidth = c.measure(hint, hintStyle) + 28.0;
    const auto centerY = l.grid.y + (l.grid.height - 22.0) * 0.5;
    const ui::Rect hintChip = direction == 0U ? ui::Rect{l.grid.x + (l.grid.width - chipWidth) * 0.5, l.grid.y + 8.0, chipWidth, 22.0}
                              : direction == 1U ? ui::Rect{l.grid.x + (l.grid.width - chipWidth) * 0.5, l.grid.bottom() - 30.0, chipWidth, 22.0}
                              : direction == 2U ? ui::Rect{l.grid.x + 8.0, centerY, chipWidth, 22.0}
                                                : ui::Rect{l.grid.right() - 8.0 - chipWidth, centerY, chipWidth, 22.0};
    c.fill(Path::capsule(hintChip), withAlpha(t.color.surfaceRaised, 0.92));
    c.stroke(Path::capsule(hintChip), withAlpha(t.color.accent, 0.8), StrokeStyle{1.0});
    c.text(hintChip, hint, hintStyle, t.color.accent);
    offscreenHint_ = direction;
  } else if (notes.empty()) {
    const auto cx = l.grid.x + l.grid.width * 0.40;
    const auto cy = l.grid.y + l.grid.height * 0.42;
    c.text({cx - 160.0, cy - 22.0, 320.0, 24.0}, "No notes yet",
           style(FontRole::UiSemibold, 18.0, 0.4, TextAlign::Center), t.color.textPrimary);
    c.text({cx - 200.0, cy + 6.0, 400.0, 20.0}, "Double-click the grid to add a note",
           style(FontRole::Ui, t.type.body, 0.0, TextAlign::Center), t.color.textSecondary);
  }

  if (state.boxSelection.has_value()) {
    const auto box = fromLegacy(*state.boxSelection);
    c.fill(Path::rect(box), t.color.selectionFill);
    c.stroke(Path::rect(box), t.color.accent, StrokeStyle{1.0});
  }
  if (state.playheadPixel >= 0.0) {
    const auto x = l.grid.x + state.playheadPixel;
    if (x >= l.grid.x && x <= l.grid.right()) {
      c.save();
      c.setGlow(t.color.accentTime, 8.0);
      Path head;
      head.moveTo({x, l.ruler.y + 6.0}).lineTo({x, l.grid.bottom()});
      c.stroke(head, t.color.accentTime, StrokeStyle{1.4});
      Path marker;
      marker.moveTo({x - 6.0, l.ruler.y + 2.0}).lineTo({x + 6.0, l.ruler.y + 2.0})
          .lineTo({x, l.ruler.y + 12.0}).close();
      c.fill(marker, t.color.accentTime);
      c.restore();
    }
  }
  if (state.lyricEditor.has_value() && !state.timeMapInputActive) {
    const auto editor = fromLegacy(*state.lyricEditor);
    c.fill(Path::roundedRect(editor, 6.0), t.color.surfaceSunken);
    c.save();
    c.setGlow(t.color.accent, 8.0);
    c.stroke(Path::roundedRect(editor, 6.0), t.color.accent, StrokeStyle{1.5});
    c.restore();
    if (!state.compositionPreview.empty())
      c.text({editor.x + 8.0, editor.y, editor.width - 16.0, editor.height},
             state.compositionPreview, style(FontRole::Ui, t.type.lyric), t.color.textPrimary);
  }
}

void SingShell::paintLane(Canvas2D& c, const DesignTokens& t, const ui::PianoRollModel& model,
                          const EditorSceneState& state) const {
  const auto& l = layout_;
  static constexpr std::array<const char*, 7U> kTabs{"Dynamics", "Formant", "Breath", "Tension",
                                                     "Air", "Gender", "Growl"};
  const auto open = state.expressionLabelVisible();
  const auto selected = open ? ui::expressionChannelIndex(state.expression.channel) + 1U : 99U;
  const auto tabWidth = std::min(104.0, l.laneTabs.width / 9.0);
  for (std::size_t i = 0U; i < kTabs.size(); ++i) {
    const ui::Rect tab{l.laneTabs.x + i * (tabWidth + 4.0), l.laneTabs.y, tabWidth, l.laneTabs.height};
    const auto active = i == selected;
    if (active) {
      c.save();
      c.setGlow(withAlpha(t.color.accent, 0.7), 8.0);
      c.fill(Path::roundedRect(tab, 6.0), withAlpha(t.color.accent, 0.20));
      c.restore();
      c.stroke(Path::roundedRect(tab, 6.0), t.color.accent, StrokeStyle{1.0});
    } else {
      c.stroke(Path::roundedRect(tab, 6.0), withAlpha(t.color.border, 0.9), StrokeStyle{1.0});
    }
    c.text(tab, kTabs[i],
           fitted(c, kTabs[i],
                  style(FontRole::UiSemibold, t.type.smallLabel, 1.0, TextAlign::Center, true),
                  tab.width - 8.0),
           active ? t.color.accent : t.color.textSecondary);
  }
  const ui::Rect info{l.laneTabs.x + kTabs.size() * (tabWidth + 4.0) + 8.0, l.laneTabs.y,
                      l.laneTabs.right() - (l.laneTabs.x + kTabs.size() * (tabWidth + 4.0) + 8.0),
                      l.laneTabs.height};
  const auto plot = l.laneTimePlot;
  sunken(c, t, l.lanePlot, 6.0);
  if (!open) {
    c.text(info, "Select a channel to draw its curve",
           style(FontRole::Ui, t.type.smallLabel, 0.0, TextAlign::Right), t.color.textSecondary);
    return;
  }
  const auto& e = state.expression;
  const auto descriptor = ui::describeExpressionChannel(e.channel);
  const auto scale = descriptor.unit == "semitones" ? 1.0 : 100.0;
  const auto unit = descriptor.unit == "semitones" ? " st" : " %";
  std::string infoText = format("%.1f", e.valueAtPlayhead * scale) + unit + " at playhead";
  if (e.draftChanged) infoText += "  \u2022 unsaved draft";
  c.text(info, e.refusal.empty() ? infoText : e.refusal,
         style(FontRole::Ui, t.type.smallLabel, 0.0, TextAlign::Right),
         e.refusal.empty() ? t.color.textSecondary : t.color.warning);
  // The same value-to-y mapping the controller uses to hit-test and edit this lane, applied to the
  // hosted lane rectangle, so a drawn point is exactly where a click edits it.
  const EditorSceneLayout legacyLayout{};
  const auto centerY = plot.y + plot.height * legacyLayout.automationCenterFraction;
  const auto span = std::max(std::abs(static_cast<double>(e.maximum - e.neutral)),
                             std::abs(static_cast<double>(e.neutral - e.minimum)));
  const auto halfScale = plot.height * NativeEditorController::HostedGeometry::kExpressionVerticalScale * 0.5;
  const auto yFor = [&](double v) {
    const auto clamped = std::clamp(v, static_cast<double>(e.minimum), static_cast<double>(e.maximum));
    return span <= 0.0 ? centerY : centerY - (clamped - e.neutral) / span * halfScale;
  };
  const auto gutter = ui::Rect{l.lanePlot.x, l.lanePlot.y, plot.x - l.lanePlot.x - 2.0, plot.height};
  const auto gutterStyle = style(FontRole::Mono, t.type.rulerMicro, 0.0, TextAlign::Right);
  c.text({gutter.x, plot.y, gutter.width, 14.0}, format("%.0f", e.maximum * scale), gutterStyle,
         t.color.textSecondary);
  c.text({gutter.x, plot.bottom() - 14.0, gutter.width, 14.0}, format("%.0f", e.minimum * scale),
         gutterStyle, t.color.textSecondary);
  Path neutral;
  neutral.moveTo({plot.x, yFor(e.neutral)}).lineTo({plot.right(), yFor(e.neutral)});
  c.stroke(neutral, withAlpha(t.color.textSecondary, 0.35), StrokeStyle{1.0, true, {3.0, 4.0}});
  c.save();
  c.clipRect(plot);
  if (e.points.empty()) {
    c.text({plot.x + 12.0, plot.y + 4.0, plot.width - 24.0, 18.0},
           "No curve stored for this channel", style(FontRole::Ui, t.type.smallLabel),
           t.color.textSecondary);
  } else {
    const auto& timeline = model.timeline();
    // The curve belongs to its region: it holds its end values only across the region's span.
    const auto regionLeft = std::max(plot.x, plot.x + timeline.tickToPixel(state.automationOriginTick));
    const auto regionRight = std::min(
        plot.right(),
        plot.x + timeline.tickToPixel(state.automationOriginTick + state.automationRegionDuration));
    Path curve;
    Path area;
    const auto first = e.points.front();
    const auto firstX = plot.x + timeline.tickToPixel(state.automationOriginTick + first.tick);
    curve.moveTo({regionLeft, yFor(first.amount)}).lineTo({firstX, yFor(first.amount)});
    area.moveTo({regionLeft, plot.bottom()}).lineTo({regionLeft, yFor(first.amount)})
        .lineTo({firstX, yFor(first.amount)});
    for (std::size_t i = 1U; i < e.points.size(); ++i) {
      const ui::Point p{plot.x + timeline.tickToPixel(state.automationOriginTick + e.points[i].tick), yFor(e.points[i].amount)};
      curve.lineTo(p);
      area.lineTo(p);
    }
    const auto lastY = yFor(e.points.back().amount);
    curve.lineTo({regionRight, lastY});
    area.lineTo({regionRight, lastY}).lineTo({regionRight, plot.bottom()}).close();
    c.fill(area, LinearGradient{{0.0, plot.y}, {0.0, plot.bottom()},
                                {{0.0, withAlpha(t.color.laneFillTop, 0.42)},
                                 {1.0, withAlpha(t.color.laneFillBottom, 0.05)}}});
    c.save();
    c.setGlow(withAlpha(t.color.laneFillTop, 0.9), 8.0);
    c.stroke(curve, t.color.laneFillTop, StrokeStyle{2.0});
    c.restore();
    for (const auto& point : e.points) {
      const ui::Point p{plot.x + timeline.tickToPixel(state.automationOriginTick + point.tick), yFor(point.amount)};
      c.fill(Path::circle(p, 3.2), t.color.textPrimary);
    }
  }
  if (state.playheadPixel >= 0.0) {
    Path head;
    head.moveTo({plot.x + state.playheadPixel, plot.y}).lineTo({plot.x + state.playheadPixel, plot.bottom()});
    c.stroke(head, withAlpha(t.color.accentTime, 0.7), StrokeStyle{1.0});
  }
  c.restore();
}

void SingShell::paintRack(Canvas2D& c, const DesignTokens& t, const EditorSceneState& state) const {
  const auto& l = layout_;
  const auto& identity = state.voiceIdentity;
  const auto voiceReady = identity.state == VoiceIdentityState::Ready ||
                          identity.state == VoiceIdentityState::Complete ||
                          identity.state == VoiceIdentityState::Rendering;
  if (l.rack != RackPresentation::Full) {
    const auto ring = l.portraitRing;
    const ui::Point center{ring.x + ring.width * 0.5, ring.y + ring.height * 0.5};
    const auto inner = ring.width * 0.5 - 3.0;
    c.fill(Path::circle(center, inner), t.color.surfaceSunken);
    if (const auto& portrait = assets().portrait; portrait) {
      c.save();
      c.clipPath(Path::circle(center, inner));
      c.drawImage(*portrait, {center.x - inner, center.y - inner, inner * 2.0, inner * 2.0},
                  voiceReady ? 1.0 : 0.55);
      c.restore();
    }
    const auto railColor = identity.state == VoiceIdentityState::Missing ||
                                   identity.state == VoiceIdentityState::Error
                               ? t.color.error
                               : identity.state == VoiceIdentityState::Warning ? t.color.warning
                                                                               : t.color.accent;
    c.save();
    c.setGlow(withAlpha(railColor, 0.9), 6.0);
    c.stroke(Path::circle(center, inner + 1.0), railColor, StrokeStyle{1.6});
    c.restore();
    c.fill(Path::circle({ring.right() - 4.0, ring.bottom() - 4.0}, 4.5), railColor);
    return;
  }
  cardHeader(c, t, l.singer, "Singer", voiceReady);
  // Voice state chip.
  const std::string stateName{voiceIdentityStateName(identity.state)};
  const auto stateColor = identity.state == VoiceIdentityState::Missing ||
                                  identity.state == VoiceIdentityState::Error
                              ? t.color.error
                              : identity.state == VoiceIdentityState::Warning ? t.color.warning
                                                                              : t.color.success;
  const auto chipWidth = c.measure(stateName, style(FontRole::UiBold, 10.5, 1.2)) + 20.0;
  const ui::Rect chip{l.singer.right() - 16.0 - chipWidth, l.singer.y + 10.0, chipWidth, 20.0};
  c.fill(Path::capsule(chip), withAlpha(stateColor, 0.16));
  c.stroke(Path::capsule(chip), withAlpha(stateColor, 0.8), StrokeStyle{1.0});
  c.text(chip, stateName, style(FontRole::UiBold, 10.5, 1.2, TextAlign::Center, true), stateColor);

  // Portrait inside the ring meter.
  const auto ring = l.portraitRing;
  const ui::Point center{ring.x + ring.width * 0.5, ring.y + ring.height * 0.5};
  const auto radius = ring.width * 0.5;
  const auto inner = radius - 18.0;
  c.fill(Path::circle(center, inner), RadialGradient{center, inner,
                                                     {{0.0, t.color.surfaceRaised}, {1.0, t.color.surfaceSunken}}});
  if (const auto& portrait = assets().portrait; portrait) {
    c.save();
    c.clipPath(Path::circle(center, inner));
    c.drawImage(*portrait, {center.x - inner, center.y - inner, inner * 2.0, inner * 2.0},
                voiceReady ? 1.0 : 0.55);
    c.restore();
  }
  // Ring: lit segments follow the real render progress or the singer's measured energy.
  double lit = 0.0;
  if (state.characterPerformance && state.characterPerformance->performing)
    lit = std::clamp(static_cast<double>(state.characterPerformance->energy), 0.0, 1.0);
  else if (state.renderStatus.state == RenderStatusState::Rendering)
    lit = std::clamp(state.renderStatus.fraction, 0.0, 1.0);
  else if (state.renderStatus.state == RenderStatusState::Ready && state.renderStatus.hasAudibleAudio)
    lit = 1.0;
  constexpr int kTicks = 72;
  for (int i = 0; i < kTicks; ++i) {
    const auto a = -kPi / 2.0 + i * 2.0 * kPi / kTicks;
    const auto on = static_cast<double>(i) < lit * kTicks;
    Color color = t.mode == DesignMode::Scene ? t.trackColors[static_cast<std::size_t>(i / 3) % 4U]
                                              : t.color.accent;
    color = withAlpha(color, on ? 1.0 : 0.16);
    Path tick;
    tick.moveTo({center.x + std::cos(a) * (radius - 12.0), center.y + std::sin(a) * (radius - 12.0)})
        .lineTo({center.x + std::cos(a) * (radius - 2.0), center.y + std::sin(a) * (radius - 2.0)});
    c.save();
    if (on) c.setGlow(withAlpha(color, 0.9), 5.0);
    c.stroke(tick, color, StrokeStyle{t.mode == DesignMode::Scene ? 3.2 : 1.6, false});
    c.restore();
  }
  c.save();
  c.setGlow(withAlpha(t.color.accent, 0.9), 10.0);
  c.stroke(Path::circle(center, inner + 1.0), withAlpha(t.color.accent, 0.85), StrokeStyle{1.6});
  c.restore();

  // Footer: the real voice identity and the way to change it.
  const auto footerY = l.singerChange.y;
  const auto name = !identity.name.empty() ? identity.name
                    : !state.characterName.empty() ? state.characterName
                                                   : std::string{"No voice selected"};
  c.text({l.singer.x + 18.0, footerY - 2.0, l.singerChange.x - l.singer.x - 28.0, 16.0}, name,
         style(FontRole::UiSemibold, t.type.label, 0.4), t.color.textPrimary);
  const auto detail = identity.state == VoiceIdentityState::Missing && !identity.recovery.empty()
                          ? identity.recovery
                          : identity.identity;
  c.text({l.singer.x + 18.0, footerY + 13.0, l.singerChange.x - l.singer.x - 28.0, 14.0}, detail,
         style(FontRole::Ui, t.type.smallLabel), t.color.textSecondary);
  c.fill(Path::capsule(l.singerChange), withAlpha(t.color.accent, 0.14));
  c.stroke(Path::capsule(l.singerChange), withAlpha(t.color.accent, 0.8), StrokeStyle{1.0});
  c.text(l.singerChange, "Change voice",
         style(FontRole::UiSemibold, t.type.smallLabel, 0.6, TextAlign::Center, true), t.color.accent);

  // Expression knobs.
  cardHeader(c, t, l.expression, "Expression", state.inspector.valid);
  const auto knobs = knobModels(state);
  const auto selectedChannel = state.expressionLabelVisible()
                                   ? ui::expressionChannelIndex(state.expression.channel)
                                   : 99U;
  for (std::size_t i = 0U; i < knobs.size(); ++i) {
    const auto& k = knobs[i];
    const auto cell = l.knob[i];
    const auto refused = !k.refusal.empty();
    const auto selectedKnob = i == selectedChannel;
    c.text({cell.x, cell.y, cell.width, 16.0}, k.label,
           style(FontRole::UiSemibold, t.type.smallLabel, 1.2, TextAlign::Center, true),
           selectedKnob ? t.color.accent : t.color.textSecondary);
    // Label (16) + knob (52) + value caption (12) leave a clear gap before the next row's label.
    const ui::Point kc{cell.x + cell.width * 0.5, cell.y + 20.0 + 26.0};
    const auto r = 26.0;
    constexpr auto start = 0.75 * kPi;
    constexpr auto sweep = 1.5 * kPi;
    Path track;
    track.arc(kc, r, start, sweep);
    c.stroke(track, t.color.knobTrack, StrokeStyle{3.5});
    auto value = k.value;
    if (knobDrag_ && knobDrag_->index == i)
      value = std::clamp(value + static_cast<double>(knobDrag_->steps) *
                                     static_cast<double>(k.descriptor.step),
                         static_cast<double>(k.descriptor.minimum),
                         static_cast<double>(k.descriptor.maximum));
    const auto span = std::max(1e-6, static_cast<double>(k.descriptor.maximum - k.descriptor.minimum));
    const auto fraction = std::clamp((value - k.descriptor.minimum) / span, 0.0, 1.0);
    const auto origin = k.descriptor.bipolar
                            ? std::clamp((k.descriptor.neutral - k.descriptor.minimum) / span, 0.0, 1.0)
                            : 0.0;
    if (!refused && std::abs(fraction - origin) > 1e-4) {
      Path arc;
      arc.arc(kc, r, start + sweep * origin, sweep * (fraction - origin));
      c.save();
      c.setGlow(withAlpha(t.color.accent, 0.9), 7.0);
      c.stroke(arc, t.mode == DesignMode::Scene
                        ? LinearGradient{{kc.x - r, kc.y}, {kc.x + r, kc.y},
                                         {{0.0, t.color.accent}, {1.0, t.color.accentAlt1}}}
                        : LinearGradient{{kc.x - r, kc.y}, {kc.x + r, kc.y},
                                         {{0.0, t.color.accentDeep}, {1.0, t.color.accent}}},
               StrokeStyle{3.5});
      c.restore();
    }
    c.save();
    c.setAlpha(refused ? 0.4 : 1.0);
    c.fill(Path::circle(kc, r - 7.0),
           RadialGradient{{kc.x - 6.0, kc.y - 8.0}, r, {{0.0, t.color.knobBodyInner}, {1.0, t.color.knobBodyOuter}}});
    c.stroke(Path::circle(kc, r - 7.0), withAlpha(kWhite, 0.08), StrokeStyle{1.0});
    const auto angle = start + sweep * fraction;
    if (t.mode == DesignMode::Emo) {
      Path pointer;
      pointer.moveTo({kc.x + std::cos(angle) * (r - 12.0), kc.y + std::sin(angle) * (r - 12.0)})
          .lineTo({kc.x + std::cos(angle) * (r - 4.0), kc.y + std::sin(angle) * (r - 4.0)});
      c.stroke(pointer, t.color.knobPointer, StrokeStyle{2.0});
    } else {
      c.fill(Path::circle({kc.x + std::cos(angle) * r, kc.y + std::sin(angle) * r}, 3.4),
             t.color.knobPointer);
    }
    c.restore();
    KnobModel shown = k;
    shown.value = value;
    const auto [number, unitText] = displayValue(shown);
    c.text({kc.x - r + 6.0, kc.y - 11.0, (r - 6.0) * 2.0, 22.0}, refused ? "\u2014" : number,
           style(FontRole::UiSemibold, number.size() > 5 ? 13.0 : t.type.knobValue, 0.0,
                 TextAlign::Center),
           refused ? t.color.textDisabled : t.color.textPrimary);
    c.text({cell.x, kc.y + r + 1.0, cell.width, 13.0},
           refused ? std::string{"Unavailable"} : std::string{unitText},
           style(FontRole::UiMedium, 10.0, 1.0, TextAlign::Center, true),
           refused ? withAlpha(t.color.warning, 0.85) : t.color.textDisabled);
    if (k.storedPoints > 0U && !refused)
      c.fill(Path::capsule({kc.x - 7.0, kc.y + r + 15.0, 14.0, 3.0}), t.color.accent);
  }

  // Style presets published by the selected voice.
  cardHeader(c, t, l.style, "Style", false);
  std::vector<std::string> styles;
  for (const auto& card : state.voicebankCards)
    if (card.id == state.inspector.voicebank.id && !card.id.empty()) styles = card.styles;
  if (styles.empty()) {
    c.text({l.style.x + 18.0, l.style.y + 52.0, l.style.width - 36.0, 18.0},
           "The selected voice publishes no style presets",
           style(FontRole::Ui, t.type.smallLabel), t.color.textSecondary);
    return;
  }
  const auto chipW = (l.style.width - 32.0 - 3.0 * 8.0) / 4.0;
  for (std::size_t i = 0U; i < std::min<std::size_t>(styles.size(), 8U); ++i) {
    const ui::Rect chipRect{l.style.x + 16.0 + (i % 4U) * (chipW + 8.0),
                            l.style.y + 46.0 + (i / 4U) * 30.0, chipW, 24.0};
    const auto overflow = i == 7U && styles.size() > 8U;
    c.stroke(Path::capsule(chipRect), withAlpha(t.color.accent, 0.55), StrokeStyle{1.0});
    c.text(chipRect, overflow ? "+" + std::to_string(styles.size() - 7U) : styles[i],
           style(FontRole::UiSemibold, t.type.smallLabel, 0.8, TextAlign::Center, true),
           t.color.textPrimary);
  }
}

void SingShell::paintStatus(Canvas2D& c, const DesignTokens& t, const EditorSceneState& state) const {
  const auto& l = layout_;
  const auto& s = state.renderStatus;
  const auto meter = ui::Rect{l.status.right() - 360.0, l.status.y + 6.0, 200.0, 16.0};
  const auto fraction = s.state == RenderStatusState::Ready ? 1.0 : std::clamp(s.fraction, 0.0, 1.0);
  std::string label{renderStatusStateName(s.state)};
  if (s.state == RenderStatusState::Rendering && s.totalPhrases > 0U)
    label += " " + std::to_string(s.completedPhrases) + "/" + std::to_string(s.totalPhrases);
  if (s.audibleAudioStale) label += " \u2022 audio stale";
  c.text({meter.right() + 12.0, l.status.y, l.status.right() - meter.right() - 24.0, l.status.height},
         label + format("  %.0f%%", fraction * 100.0),
         style(FontRole::UiSemibold, t.type.smallLabel, 1.0, TextAlign::Right, true),
         s.state == RenderStatusState::Failed ? t.color.error : t.color.textSecondary);
  if (t.mode == DesignMode::Emo) {
    // A heartbeat: flat when idle, drawn in blood red up to the rendered fraction.
    Path beat;
    const auto mid = meter.y + meter.height * 0.5;
    beat.moveTo({meter.x, mid});
    const auto spike = meter.x + meter.width * 0.55;
    beat.lineTo({spike - 14.0, mid}).lineTo({spike - 9.0, mid - 3.0}).lineTo({spike - 5.0, mid + 2.0})
        .lineTo({spike - 1.0, mid - 8.0}).lineTo({spike + 3.0, mid + 7.0}).lineTo({spike + 7.0, mid})
        .lineTo({meter.right(), mid});
    c.stroke(beat, withAlpha(t.color.textSecondary, 0.3), StrokeStyle{1.3});
    c.save();
    c.clipRect({meter.x, meter.y - 4.0, meter.width * fraction, meter.height + 8.0});
    c.setGlow(t.color.accent, 6.0);
    c.stroke(beat, s.state == RenderStatusState::Failed ? t.color.error : t.color.accent,
             StrokeStyle{1.6});
    c.restore();
  } else {
    constexpr int kBeads = 24;
    const auto w = meter.width / kBeads;
    for (int i = 0; i < kBeads; ++i) {
      const auto on = static_cast<double>(i) < fraction * kBeads;
      const auto color = t.trackColors[static_cast<std::size_t>(i) % 4U];
      c.save();
      if (on) c.setGlow(withAlpha(color, 0.8), 4.0);
      c.fill(Path::capsule({meter.x + i * w + 1.0, meter.y + 3.0, w - 2.0, meter.height - 6.0}),
             withAlpha(color, on ? 1.0 : 0.15));
      c.restore();
    }
  }
  // Left: audio device and the most important diagnostic.
  const auto online = state.audioDeviceOnline;
  c.fill(Path::circle({l.status.x + 16.0, l.status.y + 14.0}, 3.5),
         online ? t.color.success : t.color.textDisabled);
  std::string left = online ? "Audio " + state.audioBackend : std::string{"Audio offline"};
  auto leftColor = t.color.textSecondary;
  if (!state.diagnostics.empty()) {
    left = presentDiagnostic(state.diagnostics.front()).title;
    leftColor = t.color.warning;
  } else if (!s.diagnostic.empty()) {
    left = s.diagnostic;
    // A render note is status, not a warning, unless the render itself failed.
    leftColor = s.state == RenderStatusState::Failed ? t.color.warning : t.color.textSecondary;
  }
  c.text({l.status.x + 28.0, l.status.y, meter.x - l.status.x - 48.0, l.status.height}, left,
         style(FontRole::Ui, t.type.smallLabel), leftColor);
}

// ---- EXPORT workspace --------------------------------------------------------------------------

namespace {

std::string exportStateLabel(authoring::ExportState state) {
  switch (state) {
    case authoring::ExportState::Preflight: return "Checking";
    case authoring::ExportState::Staging: return "Rendering files";
    case authoring::ExportState::Prepared: return "Prepared";
    case authoring::ExportState::Publishing: return "Publishing";
    case authoring::ExportState::Committed: return "Written";
    case authoring::ExportState::Cancelled: return "Cancelled";
    case authoring::ExportState::Failed: return "Failed";
    case authoring::ExportState::Recovered: return "Recovered";
    case authoring::ExportState::RollbackRequired: return "Needs rollback";
  }
  return "Unknown";
}

std::string channelLayout(std::uint8_t channels) {
  if (channels == 1U) return "Mono";
  if (channels == 2U) return "Stereo";
  return std::to_string(channels) + " channels";
}

std::string sampleRateLabel(std::uint32_t rate) {
  const auto khz = static_cast<double>(rate) / 1000.0;
  return (std::fmod(khz, 1.0) == 0.0 ? format("%.0f", khz) : format("%.1f", khz)) + " kHz";
}

}  // namespace

namespace {

// One arrangement of the EXPORT panel, from which paint, hit-testing and accessibility all read.
// A short panel (the 480x320 minimum, a compact plug-in) folds the plan into one line so the run
// action, its state and any failure stay on screen; a narrow one stacks the status under it.
struct ExportPanelLayout final {
  bool compact{false};
  bool sideColumn{false};
  double columnWidth{0.0};
  ui::Rect summary;
  ui::Rect button;
  ui::Rect note;
  ui::Rect status;
};

ExportPanelLayout exportPanelLayout(ui::Rect area) {
  ExportPanelLayout p;
  // The full panel is chosen only where its status area holds the attempt and the last export
  // (two 38pt rows, 46pt apart); every other size folds into the compact form, whose one-line
  // status rows always keep the failure on screen.
  constexpr double kButtonTop = 64.0 + 4.0 * 46.0 + 12.0;
  constexpr double kStatusRows = 46.0 + 38.0;
  const auto sideColumnFits =
      area.width >= 720.0 && area.height >= std::max(kButtonTop + 44.0 + 44.0, 80.0 + kStatusRows);
  const auto stackedFits = area.height >= kButtonTop + 44.0 + 10.0 + 32.0 + 8.0 + kStatusRows + 24.0;
  p.sideColumn = sideColumnFits;
  p.compact = !sideColumnFits && !stackedFits;
  p.columnWidth = std::max(120.0, (p.sideColumn ? area.width * 0.5 : area.width) - 64.0);
  const auto left = area.x + 32.0;
  const auto buttonWidth = std::min(240.0, std::max(120.0, p.columnWidth));
  if (p.compact) {
    p.summary = {left, area.y + 46.0, area.width - 64.0, 18.0};
    const auto buttonY = std::max(area.y + 44.0, std::min(area.y + 70.0, area.bottom() - 52.0));
    p.button = {left, buttonY, buttonWidth, std::min(44.0, std::max(28.0, area.bottom() - buttonY - 8.0))};
    p.note = {};
    p.status = {left, p.button.bottom() + 8.0, area.width - 64.0,
                std::max(0.0, area.bottom() - p.button.bottom() - 16.0)};
    return p;
  }
  p.button = {left, area.y + 64.0 + 4.0 * 46.0 + 12.0, buttonWidth, 44.0};
  p.note = {left, p.button.bottom() + 10.0, p.columnWidth, 32.0};
  p.status = p.sideColumn
                 ? ui::Rect{area.x + area.width * 0.5 + 16.0, area.y + 64.0, p.columnWidth,
                            area.bottom() - area.y - 80.0}
                 : ui::Rect{left, p.note.bottom() + 8.0, area.width - 64.0,
                            std::max(0.0, area.bottom() - p.note.bottom() - 24.0)};
  return p;
}

std::string exportPlanSummary(const std::optional<ShellExportPlan>& plan,
                              const std::string& unavailable) {
  if (!plan) return unavailable;
  std::string summary = plan->master ? "Master " + channelLayout(plan->channels) + ", " +
                                           sampleRateLabel(plan->sampleRate) + ", " + plan->format
                                     : std::string{"No master"};
  summary += plan->stems ? "; one stem per track" : "; no stems";
  summary += "; SHA-256 receipt";
  return summary;
}

bool attemptEnded(authoring::ExportState state) noexcept {
  return state == authoring::ExportState::Failed || state == authoring::ExportState::Cancelled ||
         state == authoring::ExportState::RollbackRequired;
}

}  // namespace

ui::Rect SingShell::exportArea() const noexcept {
  const auto& l = layout_;
  return ui::Rect{l.editor.x, l.editor.y, l.editor.width, l.lane.bottom() - l.editor.y};
}

ui::Rect SingShell::exportRunButton() const noexcept {
  if (workspace_ != Workspace::Export || !presented_) return {};
  return exportPanelLayout(exportArea()).button;
}

bool SingShell::exportBusy(const NativeEditorController& controller) const {
  // Live state only: the host's own worker flag, and the editor's current export progress. The
  // editor reports "no export" as a preflight with no files, so a real zero-file preflight is
  // covered by the host flag, which is set for the whole run of its worker.
  if (hostActions_.exportBusy && hostActions_.exportBusy()) return true;
  const auto& progress = controller.exportProgress();
  switch (progress.state) {
    case authoring::ExportState::Staging:
    case authoring::ExportState::Prepared:
    case authoring::ExportState::Publishing: return true;
    case authoring::ExportState::Preflight: return progress.totalFiles != 0U;
    default: return false;
  }
}

void SingShell::setWorkspace(NativeEditorController& controller, Workspace workspace) {
  if (workspace == workspace_) return;
  // The grid the gestures and any text field belong to leaves the screen with its workspace.
  cancelGestures(controller);
  if (lyricInputActive_ || controller.textInputActive()) controller.cancelTextComposition();
  lyricInputActive_ = false;
  semanticFocus_.clear();
  workspace_ = workspace;
  repaint();
}

core::Result<void> SingShell::runExportSet(NativeEditorController& controller) {
  if (!hostActions_.exportSet)
    return core::failure(core::ErrorCode::Unsupported, hostActions_.exportUnavailable);
  if (exportBusy(controller))
    return core::failure(core::ErrorCode::Conflict, "An export is already running");
  auto result = hostActions_.exportSet();
  yieldIfModal(controller);
  repaint();
  return result;
}

core::Result<void> SingShell::dispatchController(NativeEditorController& controller,
                                                 std::string_view id, SemanticAction action) {
  // A retained host element keeps its action flags after the shell stopped publishing it (the
  // score under EXPORT, a control the layout removed); only what is on screen now may act.
  refreshSemantics(controller);
  if (!semantics_.publishes(id))
    return core::failure(core::ErrorCode::Conflict, "This element is not on screen");
  auto result = semantics_.dispatch(
      id, action, [&controller](std::string_view target, SemanticAction requested) {
        return controller.dispatchAccessibility(target, requested);
      });
  if (result && action == SemanticAction::SetFocus) controllerFocusTaken();
  yieldIfModal(controller);
  repaint();
  return result;
}

core::Result<void> SingShell::setControllerValue(NativeEditorController& controller,
                                                 std::string_view id, std::string_view value) {
  refreshSemantics(controller);
  if (!semantics_.publishes(id))
    return core::failure(core::ErrorCode::Conflict, "This element is not on screen");
  return controller.setAccessibilityValue(id, value);
}

void SingShell::paintExport(Canvas2D& c, const DesignTokens& t, const EditorSceneState& state) const {
  const auto area = exportArea();
  const auto p = exportPanelLayout(area);
  glassPanel(c, t, area, t.shape.card, 0.97);
  const auto& last = state.lastExport;
  cardHeader(c, t, area, "Export set", exportRunning_ || last.has_value());
  c.save();
  c.clipRect(area);
  const auto left = area.x + 32.0;
  const auto labelStyle = style(FontRole::UiSemibold, t.type.smallLabel, t.type.labelTracking,
                                TextAlign::Left, true);
  const auto valueStyle = style(FontRole::Ui, t.type.body);
  const auto row = [&](double x, double y, double width, std::string_view label,
                       const std::string& value, Color valueColor) {
    c.text({x, y, width, 16.0}, label, labelStyle, t.color.textSecondary);
    c.text({x, y + 18.0, width, 20.0}, value, fitted(c, value, valueStyle, width), valueColor);
  };

  // What an Export Set writes, as the host computed it for this document.
  const auto plan = hostActions_.exportPlan ? hostActions_.exportPlan() : std::nullopt;
  if (p.compact) {
    const auto summary = exportPlanSummary(plan, hostActions_.exportUnavailable);
    c.text(p.summary, summary, fitted(c, summary, style(FontRole::Ui, t.type.smallLabel), p.summary.width),
           plan ? t.color.textPrimary : t.color.warning);
  } else if (plan) {
    const auto y = area.y + 64.0;
    row(left, y, p.columnWidth, "Master mix",
        plan->master ? channelLayout(plan->channels) + " \u00b7 " + sampleRateLabel(plan->sampleRate) +
                           " \u00b7 " + plan->format
                     : std::string{"Not written"},
        t.color.textPrimary);
    row(left, y + 46.0, p.columnWidth, "Stems",
        plan->stems ? std::string{"One file per track, same format"} : std::string{"Not written"},
        t.color.textPrimary);
    row(left, y + 92.0, p.columnWidth, "Project and recipes",
        plan->asksAboutPackaging ? std::string{"You choose when the export starts"}
                                 : std::string{"Nothing to package for this project"},
        t.color.textPrimary);
    row(left, y + 138.0, p.columnWidth, "Receipt", "SHA-256 of every file, written last",
        t.color.textPrimary);
  } else {
    row(left, area.y + 64.0, p.columnWidth, "Export", hostActions_.exportUnavailable, t.color.warning);
  }

  // The run button: the host's real command, refused while an export runs or when it cannot export.
  const auto button = p.button;
  const auto available = static_cast<bool>(hostActions_.exportSet) && !exportRunning_;
  c.save();
  if (available) c.setGlow(withAlpha(t.color.accent, 0.7), 10.0);
  c.fill(Path::capsule(button), available ? t.color.accent : withAlpha(t.color.surfaceSunken, 0.9));
  c.restore();
  c.stroke(Path::capsule(button), available ? t.color.accentDeep : t.color.border, StrokeStyle{1.0});
  c.text(button, exportRunning_ ? "Exporting\u2026" : "Export set\u2026",
         style(FontRole::UiBold, t.type.label, 1.4, TextAlign::Center, true),
         available ? t.color.textOnAccent : t.color.textDisabled);
  if (p.note.width > 0.0)
    c.text(p.note,
           hostActions_.exportSet ? std::string{"Choose a new folder; an existing export set is never overwritten."}
                                  : hostActions_.exportUnavailable,
           style(FontRole::Ui, t.type.smallLabel), t.color.textSecondary);

  // Current attempt and the last written set.
  const auto& progress = state.exportProgress;
  struct StatusLine final {
    std::string label;
    std::string value;
    Color color;
  };
  std::vector<StatusLine> lines;
  if (exportRunning_) {
    lines.push_back({"Progress", exportStateLabel(progress.state) + "  " +
                                     std::to_string(progress.completedFiles) + " / " +
                                     std::to_string(progress.totalFiles) + " files" +
                                     (progress.currentOutput.empty() ? "" : "  " + progress.currentOutput),
                     t.color.textPrimary});
  } else if (attemptEnded(progress.state)) {
    lines.push_back({"Last attempt",
                     exportStateLabel(progress.state) +
                         (progress.currentOutput.empty() ? "" : ": " + progress.currentOutput),
                     progress.state == authoring::ExportState::Cancelled ? t.color.warning : t.color.error});
  } else {
    lines.push_back({"Progress", "Idle", t.color.textSecondary});
  }
  if (last) {
    const auto folder = last->setPath.empty() ? last->masterPath.parent_path() : last->setPath;
    lines.push_back({"Last export",
                     exportStateLabel(last->state) + " \u00b7 " + std::to_string(last->files.size()) +
                         " files \u00b7 " + folder.filename().string(),
                     last->state == authoring::ExportState::Committed ? t.color.success : t.color.warning});
    if (!p.compact)
      lines.push_back({"Master",
                       last->masterPath.filename().string() +
                           (last->masterSha256.size() >= 12U
                                ? "  sha256 " + last->masterSha256.substr(0U, 12U) + "\u2026"
                                : std::string{}),
                       t.color.textPrimary});
  } else {
    lines.push_back({"Last export", "Nothing exported in this session", t.color.textSecondary});
  }
  auto y = p.status.y;
  for (const auto& line : lines) {
    if (p.compact) {
      if (y + 16.0 > p.status.bottom() + 1.0) break;
      const auto text = line.label + ": " + line.value;
      c.text({p.status.x, y, p.status.width, 16.0}, text,
             fitted(c, text, style(FontRole::Ui, t.type.smallLabel), p.status.width), line.color);
      y += 18.0;
    } else {
      if (y + 38.0 > p.status.bottom() + 1.0) break;
      row(p.status.x, y, p.status.width, line.label, line.value, line.color);
      y += 46.0;
    }
  }
  c.restore();
}


core::Result<void> SingShell::nudge(NativeEditorController& controller, std::size_t index, int steps) {
  if (steps == 0) return core::success();
  switch (ui::expressionChannelAt(index)) {
    case ui::ExpressionChannel::Formant: return controller.nudgeFormantShift(steps);
    case ui::ExpressionChannel::Breathiness: return controller.nudgeBreathiness(steps);
    case ui::ExpressionChannel::Tension: return controller.nudgeTension(steps);
    case ui::ExpressionChannel::Airiness: return controller.nudgeAiriness(steps);
    case ui::ExpressionChannel::Gender: return controller.nudgeGender(steps);
    case ui::ExpressionChannel::Growl: return controller.nudgeGrowl(steps);
  }
  return core::success();
}

core::Result<void> SingShell::pointerDown(NativeEditorController& controller,
                                          const PointerEvent& event) {
  if (!presented_) return controller.pointerDown(event);
  const auto result = shellPointerDown(controller, event);
  yieldIfModal(controller);
  return result;
}

core::Result<void> SingShell::shellPointerDown(NativeEditorController& controller,
                                               const PointerEvent& event) {
  const auto p = event.position;
  const auto& l = layout_;
  if (event.button == PointerButton::Left) {
    // Workspace tabs: SING and EXPORT switch the workspace; VOICE opens the voice browser.
    for (std::size_t i = 0U; i < l.workspaceTab.size(); ++i) {
      if (l.workspaceTab[i].width < 24.0 || !contains(l.workspaceTab[i], p)) continue;
      if (i == 0U) setWorkspace(controller, Workspace::Sing);
      if (i == 4U) setWorkspace(controller, Workspace::Export);
      if (i == 1U) {
        controller.showVoicebankBrowser();
        yieldIfModal(controller);
        repaint();
      }
      return core::success();
    }
    if (workspace_ == Workspace::Export && contains(exportRunButton(), p))
      return runExportSet(controller);
  }
  // The export panel covers the grid and lane; nothing under it is reachable.
  if (workspace_ == Workspace::Export && contains(exportArea(), p)) return core::success();
  if (event.button == PointerButton::Left) {
    if (contains(l.modeSwitch, p)) {
      setMode(p.x < l.modeSwitch.x + l.modeSwitch.width * 0.5 ? DesignMode::Emo : DesignMode::Scene);
      return core::success();
    }
    if (contains(l.playButton, p)) return controller.keyDown(KeyEvent{.key = NativeKey::Space});
    if (contains(l.tempoReadout, p)) return controller.beginTempoEdit();
    if (contains(l.meterReadout, p)) return controller.beginMeterEdit();
    if (contains(l.settings, p)) {
      controller.showAudioSettings();
      repaint();
      return core::success();
    }
    if (contains(l.classicToggle, p)) {
      setEnabled(controller, false);
      return core::success();
    }
    if (contains(l.singerChange, p)) {
      controller.showVoicebankBrowser();
      repaint();
      return core::success();
    }
    if (l.rack == RackPresentation::Full) {
      for (std::size_t i = 0U; i < l.knob.size(); ++i) {
        if (!contains(l.knob[i], p)) continue;
        if (knobRefused_[i]) return core::success();
        knobDrag_ = KnobDrag{.index = i,
                             .startY = p.y,
                             .steps = 0,
                             .moved = false,
                             .revision = controller.documentRevision(),
                             .region = controller.selectedRegion(),
                             .playhead = controller.playheadTick()};
        // Grabbing a knob gives it keyboard focus, so arrow keys continue the same control.
        static constexpr std::array<const char*, 6U> kKnobIds{
            "shell.knob.formant", "shell.knob.breath", "shell.knob.tension",
            "shell.knob.air",     "shell.knob.gender", "shell.knob.growl"};
        takeSemanticFocus(controller, kKnobIds[i]);
        return core::success();
      }
    }
    const auto tabWidth = std::min(104.0, l.laneTabs.width / 9.0);
    for (std::size_t i = 0U; i < 7U; ++i) {
      const ui::Rect tab{l.laneTabs.x + static_cast<double>(i) * (tabWidth + 4.0), l.laneTabs.y,
                         tabWidth, l.laneTabs.height};
      if (!contains(tab, p)) continue;
      if (i == 0U) return controller.openDynamicsInspector();
      return controller.openExpressionLane(ui::expressionChannelAt(i - 1U));
    }
  }
  if (inMusicalArea(p)) {
    forwarding_ = ForwardArea::Grid;
    auto result = controller.pointerDown(translated(event, forwarding_));
    // A press in the grid or lane hands keyboard focus to the editor it reached.
    if (result) semanticFocus_.clear();
    return result;
  }
  if (inEditableLane(p)) {
    forwarding_ = ForwardArea::Lane;
    auto result = controller.pointerDown(translated(event, forwarding_));
    if (result) semanticFocus_.clear();
    return result;
  }
  return core::success();
}

core::Result<void> SingShell::pointerMove(NativeEditorController& controller,
                                          const PointerEvent& event) {
  if (!presented_) return controller.pointerMove(event);
  if (knobDrag_) {
    const auto steps = static_cast<int>(std::lround((knobDrag_->startY - event.position.y) / 12.0));
    if (steps != knobDrag_->steps) {
      knobDrag_->steps = steps;
      knobDrag_->moved = true;
      repaint();
    }
    return core::success();
  }
  // A gesture keeps the transform of the area it started in, wherever the pointer goes.
  if (forwarding_ != ForwardArea::None) return controller.pointerMove(translated(event, forwarding_));
  if (inMusicalArea(event.position))
    return controller.pointerMove(translated(event, ForwardArea::Grid));
  if (inEditableLane(event.position))
    return controller.pointerMove(translated(event, ForwardArea::Lane));
  return core::success();
}

core::Result<void> SingShell::pointerUp(NativeEditorController& controller,
                                        const PointerEvent& event) {
  if (!presented_) return controller.pointerUp(event);
  if (knobDrag_) {
    const auto drag = *knobDrag_;
    knobDrag_.reset();
    repaint();
    // The release commits only against the document, region and playhead the gesture began on.
    if (controller.documentRevision() != drag.revision ||
        controller.selectedRegion() != drag.region || controller.playheadTick() != drag.playhead)
      return core::success();
    // One gesture is one command, so one undo step.
    if (drag.steps != 0) return nudge(controller, drag.index, drag.steps);
    const auto opened = controller.openExpressionLane(ui::expressionChannelAt(drag.index));
    yieldIfModal(controller);
    return opened;
  }
  if (forwarding_ != ForwardArea::None) {
    const auto area = forwarding_;
    forwarding_ = ForwardArea::None;
    const auto result = controller.pointerUp(translated(event, area));
    yieldIfModal(controller);
    return result;
  }
  return core::success();
}

bool SingShell::scroll(NativeEditorController& controller, double deltaX, double deltaY,
                       ui::Point anchor, InputModifiers modifiers) {
  if (!presented_) return false;
  if (layout_.rack == RackPresentation::Full) {
    for (std::size_t i = 0U; i < layout_.knob.size(); ++i) {
      if (!contains(layout_.knob[i], anchor)) continue;
      if (knobRefused_[i] || knobDrag_) return true;
      scrollAccumulator_ += deltaY;
      const auto steps = static_cast<int>(scrollAccumulator_ / 8.0);
      if (steps != 0) {
        scrollAccumulator_ -= steps * 8.0;
        static_cast<void>(nudge(controller, i, -steps));
      }
      return true;
    }
  }
  if (inMusicalArea(anchor) ||
      (workspace_ == Workspace::Sing && contains(layout_.laneTimePlot, anchor))) {
    controller.scroll(deltaX, deltaY, toLegacy(anchor), modifiers);
    return true;
  }
  return true;
}

bool SingShell::handleShellKey(NativeEditorController& controller, const KeyEvent& event) {
  if (event.key == NativeKey::Space && event.modifiers.shift && event.modifiers.primaryShortcut()) {
    setEnabled(controller, !enabled());
    return true;
  }
  if (event.key == NativeKey::Escape && presented_ &&
      (knobDrag_ || (forwarding_ != ForwardArea::None && controller.pointerGestureActive()))) {
    cancelGestures(controller);
    return true;
  }
  // The EXPORT workspace hides the score, so no key may edit it from here, whatever holds focus.
  // Escape always returns to SING. Only the shortcuts the host declares as its own application
  // commands (it implements them itself) still reach it; every other modified key (Alt-Delete,
  // Command-D, Command-Delete, a Command-Q the host does not handle) stops here instead of
  // reaching the note editor.
  if (presented_ && workspace_ == Workspace::Export && !controller.legacyModalSurfaceActive()) {
    if (event.key == NativeKey::Escape) {
      setWorkspace(controller, Workspace::Sing);
      return true;
    }
    if (event.modifiers.primaryShortcut() || event.modifiers.alt)
      return !(hostActions_.applicationShortcut && hostActions_.applicationShortcut(event));
  }
  // Keyboard focus walks the tree the shell published, so it reaches the controls that are on
  // screen here. Text fields and classic surfaces keep their own Tab behavior.
  const auto keyboardFocusOwned = presented_ && !event.modifiers.primaryShortcut() &&
                                  !event.modifiers.alt && !controller.textInputActive() &&
                                  !controller.legacyModalSurfaceActive();
  if (event.key == NativeKey::Tab && keyboardFocusOwned) {
    refreshSemantics(controller);
    if (!semantics_.focusNext(event.modifiers.shift)) return true;
    if (const auto* node = semantics_.focusedNode(); node != nullptr) {
      const auto id = node->id;
      if (ownsSemantic(id)) {
        takeSemanticFocus(controller, id);
      } else {
        semanticFocus_.clear();
        if (!controller.dispatchAccessibility(id, SemanticAction::SetFocus)) refreshSemantics(controller);
      }
    }
    repaint();
    return true;
  }
  if (!keyboardFocusOwned) return false;
  // Keys follow the element the shell publishes as focused right now. Rebuilding first means a
  // control that has left the layout (a knob after the rack collapsed) no longer owns them.
  refreshSemantics(controller);
  const auto* focused = semantics_.focusedNode();
  // With the EXPORT workspace up the score is not on screen, so its plain keys never edit it
  // (Escape was handled above).
  const auto scoreHidden = [this] { return workspace_ == Workspace::Export; };
  if (focused == nullptr) return scoreHidden();
  const std::string id = focused->id;
  // While a shell control holds focus, plain keys belong to it and never reach the note editor
  // (Delete must not delete the selected notes because a knob is focused). Command shortcuts such
  // as undo and save still pass through.
  if (ownsSemantic(id)) {
    switch (event.key) {
      case NativeKey::Escape:
        semanticFocus_.clear();
        repaint();
        break;
      case NativeKey::Enter:
      case NativeKey::Space:
        static_cast<void>(dispatchSemantic(controller, id, SemanticAction::Activate));
        break;
      case NativeKey::Up:
      case NativeKey::Right:
        static_cast<void>(dispatchSemantic(controller, id, SemanticAction::Increment));
        break;
      case NativeKey::Down:
      case NativeKey::Left:
        static_cast<void>(dispatchSemantic(controller, id, SemanticAction::Decrement));
        break;
      default: break;
    }
    return true;
  }
  // Notes, the timeline and vibrato handles are the score editor's: its keys edit them.
  if (!rehomedControl(id)) return scoreHidden();
  // A re-homed editor control (transport, tempo, meter, voice identity, an overlap group, status
  // actions) owns the plain keys too: Enter/Space act on it, never on the selected note's lyric.
  if (event.key == NativeKey::Escape) return false;
  if (event.key == NativeKey::Enter || event.key == NativeKey::Space) {
    const auto offers = [focused](SemanticAction action) {
      return std::find(focused->actions.begin(), focused->actions.end(), action) !=
             focused->actions.end();
    };
    const auto action = offers(SemanticAction::Activate) ? std::optional{SemanticAction::Activate}
                        : offers(SemanticAction::Toggle) ? std::optional{SemanticAction::Toggle}
                                                         : std::nullopt;
    if (action && focused->enabled) {
      static_cast<void>(controller.dispatchAccessibility(id, *action));
      yieldIfModal(controller);
    }
    repaint();
  }
  return true;
}

bool SingShell::rehomedControl(std::string_view id) noexcept {
  return id.starts_with("toolbar.") || id == "voice.identity" || id.starts_with("diagnostic") ||
         id.starts_with("export.") || id.starts_with("overlap-group.") || id.starts_with("detail.");
}

void SingShell::refreshSemantics(NativeEditorController& controller) {
  controller.rebuildAccessibilityTree();
  rebuildSemantics(controller, controller.sceneState());
}

void SingShell::takeSemanticFocus(NativeEditorController& controller, std::string id) {
  // Focus leaving the editor ends any vibrato handle subfocus there. The editor's own focus moves
  // off the handle to the note that owns it through a real focus transition, so when the shell
  // later releases focus (Escape, the control leaving the layout) the focus it reports and the
  // target that receives the keys are the same note.
  controller.accessibilityFocusMoved(id);
  if (const auto* current = controller.accessibilityTree().focusedNode();
      current != nullptr && current->id.starts_with("editor.vibrato.handle.")) {
    constexpr std::string_view kPrefix{"editor.vibrato.handle."};
    const std::string_view handle{current->id};
    const auto rest = handle.substr(kPrefix.size());
    const auto kind = rest.rfind('.');
    if (kind != std::string_view::npos) {
      const auto note = "note." + std::string{rest.substr(0U, kind)};
      static_cast<void>(controller.dispatchAccessibility(note, SemanticAction::SetFocus));
    }
  }
  const auto* controllerFocus = controller.accessibilityTree().focusedNode();
  semanticFocusBaseline_ = controllerFocus == nullptr ? std::string{} : controllerFocus->id;
  semanticFocus_ = std::move(id);
}

// ---- Accessibility ----------------------------------------------------------------------------

void SingShell::rebuildSemantics(const NativeEditorController& controller,
                                 const EditorSceneState& state) {
  const auto& l = layout_;
  const auto& legacy = controller.accessibilityTree().root();
  const auto* legacyFocus = controller.accessibilityTree().focusedNode();
  // Shell focus lasts only until the controller's own focus moves (a click on a note, a controller
  // keyboard command); then the controller's focus is the one reported.
  const std::string legacyFocusId = legacyFocus == nullptr ? std::string{} : legacyFocus->id;
  if (!semanticFocus_.empty() && legacyFocusId != semanticFocusBaseline_) semanticFocus_.clear();
  std::string focusedId = semanticFocus_;
  SemanticNode root{.id = "shell",
                    .role = SemanticRole::Window,
                    .name = "Project SEAM - Sing",
                    .bounds = ui::Rect{0.0, 0.0, l.width, l.height}};
  auto& children = root.children;
  const auto add = [&](SemanticNode node) { children.push_back(std::move(node)); };
  // Controller nodes can be nested (the transport readouts and voice identity live inside the
  // toolbar group), so the lookup walks the whole tree.
  const auto findLegacy = [&](std::string_view id) -> const SemanticNode* {
    const auto search = [id](const SemanticNode& node, const auto& self) -> const SemanticNode* {
      for (const auto& child : node.children) {
        if (child.id == id) return &child;
        if (const auto* found = self(child, self); found != nullptr) return found;
      }
      return nullptr;
    };
    return search(legacy, search);
  };
  // A controller node shown by the shell at a shell rectangle keeps its id, name and actions.
  const auto rehome = [&](std::string_view id, ui::Rect bounds) {
    if (const auto* node = findLegacy(id); node != nullptr) {
      auto copy = *node;
      copy.bounds = bounds;
      copy.children.clear();
      add(std::move(copy));
    }
  };

  // Header: workspaces, look, transport, settings.
  static constexpr std::array<const char*, 5U> kWorkspaces{"Sing", "Voice", "Tune", "Mix", "Export"};
  for (std::size_t i = 0U; i < kWorkspaces.size(); ++i) {
    if (l.workspaceTab[i].width <= 0.0 || l.workspaceTabs.width <= 0.0) break;
    add(SemanticNode{.id = "shell.workspace." + lowercase(kWorkspaces[i]),
                     .role = SemanticRole::Tab,
                     .name = std::string{kWorkspaces[i]} + " workspace",
                     .bounds = l.workspaceTab[i],
                     .enabled = i == 0U || i == 1U || i == 4U,
                     .selected = (i == 0U && workspace_ == Workspace::Sing) ||
                                 (i == 4U && workspace_ == Workspace::Export),
                     .actions = i == 0U || i == 1U || i == 4U
                                    ? std::vector<SemanticAction>{SemanticAction::Activate,
                                                                  SemanticAction::SetFocus}
                                    : std::vector<SemanticAction>{SemanticAction::SetFocus},
                     .description = i == 1U   ? "Opens the voice browser"
                                    : i == 2U || i == 3U ? "Not available in this build"
                                                         : ""});
  }
  for (const auto mode : {DesignMode::Emo, DesignMode::Scene}) {
    const auto emo = mode == DesignMode::Emo;
    auto half = l.modeSwitch;
    half.width *= 0.5;
    if (!emo) half.x += half.width;
    add(SemanticNode{.id = emo ? "shell.mode.emo" : "shell.mode.scene",
                     .role = SemanticRole::RadioButton,
                     .name = emo ? "EMO look" : "SCENE look",
                     .bounds = half,
                     .selected = preferences_.mode == mode,
                     .actions = {SemanticAction::Activate, SemanticAction::SetFocus}});
  }
  rehome("toolbar.transport", l.playButton);
  rehome("toolbar.tempo", l.tempoReadout);
  rehome("toolbar.meter", l.meterReadout);
  add(SemanticNode{.id = "shell.settings", .role = SemanticRole::Button, .name = "Audio settings",
                   .bounds = l.settings,
                   .actions = {SemanticAction::Activate, SemanticAction::SetFocus}});
  add(SemanticNode{.id = "shell.classic", .role = SemanticRole::Button,
                   .name = "Switch to the classic editor", .bounds = l.classicToggle,
                   .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
                   .description = "Command-Shift-Space returns to this design"});

  // Timeline and the notes visible in the grid, in shell coordinates.
  if (const auto* timeline = findLegacy("timeline"); timeline != nullptr) {
    auto copy = *timeline;
    copy.bounds = l.grid;
    copy.children.clear();
    add(std::move(copy));
  }
  // Notes stay virtualized in the controller's tree (see the VirtualNoteSource below); only the
  // selected note's vibrato handles are listed here, clipped to the grid like the notes.
  const auto grid = l.grid;
  const auto offsetY = l.grid.y - legacyContentTop_;
  // Geometry for a note comes from the layout that was painted: overlap bands and hidden members
  // exist only in the visible layout, not in the model's raw note rectangles. The snapshot is
  // bounded to the visible notes and keyed by id; notes outside it keep their logical rectangle.
  struct PaintedNote final {
    ui::Rect bounds;
    bool hidden{false};
  };
  auto painted = std::make_shared<std::unordered_map<std::string, PaintedNote>>();
  for (const auto& visual : controller.pianoRoll().visibleNotes()) {
    auto bounds = visual.bounds;
    bounds.y += legacyContentTop_;
    painted->insert_or_assign("note." + visual.noteId.toString(),
                              PaintedNote{bounds, visual.hiddenByOverlapDensity});
  }
  const auto presentInGrid = [grid, offsetY](SemanticNode& node) {
    node.bounds.y += offsetY;
    const auto left = std::max(node.bounds.x, grid.x);
    const auto top = std::max(node.bounds.y, grid.y);
    const auto right = std::min(node.bounds.x + node.bounds.width, grid.x + grid.width);
    const auto bottom = std::min(node.bounds.y + node.bounds.height, grid.y + grid.height);
    if (right > left && bottom > top) {
      node.bounds = ui::Rect{left, top, right - left, bottom - top};
      return;
    }
    node.bounds = ui::Rect{std::clamp(node.bounds.x, grid.x, grid.x + grid.width),
                           std::clamp(node.bounds.y, grid.y, grid.y + grid.height), 0.0, 0.0};
    node.description += node.description.empty() ? "" : " / ";
    node.description += "Outside the visible grid; scroll to show it";
  };
  const auto presentNote = [presentInGrid, painted](SemanticNode& node) {
    if (const auto found = painted->find(node.id); found != painted->end()) {
      node.bounds = found->second.bounds;
      constexpr std::string_view kDense{"drawn inside a dense overlap group"};
      if (found->second.hidden && node.description.find(kDense) == std::string::npos)
        node.description += "; drawn inside a dense overlap group, whose detail lists it";
    }
    presentInGrid(node);
  };
  {
    const auto collect = [&](const SemanticNode& node, const auto& self) -> void {
      for (const auto& child : node.children) {
        if (child.id.starts_with("editor.vibrato.handle.")) {
          auto copy = child;
          presentInGrid(copy);
          add(std::move(copy));
        }
        self(child, self);
      }
    };
    collect(legacy, collect);
  }
  for (const auto& child : legacy.children) {
    if (!child.id.starts_with("overlap-group.") && !child.id.starts_with("detail.")) continue;
    auto copy = child;
    // Every descendant moves with its group: overlap groups are clipped to the grid like notes,
    // and the detail popover's rows move with the popover.
    const auto inGrid = child.id.starts_with("overlap-group.");
    const auto transform = [&](SemanticNode& node, const auto& self) -> void {
      if (inGrid)
        presentInGrid(node);
      else
        node.bounds = fromLegacy(node.bounds);
      for (auto& grandchild : node.children) self(grandchild, self);
    };
    transform(copy, transform);
    add(std::move(copy));
  }

  // Lane selector and the hosted lane.
  static constexpr std::array<const char*, 7U> kLanes{"Dynamics", "Formant", "Breath", "Tension",
                                                       "Air", "Gender", "Growl"};
  const auto open = state.expressionLabelVisible();
  const auto selectedLane = open ? ui::expressionChannelIndex(state.expression.channel) + 1U : 99U;
  const auto tabWidth = std::min(104.0, l.laneTabs.width / 9.0);
  for (std::size_t i = 0U; i < kLanes.size(); ++i) {
    add(SemanticNode{.id = "shell.lane-tab." + lowercase(kLanes[i]),
                     .role = SemanticRole::Tab,
                     .name = std::string{kLanes[i]} + " lane",
                     .bounds = {l.laneTabs.x + static_cast<double>(i) * (tabWidth + 4.0),
                                l.laneTabs.y, tabWidth, l.laneTabs.height},
                     .selected = i == selectedLane,
                     .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
                     .description = i == 0U ? "Opens the Dynamics editor" : ""});
  }
  add(SemanticNode{
      .id = "shell.lane",
      .role = SemanticRole::Lane,
      .name = open ? state.expression.label + " curve" : "Expression lane",
      .value = !open ? "No channel selected"
               : !state.expression.refusal.empty() ? state.expression.refusal
                   : state.expression.points.empty()
                       ? "No curve stored"
                       : std::to_string(state.expression.points.size()) + " points",
      .bounds = l.laneTimePlot,
      .enabled = laneEditable_,
      .actions = {SemanticAction::SetFocus},
      .description = laneEditable_ ? "Click to add a point, drag to move, Escape cancels a drag"
                                   : "Select a channel tab to edit its curve"});

  // Singer rack.
  rehome("voice.identity", l.rack == RackPresentation::Full ? l.singer : l.portraitRing);
  add(SemanticNode{.id = "shell.change-voice", .role = SemanticRole::Button, .name = "Change voice",
                   .bounds = l.singerChange,
                   .actions = {SemanticAction::Activate, SemanticAction::SetFocus}});
  if (l.rack == RackPresentation::Full) {
    const auto knobs = knobModels(state);
    for (std::size_t i = 0U; i < knobs.size(); ++i) {
      const auto& k = knobs[i];
      const auto percent = k.descriptor.unit != "semitones";
      const auto scale = percent ? 100.0 : 1.0;
      const auto [number, unit] = displayValue(k);
      const auto refused = !k.refusal.empty();
      add(SemanticNode{
          .id = "shell.knob." + lowercase(k.label),
          .role = SemanticRole::Slider,
          .name = k.label,
          .value = refused ? k.refusal : number + (percent ? "%" : " semitones"),
          .bounds = l.knob[i],
          .enabled = !refused,
          .actions = refused ? std::vector<SemanticAction>{SemanticAction::SetFocus}
                             : std::vector<SemanticAction>{SemanticAction::Increment,
                                                           SemanticAction::Decrement,
                                                           SemanticAction::Activate,
                                                           SemanticAction::SetFocus},
          .description = refused ? k.refusal
                                 : "Value at the playhead; Activate opens its curve in the lane",
          .numericValue = k.value * scale,
          .numericMinimum = static_cast<double>(k.descriptor.minimum) * scale,
          .numericMaximum = static_cast<double>(k.descriptor.maximum) * scale,
          .numericStep = static_cast<double>(k.descriptor.step) * scale});
    }
    std::string styles;
    for (const auto& card : state.voicebankCards)
      if (card.id == state.inspector.voicebank.id && !card.id.empty())
        for (const auto& style : card.styles) styles += (styles.empty() ? "" : ", ") + style;
    add(SemanticNode{.id = "shell.style", .role = SemanticRole::Panel, .name = "Style presets",
                     .value = styles.empty() ? "The selected voice publishes no style presets" : styles,
                     .bounds = l.style});
  }

  // Status bar: render state, diagnostics and export actions stay reachable.
  const auto& s = state.renderStatus;
  add(SemanticNode{.id = "shell.status", .role = SemanticRole::Status, .name = "Render status",
                   .value = std::string{renderStatusStateName(s.state)} +
                            (s.diagnostic.empty() ? "" : ": " + s.diagnostic),
                   .bounds = l.status});
  if (s.state == RenderStatusState::Rendering || s.state == RenderStatusState::Queued) {
    add(SemanticNode{.id = "shell.render-progress", .role = SemanticRole::ProgressIndicator,
                     .name = "Render progress",
                     .value = format("%.0f%%", std::clamp(s.fraction, 0.0, 1.0) * 100.0),
                     .bounds = l.status,
                     .numericValue = std::clamp(s.fraction, 0.0, 1.0) * 100.0,
                     .numericMinimum = 0.0,
                     .numericMaximum = 100.0});
  }
  for (const auto& child : legacy.children) {
    if (!child.id.starts_with("diagnostic") && !child.id.starts_with("export.")) continue;
    auto copy = child;
    copy.bounds = l.status;
    add(std::move(copy));
  }

  const auto singShown = workspace_ == Workspace::Sing;
  if (singShown) {
    // What the notes show of the rendered audio, as painted in this frame.
    add(SemanticNode{.id = "shell.waveform", .role = SemanticRole::Status,
                     .name = "Note waveform",
                     .value = waveform_.shown() ? std::string{"Showing the current render"}
                                                : waveform_.caption,
                     .bounds = layout_.gridLabel,
                     .actions = {SemanticAction::SetFocus},
                     .description = waveform_.reason});
  }
  if (!singShown) {
    // The EXPORT workspace covers the score: nothing of the grid or lane is published under it.
    std::erase_if(children, [](const SemanticNode& node) {
      return node.id == "timeline" || node.id.starts_with("editor.vibrato.handle.") ||
             node.id.starts_with("overlap-group.") || node.id.starts_with("detail.") ||
             node.id.starts_with("shell.lane");
    });
    const auto plan = hostActions_.exportPlan ? hostActions_.exportPlan() : std::nullopt;
    // Busy is read live here too: accessibility must not offer a run the paint cache still shows.
    const auto busy = exportBusy(controller);
    const auto available = static_cast<bool>(hostActions_.exportSet) && !busy;
    const auto panel = exportPanelLayout(exportArea());
    const auto summary = exportPlanSummary(plan, hostActions_.exportUnavailable);
    add(SemanticNode{.id = "shell.export.panel", .role = SemanticRole::Panel,
                     .name = "Export set", .value = summary, .bounds = exportArea(),
                     .actions = {SemanticAction::SetFocus}});
    add(SemanticNode{
        .id = "shell.export.run", .role = SemanticRole::Button, .name = "Export set",
        .bounds = panel.button, .enabled = available,
        .actions = available ? std::vector<SemanticAction>{SemanticAction::Activate,
                                                           SemanticAction::SetFocus}
                             : std::vector<SemanticAction>{SemanticAction::SetFocus},
        .description = !hostActions_.exportSet ? hostActions_.exportUnavailable
                       : busy                 ? std::string{"An export is already running"}
                                              : std::string{"Choose a new folder for the set"}});
    const auto& progress = controller.exportProgress();
    const auto statusBounds = panel.status.height > 0.0 ? panel.status : panel.button;
    if (busy) {
      const auto fraction = static_cast<double>(progress.completedFiles) /
                            static_cast<double>(std::max<std::uint64_t>(1U, progress.totalFiles));
      add(SemanticNode{.id = "shell.export.progress", .role = SemanticRole::ProgressIndicator,
                       .name = "Export progress",
                       .value = exportStateLabel(progress.state) + ", " +
                                std::to_string(progress.completedFiles) + " of " +
                                std::to_string(progress.totalFiles) + " files",
                       .bounds = statusBounds,
                       .numericValue = fraction * 100.0,
                       .numericMinimum = 0.0,
                       .numericMaximum = 100.0});
    } else if (attemptEnded(progress.state)) {
      // A failed or cancelled attempt is reported with its reason, whether or not any file was
      // counted and whatever an earlier successful export wrote.
      add(SemanticNode{.id = "shell.export.attempt", .role = SemanticRole::Status,
                       .name = "Last export attempt",
                       .value = exportStateLabel(progress.state) +
                                (progress.currentOutput.empty() ? "" : ": " + progress.currentOutput),
                       .bounds = statusBounds,
                       .actions = {SemanticAction::SetFocus}});
    }
    const auto& last = state.lastExport;
    add(SemanticNode{
        .id = "shell.export.last", .role = SemanticRole::Status, .name = "Last export",
        .value = last ? exportStateLabel(last->state) + ", " + std::to_string(last->files.size()) +
                            " files in " +
                            (last->setPath.empty() ? last->masterPath.parent_path() : last->setPath)
                                .filename()
                                .string()
                      : std::string{"Nothing exported in this session"},
        .bounds = statusBounds,
                        .actions = {SemanticAction::SetFocus}});
  }
  // A shell control that is no longer published (a knob after the rack collapsed to a rail) gives
  // up focus, and with it the keys; the editor's own focus is reported instead.
  if (!semanticFocus_.empty() && !EditorSemanticTree::containsId(root, semanticFocus_)) {
    semanticFocus_.clear();
    focusedId.clear();
  }
  if (focusedId.empty() && legacyFocus != nullptr) focusedId = legacyFocus->id;
  // A controller focus inside the hidden score is not reported while EXPORT is up.
  if (!singShown && !focusedId.empty() && !EditorSemanticTree::containsId(root, focusedId))
    focusedId.clear();
  semantics_.rebuildCustom(std::move(root), focusedId,
                           singShown ? VirtualNoteSource{.tree = &controller.accessibilityTree(),
                                                         .present = presentNote}
                                     : VirtualNoteSource{});
}

core::Result<void> SingShell::dispatchSemantic(NativeEditorController& controller,
                                               std::string_view id, SemanticAction action) {
  if (!ownsSemantic(id))
    return core::failure(core::ErrorCode::NotFound, "Not a shell accessibility element");
  if (!presented_ || controller.legacyModalSurfaceActive())
    return core::failure(core::ErrorCode::InvalidState,
                         "The redesigned editor is not on screen; this control is unavailable");
  // Validate against the current layout, capabilities and state, not the last painted tree: a
  // stale id (a knob removed by the rail layout), an unknown id, a disabled control or an
  // unsupported action is refused before anything reaches the document.
  refreshSemantics(controller);
  return semantics_.dispatch(
      id, action, [this, &controller](std::string_view target, SemanticAction requested) {
        return performSemantic(controller, target, requested);
      });
}

core::Result<void> SingShell::performSemantic(NativeEditorController& controller,
                                              std::string_view id, SemanticAction action) {
  if (action == SemanticAction::SetFocus) {
    takeSemanticFocus(controller, std::string{id});
    repaint();
    return core::success();
  }
  const auto activate = action == SemanticAction::Activate || action == SemanticAction::Toggle;
  core::Result<void> result = core::failure(core::ErrorCode::Unsupported,
                                            "This element does not support that action");
  if (id == "shell.workspace.sing" && activate) {
    setWorkspace(controller, Workspace::Sing);
    result = core::success();
  } else if (id == "shell.workspace.export" && activate) {
    setWorkspace(controller, Workspace::Export);
    result = core::success();
  } else if (id == "shell.workspace.voice" && activate) {
    controller.showVoicebankBrowser();
    result = core::success();
  } else if (id == "shell.export.run" && activate) {
    result = runExportSet(controller);
  } else if (id == "shell.mode.emo" && activate) {
    setMode(DesignMode::Emo);
    result = core::success();
  } else if (id == "shell.mode.scene" && activate) {
    setMode(DesignMode::Scene);
    result = core::success();
  } else if (id == "shell.settings" && activate) {
    controller.showAudioSettings();
    result = core::success();
  } else if (id == "shell.change-voice" && activate) {
    controller.showVoicebankBrowser();
    result = core::success();
  } else if (id == "shell.classic" && activate) {
    setEnabled(controller, false);
    result = core::success();
  } else if (id.starts_with("shell.lane-tab.") && activate) {
    static constexpr std::array<std::string_view, 6U> kChannels{"formant", "breath", "tension",
                                                                "air", "gender", "growl"};
    const auto name = id.substr(std::string_view{"shell.lane-tab."}.size());
    if (name == "dynamics") result = controller.openDynamicsInspector();
    for (std::size_t i = 0U; i < kChannels.size(); ++i)
      if (name == kChannels[i]) result = controller.openExpressionLane(ui::expressionChannelAt(i));
  } else if (id.starts_with("shell.knob.")) {
    static constexpr std::array<std::string_view, 6U> kKnobs{"formant", "breath", "tension",
                                                             "air", "gender", "growl"};
    const auto name = id.substr(std::string_view{"shell.knob."}.size());
    for (std::size_t i = 0U; i < kKnobs.size(); ++i) {
      if (name != kKnobs[i]) continue;
      if (knobRefused_[i]) {
        result = core::failure(core::ErrorCode::Unsupported,
                               "The selected singer cannot apply this control");
      } else if (action == SemanticAction::Increment) {
        result = nudge(controller, i, 1);
      } else if (action == SemanticAction::Decrement) {
        result = nudge(controller, i, -1);
      } else if (activate) {
        result = controller.openExpressionLane(ui::expressionChannelAt(i));
      }
    }
  }
  yieldIfModal(controller);
  repaint();
  return result;
}

}  // namespace seam::native_ui::design
