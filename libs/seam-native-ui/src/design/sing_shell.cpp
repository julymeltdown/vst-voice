#include "seam/native_ui/design/sing_shell.hpp"

#include "seam/native_ui/diagnostic_presentation.hpp"
#include "seam/native_ui/render_status_panel.hpp"
#include "seam/native_ui/voice_identity.hpp"
#include "seam/ui/expression_lane.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <random>
#include <string>
#include <system_error>
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
  }
  return knobs;
}

// A persisted value in the unit a musician reads: semitones stay semitones, shares become percent.
std::pair<std::string, std::string> displayValue(const KnobModel& knob) {
  if (knob.descriptor.unit == "semitones") return {format("%.1f", knob.value), "ST"};
  return {format("%.1f", knob.value * 100.0), "%"};
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
  forwarding_ = false;
  knobDrag_.reset();
  if (persist && persist_) saveDesignPreferences(preferences_);
  repaint();
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

TextInputRequest SingShell::translateTextInput(TextInputRequest request) const noexcept {
  if (presented_) request.logicalBounds = fromLegacy(request.logicalBounds);
  return request;
}

bool SingShell::inMusicalArea(ui::Point point) const noexcept {
  return contains(layout_.grid, point) || contains(layout_.ruler, point);
}

PointerEvent SingShell::translated(const PointerEvent& event) const noexcept {
  auto copy = event;
  copy.position = toLegacy(event.position);
  // The shell ruler is shorter than the legacy ruler; never let a ruler point reach the legacy
  // toolbar row above it.
  const auto legacyRulerTop = legacyContentTop_ - EditorSceneLayout{}.rulerHeight;
  if (contains(layout_.ruler, event.position))
    copy.position.y = std::max(copy.position.y, legacyRulerTop + 1.0);
  return copy;
}

void SingShell::syncHostedGrid(NativeEditorController& controller) const noexcept {
  if (presented_)
    controller.setHostedGrid(legacyContentTop_ + layout_.grid.height);
  else if (!forwarding_)
    controller.setHostedGrid(std::nullopt);
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

bool SingShell::paint(RasterCanvas& canvas, ui::PianoRollModel& model,
                      const EditorSceneState& state, time::Tick playhead) {
  presented_ = false;
  if (!enabled() || !available() || legacySurfaceRequired(state)) return false;
  layout_ = solveSingLayout(canvas.logicalWidth(), canvas.logicalHeight());
  const auto& t = tokensFor(preferences_.mode, preferences_.contrast);
  model.setViewport(ui::PianoRollViewport{
      .bounds = ui::Rect{0.0, 0.0, layout_.grid.right(), layout_.grid.height},
      .keyboardWidth = layout_.grid.x,
  });
  model.rebuildIndex();
  ppq_ = time::Tick{model.timeline().ppq()}.value();
  ensureBackground(canvas, t);
  auto& surface = canvas.surface();
  if (backgroundValid_) {
    std::copy(background_.pixels().begin(), background_.pixels().end(), surface.pixels().begin());
  } else {
    surface.clear(t.color.canvas);
  }
  auto c = paint::makeCanvas(surface, canvas.scale());
  if (!c) return false;
  const auto knobs = knobModels(state);
  for (std::size_t i = 0U; i < knobs.size(); ++i) knobRefused_[i] = !knobs[i].refusal.empty();
  paintHeader(*c, t, state, playhead);
  paintEditor(*c, t, model, state);
  paintLane(*c, t, model, state);
  paintRack(*c, t, state);
  paintStatus(*c, t, state);
  if (state.focusedElementBounds.has_value()) {
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
  presented_ = true;
  return true;
}

void SingShell::paintHeader(Canvas2D& c, const DesignTokens& t, const EditorSceneState& state,
                            time::Tick playhead) const {
  const auto& l = layout_;
  // Workspace tabs. Only SING is functional in this build; the others are shown disabled.
  static constexpr std::array<Icon, 5U> kIcons{Icon::Sing, Icon::Voice, Icon::Tune, Icon::Mix,
                                               Icon::Export};
  static constexpr std::array<const char*, 5U> kNames{"Sing", "Voice", "Tune", "Mix", "Export"};
  for (std::size_t i = 0U; i < l.workspaceTab.size(); ++i) {
    const auto tab = l.workspaceTab[i];
    if (tab.width < 24.0) continue;
    const auto active = i == 0U;
    const auto color = active ? t.color.accent : withAlpha(t.color.textSecondary, 0.45);
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
             active ? t.color.accent : withAlpha(t.color.textSecondary, 0.55));
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
  if (const auto needed = c.measure("888:8:888", readout); needed > l.positionReadout.width - 4.0)
    readout = style(FontRole::Mono,
                    std::max(12.0, t.type.transport * (l.positionReadout.width - 4.0) / needed), 0.5);
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
  c.text(l.tempoReadout, format("%.0f BPM", state.tempoBpm), small, tempoColor);
  c.text(l.meterReadout,
         std::to_string(state.meter.numerator) + "/" + std::to_string(state.meter.denominator),
         small, tempoColor);

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
    if (note.selected) {
      c.save();
      c.setGlow(withAlpha(t.color.noteSelectedA, 0.85), 10.0);
      c.fill(p, LinearGradient{{b.x, b.y}, {b.right(), b.bottom()},
                               {{0.0, t.color.noteSelectedA}, {1.0, t.color.noteSelectedB}}});
      c.restore();
      c.stroke(p, t.color.noteSelectedStroke, StrokeStyle{1.2});
    } else {
      c.fill(p, LinearGradient{{b.x, b.y}, {b.x, b.bottom()},
                               {{0.0, note.midiKey % 2U == 0U ? t.color.noteFillAlt : t.color.noteFill},
                                {1.0, t.color.surfaceSunken}}});
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

  if (notes.empty()) {
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
    c.text(tab, kTabs[i], style(FontRole::UiSemibold, t.type.smallLabel, 1.0, TextAlign::Center, true),
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
  const auto range = std::max(1e-6, static_cast<double>(e.maximum - e.minimum));
  const auto yFor = [&](double v) {
    return plot.bottom() - 6.0 - (std::clamp(v, static_cast<double>(e.minimum),
                                             static_cast<double>(e.maximum)) - e.minimum) /
                                     range * (plot.height - 12.0);
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
    Path curve;
    Path area;
    const auto first = e.points.front();
    const auto firstX = plot.x + timeline.tickToPixel(first.tick);
    curve.moveTo({plot.x, yFor(first.amount)}).lineTo({firstX, yFor(first.amount)});
    area.moveTo({plot.x, plot.bottom()}).lineTo({plot.x, yFor(first.amount)})
        .lineTo({firstX, yFor(first.amount)});
    for (std::size_t i = 1U; i < e.points.size(); ++i) {
      const ui::Point p{plot.x + timeline.tickToPixel(e.points[i].tick), yFor(e.points[i].amount)};
      curve.lineTo(p);
      area.lineTo(p);
    }
    const auto lastY = yFor(e.points.back().amount);
    curve.lineTo({plot.right(), lastY});
    area.lineTo({plot.right(), lastY}).lineTo({plot.right(), plot.bottom()}).close();
    c.fill(area, LinearGradient{{0.0, plot.y}, {0.0, plot.bottom()},
                                {{0.0, withAlpha(t.color.laneFillTop, 0.42)},
                                 {1.0, withAlpha(t.color.laneFillBottom, 0.05)}}});
    c.save();
    c.setGlow(withAlpha(t.color.laneFillTop, 0.9), 8.0);
    c.stroke(curve, t.color.laneFillTop, StrokeStyle{2.0});
    c.restore();
    for (const auto& point : e.points) {
      const ui::Point p{plot.x + timeline.tickToPixel(point.tick), yFor(point.amount)};
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
    const ui::Point kc{cell.x + cell.width * 0.5, cell.y + 20.0 + 30.0};
    const auto r = 28.0;
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
    c.text({cell.x, kc.y + r + 2.0, cell.width, 14.0},
           refused ? std::string{"Unavailable"} : std::string{unitText},
           style(FontRole::UiMedium, 10.5, 1.0, TextAlign::Center, true),
           refused ? t.color.warning : t.color.textDisabled);
    if (k.storedPoints > 0U && !refused)
      c.fill(Path::capsule({kc.x - 7.0, kc.y + r + 17.0, 14.0, 3.0}), t.color.accent);
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
  syncHostedGrid(controller);
  if (!presented_) return controller.pointerDown(event);
  const auto p = event.position;
  const auto& l = layout_;
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
      setEnabled(false);
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
        knobDrag_ = KnobDrag{i, p.y, 0, false};
        return core::success();
      }
    }
    const auto tabWidth = std::min(104.0, l.laneTabs.width / 9.0);
    for (std::size_t i = 0U; i < 7U; ++i) {
      const ui::Rect tab{l.laneTabs.x + i * (tabWidth + 4.0), l.laneTabs.y, tabWidth,
                         l.laneTabs.height};
      if (!contains(tab, p)) continue;
      if (i == 0U) return controller.openDynamicsInspector();
      return controller.openExpressionLane(ui::expressionChannelAt(i - 1U));
    }
  }
  if (inMusicalArea(p)) {
    forwarding_ = true;
    return controller.pointerDown(translated(event));
  }
  return core::success();
}

core::Result<void> SingShell::pointerMove(NativeEditorController& controller,
                                          const PointerEvent& event) {
  syncHostedGrid(controller);
  if (!presented_ && !forwarding_) return controller.pointerMove(event);
  if (knobDrag_) {
    const auto steps = static_cast<int>(std::lround((knobDrag_->startY - event.position.y) / 12.0));
    if (steps != knobDrag_->steps) {
      knobDrag_->steps = steps;
      knobDrag_->moved = true;
      repaint();
    }
    return core::success();
  }
  if (forwarding_ || inMusicalArea(event.position)) return controller.pointerMove(translated(event));
  return core::success();
}

core::Result<void> SingShell::pointerUp(NativeEditorController& controller,
                                        const PointerEvent& event) {
  syncHostedGrid(controller);
  if (knobDrag_) {
    const auto drag = *knobDrag_;
    knobDrag_.reset();
    repaint();
    // One gesture is one command, so one undo step.
    if (drag.steps != 0) return nudge(controller, drag.index, drag.steps);
    return controller.openExpressionLane(ui::expressionChannelAt(drag.index));
  }
  if (forwarding_) {
    forwarding_ = false;
    return controller.pointerUp(translated(event));
  }
  if (!presented_) return controller.pointerUp(event);
  return core::success();
}

bool SingShell::scroll(NativeEditorController& controller, double deltaX, double deltaY,
                       ui::Point anchor, InputModifiers modifiers) {
  syncHostedGrid(controller);
  if (!presented_) return false;
  if (layout_.rack == RackPresentation::Full) {
    for (std::size_t i = 0U; i < layout_.knob.size(); ++i) {
      if (!contains(layout_.knob[i], anchor)) continue;
      if (knobRefused_[i]) return true;
      scrollAccumulator_ += deltaY;
      const auto steps = static_cast<int>(scrollAccumulator_ / 8.0);
      if (steps != 0) {
        scrollAccumulator_ -= steps * 8.0;
        static_cast<void>(nudge(controller, i, -steps));
      }
      return true;
    }
  }
  if (inMusicalArea(anchor) || contains(layout_.laneTimePlot, anchor)) {
    controller.scroll(deltaX, deltaY, toLegacy(anchor), modifiers);
    return true;
  }
  return true;
}

bool SingShell::handleShellKey(const KeyEvent& event) {
  if (event.key == NativeKey::Space && event.modifiers.shift && event.modifiers.primaryShortcut()) {
    setEnabled(!enabled());
    return true;
  }
  return false;
}

}  // namespace seam::native_ui::design
