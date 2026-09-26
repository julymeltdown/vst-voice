#include "seam/native_ui/design/shell_workspace.hpp"

#include "seam/ui/expression_lane.hpp"
#include "seam/ui/vibrato_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

// The TUNE workspace (docs/design/SEAM_UI_REDESIGN_CODE_PLAN_2026-09-25.md §7.3): the six timbral
// channels drawn over one large graph across the selected region, the region's pitch curve, the
// selected note's vibrato with a preview computed from its stored parameters, and a strip of small
// expression knobs. Every edit is an existing NativeEditorController command, so one gesture is one
// undo step and Escape (cancelGestures) commits nothing.
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
constexpr std::size_t kChannels = ui::kExpressionChannelCount;
constexpr std::array<const char*, kChannels> kLabels{"Formant", "Breath", "Tension",
                                                     "Air",     "Gender", "Growl"};
constexpr std::array<const char*, kChannels> kIds{"formant", "breath", "tension",
                                                  "air",     "gender", "growl"};
// Pixels of vertical travel per knob step, and wheel distance per step, as on the SING rack.
constexpr double kKnobStepPixels = 12.0;
constexpr double kScrollStep = 8.0;
// A press this close to a stored point grabs it instead of inserting a new one.
constexpr double kPointGrabRadius = 8.0;
// Vertical breathing room inside the graph, so a point at a bound is still fully drawn.
constexpr double kPlotMargin = 5.0;

bool contains(ui::Rect r, ui::Point p) noexcept {
  return p.x >= r.x && p.y >= r.y && p.x < r.right() && p.y < r.bottom();
}

bool usable(ui::Rect r) noexcept { return r.width > 0.0 && r.height > 0.0; }

ui::Rect inset(ui::Rect r, double dx, double dy) noexcept {
  return {r.x + dx, r.y + dy, std::max(0.0, r.width - 2.0 * dx), std::max(0.0, r.height - 2.0 * dy)};
}

std::string format(const char* pattern, double value) {
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), pattern, value);
  return buffer;
}

TextStyle style(FontRole role, double size, double tracking = 0.0,
                TextAlign align = TextAlign::Left, bool upper = false) {
  return TextStyle{role, size, tracking, align, upper};
}

// Tightens tracking, then steps the size down to the 10-point floor; the canvas ellipsizes the rest.
TextStyle fitted(Canvas2D& c, std::string_view text, TextStyle s, double width) {
  if (width <= 0.0 || c.measure(text, s) <= width) return s;
  s.tracking = std::min(s.tracking, 0.4);
  while (s.size > 10.0 && c.measure(text, s) > width) s.size = std::max(10.0, s.size - 0.5);
  return s;
}

// The SING rack's card material.
void glassPanel(Canvas2D& c, const DesignTokens& t, ui::Rect r, double radius) {
  if (!usable(r)) return;
  const auto p = Path::roundedRect(r, radius);
  c.save();
  c.setAlpha(0.90);
  c.fill(p, LinearGradient{{r.x, r.y}, {r.x, r.bottom()},
                           {{0.0, t.color.surfaceRaised}, {1.0, t.color.surface}}});
  c.restore();
  c.stroke(p, withAlpha(t.color.border, 0.95), StrokeStyle{1.0});
  Path highlight;
  highlight.moveTo({r.x + radius, r.y + 1.0}).lineTo({r.right() - radius, r.y + 1.0});
  c.stroke(highlight, withAlpha(kWhite, t.light.highlightAlpha * 1.6), StrokeStyle{1.0});
}

// The SING lane's recessed plot.
void sunken(Canvas2D& c, const DesignTokens& t, ui::Rect r, double radius) {
  if (!usable(r)) return;
  const auto p = Path::roundedRect(r, radius);
  c.fill(p, LinearGradient{{r.x, r.y}, {r.x, r.bottom()},
                           {{0.0, t.color.surfaceSunken}, {1.0, t.color.surface}}});
  c.stroke(p, withAlpha(t.color.border, 0.9), StrokeStyle{1.0});
}

void cardTitle(Canvas2D& c, const DesignTokens& t, ui::Rect title, std::string_view text, bool lit) {
  if (!usable(title)) return;
  c.save();
  if (lit) c.setGlow(t.color.accent, 7.0);
  c.fill(Path::circle({title.x + 4.0, title.y + title.height * 0.5}, 3.5),
         lit ? t.color.accent : t.color.textDisabled);
  c.restore();
  const ui::Rect label{title.x + 14.0, title.y, std::max(0.0, title.width - 14.0), title.height};
  c.text(label, text,
         fitted(c, text, style(FontRole::UiSemibold, t.type.panelTitle, t.type.panelTitleTracking,
                               TextAlign::Left, true), label.width),
         t.color.textPrimary);
}

// One color per channel, drawn from the look's own roles so EMO stays restrained and SCENE vivid.
Color channelColor(const DesignTokens& t, std::size_t index) {
  const auto& c = t.color;
  const std::array<Color, kChannels> emo{c.accent, c.accentAlt1, c.accentCurve,
                                         c.accentAlt2, c.warning, c.success};
  const std::array<Color, kChannels> scene{c.accent, c.accentCurve, c.accentTime,
                                           c.accentAlt1, c.accentAlt2, c.success};
  return (t.mode == DesignMode::Scene ? scene : emo)[index % kChannels];
}

// ---- Expression knobs (the same read model and refusals as the SING rack) ----------------------

struct KnobModel final {
  ui::ExpressionChannelDescriptor descriptor;
  double value{0.0};
  std::string refusal;
  std::size_t storedPoints{0U};
};

std::array<KnobModel, kChannels> knobModels(const EditorSceneState& state) {
  std::array<KnobModel, kChannels> knobs;
  for (std::size_t i = 0U; i < knobs.size(); ++i) {
    const auto channel = ui::expressionChannelAt(i);
    knobs[i].descriptor = ui::describeExpressionChannel(channel);
    knobs[i].value = knobs[i].descriptor.neutral;
    knobs[i].refusal = state.inspector.valid ? "" : "No vocal track is selected";
    for (const auto& row : state.inspector.expressionCapabilities) {
      if (row.channel != channel) continue;
      knobs[i].value = row.valueAtPlayhead;
      knobs[i].refusal = row.refusal;
      knobs[i].storedPoints = row.storedPoints;
    }
    if (knobs[i].refusal.empty() && !state.playheadInsideRegion)
      knobs[i].refusal = "The playhead is outside the selected region";
  }
  return knobs;
}

bool semitones(const ui::ExpressionChannelDescriptor& d) { return d.unit == "semitones"; }

std::string valueText(const ui::ExpressionChannelDescriptor& d, double value) {
  return semitones(d) ? format("%.1f st", value) : format("%.0f%%", value * 100.0);
}

// The SING knob path: one channel-specific command per step count, so one gesture is one undo step.
core::Result<void> nudge(NativeEditorController& controller, std::size_t index, int steps) {
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

std::optional<std::size_t> activeChannel(const EditorSceneState& state) {
  if (!state.expressionLabelVisible()) return std::nullopt;
  return ui::expressionChannelIndex(state.expression.channel);
}

std::optional<std::size_t> activeChannel(const NativeEditorController& controller) {
  if (!controller.expressionLaneOpen()) return std::nullopt;
  return ui::expressionChannelIndex(controller.selectedExpressionChannel());
}

// ---- The selected note's vibrato ---------------------------------------------------------------

constexpr std::size_t kFields = 6U;
enum Field : std::size_t { Start, FadeIn, FadeOut, Depth, Period, Phase };
constexpr std::array<const char*, kFields> kFieldLabels{"Start", "Fade in", "Fade out",
                                                        "Depth", "Period",  "Phase"};
constexpr std::array<const char*, kFields> kFieldIds{"start", "fade-in", "fade-out",
                                                     "depth", "period",  "phase"};

struct FieldSpec final {
  double minimum{0.0};
  double maximum{1.0};
  double step{0.01};
};

// The bounds NoteVibrato::validate accepts, with the two fades sharing their span.
FieldSpec fieldSpec(const domain::NoteVibrato& v, std::size_t field) {
  switch (field) {
    case Start: return {0.0, 1.0, 0.01};
    case FadeIn: return {0.0, std::max(0.0, 1.0 - static_cast<double>(v.fadeOutFraction)), 0.01};
    case FadeOut: return {0.0, std::max(0.0, 1.0 - static_cast<double>(v.fadeInFraction)), 0.01};
    case Depth: return {0.0, 200.0, 5.0};
    case Period: return {5.0, 500.0, 5.0};
    default: return {0.0, 0.99, 0.05};
  }
}

double fieldValue(const domain::NoteVibrato& v, std::size_t field) {
  switch (field) {
    case Start: return v.startFraction;
    case FadeIn: return v.fadeInFraction;
    case FadeOut: return v.fadeOutFraction;
    case Depth: return v.depthCents;
    case Period: return v.periodMilliseconds;
    default: return v.phaseTurns;
  }
}

void setField(domain::NoteVibrato& v, std::size_t field, double value) {
  const auto f = static_cast<float>(value);
  switch (field) {
    case Start: v.startFraction = f; break;
    case FadeIn: v.fadeInFraction = f; break;
    case FadeOut: v.fadeOutFraction = f; break;
    case Depth: v.depthCents = f; break;
    case Period: v.periodMilliseconds = f; break;
    default: v.phaseTurns = f; break;
  }
}

ui::VibratoFields patchFor(std::size_t field, double value) {
  ui::VibratoFields patch;
  const auto f = static_cast<float>(value);
  switch (field) {
    case Start: patch.startFraction = f; break;
    case FadeIn: patch.fadeInFraction = f; break;
    case FadeOut: patch.fadeOutFraction = f; break;
    case Depth: patch.depthCents = f; break;
    case Period: patch.periodMilliseconds = f; break;
    default: patch.phaseTurns = f; break;
  }
  return patch;
}

std::string fieldText(std::size_t field, double value) {
  switch (field) {
    case Depth: return format("%.0f ct", value);
    case Period: return format("%.0f ms", value);
    case Phase: return format("%.0f\u00B0", value * 360.0);
    default: return format("%.0f%%", value * 100.0);
  }
}

// Display units for accessibility: percent, cents, milliseconds and degrees.
double fieldDisplayScale(std::size_t field) {
  return field == Depth || field == Period ? 1.0 : field == Phase ? 360.0 : 100.0;
}

double snapped(double value, const FieldSpec& spec) {
  const auto stepped = std::round(value / spec.step) * spec.step;
  return std::clamp(stepped, spec.minimum, spec.maximum);
}

struct NoteTarget final {
  const domain::VocalRegion* region{nullptr};
  const domain::Note* note{nullptr};
  std::size_t selected{0U};
};

NoteTarget noteTarget(const NativeEditorController& controller) {
  NoteTarget target;
  target.region = controller.pianoRoll().project().findRegion(controller.selectedRegion());
  if (target.region == nullptr) return target;
  std::optional<domain::NoteId> only;
  for (const auto& visual : controller.pianoRoll().allNotes()) {
    if (!visual.selected) continue;
    ++target.selected;
    only = visual.noteId;
  }
  if (target.selected == 1U && only) target.note = target.region->findNote(*only);
  return target;
}

double noteMilliseconds(const domain::Project& project, const domain::VocalRegion& region,
                        const domain::Note& note) {
  constexpr double kRate = 48000.0;
  const auto& tempo = project.tempoMap();
  const auto start = tempo.sampleFrameAt(region.startTick + note.startTick, kRate);
  const auto end = tempo.sampleFrameAt(region.startTick + note.endTick(), kRate);
  return std::max(0.0, static_cast<double>(end - start) / (kRate / 1000.0));
}

// The renderer's vibrato (seam-synthesis performance_compiler): onset, fades over the active span,
// then depth * envelope * sin(2 pi (elapsed / period + phase)). Returns cents at elapsed ms.
double vibratoCents(const domain::NoteVibrato& v, double durationMs, double elapsedMs,
                    double* envelopeOut = nullptr) {
  const auto onset = durationMs * static_cast<double>(v.startFraction);
  const auto active = durationMs - onset;
  if (envelopeOut != nullptr) *envelopeOut = 0.0;
  if (elapsedMs < onset || active <= 0.0) return 0.0;
  const auto position = elapsedMs - onset;
  const auto fadeIn = active * static_cast<double>(v.fadeInFraction);
  const auto fadeOut = active * static_cast<double>(v.fadeOutFraction);
  const auto envelope = std::clamp(
      std::min(fadeIn > 0.0 ? std::min(1.0, position / fadeIn) : 1.0,
               fadeOut > 0.0 ? std::min(1.0, (active - position) / fadeOut) : 1.0),
      0.0, 1.0);
  if (envelopeOut != nullptr) *envelopeOut = envelope;
  const auto period = std::max(1.0, static_cast<double>(v.periodMilliseconds));
  const auto cycles = position / period + static_cast<double>(v.phaseTurns);
  return static_cast<double>(v.depthCents) * envelope * std::sin(2.0 * kPi * cycles);
}

// ---- Layout -------------------------------------------------------------------------------------

enum class View { Curves, Vibrato };

// One geometry for paint, hit-testing and accessibility. Below 600 points of width the graph and
// the vibrato card share the middle through two view tabs instead of squeezing side by side; the
// pitch strip appears once the curve column is tall enough to give both graphs a readable height.
struct TuneLayout final {
  bool compact{false};
  bool shortBody{false};
  ui::Rect header, caption;
  std::array<ui::Rect, kChannels> chip{};
  std::array<ui::Rect, 2U> view{};
  ui::Rect graph, graphCaption, graphGutter, graphPlot;
  ui::Rect pitch, pitchCaption, pitchPlot;
  ui::Rect vibrato, vibratoTitle, vibratoToggle, vibratoPreview, vibratoMessage;
  std::array<ui::Rect, kFields> field{};
  ui::Rect macro;
  std::array<ui::Rect, kChannels> knob{};
};

void layoutVibrato(TuneLayout& l, bool shortBody) {
  const auto card = l.vibrato;
  const auto titleHeight = shortBody ? 24.0 : 32.0;
  const auto toggleWidth = std::min(56.0, std::max(0.0, card.width * 0.3));
  l.vibratoToggle = {card.right() - 10.0 - toggleWidth, card.y + (titleHeight - 20.0) * 0.5 + 1.0,
                     toggleWidth, 20.0};
  l.vibratoTitle = {card.x + 12.0, card.y + 3.0,
                    std::max(0.0, l.vibratoToggle.x - 8.0 - (card.x + 12.0)), titleHeight - 6.0};
  const ui::Rect body{card.x + 10.0, card.y + titleHeight + 2.0, std::max(0.0, card.width - 20.0),
                      std::max(0.0, card.height - titleHeight - 8.0)};
  l.vibratoMessage = body;
  constexpr double kRow = 30.0;
  if (body.height >= 48.0 + 3.0 * kRow + 6.0 && body.width >= 150.0) {
    l.vibratoPreview = {body.x, body.y, body.width, body.height - 3.0 * kRow - 6.0};
    const auto gridY = l.vibratoPreview.bottom() + 6.0;
    const auto columnWidth = (body.width - 10.0) / 2.0;
    for (std::size_t i = 0U; i < kFields; ++i)
      l.field[i] = {body.x + static_cast<double>(i % 2U) * (columnWidth + 10.0),
                    gridY + static_cast<double>(i / 2U) * kRow, columnWidth, kRow - 4.0};
  } else if (body.height >= 52.0 && body.width >= 240.0) {
    const auto rowHeight = std::min(kRow, body.height / 2.0);
    l.vibratoPreview = {body.x, body.y, body.width * 0.28, body.height};
    const auto gridX = l.vibratoPreview.right() + 8.0;
    const auto columnWidth = (body.right() - gridX - 12.0) / 3.0;
    for (std::size_t i = 0U; i < kFields; ++i)
      l.field[i] = {gridX + static_cast<double>(i % 3U) * (columnWidth + 6.0),
                    body.y + static_cast<double>(i / 3U) * rowHeight, columnWidth, rowHeight - 3.0};
  } else {
    l.vibratoPreview = body.height >= 16.0 ? body : ui::Rect{};
  }
}

TuneLayout solveTuneLayout(ui::Rect area, View view) {
  TuneLayout l;
  const auto inner = inset(area, 8.0, 8.0);
  l.shortBody = inner.height < 240.0;
  l.compact = inner.width < 600.0;
  const auto gap = l.shortBody ? 6.0 : 10.0;
  const auto headerHeight = l.shortBody ? 24.0 : 28.0;
  const auto macroHeight = l.shortBody ? 34.0 : inner.height >= 420.0 ? 58.0 : 48.0;
  l.header = {inner.x, inner.y, inner.width, headerHeight};
  if (l.compact) {
    const auto width = std::min(92.0, std::max(0.0, (inner.width - 6.0) * 0.3));
    for (std::size_t i = 0U; i < l.view.size(); ++i)
      l.view[i] = {inner.x + static_cast<double>(i) * (width + 6.0), inner.y, width, headerHeight};
    const auto x = l.view[1].right() + 12.0;
    l.caption = {x, inner.y, std::max(0.0, inner.right() - x), headerHeight};
  } else {
    // Chips first; the hint beside them only where it has room to be read.
    const auto width = std::min(104.0, (inner.width - 5.0 * 6.0) / 6.0);
    for (std::size_t i = 0U; i < kChannels; ++i)
      l.chip[i] = {inner.x + static_cast<double>(i) * (width + 6.0), inner.y, width, headerHeight};
    const auto x = l.chip.back().right() + 12.0;
    if (inner.right() - x >= 140.0) l.caption = {x, inner.y, inner.right() - x, headerHeight};
  }
  l.macro = {inner.x, inner.bottom() - macroHeight, inner.width, macroHeight};
  const auto cell = l.macro.width / static_cast<double>(kChannels);
  for (std::size_t i = 0U; i < kChannels; ++i)
    l.knob[i] = {l.macro.x + static_cast<double>(i) * cell + 2.0, l.macro.y + 3.0,
                 std::max(0.0, cell - 4.0), std::max(0.0, macroHeight - 6.0)};
  const auto middleTop = l.header.bottom() + gap;
  const ui::Rect middle{inner.x, middleTop, inner.width, std::max(0.0, l.macro.y - gap - middleTop)};
  ui::Rect curves{};
  if (!l.compact) {
    const auto cardWidth = std::clamp(inner.width * 0.3, 236.0, 320.0);
    l.vibrato = {middle.right() - cardWidth, middle.y, cardWidth, middle.height};
    curves = {middle.x, middle.y, std::max(0.0, middle.width - cardWidth - gap), middle.height};
  } else if (view == View::Vibrato) {
    l.vibrato = middle;
  } else {
    curves = middle;
  }
  if (usable(curves)) {
    const auto pitchHeight =
        curves.height >= 220.0 ? std::clamp(curves.height * 0.3, 70.0, 170.0) : 0.0;
    l.graph = {curves.x, curves.y, curves.width,
               curves.height - (pitchHeight > 0.0 ? pitchHeight + gap : 0.0)};
    const auto captionHeight = l.shortBody ? 18.0 : 22.0;
    l.graphCaption = {l.graph.x + 12.0, l.graph.y + 3.0, std::max(0.0, l.graph.width - 24.0),
                      captionHeight - 4.0};
    l.graphPlot = {l.graph.x + 40.0, l.graph.y + captionHeight + 2.0,
                   std::max(0.0, l.graph.width - 48.0),
                   std::max(0.0, l.graph.height - captionHeight - 8.0)};
    l.graphGutter = {l.graph.x + 4.0, l.graphPlot.y, 32.0, l.graphPlot.height};
    if (pitchHeight > 0.0) {
      l.pitch = {curves.x, l.graph.bottom() + gap, curves.width, pitchHeight};
      l.pitchCaption = {l.pitch.x + 12.0, l.pitch.y + 3.0, std::max(0.0, l.pitch.width - 24.0),
                        18.0};
      l.pitchPlot = {l.pitch.x + 40.0, l.pitch.y + 24.0, std::max(0.0, l.pitch.width - 48.0),
                     std::max(0.0, l.pitch.height - 30.0)};
    }
  }
  if (usable(l.vibrato)) layoutVibrato(l, l.shortBody);
  return l;
}

// The region's own span mapped onto a plot: region-local ticks left to right.
struct Axis final {
  ui::Rect plot;
  time::Tick start{0};
  time::Tick duration{0};

  [[nodiscard]] bool valid() const noexcept { return usable(plot) && duration.value() > 0; }
  [[nodiscard]] double x(time::Tick local) const noexcept {
    return plot.x + static_cast<double>(local.value()) / static_cast<double>(duration.value()) *
                        plot.width;
  }
  [[nodiscard]] time::Tick songTick(double px) const noexcept {
    const auto fraction = std::clamp((px - plot.x) / plot.width, 0.0, 1.0);
    return start + time::Tick{static_cast<std::int64_t>(
                       std::llround(fraction * static_cast<double>(duration.value())))};
  }
};

double valueY(const ui::Rect& plot, const ui::ExpressionChannelDescriptor& d, double value) {
  const auto span = std::max(1e-6, static_cast<double>(d.maximum - d.minimum));
  const auto clamped = std::clamp(value, static_cast<double>(d.minimum), static_cast<double>(d.maximum));
  const auto usableHeight = std::max(0.0, plot.height - 2.0 * kPlotMargin);
  return plot.bottom() - kPlotMargin - (clamped - d.minimum) / span * usableHeight;
}

float valueAtY(const ui::Rect& plot, const ui::ExpressionChannelDescriptor& d, double y) {
  const auto usableHeight = std::max(1e-6, plot.height - 2.0 * kPlotMargin);
  const auto fraction = std::clamp((plot.bottom() - kPlotMargin - y) / usableHeight, 0.0, 1.0);
  return static_cast<float>(d.minimum + fraction * (d.maximum - d.minimum));
}

// ---- The workspace ------------------------------------------------------------------------------

class TuneWorkspace final : public ShellWorkspace {
public:
  [[nodiscard]] std::string_view idPrefix() const noexcept override { return "shell.tune."; }

  void paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController& controller,
             const EditorSceneState& state, ui::Rect area) const override {
    const auto l = solveTuneLayout(area, view_);
    const auto knobs = knobModels(state);
    // The body covers SING's editor and lane cards completely, as EXPORT does.
    c.fill(Path::roundedRect(area, t.shape.card), t.color.canvas);
    glassPanel(c, t, area, t.shape.card);
    paintHeader(c, t, state, l);
    if (usable(l.graph)) paintGraph(c, t, controller, state, l, knobs);
    if (usable(l.pitch)) paintPitch(c, t, controller, state, l);
    if (usable(l.vibrato)) paintVibrato(c, t, controller, l);
    paintMacro(c, t, state, l, knobs);
  }

  core::Result<void> pointerDown(NativeEditorController& controller, const PointerEvent& event,
                                 ui::Rect area) override {
    focusRequest_.clear();
    if (event.button != PointerButton::Left || gestureActive()) return core::success();
    const auto l = solveTuneLayout(area, view_);
    const auto p = event.position;
    for (std::size_t i = 0U; i < l.view.size(); ++i) {
      if (!l.compact || !contains(l.view[i], p)) continue;
      focusRequest_ = viewId(i);
      view_ = i == 0U ? View::Curves : View::Vibrato;
      return core::success();
    }
    for (std::size_t i = 0U; i < kChannels; ++i) {
      if (l.compact || !contains(l.chip[i], p)) continue;
      focusRequest_ = std::string{"shell.tune.channel."} + kIds[i];
      return controller.openExpressionLane(ui::expressionChannelAt(i));
    }
    for (std::size_t i = 0U; i < kChannels; ++i) {
      if (!contains(l.knob[i], p)) continue;
      focusRequest_ = std::string{"shell.tune.knob."} + kIds[i];
      if (!knobModels(controller.sceneState())[i].refusal.empty()) return core::success();
      knobDrag_ = KnobDrag{.index = i, .startY = p.y, .revision = controller.documentRevision(),
                           .region = controller.selectedRegion(),
                           .playhead = controller.playheadTick()};
      return core::success();
    }
    if (usable(l.vibrato)) {
      const auto target = noteTarget(controller);
      if (contains(l.vibratoToggle, p)) {
        focusRequest_ = "shell.tune.vibrato.enabled";
        if (target.note == nullptr) return core::success();
        ui::VibratoFields patch;
        patch.enabled = !target.note->vibrato.enabled;
        return controller.applyVibratoToSelection(patch);
      }
      for (std::size_t i = 0U; i < kFields; ++i) {
        if (!usable(l.field[i]) || !contains(l.field[i], p)) continue;
        focusRequest_ = std::string{"shell.tune.vibrato."} + kFieldIds[i];
        if (target.note == nullptr) return core::success();
        const auto source = fieldValue(target.note->vibrato, i);
        fieldDrag_ = FieldDrag{.field = i, .note = target.note->id,
                               .revision = controller.documentRevision(), .source = source,
                               .value = fieldAt(target.note->vibrato, i, l.field[i], p.x)};
        return core::success();
      }
    }
    if (usable(l.graphPlot) && contains(l.graphPlot, p)) {
      focusRequest_ = "shell.tune.graph";
      const auto active = activeChannel(controller);
      const auto axis = axisFor(controller, l.graphPlot);
      if (!active || !axis.valid()) return core::success();
      const auto descriptor = ui::describeExpressionChannel(ui::expressionChannelAt(*active));
      const auto state = controller.sceneState();
      std::optional<time::Tick> grab;
      auto best = kPointGrabRadius * kPointGrabRadius;
      for (const auto& point : state.expression.points) {
        const auto dx = axis.x(point.tick) - p.x;
        const auto dy = valueY(l.graphPlot, descriptor, point.amount) - p.y;
        if (dx * dx + dy * dy > best) continue;
        best = dx * dx + dy * dy;
        grab = point.tick;
      }
      const auto erase = event.modifiers.shift && grab.has_value();
      auto result = controller.pressExpressionPoint(grab, axis.songTick(p.x),
                                                    valueAtY(l.graphPlot, descriptor, p.y), erase);
      graphDrag_ = result && controller.pointerGestureActive();
      return result;
    }
    if (usable(l.pitchPlot) && contains(l.pitchPlot, p)) focusRequest_ = "shell.tune.pitch";
    return core::success();
  }

  core::Result<void> pointerMove(NativeEditorController& controller, const PointerEvent& event,
                                 ui::Rect area) override {
    const auto p = event.position;
    if (knobDrag_) {
      knobDrag_->steps = static_cast<int>(std::lround((knobDrag_->startY - p.y) / kKnobStepPixels));
      return core::success();
    }
    if (fieldDrag_) {
      const auto l = solveTuneLayout(area, view_);
      const auto target = noteTarget(controller);
      if (target.note != nullptr && usable(l.field[fieldDrag_->field]))
        fieldDrag_->value = fieldAt(target.note->vibrato, fieldDrag_->field,
                                    l.field[fieldDrag_->field], p.x);
      return core::success();
    }
    if (graphDrag_) {
      const auto l = solveTuneLayout(area, view_);
      const auto active = activeChannel(controller);
      const auto axis = axisFor(controller, l.graphPlot);
      if (!active || !axis.valid()) return core::success();
      const auto descriptor = ui::describeExpressionChannel(ui::expressionChannelAt(*active));
      return controller.dragExpressionPoint(axis.songTick(p.x),
                                            valueAtY(l.graphPlot, descriptor, p.y));
    }
    return core::success();
  }

  core::Result<void> pointerUp(NativeEditorController& controller, const PointerEvent& event,
                               ui::Rect area) override {
    static_cast<void>(event);
    static_cast<void>(area);
    if (knobDrag_) {
      const auto drag = *knobDrag_;
      knobDrag_.reset();
      // The release commits only against the document, region and playhead the gesture began on.
      if (controller.documentRevision() != drag.revision ||
          controller.selectedRegion() != drag.region || controller.playheadTick() != drag.playhead)
        return core::success();
      if (drag.steps != 0) return nudge(controller, drag.index, drag.steps);
      return controller.openExpressionLane(ui::expressionChannelAt(drag.index));
    }
    if (fieldDrag_) {
      const auto drag = *fieldDrag_;
      fieldDrag_.reset();
      const auto target = noteTarget(controller);
      if (controller.documentRevision() != drag.revision || target.note == nullptr ||
          target.note->id != drag.note || std::abs(drag.value - drag.source) < 1e-6)
        return core::success();
      return controller.applyVibratoToSelection(patchFor(drag.field, drag.value));
    }
    if (graphDrag_) {
      graphDrag_ = false;
      return controller.releaseExpressionPoint();
    }
    return core::success();
  }

  bool scroll(NativeEditorController& controller, ui::Point anchor, double deltaX, double deltaY,
              ui::Rect area) override {
    static_cast<void>(deltaX);
    const auto l = solveTuneLayout(area, view_);
    for (std::size_t i = 0U; i < kChannels; ++i) {
      if (!contains(l.knob[i], anchor)) continue;
      if (gestureActive() || !knobModels(controller.sceneState())[i].refusal.empty()) return true;
      scrollAccumulator_ += deltaY;
      const auto steps = static_cast<int>(scrollAccumulator_ / kScrollStep);
      if (steps != 0) {
        scrollAccumulator_ -= steps * kScrollStep;
        static_cast<void>(nudge(controller, i, -steps));
      }
      return true;
    }
    return false;
  }

  void semantics(const NativeEditorController& controller, const EditorSceneState& state,
                 ui::Rect area, std::vector<SemanticNode>& out) const override;

  core::Result<void> perform(NativeEditorController& controller, std::string_view id,
                             SemanticAction action) override;

  void cancelGestures(NativeEditorController& controller) override {
    knobDrag_.reset();
    fieldDrag_.reset();
    scrollAccumulator_ = 0.0;
    if (graphDrag_) {
      graphDrag_ = false;
      controller.cancelPointerGesture();
    }
  }

  [[nodiscard]] bool gestureActive() const noexcept override {
    return knobDrag_.has_value() || fieldDrag_.has_value() || graphDrag_;
  }

  [[nodiscard]] std::string takeFocusRequest() override {
    auto request = std::move(focusRequest_);
    focusRequest_.clear();
    return request;
  }

private:
  struct KnobDrag final {
    std::size_t index{0U};
    double startY{0.0};
    int steps{0};
    std::uint64_t revision{0U};
    domain::RegionId region{};
    time::Tick playhead{0};
  };
  struct FieldDrag final {
    std::size_t field{0U};
    domain::NoteId note{};
    std::uint64_t revision{0U};
    double source{0.0};
    double value{0.0};
  };

  static std::string viewId(std::size_t i) {
    return i == 0U ? "shell.tune.view.curves" : "shell.tune.view.vibrato";
  }

  static Axis axisFor(const NativeEditorController& controller, ui::Rect plot) {
    const auto* region = controller.pianoRoll().project().findRegion(controller.selectedRegion());
    if (region == nullptr) return Axis{plot};
    return Axis{plot, region->startTick, region->durationTick};
  }

  static double fieldAt(const domain::NoteVibrato& v, std::size_t field, ui::Rect cell, double x) {
    const auto spec = fieldSpec(v, field);
    const auto fraction = cell.width > 0.0 ? std::clamp((x - cell.x) / cell.width, 0.0, 1.0) : 0.0;
    // The range of every field is drawn over the full track, whatever the shared-fade bound.
    const auto full = fieldSpec(domain::NoteVibrato{}, field);
    const auto maximum = field == FadeIn || field == FadeOut ? 1.0 : full.maximum;
    return snapped(full.minimum + fraction * (maximum - full.minimum), spec);
  }

  // The value a control shows: the stored one, or the one a gesture in progress would commit.
  [[nodiscard]] domain::NoteVibrato shownVibrato(const domain::Note& note) const {
    auto shown = note.vibrato;
    if (fieldDrag_ && fieldDrag_->note == note.id) setField(shown, fieldDrag_->field, fieldDrag_->value);
    return shown;
  }

  void paintHeader(Canvas2D& c, const DesignTokens& t, const EditorSceneState& state,
                   const TuneLayout& l) const;
  void paintGraph(Canvas2D& c, const DesignTokens& t, const NativeEditorController& controller,
                  const EditorSceneState& state, const TuneLayout& l,
                  const std::array<KnobModel, kChannels>& knobs) const;
  void paintPitch(Canvas2D& c, const DesignTokens& t, const NativeEditorController& controller,
                  const EditorSceneState& state, const TuneLayout& l) const;
  void paintVibrato(Canvas2D& c, const DesignTokens& t, const NativeEditorController& controller,
                    const TuneLayout& l) const;
  void paintMacro(Canvas2D& c, const DesignTokens& t, const EditorSceneState& state,
                  const TuneLayout& l, const std::array<KnobModel, kChannels>& knobs) const;

  View view_{View::Curves};
  std::optional<KnobDrag> knobDrag_;
  std::optional<FieldDrag> fieldDrag_;
  bool graphDrag_{false};
  double scrollAccumulator_{0.0};
  std::string focusRequest_;
};

void tab(Canvas2D& c, const DesignTokens& t, ui::Rect r, std::string_view label, bool active,
         std::optional<Color> swatch) {
  if (!usable(r)) return;
  if (active) {
    c.save();
    c.setGlow(withAlpha(t.color.accent, 0.7), 8.0);
    c.fill(Path::roundedRect(r, 6.0), withAlpha(t.color.accent, 0.20));
    c.restore();
    c.stroke(Path::roundedRect(r, 6.0), t.color.accent, StrokeStyle{1.0});
  } else {
    c.stroke(Path::roundedRect(r, 6.0), withAlpha(t.color.border, 0.9), StrokeStyle{1.0});
  }
  auto text = r;
  if (swatch && r.width >= 44.0) {
    c.fill(Path::capsule({r.x + 7.0, r.y + r.height * 0.5 - 1.5, 9.0, 3.0}), *swatch);
    text = {r.x + 16.0, r.y, r.width - 20.0, r.height};
  } else {
    text = inset(r, 4.0, 0.0);
  }
  c.text(text, label,
         fitted(c, label, style(FontRole::UiSemibold, t.type.smallLabel, 1.0, TextAlign::Center, true),
                text.width),
         active ? t.color.accent : t.color.textSecondary);
}

void TuneWorkspace::paintHeader(Canvas2D& c, const DesignTokens& t, const EditorSceneState& state,
                                const TuneLayout& l) const {
  const auto active = activeChannel(state);
  if (l.compact) {
    tab(c, t, l.view[0], "Curves", view_ == View::Curves, std::nullopt);
    tab(c, t, l.view[1], "Vibrato", view_ == View::Vibrato, std::nullopt);
  } else {
    for (std::size_t i = 0U; i < kChannels; ++i)
      tab(c, t, l.chip[i], kLabels[i], active == i, channelColor(t, i));
  }
  std::string caption = active ? "Drag to draw \u2022 Shift-click removes a point"
                               : "Choose a channel to edit its curve";
  if (l.compact) caption = active ? std::string{kLabels[*active]} + " selected" : "Pick a knob";
  c.text(l.caption, caption, style(FontRole::Ui, t.type.smallLabel, 0.0, TextAlign::Right),
         t.color.textSecondary);
}

void TuneWorkspace::paintGraph(Canvas2D& c, const DesignTokens& t,
                               const NativeEditorController& controller,
                               const EditorSceneState& state, const TuneLayout& l,
                               const std::array<KnobModel, kChannels>& knobs) const {
  glassPanel(c, t, l.graph, t.shape.card);
  const auto active = activeChannel(state);
  const auto plot = l.graphPlot;
  sunken(c, t, plot, 6.0);
  const auto* region = controller.pianoRoll().project().findRegion(controller.selectedRegion());
  // Caption: the channel being edited, its value at the playhead and whether it can be rendered.
  std::string caption = "Expression";
  auto captionColor = t.color.textSecondary;
  if (region == nullptr) {
    caption = "Select a region to shape its expression";
  } else if (active) {
    const auto& k = knobs[*active];
    caption = std::string{kLabels[*active]} + "  " +
              valueText(k.descriptor, state.expression.valueAtPlayhead) + " at playhead  \u2022  " +
              std::to_string(state.expression.points.size()) + " points";
    if (state.expression.draftChanged) caption += "  \u2022  editing";
    if (!state.expression.refusal.empty()) {
      caption = std::string{kLabels[*active]} + ": " + state.expression.refusal;
      captionColor = t.color.warning;
    }
  } else {
    caption = "All channels  \u2022  choose one to edit";
  }
  c.text(l.graphCaption, caption, style(FontRole::UiMedium, t.type.smallLabel, 0.4), captionColor);
  if (region == nullptr || !usable(plot)) return;
  const Axis axis{plot, region->startTick, region->durationTick};
  if (!axis.valid()) return;

  // Value grid, then the musical grid: beats and numbered bars in song time, as on the SING ruler.
  c.save();
  c.clipRect(plot);
  for (const double f : {0.25, 0.5, 0.75}) {
    Path row;
    const auto y = plot.bottom() - kPlotMargin - f * (plot.height - 2.0 * kPlotMargin);
    row.moveTo({plot.x, y}).lineTo({plot.right(), y});
    c.stroke(row, t.color.gridWeak, StrokeStyle{0.6});
  }
  const auto quarter = controller.pianoRoll().timeline().ppq();
  const auto quartersPerBar = std::max<std::int64_t>(
      1, static_cast<std::int64_t>(state.meter.numerator) * 4 /
             std::max<std::int64_t>(1, state.meter.denominator));
  if (quarter > 0) {
    const auto beatWidth = static_cast<double>(quarter) / static_cast<double>(axis.duration.value()) *
                           plot.width;
    const auto barWidth = beatWidth * static_cast<double>(quartersPerBar);
    const auto end = axis.start + axis.duration;
    auto index = (axis.start.value() + quarter - 1) / quarter;
    for (auto tick = time::Tick{index * quarter}; tick <= end; tick += time::Tick{quarter}, ++index) {
      const auto bar = index % quartersPerBar == 0;
      if (!bar && beatWidth < 6.0) continue;
      const auto x = axis.x(tick - axis.start);
      Path v;
      v.moveTo({x, plot.y}).lineTo({x, plot.bottom()});
      c.stroke(v, bar ? t.color.gridBar : t.color.gridWeak, StrokeStyle{bar ? 1.0 : 0.6});
      if (bar && barWidth >= 26.0 && x + 5.0 < plot.right() - 16.0)
        c.text({x + 4.0, plot.y + 2.0, std::min(40.0, plot.right() - x - 6.0), 12.0},
               std::to_string(index / quartersPerBar + 1),
               style(FontRole::Mono, t.type.rulerMicro), t.color.textSecondary);
    }
  }
  c.restore();

  // Scale of the channel being edited.
  if (active && usable(l.graphGutter)) {
    const auto& d = knobs[*active].descriptor;
    const auto scale = semitones(d) ? 1.0 : 100.0;
    const auto gutterStyle = style(FontRole::Mono, t.type.rulerMicro, 0.0, TextAlign::Right);
    c.text({l.graphGutter.x, plot.y, l.graphGutter.width, 14.0}, format("%.0f", d.maximum * scale),
           gutterStyle, t.color.textSecondary);
    if (plot.height >= 44.0)
      c.text({l.graphGutter.x, plot.bottom() - 14.0, l.graphGutter.width, 14.0},
             format("%.0f", d.minimum * scale), gutterStyle, t.color.textSecondary);
  }

  // Every stored curve across the region; the edited one last, lit and with its points.
  c.save();
  c.clipRect(plot);
  const auto draw = [&](std::size_t i, bool lit) {
    const auto channel = ui::expressionChannelAt(i);
    const auto d = ui::describeExpressionChannel(channel);
    const auto points = lit ? state.expression.points : ui::readExpressionPoints(*region, channel);
    const auto color = channelColor(t, i);
    if (points.empty()) {
      // A channel without points sits at its neutral value; it is still drawn, so the graph
      // always shows all six channels.
      Path neutral;
      neutral.moveTo({plot.x, valueY(plot, d, d.neutral)})
          .lineTo({plot.right(), valueY(plot, d, d.neutral)});
      c.stroke(neutral, withAlpha(color, lit ? 0.75 : 0.45),
               StrokeStyle{lit ? 1.2 : 1.0, true, {4.0, 4.0}});
      return;
    }
    Path curve;
    curve.moveTo({plot.x, valueY(plot, d, points.front().amount)});
    for (const auto& point : points) curve.lineTo({axis.x(point.tick), valueY(plot, d, point.amount)});
    curve.lineTo({plot.right(), valueY(plot, d, points.back().amount)});
    if (!lit) {
      c.stroke(curve, withAlpha(color, 0.62), StrokeStyle{1.4});
      return;
    }
    Path area = curve;
    area.lineTo({plot.right(), plot.bottom()}).lineTo({plot.x, plot.bottom()}).close();
    c.fill(area, LinearGradient{{0.0, plot.y}, {0.0, plot.bottom()},
                                {{0.0, withAlpha(color, 0.30)}, {1.0, withAlpha(color, 0.02)}}});
    c.save();
    c.setGlow(withAlpha(color, 0.9), 8.0);
    c.stroke(curve, color, StrokeStyle{2.2});
    c.restore();
    for (const auto& point : points) {
      const ui::Point p{axis.x(point.tick), valueY(plot, d, point.amount)};
      c.fill(Path::circle(p, 3.6), t.color.textPrimary);
      c.stroke(Path::circle(p, 3.6), color, StrokeStyle{1.2});
    }
  };
  for (std::size_t i = 0U; i < kChannels; ++i)
    if (i != active) draw(i, false);
  if (active) draw(*active, true);
  if (state.playheadInsideRegion) {
    const auto local = controller.playheadTick() - region->startTick;
    const auto x = axis.x(local);
    Path head;
    head.moveTo({x, plot.y}).lineTo({x, plot.bottom()});
    c.stroke(head, withAlpha(t.color.accentTime, 0.7), StrokeStyle{1.0});
  }
  c.restore();
}

void TuneWorkspace::paintPitch(Canvas2D& c, const DesignTokens& t,
                               const NativeEditorController& controller,
                               const EditorSceneState& state, const TuneLayout& l) const {
  glassPanel(c, t, l.pitch, t.shape.card);
  const auto* region = controller.pianoRoll().project().findRegion(controller.selectedRegion());
  const auto count = state.pitchAutomation.size();
  c.text(l.pitchCaption,
         "Pitch  \u2022  " + std::to_string(count) +
             (count == 1U ? " point" : " points") + "  \u2022  read-only here; edit points in SING",
         style(FontRole::UiMedium, t.type.smallLabel, 0.4), t.color.textSecondary);
  const auto plot = l.pitchPlot;
  sunken(c, t, plot, 6.0);
  if (region == nullptr || !usable(plot)) return;
  const Axis axis{plot, region->startTick, region->durationTick};
  if (!axis.valid()) return;
  double range = 100.0;
  for (const auto& point : state.pitchAutomation)
    range = std::max(range, std::abs(static_cast<double>(point.cents)) * 1.15);
  const auto centerY = plot.y + plot.height * 0.5;
  const auto half = std::max(0.0, plot.height * 0.5 - kPlotMargin);
  const auto yFor = [&](double cents) { return centerY - std::clamp(cents / range, -1.0, 1.0) * half; };
  c.save();
  c.clipRect(plot);
  Path zero;
  zero.moveTo({plot.x, centerY}).lineTo({plot.right(), centerY});
  c.stroke(zero, withAlpha(t.color.textSecondary, 0.35), StrokeStyle{1.0, true, {3.0, 4.0}});
  // Where the notes sit, so the curve reads against the phrase; the selected note is lit.
  const auto target = noteTarget(controller);
  for (const auto& note : region->notes) {
    const auto x0 = axis.x(note.startTick);
    const auto x1 = axis.x(note.endTick());
    const auto selected = target.note != nullptr && target.note->id == note.id;
    c.fill(Path::capsule({x0 + 1.0, plot.bottom() - 7.0, std::max(2.0, x1 - x0 - 2.0), 3.0}),
           selected ? t.color.accent : withAlpha(t.color.noteStroke, 0.55));
  }
  if (count > 0U) {
    Path curve;
    const auto step = 2.0;
    for (double x = plot.x; x <= plot.right(); x += step) {
      const auto local = axis.songTick(x) - axis.start;
      const ui::Point p{x, yFor(region->pitchAutomation.valueAt(local))};
      if (x == plot.x) curve.moveTo(p);
      else curve.lineTo(p);
    }
    c.save();
    c.setGlow(withAlpha(t.color.pitchGlow, 0.8), 6.0);
    c.stroke(curve, t.color.pitchCurve, StrokeStyle{1.8});
    c.restore();
    for (const auto& point : state.pitchAutomation) {
      const ui::Point p{axis.x(point.tick), yFor(point.cents)};
      c.fill(Path::circle(p, 3.2), t.color.textPrimary);
      c.stroke(Path::circle(p, 3.2), t.color.pitchCurve, StrokeStyle{1.0});
    }
  } else if (plot.height >= 30.0) {
    c.text(inset(plot, 10.0, 0.0), "No pitch points stored",
           style(FontRole::Ui, t.type.smallLabel), t.color.textSecondary);
  }
  c.restore();
}

void TuneWorkspace::paintVibrato(Canvas2D& c, const DesignTokens& t,
                                 const NativeEditorController& controller,
                                 const TuneLayout& l) const {
  glassPanel(c, t, l.vibrato, t.shape.card);
  const auto target = noteTarget(controller);
  cardTitle(c, t, l.vibratoTitle, "Note \u2022 vibrato",
            target.note != nullptr && target.note->vibrato.enabled);
  if (target.note == nullptr) {
    const auto message = target.selected == 0U
                             ? std::string{"Select one note in SING to shape its vibrato"}
                             : std::to_string(target.selected) +
                                   " notes are selected; select one to shape its vibrato";
    c.text({l.vibratoMessage.x, l.vibratoMessage.y, l.vibratoMessage.width,
            std::min(20.0, l.vibratoMessage.height)},
           message, style(FontRole::Ui, t.type.smallLabel), t.color.textSecondary);
    return;
  }
  const auto& note = *target.note;
  const auto v = shownVibrato(note);
  // Enable switch.
  if (usable(l.vibratoToggle)) {
    const auto on = v.enabled;
    c.fill(Path::capsule(l.vibratoToggle), withAlpha(on ? t.color.accent : t.color.surfaceSunken, on ? 0.22 : 1.0));
    c.stroke(Path::capsule(l.vibratoToggle), on ? t.color.accent : t.color.border, StrokeStyle{1.0});
    c.text(l.vibratoToggle, on ? "On" : "Off",
           style(FontRole::UiSemibold, t.type.smallLabel, 1.0, TextAlign::Center, true),
           on ? t.color.accent : t.color.textSecondary);
  }
  // Live preview: the renderer's own vibrato formula over this note's duration.
  const auto durationMs = noteMilliseconds(controller.pianoRoll().project(), *target.region, note);
  if (usable(l.vibratoPreview)) {
    const auto r = l.vibratoPreview;
    sunken(c, t, r, 6.0);
    const auto plot = inset(r, 6.0, 5.0);
    const auto centerY = plot.y + plot.height * 0.5;
    const auto scale = std::max(50.0, static_cast<double>(v.depthCents)) * 1.1;
    const auto yFor = [&](double cents) { return centerY - cents / scale * plot.height * 0.5; };
    const auto color = v.enabled ? t.color.pitchCurve : t.color.textDisabled;
    c.save();
    c.clipRect(r);
    Path mid;
    mid.moveTo({plot.x, centerY}).lineTo({plot.right(), centerY});
    c.stroke(mid, withAlpha(t.color.textSecondary, 0.3), StrokeStyle{1.0, true, {3.0, 4.0}});
    const auto onsetX = plot.x + plot.width * static_cast<double>(v.startFraction);
    Path onset;
    onset.moveTo({onsetX, plot.y}).lineTo({onsetX, plot.bottom()});
    c.stroke(onset, withAlpha(t.color.accent, 0.5), StrokeStyle{1.0, true, {2.0, 3.0}});
    if (durationMs > 0.0 && plot.width > 2.0) {
      Path wave;
      Path upper;
      Path lower;
      const auto samples = static_cast<int>(std::max(2.0, plot.width));
      for (int i = 0; i <= samples; ++i) {
        const auto fraction = static_cast<double>(i) / samples;
        double envelope = 0.0;
        const auto cents = vibratoCents(v, durationMs, fraction * durationMs, &envelope);
        const auto x = plot.x + fraction * plot.width;
        const ui::Point p{x, yFor(cents)};
        const ui::Point up{x, yFor(static_cast<double>(v.depthCents) * envelope)};
        const ui::Point down{x, yFor(-static_cast<double>(v.depthCents) * envelope)};
        if (i == 0) {
          wave.moveTo(p);
          upper.moveTo(up);
          lower.moveTo(down);
        } else {
          wave.lineTo(p);
          upper.lineTo(up);
          lower.lineTo(down);
        }
      }
      c.stroke(upper, withAlpha(color, 0.35), StrokeStyle{1.0, true, {2.0, 3.0}});
      c.stroke(lower, withAlpha(color, 0.35), StrokeStyle{1.0, true, {2.0, 3.0}});
      c.save();
      if (v.enabled) c.setGlow(withAlpha(t.color.pitchGlow, 0.8), 6.0);
      c.stroke(wave, color, StrokeStyle{1.8});
      c.restore();
    }
    if (plot.height >= 34.0 && plot.width >= 140.0)
      c.text({plot.x + 2.0, plot.y, plot.width - 4.0, 13.0},
             v.enabled ? format("%.0f ms note", durationMs) : "Off \u2022 preview of the stored shape",
             style(FontRole::Ui, t.type.rulerMicro), t.color.textSecondary);
    c.restore();
  }
  // Parameters.
  for (std::size_t i = 0U; i < kFields; ++i) {
    const auto cell = l.field[i];
    if (!usable(cell)) continue;
    const auto value = fieldValue(v, i);
    const auto full = fieldSpec(domain::NoteVibrato{}, i);
    const auto maximum = i == FadeIn || i == FadeOut ? 1.0 : full.maximum;
    const auto fraction =
        std::clamp((value - full.minimum) / std::max(1e-6, maximum - full.minimum), 0.0, 1.0);
    const auto dragging = fieldDrag_ && fieldDrag_->field == i;
    // The value is short and exact, so it keeps its width; the label takes what is left.
    const auto valueString = fieldText(i, value);
    const auto valueStyle = style(FontRole::UiMedium, t.type.smallLabel, 0.0, TextAlign::Right);
    const auto valueWidth = std::min(cell.width * 0.6, c.measure(valueString, valueStyle) + 2.0);
    const ui::Rect labelRect{cell.x, cell.y, std::max(0.0, cell.width - valueWidth - 4.0), 13.0};
    const ui::Rect valueRect{cell.right() - valueWidth, cell.y, valueWidth, 13.0};
    c.text(labelRect, kFieldLabels[i],
           fitted(c, kFieldLabels[i], style(FontRole::UiSemibold, t.type.smallLabel, 0.6, TextAlign::Left, true),
                  labelRect.width),
           dragging ? t.color.accent : t.color.textSecondary);
    c.text(valueRect, valueString, valueStyle, t.color.textPrimary);
    const auto trackY = std::min(cell.bottom() - 4.0, cell.y + 18.0);
    const ui::Rect track{cell.x, trackY - 1.5, cell.width, 3.0};
    c.fill(Path::capsule(track), t.color.knobTrack);
    const ui::Rect lit{track.x, track.y, track.width * fraction, track.height};
    if (lit.width > 0.5) c.fill(Path::capsule(lit), v.enabled ? t.color.accent : t.color.textDisabled);
    const ui::Point thumb{std::clamp(track.x + track.width * fraction, track.x + 4.0, track.right() - 4.0),
                          trackY};
    c.fill(Path::circle(thumb, 4.0), t.color.knobPointer);
  }
}

void TuneWorkspace::paintMacro(Canvas2D& c, const DesignTokens& t, const EditorSceneState& state,
                               const TuneLayout& l,
                               const std::array<KnobModel, kChannels>& knobs) const {
  glassPanel(c, t, l.macro, t.shape.control * 1.5);
  const auto active = activeChannel(state);
  for (std::size_t i = 0U; i < kChannels; ++i) {
    const auto cell = l.knob[i];
    if (!usable(cell)) continue;
    const auto& k = knobs[i];
    const auto refused = !k.refusal.empty();
    const auto color = channelColor(t, i);
    if (active == i) {
      c.fill(Path::roundedRect(cell, 6.0), withAlpha(color, 0.14));
      c.stroke(Path::roundedRect(cell, 6.0), withAlpha(color, 0.8), StrokeStyle{1.0});
    }
    auto value = k.value;
    if (knobDrag_ && knobDrag_->index == i)
      value = std::clamp(value + knobDrag_->steps * static_cast<double>(k.descriptor.step),
                         static_cast<double>(k.descriptor.minimum),
                         static_cast<double>(k.descriptor.maximum));
    const auto wide = cell.width >= 120.0 && cell.height >= 40.0;
    const auto radius = wide ? std::min(16.0, (cell.height - 10.0) * 0.5)
                             : std::clamp((cell.height - 19.0) * 0.5, 6.0, 10.0);
    ui::Point center{};
    ui::Rect labelRect{};
    ui::Rect valueRect{};
    if (wide) {
      center = {cell.x + 8.0 + radius, cell.y + cell.height * 0.5};
      const auto x = center.x + radius + 10.0;
      labelRect = {x, center.y - 15.0, std::max(0.0, cell.right() - 6.0 - x), 14.0};
      valueRect = {x, center.y + 1.0, std::max(0.0, cell.right() - 6.0 - x), 15.0};
    } else {
      labelRect = {cell.x + 3.0, cell.y + 2.0, std::max(0.0, cell.width - 6.0), 13.0};
      const auto rowTop = labelRect.bottom() + 1.0;
      center = {cell.x + 5.0 + radius, rowTop + (cell.bottom() - rowTop) * 0.5};
      const auto x = center.x + radius + 5.0;
      valueRect = {x, center.y - 7.0, std::max(0.0, cell.right() - 3.0 - x), 14.0};
    }
    c.text(labelRect, kLabels[i],
           fitted(c, kLabels[i],
                  style(FontRole::UiSemibold, t.type.smallLabel, 1.0,
                        wide ? TextAlign::Left : TextAlign::Center, true),
                  labelRect.width),
           active == i ? color : t.color.textSecondary);
    constexpr auto start = 0.75 * kPi;
    constexpr auto sweep = 1.5 * kPi;
    Path trackArc;
    trackArc.arc(center, radius, start, sweep);
    c.stroke(trackArc, t.color.knobTrack, StrokeStyle{2.5});
    const auto span = std::max(1e-6, static_cast<double>(k.descriptor.maximum - k.descriptor.minimum));
    const auto fraction = std::clamp((value - k.descriptor.minimum) / span, 0.0, 1.0);
    const auto origin = k.descriptor.bipolar
                            ? std::clamp((k.descriptor.neutral - k.descriptor.minimum) / span, 0.0, 1.0)
                            : 0.0;
    if (!refused && std::abs(fraction - origin) > 1e-4) {
      Path arc;
      arc.arc(center, radius, start + sweep * origin, sweep * (fraction - origin));
      c.save();
      c.setGlow(withAlpha(color, 0.9), 5.0);
      c.stroke(arc, color, StrokeStyle{2.5});
      c.restore();
    }
    c.save();
    c.setAlpha(refused ? 0.4 : 1.0);
    c.fill(Path::circle(center, std::max(2.0, radius - 4.0)),
           RadialGradient{{center.x - 3.0, center.y - 4.0}, radius,
                          {{0.0, t.color.knobBodyInner}, {1.0, t.color.knobBodyOuter}}});
    const auto angle = start + sweep * fraction;
    Path pointer;
    pointer.moveTo({center.x + std::cos(angle) * (radius * 0.35), center.y + std::sin(angle) * (radius * 0.35)})
        .lineTo({center.x + std::cos(angle) * (radius - 3.0), center.y + std::sin(angle) * (radius - 3.0)});
    c.stroke(pointer, t.color.knobPointer, StrokeStyle{1.6});
    c.restore();
    auto text = refused ? std::string{"\u2014"} : valueText(k.descriptor, value);
    const auto valueStyle = style(FontRole::UiMedium, t.type.label, 0.0, TextAlign::Left);
    // A narrow cell drops the semitone unit before it would elide the number.
    if (!refused && semitones(k.descriptor) && c.measure(text, valueStyle) > valueRect.width)
      text = format("%.1f", value);
    c.text(valueRect, text,
           fitted(c, text, valueStyle, valueRect.width),
           refused ? t.color.textDisabled : t.color.textPrimary);
  }
}

void TuneWorkspace::semantics(const NativeEditorController& controller,
                              const EditorSceneState& state, ui::Rect area,
                              std::vector<SemanticNode>& out) const {
  const auto l = solveTuneLayout(area, view_);
  const auto knobs = knobModels(state);
  const auto active = activeChannel(state);
  const auto* region = controller.pianoRoll().project().findRegion(controller.selectedRegion());
  if (l.compact) {
    static constexpr std::array<const char*, 2U> kViews{"Curves", "Vibrato"};
    for (std::size_t i = 0U; i < l.view.size(); ++i)
      out.push_back(SemanticNode{
          .id = viewId(i), .role = SemanticRole::Tab, .name = std::string{kViews[i]} + " view",
          .bounds = l.view[i],
          .selected = (i == 0U) == (view_ == View::Curves),
          .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
          .description = i == 0U ? "Shows the expression and pitch graphs"
                                 : "Shows the selected note's vibrato"});
  } else {
    for (std::size_t i = 0U; i < kChannels; ++i)
      out.push_back(SemanticNode{.id = std::string{"shell.tune.channel."} + kIds[i],
                                 .role = SemanticRole::Tab,
                                 .name = std::string{kLabels[i]} + " channel",
                                 .bounds = l.chip[i],
                                 .selected = active == i,
                                 .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
                                 .description = "Selects the curve the graph edits"});
  }
  if (usable(l.graphPlot)) {
    const auto editable = active && knobs[*active].refusal.empty() && region != nullptr;
    SemanticNode graph{
        .id = "shell.tune.graph",
        .role = SemanticRole::Lane,
        .name = active ? std::string{kLabels[*active]} + " curve" : std::string{"Expression curves"},
        .value = !active ? "No channel selected"
                 : !state.expression.refusal.empty()
                     ? state.expression.refusal
                     : valueText(knobs[*active].descriptor, state.expression.valueAtPlayhead) +
                           " at the playhead, " + std::to_string(state.expression.points.size()) +
                           " points",
        .bounds = l.graphPlot,
        .enabled = region != nullptr,
        .actions = editable ? std::vector<SemanticAction>{SemanticAction::Increment,
                                                          SemanticAction::Decrement,
                                                          SemanticAction::SetFocus}
                            : std::vector<SemanticAction>{SemanticAction::SetFocus},
        .description = active ? "Click to add a point, drag to move, Shift-click removes; "
                                "Escape cancels a drag. Increment and Decrement nudge the value at "
                                "the playhead"
                              : "Choose a channel to edit its curve"};
    // Every channel's stored curve is drawn here; each is listed so none is visual-only.
    for (std::size_t i = 0U; i < kChannels; ++i) {
      const auto channel = ui::expressionChannelAt(i);
      const auto count = active == i ? state.expression.points.size()
                         : region != nullptr ? ui::readExpressionPoints(*region, channel).size()
                                             : 0U;
      graph.children.push_back(SemanticNode{
          .id = std::string{"shell.tune.curve."} + kIds[i],
          .role = SemanticRole::Lane,
          .name = std::string{kLabels[i]} + " curve",
          .value = count == 0U ? std::string{"No curve stored"}
                               : std::to_string(count) + (count == 1U ? " point" : " points"),
          .bounds = l.graphPlot,
          .selected = active == i,
          .description = knobs[i].refusal});
    }
    out.push_back(std::move(graph));
  }
  if (usable(l.pitchPlot)) {
    out.push_back(SemanticNode{
        .id = "shell.tune.pitch",
        .role = SemanticRole::Lane,
        .name = "Pitch curve",
        .value = std::to_string(state.pitchAutomation.size()) +
                 (state.pitchAutomation.size() == 1U ? " point" : " points"),
        .bounds = l.pitchPlot,
        .actions = {SemanticAction::SetFocus},
        .description = "Read-only here; pitch points are edited in the SING lane"});
  }
  if (usable(l.vibrato)) {
    const auto target = noteTarget(controller);
    std::string summary = target.selected == 0U ? "Select one note in SING to shape its vibrato"
                          : target.note == nullptr
                              ? std::to_string(target.selected) + " notes are selected; select one"
                              : "";
    if (target.note != nullptr) {
      const auto v = shownVibrato(*target.note);
      summary = std::string{v.enabled ? "On" : "Off"} + ", depth " + fieldText(Depth, v.depthCents) +
                ", period " + fieldText(Period, v.periodMilliseconds);
    }
    out.push_back(SemanticNode{.id = "shell.tune.vibrato", .role = SemanticRole::Panel,
                               .name = "Note vibrato", .value = summary, .bounds = l.vibrato,
                               .description = "The preview is computed from the stored values"});
    if (target.note != nullptr) {
      const auto v = shownVibrato(*target.note);
      out.push_back(SemanticNode{
          .id = "shell.tune.vibrato.enabled", .role = SemanticRole::Button, .name = "Vibrato",
          .value = v.enabled ? "On" : "Off", .bounds = l.vibratoToggle, .selected = v.enabled,
          .actions = {SemanticAction::Toggle, SemanticAction::Activate, SemanticAction::SetFocus}});
      for (std::size_t i = 0U; i < kFields; ++i) {
        if (!usable(l.field[i])) continue;
        const auto spec = fieldSpec(v, i);
        const auto scale = fieldDisplayScale(i);
        out.push_back(SemanticNode{
            .id = std::string{"shell.tune.vibrato."} + kFieldIds[i],
            .role = SemanticRole::Slider,
            .name = std::string{"Vibrato "} + kFieldLabels[i],
            .value = fieldText(i, fieldValue(v, i)),
            .bounds = l.field[i],
            .actions = {SemanticAction::Increment, SemanticAction::Decrement,
                        SemanticAction::SetFocus},
            .numericValue = fieldValue(v, i) * scale,
            .numericMinimum = spec.minimum * scale,
            .numericMaximum = spec.maximum * scale,
            .numericStep = spec.step * scale});
      }
    }
  }
  for (std::size_t i = 0U; i < kChannels; ++i) {
    const auto& k = knobs[i];
    const auto scale = semitones(k.descriptor) ? 1.0 : 100.0;
    const auto refused = !k.refusal.empty();
    out.push_back(SemanticNode{
        .id = std::string{"shell.tune.knob."} + kIds[i],
        .role = SemanticRole::Slider,
        .name = kLabels[i],
        .value = refused ? k.refusal : valueText(k.descriptor, k.value),
        .bounds = l.knob[i],
        .enabled = !refused,
        .selected = active == i,
        .actions = refused ? std::vector<SemanticAction>{SemanticAction::SetFocus}
                           : std::vector<SemanticAction>{SemanticAction::Increment,
                                                         SemanticAction::Decrement,
                                                         SemanticAction::Activate,
                                                         SemanticAction::SetFocus},
        .description = refused ? k.refusal
                               : "Value at the playhead; Activate edits its curve in the graph",
        .numericValue = k.value * scale,
        .numericMinimum = static_cast<double>(k.descriptor.minimum) * scale,
        .numericMaximum = static_cast<double>(k.descriptor.maximum) * scale,
        .numericStep = static_cast<double>(k.descriptor.step) * scale});
  }
}

core::Result<void> TuneWorkspace::perform(NativeEditorController& controller, std::string_view id,
                                          SemanticAction action) {
  const auto unsupported = [] {
    return core::failure(core::ErrorCode::Unsupported, "This element does not support that action");
  };
  const auto step = action == SemanticAction::Increment   ? 1
                    : action == SemanticAction::Decrement ? -1
                                                          : 0;
  const auto activate = action == SemanticAction::Activate || action == SemanticAction::Toggle;
  const auto channelNamed = [](std::string_view name) -> std::optional<std::size_t> {
    for (std::size_t i = 0U; i < kChannels; ++i)
      if (name == kIds[i]) return i;
    return std::nullopt;
  };
  if (id == "shell.tune.view.curves" || id == "shell.tune.view.vibrato") {
    if (!activate) return unsupported();
    view_ = id == "shell.tune.view.curves" ? View::Curves : View::Vibrato;
    return core::success();
  }
  if (id.starts_with("shell.tune.channel.")) {
    const auto index = channelNamed(id.substr(std::string_view{"shell.tune.channel."}.size()));
    if (!index || !activate) return unsupported();
    return controller.openExpressionLane(ui::expressionChannelAt(*index));
  }
  if (id.starts_with("shell.tune.knob.")) {
    const auto index = channelNamed(id.substr(std::string_view{"shell.tune.knob."}.size()));
    if (!index) return unsupported();
    if (!knobModels(controller.sceneState())[*index].refusal.empty())
      return core::failure(core::ErrorCode::Unsupported,
                           "The selected singer cannot apply this control");
    if (step != 0) return nudge(controller, *index, step);
    if (action == SemanticAction::Activate)
      return controller.openExpressionLane(ui::expressionChannelAt(*index));
    return unsupported();
  }
  if (id == "shell.tune.graph") {
    const auto active = activeChannel(controller);
    if (!active || step == 0) return unsupported();
    return nudge(controller, *active, step);
  }
  if (id == "shell.tune.vibrato.enabled") {
    const auto target = noteTarget(controller);
    if (!activate) return unsupported();
    if (target.note == nullptr)
      return core::failure(core::ErrorCode::InvalidState, "Select one note to shape its vibrato");
    ui::VibratoFields patch;
    patch.enabled = !target.note->vibrato.enabled;
    return controller.applyVibratoToSelection(patch);
  }
  if (id.starts_with("shell.tune.vibrato.")) {
    const auto name = id.substr(std::string_view{"shell.tune.vibrato."}.size());
    for (std::size_t i = 0U; i < kFields; ++i) {
      if (name != kFieldIds[i]) continue;
      if (step == 0) return unsupported();
      const auto target = noteTarget(controller);
      if (target.note == nullptr)
        return core::failure(core::ErrorCode::InvalidState, "Select one note to shape its vibrato");
      const auto spec = fieldSpec(target.note->vibrato, i);
      const auto current = fieldValue(target.note->vibrato, i);
      const auto next = snapped(current + step * spec.step, spec);
      if (std::abs(next - current) < 1e-6) return core::success();
      return controller.applyVibratoToSelection(patchFor(i, next));
    }
  }
  return unsupported();
}

}  // namespace

std::unique_ptr<ShellWorkspace> makeTuneWorkspace() { return std::make_unique<TuneWorkspace>(); }

}  // namespace seam::native_ui::design
