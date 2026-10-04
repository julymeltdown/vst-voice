#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/native_ui/design/background_wash.hpp"
#include "seam/native_ui/design/shell_strings.hpp"

#include "seam/native_ui/diagnostic_presentation.hpp"
#include "seam/native_ui/render_status_panel.hpp"
#include "seam/native_ui/voice_identity.hpp"
#include "seam/native_ui/list_entry_ids.hpp"
#include "seam/ui/expression_lane.hpp"
#include "seam/ui/phoneme_lane_model.hpp"
#include "seam/native_ui/paint/qoi.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
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

// Bind a changing overlay's identity to the exact panel and hit rectangles that were painted.
// Content identity alone is not enough when a resize or responsive reflow maps the same list rows
// to different points on screen.
std::optional<std::uint64_t> drawnOverlayFingerprint(
    const ShellOverlay& overlay, const NativeEditorController& controller,
    const EditorSceneState& state, const SingLayout& layout, ui::Rect panel) {
  if (panel.width <= 0.0 || panel.height <= 0.0) return std::nullopt;
  const auto content = overlay.drawnContentForInput(controller, state, layout, panel);
  if (!content.has_value()) return std::nullopt;

  IdentityHash hash;
  hash.number(*content);
  const auto addRect = [&hash](ui::Rect rect) {
    hash.number(std::bit_cast<std::uint64_t>(rect.x));
    hash.number(std::bit_cast<std::uint64_t>(rect.y));
    hash.number(std::bit_cast<std::uint64_t>(rect.width));
    hash.number(std::bit_cast<std::uint64_t>(rect.height));
  };
  addRect(panel);
  for (const auto& control : overlay.controls(controller, state, layout, panel)) {
    hash.text(control.id);
    addRect(control.bounds);
    hash.number(static_cast<std::uint64_t>(control.role));
    hash.number(control.enabled ? 1U : 0U);
    hash.number(control.selected ? 1U : 0U);
    hash.number(control.activatable ? 1U : 0U);
  }
  return hash.value();
}

// Append independent subpaths without joining their edges. Used only for adjacent draw calls
// whose shapes cannot overlap; their individual coverage and paint order are then unchanged.
void appendSubpaths(Path& destination, const Path& source) {
  for (const auto& element : source.elements()) {
    switch (element.verb) {
      case Path::Verb::Move: destination.moveTo(element.a); break;
      case Path::Verb::Line: destination.lineTo(element.a); break;
      case Path::Verb::Quad: destination.quadTo(element.a, element.b); break;
      case Path::Verb::Cubic: destination.cubicTo(element.a, element.b, element.c); break;
      case Path::Verb::Close: destination.close(); break;
    }
  }
}

// Reference painter for pixel-fidelity diagnostics. It follows the original random stream and
// path construction exactly; the production path below writes these same shapes in software.
void paintVectorWash(Canvas2D& c, const DesignTokens& t) {
  const auto W = c.width();
  const auto H = c.height();
  c.fill(Path::rect({0.0, 0.0, W, H}), t.color.canvas);
  std::mt19937 rng{t.mode == DesignMode::Emo ? 0x5EA1u : 0x5CE7u};
  const auto uniform = [&](double lo, double hi) {
    return std::uniform_real_distribution<double>{lo, hi}(rng);
  };
  if (t.mode == DesignMode::Emo) {
    c.fill(Path::rect({0, 0, W, H}),
           RadialGradient{{W * 0.18, H * 0.05}, W * 0.55,
                          {{0.0, withAlpha(t.color.accentDeep, 0.35)}, {1.0, withAlpha(kBlack, 0.0)}}});
    c.fill(Path::rect({0, 0, W, H}),
           RadialGradient{{W * 0.86, H * 0.42}, W * 0.40,
                          {{0.0, withAlpha(t.color.accent, 0.18)}, {1.0, withAlpha(kBlack, 0.0)}}});
    if (t.light.textureAlpha > 0.0) for (int i = 0; i < 170; ++i) {
      Path strand;
      const ui::Point a{uniform(-0.1, 1.1) * W, uniform(-0.1, 1.1) * H};
      const ui::Point d{uniform(-0.1, 1.1) * W, uniform(-0.1, 1.1) * H};
      strand.moveTo(a).cubicTo({uniform(0, W), uniform(0, H)}, {uniform(0, W), uniform(0, H)}, d);
      const auto red = uniform(0.0, 1.0) < 0.7;
      c.stroke(strand, withAlpha(red ? t.color.texturePrimary : t.color.textureSecondary,
                                 uniform(0.25, 1.0) * t.light.textureAlpha * (red ? 1.9 : 0.8)),
               StrokeStyle{uniform(0.5, 1.4)});
    }
  } else {
    c.fill(Path::rect({0, 0, W, H}),
           RadialGradient{{W * 0.12, H * 0.1}, W * 0.50,
                          {{0.0, withAlpha(t.color.accentAlt1, 0.32)}, {1.0, withAlpha(kBlack, 0.0)}}});
    c.fill(Path::rect({0, 0, W, H}),
           RadialGradient{{W * 0.92, H * 0.85}, W * 0.45,
                          {{0.0, withAlpha(t.color.accent, 0.22)}, {1.0, withAlpha(kBlack, 0.0)}}});
    if (t.light.textureAlpha > 0.0) {
      const std::array<Color, 4U> sparkle{t.color.accent, t.color.accentCurve, t.color.accentAlt1, kWhite};
      for (int i = 0; i < 1400; ++i) {
        const ui::Point p{uniform(0, W), uniform(0, H)};
        const auto color = withAlpha(sparkle[static_cast<std::size_t>(uniform(0, 3.999))],
                                     uniform(0.15, 1.0) * t.light.textureAlpha * 4.0);
        if (uniform(0.0, 1.0) < 0.82) c.fill(Path::circle(p, uniform(0.35, 1.1)), color);
        else {
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
}

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

// How far past its bounds character artwork may draw: the ring's and the avatar's glows (at most
// 10 points of blur, which reaches about twice that) and their outlines.
constexpr double kGlowReach = 24.0;

ui::Rect grown(ui::Rect r, double by) noexcept {
  return {r.x - by, r.y - by, r.width + 2.0 * by, r.height + 2.0 * by};
}

// The blink lid's inputs, for a character item's hash: the drawn state's eye boxes and the skin
// tone the lid closes with. Both come from the package and its outfit, so a mode switch or a new
// package that moves or recolours the lid changes the item.
paint::ContentHash& addLid(paint::ContentHash& h, const std::vector<character::EyeBox>& eyes,
                           std::optional<Color> tone) noexcept {
  h.add(static_cast<std::uint64_t>(eyes.size()));
  for (const auto& eye : eyes) h.add(eye.x).add(eye.y).add(eye.width).add(eye.height);
  h.add(tone.has_value());
  if (tone.has_value()) h.add(*tone);
  return h;
}

// Whether paintCharacterPortrait draws a figure into this destination: the package's decoded state
// art when it has pixels, else the look's portrait. The recorded frame needs the answer before the
// portrait is drawn, because drawing happens at composition.
bool portraitDraws(ui::Rect destination, const PixelSurface* package, const paint::Image* look) {
  if (destination.width <= 0.0 || destination.height <= 0.0) return false;
  if (package != nullptr && package->width() > 0U && package->height() > 0U) return true;
  return look != nullptr && look->width() > 0U && look->height() > 0U;
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

// fitted() only shrinks to a 10pt floor and then lets the canvas truncate, which
// is how a track named "Diagnostic dry render..." became "DIAGNOSTIC DRY...".
// Truncating the tail throws away the part that identifies the track: the leading
// words are usually shared by every row in a bank, and the distinguishing part is
// at the end. This trims the middle instead, so both the family and the specific
// name stay readable inside a fixed 24pt chip.
std::string midEllipsis(Canvas2D& c, std::string_view text, const TextStyle& s,
                        double width) {
  if (width <= 0.0 || text.empty() || c.measure(text, s) <= width) return std::string{text};
  static constexpr std::string_view kMarker = "\xE2\x80\xA6";  // U+2026
  if (c.measure(kMarker, s) > width) return {};
  // Grow both ends together so the result stays balanced, keeping a character of
  // the tail whenever one fits: the tail is the part that distinguishes tracks.
  std::size_t head = 0;
  std::size_t tail = 0;
  while (head + tail < text.size()) {
    const std::size_t nextHead = head + 1U;
    const std::size_t nextTail = tail + 1U;
    const std::string candidate =
        std::string{text.substr(0, nextHead)} + std::string{kMarker} +
        std::string{text.substr(text.size() - nextTail)};
    if (c.measure(candidate, s) > width) break;
    head = nextHead;
    tail = nextTail;
  }
  if (head == 0U || tail == 0U) {
    std::size_t kept = 0;
    while (kept < text.size() &&
           c.measure(std::string{text.substr(0, kept + 1U)} + std::string{kMarker}, s) <= width) {
      ++kept;
    }
    return kept == 0U ? std::string{} : std::string{text.substr(0, kept)} + std::string{kMarker};
  }
  return std::string{text.substr(0, head)} + std::string{kMarker} +
         std::string{text.substr(text.size() - tail)};
}

// A glass panel's border and top highlight, drawn over its translucent fill.
void glassPanelEdges(Canvas2D& c, const DesignTokens& t, ui::Rect r, double radius) {
  if (r.width <= 0.0 || r.height <= 0.0) return;
  c.stroke(Path::roundedRect(r, radius), withAlpha(t.color.border, 0.95), StrokeStyle{1.0});
  Path highlight;
  highlight.moveTo({r.x + radius, r.y + 1.0}).lineTo({r.right() - radius, r.y + 1.0});
  c.stroke(highlight, withAlpha(kWhite, t.light.highlightAlpha * 1.6), StrokeStyle{1.0});
}

void glassPanel(Canvas2D& c, const DesignTokens& t, ui::Rect r, double radius,
                double alpha = 0.90) {
  if (r.width <= 0.0 || r.height <= 0.0) return;
  c.save();
  c.setAlpha(alpha);
  c.fill(Path::roundedRect(r, radius),
         LinearGradient{{r.x, r.y}, {r.x, r.bottom()},
                        {{0.0, t.color.surfaceRaised}, {1.0, t.color.surface}}});
  c.restore();
  glassPanelEdges(c, t, r, radius);
}

// The shell's background panels, in painting order. Their translucent fills are painted in
// software with the wash (paintGlassPanelFills); paintBackground draws their edges over them.
std::vector<GlassPanelFill> backgroundGlassPanels(const SingLayout& l, const DesignTokens& t) {
  std::vector<GlassPanelFill> fills;
  const auto add = [&](ui::Rect r, double radius, double opacity = 0.90) {
    if (r.width > 0.0 && r.height > 0.0)
      fills.push_back({r, radius, t.color.surfaceRaised, t.color.surface, opacity});
  };
  add(l.header, t.shape.hero);
  add(l.editor, t.shape.card);
  add(l.lane, t.shape.card);
  if (l.rack == RackPresentation::Full) {
    add(l.singer, t.shape.card);
    add(l.expression, t.shape.card);
    add(l.style, t.shape.card);
  } else {
    add(l.rackArea, t.shape.card);
  }
  add(l.status, 10.0, 0.80);
  return fills;
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
         style(t.type.heading, t.type.panelTitle, t.type.panelTitleTracking,
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
  static constexpr std::array<Str, 6U> kLabels{Str::Formant, Str::Breath, Str::Tension,
                                                       Str::Air, Str::Gender, Str::Growl};
  for (std::size_t i = 0U; i < knobs.size(); ++i) {
    const auto channel = ui::expressionChannelAt(i);
    knobs[i].descriptor = ui::describeExpressionChannel(channel);
    knobs[i].label = tr(kLabels[i]);
    knobs[i].value = knobs[i].descriptor.neutral;
    knobs[i].refusal = state.inspector.valid ? "" : tr(Str::NoVocalTrackIsSelected);
    for (const auto& row : state.inspector.expressionCapabilities) {
      if (row.channel != channel) continue;
      knobs[i].value = row.valueAtPlayhead;
      knobs[i].refusal = row.refusal;
      knobs[i].storedPoints = row.storedPoints;
    }
    // The knob still reads the region's edge value, but an edit "at the playhead" is refused.
    if (knobs[i].refusal.empty() && !state.playheadInsideRegion)
      knobs[i].refusal = tr(Str::ThePlayheadIsOutsideTheSelected);
  }
  return knobs;
}

// A persisted value in the unit a musician reads: semitones stay semitones, shares become percent.
std::pair<std::string, std::string> displayValue(const KnobModel& knob) {
  if (knob.descriptor.unit == "semitones") return {format("%.1f", knob.value), tr(Str::ST)};
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

ui::Rect singNoteCapsuleBounds(const ui::NoteVisual& note, ui::Rect grid) noexcept {
  auto b = note.bounds;
  const auto inset = std::min(std::max(0.0, b.height) * 0.1,
                              note.overlapMemberCount > 1U ? 0.35 : 1.5);
  b.y += grid.y + inset;
  b.height = std::max(0.0, b.height - 2.0 * inset);
  return b;
}

ui::Rect singLyricEditorBounds(ui::Rect note, double textWidth, ui::Rect grid) noexcept {
  const auto wanted = std::max({note.width, kLyricEditorMinWidth,
                                std::max(0.0, textWidth) + 2.0 * kLyricEditorInset +
                                    kLyricEditorCaretRoom});
  const auto width = std::min(wanted, std::max(0.0, grid.width));
  const auto height = std::min(std::max(note.height, kLyricEditorMinHeight), std::max(0.0, grid.height));
  const auto x = std::max(grid.x, std::min(note.x, grid.right() - width));
  const auto centred = note.y + (note.height - height) * 0.5;
  const auto y = std::max(grid.y, std::min(centred, grid.bottom() - height));
  return {x, y, width, height};
}

std::vector<SingOverlapBadge> layoutSingOverlapBadges(
    const std::vector<ui::NoteVisual>& notes, ui::Rect grid) {
  std::vector<SingOverlapBadge> result;
  if (grid.width < 44.0 || grid.height < 24.0) return result;
  struct Group final { ui::Rect bounds; std::size_t members{0U}; };
  std::map<std::size_t, Group> groups;
  std::vector<ui::Rect> occupied;
  occupied.reserve(notes.size());
  for (const auto& note : notes) {
    auto b = note.bounds;
    b.y += grid.y;
    if (!note.hiddenByOverlapDensity) occupied.push_back(b);
    if (note.overlapMemberCount < 2U) continue;
    auto full = note.timelineBounds;
    full.y += grid.y;
    const auto [it, inserted] = groups.try_emplace(note.overlapGroup, Group{full, note.overlapMemberCount});
    if (!inserted) {
      auto& bounds = it->second.bounds;
      const auto right = std::max(bounds.right(), full.right());
      const auto bottom = std::max(bounds.bottom(), full.bottom());
      bounds.x = std::min(bounds.x, full.x);
      bounds.y = std::min(bounds.y, full.y);
      bounds.width = right - bounds.x;
      bounds.height = bottom - bounds.y;
    }
  }
  const auto overlapArea = [](ui::Rect a, ui::Rect b) {
    return std::max(0.0, std::min(a.right(), b.right()) - std::max(a.x, b.x)) *
           std::max(0.0, std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y));
  };
  for (const auto& [id, group] : groups) {
    // Prefer the end of the whole group, not the first member's end, which can lie under another
    // note. Nearby free slots preserve the connection to the stack without moving musical data.
    const auto width = std::min(grid.width, std::max(36.0,
        18.0 + 8.0 * static_cast<double>(std::to_string(group.members).size())));
    const auto& b = group.bounds;
    const std::array<ui::Point, 6U> positions{{
        {b.right() + 6.0, b.y + (b.height - 24.0) * 0.5},
        {b.right() - width, b.y - 26.0}, {b.x, b.y - 26.0},
        {b.right() - width, b.bottom() + 2.0}, {b.x, b.bottom() + 2.0},
        {b.x - width - 6.0, b.y + (b.height - 24.0) * 0.5}}};
    ui::Rect chosen;
    auto leastOverlap = std::numeric_limits<double>::max();
    for (const auto& p : positions) {
      const ui::Rect candidate{std::clamp(p.x, grid.x, grid.right() - width),
                                std::clamp(p.y, grid.y, grid.bottom() - 24.0), width, 24.0};
      double overlap = 0.0;
      for (const auto& note : occupied) overlap += overlapArea(candidate, note);
      for (const auto& badge : result) overlap += overlapArea(candidate, badge.bounds) * 4.0;
      if (overlap < leastOverlap) { chosen = candidate; leastOverlap = overlap; }
      if (overlap == 0.0) break;
    }
    result.push_back({id, group.members, chosen});
  }
  return result;
}

void SingShell::activate(const std::filesystem::path& assetRoot) {
  const auto captureProfile = std::getenv("SEAM_UI_CAPTURE_PROFILE") != nullptr;
  // Evidence uses explicit English, motion and contrast defaults, without reading or writing the
  // shared user preference suite. The project still owns its character display setting.
  auto preferences = captureProfile ? DesignPreferences{} : loadDesignPreferences();
  if (const char* forced = std::getenv("SEAM_UI_DESIGN"); forced != nullptr) {
    // Captures only: force a look (emo or scene) without touching the saved preference.
    preferences.mode = parseDesignMode(forced, preferences.mode);
  }
  if (const char* contrast = std::getenv("SEAM_UI_CONTRAST"); contrast != nullptr) {
    const std::string_view value{contrast};
    if (value == "standard" || value == "high") {
      preferences.contrast = value == "high" ? Contrast::High : Contrast::Standard;
      preferences.contrastFollowsSystem = false;
    }
  }
  // Captures only: open a workspace other than SING. The workspace is never a saved preference.
  if (const char* workspace = std::getenv("SEAM_UI_WORKSPACE"); workspace != nullptr) {
    const std::string_view name{workspace};
    if (name == "export") workspace_ = Workspace::Export;
    if (name == "tune") workspace_ = Workspace::Tune;
    if (name == "mix") workspace_ = Workspace::Mix;
    if (name == "voice") workspace_ = Workspace::Voice;
  }
  // Captures only: start with the compact inspector open (ignored at the full rack).
  if (const char* inspector = std::getenv("SEAM_UI_INSPECTOR");
      inspector != nullptr && std::string_view{inspector} == "open")
    inspectorWanted_ = true;
  // Captures only: freeze the animation clock, so two runs of the same build draw the same frame
  // (the packet's reproducibility rule). The clock is the host's injectable one, so this freezes the
  // blink, the breathing, the ring's phase and every tween together, without changing what is drawn.
  if (std::getenv("SEAM_UI_FREEZE_CLOCK") != nullptr)
    setUiClock([] { return std::chrono::steady_clock::time_point{} + std::chrono::seconds{10}; });
  activate(assetRoot, preferences);
  persist_ = !captureProfile;
}

void SingShell::activate(const std::filesystem::path& assetRoot, DesignPreferences preferences) {
  preferences_ = preferences;
  active_ = true;
  persist_ = false;
  ++artGeneration_;
  layers_.invalidate();
  // The translation files sit beside the design assets; the preferences name the language.
  translations_ = locateShellTranslations(assetRoot);
  uninstallShellStrings(strings_.get());
  strings_.reset();
  stringsLanguage_ = shellLanguages().front().code;
  languageReport_ = {};
  applyLanguage();
  // An Increase Contrast change in System Settings repaints an idle editor: a frame reads the
  // system setting itself, but nothing else would ask for one.
  if (available()) displayOptionsObservation_ = observeSystemDisplayOptions([this] { repaint(); });
  // The bundled faces are registered for this process when the shell is activated (the standalone
  // app and the plug-in both activate one), before the first frame measures any text.
  if (available()) static_cast<void>(paint::bundledFonts());
  if (assetRoot.empty() || !available()) return;
  for (const auto mode : {DesignMode::Emo, DesignMode::Scene}) {
    const auto folder = assetRoot / std::string{designModeName(mode)};
    auto& slot = assets_[mode == DesignMode::Scene ? 1U : 0U];
    slot.portrait = paint::loadImage(folder / "portrait.png");
    slot.stage = paint::loadImage(folder / "stage.png");
    slot.wordmark = paint::loadImage(folder / "wordmark.png");
    slot.splash = paint::loadImage(folder / "splash.png");
  }
  // The character package is optional artwork over the look: when none is present the shell draws
  // this look's own portrait and invents no state, so a missing package changes nothing it claims.
  setCharacterPackage(locateCharacterAssets(assetRoot));
}

void SingShell::setCharacterPackage(const std::filesystem::path& packageRoot) {
  static_cast<void>(character_.loadPackage(packageRoot));
  listeningPortrait_.reset();
  listeningPortraitPath_.clear();
  // A mode with its own state set in the package draws it; any other mode draws the shared set.
  character_.setOutfit(std::string{designModeName(preferences_.mode)});
  // Recorded portraits are hashed by identity: a reload may reuse an address, so every layer that
  // drew the previous package is dropped with it.
  ++artGeneration_;
  stagePlacement_.reset();
  stageFade_.reset();
  repaint();
}

const PixelSurface* SingShell::characterPortrait(CharacterState state) const {
  if (characterDisplay_ == domain::CharacterDisplayMode::Off) return nullptr;
  return character_.portrait(state);
}

// The look's own portrait, the fallback for a package that has none; nothing when the display is Off.
const paint::Image* SingShell::lookPortrait() const {
  if (characterDisplay_ == domain::CharacterDisplayMode::Off) return nullptr;
  return assets().portrait.get();
}

const PixelSurface* SingShell::characterMouth(character::MouthShape shape) const {
  return character_.mouth(shape);
}

std::shared_ptr<const paint::Image> SingShell::lookListeningPortrait() const {
  const auto* package = character_.package();
  if (package == nullptr) {
    listeningPortrait_.reset();
    listeningPortraitPath_.clear();
    return {};
  }
  const auto path = package->manifest.layered()
      ? package->posePath(character::Pose::Listening, character_.outfit())
      : characterStateAssetPath(*package, CharacterState::Listening, character_.outfit());
  if (path.empty()) {
    listeningPortrait_.reset();
    listeningPortraitPath_.clear();
    return {};
  }
  if (listeningPortrait_ != nullptr && listeningPortraitPath_ == path) return listeningPortrait_;
  listeningPortraitPath_ = path;
  ++artGeneration_;
  // The hero's pose is decoded once through the relevant PPM or QOI path and then uses the same
  // masking and filtering as the look's artwork.
  if (path.extension() == ".qoi") {
    auto pixels = paint::loadQoi(path);
    listeningPortrait_ = pixels ? paint::imageFromPixels(pixels.value()) : nullptr;
  } else {
    listeningPortrait_ = paint::loadImage(path);
  }
  return listeningPortrait_;
}

CharacterCanvas SingShell::characterCanvas(paint::Canvas2D& vector) const {
  return CharacterCanvas{vector, *raster_};
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

std::filesystem::path locateCharacterAssets(const std::filesystem::path& designAssets,
                                            const std::filesystem::path& bundleResources) {
  std::vector<std::filesystem::path> candidates;
  if (const char* root = std::getenv("SEAM_CHARACTER_ASSETS"); root != nullptr && *root != '\0')
    candidates.emplace_back(root);
  if (!designAssets.empty()) {
    // The design assets and the character package sit beside each other in the source tree and in a
    // bundle's Resources directory, so one location answers for both.
    candidates.push_back(designAssets.parent_path() / "character-01");
  }
  if (!bundleResources.empty()) candidates.push_back(bundleResources / "character-01");
  if (const auto own = paint::codeBundleResources(); !own.empty())
    candidates.push_back(own / "character-01");
#if defined(SEAM_UI_DESIGN_SOURCE_ASSETS)
  candidates.emplace_back(std::filesystem::path{SEAM_UI_DESIGN_SOURCE_ASSETS}.parent_path() /
                          "character-01");
#endif
  for (const auto& candidate : candidates) {
    std::error_code error;
    if (std::filesystem::is_regular_file(candidate / "manifest.json", error)) return candidate;
  }
  return {};
}

std::filesystem::path locateShellTranslations(const std::filesystem::path& designAssets) {
  std::vector<std::filesystem::path> candidates;
  if (const char* root = std::getenv("SEAM_L10N_ASSETS"); root != nullptr && *root != '\0')
    candidates.emplace_back(root);
  // Beside the design assets, as the character package is: assets/l10n in the source tree and
  // Resources/l10n in a bundle.
  if (!designAssets.empty()) candidates.push_back(designAssets.parent_path() / "l10n");
  if (const auto own = paint::codeBundleResources(); !own.empty()) candidates.push_back(own / "l10n");
#if defined(SEAM_UI_DESIGN_SOURCE_ASSETS)
  candidates.emplace_back(std::filesystem::path{SEAM_UI_DESIGN_SOURCE_ASSETS}.parent_path() / "l10n");
#endif
  for (const auto& candidate : candidates) {
    std::error_code error;
    if (std::filesystem::is_directory(candidate, error)) return candidate;
  }
  return {};
}

bool SingShell::rehomedSurface(OverlayKind kind) noexcept {
  switch (kind) {
    case OverlayKind::SampleMicroscope:
    case OverlayKind::PhonemeReview:
    case OverlayKind::TimeMap:
    case OverlayKind::RecoverySupport:
    case OverlayKind::OverlapDetail:
    case OverlayKind::Diagnostics:
    case OverlayKind::ReplacementReview:
    case OverlayKind::AudioSettings:
    case OverlayKind::Settings:
    case OverlayKind::VoicebankBrowser:
    case OverlayKind::SingerMenu:
    case OverlayKind::About:
    case OverlayKind::TextField: return true;
    case OverlayKind::None: return false;
  }
  return false;
}

const ShellOverlay* SingShell::activeOverlay(const NativeEditorController& controller) const {
  if (!presented_) return nullptr;
  return activeOverlay(controller, controller.sceneState());
}

const ShellOverlay* SingShell::activeOverlay(const NativeEditorController& controller,
                                             const EditorSceneState& state) const {
  if (!presented_) return nullptr;
  // The microscope is modal above everything, as the classic painter drew it last; an open text
  // field is next (it belongs to whatever opened it, which it replaces while it is open); then the
  // surfaces the shell opens over the score. Only one is ever shown.
  for (const auto* overlay :
       {microscopeOverlay_.get(), fieldOverlay_.get(), overlapOverlay_.get(), reviewOverlay_.get(),
       voicebankOverlay_.get(), settingsOverlay_.get(), audioOverlay_.get(), timeMapOverlay_.get(), phonemeOverlay_.get(),
        supportOverlay_.get(), diagnosticsOverlay_.get(), singerMenuOverlay_.get(),
        aboutOverlay_.get()}) {
    if (overlay == nullptr || !overlay->wanted(controller, state)) continue;
    // The DIAGNOSTICS popover is the shell's own presentation, so it needs its flag.
    if (overlay->kind() == OverlayKind::Diagnostics && !diagnosticsOpen_) continue;
    // So is the singer menu, which is last: any surface the controller opens is above it.
    if (overlay->kind() == OverlayKind::SingerMenu && !singerMenuOpen_) continue;
    // And the About sheet, which only opens when nothing else is up.
    if (overlay->kind() == OverlayKind::About && !aboutOpen_) continue;
    if (overlay->kind() == OverlayKind::Settings && !settingsOpen_) continue;
    if (overlay->panel(controller, state, layout_, overlaySlot(controller, state)).width <= 0.0)
      continue;
    return overlay;
  }
  return nullptr;
}

OverlayKind SingShell::overlayKind(const NativeEditorController& controller) const {
  const auto* overlay = activeOverlay(controller);
  return overlay == nullptr ? OverlayKind::None : overlay->kind();
}

bool SingShell::overlayPresented(const NativeEditorController& controller) const {
  return activeOverlay(controller) != nullptr;
}

void SingShell::setMode(DesignMode mode, bool persist) {
  if (preferences_.mode != mode) {
    modePrevious_ = !preferences_.reduceMotion && layers_.contentSnapshot() != nullptr
                        ? paint::imageFromPixels(*layers_.contentSnapshot()) : nullptr;
    modeTween_.start(uiNow(), std::chrono::milliseconds{200},
                     preferences_.reduceMotion || modePrevious_ == nullptr);
    tabPrevious_.reset();
    tabTween_.reset();
  }
  preferences_.mode = mode;
  // The mode's outfit draws its own state set. Switching it drops the decoded portraits, and a new
  // decode may reuse a freed address, so the art the recorded layers hashed by identity goes too.
  const auto outfit = character_.outfit();
  character_.setOutfit(std::string{designModeName(mode)});
  if (character_.outfit() != outfit) ++artGeneration_;
  if (persist && persist_) saveDesignPreferences(preferences_);
  repaint();
}

void SingShell::setContrast(Contrast contrast, bool persist) {
  preferences_.contrast = contrast;
  preferences_.contrastFollowsSystem = false;
  if (persist && persist_) saveDesignPreferences(preferences_);
  repaint();
}

void SingShell::followSystemContrast(bool persist) {
  preferences_.contrastFollowsSystem = true;
  preferences_.contrast = systemIncreaseContrast() ? Contrast::High : Contrast::Standard;
  if (persist && persist_) saveDesignPreferences(preferences_);
  repaint();
}

void SingShell::setReduceMotion(bool reduceMotion, bool persist) {
  preferences_.reduceMotion = reduceMotion;
  preferences_.reduceMotionFollowsSystem = false;
  // The motion the animator was in the middle of is dropped rather than resumed, so a later frame
  // starts from rest instead of jumping to where the clock had reached.
  animator_ = CharacterAnimator{};
  motion_ = {};
  stageFade_.reset();
  tabTween_.reset();
  modeTween_.reset();
  tabPrevious_.reset();
  modePrevious_.reset();
  renderSweep_.reset();
  toastTween_.reset();
  addedNotes_.clear();
  if (persist && persist_) saveDesignPreferences(preferences_);
  repaint();
}

void SingShell::followSystemReduceMotion(bool persist) {
  setReduceMotion(systemReduceMotion(), false);
  preferences_.reduceMotionFollowsSystem = true;
  if (persist && persist_) saveDesignPreferences(preferences_);
}

void SingShell::setSettingsOpen(bool open) {
  settingsOpen_ = open;
  repaint();
}

SingShell::~SingShell() { uninstallShellStrings(strings_.get()); }

void SingShell::applyLanguage() {
  preferences_.language = std::string{shellLanguageFor(
      preferences_.languageFollowsSystem ? systemPreferredLanguage() : preferences_.language)};
  if (stringsLanguage_ != preferences_.language) {
    uninstallShellStrings(strings_.get());
    strings_.reset();
    languageReport_ = {};
    stringsLanguage_ = preferences_.language;
    // English is compiled in. Any other language reads its file; a missing or unreadable file
    // leaves the shell in English rather than half-translated.
    if (preferences_.language != shellLanguages().front().code && !translations_.empty()) {
      if (auto loaded = loadShellStrings(translations_ / (preferences_.language + ".json"))) {
        languageReport_ = std::move(loaded.value().report);
        strings_ = std::make_unique<ShellStringTable>(std::move(loaded.value().table));
      }
    }
    // Every cached layer drew the previous language's words.
    layers_.invalidate();
    ++artGeneration_;
    repaint();
  }
  // An English shell installs nothing, so it never replaces a table someone else installed.
  if (strings_ != nullptr) installShellStrings(strings_.get());
}

void SingShell::setLanguage(std::string_view language, bool persist) {
  preferences_.language = std::string{shellLanguageFor(language)};
  preferences_.languageFollowsSystem = false;
  applyLanguage();
  if (persist && persist_) saveDesignPreferences(preferences_);
  repaint();
}

void SingShell::followSystemLanguage(bool persist) {
  preferences_.languageFollowsSystem = true;
  applyLanguage();
  if (persist && persist_) saveDesignPreferences(preferences_);
  repaint();
}

void SingShell::cycleLanguage(int direction, bool persist) {
  // Choices: the system's language, then each offered language in order.
  const auto& languages = shellLanguages();
  const auto choices = static_cast<int>(languages.size()) + 1;
  auto current = 0;
  if (!preferences_.languageFollowsSystem) {
    for (std::size_t i = 0U; i < languages.size(); ++i)
      if (languages[i].code == preferences_.language) current = static_cast<int>(i) + 1;
  }
  const auto next = ((current + (direction < 0 ? -1 : 1)) % choices + choices) % choices;
  if (next == 0) followSystemLanguage(persist);
  else setLanguage(languages[static_cast<std::size_t>(next - 1)].code, persist);
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

namespace {

// The rectangle in shell space an inline (non-lyric) field sits at for a layout: inside the time
// map for its event field, else where textFieldPlacement puts the field card. A request that names
// no surface is treated as the bounded field.
ui::Rect shellFieldBounds(TextInputAnchor anchor, const SingLayout& layout) {
  if (anchor == TextInputAnchor::TimeMapPanel)
    return timeMapFieldPlacement(timeMapPanelBounds(layout.overlay)).input;
  return textFieldPlacement(anchor == TextInputAnchor::ClassicSurface ? TextInputAnchor::BoundedField
                                                                      : anchor,
                            layout)
      .input;
}

}  // namespace

TextInputRequest SingShell::translateTextInput(TextInputRequest request) {
  // The controller states which surface the field belongs to. A note-grid anchor moves with the
  // grid; every other field is placed on the rectangle the shell draws it at (the time map's event
  // field, the transport's tempo or meter field, the inline field card), the same function the
  // overlay lays it out with. A new request has replaced whatever composition was open before.
  lyricInputActive_ = false;
  fieldAnchor_.reset();
  if (!presented_) return request;
  if (request.anchor == TextInputAnchor::NoteGrid) {
    lyricInputActive_ = true;
    request.logicalBounds = fromLegacy(request.logicalBounds);
    return request;
  }
  const auto bounds = shellFieldBounds(request.anchor, layout_);
  if (bounds.width <= 0.0 || bounds.height <= 0.0) return request;
  fieldAnchor_ = request.anchor;
  fieldBounds_ = bounds;
  request.logicalBounds = bounds;
  return request;
}

bool SingShell::inMusicalArea(ui::Point point) const noexcept {
  return workspace_ == Workspace::Sing &&
         (contains(layout_.grid, point) || contains(layout_.ruler, point));
}

bool SingShell::inEditableLane(ui::Point point) const noexcept {
  if (workspace_ != Workspace::Sing || !contains(layout_.laneTimePlot, point)) return false;
  if (!technicalLane_) return laneEditable_;
  // Only an open band takes a gesture; a collapsed band's strip is its own expand control.
  const auto bands = technicalBands();
  for (std::size_t i = 0U; i < bands.band.size(); ++i)
    if (!bands.collapsed[i] && contains(bands.band[i], point)) return true;
  return false;
}

NativeEditorController::HostedGeometry SingShell::hostedGeometry() const noexcept {
  if (technicalLane_) {
    const auto bands = technicalBands();
    return {.pianoBottom = legacyContentTop_ + layout_.grid.height,
            .laneHeight = 0.0,
            .phonemeHeight = bands.band[0U].height,
            .unitHeight = bands.band[1U].height,
            .seamHeight = bands.band[2U].height};
  }
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

void SingShell::frameNotesIfNeeded(NativeEditorController& controller, double previousGridHeight) {
  auto& model = controller.pianoRoll();
  auto& pitch = model.pitch();
  const auto regionId = model.regionId();
  const auto newRegion = !framedRegion_.has_value() || *framedRegion_ != regionId;
  const auto* region = model.project().findRegion(regionId);
  const auto noteCount = region == nullptr ? 0U : region->notes.size();
  const auto contentAppeared = lastFramingNoteCount_ == 0U && noteCount > 0U;
  lastFramingNoteCount_ = noteCount;
  const auto shrunk = previousGridHeight > layout_.grid.height + 1.0;
  if (!newRegion && !shrunk && !contentAppeared) return;
  const auto userMovedPitch = newRegion && pitch.topMidiKey() == 84
                                 ? false
                                 : framedTopMidi_.has_value()
                                       ? pitch.topMidiKey() != *framedTopMidi_
                                       : pitch.topMidiKey() != 84;
  framedRegion_ = regionId;
  if (userMovedPitch) {
    framedTopMidi_.reset();
    return;
  }
  if (region == nullptr || layout_.grid.height < pitch.rowHeight() * 2.0) return;
  std::array<std::size_t, 128U> pitches{};
  std::size_t count = 0U;
  std::size_t visible = 0U;
  for (const auto& note : region->notes) {
    const auto start = region->startTick + note.startTick;
    const auto x = layout_.grid.x + model.timeline().tickToPixel(start);
    const auto right = x + model.timeline().durationToPixels(note.durationTick);
    if (right <= layout_.grid.x || x >= layout_.grid.right()) continue;
    ++pitches[note.midiKey];
    ++count;
    const auto y = pitch.midiToPixel(note.midiKey);
    if (y >= 0.0 && y + pitch.rowHeight() <= layout_.grid.height) ++visible;
  }
  if (count == 0U || visible > 0U) {
    framedTopMidi_ = pitch.topMidiKey();
    return;
  }
  // Place the median visible pitch near the centre. Outlier high and low notes cannot push the
  // whole phrase away; a user can then scroll for them. This changes only presentation state.
  std::size_t cumulative = 0U;
  std::int32_t median = 60;
  for (std::size_t key = 0U; key < pitches.size(); ++key) {
    cumulative += pitches[key];
    if (cumulative > count / 2U) {
      median = static_cast<std::int32_t>(key);
      break;
    }
  }
  const auto rows = static_cast<std::int32_t>(std::floor(layout_.grid.height / pitch.rowHeight()));
  pitch.setTopMidiKey(std::clamp(median + std::max(1, rows / 2), 0, 127));
  framedTopMidi_ = pitch.topMidiKey();
  model.rebuildIndex();
}

void SingShell::cancelGestures(NativeEditorController& controller) {
  const auto hadGesture = knobDrag_.has_value() || forwarding_ != ForwardArea::None ||
                          bodyGesture_ || overlayGesture_.has_value();
  knobDrag_.reset();
  overlayGesture_.reset();
  forwarding_ = ForwardArea::None;
  if (bodyGesture_) {
    bodyGesture_ = false;
    tune_->cancelGestures(controller);
    mix_->cancelGestures(controller);
    voice_->cancelGestures(controller);
  }
  scrollAccumulator_ = 0.0;
  controller.cancelPointerGesture();
  if (hadGesture) repaint();
}

void SingShell::releaseSurface(NativeEditorController& controller) {
  if (presented_ || controller.hostedGrid().has_value()) {
    cancelGestures(controller);
    // A lyric or inline field anchored in shell space would be misplaced on the classic surface.
    if (lyricInputActive_ || fieldAnchor_) controller.cancelTextComposition();
    lyricInputActive_ = false;
    fieldAnchor_.reset();
  }
  // The classic surface owns keyboard focus from here on.
  semanticFocus_.clear();
  overlayOpener_.clear();
  presentedOverlay_ = OverlayKind::None;
  fieldOpenedOver_ = OverlayKind::None;
  tabTween_.reset();
  modeTween_.reset();
  tabPrevious_.reset();
  modePrevious_.reset();
  workspaceMenuOpen_ = false;
  singerMenuOpen_ = false;
  aboutOpen_ = false;
  settingsOpen_ = false;
  presented_ = false;
  controller.setHostedGrid(std::nullopt);
  // Whatever paints next is not a shell frame, so the next shell frame starts from nothing.
  layers_.invalidate();
  lastDamage_ = FrameDamage::everything();
}

bool SingShell::prepareFrame(NativeEditorController& controller, double logicalWidth,
                             double logicalHeight) {
  const ScopedActiveShellStrings activeStrings{strings_.get()};
  if (controllerSerial_ != controller.instanceSerial()) {
    // Opening or recovering a project replaces the controller and its pitch transform. A region
    // can keep the same id, so region identity alone cannot tell us that framing was lost.
    releaseSurface(controller);
    controllerSerial_ = controller.instanceSerial();
    framedRegion_.reset();
    framedTopMidi_.reset();
    lastFramingNoteCount_ = 0U;
    animatedRegion_ = {};
    seenNotes_.clear();
    addedNotes_.clear();
    tabTween_.reset();
    modeTween_.reset();
    tabPrevious_.reset();
    modePrevious_.reset();
    renderSweep_.reset();
    toastTween_.reset();
    previousDiagnosticVisible_ = false;
    previousRenderState_ = RenderStatusState::Idle;
    // A replaced controller is a different document: an open overlay's model went with it, and a
    // popover the shell holds open would otherwise point at stale state.
    diagnosticsOpen_ = false;
    singerMenuOpen_ = false;
    aboutOpen_ = false;
    settingsOpen_ = false;
    overlayGesture_.reset();
    overlayOpener_.clear();
    presentedOverlay_ = OverlayKind::None;
    fieldOpenedOver_ = OverlayKind::None;
    fieldAnchor_.reset();
  }
  // Every modal surface the controller opens is presented by the shell itself. Only a shell that
  // was never activated, or a platform without the vector backend, does not present.
  if (!enabled()) {
    releaseSurface(controller);
    return false;
  }
  // Following the system, the frame reads Increase Contrast itself, so switching it in System
  // Settings changes the open editor on its next frame; the display-options observer asks for
  // that frame when the editor is otherwise idle.
  if (preferences_.contrastFollowsSystem) {
    const auto system = systemIncreaseContrast() ? Contrast::High : Contrast::Standard;
    // The contrast picks the token table, which keys the cached background.
    if (system != preferences_.contrast) preferences_.contrast = system;
  }
  if (preferences_.reduceMotionFollowsSystem) preferences_.reduceMotion = systemReduceMotion();
  // The inspector exists only in the compact presentations; a window that grows back to the full
  // rack forgets it, so shrinking again starts closed.
  auto next = solveSingLayout(logicalWidth, logicalHeight, inspectorWanted_);
  if (next.rack == RackPresentation::Full) inspectorWanted_ = false;
  if (next.workspaceMenuButton.width < 24.0) workspaceMenuOpen_ = false;
  // The singer menu is anchored to its button: a resize closes it, and focus returns to the button
  // (a layout that no longer shows the button drops that focus on the next rebuild).
  if (singerMenuOpen_ && presented_ &&
      (next.width != layout_.width || next.height != layout_.height)) {
    singerMenuOpen_ = false;
    takeSemanticFocus(controller, std::string{kSingerMenuButtonId});
  }
  if (next.width != layout_.width || next.height != layout_.height) {
    tabTween_.reset();
    modeTween_.reset();
    tabPrevious_.reset();
    modePrevious_.reset();
  }
  if (settingsOpen_ && presented_ &&
      (next.width != layout_.width || next.height != layout_.height)) {
    settingsOpen_ = false;
    takeSemanticFocus(controller, "shell.settings");
  }
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
  // An inline field the host's input client was placed on is cancelled when the new layout moves
  // its rectangle (a resize), never left typing into a field drawn somewhere else.
  if (fieldAnchor_) {
    const auto placed = shellFieldBounds(*fieldAnchor_, next);
    if (!presented_ || placed.x != fieldBounds_.x || placed.y != fieldBounds_.y ||
        placed.width != fieldBounds_.width || placed.height != fieldBounds_.height) {
      fieldAnchor_.reset();
      controller.cancelTextComposition();
    }
  }
  // A TUNE or MIX gesture measures against the body it started in (a fader's travel, a graph's
  // axis); when that body moves or resizes it is abandoned, never committed against new geometry.
  const auto bodyMoved = next.editor.x != layout_.editor.x || next.editor.y != layout_.editor.y ||
                         next.editor.width != layout_.editor.width ||
                         next.lane.bottom() != layout_.lane.bottom();
  if (bodyGesture_ && (bodyMoved || !presented_)) cancelGestures(controller);
  const auto previousGridHeight = layout_.grid.height;
  layout_ = next;
  // An expression lane opened elsewhere (the menu, a knob) takes the lane band back.
  if (controller.expressionLaneOpen()) technicalLane_ = false;
  syncTechnicalBands(controller);
  applyGeometry(controller);
  frameNotesIfNeeded(controller, previousGridHeight);
  presented_ = true;
  // The DIAGNOSTICS popover vanishes with the last diagnostic; the shell's flag goes with it, so the
  // next failure shows its toast and never reopens the popover modally on its own.
  if (diagnosticsOpen_ && controller.sceneState().diagnostics.empty()) diagnosticsOpen_ = false;
  // A surface opened since the last frame (by the host's menu, a recovery action or a shell
  // control) covers the score: a lyric field open under it goes.
  cancelCoveredLyric(controller);
  return true;
}

void SingShell::setInspectorOpen(NativeEditorController& controller, bool open) {
  if (layout_.rack == RackPresentation::Full) open = false;
  inspectorWanted_ = open;
  if (open == layout_.inspectorOpen) return;
  // A knob gesture belongs to the knob rectangles it started on.
  if (knobDrag_) cancelGestures(controller);
  const auto* controllerFocus = controller.accessibilityTree().focusedNode();
  const auto focusInside =
      !open && (semanticFocus_ == "shell.inspector" || semanticFocus_ == "shell.change-voice" ||
                semanticFocus_ == kSingerMenuButtonId ||
                semanticFocus_ == "shell.style" || semanticFocus_.starts_with("shell.knob.") ||
                (semanticFocus_.empty() && controllerFocus != nullptr &&
                 controllerFocus->id == "voice.identity"));
  // The open inspector is modal: an open lyric field over the score it covers is abandoned.
  if (open && lyricInputActive_) {
    controller.cancelTextComposition();
    lyricInputActive_ = false;
  }
  layout_ = solveSingLayout(layout_.width, layout_.height, open);
  if (open || focusInside) {
    // Every way of opening it (pointer, keyboard, accessibility) gives the inspector the keyboard,
    // so no key reaches the score it covers; closing returns focus that was inside to the button.
    refreshSemantics(controller);
    takeSemanticFocus(controller, "shell.inspector");
  }
  repaint();
}

void SingShell::setWorkspaceMenuOpen(NativeEditorController& controller, bool open) {
  if (layout_.workspaceMenuButton.width < 24.0) open = false;
  if (workspaceMenuOpen_ == open) return;
  if (open && lyricInputActive_) {
    controller.cancelTextComposition();
    lyricInputActive_ = false;
  }
  if (open && (knobDrag_ || forwarding_ != ForwardArea::None)) cancelGestures(controller);
  workspaceMenuOpen_ = open;
  refreshSemantics(controller);
  takeSemanticFocus(controller, "shell.workspace-menu");
  repaint();
}

void SingShell::setDiagnosticsOpen(bool open) {
  // The DIAGNOSTICS popover is presentation only: the controller's diagnostics are unchanged, and
  // closing it changes no project state. It is closed by a controller replacement like any overlay.
  if (diagnosticsOpen_ == open) return;
  diagnosticsOpen_ = open;
  if (open) diagnosticsOverlay_->presented();
  repaint();
}

void SingShell::invalidateLayers() noexcept {
  layers_.invalidate();
  lastDamage_ = FrameDamage::everything();
}

void SingShell::invalidateBackgroundLayers() noexcept {
  layers_.invalidateBackground();
  lastDamage_ = FrameDamage::everything();
}

std::uint64_t SingShell::backgroundKey(const DesignTokens& tokens, const PixelSurface& surface,
                                       double scale) const noexcept {
  // The plan's invalidation rule for the background (window size, scale, look, contrast), spelled
  // out as what paintBackground reads: the tokens, the panel rectangles and the wordmark.
  const auto& l = layout_;
  paint::ContentHash h{artGeneration_};
  h.add(static_cast<const void*>(&tokens))
      .add(static_cast<std::uint64_t>(surface.width()))
      .add(static_cast<std::uint64_t>(surface.height()))
      .add(scale)
      .add(static_cast<std::uint64_t>(l.rack))
      .add(l.header).add(l.editor).add(l.lane).add(l.singer).add(l.expression).add(l.style)
      .add(l.rackArea).add(l.status).add(l.transport).add(l.ruler).add(l.wordmark)
      .add(l.grid)
      .add(static_cast<std::uint64_t>(workspace_))
      .add(static_cast<const void*>(assets().wordmark.get()));
  return h.value();
}

double SingShell::measureText(std::string_view utf8, const paint::TextStyle& style) const {
  if (metrics_ == nullptr) return 0.0;
  std::string key;
  key.reserve(utf8.size() + 24U);
  const auto append = [&key](const auto& value) {
    key.append(reinterpret_cast<const char*>(&value), sizeof value);
  };
  append(style.role);
  append(style.size);
  append(style.tracking);
  append(style.uppercase);
  key.append(utf8);
  if (const auto found = measureCache_.find(key); found != measureCache_.end()) return found->second;
  // Bounded: a long session of edited lyrics must not grow this without limit.
  if (measureCache_.size() >= 8192U) measureCache_.clear();
  const auto width = metrics_->measure(utf8, style);
  measureCache_.emplace(std::move(key), width);
  return width;
}

void SingShell::characterArt(paint::Canvas2D& c, ui::Rect bounds, std::uint64_t hash,
                             std::function<void(CharacterCanvas)> draw) const {
  if (auto* recorder = dynamic_cast<paint::RecordingCanvas*>(&c); recorder != nullptr) {
    recorder->drawRaster(bounds, paint::ContentHash{hash}.add(artGeneration_).value(),
                         [draw = std::move(draw)](paint::Canvas2D& vector, RasterCanvas& raster) {
                           draw(CharacterCanvas{vector, raster});
                         });
    return;
  }
  draw(characterCanvas(c));
}

core::Result<void> SingShell::setSingerMenuOpen(NativeEditorController& controller, bool open) {
  if (!open) {
    if (!singerMenuOpen_) return core::success();
    singerMenuOpen_ = false;
    overlayOpener_.clear();
    refreshSemantics(controller);
    takeSemanticFocus(controller, std::string{kSingerMenuButtonId});
    repaint();
    return core::success();
  }
  if (singerMenuOpen_) return core::success();
  if (!presented_ || !knobsShown() || layout_.singerMenu.width <= 0.0)
    return core::failure(core::ErrorCode::InvalidState, tr(Str::TheSingerCardIsNotOn));
  if (activeOverlay(controller) != nullptr)
    return core::failure(core::ErrorCode::Conflict, tr(Str::CloseTheOpenSurfaceFirst));
  const auto state = controller.sceneState();
  if (singerMenuOverlay_->panel(controller, state, layout_, overlaySlot(controller, state)).width <=
      0.0)
    return core::failure(core::ErrorCode::InvalidState, tr(Str::TheWindowIsTooSmallFor));
  // The menu is modal like the workspace menu: a lyric field or gesture over the score it covers
  // is abandoned first.
  if (lyricInputActive_) {
    controller.cancelTextComposition();
    lyricInputActive_ = false;
  }
  if (knobDrag_ || forwarding_ != ForwardArea::None) cancelGestures(controller);
  // The button holds focus as the menu opens, so it is the opener Escape and a finished command
  // return focus to, however the menu was opened.
  takeSemanticFocus(controller, std::string{kSingerMenuButtonId});
  singerMenuOpen_ = true;
  refreshSemantics(controller);
  repaint();
  return core::success();
}

core::Result<void> SingShell::setAboutOpen(NativeEditorController& controller, bool open) {
  if (!open) {
    if (!aboutOpen_) return core::success();
    aboutOpen_ = false;
    returnFocusToOverlayOpener(controller);
    repaint();
    return core::success();
  }
  if (aboutOpen_) return core::success();
  if (!presented_) return core::failure(core::ErrorCode::InvalidState, tr(Str::TheAboutSheetNeedsTheDesign));
  if (activeOverlay(controller) != nullptr)
    return core::failure(core::ErrorCode::Conflict, tr(Str::CloseTheOpenSurfaceFirst));
  const auto state = controller.sceneState();
  if (aboutOverlay_->panel(controller, state, layout_, overlaySlot(controller, state)).width <= 0.0)
    return core::failure(core::ErrorCode::InvalidState, tr(Str::TheWindowIsTooSmallForTheAbout));
  if (lyricInputActive_) {
    controller.cancelTextComposition();
    lyricInputActive_ = false;
  }
  if (knobDrag_ || forwarding_ != ForwardArea::None) cancelGestures(controller);
  aboutOpen_ = true;
  refreshSemantics(controller);
  takeSemanticFocus(controller, std::string{kAboutCloseId});
  repaint();
  return core::success();
}

void SingShell::paintBackground(Canvas2D& c, const DesignTokens& t) const {
  // The panels' translucent fills are painted in software with the wash, under this chrome; the
  // vector reference draws the wash and the fills here instead.
  const auto vectorReference = std::getenv("SEAM_WASH_VECTOR_REFERENCE") != nullptr;
  if (vectorReference) paintVectorWash(c, t);
  const auto& l = layout_;
  const auto panel = [&](ui::Rect r, double radius, double alpha = 0.90) {
    if (vectorReference) glassPanel(c, t, r, radius, alpha);
    else glassPanelEdges(c, t, r, radius);
  };
  panel(l.header, t.shape.hero);
  panel(l.editor, t.shape.card);
  panel(l.lane, t.shape.card);
  if (l.rack == RackPresentation::Full) {
    panel(l.singer, t.shape.card);
    panel(l.expression, t.shape.card);
    panel(l.style, t.shape.card);
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
    panel(l.rackArea, t.shape.card);
  }
  panel(l.status, 10.0, 0.80);
  sunken(c, t, l.transport, 10.0);
  // The SING grid's translucent backdrop depends on the layout alone, so it is panel chrome: drawn
  // once here, under the grid layer's key rows and lines, rather than on every scroll. The other
  // workspaces cover the score with their own body and never showed it.
  if (workspace_ == Workspace::Sing && l.grid.width > 0.0 && l.grid.height > 0.0)
    c.fill(Path::rect(l.grid), withAlpha(t.color.canvas, 0.55));

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

  if (const auto& wordmark = assets().wordmark; wordmark && l.wordmark.width > 0.0) {
    const auto aspect = static_cast<double>(wordmark->width()) / wordmark->height();
    const auto height = std::min(l.wordmark.height, l.wordmark.width / aspect);
    c.drawImage(*wordmark, {l.wordmark.x, l.wordmark.y + (l.wordmark.height - height) * 0.5,
                            height * aspect, height});
  } else if (l.wordmark.width > 0.0) {
    c.text(l.wordmark, tr(Str::SEAM), style(t.type.display, 34.0, 6.0), t.color.textPrimary);
  }
}

bool SingShell::paint(RasterCanvas& canvas, NativeEditorController& controller,
                      const EditorSceneState& state, time::Tick playhead) {
  const ScopedActiveShellStrings activeStrings{strings_.get()};
  // The frame that paints decides whether it owes another; one that does not paint owes none.
  idleFrameDue_.reset();
  if (!presented_ ||
      layout_.width != std::max(canvas.logicalWidth(), 480.0) ||
      layout_.height != std::max(canvas.logicalHeight(), 320.0)) {
    // prepareFrame did not run for this canvas (or the shell was disabled meanwhile).
    if (!presented_ ||
        !prepareFrame(controller, canvas.logicalWidth(), canvas.logicalHeight())) {
      releaseSurface(controller);
      return false;
    }
  }
  auto& model = controller.pianoRoll();
  laneEditable_ = state.expressionLabelVisible() && state.expression.refusal.empty();
  const auto& t = tokensFor(preferences_.mode, preferences_.contrast);
  ppq_ = time::Tick{model.timeline().ppq()}.value();
  auto& surface = canvas.surface();
  const auto scale = canvas.scale();
  if (metrics_ == nullptr) {
    metricsSurface_ = PixelSurface{1U, 1U};
    metrics_ = paint::makeCanvas(metricsSurface_, 1.0);
  }
  if (metrics_ == nullptr || surface.width() == 0U || surface.height() == 0U) {
    releaseSurface(controller);
    return false;
  }
  // The frame is recorded, layer by layer, and composed at the end: the painters below draw into
  // the recording, and only layers whose drawing changed are rasterized again.
  paint::RecordingCanvas frame{
      static_cast<double>(surface.width()) / scale, static_cast<double>(surface.height()) / scale,
      scale, [this](std::string_view utf8, const paint::TextStyle& style) {
        return measureText(utf8, style);
      },
      artGeneration_};
  // High Contrast paints without glow (the recorder drops it as GlowlessCanvas would), and every
  // painter below draws through this one canvas.
  frame.setGlowless(t.contrast == Contrast::High);
  auto* c = &frame;
  // The raster front carries the character package's state or QOI ring artwork into the same frame the vector
  // canvas paints; both fronts live only for this call.
  raster_ = &canvas;
  struct RasterScope final {
    RasterCanvas** slot;
    ~RasterScope() { *slot = nullptr; }
  } rasterScope{&raster_};
  // The audition level belongs to the VOICE workspace, whose session owns the player. It is read
  // before the state is resolved, so the protagonist listens to the audition on the same frame the
  // hero shows it, and cleared as soon as VOICE is not the visible workspace.
  if (workspace_ == Workspace::Voice) {
    auditionLevel_ = voice_->auditionLevel();
  } else {
    auditionLevel_.reset();
  }
  // One state for the whole frame, from the read models the state already carries. The animation phase
  // is resolved once too, so the ring, the avatar and the VOICE hero agree within a frame.
  characterState_ = resolveCharacterState(characterSurfaceInput(state, auditionLevel_));
  characterDisplay_ = state.characterMode;
  const auto reduceMotion = preferences_.reduceMotion;
  frameNow_ = uiClock_ ? uiClock_() : std::chrono::steady_clock::now();
  if (previousRenderState_ != RenderStatusState::Ready &&
      state.renderStatus.state == RenderStatusState::Ready)
    renderSweep_.start(frameNow_, std::chrono::milliseconds{250}, reduceMotion);
  previousRenderState_ = state.renderStatus.state;
  const auto hasDiagnostic = !state.diagnostics.empty();
  if (hasDiagnostic != previousDiagnosticVisible_) {
    toastAppearing_ = hasDiagnostic;
    toastTween_.start(frameNow_, std::chrono::milliseconds{hasDiagnostic ? 150 : 120}, reduceMotion);
    previousDiagnosticVisible_ = hasDiagnostic;
  }
  // A committed receipt is the only export signal that can briefly show the complete pose.
  // Keep the dwell tied to the receipt identity, not to a paint count or export progress.
  if (state.lastExport && state.lastExport->state == authoring::ExportState::Committed) {
    const auto key = state.lastExport->masterPath.string() + ":" + state.lastExport->masterSha256;
    if (key != lastCompletedExportKey_) {
      lastCompletedExportKey_ = key;
      exportCompleteUntil_ = frameNow_ + std::chrono::milliseconds{1500};
    }
    if (frameNow_ < exportCompleteUntil_ && characterState_ == CharacterState::Idle)
      characterState_ = CharacterState::Complete;
  }
  animator_.advance(characterState_, frameNow_, reduceMotion);
  motion_ = animator_.motion();
  motionShown_ = false;
  const auto knobs = knobModels(state);
  for (std::size_t i = 0U; i < knobs.size(); ++i) knobRefused_[i] = !knobs[i].refusal.empty();
  paintHeader(*c, t, state, playhead);
  exportRunning_ = exportBusy(controller);
  waveform_ = hostActions_.regionWaveform
                  ? hostActions_.regionWaveform()
                 : RegionWaveform{nullptr, tr(Str::NoWaveform),
                                  tr(Str::ThisHostDoesNotGiveThe)};
  if (waveform_.shown() && waveform_.view->key.region != model.regionId())
    waveform_ = RegionWaveform{nullptr, tr(Str::OtherRegion),
                               tr(Str::TheRenderedAudioBelongsToA)};
  if (workspace_ == Workspace::Voice) {
    // The listening singer uses this look's portrait; finished designer work and a requested
    // audition are collected before the body paints, and frames continue while either runs.
    const auto artShown = characterDisplay_ != domain::CharacterDisplayMode::Off;
    voice_->setPortrait(artShown ? assets().portrait : nullptr);
    voice_->setListeningPortrait(artShown ? lookListeningPortrait() : nullptr);
    if (voice_->poll()) repaint();
  }
  if (auto* body = bodyWorkspace(); body != nullptr) {
    stageShown_ = false;
    body->paint(*c, t, controller, state, workspaceArea());
    // The keyboard focus ring for the body's focused control, at the bounds it publishes now.
    if (semanticFocus_.starts_with(body->idPrefix())) {
      const paint::LayerScope focusLayer{*c, paint::Layer::Dynamic, "focus"};
      std::vector<SemanticNode> nodes;
      body->semantics(controller, state, workspaceArea(), nodes);
      for (const auto& node : nodes) {
        if (node.id != semanticFocus_ || node.bounds.width <= 0.0) continue;
        const auto& r = node.bounds;
        c->save();
        c->setGlow(t.color.focusRing, 6.0);
        c->stroke(Path::roundedRect({r.x - 2, r.y - 2, r.width + 4, r.height + 4}, 5),
                  t.color.focusRing, StrokeStyle{2.0});
        c->restore();
        break;
      }
    }
  } else if (workspace_ == Workspace::Export) {
    stageShown_ = false;
    paintExport(*c, t, state);
  } else {
    paintEditor(*c, t, model, state);
    paintLane(*c, t, model, state);
  }
  paintRack(*c, t, state);
  paintStatus(*c, t, state);
  // The prior composed image fades over the new workspace or look in the dynamic layer. Capturing
  // it only when a transition starts leaves the static cache and idle frames untouched.
  {
    const paint::LayerScope transition{*c, paint::Layer::Dynamic, "transitions"};
    if (tabPrevious_ && tabTween_.running(frameNow_, reduceMotion)) {
      const auto p = tabTween_.eased(frameNow_, reduceMotion);
      c->save();
      c->clipRect(workspaceArea());
      c->translate(0.0, -8.0 * p);
      c->drawImage(*tabPrevious_, {0.0, 0.0, layout_.width, layout_.height}, 1.0 - p);
      c->restore();
    }
    if (modePrevious_ && modeTween_.running(frameNow_, reduceMotion))
      c->drawImage(*modePrevious_, {0.0, 0.0, layout_.width, layout_.height},
                   1.0 - modeTween_.eased(frameNow_, reduceMotion));
    if (workspace_ == Workspace::Sing && renderSweep_.running(frameNow_, reduceMotion)) {
      const auto p = renderSweep_.progress(frameNow_, reduceMotion);
      const auto x = layout_.grid.x + (layout_.grid.width + 36.0) * p - 18.0;
      c->save();
      c->clipRect(layout_.grid);
      for (const auto& note : model.visibleNotes()) {
        if (note.hiddenByOverlapDensity) continue;
        const ui::Rect b{note.bounds.x, note.bounds.y + layout_.grid.y,
                         note.bounds.width, note.bounds.height};
        const auto left = std::max(x - 18.0, b.x);
        const auto right = std::min(x + 18.0, b.right());
        if (right > left)
          c->fill(Path::rect({left, b.y, right - left, b.height}),
                  withAlpha(t.color.accent, 0.22 * (1.0 - p)));
      }
      c->restore();
    }
  }
  if (workspace_ == Workspace::Sing && state.focusedElementBounds.has_value()) {
    const auto focus = fromLegacy(*state.focusedElementBounds);
    // The note focus ring never draws over the open inspector.
    if (intersects(focus, layout_.grid) &&
        !(layout_.inspectorOpen && intersects(focus, layout_.inspector))) {
      const paint::LayerScope focusLayer{*c, paint::Layer::Dynamic, "focus"};
      c->save();
      c->setGlow(t.color.focusRing, 6.0);
      c->stroke(Path::roundedRect({focus.x - 2, focus.y - 2, focus.width + 4, focus.height + 4}, 5),
                t.color.focusRing, StrokeStyle{2.0});
      c->restore();
    }
  }
  // Everything below is drawn over the score and so over the playhead: it belongs to the dynamic
  // layer, which keeps it above the playhead however often that moves.
  if (workspaceMenuOpen_) {
    const paint::LayerScope menuLayer{*c, paint::Layer::Dynamic, "menu"};
    paintWorkspaceMenu(*c, t);
  }
  // A re-homed overlay is the topmost surface: it covers the score, the lane and the rack with its
  // own card, so nothing it hides is reachable or published while it is up.
  if (const auto* overlay = activeOverlay(controller, state); overlay != nullptr) {
    stageShown_ = false;
    const paint::LayerScope overlayLayer{*c, paint::Layer::Dynamic, "overlay"};
    paintOverlay(*c, t, controller, state, *overlay);
  } else {
    overlayDrawn_.reset();
  }
  // The kit tooltip is the topmost item, over overlays and menus, and a dynamic item of its own:
  // showing, moving or hiding it damages only its card. Its candidates are read against this
  // frame's elided labels.
  elidedLabels_ = frame.elidedText();
  lastPaintAt_ = frameNow_;
  updateTooltip(frameNow_);
  shownTooltip_.reset();
  if (const auto* tip = tooltip_.shown(frameNow_); tip != nullptr) {
    const auto placed = layoutTooltip(
        tip->text, tip->target, ui::Rect{0.0, 0.0, layout_.width, layout_.height}, t,
        [this](std::string_view utf8, const paint::TextStyle& s) { return measureText(utf8, s); });
    if (!placed.empty()) {
      const paint::LayerScope tooltipLayer{*c, paint::Layer::Dynamic, "tooltip"};
      paintTooltip(*c, t, placed);
      shownTooltip_ = ShownTooltip{tip->id, tip->text, tip->target, placed.box};
    }
  }
  const auto softwareWash = std::getenv("SEAM_WASH_VECTOR_REFERENCE") == nullptr;
  const auto panels = backgroundGlassPanels(layout_, t);
  const auto composed = layers_.compose(
      canvas,
      paint::BackgroundLayer{.key = backgroundKey(t, surface, scale),
                             .clear = t.color.canvas,
                             .paint = [this, &t](Canvas2D& background) { paintBackground(background, t); },
                             .paintBase = [&t, &panels, softwareWash](PixelSurface& band, double bandScale,
                                               std::uint32_t top, std::uint32_t fullHeight) {
                               if (!softwareWash) return;
                               paintBackgroundWash(band, bandScale, top, fullHeight, t);
                               paintGlassPanelFills(band, bandScale, top, panels);
                             },
                             .paintBaseOverwritesBand = softwareWash},
      frame, retainedSurface_);
  lastDamage_ = composed.damage;
  lastLayers_ = composed.rasterized;
  scheduleAnimationRepaint();
  return true;
}

void SingShell::scheduleAnimationRepaint() {
  if (!tabTween_.running(frameNow_, preferences_.reduceMotion)) tabPrevious_.reset();
  if (!modeTween_.running(frameNow_, preferences_.reduceMotion)) modePrevious_.reset();
  // A frame is requested only while something is actually animating, so a still protagonist stops the
  // loop: a held pose (listening, complete, warning, error) and every state under Reduce Motion ask
  // for nothing at all, and a state that only breathes or spins asks only for as long as it does. The
  // Stage's own fade is the other source, and it asks until it settles.
  //
  // A tween, the render spinner and a blink need the next frame as soon as it can be painted. The
  // breath does not: it moves a third of a point between frames a tenth of a second apart, so it
  // asks for them through nextFrameDue() at that pace, and a window that is otherwise still idles
  // between them instead of repainting at the display's rate.
  //
  // The request asks for a frame; what that frame costs is decided when it is composed. A blink or a
  // breath changes only the avatar and ring items of the dynamic layer, so the frame rasterizes that
  // layer alone and lastFrameDamage() names just those rectangles for the presenter to invalidate.
  if (tabTween_.running(frameNow_, preferences_.reduceMotion) ||
      modeTween_.running(frameNow_, preferences_.reduceMotion) ||
      renderSweep_.running(frameNow_, preferences_.reduceMotion) ||
      toastTween_.running(frameNow_, preferences_.reduceMotion) ||
      std::any_of(addedNotes_.begin(), addedNotes_.end(), [this](const auto& entry) {
        return entry.second.running(frameNow_, preferences_.reduceMotion);
      })) {
    repaint();
    return;
  }
  if (characterState_ == CharacterState::Complete && frameNow_ < exportCompleteUntil_) {
    repaint();
    return;
  }
  if (stageFade_.fading()) {
    repaint();
    return;
  }
  // The state animating is not enough: at a width whose rack is a rail or a drawer and whose header
  // has no avatar, no painted surface carries the motion and the next frame would be identical.
  if (preferences_.reduceMotion || !characterStateAnimates(characterState_) || !motionShown_) return;
  const auto delay = animator_.nextFrameDelay(frameNow_);
  if (!delay.has_value()) return;
  if (*delay <= std::chrono::steady_clock::duration::zero()) {
    repaint();
    return;
  }
  idleFrameDue_ = frameNow_ + *delay;
}

namespace {
// The header meter's clip light at the right end of l.outputMeter: what is painted, and the
// taller strip a click on it lands in. Shared by paint, pointer and semantics.
struct OutputClipLight final {
  ui::Rect light;
  ui::Rect hit;
};
OutputClipLight outputClipLight(ui::Rect meter) {
  return {{meter.right() - 12.0, meter.y + 12.0, 10.0, 20.0},
          {meter.right() - 16.0, meter.y, 16.0, meter.height}};
}
}  // namespace

void SingShell::paintHeader(Canvas2D& c, const DesignTokens& t, const EditorSceneState& state,
                            time::Tick playhead) const {
  const auto& l = layout_;
  // Workspace tabs: SING, VOICE, TUNE, MIX and EXPORT.
  static constexpr std::array<Icon, 5U> kIcons{Icon::Sing, Icon::Voice, Icon::Tune, Icon::Mix,
                                               Icon::Export};
  static constexpr std::array<Str, 5U> kNames{Str::Sing, Str::Voice, Str::Tune, Str::Mix, Str::Export};
  for (std::size_t i = 0U; i < l.workspaceTab.size(); ++i) {
    const auto tab = l.workspaceTab[i];
    if (tab.width < 24.0) continue;
    const auto active = tabSelected(i);
    const auto color = active ? t.color.accent : t.color.textSecondary;
    const auto iconCenter = ui::Point{tab.x + tab.width * 0.5,
                                      tab.y + (l.workspaceLabelsVisible ? tab.height * 0.36
                                                                        : tab.height * 0.5)};
    c.save();
    if (active) c.setGlow(t.color.accent, 8.0);
    icon(c, kIcons[i], iconCenter, 22.0, color);
    c.restore();
    if (l.workspaceLabelsVisible)
      c.text({tab.x, tab.y + tab.height * 0.62, tab.width, 16.0}, tr(kNames[i]),
             style(FontRole::UiSemibold, t.type.smallLabel, t.type.labelTracking, TextAlign::Center,
                   true),
             active ? t.color.accent : withAlpha(t.color.textSecondary, 0.9));
    if (active) {
      c.save();
      c.setGlow(t.color.accent, 8.0);
      c.fill(Path::capsule({tab.x + 18.0, tab.bottom() - 3.0, tab.width - 36.0, 2.5}),
             t.color.accent);
      c.restore();
    }
  }

  if (l.workspaceMenuButton.width >= 24.0) {
    const auto b = l.workspaceMenuButton;
    c.fill(Path::roundedRect(b, 7.0), withAlpha(t.color.surfaceRaised, 0.96));
    c.stroke(Path::roundedRect(b, 7.0), workspaceMenuOpen_ ? t.color.accent : t.color.border,
             StrokeStyle{1.0});
    const auto label = b.width >= 76.0 ? tr(Str::SEAM2) : "≡";
    c.text(b, label, style(FontRole::UiSemibold, 13.0, 0.4, TextAlign::Center),
           workspaceMenuOpen_ ? t.color.accent : t.color.textPrimary);
  }

  // Mode switch.
  const auto sw = l.modeSwitch;
  if (sw.width > 0.0) {
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
  c.text({sw.x, sw.y, half, sw.height}, tr(Str::Emo), labelStyle,
         preferences_.mode == DesignMode::Emo ? t.color.textOnAccent : t.color.textSecondary);
  c.text({sw.x + half, sw.y, half, sw.height}, tr(Str::Scene), labelStyle,
         preferences_.mode == DesignMode::Scene ? t.color.textOnAccent : t.color.textSecondary);
  }

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
  {
    // The running position changes on every playback frame: a dynamic item of its own.
    const paint::LayerScope transport{c, paint::Layer::Dynamic, "transport"};
    c.save();
    c.setGlow(withAlpha(t.color.accentTime, 0.75), 7.0);
    c.text(l.positionReadout, position, readout, t.color.accentTime);
    c.restore();
  }
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

  // Output meter: segments on a -60..0 dBFS scale per channel of the measured bus, a peak-hold
  // marker and a clip light. Without a measured level (device stopped or unavailable) it shows
  // only the empty scale: nothing here is ever estimated.
  if (l.outputMeterVisible) {
    const auto m = l.outputMeter;
    c.text({m.x, m.y, 36.0, m.height}, tr(Str::Out),
           style(FontRole::UiSemibold, t.type.smallLabel, 1.2, TextAlign::Left, true),
           t.color.textSecondary);
    // The measured level moves on every playback frame; the label above does not.
    const paint::LayerScope meterLayer{c, paint::Layer::Dynamic, "meter"};
    constexpr int kSegments = 18;             // 3.33 dB each
    constexpr double kFloorDb = -60.0;
    constexpr double kSegmentDb = -kFloorDb / kSegments;
    const auto zone = [&t](int i) {
      return i < 12 ? t.color.meterLow : (i < 16 ? t.color.meterMid : t.color.meterHigh);
    };
    const auto toDb = [](float linear) {
      return linear > 0.0F ? 20.0 * std::log10(static_cast<double>(linear)) : -1000.0;
    };
    const auto& level = state.outputLevel;
    const auto rows = level.has_value() ? std::max<std::size_t>(level->peak.size(), 1U) : 2U;
    // Rows share the 24pt band below the label baseline; a mono bus is one centred row.
    const auto pitch = 24.0 / static_cast<double>(rows);
    const auto rowHeight = std::min(8.0, pitch * 2.0 / 3.0);
    const auto bandTop = m.y + 22.0 - (static_cast<double>(rows) * pitch) * 0.5;
    for (std::size_t row = 0U; row < rows; ++row) {
      const auto y = bandTop + static_cast<double>(row) * pitch + (pitch - rowHeight) * 0.5;
      const auto db = level.has_value() && row < level->peak.size() ? toDb(level->peak[row])
                                                                     : -1000.0;
      for (int i = 0; i < kSegments; ++i) {
        const auto lit = db > kFloorDb + i * kSegmentDb;
        c.fill(Path::roundedRect({m.x + 34.0 + i * 6.0, y, 4.0, rowHeight}, 1.0),
               withAlpha(zone(i), lit ? 1.0 : 0.22));
      }
      if (level.has_value() && row < level->hold.size()) {
        const auto holdDb = toDb(level->hold[row]);
        if (holdDb > kFloorDb) {
          const auto x = m.x + 34.0 + std::min(1.0, (holdDb - kFloorDb) / -kFloorDb) *
                                          (kSegments * 6.0 - 2.0);
          c.fill(Path::roundedRect({x - 0.75, y - 1.0, 1.5, rowHeight + 2.0}, 0.75),
                 t.color.textPrimary);
        }
      }
    }
    const auto clip = outputClipLight(m);
    const auto clipped = level.has_value() && level->clipped;
    c.save();
    if (clipped) c.setGlow(t.color.meterHigh, 6.0);
    c.fill(Path::roundedRect(clip.light, 2.0), withAlpha(t.color.meterHigh, clipped ? 1.0 : 0.22));
    c.restore();
  }
  icon(c, Icon::Gear, {l.settings.x + 16.0, l.settings.y + 16.0}, 22.0, t.color.textSecondary);
  // The language control: the current language's code in a keyline, the gear's neighbour.
  if (l.language.width > 0.0) {
    std::string code{preferences_.language};
    std::transform(code.begin(), code.end(), code.begin(),
                   [](char ch) { return ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch; });
    const auto box = ui::Rect{l.language.x + 3.0, l.language.y + 6.0, l.language.width - 6.0,
                              l.language.height - 12.0};
    c.stroke(Path::roundedRect(box, 5.0), t.color.textSecondary, StrokeStyle{1.25});
    c.text(box, code, style(FontRole::UiSemibold, t.type.rulerMicro + 1.0, 0.4, TextAlign::Center),
           t.color.textSecondary);
  }

  // The protagonist's avatar, where the header has room for it: the same state the SINGER card
  // spells out, so the header never disagrees with the rack. It is the singer's status, not a
  // control, so it carries no actions and is excluded from the accessibility tree with the rest of
  // the decorative character artwork.
  if (l.headerAvatar.width > 0.0) {
    // The avatar blinks and breathes: a dynamic item, drawn through both fronts at composition.
    const paint::LayerScope avatarLayer{c, paint::Layer::Dynamic, "avatar"};
    const auto* package = characterDisplay_ == domain::CharacterDisplayMode::Off
        ? nullptr : character_.avatar(characterState_);
    const auto* look = lookPortrait();
    const auto bounds = l.headerAvatar;
    const auto pose = characterState_;
    const auto blink = motion_.blink;
    const auto breath = motion_.breath;
    // The lid closes over this state's eyes, in the outfit the mode draws.
    auto eyes = character_.ringEyes(pose);
    const auto lidTone = character_.ringLidTone(pose);
    paint::ContentHash h;
    h.add(std::string_view{"avatar"}).add(static_cast<const void*>(&t)).add(bounds)
        .add(static_cast<std::uint64_t>(pose)).add(std::string_view{character_.outfit()})
        .add(static_cast<const void*>(package)).add(static_cast<const void*>(look)).add(blink)
        .add(breath);
    const auto hash = addLid(h, eyes, lidTone).value();
    characterArt(c, grown(bounds, kGlowReach), hash,
                 [&t, bounds, pose, package, look, blink, breath, eyes = std::move(eyes),
                  lidTone](CharacterCanvas art) {
                   static_cast<void>(paintCharacterAvatar(art, t, bounds, pose, package, look, 1.0,
                                                          blink, breath, eyes, lidTone));
                 });
    // What the avatar returns, known before it is drawn: motion shows where a figure is drawn.
    motionShown_ |= characterMotionShown(pose, portraitDraws(bounds, package, look), false);
  }
}

void SingShell::paintWorkspaceMenu(Canvas2D& c, const DesignTokens& t) const {
  const auto& l = layout_;
  if (l.workspaceMenu.width <= 0.0) return;
  c.save();
  c.setGlow(withAlpha(t.color.accent, 0.28), 12.0);
  c.fill(Path::roundedRect(l.workspaceMenu, 9.0), t.color.surfaceRaised);
  c.stroke(Path::roundedRect(l.workspaceMenu, 9.0), t.color.borderStrong, StrokeStyle{1.0});
  c.restore();
  static constexpr std::array<Str, 5U> kLabels{Str::Sing, Str::Voice, Str::Tune, Str::Mix,
                                                       Str::Export};
  for (std::size_t i = 0U; i < l.workspaceMenuRow.size(); ++i) {
    const auto r = l.workspaceMenuRow[i];
    const auto selected = tabSelected(i);
    if (selected) c.fill(Path::roundedRect(r, 5.0), withAlpha(t.color.accent, 0.18));
    c.text({r.x + 12.0, r.y, r.width - 24.0, r.height}, tr(kLabels[i]),
           style(FontRole::UiSemibold, 13.0), selected ? t.color.accent : t.color.textPrimary);
  }
  for (std::size_t i = 0U; i < l.modeMenuRow.size(); ++i) {
    const auto r = l.modeMenuRow[i];
    if (r.width <= 0.0) continue;
    const auto selected = (i == 0U) == (preferences_.mode == DesignMode::Emo);
    if (selected) c.fill(Path::roundedRect(r, 5.0), withAlpha(t.color.accent, 0.18));
    c.text({r.x + 12.0, r.y, r.width - 24.0, r.height}, i == 0U ? tr(Str::Emo) : tr(Str::Scene),
           style(FontRole::UiSemibold, 13.0), selected ? t.color.accent : t.color.textPrimary);
  }
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
  // Layers (plan section 10): the ruler, keys, grid lines and the Stage figure are the grid layer;
  // notes, curves and labels the content layer; hover, the box selection, the playhead and the
  // lyric field the dynamic layer. Each part below says which it draws into.
  auto* recorder = dynamic_cast<paint::RecordingCanvas*>(&c);
  const auto layerTo = [recorder](paint::Layer layer, std::string_view item = {}) {
    if (recorder != nullptr) recorder->setLayer(layer, item);
  };
  // Tool strip: the real track and project.
  const auto chip = l.trackLabel;
  c.fill(Path::roundedRect(chip, 6.0), withAlpha(t.color.surfaceSunken, 0.85));
  c.stroke(Path::roundedRect(chip, 6.0), t.color.border, StrokeStyle{1.0});
  const auto trackName = state.inspector.valid && !state.inspector.name.empty()
                             ? state.inspector.name
                             : std::string{tr(Str::NoTrack)};
  const auto trackStyle = style(FontRole::UiSemibold, t.type.label, 0.6, TextAlign::Left, true);
  c.text({chip.x + 10, chip.y, chip.width - 20, chip.height},
         midEllipsis(c, trackName, trackStyle, chip.width - 20.0), trackStyle,
         t.color.textPrimary);
  const auto name = state.projectName.empty() ? std::string{tr(Str::Untitled)} : state.projectName;
  const auto project = state.dirty ? trf(Str::Edited, {name}) : name;
  const auto projectStyle = style(FontRole::Ui, t.type.label);
  c.text({chip.right() + 14.0, chip.y, l.gridLabel.x - chip.right() - 24.0, chip.height},
         midEllipsis(c, project, projectStyle, l.gridLabel.x - chip.right() - 24.0),
         projectStyle, t.color.textSecondary);
  // Whether the notes carry the current render's waveform, and if not, the short reason (the full
  // reason is published to accessibility).
  if (l.gridLabel.width > 0.0 && l.gridLabel.x > chip.right() + 24.0 && !waveform_.caption.empty()) {
    const auto captionStyle = style(FontRole::UiSemibold, t.type.smallLabel, 0.6, TextAlign::Right, true);
    c.text(l.gridLabel, waveform_.caption,
           fitted(c, waveform_.caption, captionStyle, l.gridLabel.width),
           waveform_.shown() ? t.color.waveInNote : t.color.textSecondary);
  }

  // Ruler and grid lines share one tick-to-x transform with the lane below.
  layerTo(paint::Layer::Grid);
  const auto& timeline = model.timeline();
  const auto quarter = time::Tick{timeline.ppq()};
  const auto visibleStart = timeline.pixelToTick(0.0);
  const auto visibleEnd = timeline.pixelToTick(l.grid.width);
  const auto quartersPerBar = std::max<std::int64_t>(
      1, static_cast<std::int64_t>(state.meter.numerator) * 4 /
             std::max<std::int64_t>(1, state.meter.denominator));
  c.save();
  c.clipRect(l.grid);
  const auto& pitch = model.pitch();
  Path weakRows;
  const auto flushWeakRows = [&] {
    if (weakRows.empty()) return;
    c.stroke(weakRows, t.color.gridWeak, StrokeStyle{0.6});
    weakRows = Path{};
  };
  for (auto midi = pitch.topMidiKey(); midi >= 0; --midi) {
    const auto y = l.grid.y + pitch.midiToPixel(midi);
    if (y > l.grid.bottom()) break;
    const auto black = midi % 12 == 1 || midi % 12 == 3 || midi % 12 == 6 || midi % 12 == 8 ||
                       midi % 12 == 10;
    // A row fill or stronger octave stroke ends a run. The remaining weak lines are separated
    // by a whole pitch row, so their coverage cannot overlap when replayed in one path.
    if (black || midi % 12 == 0) flushWeakRows();
    if (black) c.fill(Path::rect({l.grid.x, y, l.grid.width, pitch.rowHeight()}),
                      withAlpha(kBlack, 0.22));
    if (midi % 12 == 0) {
      Path row;
      row.moveTo({l.grid.x, y}).lineTo({l.grid.right(), y});
      c.stroke(row, t.color.gridStrong, StrokeStyle{1.0});
    } else {
      weakRows.moveTo({l.grid.x, y}).lineTo({l.grid.right(), y});
      if (black) flushWeakRows();
    }
  }
  flushWeakRows();
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
  // The re-homed overlays' openers, drawn from the same rectangles their semantics publish.
  if (l.rulerTimeMapButton.width > 0.0) {
    const auto& b = l.rulerTimeMapButton;
    c.fill(Path::roundedRect(b, 6.0), withAlpha(t.color.surfaceSunken, 0.9));
    c.stroke(Path::roundedRect(b, 6.0), withAlpha(t.color.border, 0.95), StrokeStyle{1.0});
    c.text(b, tr(Str::TimeMap),
           fitted(c, tr(Str::TimeMap),
                  style(FontRole::UiSemibold, t.type.smallLabel, 1.0, TextAlign::Center, true),
                  b.width - 10.0),
           t.color.textSecondary);
  }

  // Keyboard.
  c.save();
  c.clipPath(Path::roundedRect(l.keyboard, 4.0));
  std::optional<ui::Rect> whiteKeys;
  const auto flushWhiteKeys = [&] {
    if (!whiteKeys) return;
    c.fill(Path::rect(*whiteKeys), t.color.keyWhite);
    whiteKeys.reset();
  };
  Path blackKeys;
  const auto flushBlackKeys = [&] {
    if (blackKeys.empty()) return;
    c.fill(blackKeys, t.color.keyBlack);
    blackKeys = Path{};
  };
  for (auto midi = pitch.topMidiKey(); midi >= 0; --midi) {
    const auto y = l.keyboard.y + pitch.midiToPixel(midi);
    if (y > l.keyboard.bottom()) break;
    const auto black = midi % 12 == 1 || midi % 12 == 3 || midi % 12 == 6 || midi % 12 == 8 ||
                       midi % 12 == 10;
    // Every row is white underneath: black keys are short keys laid over the white ones, and only
    // the B|C and E|F boundaries draw a full-width seam, as on a real keyboard.
    const auto key = ui::Rect{l.keyboard.x, y, l.keyboard.width, pitch.rowHeight()};
    // Device-aligned opaque rows have the same coverage as their union. Fractional rows keep
    // their individual fills because merging them would remove the antialiased internal edge.
    const auto aligned = [&](double value) {
      return std::abs(value * c.scale() - std::round(value * c.scale())) < 1e-6;
    };
    if (aligned(key.x) && aligned(key.right()) && aligned(key.y) && aligned(key.bottom())) {
      if (whiteKeys && whiteKeys->bottom() == key.y)
        whiteKeys->height += key.height;
      else {
        flushWhiteKeys();
        whiteKeys = key;
      }
    } else {
      flushWhiteKeys();
      c.fill(Path::rect(key), t.color.keyWhite);
    }
    const auto degree = midi % 12;
    if (!black && (degree == 11 || degree == 4)) {
      flushWhiteKeys();
      Path edge;
      edge.moveTo({key.x, y}).lineTo({key.right(), y});
      c.stroke(edge, withAlpha(kBlack, 0.28), StrokeStyle{0.8});
    }
  }
  flushWhiteKeys();
  for (auto midi = pitch.topMidiKey(); midi >= 0; --midi) {
    const auto y = l.keyboard.y + pitch.midiToPixel(midi);
    if (y > l.keyboard.bottom()) break;
    const auto black = midi % 12 == 1 || midi % 12 == 3 || midi % 12 == 6 || midi % 12 == 8 ||
                       midi % 12 == 10;
    if (black)
      appendSubpaths(blackKeys, Path::roundedRect(
          {l.keyboard.x, y + 1.0, l.keyboard.width * 0.6, pitch.rowHeight() - 2.0}, 2.0));
    if (midi % 12 == 0) {
      flushBlackKeys();
      c.text({l.keyboard.x, y, l.keyboard.width - 4.0, pitch.rowHeight()},
             "C" + std::to_string(midi / 12 - 1), style(FontRole::UiSemibold, 10.0, 0.0, TextAlign::Right),
             t.color.keyLabel);
    }
  }
  flushBlackKeys();
  c.restore();

  const auto notes = model.visibleNotes();
  const auto* region = model.project().findRegion(model.regionId());
  if (animatedRegion_ != model.regionId()) {
    animatedRegion_ = model.regionId();
    seenNotes_.clear();
    addedNotes_.clear();
    if (region != nullptr)
      for (const auto& note : region->notes) seenNotes_.insert(note.id);
  } else if (region != nullptr) {
    std::unordered_set<domain::NoteId> current;
    for (const auto& note : region->notes) {
      current.insert(note.id);
      if (!seenNotes_.contains(note.id))
        addedNotes_[note.id].start(frameNow_, std::chrono::milliseconds{120}, preferences_.reduceMotion);
    }
    seenNotes_ = std::move(current);
    std::erase_if(addedNotes_, [this](const auto& entry) {
      return !seenNotes_.contains(entry.first) ||
             !entry.second.running(frameNow_, preferences_.reduceMotion);
    });
  }
  // The singer stands behind the notes and fades back whenever a note or the pointer shares her
  // space. She is off in High Contrast, off with an expanded lane, off without the full rack
  // (§3.4 compact widths) and off unless the project's character display is Full, and she is never
  // hit-testable or published to accessibility.
  StageInput stageInput;
  stageInput.fullRack = l.rack == RackPresentation::Full && l.grid.width > 520.0;
  stageInput.highContrast = preferences_.contrast == Contrast::High;
  stageInput.laneExpanded = laneExpanded(state);
  stageInput.displayFull = state.characterMode == domain::CharacterDisplayMode::Full;
  stageInput.grid = l.grid;
  stageInput.splashShown = notes.empty() && model.noteCount() == 0U &&
                           emptyProjectSplashBounds(l.grid, assets().splash.get()).has_value();
  const auto* layeredStage = character_.stageManifest();
  const auto stageAspect = layeredStage != nullptr
      ? static_cast<double>(layeredStage->width) / layeredStage->height
      : assets().stage ? static_cast<double>(assets().stage->width()) / assets().stage->height()
                       : 0.0;
  auto placement = resolveStage(stageInput, stageAspect);
  for (const auto& note : notes) {
    auto bounds = note.bounds;
    bounds.y += l.grid.y;
    if (intersects(bounds, placement.bounds)) {
      stageInput.noteIntersects = true;
      break;
    }
  }
  if (pointerPosition_.has_value()) {
    const auto& p = *pointerPosition_;
    stageInput.pointerInside =
        p.x >= placement.bounds.x && p.x < placement.bounds.right() && p.y >= placement.bounds.y &&
        p.y < placement.bounds.bottom();
  }
  placement = resolveStage(stageInput, stageAspect);
  stagePlacement_ = placement;
  stageShown_ = placement.shown;
  if (placement.shown && (layeredStage != nullptr || assets().stage)) {
    // The figure is drawn before the notes and the pitch curve, so it sits below them, and the fade's
    // ticking is what keeps frames coming while it settles. Reduce Motion applies the change at once.
    const auto opacity = stageFade_.advance(placement, frameNow_, preferences_.reduceMotion);
    const auto& layers = character_.stageLayers();
    if (!layers.empty()) {
      for (const auto& layer : layers)
        paintStageFigure(characterCanvas(c), l.grid, placement, opacity, *layer);
      const auto eyes = characterState_ == CharacterState::Singing
          ? character::StageEyes::Half
          : motion_.blink > 0.5 ? character::StageEyes::Closed : character::StageEyes::Open;
      if (const auto* sprite = character_.stageEyes(eyes); sprite != nullptr) {
        const auto scaleX = placement.bounds.width / layeredStage->width;
        const auto scaleY = placement.bounds.height / layeredStage->height;
        const auto& box = layeredStage->eyeBox;
        c.save();
        c.clipRect(l.grid);
        c.drawImage(*sprite, {placement.bounds.x + box.x * scaleX,
                              placement.bounds.y + box.y * scaleY,
                              box.width * scaleX, box.height * scaleY}, opacity);
        c.restore();
      }
    } else if (assets().stage) {
      paintStageFigure(characterCanvas(c), l.grid, placement, opacity, *assets().stage);
    }
  } else {
    static_cast<void>(stageFade_.advance(placement, frameNow_, preferences_.reduceMotion));
  }
  stagePointerAt_ = pointerPosition_;

  c.save();
  c.clipRect(l.grid);
  // Score pitch line: note targets with short glides between adjacent notes, broken at rests. It is
  // the last thing the grid layer draws: it lies directly on the grid and under every note, and it
  // follows the notes' places but not their selection, so selecting notes never redraws its glow.
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
  layerTo(paint::Layer::Content);

  // Notes as capsules.
  std::vector<ui::Rect> capsules;
  for (const auto& note : notes) {
    if (note.hiddenByOverlapDensity) continue;
    const auto b = singNoteCapsuleBounds(note, l.grid);
    capsules.push_back(b);
    if (const auto added = addedNotes_.find(note.noteId);
        added != addedNotes_.end() && added->second.running(frameNow_, preferences_.reduceMotion))
      continue;
    const auto radius = std::min(t.shape.note, b.height * 0.5);
    const auto p = Path::roundedRect(b, radius);
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
      if (note.overlapMemberCount > 1U) {
        // The old inset and halo consumed most of a compressed band's height. A filled body
        // separates the members without changing any note's musical duration or pitch.
        c.fill(p, withAlpha(t.color.noteStroke, 0.30 + 0.10 * static_cast<double>(note.overlapBand)));
      }
      wave();
      c.save();
      c.setGlow(withAlpha(t.color.noteStroke, 0.55), note.overlapMemberCount > 1U ? 1.0 : 5.0);
      c.stroke(p, withAlpha(t.color.noteStroke, 0.8), StrokeStyle{1.1});
      c.restore();
    }
  }
  layerTo(paint::Layer::Dynamic, "note-add");
  for (const auto& note : notes) {
    const auto added = addedNotes_.find(note.noteId);
    if (added == addedNotes_.end() || note.hiddenByOverlapDensity) continue;
    const auto progress = added->second.eased(frameNow_, preferences_.reduceMotion);
    const auto scale = 0.92 + 0.08 * progress;
    auto bounds = singNoteCapsuleBounds(note, l.grid);
    const auto dx = bounds.width * (1.0 - scale) * 0.5;
    const auto dy = bounds.height * (1.0 - scale) * 0.5;
    bounds = {bounds.x + dx, bounds.y + dy, bounds.width * scale, bounds.height * scale};
    const auto capsule = Path::roundedRect(bounds, std::min(t.shape.note, bounds.height * 0.5));
    c.save();
    c.setGlow(withAlpha(t.color.noteSelectedA, (1.0 - progress) * 0.7), 10.0);
    c.fill(capsule, note.selected ? t.color.noteSelectedA : t.color.noteFill);
    c.stroke(capsule, t.color.noteStroke, StrokeStyle{1.1});
    c.restore();
  }
  // Hover and keyboard focus brighten an unselected note's outline. They change under the pointer
  // on any frame, so the brighter outline is a dynamic item drawn over the note: moving the pointer
  // repaints that note's rectangle, never the content layer.
  layerTo(paint::Layer::Dynamic, "hover");
  for (const auto& note : notes) {
    if (note.hiddenByOverlapDensity || note.selected) continue;
    if (state.hoveredNote != note.noteId && state.focusedNote != note.noteId) continue;
    const auto b = singNoteCapsuleBounds(note, l.grid);
    c.stroke(Path::roundedRect(b, std::min(t.shape.note, b.height * 0.5)), t.color.noteStroke,
             StrokeStyle{1.6});
  }
  layerTo(paint::Layer::Content);

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
      // The handle holding keyboard focus (Alt+V, then arrows) wears a focus ring.
      const auto dot = [&](std::optional<ui::Point> p, double r, VibratoHandleKind kind) {
        if (!p) return;
        c.fill(Path::circle(*p, r), t.color.focusRing);
        c.stroke(Path::circle(*p, r), t.color.canvas, StrokeStyle{1.0});
        if (state.vibratoKeyboardFocus == kind)
          c.stroke(Path::circle(*p, r + 3.0), t.color.focusRing, StrokeStyle{2.0});
      };
      dot(handles->onset, 4.0, VibratoHandleKind::Onset);
      dot(handles->depth, 4.0, VibratoHandleKind::Depth);
      dot(handles->fadeIn, 3.0, VibratoHandleKind::FadeIn);
      dot(handles->fadeOut, 3.0, VibratoHandleKind::FadeOut);
      dot(handles->period, 3.5, VibratoHandleKind::Period);
      dot(handles->phase, 3.0, VibratoHandleKind::Phase);
    }
  }

  const auto badges = layoutSingOverlapBadges(notes, l.grid);
  // Badge slots also reserve space from lyric labels; a count must never obscure a syllable.
  for (const auto& badge : badges) capsules.push_back(badge.bounds);
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
      // Selected lyrics are placed first. Hover does not reorder labels: a label never jumps under
      // the pointer, and hovering repaints only the hovered note.
      const auto priority = note.selected ? 0 : 1;
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
      // Inside the note first, above it only as a fallback. This used to be the
      // other way round, and because the above-note slot almost always succeeds
      // every kana floated above its note instead of sitting in it. That also made
      // the label ride up and down with each note's pitch, because the slot is
      // anchored to the note's top edge -- which is the vertical jitter this
      // arrangement caused. Notation reads the other way: the syllable belongs to
      // the note, and a label only steps out when the note is too small or too
      // crowded to hold it.
      const std::array<ui::Rect, 2U> slots{
          ui::Rect{b.x + 4.0, b.y, width, b.height},
          ui::Rect{b.x - 2.0, b.y - 19.0, width, 18.0}};
      for (std::size_t s = 0U; s < slots.size(); ++s) {
        const auto slot = slots[s];
        const auto inside = s == 0U;
        if (inside && (b.height < 15.0 || width > b.width - 6.0)) continue;
        if (!inside && (slot.y < l.grid.y || slot.right() > l.grid.right())) continue;
        bool free = std::none_of(placed.begin(), placed.end(),
                                 [&](const ui::Rect& other) { return intersects(slot, other); });
        free = free && std::none_of(badges.begin(), badges.end(), [&](const auto& badge) {
          return intersects(slot, badge.bounds);
        });
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

  // Small stacks are discoverable too; editing each member remains in the detail popover.
  for (const auto& badge : badges) {
    c.fill(Path::capsule(badge.bounds), t.color.surfaceRaised);
    c.stroke(Path::capsule(badge.bounds), t.color.focusRing, StrokeStyle{1.0});
    c.text(badge.bounds, "\xC3\x97" + std::to_string(badge.members),
           style(FontRole::UiBold, t.type.label, 0.0, TextAlign::Center), t.color.focusRing);
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
    static constexpr std::array<Str, 4U> kWhere{Str::NotesAbove, Str::NotesBelow, Str::NotesEarlier,
                                                Str::NotesLater};
    const auto count = away[direction] > 0U ? away[direction] : total;
    const auto hint = trf(kWhere[direction], {std::to_string(count)});
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
    // The region genuinely has no notes: the seated pose and the instruction to write the first one.
    // No pose asset is declared by this package, so the state portrait stands in for it and the line
    // is the shell's own, shown only here.
    const auto* package = character_.pose(character::Pose::Empty) != nullptr &&
                                  characterDisplay_ != domain::CharacterDisplayMode::Off
                              ? character_.pose(character::Pose::Empty)
                              : characterPortrait(CharacterState::Idle);
    const auto* look = lookPortrait();
    // The mode's key art stands in for the seated pose when the roll can hold it; it is character
    // artwork too, so a display that is Off draws only the line.
    const auto* splash =
        characterDisplay_ == domain::CharacterDisplayMode::Off ? nullptr : assets().splash.get();
    const auto hash = paint::ContentHash{}
                          .add(std::string_view{"empty-project"}).add(static_cast<const void*>(&t))
                          .add(l.grid).add(static_cast<const void*>(package))
                          .add(static_cast<const void*>(look)).add(static_cast<const void*>(splash))
                          .add(std::string_view{character_.outfit()}).value();
    characterArt(c, l.grid, hash, [&t, &l, package, look, splash](CharacterCanvas art) {
      paintEmptyProject(art, t, l, package, look, splash);
    });
  }

  if (state.boxSelection.has_value()) {
    layerTo(paint::Layer::Dynamic, "selection-box");
    const auto box = fromLegacy(*state.boxSelection);
    c.fill(Path::rect(box), t.color.selectionFill);
    c.stroke(Path::rect(box), t.color.accent, StrokeStyle{1.0});
  }
  if (state.playheadPixel >= 0.0) {
    layerTo(paint::Layer::Dynamic, "playhead");
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
    layerTo(paint::Layer::Dynamic, "lyric-editor");
    const auto textStyle = style(FontRole::Ui, t.type.lyric);
    const auto textWidth =
        state.compositionPreview.empty() ? 0.0 : c.measure(state.compositionPreview, textStyle);
    const auto editor = singLyricEditorBounds(fromLegacy(*state.lyricEditor), textWidth, l.grid);
    c.fill(Path::roundedRect(editor, 6.0), t.color.surfaceSunken);
    c.save();
    c.setGlow(t.color.accent, 8.0);
    c.stroke(Path::roundedRect(editor, 6.0), t.color.accent, StrokeStyle{1.5});
    c.restore();
    if (!state.compositionPreview.empty())
      c.text({editor.x + kLyricEditorInset, editor.y, editor.width - 2.0 * kLyricEditorInset,
              editor.height},
             state.compositionPreview, textStyle, t.color.textPrimary);
  }
  layerTo(paint::Layer::Content);
}

void SingShell::paintLane(Canvas2D& c, const DesignTokens& t, const ui::PianoRollModel& model,
                          const EditorSceneState& state) const {
  const auto& l = layout_;
  static constexpr std::array<Str, 8U> kTabs{Str::Dynamics, Str::Formant, Str::Breath, Str::Tension,
                                             Str::Air,      Str::Gender,  Str::Growl,  Str::Phonemes};
  const auto open = state.expressionLabelVisible();
  const auto selected = technicalLane_ ? 7U
                        : open         ? ui::expressionChannelIndex(state.expression.channel) + 1U
                                       : 99U;
  const auto tabWidth = singLaneTabWidth(l);
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
    c.text(tab, tr(kTabs[i]),
           fitted(c, tr(kTabs[i]),
                  style(FontRole::UiSemibold, t.type.smallLabel, 1.0, TextAlign::Center, true),
                  tab.width - 8.0),
           active ? t.color.accent : t.color.textSecondary);
  }
  const ui::Rect info{l.laneTabs.x + kTabs.size() * (tabWidth + 4.0) + 8.0, l.laneTabs.y,
                      std::max(0.0, (l.laneReviewButton.width > 0.0 ? l.laneReviewButton.x - 8.0
                                                                     : l.laneTabs.right()) -
                                        (l.laneTabs.x + kTabs.size() * (tabWidth + 4.0) + 8.0)),
                      l.laneTabs.height};
  const auto plot = l.laneTimePlot;
  sunken(c, t, l.lanePlot, 6.0);
  // The retained-edit review opener, at the same rectangle its node is published in.
  if (l.laneReviewButton.width > 0.0) {
    const auto& b = l.laneReviewButton;
    c.fill(Path::roundedRect(b, 6.0), withAlpha(t.color.surfaceSunken, 0.9));
    c.stroke(Path::roundedRect(b, 6.0), withAlpha(t.color.border, 0.95), StrokeStyle{1.0});
    const std::string_view label{tr(Str::Review)};
    c.text(b, label,
           fitted(c, label,
                  style(FontRole::UiSemibold, t.type.smallLabel, 1.0, TextAlign::Center, true),
                  b.width - 10.0),
           t.color.textSecondary);
  }
  if (technicalLane_) {
    // B auditions the seam's alternate render only where the host can play it; a plug-in cannot.
    const auto hint = !state.selectedSeam.has_value() ? std::string{tr(Str::DragPhonemeEdgesClickAUnit)}
                      : !state.seamPreviewConnected
                          ? std::string{tr(Str::SeamArrowsEditCCurveB2)}
                      // Whole sentences, so a translation never assembles "B" + a loose word.
                      : state.seamPreviewAlternate ? std::string{tr(Str::SeamHintAlternatePreview)}
                                                   : std::string{tr(Str::SeamHintBasePreview)};
    if (info.width > 24.0)
      c.text(info, hint,
             fitted(c, hint, style(FontRole::Ui, t.type.smallLabel, 0.0, TextAlign::Right),
                    info.width),
             t.color.textSecondary);
    paintTechnicalLanes(c, t, model, state);
    return;
  }
  if (!open) {
    // The unused band gives readable guidance, not a hint elided between eight tabs. Failure
    // toasts own this space when present; do not put competing instructions under them.
    if (characterState_ != CharacterState::Error && state.diagnostics.empty()) {
      c.save();
      c.clipRect(plot);
      const auto heading = style(FontRole::UiSemibold, t.type.body, 0.0, TextAlign::Center);
      const auto body = style(FontRole::Ui, t.type.label, 0.0, TextAlign::Center);
      const auto lines = wrapTooltipText(tr(Str::LaneGettingStarted),
          std::max(1.0, plot.width - 32.0), body,
          [&](std::string_view text, const TextStyle& textStyle) { return c.measure(text, textStyle); },
          plot.height >= 76.0 ? 2U : 1U);
      const auto total = 24.0 + static_cast<double>(lines.size()) * 18.0;
      auto y = plot.y + std::max(0.0, (plot.height - total) * 0.5);
      c.text({plot.x + 16.0, y, std::max(1.0, plot.width - 32.0), 22.0},
             tr(Str::SelectAChannelToDrawIts), heading, t.color.textPrimary);
      y += 24.0;
      for (const auto& line : lines) {
        c.text({plot.x + 16.0, y, std::max(1.0, plot.width - 32.0), 18.0}, line,
               body, t.color.textSecondary);
        y += 18.0;
      }
      c.restore();
    }
    return;
  }
  const auto& e = state.expression;
  const auto descriptor = ui::describeExpressionChannel(e.channel);
  const auto scale = descriptor.unit == "semitones" ? 1.0 : 100.0;
  const auto value = format("%.1f", e.valueAtPlayhead * scale);
  std::string infoText = descriptor.unit == "semitones"
                             ? trf(Str::SemitonesAtPlayhead, {value})
                             : trf(Str::PercentAtPlayhead, {value});
  if (e.draftChanged) infoText = trf(Str::WithUnsavedDraft, {infoText});
  c.text(info, infoText, style(FontRole::Ui, t.type.smallLabel, 0.0, TextAlign::Right),
         t.color.textSecondary);
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
  // The lane's first line: why the selected singer refuses this channel, else that no curve is
  // stored. The refusal lives here, at every window size, because the tab row's info slot is only
  // a few points wide below very wide windows; the elided text is whole on the lane's node.
  const ui::Rect firstLine{plot.x + 12.0, plot.y + 4.0, plot.width - 24.0, 18.0};
  if (!e.refusal.empty())
    c.text(firstLine, e.refusal, style(FontRole::Ui, t.type.smallLabel), t.color.warning);
  if (e.points.empty()) {
    if (e.refusal.empty())
      c.text(firstLine, tr(Str::NoCurveStoredForThisChannel), style(FontRole::Ui, t.type.smallLabel),
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
    const paint::LayerScope playheadLayer{c, paint::Layer::Dynamic, "lane-playhead"};
    Path head;
    head.moveTo({plot.x + state.playheadPixel, plot.y}).lineTo({plot.x + state.playheadPixel, plot.bottom()});
    c.stroke(head, withAlpha(t.color.accentTime, 0.7), StrokeStyle{1.0});
  }
  c.restore();
}


// ---- Phonemes lane -----------------------------------------------------------------------------

namespace {

constexpr std::array<domain::TechnicalLane, 3U> kTechnicalBandLanes{
    domain::TechnicalLane::Phoneme, domain::TechnicalLane::Unit, domain::TechnicalLane::Seam};
constexpr std::array<Str, 3U> kTechnicalBandNames{Str::Phoneme, Str::Unit, Str::Seam};
constexpr std::array<const char*, 3U> kTechnicalBandIds{"phoneme", "unit", "seam"};
// A collapsed band keeps a strip this tall for its label; the open bands share the rest.
constexpr double kCollapsedBandHeight = 12.0;

std::string unitRendererLabel(domain::UnitRendererKind kind) {
  switch (kind) {
    case domain::UnitRendererKind::Raw: return tr(Str::RAW);
    case domain::UnitRendererKind::ClassicPsola: return tr(Str::PSOLA);
    case domain::UnitRendererKind::SpectralClassic: return tr(Str::SPEC);
    case domain::UnitRendererKind::Stretch: return tr(Str::STR);
    case domain::UnitRendererKind::Inherit: return tr(Str::AUTO);
  }
  return tr(Str::AUTO);
}

}  // namespace

namespace {

// The SINGER card's ⋯ button beside Change voice, in the same capsule. It lights while its menu
// is open and carries the focus ring while it holds the keyboard.
void paintSingerMenuButton(Canvas2D& c, const DesignTokens& t, ui::Rect r, bool open, bool focused) {
  if (r.width <= 0.0 || r.height <= 0.0) return;
  c.fill(Path::capsule(r), withAlpha(t.color.accent, open ? 0.32 : 0.14));
  c.stroke(Path::capsule(r), withAlpha(t.color.accent, 0.8), StrokeStyle{1.0});
  const ui::Point centre{r.x + r.width * 0.5, r.y + r.height * 0.5};
  for (const auto dx : {-5.0, 0.0, 5.0})
    c.fill(Path::circle({centre.x + dx, centre.y}, 1.6), t.color.accent);
  if (!focused) return;
  c.save();
  c.setGlow(t.color.focusRing, 6.0);
  c.stroke(Path::capsule({r.x - 2.0, r.y - 2.0, r.width + 4.0, r.height + 4.0}), t.color.focusRing,
           StrokeStyle{2.0});
  c.restore();
}

}  // namespace

SingShell::TechnicalBands SingShell::technicalBands() const noexcept {
  TechnicalBands out;
  out.collapsed = technicalCollapsed_;
  const auto& l = layout_;
  const auto plot = l.laneTimePlot;
  constexpr std::array<double, 3U> kWeight{0.38, 0.34, 0.28};
  double openWeight = 0.0;
  double collapsedHeight = 0.0;
  for (std::size_t i = 0U; i < 3U; ++i) {
    if (out.collapsed[i]) collapsedHeight += kCollapsedBandHeight;
    else openWeight += kWeight[i];
  }
  const auto free = std::max(0.0, plot.height - collapsedHeight);
  auto y = plot.y;
  for (std::size_t i = 0U; i < 3U; ++i) {
    const auto wanted = out.collapsed[i] ? kCollapsedBandHeight
                        : openWeight > 0.0 ? std::floor(free * kWeight[i] / openWeight)
                                           : 0.0;
    const auto height = std::clamp(wanted, 0.0, std::max(0.0, plot.bottom() - y));
    out.band[i] = {plot.x, y, plot.width, height};
    // The label sits in the lane's value gutter, left of the shared musical axis.
    out.toggle[i] = {l.lanePlot.x, y, std::max(0.0, plot.x - l.lanePlot.x - 2.0), height};
    y += height;
  }
  // Rounding leaves the last open band the remainder, so the bands fill the plot exactly.
  for (std::size_t i = 3U; i-- > 0U;) {
    if (out.collapsed[i]) continue;
    const auto extra = plot.bottom() - y;
    out.band[i].height += extra;
    out.toggle[i].height += extra;
    for (std::size_t j = i + 1U; j < 3U; ++j) {
      out.band[j].y += extra;
      out.toggle[j].y += extra;
    }
    break;
  }
  return out;
}

void SingShell::syncTechnicalBands(const NativeEditorController& controller) {
  const auto& lanes = controller.project().settings().technicalLanes;
  for (std::size_t i = 0U; i < 3U; ++i)
    technicalCollapsed_[i] =
        lanes[static_cast<std::size_t>(kTechnicalBandLanes[i])].mode ==
        domain::TechnicalLaneMode::Collapsed;
}

core::Result<void> SingShell::showTechnicalLanes(NativeEditorController& controller) {
  if (forwarding_ != ForwardArea::None) cancelGestures(controller);
  // One curve at a time owns the band: an open expression lane is closed first, so its channel
  // keys cannot edit a curve that is no longer drawn.
  if (controller.expressionLaneOpen()) {
    auto closed = controller.closeExpressionLane();
    if (!closed) return closed;
  }
  technicalLane_ = true;
  syncTechnicalBands(controller);
  if (presented_) applyGeometry(controller);
  repaint();
  return core::success();
}

core::Result<void> SingShell::toggleTechnicalBand(NativeEditorController& controller,
                                                  std::size_t band) {
  if (band >= kTechnicalBandLanes.size())
    return core::failure(core::ErrorCode::InvalidArgument, tr(Str::UnknownTechnicalLane));
  if (forwarding_ != ForwardArea::None) cancelGestures(controller);
  auto result = controller.setTechnicalLaneCollapsed(kTechnicalBandLanes[band],
                                                     !technicalCollapsed_[band]);
  syncTechnicalBands(controller);
  if (presented_) applyGeometry(controller);
  repaint();
  return result;
}

void SingShell::paintTechnicalLanes(Canvas2D& c, const DesignTokens& t,
                                    const ui::PianoRollModel& model,
                                    const EditorSceneState& state) const {
  const auto plot = layout_.laneTimePlot;
  if (plot.width <= 0.0 || plot.height <= 0.0) return;
  const auto bands = technicalBands();
  // The controller hit-tests these bands with the classic lane metrics, so the shell paints with
  // the same ones: a drawn phoneme edge is exactly where a drag grabs it.
  const EditorSceneLayout metrics{};
  const auto labelStyle = style(FontRole::UiSemibold, t.type.smallLabel, 0.4, TextAlign::Right);
  for (std::size_t i = 0U; i < 3U; ++i) {
    const auto& toggle = bands.toggle[i];
    if (toggle.width > 12.0 && toggle.height >= 10.0) {
      const std::string label = std::string{tr(kTechnicalBandNames[i])} + (bands.collapsed[i] ? " +" : "");
      c.text({toggle.x, toggle.y, toggle.width, std::min(toggle.height, 16.0)}, label,
             fitted(c, label, labelStyle, toggle.width), t.color.textSecondary);
    }
    if (i > 0U) {
      Path divider;
      divider.moveTo({plot.x, bands.band[i].y}).lineTo({plot.right(), bands.band[i].y});
      c.stroke(divider, withAlpha(t.color.gridStrong, 0.8), StrokeStyle{1.0});
    }
  }
  c.save();
  c.clipRect(plot);
  // Phonemes, and the x ranges units and seams are addressed by.
  const auto& phonemeBand = bands.band[0U];
  ui::PhonemeLaneModel lane;
  lane.rebuild(model, state.phonemes, metrics.phonemeContentTop(phonemeBand.y),
               metrics.phonemeContentHeight(phonemeBand.height));
  const auto& visuals = lane.visuals();
  if (!bands.collapsed[0U]) {
    for (const auto& visual : visuals) {
      auto r = visual.bounds;
      if (r.right() < plot.x || r.x > plot.right() || r.width <= 0.0) continue;
      const auto fill = visual.locked ? t.color.accentAlt1 : t.color.noteFill;
      c.fill(Path::roundedRect(r, 3.0), withAlpha(fill, 0.85));
      c.stroke(Path::roundedRect(r, 3.0),
               visual.timingOverridden ? t.color.accent : withAlpha(t.color.border, 0.95),
               StrokeStyle{1.0});
      const auto marker = visual.timingConflict ? "!" : visual.timingInferred ? "^"
                          : visual.timingEstimated                            ? "~"
                                                                              : "";
      const auto text = std::string{marker} + visual.symbol;
      const ui::Rect textBox{r.x + 3.0, r.y, std::max(0.0, r.width - 6.0), r.height};
      if (textBox.width >= 8.0)
        c.text(textBox, text, fitted(c, text, style(FontRole::Mono, t.type.smallLabel), textBox.width),
               t.color.phonemeText);
    }
    if (visuals.empty())
      c.text({plot.x + 8.0, phonemeBand.y, plot.width - 16.0, phonemeBand.height},
             tr(Str::NoPhonemesInThisRegion), style(FontRole::Ui, t.type.smallLabel),
             t.color.textSecondary);
  }
  // Units: each phoneme's span is a unit target; a stored override is drawn as a card.
  const auto& unitBand = bands.band[1U];
  if (!bands.collapsed[1U]) {
    for (const auto& visual : visuals) {
      if (visual.bounds.x < plot.x || visual.bounds.x > plot.right()) continue;
      Path tick;
      tick.moveTo({visual.bounds.x, unitBand.y + 3.0}).lineTo({visual.bounds.x, unitBand.bottom() - 3.0});
      c.stroke(tick, withAlpha(t.color.gridWeak, 0.9), StrokeStyle{1.0});
    }
    for (const auto& unit : state.unitOverrides) {
      const auto start = std::find_if(visuals.begin(), visuals.end(),
                                      [&unit](const ui::PhonemeVisual& v) { return v.key == unit.startKey; });
      if (start == visuals.end()) continue;
      auto end = start;
      for (std::uint16_t n = 1U; n < unit.tokenCount && end + 1 != visuals.end(); ++n) ++end;
      const auto left = std::max(plot.x, start->bounds.x);
      const auto right = std::min(plot.right(), end->bounds.right());
      if (right <= left) continue;
      const ui::Rect card{left, metrics.unitContentTop(unitBand.y), std::max(3.0, right - left),
                          metrics.unitContentHeight(unitBand.height)};
      c.fill(Path::roundedRect(card, 3.0), withAlpha(t.color.noteFillAlt, 0.9));
      c.stroke(Path::roundedRect(card, 3.0), unit.locked ? t.color.accent : withAlpha(t.color.border, 0.95),
               StrokeStyle{1.0});
      const auto text = unit.unitId + "  " + unitRendererLabel(unit.renderer);
      const ui::Rect textBox{card.x + 4.0, card.y, std::max(0.0, card.width - 8.0), card.height};
      if (textBox.width >= 12.0)
        c.text(textBox, text, fitted(c, text, style(FontRole::Mono, t.type.smallLabel), textBox.width),
               t.color.textPrimary);
    }
  }
  // Seams: a rail at each overridden boundary, filled to its amount.
  const auto& seamBand = bands.band[2U];
  if (!bands.collapsed[2U]) {
    for (const auto& seam : state.seamOverrides) {
      const auto visual = std::find_if(visuals.begin(), visuals.end(), [&seam](const ui::PhonemeVisual& v) {
        return v.key == seam.incomingStartKey;
      });
      if (visual == visuals.end()) continue;
      const auto x = visual->bounds.x;
      if (x < plot.x || x > plot.right()) continue;
      const auto amount = std::clamp(static_cast<double>(seam.seamAmount.value_or(0.5F)), 0.0, 1.0);
      const ui::Rect rail{x - metrics.seamRailWidth * 0.5, seamBand.y + 3.0, metrics.seamRailWidth,
                          std::max(1.0, seamBand.height - 6.0)};
      const auto selected = state.selectedSeam.has_value() && *state.selectedSeam == seam.incomingStartKey;
      c.fill(Path::roundedRect(rail, 2.0), withAlpha(t.color.surfaceSunken, 0.95));
      const auto bar = std::max(1.0, rail.height * amount);
      c.fill(Path::roundedRect({rail.x, rail.bottom() - bar, rail.width, bar}, 2.0), t.color.accentCurve);
      c.stroke(Path::roundedRect(rail, 2.0),
               selected ? t.color.accentTime : seam.locked ? t.color.accent : withAlpha(t.color.border, 0.95),
               StrokeStyle{selected ? 2.0 : 1.0});
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
    // The rail or drawer portrait is the inspector's button; it lights while the inspector is open.
    const auto ring = l.inspectorButton;
    const ui::Point center{ring.x + ring.width * 0.5, ring.y + ring.height * 0.5};
    const auto inner = ring.width * 0.5 - 3.0;
    if (l.inspectorOpen) {
      c.save();
      c.setGlow(withAlpha(t.color.accent, 0.9), 10.0);
      c.fill(Path::circle(center, inner + 3.0), withAlpha(t.color.accent, 0.35));
      c.restore();
    }
    c.fill(Path::circle(center, inner), t.color.surfaceSunken);
    const auto railColor = identity.state == VoiceIdentityState::Missing ||
                                   identity.state == VoiceIdentityState::Error
                               ? t.color.error
                               : identity.state == VoiceIdentityState::Warning ? t.color.warning
                                                                              : t.color.accent;
    {
      const ui::Rect box{center.x - inner, center.y - inner, inner * 2.0, inner * 2.0};
      const auto* package = characterPortrait(characterState_);
      const auto* look = lookPortrait();
      const auto opacity = voiceReady ? 1.0 : 0.55;
      const auto hash = paint::ContentHash{}
                            .add(std::string_view{"rail-portrait"}).add(box)
                            .add(static_cast<const void*>(package))
                            .add(static_cast<const void*>(look)).add(opacity).value();
      characterArt(c, box, hash, [box, package, look, opacity](CharacterCanvas art) {
        static_cast<void>(paintCharacterPortrait(art, box, true, package, look, opacity));
      });
    }
    c.save();
    c.setGlow(withAlpha(railColor, 0.9), 6.0);
    c.stroke(Path::circle(center, inner + 1.0), railColor, StrokeStyle{1.6});
    c.restore();
    c.fill(Path::circle({ring.right() - 4.0, ring.bottom() - 4.0}, 4.5), railColor);
    if (l.inspectorOpen) {
      // The drawer covers the score, so it stays above the playhead: a dynamic item while open.
      const paint::LayerScope inspectorLayer{c, paint::Layer::Dynamic, "inspector"};
      paintInspector(c, t, state);
    }
    return;
  }
  cardHeader(c, t, l.singer, tr(Str::Singer), voiceReady);
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
  // Ring: lit ticks follow the real render progress or the singer's measured energy, 64 of them, with
  // the state's own portrait inside.
  double lit = 0.0;
  if (state.characterPerformance && state.characterPerformance->performing)
    lit = std::clamp(static_cast<double>(state.characterPerformance->energy), 0.0, 1.0);
  else if (state.renderStatus.state == RenderStatusState::Rendering)
    lit = std::clamp(state.renderStatus.fraction, 0.0, 1.0);
  else if (state.renderStatus.state == RenderStatusState::Ready && state.renderStatus.hasAudibleAudio)
    lit = 1.0;
  const auto performanceState = characterState_;
  // While singing, the mouth is the shape the published performance reports, drawn from the
  // package's own sprite at its declared placement. A status-only package declares none, so the
  // portrait is shown as it is rather than with a mouth the artwork does not have.
  const PixelSurface* singingMouth = nullptr;
  std::optional<character::MouthPlacement> singingPlacement;
  if (characterState_ == CharacterState::Singing &&
      characterDisplay_ != domain::CharacterDisplayMode::Off) {
    if (const auto& performance = state.characterPerformance; performance.has_value()) {
      // A held, neutral vowel gives singing a stable face when Reduce Motion is enabled.
      singingMouth = character_.ringMouth(preferences_.reduceMotion
          ? character::MouthShape::Open : performance->mouth);
      singingPlacement = character_.ringMouthPlacement();
    }
  }
  const SingerRingSpec spec{
      .bounds = ring,
      .state = performanceState,
      .lit = lit,
      .rotation = motion_.spinner,
      .packagePortrait = characterDisplay_ == domain::CharacterDisplayMode::Off
          ? nullptr : character_.ringPortrait(performanceState),
      .lookPortrait = lookPortrait(),
      .portraitOpacity = voiceReady ? 1.0 : 0.55,
      .mouthSprite = singingMouth,
      .mouthPlacement = singingPlacement,
      .mouthOpacity = voiceReady ? 1.0 : 0.55,
      .breath = motion_.breath,
      .blink = motion_.blink,
      .eyes = character_.ringEyes(performanceState),
      .lidTone = character_.ringLidTone(performanceState),
      // The glow sprites composite straight onto the surface, past the glowless canvas High
      // Contrast replays through, so High Contrast draws the ring without them (and without glow).
      .glows = t.contrast == Contrast::High ? nullptr : &ringGlows_,
  };
  {
    // What holds still while the singer sings (the backdrop and the unlit ticks) is content; only
    // the lit ticks, the portrait and the state ring are redrawn as they move.
    paintSingerRingBase(c, t, spec);
    // The ring lights with the singer's energy and the render, and the portrait breathes and
    // blinks inside it: a dynamic item.
    const paint::LayerScope ringLayer{c, paint::Layer::Dynamic, "ring"};
    paint::ContentHash h;
    h.add(std::string_view{"ring"}).add(static_cast<const void*>(&t)).add(spec.bounds)
        .add(static_cast<std::uint64_t>(spec.state)).add(spec.lit).add(spec.rotation)
        .add(static_cast<const void*>(spec.packagePortrait))
        .add(static_cast<const void*>(spec.lookPortrait)).add(spec.portraitOpacity)
        .add(static_cast<const void*>(spec.mouthSprite)).add(spec.mouthPlacement.has_value())
        .add(spec.mouthOpacity).add(spec.breath).add(spec.blink)
        .add(std::string_view{character_.outfit()});
    addLid(h, spec.eyes, spec.lidTone);
    if (spec.mouthPlacement.has_value())
      h.add(spec.mouthPlacement->x).add(spec.mouthPlacement->y).add(spec.mouthPlacement->width)
          .add(spec.mouthPlacement->height);
    characterArt(c, grown(ring, kGlowReach), h.value(), [&t, spec](CharacterCanvas art) {
      static_cast<void>(paintSingerRingLive(art, t, spec));
    });
    // What paintSingerRing returns, known before it is drawn.
    if (ring.width > 0.0 && ring.height > 0.0)
      motionShown_ |= characterMotionShown(spec.state,
                                           portraitDraws(ring, spec.packagePortrait,
                                                         spec.lookPortrait),
                                           true);
  }

  // Footer: the real voice identity and the way to change it.
  const auto footerY = l.singerChange.y;
  const auto name = !identity.name.empty() ? identity.name
                    : !state.characterName.empty() ? state.characterName
                                                   : std::string{tr(Str::NoVoiceSelected)};
  c.text({l.singer.x + 18.0, footerY - 2.0, l.singerMenu.x - l.singer.x - 28.0, 16.0}, name,
         style(FontRole::UiSemibold, t.type.label, 0.4), t.color.textPrimary);
  const auto detail = identity.state == VoiceIdentityState::Missing && !identity.recovery.empty()
                          ? identity.recovery
                          : identity.identity;
  c.text({l.singer.x + 18.0, footerY + 13.0, l.singerMenu.x - l.singer.x - 28.0, 14.0}, detail,
         style(FontRole::Ui, t.type.smallLabel), t.color.textSecondary);
  c.fill(Path::capsule(l.singerChange), withAlpha(t.color.accent, 0.14));
  c.stroke(Path::capsule(l.singerChange), withAlpha(t.color.accent, 0.8), StrokeStyle{1.0});
  c.text(l.singerChange, tr(Str::ChangeVoice),
         style(FontRole::UiSemibold, t.type.smallLabel, 0.6, TextAlign::Center, true), t.color.accent);
  paintSingerMenuButton(c, t, l.singerMenu, singerMenuOpen_, semanticFocus_ == kSingerMenuButtonId);

  // Expression knobs.
  cardHeader(c, t, l.expression, tr(Str::Expression), state.inspector.valid);
  paintKnobs(c, t, state);

  // Style presets published by the selected voice.
  cardHeader(c, t, l.style, tr(Str::Style), false);
  std::vector<std::string> styles;
  for (const auto& card : state.voicebankCards)
    if (card.id == state.inspector.voicebank.id && !card.id.empty()) styles = card.styles;
  if (styles.empty()) {
    c.text({l.style.x + 18.0, l.style.y + 52.0, l.style.width - 36.0, 18.0},
           tr(Str::TheSelectedVoicePublishesNoStyle),
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

void SingShell::paintInspector(Canvas2D& c, const DesignTokens& t, const EditorSceneState& state) const {
  const auto& l = layout_;
  const auto& identity = state.voiceIdentity;
  // Dim the score it covers, so the inspector reads as a drawer over it, not part of it.
  c.fill(Path::rect({l.editor.x, l.editor.y, l.editor.width, l.lane.bottom() - l.editor.y}),
         withAlpha(t.color.canvas, 0.45));
  c.fill(Path::roundedRect(l.inspector, t.shape.card), t.color.surfaceSunken);
  glassPanel(c, t, l.inspector, t.shape.card);
  c.save();
  c.setGlow(withAlpha(t.color.accent, 0.5), 8.0);
  c.stroke(Path::roundedRect(l.inspector, t.shape.card), withAlpha(t.color.accent, 0.7),
           StrokeStyle{1.2});
  c.restore();

  // Singer row: portrait, voice, state and style, and Change voice.
  const auto voiceReady = identity.state == VoiceIdentityState::Ready ||
                          identity.state == VoiceIdentityState::Complete ||
                          identity.state == VoiceIdentityState::Rendering;
  const auto stateColor = identity.state == VoiceIdentityState::Missing ||
                                  identity.state == VoiceIdentityState::Error
                              ? t.color.error
                              : identity.state == VoiceIdentityState::Warning ? t.color.warning
                                                                              : t.color.success;
  const auto ring = l.portraitRing;
  const ui::Point center{ring.x + ring.width * 0.5, ring.y + ring.height * 0.5};
  const auto inner = ring.width * 0.5 - 2.0;
  c.fill(Path::circle(center, inner), t.color.surfaceSunken);
  {
    const ui::Rect box{center.x - inner, center.y - inner, inner * 2.0, inner * 2.0};
    const auto* package = characterPortrait(characterState_);
    const auto* look = lookPortrait();
    const auto opacity = voiceReady ? 1.0 : 0.55;
    const auto hash = paint::ContentHash{}
                          .add(std::string_view{"inspector-portrait"}).add(box)
                          .add(static_cast<const void*>(package))
                          .add(static_cast<const void*>(look)).add(opacity).value();
    characterArt(c, box, hash, [box, package, look, opacity](CharacterCanvas art) {
      static_cast<void>(paintCharacterPortrait(art, box, true, package, look, opacity));
    });
  }
  c.stroke(Path::circle(center, inner + 1.0), withAlpha(t.color.accent, 0.85), StrokeStyle{1.4});
  const auto name = !identity.name.empty() ? identity.name
                    : !state.characterName.empty() ? state.characterName
                                                   : std::string{tr(Str::NoVoiceSelected)};
  const auto textX = ring.right() + 12.0;
  c.text({textX, l.singer.y + 4.0, l.singerMenu.x - 8.0 - textX, 18.0}, name,
         style(FontRole::UiSemibold, t.type.label, 0.4), t.color.textPrimary);
  std::string styles;
  for (const auto& card : state.voicebankCards)
    if (card.id == state.inspector.voicebank.id && !card.id.empty())
      for (const auto& preset : card.styles) styles += (styles.empty() ? "" : ", ") + preset;
  c.text(l.style,
         styles.empty() ? trf(Str::IdentityNoStylePresets, {voiceIdentityStateName(identity.state)})
                        : trf(Str::IdentityWithStyles,
                              {voiceIdentityStateName(identity.state), styles}),
         style(FontRole::UiMedium, t.type.smallLabel, 0.6), stateColor);
  c.fill(Path::capsule(l.singerChange), withAlpha(t.color.accent, 0.14));
  c.stroke(Path::capsule(l.singerChange), withAlpha(t.color.accent, 0.8), StrokeStyle{1.0});
  c.text(l.singerChange, tr(Str::ChangeVoice),
         style(FontRole::UiSemibold, t.type.smallLabel, 0.6, TextAlign::Center, true), t.color.accent);
  paintSingerMenuButton(c, t, l.singerMenu, singerMenuOpen_, semanticFocus_ == kSingerMenuButtonId);

  cardHeader(c, t, l.expression, tr(Str::Expression), state.inspector.valid);
  paintKnobs(c, t, state);
}

void SingShell::paintKnobs(Canvas2D& c, const DesignTokens& t, const EditorSceneState& state) const {
  const auto& l = layout_;
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
    c.text({kc.x - r + 6.0, kc.y - 11.0, (r - 6.0) * 2.0, 22.0}, refused ? tr(Str::Text2) : number,
           style(FontRole::UiSemibold, number.size() > 5 ? 13.0 : t.type.knobValue, 0.0,
                 TextAlign::Center),
           refused ? t.color.textDisabled : t.color.textPrimary);
    c.text({cell.x, kc.y + r + 1.0, cell.width, 13.0},
           refused ? std::string{tr(Str::Unavailable)} : std::string{unitText},
           style(FontRole::UiMedium, 10.0, 1.0, TextAlign::Center, true),
           refused ? withAlpha(t.color.warning, 0.85) : t.color.textDisabled);
    if (k.storedPoints > 0U && !refused)
      c.fill(Path::capsule({kc.x - 7.0, kc.y + r + 15.0, 14.0, 3.0}), t.color.accent);
  }
}

void SingShell::paintStatus(Canvas2D& c, const DesignTokens& t, const EditorSceneState& state) const {
  const auto& l = layout_;
  const auto& s = state.renderStatus;
  // The diagnostics toast stack above the status bar: the shell's own presentation of what the
  // classic painter drew as a full-width strip. The topmost diagnostic is a toast; the whole stack
  // opens as a popover.
  if (!state.diagnostics.empty()) {
    const auto& diagnostic = state.diagnostics.front();
    const auto presentation = presentDiagnostic(diagnostic);
    const auto tone = diagnostic.severity == authoring::DiagnosticSeverity::Critical
                          ? t.color.error
                          : diagnostic.severity == authoring::DiagnosticSeverity::Warning
                                ? t.color.warning
                                : t.color.info;
    auto title = presentation.title;
    if (state.diagnostics.size() > 1U)
      title = trf(Str::TitleAndMore, {title, std::to_string(state.diagnostics.size() - 1U)});
    lastDiagnosticTitle_ = title;
    lastDiagnosticTone_ = tone;
    const auto toast = diagnosticsToastBounds();
    // The toast stands over the lane, whose playhead is dynamic: it is dynamic too, recorded after
    // that playhead, so a moving playhead never draws across it.
    const paint::LayerScope toastLayer{c, paint::Layer::Dynamic, "diagnostics-toast"};
    c.save();
    if (toastAppearing_ && toastTween_.running(frameNow_, preferences_.reduceMotion))
      c.setAlpha(toastTween_.eased(frameNow_, preferences_.reduceMotion));
    c.setGlow(withAlpha(tone, 0.5), 10.0);
    c.fill(Path::roundedRect(toast, 8.0), withAlpha(t.color.surfaceRaised, 0.97));
    c.restore();
    c.save();
    if (toastAppearing_ && toastTween_.running(frameNow_, preferences_.reduceMotion))
      c.setAlpha(toastTween_.eased(frameNow_, preferences_.reduceMotion));
    c.stroke(Path::roundedRect(toast, 8.0), withAlpha(tone, 0.9), StrokeStyle{1.0});
    c.fill(Path::capsule({toast.x + 12.0, toast.y + 9.0, 4.0, toast.height - 18.0}), tone);
    c.text({toast.x + 24.0, toast.y, std::max(1.0, toast.width - 36.0), toast.height}, title,
           style(FontRole::UiSemibold, t.type.smallLabel), t.color.textPrimary);
    if (const auto open = diagnosticsOpenButton(); open.width > 0.0) {
      c.fill(Path::roundedRect(open, 8.0),
             diagnosticsOpen_ ? withAlpha(t.color.accent, 0.22) : withAlpha(t.color.surfaceSunken, 0.9));
      c.stroke(Path::roundedRect(open, 8.0),
               diagnosticsOpen_ ? t.color.accent : withAlpha(t.color.border, 0.95), StrokeStyle{1.0});
      c.text(open, tr(Str::Diagnostics),
             fitted(c, tr(Str::Diagnostics),
                    style(FontRole::UiSemibold, t.type.smallLabel, 1.0, TextAlign::Center, true),
                    open.width - 10.0),
             diagnosticsOpen_ ? t.color.accent : t.color.textSecondary);
    }
    c.restore();
  } else if (!lastDiagnosticTitle_.empty() &&
             toastTween_.running(frameNow_, preferences_.reduceMotion)) {
    const paint::LayerScope toastLayer{c, paint::Layer::Dynamic, "diagnostics-toast"};
    const auto toast = diagnosticsToastBounds();
    c.save();
    c.setAlpha(1.0 - toastTween_.eased(frameNow_, preferences_.reduceMotion));
    c.fill(Path::roundedRect(toast, 8.0), t.color.surfaceRaised);
    c.stroke(Path::roundedRect(toast, 8.0), lastDiagnosticTone_, StrokeStyle{1.0});
    c.text({toast.x + 24.0, toast.y, std::max(1.0, toast.width - 36.0), toast.height},
           lastDiagnosticTitle_, style(FontRole::UiSemibold, t.type.smallLabel), t.color.textPrimary);
    c.restore();
  }
  const auto meter = ui::Rect{l.status.right() - 360.0, l.status.y + 6.0, 200.0, 16.0};
  const auto fraction = s.state == RenderStatusState::Ready ? 1.0 : std::clamp(s.fraction, 0.0, 1.0);
  std::string label{renderStatusStateName(s.state)};
  if (s.state == RenderStatusState::Rendering && s.totalPhrases > 0U)
    label += " " + std::to_string(s.completedPhrases) + "/" + std::to_string(s.totalPhrases);
  if (s.audibleAudioStale) label = trf(Str::WithAudioStale, {label});
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
  // The export-progress segment: the classic full-width strip becomes this status-bar segment,
  // naming the attempt and the current output.
  if (const auto segment = exportStatusSegment(state); segment.width > 0.0) {
    const auto& progress = state.exportProgress;
    const auto exportFraction = std::clamp(
        static_cast<double>(progress.completedFiles) /
            static_cast<double>(std::max<std::uint64_t>(1U, progress.totalFiles)),
        0.0, 1.0);
    c.fill(Path::roundedRect(segment, 5.0), withAlpha(t.color.surfaceSunken, 0.9));
    c.fill(Path::roundedRect(
               {segment.x, segment.y, segment.width * exportFraction, segment.height}, 5.0),
           withAlpha(t.color.accent, 0.30));
    c.stroke(Path::roundedRect(segment, 5.0), withAlpha(t.color.border, 0.95), StrokeStyle{1.0});
    auto text = trf(Str::ExportStateProgress,
                    {authoring::exportStateName(progress.state),
                     std::to_string(progress.completedFiles), std::to_string(progress.totalFiles)});
    if (progress.state != authoring::ExportState::Committed &&
        progress.state != authoring::ExportState::Recovered && !progress.currentOutput.empty())
      text += " / " + progress.currentOutput;
    c.text({segment.x + 8.0, segment.y, std::max(1.0, segment.width - 16.0), segment.height}, text,
           fitted(c, text, style(FontRole::UiSemibold, t.type.smallLabel),
                  std::max(1.0, segment.width - 16.0)),
           progress.state == authoring::ExportState::Failed ? t.color.error
                                                           : t.color.textPrimary);
  }
  c.fill(Path::circle({l.status.x + 16.0, l.status.y + 14.0}, 3.5),
         online ? t.color.success : t.color.textDisabled);
  const auto left = singStatusMessage(state);
  c.text({l.status.x + 28.0, l.status.y, meter.x - l.status.x - 48.0, l.status.height}, left.text,
         style(FontRole::Ui, t.type.smallLabel),
         left.tone == StatusTone::Warning ? t.color.warning : t.color.textSecondary);
  // The error toast above the bar: the head-in-hand crop, a title naming the failure and the cause
  // behind the line to the left, for a failed render or a missing voicebank and nothing else. The
  // SINGER card keeps the recovery action, which stays reachable in the rack to the right of this
  // rectangle.
  // It stacks above the diagnostics toast when one shows, and is left out where it cannot.
  const auto input = characterSurfaceInput(state, auditionLevel_);
  // Sized to the wrapped reason, measured with the font the toast paints, so a one-line cause
  // leaves the lane and its playhead visible above the card.
  const CharacterToastMeasure toastMeasure{
      characterToastReasonStyle(t),
      [&c](std::string_view text, const TextStyle& textStyle) { return c.measure(text, textStyle); }};
  errorToast_ = characterErrorToast(l, input, singErrorToastCause(state),
                                    state.diagnostics.empty()
                                        ? std::nullopt
                                        : std::optional<ui::Rect>{diagnosticsToastBounds()},
                                    &toastMeasure);
  if (errorToast_.has_value()) {
    const auto toast = *errorToast_;
    // Over the lane like the diagnostics toast, and so above the lane's playhead for the same reason.
    const paint::LayerScope toastLayer{c, paint::Layer::Dynamic, "error-toast"};
    const auto* package = characterDisplay_ == domain::CharacterDisplayMode::Off
        ? nullptr : character_.pose(character::Pose::Error) != nullptr
                        ? character_.pose(character::Pose::Error)
                        : characterPortrait(CharacterState::Error);
    const auto* look = lookPortrait();
    const auto hash = paint::ContentHash{}
                          .add(std::string_view{"toast"}).add(static_cast<const void*>(&t))
                          .add(toast.bounds).add(toast.pose).add(toast.title).add(toast.reason)
                          .add(static_cast<const void*>(package))
                          .add(static_cast<const void*>(look)).value();
    characterArt(c, grown(toast.bounds, 32.0), hash, [&t, toast, package, look](CharacterCanvas art) {
      paintCharacterToast(art, t, toast, package, look);
    });
  }
}

bool SingShell::laneExpanded(const EditorSceneState& state) noexcept {
  // An expanded technical lane gives its height back to the roll and takes the Stage with it, exactly
  // as the classic layout resolves the same presentations.
  for (const auto& presentation : state.technicalLanes)
    if (presentation.mode == domain::TechnicalLaneMode::Expanded) return true;
  return false;
}

void SingShell::notePointer(ui::Point point) {
  const auto moved = !pointerPosition_.has_value() || pointerPosition_->x != point.x ||
                     pointerPosition_->y != point.y;
  pointerPosition_ = point;
  if (!moved) return;
  // Only the Stage reacts to a hovering pointer, so only a pointer that changed sides of the figure
  // asks the host for another frame. A pointer that never reaches the Stage repaints nothing.
  if (!stagePlacement_.has_value() || !stagePlacement_->shown) return;
  const auto& bounds = stagePlacement_->bounds;
  const auto inside = point.x >= bounds.x && point.x < bounds.right() && point.y >= bounds.y &&
                      point.y < bounds.bottom();
  const auto wasInside = stagePointerAt_.has_value() && stagePointerAt_->x >= bounds.x &&
                         stagePointerAt_->x < bounds.right() && stagePointerAt_->y >= bounds.y &&
                         stagePointerAt_->y < bounds.bottom();
  if (inside != wasInside) repaint();
}

std::chrono::steady_clock::time_point SingShell::uiNow() const {
  return uiClock_ ? uiClock_() : std::chrono::steady_clock::now();
}

bool SingShell::pointerGestureActive() const noexcept {
  return knobDrag_.has_value() || bodyGesture_ || overlayGesture_.has_value() ||
         forwarding_ != ForwardArea::None;
}

std::optional<TooltipSubject> SingShell::tooltipSubjectForNode(const SemanticNode& node) const {
  if (!tooltipRole(node.role) || node.bounds.width <= 0.0 || node.bounds.height <= 0.0)
    return std::nullopt;
  std::string text;
  // The whole text of a label the frame elided inside this control comes first: it is what the
  // control shows cut short, and the node carries it at this place (section 8).
  for (auto it = elidedLabels_.rbegin(); it != elidedLabels_.rend(); ++it) {
    const ui::Point middle{it->bounds.x + it->bounds.width * 0.5, it->bounds.y + it->bounds.height * 0.5};
    if (contains(node.bounds, middle)) {
      text = it->text;
      break;
    }
  }
  if (!node.description.empty() && node.description != text) {
    if (!text.empty()) text += '\n';
    text += node.description;
  }
  if (text.empty()) return std::nullopt;
  return TooltipSubject{node.id, node.bounds, std::move(text)};
}

std::optional<TooltipSubject> SingShell::tooltipSubjectFor(std::string_view id) const {
  const auto search = [id](const SemanticNode& node, const auto& self) -> const SemanticNode* {
    for (const auto& child : node.children) {
      if (child.id == id) return &child;
      if (const auto* found = self(child, self); found != nullptr) return found;
    }
    return nullptr;
  };
  const auto* node = search(semantics_.root(), search);
  return node != nullptr ? tooltipSubjectForNode(*node) : std::nullopt;
}

std::optional<TooltipSubject> SingShell::tooltipSubjectAt(ui::Point point) const {
  if (!presented_) return std::nullopt;
  // The smallest control under the point is the one the pointer is on (a button on its card). A
  // field, a note, a lane or the timeline under it means the pointer is editing: no tooltip.
  const SemanticNode* best = nullptr;
  auto blocked = false;
  const auto visit = [&](const SemanticNode& node, const auto& self) -> void {
    for (const auto& child : node.children) {
      if (contains(child.bounds, point)) {
        if (child.role == SemanticRole::TextField || child.role == SemanticRole::Note ||
            child.role == SemanticRole::Lane || child.role == SemanticRole::Timeline)
          blocked = true;
        if (tooltipRole(child.role) &&
            (best == nullptr ||
             child.bounds.width * child.bounds.height < best->bounds.width * best->bounds.height))
          best = &child;
      }
      self(child, self);
    }
  };
  visit(semantics_.root(), visit);
  if (best != nullptr) return tooltipSubjectForNode(*best);
  if (blocked || inMusicalArea(point) || inEditableLane(point)) return std::nullopt;
  // A label the frame elided outside any control: the topmost one under the point, when it is on
  // the surface that is up (an open overlay or menu covers everything drawn before it).
  for (auto it = elidedLabels_.rbegin(); it != elidedLabels_.rend(); ++it) {
    if (!contains(it->bounds, point)) continue;
    if (presentedOverlay_ != OverlayKind::None && it->item != "overlay") continue;
    if (workspaceMenuOpen_ && it->item != "menu") continue;
    return TooltipSubject{"label:" + it->text, it->bounds, it->text};
  }
  return std::nullopt;
}

void SingShell::updateTooltip(std::chrono::steady_clock::time_point now) {
  // Shell focus is a candidate only after keyboard input: a click that focuses a control is not a
  // request to explain it.
  tooltip_.focus(keyboardInput_ && !semanticFocus_.empty() ? tooltipSubjectFor(semanticFocus_)
                                                           : std::nullopt,
                 now);
  tooltip_.hover(pointerPosition_.has_value() && !pointerGestureActive()
                     ? tooltipSubjectAt(*pointerPosition_)
                     : std::nullopt,
                 now);
}

std::optional<std::chrono::steady_clock::time_point> SingShell::nextFrameDue() const noexcept {
  if (!presented_) return std::nullopt;
  auto due = idleFrameDue_;
  const auto at = tooltip_.showsAt();
  // Once a frame has painted at or after that time, the tip is on screen (or had no room).
  if (at.has_value() && !(lastPaintAt_.has_value() && *lastPaintAt_ >= *at))
    due = due.has_value() ? std::min(*due, *at) : *at;
  return due;
}

StatusMessage singStatusMessage(const EditorSceneState& state) {
  const auto& s = state.renderStatus;
  const auto failed = s.state == RenderStatusState::Failed;
  if (!state.diagnostics.empty()) {
    auto text = presentDiagnostic(state.diagnostics.front()).title;
    if (failed && !s.diagnostic.empty() && s.diagnostic != text) text = trf(Str::WithDetail, {text, s.diagnostic});
    return {std::move(text), StatusTone::Warning};
  }
  // A selected seam in a host that cannot audition its B render says so in the status line, the
  // one place visible at every window size; a failed render's reason still comes first.
  if (state.selectedSeam.has_value() && !state.seamPreviewConnected && !failed)
    return {tr(Str::SeamBPreviewIsNotAvailable),
            StatusTone::Normal};
  // A render note is status, not a warning, unless the render itself failed.
  if (!s.diagnostic.empty()) return {s.diagnostic, failed ? StatusTone::Warning : StatusTone::Normal};
  return {state.audioDeviceOnline ? trf(Str::AudioBackend, {state.audioBackend})
                                  : std::string{tr(Str::AudioOffline)},
          StatusTone::Normal};
}

std::string singErrorToastCause(const EditorSceneState& state) {
  const auto& s = state.renderStatus;
  if (s.state == RenderStatusState::Failed && !s.diagnostic.empty()) return s.diagnostic;
  if (!state.diagnostics.empty()) {
    auto presentation = presentDiagnostic(state.diagnostics.front());
    if (!presentation.impact.empty()) return std::move(presentation.impact);
  }
  return singStatusMessage(state).text;
}

// ---- EXPORT workspace --------------------------------------------------------------------------

namespace {

std::string exportStateLabel(authoring::ExportState state) {
  switch (state) {
    case authoring::ExportState::Preflight: return tr(Str::Checking);
    case authoring::ExportState::Staging: return tr(Str::RenderingFiles);
    case authoring::ExportState::Prepared: return tr(Str::Prepared);
    case authoring::ExportState::Publishing: return tr(Str::Publishing);
    case authoring::ExportState::Committed: return tr(Str::Written);
    case authoring::ExportState::Cancelled: return tr(Str::Cancelled);
    case authoring::ExportState::Failed: return tr(Str::Failed);
    case authoring::ExportState::Recovered: return tr(Str::Recovered);
    case authoring::ExportState::RollbackRequired: return tr(Str::NeedsRollback);
  }
  return tr(Str::Unknown);
}

std::string channelLayout(std::uint8_t channels) {
  if (channels == 1U) return tr(Str::Mono);
  if (channels == 2U) return tr(Str::Stereo);
  return trf(Str::ChannelCount, {std::to_string(channels)});
}

std::string sampleRateLabel(std::uint32_t rate) {
  const auto khz = static_cast<double>(rate) / 1000.0;
  return trf(Str::KilohertzValue,
             {std::fmod(khz, 1.0) == 0.0 ? format("%.0f", khz) : format("%.1f", khz)});
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
  // Beside the run button: the final bounce's timing choice, for a host that offers one.
  ui::Rect bounce;
  ui::Rect note;
  ui::Rect status;
};

ui::Rect exportBounceBeside(ui::Rect area, ui::Rect button) {
  const auto left = button.right() + 12.0;
  const auto width = std::min(240.0, area.right() - 32.0 - left);
  return width >= 120.0 ? ui::Rect{left, button.y, width, button.height} : ui::Rect{};
}

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
    p.bounce = exportBounceBeside(area, p.button);
    p.note = {};
    p.status = {left, p.button.bottom() + 8.0, area.width - 64.0,
                std::max(0.0, area.bottom() - p.button.bottom() - 16.0)};
    return p;
  }
  p.button = {left, area.y + 64.0 + 4.0 * 46.0 + 12.0, buttonWidth, 44.0};
  p.bounce = exportBounceBeside(area, p.button);
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
  const auto master = plan->master ? trf(Str::MasterLayoutRateFormat,
                                         {channelLayout(plan->channels),
                                          sampleRateLabel(plan->sampleRate), plan->format})
                                   : std::string{tr(Str::NoMaster)};
  return trf(Str::ExportPlanSummary,
             {master, plan->stems ? tr(Str::OneStemPerTrack) : tr(Str::NoStems)});
}

bool attemptEnded(authoring::ExportState state) noexcept {
  return state == authoring::ExportState::Failed || state == authoring::ExportState::Cancelled ||
         state == authoring::ExportState::RollbackRequired;
}

}  // namespace

ui::Rect SingShell::overlaySlot(const NativeEditorController& controller,
                                const EditorSceneState& state) const noexcept {
  static_cast<void>(controller);
  static_cast<void>(state);
  return layout_.overlay;
}

ui::Rect SingShell::diagnosticsToastBounds() const noexcept {
  const auto& l = layout_;
  const auto width = std::min(std::max(240.0, l.status.width * 0.42), 380.0);
  return {l.status.x, l.status.y - 34.0, width, 28.0};
}

ui::Rect SingShell::diagnosticsOpenButton() const noexcept {
  const auto& l = layout_;
  const auto toast = diagnosticsToastBounds();
  const auto width = std::min(std::max(0.0, l.status.right() - toast.right() - 8.0), 108.0);
  if (width < 72.0) return {};
  return {l.status.right() - width, l.status.y - 34.0, width, 28.0};
}

ui::Rect SingShell::exportStatusSegment(const EditorSceneState& state) const noexcept {
  if (state.exportProgress.totalFiles == 0U) return {};
  const auto& l = layout_;
  // A segment at the status bar's right end, left of the render meter, so the render readout keeps
  // its own space.
  const auto meter = ui::Rect{l.status.right() - 360.0, l.status.y + 6.0, 200.0, 16.0};
  const auto width = std::min(std::max(0.0, meter.x - l.status.x - 200.0), 260.0);
  if (width < 140.0) return {};
  return {meter.x - width - 8.0, l.status.y, width, l.status.height};
}

std::vector<std::pair<std::size_t, ui::Rect>> SingShell::overlapBadges(
    const NativeEditorController& controller) const {
  std::vector<std::pair<std::size_t, ui::Rect>> out;
  if (!presented_ || workspace_ != Workspace::Sing || layout_.inspectorOpen) return out;
  for (const auto& badge : layoutSingOverlapBadges(controller.pianoRoll().visibleNotes(), layout_.grid))
    out.emplace_back(badge.group, badge.bounds);
  return out;
}

core::Result<void> SingShell::closeOverlay(NativeEditorController& controller,
                                           const ShellOverlay& overlay) {
  const auto result = overlay.close(controller);
  if (overlay.kind() == OverlayKind::Diagnostics) diagnosticsOpen_ = false;
  if (overlay.kind() == OverlayKind::SingerMenu) singerMenuOpen_ = false;
  if (overlay.kind() == OverlayKind::About) aboutOpen_ = false;
  if (overlay.kind() == OverlayKind::Settings) settingsOpen_ = false;
  return result;
}

core::Result<void> SingShell::performOverlay(NativeEditorController& controller,
                                             const ShellOverlay& overlay, std::string_view id,
                                             SemanticAction action) {
  auto result = overlay.perform(controller, id, action);
  if (overlay.kind() == OverlayKind::SingerMenu && result && singerMenuOpen_) {
    // The command ran: its own surface (a review, the voice browser, a field) is up now, or the
    // host ran it. Focus returns to the button; a surface that opened takes it on the next rebuild,
    // and Escape there still returns to the button.
    singerMenuOpen_ = false;
    takeSemanticFocus(controller, std::string{kSingerMenuButtonId});
  }
  // The About sheet's one control is Close.
  if (overlay.kind() == OverlayKind::About && result && aboutOpen_) {
    aboutOpen_ = false;
    returnFocusToOverlayOpener(controller);
  }
  if (overlay.kind() == OverlayKind::Settings && result && !settingsOpen_ && !aboutOpen_)
    returnFocusToOverlayOpener(controller);
  return result;
}

void SingShell::returnFocusToOverlayOpener(NativeEditorController& controller) {
  const auto opener = std::exchange(overlayOpener_, std::string{});
  refreshSemantics(controller);
  if (!opener.empty() && semantics_.publishes(opener))
    takeSemanticFocus(controller, opener);
  else
    semanticFocus_.clear();
}

void SingShell::cancelCoveredLyric(NativeEditorController& controller) {
  if (!presented_) return;
  // The lyric composition is the only one without a field kind; every other field is the surface's
  // own (an inline field card, the time map's event field) and stays open.
  const auto lyricOpen =
      controller.textInputActive() &&
      controller.textFieldView().kind == NativeEditorController::TextFieldView::Kind::None;
  // Nothing to cancel: skip the overlay lookup, whose scene state costs a pronunciation pass over
  // the whole region, since prepareFrame runs this every frame.
  if (!lyricOpen && !lyricInputActive_) return;
  if (activeOverlay(controller) == nullptr) return;
  if (lyricOpen) controller.cancelTextComposition();
  lyricInputActive_ = false;
}

bool SingShell::overlayPublishes(const NativeEditorController& controller,
                                 std::string_view id) const {
  const auto* overlay = activeOverlay(controller);
  if (overlay == nullptr) return false;
  if (id == std::string{overlay->idPrefix()} + "panel") return true;
  const auto state = controller.sceneState();
  const auto card =
      overlay->panel(controller, state, layout_, overlaySlot(controller, state));
  for (const auto& control : overlay->controls(controller, state, layout_, card))
    if (control.id == id) return true;
  return false;
}

void SingShell::paintOverlay(Canvas2D& c, const DesignTokens& t,
                             const NativeEditorController& controller,
                             const EditorSceneState& state, const ShellOverlay& overlay) const {
  const auto slot = overlaySlot(controller, state);
  const auto panel = paintShellOverlay(overlay, c, t, controller, state, layout_, slot);
  // What a press that comes next has to find still there: both its content and the actual hit-map.
  overlayDrawn_.reset();
  if (const auto drawn = drawnOverlayFingerprint(overlay, controller, state, layout_, panel);
      drawn.has_value())
    overlayDrawn_ = DrawnOverlay{overlay.kind(), *drawn};
  // The menu shows which item holds the keyboard, since arrows and Tab walk it.
  if (overlay.kind() != OverlayKind::SingerMenu || panel.width <= 0.0 || semanticFocus_.empty())
    return;
  for (const auto& control : overlay.controls(controller, state, layout_, panel)) {
    if (control.id != semanticFocus_) continue;
    const auto& r = control.bounds;
    c.save();
    c.setGlow(t.color.focusRing, 6.0);
    c.stroke(Path::roundedRect({r.x - 2.0, r.y - 2.0, r.width + 4.0, r.height + 4.0},
                               t.shape.control + 2.0),
             t.color.focusRing, StrokeStyle{2.0});
    c.restore();
    break;
  }
}


std::vector<SemanticNode> SingShell::overlaySemantics(const NativeEditorController& controller,
                                                      const EditorSceneState& state) const {
  std::vector<SemanticNode> out;
  const auto* overlay = activeOverlay(controller, state);
  if (overlay == nullptr) return out;
  const auto slot = overlaySlot(controller, state);
  const auto panel = overlay->panel(controller, state, layout_, slot);
  if (panel.width <= 0.0) return out;
  const auto& legacy = controller.accessibilityTree();
  // The card itself is a panel node, so assistive software reads the surface, not a bare control.
  out.push_back(SemanticNode{.id = std::string{overlay->idPrefix()} + "panel",
                             .role = SemanticRole::Panel,
                             .name = overlay->title(controller, state),
                             .bounds = panel,
                             .actions = {SemanticAction::SetFocus},
                             .description = tr(Str::EscapeClosesThisSurface)});
  for (const auto& control : overlay->controls(controller, state, layout_, panel)) {
    // The overlay states the role and name it publishes; where the controller also publishes the
    // id, its value, description and additional actions merge in, so the surface keeps the
    // controller's meaning and its real command. Bounds are always the shell's own.
    SemanticNode node{.id = control.id,
                      .role = control.role,
                      .name = control.name,
                      .bounds = control.bounds,
                      .enabled = control.enabled,
                      .selected = control.selected,
                      .actions = control.activatable
                                     ? std::vector<SemanticAction>{SemanticAction::Activate,
                                                                   SemanticAction::SetFocus}
                                     : std::vector<SemanticAction>{SemanticAction::SetFocus}};
    if (const auto* published = overlayControllerNode(legacy, control.id); published != nullptr) {
      if (!published->value.empty()) node.value = published->value;
      if (!published->description.empty()) node.description = published->description;
      if (!published->enabled) {
        node.enabled = false;
        node.actions = {SemanticAction::SetFocus};
      }
    }
    // What the overlay states itself wins: the full text a painted label elides, a field's text as
    // typed, a device's kind. A field's text as typed wins even when empty: a cleared field never
    // reads the committed value under it. A field takes text; a stepped setting steps either way.
    if (control.editable || !control.value.empty()) node.value = control.value;
    if (!control.description.empty()) node.description = control.description;
    if (control.editable && node.enabled) {
      node.actions = {SemanticAction::SetFocus, SemanticAction::EditText};
      node.editableValue = control.value;
    }
    if (control.adjustable && node.enabled) {
      node.actions.push_back(SemanticAction::Increment);
      node.actions.push_back(SemanticAction::Decrement);
    }
    out.push_back(std::move(node));
  }
  return out;
}

ui::Rect SingShell::exportArea() const noexcept {
  const auto& l = layout_;
  return ui::Rect{l.editor.x, l.editor.y, l.editor.width, l.lane.bottom() - l.editor.y};
}

ui::Rect SingShell::exportRunButton() const noexcept {
  if (workspace_ != Workspace::Export || !presented_) return {};
  return exportPanelLayout(exportArea()).button;
}

ui::Rect SingShell::exportBounceButton() const noexcept {
  if (workspace_ != Workspace::Export || !presented_) return {};
  return exportPanelLayout(exportArea()).bounce;
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
  workspaceMenuOpen_ = false;
  if (workspace == workspace_) return;
  tabPrevious_ = !preferences_.reduceMotion && layers_.contentSnapshot() != nullptr
                     ? paint::imageFromPixels(*layers_.contentSnapshot()) : nullptr;
  tabTween_.start(uiNow(), std::chrono::milliseconds{150},
                  preferences_.reduceMotion || tabPrevious_ == nullptr);
  modePrevious_.reset();
  modeTween_.reset();
  // The singer menu belongs to the workspace it was opened over, like any sheet.
  singerMenuOpen_ = false;
  settingsOpen_ = false;
  // A sheet or field opened over one workspace does not follow the creator to the next one.
  dismissOverlay(controller);
  // The grid the gestures and any text field belong to leaves the screen with its workspace.
  cancelGestures(controller);
  if (lyricInputActive_ || controller.textInputActive()) controller.cancelTextComposition();
  lyricInputActive_ = false;
  fieldAnchor_.reset();
  semanticFocus_.clear();
  workspace_ = workspace;
  repaint();
}

void SingShell::dismissOverlay(NativeEditorController& controller) {
  overlayGesture_.reset();
  overlayOpener_.clear();
  const auto* overlay = activeOverlay(controller);
  // An open field is cancelled, never committed, by leaving its workspace.
  if (controller.textInputActive()) controller.cancelTextComposition();
  fieldAnchor_.reset();
  if (overlay == nullptr) return;
  auto kind = overlay->kind();
  // A review's draft field returns to its review when cancelled; that review goes with it.
  if (kind == OverlayKind::TextField) kind = OverlayKind::ReplacementReview;
  // The presented surface closes through its own command. A surface with an inner page (a review's
  // detail) steps back first, so its close runs until the surface is gone.
  for (std::size_t step = 0U; step < 3U; ++step) {
    const auto* current = activeOverlay(controller);
    if (current == nullptr || current->kind() != kind) break;
    static_cast<void>(closeOverlay(controller, *current));
  }
}

ShellWorkspace* SingShell::bodyWorkspace() const noexcept {
  if (workspace_ == Workspace::Tune) return tune_.get();
  if (workspace_ == Workspace::Mix) return mix_.get();
  if (workspace_ == Workspace::Voice) return voice_.get();
  return nullptr;
}

std::optional<core::Result<void>> SingShell::routeUndo(bool redo) {
  if (!presented_) return std::nullopt;
  // A drag in a workspace body (a VOICE knob or formant, a TUNE or MIX handle) is an edit still in
  // progress, and its release commits against the history it started from. Undo underneath it, the
  // designer's or the song's, is refused, and refused here: returning nothing would let the menu's
  // Undo fall through to the hidden song mid-drag.
  if (const auto* body = bodyWorkspace(); bodyGesture_ || (body != nullptr && body->gestureActive()))
    return core::failure(core::ErrorCode::Conflict, tr(Str::FinishTheDragBeforeUndoOr));
  if (workspace_ != Workspace::Voice) return std::nullopt;
  // The designer owns the command only when it can actually step through its own history;
  // otherwise the application's Undo and Redo keep working.
  if (voice_ == nullptr || !voice_->ownsUndo(redo)) return std::nullopt;
  auto result = voice_->undo(redo);
  repaint();
  return result;
}

bool SingShell::tabSelected(std::size_t tab) const noexcept {
  switch (tab) {
    case 0U: return workspace_ == Workspace::Sing;
    case 1U: return workspace_ == Workspace::Voice;
    case 2U: return workspace_ == Workspace::Tune;
    case 3U: return workspace_ == Workspace::Mix;
    case 4U: return workspace_ == Workspace::Export;
    default: return false;
  }
}

void SingShell::openTab(NativeEditorController& controller, std::size_t tab) {
  switch (tab) {
    case 0U: setWorkspace(controller, Workspace::Sing); break;
    case 1U: setWorkspace(controller, Workspace::Voice); break;
    case 2U: setWorkspace(controller, Workspace::Tune); break;
    case 3U: setWorkspace(controller, Workspace::Mix); break;
    case 4U: setWorkspace(controller, Workspace::Export); break;
    default: break;
  }
  repaint();
}

core::Result<void> SingShell::runExportSet(NativeEditorController& controller) {
  if (!hostActions_.exportSet)
    return core::failure(core::ErrorCode::Unsupported, hostActions_.exportUnavailable);
  if (exportBusy(controller))
    return core::failure(core::ErrorCode::Conflict, tr(Str::AnExportIsAlreadyRunning));
  auto result = hostActions_.exportSet();
  repaint();
  return result;
}

core::Result<void> SingShell::dispatchController(NativeEditorController& controller,
                                                 std::string_view id, SemanticAction action) {
  // A retained host element keeps its action flags after the shell stopped publishing it (the
  // score under EXPORT, a control the layout removed); only what is on screen now may act.
  refreshSemantics(controller);
  if (!semantics_.publishes(id))
    return core::failure(core::ErrorCode::Conflict, tr(Str::ThisElementIsNotOnScreen));
  if (bodyGesture_ && action != SemanticAction::SetFocus)
    return core::failure(core::ErrorCode::InvalidState, tr(Str::ADragIsInProgress));
  // A control of the presented overlay belongs to the overlay even when it is not a shell id (the
  // time map's rows and actions, the overlap rows): the controller does not publish those ids, so
  // its own dispatch would refuse them. The overlay runs the same command its pointer and keys run.
  if (const auto* overlay = activeOverlay(controller);
      overlay != nullptr && overlayPublishes(controller, id)) {
    const auto panelId = std::string{overlay->idPrefix()} + "panel";
    auto result = semantics_.dispatch(
        id, action,
        [this, &controller, overlay, &panelId](std::string_view target,
                                               SemanticAction requested) -> core::Result<void> {
          if (requested == SemanticAction::SetFocus) {
            takeSemanticFocus(controller, std::string{target});
            return core::success();
          }
          if (target == panelId) return core::success();
          return performOverlay(controller, *overlay, target, requested);
        });
    repaint();
    return result;
  }
  auto result = semantics_.dispatch(
      id, action, [&controller](std::string_view target, SemanticAction requested) {
        return controller.dispatchAccessibility(target, requested);
      });
  if (result && action == SemanticAction::SetFocus) controllerFocusTaken();
  repaint();
  return result;
}

core::Result<void> SingShell::setControllerValue(NativeEditorController& controller,
                                                 std::string_view id, std::string_view value) {
  refreshSemantics(controller);
  if (!semantics_.publishes(id))
    return core::failure(core::ErrorCode::Conflict, tr(Str::ThisElementIsNotOnScreen));
  if (bodyGesture_) return core::failure(core::ErrorCode::InvalidState, tr(Str::ADragIsInProgress));
  // A control of the presented overlay takes a value only where the overlay says so (a field's
  // text), through the controller's own value path for that field; the covered score's controls
  // are not published while the card is up.
  if (const auto* overlay = activeOverlay(controller);
      overlay != nullptr && overlayPublishes(controller, id)) {
    auto result = overlay->setValue(controller, id, value);
    refreshSemantics(controller);
    repaint();
    return result;
  }
  return controller.setAccessibilityValue(id, value);
}

void SingShell::paintExport(Canvas2D& c, const DesignTokens& t, const EditorSceneState& state) const {
  const auto area = exportArea();
  const auto p = exportPanelLayout(area);
  glassPanel(c, t, area, t.shape.card, 0.97);
  const auto& last = state.lastExport;
  cardHeader(c, t, area, tr(Str::ExportSet), exportRunning_ || last.has_value());
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
    row(left, y, p.columnWidth, tr(Str::MasterMix),
        plan->master ? trf(Str::LayoutRateFormat, {channelLayout(plan->channels),
                                                     sampleRateLabel(plan->sampleRate), plan->format})
                     : std::string{tr(Str::NotWritten)},
        t.color.textPrimary);
    row(left, y + 46.0, p.columnWidth, tr(Str::Stems),
        plan->stems ? std::string{tr(Str::OneFilePerTrackSameFormat)} : std::string{tr(Str::NotWritten)},
        t.color.textPrimary);
    row(left, y + 92.0, p.columnWidth, tr(Str::ProjectAndRecipes),
        plan->asksAboutPackaging ? std::string{tr(Str::YouChooseWhenTheExportStarts)}
                                 : std::string{tr(Str::NothingToPackageForThisProject)},
        t.color.textPrimary);
    row(left, y + 138.0, p.columnWidth, tr(Str::Receipt), tr(Str::SHA256OfEveryFileWritten),
        t.color.textPrimary);
  } else {
    row(left, area.y + 64.0, p.columnWidth, tr(Str::Export), hostActions_.exportUnavailable, t.color.warning);
  }

  // The run button: the host's real command, refused while an export runs or when it cannot export.
  const auto button = p.button;
  const auto available = static_cast<bool>(hostActions_.exportSet) && !exportRunning_;
  c.save();
  if (available) c.setGlow(withAlpha(t.color.accent, 0.7), 10.0);
  c.fill(Path::capsule(button), available ? t.color.accent : withAlpha(t.color.surfaceSunken, 0.9));
  c.restore();
  c.stroke(Path::capsule(button), available ? t.color.accentDeep : t.color.border, StrokeStyle{1.0});
  c.text(button, exportRunning_ ? tr(Str::Exporting) : tr(Str::ExportSet2),
         style(FontRole::UiBold, t.type.label, 1.4, TextAlign::Center, true),
         available ? t.color.textOnAccent : t.color.textDisabled);
  // A plug-in's final bounce is rendered by the DAW; the choice here is which timing it follows.
  if (state.bounceTimingAvailable && p.bounce.width > 0.0) {
    const auto label = state.bounceFollowHost ? std::string{tr(Str::BounceFollowHost)}
                                              : std::string{tr(Str::BounceFixedAudio)};
    c.fill(Path::capsule(p.bounce), withAlpha(t.color.surfaceSunken, 0.9));
    c.stroke(Path::capsule(p.bounce), state.bounceFollowHost ? t.color.accentTime : t.color.border,
             StrokeStyle{1.0});
    c.text(p.bounce, label,
           fitted(c, label, style(FontRole::UiSemibold, t.type.label, 0.6, TextAlign::Center),
                  p.bounce.width - 16.0),
           t.color.textPrimary);
  }
  if (p.note.width > 0.0)
    c.text(p.note,
           // The note under the button says what to do when the export can happen. When it cannot,
           // the row above the button has already said why, in the same words: the frame showed the
           // sentence twice, 200 points apart, which reads as the window repeating itself rather
           // than as two pieces of information. So the note is left empty when there is nothing to
           // choose, and the reason stays where it was first said.
           hostActions_.exportSet ? std::string{tr(Str::ChooseANewFolderAnExisting)} : std::string{},
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
    lines.push_back({tr(Str::Progress),
                     trf(Str::ExportStateFilesDone, {exportStateLabel(progress.state),
                                                     std::to_string(progress.completedFiles),
                                                     std::to_string(progress.totalFiles)}) +
                         (progress.currentOutput.empty() ? "" : "  " + progress.currentOutput),
                     t.color.textPrimary});
  } else if (attemptEnded(progress.state)) {
    lines.push_back({tr(Str::LastAttempt),
                     exportStateLabel(progress.state) +
                         (progress.currentOutput.empty() ? "" : ": " + progress.currentOutput),
                     progress.state == authoring::ExportState::Cancelled ? t.color.warning : t.color.error});
  } else {
    lines.push_back({tr(Str::Progress), tr(Str::Idle), t.color.textSecondary});
  }
  if (last) {
    const auto folder = last->setPath.empty() ? last->masterPath.parent_path() : last->setPath;
    lines.push_back({tr(Str::LastExport),
                     trf(Str::ExportStateFilesFolder, {exportStateLabel(last->state),
                                                       std::to_string(last->files.size()),
                                                       folder.filename().string()}),
                     last->state == authoring::ExportState::Committed ? t.color.success : t.color.warning});
    if (!p.compact)
      lines.push_back({tr(Str::Master2),
                       last->masterSha256.size() >= 12U
                           ? trf(Str::FileWithSha256, {last->masterPath.filename().string(),
                                                       last->masterSha256.substr(0U, 12U)})
                           : last->masterPath.filename().string(),
                       t.color.textPrimary});
    if (!p.compact && last->state == authoring::ExportState::Committed &&
        characterDisplay_ != domain::CharacterDisplayMode::Off) {
      if (const auto* complete = character_.poseImage(character::Pose::Complete); complete != nullptr) {
        const auto side = std::min(180.0, std::max(0.0, area.width - p.columnWidth - 96.0));
        if (side > 0.0)
          c.drawImage(*complete, {area.right() - side - 32.0, area.y + 62.0, side, side}, 0.88);
      }
    }
  } else {
    lines.push_back({tr(Str::LastExport), tr(Str::NothingExportedInThisSession), t.color.textSecondary});
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
  // The shell is the only editor surface: with nothing presented there is nothing under the
  // pointer, so no press reaches the controller at coordinates nobody drew.
  if (!presented_) return core::success();
  notePointer(event.position);
  // A press hides the tooltip; it stays hidden until the pointer moves to another target.
  keyboardInput_ = false;
  updateTooltip(uiNow());
  tooltip_.dismiss();
  if (shownTooltip_.has_value()) repaint();
  const auto result = shellPointerDown(controller, event);
  return result;
}

core::Result<void> SingShell::shellPointerDown(NativeEditorController& controller,
                                               const PointerEvent& event) {
  // A press while an earlier gesture is still open means its release never arrived (a host that
  // dropped a pointer-up). That gesture is abandoned, so this press can never drive or commit it.
  if (knobDrag_ || bodyGesture_) cancelGestures(controller);
  const auto p = event.position;
  const auto& l = layout_;
  // A re-homed overlay is modal over the score and the rack. A press on one of its controls runs
  // that control's own command; a press on the card itself is absorbed; a press outside closes it,
  // as the classic surfaces closed on Escape or their close button alone.
  cancelCoveredLyric(controller);
  const auto* overlay = activeOverlay(controller);
  // A list-bearing sheet may disappear, or another sheet may replace it, after its last pixels were
  // painted but before the next frame. The press still belongs to the modal surface the creator saw;
  // do not route its coordinates into the newly exposed score or a different sheet.
  if (overlayDrawn_.has_value() &&
      (overlay == nullptr || overlayDrawn_->kind != overlay->kind())) {
    repaint();
    return core::failure(core::ErrorCode::Conflict, tr(Str::ThisElementIsNotOnScreen));
  }
  if (overlay != nullptr) {
    const auto state = controller.sceneState();
    const auto slot = overlaySlot(controller, state);
    const auto panel = overlay->panel(controller, state, layout_, slot);
    // The press was aimed at the frame on screen. An overlay whose controls name content that can
    // change under the pointer (the diagnostics popover's issues) gives a number for what it draws,
    // and the press is handled only if the last painted frame drew this overlay with the number it
    // has now. Otherwise the control under the pointer is not the one the creator saw there: the
    // content changed since that frame (an eviction, a dismissal), or no painted frame has shown this
    // overlay at all (it was opened, or closed and opened again, since the last paint), and the press
    // would act on whatever stands under it. It does nothing, and the next frame shows what is there.
    if (const auto now = drawnOverlayFingerprint(*overlay, controller, state, layout_, panel);
        now.has_value()) {
      const auto seen = overlayDrawn_.has_value() && overlayDrawn_->kind == overlay->kind() &&
                        overlayDrawn_->content == *now;
      if (!seen) {
        repaint();
        return core::failure(core::ErrorCode::Conflict, tr(Str::ThisElementIsNotOnScreen));
      }
    }
    if (event.button == PointerButton::Left) {
      // A plot inside the card (the dynamics inspector's curve) takes the press itself, in the
      // controller's own geometry, and may keep the pointer for a drag.
      overlayGesture_.reset();
      if (auto pressed = overlay->press(controller, state, layout_, panel, event); pressed.handled) {
        overlayGesture_ = pressed.gesture;
        takeSemanticFocus(controller, std::string{overlay->idPrefix()} + "panel");
        repaint();
        return pressed.result;
      }
      for (const auto& control : overlay->controls(controller, state, layout_, panel)) {
        if (!contains(control.bounds, p)) continue;
        // A disabled control (a time-map row while the event field is open, a pager at its end)
        // absorbs the press and runs nothing, as the classic panel ignored it.
        if (!control.enabled) {
          repaint();
          return core::success();
        }
        // A field keeps the keyboard where it is (its input client); any other control takes it.
        if (control.role != SemanticRole::TextField)
          takeSemanticFocus(controller, std::string{overlay->idPrefix()} + "panel");
        if (!control.activatable && control.role != SemanticRole::TextField) {
          repaint();
          return core::success();
        }
        const auto result = performOverlay(controller, *overlay, control.id, SemanticAction::Activate);
        repaint();
        return result;
      }
    }
    // The panel body absorbs the press; the dimmed field outside it closes the overlay.
    if (!contains(panel, p)) {
      const auto closed = closeOverlay(controller, *overlay);
      repaint();
      return closed;
    }
    return core::success();
  }
  if (workspaceMenuOpen_) {
    if (event.button == PointerButton::Left) {
      for (std::size_t i = 0U; i < l.workspaceMenuRow.size(); ++i) {
        if (!contains(l.workspaceMenuRow[i], p)) continue;
        setWorkspaceMenuOpen(controller, false);
        openTab(controller, i);
        return core::success();
      }
      for (std::size_t i = 0U; i < l.modeMenuRow.size(); ++i) {
        if (!contains(l.modeMenuRow[i], p)) continue;
        setWorkspaceMenuOpen(controller, false);
        setMode(i == 0U ? DesignMode::Emo : DesignMode::Scene);
        return core::success();
      }
    }
    setWorkspaceMenuOpen(controller, false);
    return core::success();
  }
  if (event.button == PointerButton::Left && l.workspaceMenuButton.width >= 24.0 &&
      contains(l.workspaceMenuButton, p)) {
    setWorkspaceMenuOpen(controller, true);
    return core::success();
  }
  // A knob press starts one gesture; the knob takes keyboard focus so arrows continue it.
  const auto pressKnob = [&](ui::Point point) -> std::optional<core::Result<void>> {
    for (std::size_t i = 0U; i < l.knob.size(); ++i) {
      if (!contains(l.knob[i], point)) continue;
      if (knobRefused_[i]) return core::success();
      knobDrag_ = KnobDrag{.index = i,
                           .startY = point.y,
                           .steps = 0,
                           .moved = false,
                           .revision = controller.documentRevision(),
                           .region = controller.selectedRegion(),
                           .playhead = controller.playheadTick()};
      static constexpr std::array<const char*, 6U> kKnobIds{
          "shell.knob.formant", "shell.knob.breath", "shell.knob.tension",
          "shell.knob.air",     "shell.knob.gender", "shell.knob.growl"};
      takeSemanticFocus(controller, kKnobIds[i]);
      return core::success();
    }
    return std::nullopt;
  };
  // The open inspector lies over the score (and over the header on short windows): it takes every
  // press inside it, and a press anywhere else only closes it.
  if (l.inspectorOpen) {
    if (!contains(l.inspector, p)) {
      setInspectorOpen(controller, false);
      return core::success();
    }
    if (event.button != PointerButton::Left) return core::success();
    if (contains(l.singerChange, p)) {
      controller.showVoicebankBrowser();
      repaint();
      return core::success();
    }
    if (l.singerMenu.width > 0.0 && contains(l.singerMenu, p))
      return setSingerMenuOpen(controller, true);
    if (auto knob = pressKnob(p)) return std::move(*knob);
    return core::success();
  }
  if (event.button == PointerButton::Left) {
    // The +N badge of an overlapping note opens its detail popover, anchored to that badge. The
    // hit rectangle is the painted badge, so the popover opens where the creator clicked.
    for (const auto& [group, badge] : overlapBadges(controller)) {
      if (!contains(badge, p)) continue;
      return controller.openOverlapDetail(group);
    }
    // Workspace tabs: SING, VOICE, TUNE, MIX and EXPORT switch the workspace.
    for (std::size_t i = 0U; i < l.workspaceTab.size(); ++i) {
      if (l.workspaceTab[i].width < 24.0 || !contains(l.workspaceTab[i], p)) continue;
      openTab(controller, i);
      return core::success();
    }
    if (workspace_ == Workspace::Export && contains(exportRunButton(), p))
      return runExportSet(controller);
    if (workspace_ == Workspace::Export && contains(exportBounceButton(), p) &&
        controller.sceneState().bounceTimingAvailable)
      return controller.toggleBounceTiming();
  }
  // TUNE and MIX own every press in their body; a gesture started there stays theirs.
  if (auto* body = bodyWorkspace(); body != nullptr && contains(workspaceArea(), p)) {
    semanticFocus_.clear();
    auto result = body->pointerDown(controller, event, workspaceArea());
    bodyGesture_ = body->gestureActive();
    if (auto focus = body->takeFocusRequest(); !focus.empty() && focus.starts_with(body->idPrefix()))
      takeSemanticFocus(controller, std::move(focus));
    if (body->takeWorkspaceRequest() == "sing") setWorkspace(controller, Workspace::Sing);
    repaint();
    return result;
  }
  // The export panel covers the grid and lane; nothing under it is reachable.
  if (workspace_ == Workspace::Export && contains(exportArea(), p)) return core::success();
  if (event.button == PointerButton::Left) {
    if (contains(l.modeSwitch, p)) {
      setMode(p.x < l.modeSwitch.x + l.modeSwitch.width * 0.5 ? DesignMode::Emo : DesignMode::Scene);
      return core::success();
    }
    if (contains(l.playButton, p)) return controller.keyDown(KeyEvent{.key = NativeKey::Space});
    if (l.outputMeterVisible && contains(outputClipLight(l.outputMeter).hit, p)) {
      controller.resetOutputClip();
      return core::success();
    }
    if (contains(l.tempoReadout, p)) return controller.beginTempoEdit();
    if (contains(l.meterReadout, p)) return controller.beginMeterEdit();
    if (contains(l.settings, p)) {
      takeSemanticFocus(controller, "shell.settings");
      setSettingsOpen(true);
      repaint();
      return core::success();
    }
    if (contains(l.language, p)) {
      cycleLanguage(1);
      return core::success();
    }
    if (const auto open = diagnosticsOpenButton();
        open.width > 0.0 && contains(open, p) && !controller.diagnosticPanel().entries().empty()) {
      setDiagnosticsOpen(!diagnosticsOpen_);
      return core::success();
    }
    if (contains(l.singerChange, p)) {
      controller.showVoicebankBrowser();
      repaint();
      return core::success();
    }
    if (knobsShown() && l.singerMenu.width > 0.0 && contains(l.singerMenu, p))
      return setSingerMenuOpen(controller, true);
    if (l.inspectorButton.width > 0.0 && contains(l.inspectorButton, p)) {
      setInspectorOpen(controller, true);
      return core::success();
    }
    if (l.rack == RackPresentation::Full)
      if (auto knob = pressKnob(p)) return std::move(*knob);
    // The ruler's time-map opener and the lane's review opener are painted controls inside strips
    // the score owns: a press on them opens their surface with the command their nodes run, and
    // never reaches the ruler (a seek) or the lane.
    if (workspace_ == Workspace::Sing && l.rulerTimeMapButton.width > 0.0 &&
        contains(l.rulerTimeMapButton, p)) {
      takeSemanticFocus(controller, "shell.ruler.time-map");
      auto opened = controller.openTimeMapPanel();
      repaint();
      return opened;
    }
    if (workspace_ == Workspace::Sing && l.laneReviewButton.width > 0.0 &&
        contains(l.laneReviewButton, p)) {
      takeSemanticFocus(controller, "shell.lane.review");
      auto opened = controller.openPhonemeReview();
      repaint();
      return opened;
    }
    const auto tabWidth = singLaneTabWidth(l);
    for (std::size_t i = 0U; i < 8U; ++i) {
      const ui::Rect tab{l.laneTabs.x + static_cast<double>(i) * (tabWidth + 4.0), l.laneTabs.y,
                         tabWidth, l.laneTabs.height};
      if (!contains(tab, p)) continue;
      if (i == 0U) return controller.openDynamicsInspector();
      if (i == 7U) return showTechnicalLanes(controller);
      technicalLane_ = false;
      applyGeometry(controller);
      return controller.openExpressionLane(ui::expressionChannelAt(i - 1U));
    }
    // A band label, or a collapsed band's strip, collapses or expands that band.
    if (workspace_ == Workspace::Sing && technicalLane_) {
      const auto bands = technicalBands();
      for (std::size_t i = 0U; i < bands.band.size(); ++i) {
        if (contains(bands.toggle[i], p) || (bands.collapsed[i] && contains(bands.band[i], p)))
          return toggleTechnicalBand(controller, i);
      }
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
  if (!presented_) return core::success();
  notePointer(event.position);
  {
    // The tooltip follows the pointer: a new target starts its delay, leaving one hides it.
    const auto now = uiNow();
    updateTooltip(now);
    const auto* tip = tooltip_.shown(now);
    if ((tip != nullptr) != shownTooltip_.has_value() ||
        (tip != nullptr && tip->id != shownTooltip_->subject))
      repaint();
  }
  if (overlayGesture_) {
    const auto* overlay = activeOverlay(controller);
    if (overlay == nullptr) {
      overlayGesture_.reset();
      return core::success();
    }
    auto result = overlay->drag(controller, *overlayGesture_, event, false);
    repaint();
    return result;
  }
  if (bodyGesture_) {
    auto* body = bodyWorkspace();
    if (body == nullptr) return core::success();
    auto result = body->pointerMove(controller, event, workspaceArea());
    repaint();
    return result;
  }
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
  if (!presented_) return core::success();
  if (overlayGesture_) {
    const auto gesture = *overlayGesture_;
    overlayGesture_.reset();
    const auto* overlay = activeOverlay(controller);
    if (overlay == nullptr) {
      controller.cancelPointerGesture();
      return core::success();
    }
    auto result = overlay->drag(controller, gesture, event, true);
    repaint();
    return result;
  }
  if (bodyGesture_) {
    bodyGesture_ = false;
    auto* body = bodyWorkspace();
    if (body == nullptr) return core::success();
    auto result = body->pointerUp(controller, event, workspaceArea());
    repaint();
    return result;
  }
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
    return opened;
  }
  if (forwarding_ != ForwardArea::None) {
    const auto area = forwarding_;
    forwarding_ = ForwardArea::None;
    const auto result = controller.pointerUp(translated(event, area));
    return result;
  }
  return core::success();
}

bool SingShell::scroll(NativeEditorController& controller, double deltaX, double deltaY,
                      ui::Point anchor, InputModifiers modifiers) {
  // A scroll hides the tooltip.
  tooltip_.dismiss();
  if (shownTooltip_.has_value()) repaint();
  if (workspaceMenuOpen_) return true;
  if (!presented_) return true;  // nothing is on screen to scroll
  // An overlay is modal: a scroll over its card is its own (a long list pages, a plot pans), and
  // nothing under the dimmed field scrolls.
  if (const auto* overlay = activeOverlay(controller); overlay != nullptr) {
    if (overlayGesture_) return true;
    const auto state = controller.sceneState();
    const auto panel = overlay->panel(controller, state, layout_, overlaySlot(controller, state));
    if (contains(panel, anchor) &&
        overlay->scroll(controller, state, layout_, panel, anchor, deltaX, deltaY, modifiers))
      repaint();
    return true;
  }
  // A TUNE or MIX drag owns the pointer until it ends; no knob moves under it.
  if (bodyGesture_) return true;
  if (knobsShown()) {
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
  if (layout_.inspectorOpen && contains(layout_.inspector, anchor)) return true;
  if (auto* body = bodyWorkspace(); body != nullptr) {
    // The open inspector is modal and a body gesture owns the pointer: the body scrolls neither.
    if (!layout_.inspectorOpen && !bodyGesture_ && contains(workspaceArea(), anchor) &&
        body->scroll(controller, anchor, deltaX, deltaY, workspaceArea()))
      repaint();
    return true;
  }
  if (inMusicalArea(anchor) ||
      (workspace_ == Workspace::Sing && contains(layout_.laneTimePlot, anchor))) {
    controller.scroll(deltaX, deltaY, toLegacy(anchor), modifiers);
    return true;
  }
  return true;
}

bool SingShell::handleShellKey(NativeEditorController& controller, const KeyEvent& event) {
  // A re-homed overlay owns the keyboard while it is up. Escape closes it and returns focus to the
  // control that opened it; the overlay's own keys run its real commands; every other plain key
  // stops here, so nothing reaches the covered score.
  if (presented_) {
    keyboardInput_ = true;
    // Escape hides a shown tooltip and does nothing else, so focus and any open surface stay
    // (WCAG 1.4.13). Other keys hide a tip the pointer opened; a keyboard tip follows focus.
    if (event.key == NativeKey::Escape && shownTooltip_.has_value() &&
        tooltip_.shown(uiNow()) != nullptr) {
      tooltip_.dismiss();
      repaint();
      return true;
    }
    if (!tooltip_.fromKeyboard() && event.key != NativeKey::Tab) tooltip_.dismiss();
    // A surface that opened without a frame in between still takes the keyboard from a lyric.
    cancelCoveredLyric(controller);
    if (const auto* overlay = activeOverlay(controller); overlay != nullptr) {
      // A field open in the overlay (the time map's event field, an inline field card) has the
      // host's text input client: every key but Escape is the controller's own text handling
      // (Enter and Tab commit, as they did on the classic surface). Only a field the overlay owns
      // passes keys through; a composition without a field kind is never the overlay's.
      if (controller.textInputActive() &&
          controller.textFieldView().kind != NativeEditorController::TextFieldView::Kind::None &&
          event.key != NativeKey::Escape)
        return false;
      // A drag inside the card owns the input; Escape cancels it without committing.
      if (overlayGesture_) {
        if (event.key == NativeKey::Escape) cancelGestures(controller);
        return true;
      }
      if (event.key == NativeKey::Escape) {
        // The tree is current first, so the control that opened the surface is known even when no
        // rebuild ran since it opened.
        refreshSemantics(controller);
        // A surface with an inner page returns from it first and stays open.
        if (overlay->back(controller)) {
          refreshSemantics(controller);
          repaint();
          return true;
        }
        const auto fallback = overlay->openerId(controller, controller.sceneState());
        const auto opener = overlayOpener_.empty() ? fallback : overlayOpener_;
        const auto kind = overlay->kind();
        static_cast<void>(closeOverlay(controller, *overlay));
        // The controller's own tree follows the close first, so the focus snapshot the shell keeps
        // is the tree it will compare against on the next frame (the inspector close does the same).
        refreshSemantics(controller);
        // Focus returns to the control that opened it, when the shell publishes that control. A
        // field that returned to the surface under it (a review's draft field) leaves the keyboard
        // with that surface.
        const auto* next = activeOverlay(controller);
        // A close that only stepped back (a review's detail to its list, a draft field to its
        // review) leaves the surface up: its opener is kept for the Escape that closes it.
        if (next == nullptr) overlayOpener_.clear();
        const auto uncovered = next != nullptr && next->kind() != kind;
        if (uncovered) {
          semanticFocus_.clear();  // the surface that is up now takes focus on the next rebuild
        } else if (!opener.empty() && semantics_.publishes(opener)) {
          takeSemanticFocus(controller, opener);
        } else if (!fallback.empty() && semantics_.publishes(fallback)) {
          takeSemanticFocus(controller, fallback);
        } else {
          semanticFocus_.clear();
        }
        repaint();
        return true;
      }
      // An overlay's own keys are plain keys (Shift allowed). A Command or Option chord is an
      // application command, never an overlay key: Command-N is New Project, not Add Tempo, and
      // Command-Delete removes nothing. The host's own shortcuts pass through; the rest stop here.
      if (event.modifiers.primaryShortcut() || event.modifiers.alt)
        return !(hostActions_.applicationShortcut && hostActions_.applicationShortcut(event));
      // Tab walks the overlay's own controls, as the classic panels walked theirs; nothing under
      // the card is reachable, and the card itself is skipped. The singer menu's arrows walk its
      // items the same way (Up is Shift-Tab).
      const auto menu = overlay->kind() == OverlayKind::SingerMenu;
      if (event.key == NativeKey::Tab ||
          (menu && (event.key == NativeKey::Up || event.key == NativeKey::Down))) {
        const auto backwards =
            event.key == NativeKey::Tab ? event.modifiers.shift : event.key == NativeKey::Up;
        refreshSemantics(controller);
        const auto panelId = std::string{overlay->idPrefix()} + "panel";
        for (std::size_t step = 0U; step < 256U; ++step) {
          if (!semantics_.focusNext(backwards)) break;
          const auto* node = semantics_.focusedNode();
          if (node == nullptr) break;
          if (node->id == panelId || !overlayPublishes(controller, node->id)) continue;
          takeSemanticFocus(controller, node->id);
          break;
        }
        refreshSemantics(controller);
        repaint();
        return true;
      }
      refreshSemantics(controller);
      const auto* focused = semantics_.focusedNode();
      const std::string id = focused == nullptr ? std::string{} : focused->id;
      // Enter and Space run the focused menu item; a command that ran closes the menu.
      if ((menu || overlay->kind() == OverlayKind::About) &&
          (event.key == NativeKey::Enter || event.key == NativeKey::Space)) {
        if (overlayPublishes(controller, id))
          static_cast<void>(performOverlay(controller, *overlay, id, SemanticAction::Activate));
        refreshSemantics(controller);
        repaint();
        return true;
      }
      if (overlay->key(controller, id, event)) {
        repaint();
        return true;
      }
      return true;
    }
  }
  if (event.key == NativeKey::Escape && presented_ &&
      (knobDrag_ || bodyGesture_ ||
       (forwarding_ != ForwardArea::None && controller.pointerGestureActive()))) {
    cancelGestures(controller);
    return true;
  }
  // A TUNE or MIX drag in progress owns the input: any other key would commit its own edit under
  // the drag and leave the drag's release against a changed document. Escape (above) cancels it.
  if (presented_ && bodyGesture_) return true;
  if (event.key == NativeKey::Escape && presented_ && workspaceMenuOpen_) {
    setWorkspaceMenuOpen(controller, false);
    return true;
  }
  // Escape closes the compact inspector first (focus inside it returns to its button).
  if (event.key == NativeKey::Escape && presented_ && layout_.inspectorOpen &&
      !controller.textInputActive()) {
    setInspectorOpen(controller, false);
    return true;
  }
  // TUNE, MIX, EXPORT and the open inspector cover the score, so no key may edit it from here,
  // whatever holds focus. Escape returns to SING (the inspector handled its Escape above). Only
  // the shortcuts the host declares as its own application commands (it implements them itself)
  // still reach it; every other modified key (Alt-Delete, Command-D, Command-Delete, a Command-Q
  // the host does not handle) stops here instead of reaching the note editor.
  if (presented_ && (workspace_ != Workspace::Sing || layout_.inspectorOpen ||
                     workspaceMenuOpen_)) {
    if (event.key == NativeKey::Escape && workspace_ != Workspace::Sing) {
      // A workspace's open action menu closes first; the next Escape returns to SING.
      if (auto* body = bodyWorkspace(); body != nullptr && body->dismissTransient()) {
        repaint();
        return true;
      }
      setWorkspace(controller, Workspace::Sing);
      return true;
    }
    // VOICE shows the Voice Designer, so its undo and redo are the designer's while it has a
    // history to step through. Otherwise the command falls through to the application's own Undo.
    if (workspace_ == Workspace::Voice && event.modifiers.primaryShortcut() &&
        !event.modifiers.alt && (event.key == NativeKey::Z || event.key == NativeKey::Y)) {
      if (auto handled = routeUndo(event.key == NativeKey::Y || event.modifiers.shift))
        return static_cast<void>(std::move(*handled)), true;
    }
    if (event.modifiers.primaryShortcut() || event.modifiers.alt)
      return !(hostActions_.applicationShortcut && hostActions_.applicationShortcut(event));
  }
  // Keyboard focus walks the tree the shell published, so it reaches the controls that are on
  // screen here. An open text field keeps its own Tab behavior (commit and move on).
  const auto keyboardFocusOwned = presented_ && !event.modifiers.primaryShortcut() &&
                                  !event.modifiers.alt && !controller.textInputActive();
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
  // With the EXPORT workspace up or the inspector open the score is covered, so its plain keys
  // never edit it (Escape was handled above).
  const auto scoreHidden = [this] {
    return workspace_ != Workspace::Sing || layout_.inspectorOpen || workspaceMenuOpen_;
  };
  if (focused == nullptr) return scoreHidden();
  const std::string id = focused->id;
  // While a shell control holds focus, plain keys belong to it and never reach the note editor
  // (Delete must not delete the selected notes because a knob is focused). Command shortcuts such
  // as undo and save still pass through.
  if (ownsSemantic(id)) {
    if (auto* body = bodyWorkspace(); body != nullptr && id.starts_with(body->idPrefix()) &&
                                      event.key != NativeKey::Escape &&
                                      body->key(controller, id, event)) {
      repaint();
      return true;
    }
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
  const ScopedActiveShellStrings activeStrings{strings_.get()};
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
  const ScopedActiveShellStrings activeStrings{strings_.get()};
  const auto& l = layout_;
  const auto& legacy = controller.accessibilityTree().root();
  const auto* legacyFocus = controller.accessibilityTree().focusedNode();
  // Shell focus lasts only until the controller's own focus moves (a click on a note, a controller
  // keyboard command); then the controller's focus is the one reported.
  const std::string legacyFocusId = legacyFocus == nullptr ? std::string{} : legacyFocus->id;
  // While an overlay presents, the controller's own focus moves inside a tree the card replaces
  // (the time map's rows, a review's buttons), so it never takes the keyboard from the card.
  const auto overlayUp = activeOverlay(controller, state) != nullptr;
  if (!semanticFocus_.empty() && legacyFocusId != semanticFocusBaseline_ && !overlayUp)
    semanticFocus_.clear();
  std::string focusedId = semanticFocus_;
  SemanticNode root{.id = "shell",
                    .role = SemanticRole::Window,
                    .name = tr(Str::ProjectSEAMSing),
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
  static constexpr std::array<Str, 5U> kWorkspaces{Str::Sing, Str::Voice, Str::Tune, Str::Mix, Str::Export};
  // The tabs show only their icons below the label width, so each explains itself on hover.
  static constexpr std::array<Str, 5U> kWorkspaceTips{Str::TipSingWorkspace, Str::TipVoiceWorkspace,
                                                      Str::TipTuneWorkspace, Str::TipMixWorkspace,
                                                      Str::TipExportWorkspace};
  for (std::size_t i = 0U; i < kWorkspaces.size(); ++i) {
    if (l.workspaceTab[i].width <= 0.0 || l.workspaceTabs.width <= 0.0) break;
    add(SemanticNode{.id = "shell.workspace." + lowercase(englishShellString(kWorkspaces[i])),
                     .role = SemanticRole::Tab,
                     .name = trf(Str::NamedWorkspace, {tr(kWorkspaces[i])}),
                     .bounds = l.workspaceTab[i],
                     .selected = tabSelected(i),
                     .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
                     .description = tr(kWorkspaceTips[i])});
  }
  if (l.workspaceMenuButton.width >= 24.0) {
    add(SemanticNode{.id = "shell.workspace-menu", .role = SemanticRole::Button,
                     .name = tr(Str::WorkspacesAndAppearance),
                     .value = workspaceMenuOpen_ ? tr(Str::Open) : tr(Str::Closed),
                     .bounds = l.workspaceMenuButton,
                     .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
                     .description = tr(Str::TipWorkspaceMenu)});
    if (workspaceMenuOpen_) {
      static constexpr std::array<const char*, 5U> kIds{"sing", "voice", "tune", "mix", "export"};
      static constexpr std::array<Str, 5U> kNames{Str::Sing, Str::Voice, Str::Tune, Str::Mix,
                                                          Str::Export};
      for (std::size_t i = 0U; i < l.workspaceMenuRow.size(); ++i)
        add(SemanticNode{.id = std::string{"shell.workspace."} + kIds[i],
                         .role = SemanticRole::Button, .name = tr(kNames[i]),
                         .bounds = l.workspaceMenuRow[i],
                         .selected = tabSelected(i),
                         .actions = {SemanticAction::Activate, SemanticAction::SetFocus}});
    }
  }
  for (const auto mode : {DesignMode::Emo, DesignMode::Scene}) {
    const auto emo = mode == DesignMode::Emo;
    auto half = l.modeSwitch;
    half.width *= 0.5;
    if (!emo) half.x += half.width;
    if (half.width <= 0.0 && !workspaceMenuOpen_) continue;
    if (half.width <= 0.0) half = l.modeMenuRow[emo ? 0U : 1U];
    add(SemanticNode{.id = emo ? "shell.mode.emo" : "shell.mode.scene",
                     .role = SemanticRole::RadioButton,
                     .name = emo ? tr(Str::EMOLook) : tr(Str::SCENELook),
                     .bounds = half,
                     .selected = preferences_.mode == mode,
                     .actions = {SemanticAction::Activate, SemanticAction::SetFocus}});
  }
  rehome("toolbar.transport", l.playButton);
  // The play button is an icon: it says what it does, or why it cannot.
  if (!children.empty() && children.back().id == "toolbar.transport")
    children.back().description =
        children.back().enabled ? tr(Str::TipPlayStop) : tr(Str::TipNothingToPlay);
  rehome("toolbar.tempo", l.tempoReadout);
  rehome("toolbar.meter", l.meterReadout);
  if (l.outputMeterVisible) {
    // The header output meter reads the measured level per channel; without one it says so
    // rather than reporting a level. Activate clears a latched clip light.
    SemanticNode meter{.id = "shell.output-meter", .role = SemanticRole::Status,
                       .name = tr(Str::OutputLevel), .bounds = l.outputMeter};
    if (const auto& level = state.outputLevel; level.has_value()) {
      if (!level->bus.empty()) meter.name += ", " + level->bus;
      const auto channels = level->peak.size();
      for (std::size_t i = 0U; i < channels; ++i) {
        if (i > 0U) meter.value += ", ";
        const auto peak = level->peak[i] > 0.0F
                              ? format("%.1f dBFS", 20.0 * std::log10(static_cast<double>(level->peak[i])))
                              : std::string{tr(Str::InfDBFS)};
        if (channels == 2U) meter.value += trf(i == 0U ? Str::L : Str::R, {peak});
        else if (channels > 2U) meter.value += trf(Str::ChannelLevel, {std::to_string(i + 1U), peak});
        else meter.value += peak;
      }
      if (level->clipped) {
        meter.value = trf(Str::WithClipped, {meter.value});
        meter.actions.push_back(SemanticAction::Activate);
        meter.description = tr(Str::ActivateToResetTheClipLight);
      }
    } else {
      meter.value = tr(Str::NotMeasured);
      meter.description = tr(Str::NoOutputDeviceIsPlaying);
    }
    add(std::move(meter));
  }
  add(SemanticNode{.id = "shell.settings", .role = SemanticRole::Button, .name = tr(Str::SettingsSheet),
                   .bounds = l.settings,
                   .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
                   .description = tr(Str::TipSettingsSheet)});
  {
    const auto& languages = shellLanguages();
    const auto current = std::find_if(languages.begin(), languages.end(), [this](const ShellLanguage& language) {
      return language.code == preferences_.language;
    });
    const std::string name{current != languages.end() ? current->name : languages.front().name};
    add(SemanticNode{.id = "shell.language", .role = SemanticRole::Button, .name = tr(Str::Language),
                     .value = preferences_.languageFollowsSystem ? trf(Str::LanguageFollowsSystem, {name})
                                                                 : name,
                     .bounds = l.language,
                     .actions = {SemanticAction::Activate, SemanticAction::Increment,
                                 SemanticAction::Decrement, SemanticAction::SetFocus},
                     .description = tr(Str::LanguageControlDescription)});
  }
  // The track chip is painted only above the SING score; a covering workspace paints its own body
  // there, so it does not publish it.
  if (workspace_ == Workspace::Sing) {
    add(SemanticNode{.id = "shell.track", .role = SemanticRole::Status, .name = tr(Str::Track),
                     .value = state.inspector.valid && !state.inspector.name.empty()
                                  ? state.inspector.name
                                  : std::string{tr(Str::NoTrack)},
                     .bounds = l.trackLabel});
  }

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
    node.description += tr(Str::OutsideTheVisibleGridScrollTo);
  };
  const auto presentNote = [presentInGrid, painted](SemanticNode& node) {
    if (const auto found = painted->find(node.id); found != painted->end()) {
      node.bounds = found->second.bounds;
      const std::string_view dense{tr(Str::DenseOverlapNote)};
      if (found->second.hidden && node.description.find(dense) == std::string::npos)
        node.description = node.description.empty() ? std::string{dense}
                                                    : trf(Str::WithNote, {node.description, dense});
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
    // The visible count badge is the group's one actionable shell control. Importing the old
    // score-sized group control too gives screen-reader users two conflicting activation paths.
    if (!child.id.starts_with("detail.")) continue;
    auto copy = child;
    const auto transform = [&](SemanticNode& node, const auto& self) -> void {
      node.bounds = fromLegacy(node.bounds);
      for (auto& grandchild : node.children) self(grandchild, self);
    };
    transform(copy, transform);
    add(std::move(copy));
  }

  // Lane selector and the hosted lane.
  static constexpr std::array<Str, 8U> kLanes{Str::Dynamics, Str::Formant, Str::Breath, Str::Tension,
                                              Str::Air,      Str::Gender,  Str::Growl,  Str::Phonemes};
  const auto open = state.expressionLabelVisible();
  const auto selectedLane = technicalLane_ ? 7U
                            : open ? ui::expressionChannelIndex(state.expression.channel) + 1U
                                   : 99U;
  const auto tabWidth = singLaneTabWidth(l);
  for (std::size_t i = 0U; i < kLanes.size(); ++i) {
    add(SemanticNode{.id = "shell.lane-tab." + lowercase(englishShellString(kLanes[i])),
                     .role = SemanticRole::Tab,
                     .name = trf(Str::NamedLane, {tr(kLanes[i])}),
                     .bounds = {l.laneTabs.x + static_cast<double>(i) * (tabWidth + 4.0),
                                l.laneTabs.y, tabWidth, l.laneTabs.height},
                     .selected = i == selectedLane,
                     .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
                     .description = i == 0U   ? std::string{tr(Str::OpensTheDynamicsEditor)}
                                    : i == 7U ? std::string{tr(Str::ShowsThePhonemeUnitAndSeam)}
                                              : std::string{}});
  }
  if (technicalLane_) {
    add(SemanticNode{
        .id = "shell.lane",
        .role = SemanticRole::Lane,
        .name = tr(Str::PhonemesLane),
        .value = trf(state.selectedSeam.has_value() ? Str::PhonemeLaneSummarySelected
                                                     : Str::PhonemeLaneSummary,
                     {std::to_string(state.phonemes.tokens.size()),
                      std::to_string(state.unitOverrides.size()),
                      std::to_string(state.seamOverrides.size())}),
        .bounds = l.laneTimePlot,
        .enabled = true,
        .actions = {SemanticAction::SetFocus},
        .description = tr(Str::DragAPhonemeEdgeToMove)});
    const auto bands = technicalBands();
    for (std::size_t i = 0U; i < bands.band.size(); ++i) {
      add(SemanticNode{.id = std::string{"shell.lane.band."} + kTechnicalBandIds[i],
                       .role = SemanticRole::Button,
                       .name = trf(Str::NamedLane, {tr(kTechnicalBandNames[i])}),
                       .value = bands.collapsed[i] ? tr(Str::Collapsed) : tr(Str::Expanded),
                       .bounds = bands.toggle[i].width > 0.0 ? bands.toggle[i] : bands.band[i],
                       .actions = {SemanticAction::Activate, SemanticAction::Toggle,
                                   SemanticAction::SetFocus},
                       .description = bands.collapsed[i] ? tr(Str::ActivateToExpandThisLane)
                                                         : tr(Str::ActivateToCollapseThisLane)});
    }
  } else {
    add(SemanticNode{
        .id = "shell.lane",
        .role = SemanticRole::Lane,
        .name = open ? trf(Str::NamedCurve, {state.expression.label}) : tr(Str::ExpressionLane),
        .value = !open ? tr(Str::NoChannelSelected)
                 : !state.expression.refusal.empty() ? state.expression.refusal
                     : state.expression.points.empty()
                         ? tr(Str::NoCurveStored)
                         : trf(Str::PointCount, {std::to_string(state.expression.points.size())}),
        .bounds = l.laneTimePlot,
        .enabled = laneEditable_,
        .actions = {SemanticAction::SetFocus},
        .description = laneEditable_ ? tr(Str::ClickToAddAPointDrag)
                         : !open ? trf(Str::WithNote, {tr(Str::SelectAChannelToDrawIts),
                                                        tr(Str::LaneGettingStarted)})
                                 : state.expression.refusal});
  }

  // Singer rack.
  // Compact presentations publish the inspector button; its contents follow it in the tree (so Tab
  // from the button walks into them) only while it is open, at the rectangles painted there.
  if (l.rack != RackPresentation::Full) {
    add(SemanticNode{.id = "shell.inspector",
                     .role = SemanticRole::Button,
                     .name = tr(Str::SingerInspector),
                     .value = l.inspectorOpen ? tr(Str::Open) : tr(Str::Closed),
                     .bounds = l.inspectorButton,
                     .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
                     .description = tr(Str::ShowsTheSingerTheExpressionKnobs)});
  }
  if (knobsShown()) {
    rehome("voice.identity", l.singer);
    add(SemanticNode{.id = "shell.change-voice", .role = SemanticRole::Button, .name = tr(Str::ChangeVoice),
                     .bounds = l.singerChange,
                     .actions = {SemanticAction::Activate, SemanticAction::SetFocus}});
    if (l.singerMenu.width > 0.0)
      add(SemanticNode{.id = std::string{kSingerMenuButtonId},
                       .role = SemanticRole::Button,
                       .name = tr(Str::SingerActions),
                       .value = singerMenuOpen_ ? tr(Str::Open) : tr(Str::Closed),
                       .bounds = l.singerMenu,
                       .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
                       .description = tr(Str::OpensTheSingerSReviewsInspectors)});
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
          .value = refused ? k.refusal : (percent ? number + "%" : trf(Str::Semitones, {number})),
          .bounds = l.knob[i],
          .enabled = !refused,
          .actions = refused ? std::vector<SemanticAction>{SemanticAction::SetFocus}
                             : std::vector<SemanticAction>{SemanticAction::Increment,
                                                           SemanticAction::Decrement,
                                                           SemanticAction::Activate,
                                                           SemanticAction::SetFocus},
          .description = refused ? k.refusal
                                 : tr(Str::ValueAtThePlayheadActivateOpens),
          .numericValue = k.value * scale,
          .numericMinimum = static_cast<double>(k.descriptor.minimum) * scale,
          .numericMaximum = static_cast<double>(k.descriptor.maximum) * scale,
          .numericStep = static_cast<double>(k.descriptor.step) * scale});
    }
    std::string styles;
    for (const auto& card : state.voicebankCards)
      if (card.id == state.inspector.voicebank.id && !card.id.empty())
        for (const auto& style : card.styles) styles += (styles.empty() ? "" : ", ") + style;
    add(SemanticNode{.id = "shell.style", .role = SemanticRole::Panel, .name = tr(Str::StylePresets),
                     .value = styles.empty() ? tr(Str::TheSelectedVoicePublishesNoStyle) : styles,
                     .bounds = l.style});
  }

  // Status bar: render state, diagnostics and export actions stay reachable.
  const auto& s = state.renderStatus;
  add(SemanticNode{.id = "shell.status", .role = SemanticRole::Status, .name = tr(Str::RenderStatus),
                   .value = std::string{renderStatusStateName(s.state)} +
                            (s.diagnostic.empty() ? "" : ": " + s.diagnostic),
                   .bounds = l.status,
                   // The bar's left message (the audio device, the leading diagnostic or the
                   // render note), which narrow windows elide: said whole here and in its tooltip.
                   .description = singStatusMessage(state).text});
  if (s.state == RenderStatusState::Rendering || s.state == RenderStatusState::Queued) {
    add(SemanticNode{.id = "shell.render-progress", .role = SemanticRole::ProgressIndicator,
                     .name = tr(Str::RenderProgress),
                     .value = format("%.0f%%", std::clamp(s.fraction, 0.0, 1.0) * 100.0),
                     .bounds = l.status,
                     .numericValue = std::clamp(s.fraction, 0.0, 1.0) * 100.0,
                     .numericMinimum = 0.0,
                     .numericMaximum = 100.0});
  }
  // Diagnostics are presented as the shell's own toast stack and popover, so they are not folded
  // into the status bar here. Export nodes with no shell rectangle of their own (the cancel action
  // of a strip too narrow for a segment) keep the status bar's bounds.
  for (const auto& child : legacy.children) {
    if (!child.id.starts_with("diagnostic") && !child.id.starts_with("export.")) continue;
    if (child.id.starts_with("diagnostic")) continue;
    if (child.id == "export.progress" && exportStatusSegment(state).width > 0.0) continue;
    auto copy = child;
    copy.bounds = l.status;
    copy.children.clear();
    add(std::move(copy));
  }

  const auto singShown = workspace_ == Workspace::Sing;
  // The export-progress segment in the status bar, reusing the controller's own export nodes (the
  // progress status and its cancel action), which are the same commands the classic strip ran.
  if (const auto segment = exportStatusSegment(state); segment.width > 0.0) {
    if (const auto* progress = findLegacy("export.progress"); progress != nullptr) {
      auto copy = *progress;
      copy.bounds = segment;
      copy.children.clear();
      for (const auto& child : progress->children) {
        if (child.id != "export.cancel") continue;
        auto cancel = child;
        // The cancel button sits at the segment's right end, inside the segment's own bounds.
        const auto width = std::min(84.0, std::max(0.0, segment.width * 0.35));
        if (width < 48.0) break;
        cancel.bounds = {segment.right() - width - 4.0, segment.y + 2.0, width,
                         segment.height - 4.0};
        copy.children.push_back(std::move(cancel));
      }
      add(std::move(copy));
    }
  }
  if (!state.diagnostics.empty()) {
    // The toast stack above the status bar: the shell's own presentation of the topmost
    // diagnostic, and the popover opener beside it.
    add(SemanticNode{.id = "shell.diagnostics.toast", .role = SemanticRole::Status,
                     .name = tr(Str::Diagnostic),
                     .value = state.diagnostics.size() > 1U
                                  ? trf(Str::TitleAndMore,
                                        {presentDiagnostic(state.diagnostics.front()).title,
                                         std::to_string(state.diagnostics.size() - 1U)})
                                  : presentDiagnostic(state.diagnostics.front()).title,
                     .bounds = diagnosticsToastBounds(),
                     .actions = {SemanticAction::SetFocus}});
    if (const auto openButton = diagnosticsOpenButton(); openButton.width > 0.0)
      add(SemanticNode{.id = "shell.diagnostics.open", .role = SemanticRole::Button,
                       .name = tr(Str::Diagnostics), .bounds = openButton,
                       .selected = diagnosticsOpen_,
                       .actions = {SemanticAction::Activate, SemanticAction::SetFocus}});
  }
  if (singShown) {
    // The re-homed overlays' openers, at the rectangles the shell paints them in. Each opens its
    // surface through the controller's own command.
    if (l.rulerTimeMapButton.width > 0.0)
      add(SemanticNode{.id = "shell.ruler.time-map", .role = SemanticRole::Button,
                       .name = tr(Str::TempoAndMeterEvents),
                       .bounds = l.rulerTimeMapButton,
                       .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
                       // The button's painted caption, which it may show elided: its tooltip and
                       // a screen reader then say the same words.
                       .description = tr(Str::TimeMap)});
    if (l.laneReviewButton.width > 0.0)
      add(SemanticNode{.id = "shell.lane.review", .role = SemanticRole::Button,
                       .name = tr(Str::ReviewRetainedEdits), .bounds = l.laneReviewButton,
                       .actions = {SemanticAction::Activate, SemanticAction::SetFocus}});
    for (const auto& badge : layoutSingOverlapBadges(controller.pianoRoll().visibleNotes(), l.grid))
      add(SemanticNode{.id = "shell.note.overlap." + std::to_string(badge.group),
                       .role = SemanticRole::Button,
                       .name = tr(Str::OverlappingNotes),
                       .value = trf(Str::OverlappingNoteCount, {std::to_string(badge.members)}),
                       .bounds = badge.bounds,
                       .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
                       .description = tr(Str::OpensTheOverlapDetailBesideThe)});
    // What the notes show of the rendered audio, as painted in this frame.
    add(SemanticNode{.id = "shell.waveform", .role = SemanticRole::Status,
                     .name = tr(Str::NoteWaveform),
                     .value = waveform_.shown() ? std::string{tr(Str::ShowingTheCurrentRender)}
                                                : waveform_.caption,
                     .bounds = layout_.gridLabel,
                     .actions = {SemanticAction::SetFocus},
                     .description = waveform_.reason});
  }
  // The open inspector is modal: only it and the read-only status are published, so Tab stays in
  // it and no action reaches the score, the lane or a control it covers.
  // The open inspector covers whichever workspace is shown, so it is modal over TUNE, MIX and
  // EXPORT too: their nodes are neither kept here nor added below.
  if (l.inspectorOpen) {
    std::erase_if(children, [](const SemanticNode& node) {
      return !(node.id == "shell.inspector" || node.id == "shell.change-voice" ||
               node.id == kSingerMenuButtonId ||
               node.id == "shell.style" || node.id.starts_with("shell.knob.") ||
               node.id == "voice.identity" || node.id == "shell.status" ||
               node.id == "shell.render-progress");
    });
  }
  const auto* overlay = activeOverlay(controller, state);
  // A surface the controller opened while the singer menu was up (a host command) replaces the
  // menu; it does not come back when that surface closes.
  if (singerMenuOpen_ && overlay != nullptr && overlay->kind() != OverlayKind::SingerMenu)
    singerMenuOpen_ = false;
  if (aboutOpen_ && overlay != nullptr && overlay->kind() != OverlayKind::About) aboutOpen_ = false;
  const auto scoreCovered =
      !singShown || l.inspectorOpen || workspaceMenuOpen_ || overlay != nullptr;
  if (!singShown) {
    // TUNE, MIX and EXPORT cover the score: nothing of the grid or lane is published under them.
    std::erase_if(children, [](const SemanticNode& node) {
      return node.id == "timeline" || node.id.starts_with("editor.vibrato.handle.") ||
             node.id.starts_with("overlap-group.") || node.id.starts_with("detail.") ||
             node.id.starts_with("shell.lane");
    });
  }
  if (const auto* body = bodyWorkspace(); body != nullptr && !l.inspectorOpen) {
    std::vector<SemanticNode> nodes;
    body->semantics(controller, state, workspaceArea(), nodes);
    for (auto& node : nodes)
      if (node.id.starts_with(body->idPrefix())) add(std::move(node));
  }
  if (workspace_ == Workspace::Export && !l.inspectorOpen) {
    const auto plan = hostActions_.exportPlan ? hostActions_.exportPlan() : std::nullopt;
    // Busy is read live here too: accessibility must not offer a run the paint cache still shows.
    const auto busy = exportBusy(controller);
    const auto available = static_cast<bool>(hostActions_.exportSet) && !busy;
    const auto panel = exportPanelLayout(exportArea());
    const auto summary = exportPlanSummary(plan, hostActions_.exportUnavailable);
    add(SemanticNode{.id = "shell.export.panel", .role = SemanticRole::Panel,
                     .name = tr(Str::ExportSet), .value = summary, .bounds = exportArea(),
                     .actions = {SemanticAction::SetFocus}});
    add(SemanticNode{
        .id = "shell.export.run", .role = SemanticRole::Button, .name = tr(Str::ExportSet),
        .bounds = panel.button, .enabled = available,
        .actions = available ? std::vector<SemanticAction>{SemanticAction::Activate,
                                                           SemanticAction::SetFocus}
                             : std::vector<SemanticAction>{SemanticAction::SetFocus},
        .description = !hostActions_.exportSet ? hostActions_.exportUnavailable
                       : busy                 ? std::string{tr(Str::AnExportIsAlreadyRunning)}
                                              : std::string{tr(Str::ChooseANewFolderForThe)}});
    if (state.bounceTimingAvailable && panel.bounce.width > 0.0)
      add(SemanticNode{
          .id = "shell.export.bounce", .role = SemanticRole::Button,
          .name = state.bounceFollowHost ? tr(Str::BounceTimingFollowTheHost)
                                         : tr(Str::BounceTimingScoreTempoMap),
          .value = state.bounceFollowHost ? tr(Str::FollowHost) : tr(Str::FixedAudio),
          .bounds = panel.bounce, .selected = state.bounceFollowHost,
          .actions = {SemanticAction::Activate, SemanticAction::Toggle, SemanticAction::SetFocus},
          .description = tr(Str::ChooseWhetherAFinalBounceFollows)});
    const auto& progress = controller.exportProgress();
    const auto statusBounds = panel.status.height > 0.0 ? panel.status : panel.button;
    if (busy) {
      const auto fraction = static_cast<double>(progress.completedFiles) /
                            static_cast<double>(std::max<std::uint64_t>(1U, progress.totalFiles));
      add(SemanticNode{.id = "shell.export.progress", .role = SemanticRole::ProgressIndicator,
                       .name = tr(Str::ExportProgress),
                       .value = trf(Str::ExportStateFilesOf,
                                    {exportStateLabel(progress.state),
                                     std::to_string(progress.completedFiles),
                                     std::to_string(progress.totalFiles)}),
                       .bounds = statusBounds,
                       .numericValue = fraction * 100.0,
                       .numericMinimum = 0.0,
                       .numericMaximum = 100.0});
    } else if (attemptEnded(progress.state)) {
      // A failed or cancelled attempt is reported with its reason, whether or not any file was
      // counted and whatever an earlier successful export wrote.
      add(SemanticNode{.id = "shell.export.attempt", .role = SemanticRole::Status,
                       .name = tr(Str::LastExportAttempt),
                       .value = exportStateLabel(progress.state) +
                                (progress.currentOutput.empty() ? "" : ": " + progress.currentOutput),
                       .bounds = statusBounds,
                       .actions = {SemanticAction::SetFocus}});
    }
    const auto& last = state.lastExport;
    add(SemanticNode{
        .id = "shell.export.last", .role = SemanticRole::Status, .name = tr(Str::LastExport),
        .value = last ? trf(Str::ExportStateFilesIn,
                            {exportStateLabel(last->state), std::to_string(last->files.size()),
                             (last->setPath.empty() ? last->masterPath.parent_path() : last->setPath)
                                 .filename()
                                 .string()})
                      : std::string{tr(Str::NothingExportedInThisSession)},
        .bounds = statusBounds,
                        .actions = {SemanticAction::SetFocus}});
  }
  if (workspaceMenuOpen_) {
    const auto modesInMenu = l.modeMenuRow[0].width > 0.0;
    std::erase_if(children, [modesInMenu](const SemanticNode& node) {
      return node.id != "shell.workspace-menu" && !node.id.starts_with("shell.workspace.") &&
             !(modesInMenu && node.id.starts_with("shell.mode.")) && node.id != "shell.status" &&
             node.id != "shell.render-progress";
    });
  }
  // A re-homed overlay is modal above the shell like the compact inspector: only its own card and
  // the read-only status remain, so Tab stays inside it, no note node is published under it and no
  // action reaches the score or the lane it covers.
  if (overlay != nullptr) {
    if (overlay->kind() != presentedOverlay_) {
      if (presentedOverlay_ == OverlayKind::TextField && overlay->kind() == fieldOpenedOver_)
        overlay->resumed();
      else
        overlay->presented();
      fieldOpenedOver_ =
          overlay->kind() == OverlayKind::TextField ? presentedOverlay_ : OverlayKind::None;
      // The control that opened the surface from the shell (MIX's Settings, VOICE's browser
      // button) is where Escape returns focus; a surface that follows another keeps the first one.
      if (presentedOverlay_ == OverlayKind::None)
        overlayOpener_ = semanticFocus_.starts_with("shell.overlay.") ? std::string{} : semanticFocus_;
    }
    auto nodes = overlaySemantics(controller, state);
    std::vector<std::string> published;
    published.reserve(nodes.size());
    for (const auto& node : nodes) published.push_back(node.id);
    // Everything under the card goes, including a shell node that shares an id with one of the
    // card's controls (the transport's tempo readout while its field is open): the card publishes
    // its own node for that id, once, at the card's rectangle.
    std::erase_if(children, [](const SemanticNode& node) {
      return node.id != "shell.status" && node.id != "shell.render-progress";
    });
    // The card always holds the keyboard: when it opens, or when the control that had focus left
    // it, its first control takes focus, so Enter and Space act on something the creator can see.
    const auto panelId = std::string{overlay->idPrefix()} + "panel";
    const auto holdsFocus =
        !semanticFocus_.empty() && semanticFocus_ != panelId &&
        std::find(published.begin(), published.end(), semanticFocus_) != published.end();
    // A field that opens inside the card (the time map's event field, the next prompt of a
    // two-step entry) takes the keyboard when it appears, as the classic panel focused its input.
    std::string field;
    for (const auto& node : nodes)
      if (node.enabled && std::find(node.actions.begin(), node.actions.end(),
                                    SemanticAction::EditText) != node.actions.end()) {
        field = node.id;
        break;
      }
    if (!field.empty() && field != overlayField_ && overlay->kind() == presentedOverlay_) {
      semanticFocus_ = field;
      semanticFocusBaseline_ = legacyFocusId;
      focusedId = field;
    }
    overlayField_ = field;
    if (overlay->kind() != presentedOverlay_ || !holdsFocus) {
      std::string first;
      for (const auto& node : nodes) {
        if (node.id == panelId) continue;
        const auto offers = [&node](SemanticAction action) {
          return std::find(node.actions.begin(), node.actions.end(), action) != node.actions.end();
        };
        // A field takes focus before anything else: it is what the surface was opened to fill.
        const auto activatable =
            node.enabled && (offers(SemanticAction::Activate) || offers(SemanticAction::EditText));
        if (first.empty() || activatable) first = node.id;
        if (activatable) break;
      }
      if (!first.empty() && (overlay->kind() != presentedOverlay_ || !holdsFocus)) {
        semanticFocus_ = first;
        semanticFocusBaseline_ = legacyFocusId;
        focusedId = first;
      }
    }
    presentedOverlay_ = overlay->kind();
    for (auto& node : nodes) children.push_back(std::move(node));
  } else {
    presentedOverlay_ = OverlayKind::None;
    fieldOpenedOver_ = OverlayKind::None;
    overlayField_.clear();
  }
  // A shell control that is no longer published (a knob after the rack collapsed to a rail) gives
  // up focus, and with it the keys; the editor's own focus is reported instead.
  if (!semanticFocus_.empty() && !EditorSemanticTree::containsId(root, semanticFocus_)) {
    semanticFocus_.clear();
    focusedId.clear();
  }
  if (focusedId.empty() && legacyFocus != nullptr) focusedId = legacyFocus->id;
  // A controller focus inside the covered score is not reported while EXPORT or the inspector is up.
  if (scoreCovered && !focusedId.empty() && !EditorSemanticTree::containsId(root, focusedId))
    focusedId.clear();
  semantics_.rebuildCustom(std::move(root), focusedId,
                           !scoreCovered ? VirtualNoteSource{.tree = &controller.accessibilityTree(),
                                                             .present = presentNote}
                                         : VirtualNoteSource{});
}

core::Result<void> SingShell::dispatchSemantic(NativeEditorController& controller,
                                               std::string_view id, SemanticAction action) {
  if (!ownsSemantic(id))
    return core::failure(core::ErrorCode::NotFound, tr(Str::NotAShellAccessibilityElement));
  if (!presented_)
    return core::failure(core::ErrorCode::InvalidState,
                         tr(Str::TheRedesignedEditorIsNotOn));
  // Validate against the current layout, capabilities and state, not the last painted tree: a
  // stale id (a knob removed by the rail layout), an unknown id, a disabled control or an
  // unsupported action is refused before anything reaches the document.
  refreshSemantics(controller);
  if (bodyGesture_ && action != SemanticAction::SetFocus)
    return core::failure(core::ErrorCode::InvalidState, tr(Str::ADragIsInProgress));
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
                                            tr(Str::ThisElementDoesNotSupportThat));
  if (id == "shell.workspace-menu" && activate) {
    setWorkspaceMenuOpen(controller, !workspaceMenuOpen_);
    result = core::success();
  } else if (id == "shell.workspace.sing" && activate) {
    setWorkspace(controller, Workspace::Sing);
    result = core::success();
  } else if (id == "shell.workspace.tune" && activate) {
    setWorkspace(controller, Workspace::Tune);
    result = core::success();
  } else if (id == "shell.workspace.mix" && activate) {
    setWorkspace(controller, Workspace::Mix);
    result = core::success();
  } else if (auto* body = bodyWorkspace(); body != nullptr && id.starts_with(body->idPrefix())) {
    result = body->perform(controller, id, action);
  } else if (id == "shell.workspace.export" && activate) {
    setWorkspace(controller, Workspace::Export);
    result = core::success();
  } else if (id == "shell.workspace.voice" && activate) {
    setWorkspace(controller, Workspace::Voice);
    result = core::success();
  } else if (id == "shell.export.run" && activate) {
    result = runExportSet(controller);
  } else if (id == "shell.mode.emo" && activate) {
    setWorkspaceMenuOpen(controller, false);
    setMode(DesignMode::Emo);
    result = core::success();
  } else if (id == "shell.mode.scene" && activate) {
    setWorkspaceMenuOpen(controller, false);
    setMode(DesignMode::Scene);
    result = core::success();
  } else if (id == "shell.settings" && activate) {
    takeSemanticFocus(controller, "shell.settings");
    setSettingsOpen(true);
    result = core::success();
  } else if (id == "shell.language" && (activate || action == SemanticAction::Increment ||
                                         action == SemanticAction::Decrement)) {
    cycleLanguage(action == SemanticAction::Decrement ? -1 : 1);
    result = core::success();
  } else if (id == "shell.ruler.time-map" && activate) {
    // The transport display's tempo readout opens the time map; the ruler's own button opens the
    // same panel the classic TIME MAP button did.
    result = controller.openTimeMapPanel();
  } else if (id == "shell.lane.review" && activate) {
    result = controller.openPhonemeReview();
  } else if (id == "shell.diagnostics.open" && activate) {
    setDiagnosticsOpen(!diagnosticsOpen_);
    result = core::success();
  } else if (id.starts_with("shell.note.overlap.") && activate) {
    const auto index = id.substr(std::string_view{"shell.note.overlap."}.size());
    std::size_t group = 0U;
    const auto parsed = std::from_chars(index.data(), index.data() + index.size(), group);
    result = parsed.ec == std::errc{} && parsed.ptr == index.data() + index.size()
                 ? controller.openOverlapDetail(group)
                 : core::failure(core::ErrorCode::InvalidArgument, tr(Str::InvalidOverlapGroup));
  } else if (auto* overlay = const_cast<ShellOverlay*>(activeOverlay(controller));
             overlay != nullptr && overlayPublishes(controller, id)) {
    // A control of a presented overlay: its own real command, validated against its current state.
    refreshSemantics(controller);
    if (!semantics_.publishes(id)) {
      result = core::failure(core::ErrorCode::Conflict, tr(Str::ThisControlIsNotOnScreen));
    } else if (id == std::string{overlay->idPrefix()} + "panel") {
      result = core::success();
    } else {
      result = performOverlay(controller, *overlay, id, action);
    }
  } else if (id == "shell.output-meter" && activate) {
    controller.resetOutputClip();
    result = core::success();
  } else if (id == "shell.change-voice" && activate) {
    controller.showVoicebankBrowser();
    result = core::success();
  } else if (id == kSingerMenuButtonId && activate) {
    result = setSingerMenuOpen(controller, !singerMenuOpen_);
  } else if (id == "shell.inspector" && activate) {
    // Opening moves focus to the button (so the next Tab walks into the inspector); closing
    // returns focus from inside to it.
    setInspectorOpen(controller, !layout_.inspectorOpen);
    result = core::success();
  } else if (id.starts_with("shell.lane-tab.") && activate) {
    static constexpr std::array<std::string_view, 6U> kChannels{"formant", "breath", "tension",
                                                                "air", "gender", "growl"};
    const auto name = id.substr(std::string_view{"shell.lane-tab."}.size());
    if (name == "dynamics") result = controller.openDynamicsInspector();
    if (name == "phonemes") result = showTechnicalLanes(controller);
    for (std::size_t i = 0U; i < kChannels.size(); ++i) {
      if (name != kChannels[i]) continue;
      technicalLane_ = false;
      if (presented_) applyGeometry(controller);
      result = controller.openExpressionLane(ui::expressionChannelAt(i));
    }
  } else if (id == "shell.export.bounce" && activate) {
    result = controller.toggleBounceTiming();
  } else if (id.starts_with("shell.lane.band.") && activate) {
    const auto name = id.substr(std::string_view{"shell.lane.band."}.size());
    for (std::size_t i = 0U; i < kTechnicalBandIds.size(); ++i)
      if (name == kTechnicalBandIds[i]) result = toggleTechnicalBand(controller, i);
  } else if (id.starts_with("shell.knob.")) {
    static constexpr std::array<std::string_view, 6U> kKnobs{"formant", "breath", "tension",
                                                             "air", "gender", "growl"};
    const auto name = id.substr(std::string_view{"shell.knob."}.size());
    for (std::size_t i = 0U; i < kKnobs.size(); ++i) {
      if (name != kKnobs[i]) continue;
      if (knobRefused_[i]) {
        result = core::failure(core::ErrorCode::Unsupported,
                               tr(Str::TheSelectedSingerCannotApplyThis));
      } else if (action == SemanticAction::Increment) {
        result = nudge(controller, i, 1);
      } else if (action == SemanticAction::Decrement) {
        result = nudge(controller, i, -1);
      } else if (activate) {
        result = controller.openExpressionLane(ui::expressionChannelAt(i));
      }
    }
  }
  repaint();
  return result;
}

}  // namespace seam::native_ui::design
