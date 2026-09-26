#include "seam/native_ui/design/voice_workspace.hpp"

#include "seam/phonemizer/phonemizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The VOICE workspace (docs/design/SEAM_UI_REDESIGN_CODE_PLAN_2026-09-25.md §7.2). The host's Voice
// Designer session is shown as three modules on a signal rail: SOURCE (phonation and modulation
// knobs), RESONANCE (pose chips, a spectral envelope editor with a draggable handle per formant and
// the nasal coupling knob) and NOISE (frication sources and their exact seed), then OUTPUT
// (audition play, A/B against a pinned reference, measured level). Every value is read from the
// session's recipe and every edit is a session command: a drag is one session gesture (one undo
// step, nothing on Escape) and a keyboard step is one session edit.
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
using voice_design::VoiceRecipe;

constexpr double kPi = std::numbers::pi;
constexpr Color kWhite{255, 255, 255, 255};
constexpr std::string_view kPrefix = "shell.voice.";

// The audition renders at 48 kHz, so the envelope is the tract's response at that rate.
constexpr double kSampleRate = 48000.0;
constexpr double kMinHz = 80.0;
constexpr double kMaxHz = 8000.0;
// The envelope's dB axis spans exactly the gain a resonance may carry.
constexpr double kMinDb = -48.0;
constexpr double kMaxDb = 24.0;
// Knob travel: 240 points of vertical drag cover the full range; Shift is ten times finer.
constexpr double kKnobTravel = 240.0;
constexpr double kScrollStep = 8.0;
// Shift-drag on a formant: 60 points of horizontal travel double or halve its bandwidth.
constexpr double kBandwidthOctavePixels = 60.0;
constexpr double kHandleRadius = 7.0;
constexpr double kHandleHit = 22.0;
constexpr double kCardHeader = 32.0;
constexpr double kRowHeight = 30.0;

bool contains(ui::Rect r, ui::Point p) noexcept {
  return p.x >= r.x && p.y >= r.y && p.x < r.right() && p.y < r.bottom();
}

bool usable(ui::Rect r) noexcept { return r.width > 0.0 && r.height > 0.0; }

ui::Rect inset(ui::Rect r, double dx, double dy) noexcept {
  return {r.x + dx, r.y + dy, std::max(0.0, r.width - 2.0 * dx), std::max(0.0, r.height - 2.0 * dy)};
}

std::string format(const char* pattern, double value) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), pattern, value);
  return buffer;
}

TextStyle style(FontRole role, double size, double tracking = 0.0,
                TextAlign align = TextAlign::Left, bool upper = false) {
  return TextStyle{role, size, tracking, align, upper};
}

TextStyle fitted(Canvas2D& c, std::string_view text, TextStyle s, double width) {
  if (width <= 0.0 || c.measure(text, s) <= width) return s;
  s.tracking = std::min(s.tracking, 0.4);
  while (s.size > 10.0 && c.measure(text, s) > width) s.size = std::max(10.0, s.size - 0.5);
  return s;
}

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

void sunken(Canvas2D& c, const DesignTokens& t, ui::Rect r, double radius) {
  if (!usable(r)) return;
  const auto p = Path::roundedRect(r, radius);
  c.fill(p, LinearGradient{{r.x, r.y}, {r.x, r.bottom()},
                           {{0.0, t.color.surfaceSunken}, {1.0, t.color.surface}}});
  c.stroke(p, withAlpha(t.color.border, 0.9), StrokeStyle{1.0});
}

void button(Canvas2D& c, const DesignTokens& t, ui::Rect r, std::string_view label, bool enabled,
            bool lit = false) {
  if (!usable(r)) return;
  const auto p = Path::roundedRect(r, 6.0);
  c.save();
  c.setAlpha(enabled ? 1.0 : 0.45);
  if (lit) {
    c.setGlow(withAlpha(t.color.accent, 0.6), 8.0);
    c.fill(p, withAlpha(t.color.accent, 0.24));
  } else {
    c.fill(p, withAlpha(t.color.surfaceRaised, 0.9));
  }
  c.stroke(p, lit ? t.color.accent : withAlpha(t.color.borderStrong, 0.9), StrokeStyle{1.0});
  c.text(inset(r, 4.0, 0.0), label,
         fitted(c, label, style(FontRole::UiSemibold, t.type.label, 0.6, TextAlign::Center),
                r.width - 8.0),
         lit ? t.color.accent : t.color.textPrimary);
  c.restore();
}

void overflowButton(Canvas2D& c, const DesignTokens& t, ui::Rect r, bool open) {
  if (!usable(r)) return;
  if (open) c.fill(Path::roundedRect(r, 5.0), withAlpha(t.color.accent, 0.22));
  c.stroke(Path::roundedRect(r, 5.0), withAlpha(t.color.border, open ? 1.0 : 0.7), StrokeStyle{1.0});
  for (int i = -1; i <= 1; ++i)
    c.fill(Path::circle({r.x + r.width * 0.5 + 5.0 * i, r.y + r.height * 0.5}, 1.6),
           open ? t.color.accent : t.color.textSecondary);
}

// ---- Recipe fields ------------------------------------------------------------------------------

struct KnobSpec final {
  const char* id;
  const char* label;
  double minimum;
  double maximum;
  double step;
};

// The bounds VoiceRecipe::validate accepts for each source field.
constexpr std::array<KnobSpec, 6U> kSourceKnobs{{
    {"open-quotient", "Open Q", 0.05, 0.95, 0.01},
    {"tilt", "Tilt", -48.0, 0.0, 0.5},
    {"aspiration", "Aspir", 0.0, 1.0, 0.01},
    {"pitch-depth", "Pitch depth \u00A2", 0.0, 100.0, 1.0},
    {"amp-depth", "Amp depth", 0.0, 1.0, 0.01},
    {"rate", "Rate", 0.0, 20.0, 0.1},
}};
constexpr KnobSpec kNasalKnob{"nasal", "Nasal", 0.0, 1.0, 0.01};

double sourceValue(const VoiceRecipe& r, std::size_t i) {
  switch (i) {
    case 0U: return r.phonation.openQuotient;
    case 1U: return r.phonation.spectralTiltDbPerOctave;
    case 2U: return r.phonation.aspiration;
    case 3U: return r.modulation.jitterCents;
    case 4U: return r.modulation.shimmerAmount;
    default: return r.modulation.rateHz;
  }
}

void setSourceValue(VoiceRecipe& r, std::size_t i, double v) {
  switch (i) {
    case 0U: r.phonation.openQuotient = v; break;
    case 1U: r.phonation.spectralTiltDbPerOctave = v; break;
    case 2U: r.phonation.aspiration = v; break;
    case 3U: r.modulation.jitterCents = v; break;
    case 4U: r.modulation.shimmerAmount = v; break;
    default: r.modulation.rateHz = v; break;
  }
}

std::string sourceText(std::size_t i, double v) {
  switch (i) {
    case 1U: return format("%.1f dB", v);
    case 3U: return format("%.0f\u00A2", v);
    case 5U: return format("%.1f Hz", v);
    default: return format("%.2f", v);
  }
}

double snapped(double value, const KnobSpec& spec) {
  const auto stepped = std::round(value / spec.step) * spec.step;
  return std::clamp(stepped, spec.minimum, spec.maximum);
}

// Frication fields: 0 centre, 1 bandwidth, 2 gain.
constexpr std::array<const char*, 3U> kNoiseFieldIds{"center", "bandwidth", "gain"};
constexpr std::array<const char*, 3U> kNoiseFieldLabels{"Center", "BW", "Gain"};

double noiseValue(const voice_design::FricationConfig& s, std::size_t field) {
  return field == 0U ? s.centerHz : field == 1U ? s.bandwidthHz : s.gain;
}

std::string noiseText(std::size_t field, double v) {
  if (field == 2U) return format("%.3f", v);
  return v >= 1000.0 ? format("%.1fk", v / 1000.0) : format("%.0f", v);
}

// The range a frication field may take with the other fields held, as validate() requires
// (80..16 kHz centre, 20..16 kHz bandwidth, centre/bandwidth in [0.25, 20], gain 0..0.25).
std::pair<double, double> noiseRange(const voice_design::FricationConfig& s, std::size_t field) {
  if (field == 0U) return {std::max(80.0, 0.25 * s.bandwidthHz), std::min(16000.0, 20.0 * s.bandwidthHz)};
  if (field == 1U) return {std::max(20.0, s.centerHz / 20.0), std::min(16000.0, s.centerHz / 0.25)};
  return {0.0, 0.25};
}

// The whole axis a frication slider draws: logarithmic in Hz, linear in gain.
std::pair<double, double> noiseAxis(std::size_t field) {
  return field == 0U ? std::pair{80.0, 16000.0} : field == 1U ? std::pair{20.0, 16000.0}
                                                               : std::pair{0.0, 0.25};
}

double noiseFraction(std::size_t field, double v) {
  const auto [lo, hi] = noiseAxis(field);
  if (field == 2U) return std::clamp((v - lo) / (hi - lo), 0.0, 1.0);
  return std::clamp(std::log(v / lo) / std::log(hi / lo), 0.0, 1.0);
}

double noiseAt(std::size_t field, double fraction) {
  const auto [lo, hi] = noiseAxis(field);
  fraction = std::clamp(fraction, 0.0, 1.0);
  if (field == 2U) return std::round((lo + fraction * (hi - lo)) * 1000.0) / 1000.0;
  return std::round(lo * std::pow(hi / lo, fraction));
}

// ---- The tract's response -----------------------------------------------------------------------

// The magnitude response of seam-voice-design's VocalTract for one pose: the RBJ constant-peak
// band-pass per resonance in parallel, weighted by 10^(gain/20) normalised by their sum, then the
// nasal stage (resonance and notch) mixed by the coupling as the tract mixes it. Returned in dB
// relative to that normalisation, so a lone resonance peaks at its own gain.
std::complex<double> biquad(double b0, double b1, double b2, double a1, double a2,
                            std::complex<double> zi) {
  return (b0 + b1 * zi + b2 * zi * zi) / (1.0 + a1 * zi + a2 * zi * zi);
}

std::complex<double> bandPass(double f0, double bw, std::complex<double> zi) {
  const auto omega = 2.0 * kPi * f0 / kSampleRate;
  const auto alpha = std::sin(omega) / (2.0 * (f0 / bw));
  const auto a0 = 1.0 + alpha;
  return biquad(alpha / a0, 0.0, -alpha / a0, -2.0 * std::cos(omega) / a0, (1.0 - alpha) / a0, zi);
}

std::complex<double> notch(double f0, double bw, std::complex<double> zi) {
  const auto omega = 2.0 * kPi * f0 / kSampleRate;
  const auto alpha = std::sin(omega) / (2.0 * (f0 / bw));
  const auto a0 = 1.0 + alpha;
  const auto a1 = -2.0 * std::cos(omega) / a0;
  return biquad(1.0 / a0, a1, 1.0 / a0, a1, (1.0 - alpha) / a0, zi);
}

double responseDb(const voice_design::VoicePose& pose, double hz) {
  const auto omega = 2.0 * kPi * hz / kSampleRate;
  const auto zi = std::polar(1.0, -omega);
  double total = 0.0;
  for (const auto& band : pose.formants) total += std::pow(10.0, band.gainDb / 20.0);
  if (total <= 0.0) return kMinDb;
  std::complex<double> oral{0.0, 0.0};
  for (const auto& band : pose.formants) {
    if (band.frequencyHz >= kSampleRate * 0.5) continue;
    oral += std::pow(10.0, band.gainDb / 20.0) / total * bandPass(band.frequencyHz, band.bandwidthHz, zi);
  }
  auto out = oral;
  if (pose.nasal && pose.nasalCoupling != 0.0 && pose.nasal->resonanceHz < kSampleRate * 0.5 &&
      pose.nasal->antiresonanceHz < kSampleRate * 0.5) {
    const auto c = pose.nasalCoupling;
    const auto resonance = bandPass(pose.nasal->resonanceHz, pose.nasal->resonanceBandwidthHz, zi);
    const auto zero = notch(pose.nasal->antiresonanceHz, pose.nasal->antiresonanceBandwidthHz, zi);
    out = phonemizer::isNasalSymbol(pose.phone) ? c * zero * resonance
                                                : (1.0 - c) * oral + c * zero * (0.5 * oral + 0.5 * resonance);
  }
  const auto magnitude = std::abs(out) * total;
  return magnitude > 1e-9 ? 20.0 * std::log10(magnitude) : kMinDb - 12.0;
}

double hzToX(ui::Rect plot, double hz) {
  const auto f = std::log(std::clamp(hz, kMinHz, kMaxHz) / kMinHz) / std::log(kMaxHz / kMinHz);
  return plot.x + f * plot.width;
}

double xToHz(ui::Rect plot, double x) {
  const auto f = plot.width > 0.0 ? std::clamp((x - plot.x) / plot.width, 0.0, 1.0) : 0.0;
  return kMinHz * std::pow(kMaxHz / kMinHz, f);
}

double dbToY(ui::Rect plot, double db) {
  const auto f = (std::clamp(db, kMinDb, kMaxDb) - kMinDb) / (kMaxDb - kMinDb);
  return plot.bottom() - 6.0 - f * std::max(0.0, plot.height - 12.0);
}

double yToDb(ui::Rect plot, double y) {
  const auto span = std::max(1.0, plot.height - 12.0);
  return kMinDb + std::clamp((plot.bottom() - 6.0 - y) / span, 0.0, 1.0) * (kMaxDb - kMinDb);
}

// ---- Layout -------------------------------------------------------------------------------------

enum class Card : std::size_t { Source = 0U, Resonance = 1U, Noise = 2U, Output = 3U };
constexpr std::array<const char*, 4U> kCardIds{"source", "resonance", "noise", "output"};
constexpr std::array<const char*, 4U> kCardTitles{"Source", "Resonance", "Noise", "Output"};

// Wide: the singer column and the three modules in a row, the rail and OUTPUT below. Stacked:
// RESONANCE beside the singer, SOURCE and NOISE under it. Tabbed (short or narrow bodies): one
// module at a time behind three tabs, OUTPUT in a strip with a small portrait.
enum class Mode { Wide, Stacked, Tabbed };

struct VoiceLayout final {
  Mode mode{Mode::Wide};
  ui::Rect area;
  ui::Rect header, identity, newButton, openButton;
  std::array<ui::Rect, 3U> view{};
  ui::Rect hero, ring, heroCaption;
  std::array<ui::Rect, 4U> card{};
  std::array<ui::Rect, 4U> more{};
  std::array<ui::Rect, 4U> body{};
  ui::Rect rail;
  std::array<ui::Rect, 6U> knob{};
  std::array<ui::Rect, 2U> knobCaption{};
  ui::Rect chipStrip, envelope, plot, nasal;
  ui::Rect list, seedCaption, seed;
  ui::Rect play, abA, abB, level, caption;
};

void layoutCardFrame(VoiceLayout& l, std::size_t i, ui::Rect r, bool header) {
  l.card[i] = r;
  if (!usable(r)) return;
  if (header) {
    l.more[i] = {r.right() - 8.0 - 24.0, r.y + 4.0, 24.0, 24.0};
    l.body[i] = {r.x + 12.0, r.y + kCardHeader + 4.0, std::max(0.0, r.width - 24.0),
                 std::max(0.0, r.height - kCardHeader - 12.0)};
  } else {
    l.body[i] = inset(r, 8.0, 8.0);
  }
}

void layoutSource(VoiceLayout& l, ui::Rect b) {
  if (!usable(b)) return;
  const auto twoRows = b.height >= 136.0;
  if (twoRows) {
    // Caption, row, caption, row: 18 + cell + 20 + cell.
    const auto cell = std::min(104.0, (b.height - 38.0) / 2.0);
    const auto width = b.width / 3.0;
    l.knobCaption[0] = {b.x, b.y, b.width, 16.0};
    for (std::size_t i = 0U; i < 3U; ++i)
      l.knob[i] = {b.x + width * static_cast<double>(i), b.y + 18.0, width, cell};
    l.knobCaption[1] = {b.x, b.y + 18.0 + cell + 2.0, b.width, 16.0};
    for (std::size_t i = 3U; i < 6U; ++i)
      l.knob[i] = {b.x + width * static_cast<double>(i - 3U), l.knobCaption[1].bottom() + 2.0, width, cell};
  } else {
    const auto width = b.width / 6.0;
    for (std::size_t i = 0U; i < 6U; ++i)
      l.knob[i] = {b.x + width * static_cast<double>(i), b.y, width, std::min(b.height, 96.0)};
  }
}

void layoutResonance(VoiceLayout& l, ui::Rect b) {
  if (!usable(b)) return;
  l.chipStrip = {b.x, b.y, b.width, 24.0};
  const auto top = l.chipStrip.bottom() + 6.0;
  const auto height = std::max(0.0, b.bottom() - top);
  if (height >= 320.0) {
    // A tall module keeps the envelope at its full width and puts the nasal knob under it.
    l.nasal = {b.x, b.bottom() - 96.0, 96.0, 96.0};
    l.envelope = {b.x, top, b.width, std::max(0.0, l.nasal.y - 8.0 - top)};
  } else {
    const auto nasalWidth = std::min(88.0, std::max(56.0, b.width * 0.2));
    l.nasal = {b.right() - nasalWidth, top, nasalWidth, std::min(height, 96.0)};
    l.envelope = {b.x, top, std::max(0.0, l.nasal.x - 8.0 - b.x), height};
  }
  // A 34-point gutter for the dB labels and 14 points under the plot for the frequency labels.
  const auto labels = l.envelope.height >= 72.0;
  l.plot = labels ? ui::Rect{l.envelope.x + 34.0, l.envelope.y + 4.0,
                             std::max(0.0, l.envelope.width - 38.0),
                             std::max(0.0, l.envelope.height - 20.0)}
                  : inset(l.envelope, 4.0, 3.0);
}

void layoutNoise(VoiceLayout& l, ui::Rect b) {
  if (!usable(b)) return;
  if (b.height >= 120.0) {
    l.seedCaption = {b.x, b.bottom() - 26.0, 64.0, 26.0};
    l.seed = {b.x + 64.0, b.bottom() - 26.0, std::max(0.0, b.width - 64.0), 26.0};
    l.list = {b.x, b.y, b.width, std::max(0.0, l.seed.y - 8.0 - b.y)};
  } else {
    const auto width = std::min(150.0, b.width * 0.36);
    l.seedCaption = {b.right() - width, b.y, width, 16.0};
    l.seed = {b.right() - width, b.y + 18.0, width, 26.0};
    l.list = {b.x, b.y, std::max(0.0, b.width - width - 10.0), b.height};
  }
}

void layoutOutputRow(VoiceLayout& l, ui::Rect r, bool withRing) {
  // Portrait (tabbed only), Play, A and B, then the level and status caption.
  auto x = r.x;
  const auto h = std::min(32.0, r.height);
  const auto y = r.y + (r.height - h) * 0.5;
  if (withRing) {
    l.ring = {x, y, h, h};
    x += h + 8.0;
  }
  l.play = {x, y, 72.0, h};
  x += 78.0;
  l.abA = {x, y, 30.0, h};
  l.abB = {x + 32.0, y, 30.0, h};
  x += 70.0;
  l.level = {x, y, std::max(0.0, r.right() - x), h};
  l.caption = l.level;
}

VoiceLayout solveVoiceLayout(ui::Rect area, Card view) {
  VoiceLayout l;
  l.area = area;
  l.mode = area.width >= 1000.0 && area.height >= 520.0  ? Mode::Wide
           : area.width >= 640.0 && area.height >= 460.0 ? Mode::Stacked
                                                         : Mode::Tabbed;
  if (l.mode == Mode::Tabbed) {
    constexpr double pad = 8.0;
    const ui::Rect top{area.x + pad, area.y + pad, std::max(0.0, area.width - 2.0 * pad), 28.0};
    const auto tabWidth = std::min(104.0, (top.width - 32.0) / 3.0);
    for (std::size_t i = 0U; i < l.view.size(); ++i)
      l.view[i] = {top.x + (tabWidth + 4.0) * static_cast<double>(i), top.y, tabWidth, top.height};
    const auto shown = static_cast<std::size_t>(view);
    l.more[shown] = {top.right() - 24.0, top.y + 2.0, 24.0, 24.0};
    l.identity = {l.view[2].right() + 8.0, top.y, std::max(0.0, l.more[shown].x - 8.0 - (l.view[2].right() + 8.0)), top.height};
    const ui::Rect output{area.x + pad, area.bottom() - pad - 36.0, top.width, 36.0};
    l.card[3] = output;
    l.more[3] = {output.right() - 24.0, output.y + 6.0, 24.0, 24.0};
    layoutOutputRow(l, {output.x, output.y, std::max(0.0, l.more[3].x - 8.0 - output.x), output.height}, true);
    l.newButton = {l.play.x, l.play.y, 72.0, l.play.height};
    l.openButton = {l.abA.x, l.play.y, 62.0, l.play.height};
    l.hero = l.ring;
    const ui::Rect body{top.x, top.bottom() + 6.0, top.width,
                        std::max(0.0, output.y - 6.0 - (top.bottom() + 6.0))};
    l.card[shown] = body;
    l.body[shown] = body;
    if (view == Card::Source) layoutSource(l, body);
    if (view == Card::Resonance) layoutResonance(l, body);
    if (view == Card::Noise) layoutNoise(l, body);
    return l;
  }
  constexpr double pad = 12.0;
  constexpr double gap = 12.0;
  l.header = {area.x + pad, area.y + pad, std::max(0.0, area.width - 2.0 * pad), 28.0};
  l.openButton = {l.header.right() - 84.0, l.header.y, 84.0, 28.0};
  l.newButton = {l.openButton.x - 8.0 - 120.0, l.header.y, 120.0, 28.0};
  l.identity = {l.header.x, l.header.y, std::max(0.0, l.newButton.x - 12.0 - l.header.x), 28.0};
  const auto top = l.header.bottom() + 8.0;
  if (l.mode == Mode::Wide) {
    const auto outputHeight = 84.0;
    const auto bottom = area.bottom() - pad - outputHeight - gap - 28.0;  // room for the rail
    l.hero = {area.x + pad, top, 180.0, std::max(0.0, bottom - top)};
    const auto ringSize = std::clamp(std::min(l.hero.width - 24.0, l.hero.height - 72.0), 64.0, 176.0);
    l.ring = {l.hero.x + (l.hero.width - ringSize) * 0.5, l.hero.y + 20.0, ringSize, ringSize};
    l.heroCaption = {l.hero.x + 8.0, l.ring.bottom() + 10.0, l.hero.width - 16.0, 40.0};
    const auto left = l.hero.right() + gap;
    const auto width = std::max(0.0, area.right() - pad - left - 2.0 * gap);
    const std::array<double, 3U> shares{0.33, 0.36, 0.31};
    auto x = left;
    for (std::size_t i = 0U; i < 3U; ++i) {
      layoutCardFrame(l, i, {x, top, width * shares[i], std::max(0.0, bottom - top)}, true);
      x += width * shares[i] + gap;
    }
    const ui::Rect output{area.right() - pad - 440.0, area.bottom() - pad - outputHeight, 440.0, outputHeight};
    layoutCardFrame(l, 3U, output, true);
    layoutOutputRow(l, {l.body[3].x, l.body[3].y, l.body[3].width, l.body[3].height}, false);
    const auto first = l.card[0].x + l.card[0].width * 0.5;
    const auto last = std::max(l.card[2].x + l.card[2].width * 0.5, output.x);
    l.rail = {first, bottom + 14.0, std::max(0.0, last - first), 1.0};
  } else {
    const auto outputHeight = 72.0;
    const auto bottom = area.bottom() - pad - outputHeight - gap;
    const auto rows = std::max(0.0, bottom - top - gap);
    const auto upper = std::floor(rows * 0.5);
    l.hero = {area.x + pad, top, 150.0, upper};
    const auto ringSize = std::clamp(std::min(l.hero.width - 16.0, l.hero.height - 56.0), 56.0, 132.0);
    l.ring = {l.hero.x + (l.hero.width - ringSize) * 0.5, l.hero.y + 8.0, ringSize, ringSize};
    l.heroCaption = {l.hero.x + 4.0, l.ring.bottom() + 6.0, l.hero.width - 8.0, 36.0};
    layoutCardFrame(l, 1U, {l.hero.right() + gap, top, std::max(0.0, area.right() - pad - l.hero.right() - gap), upper}, true);
    const auto lowerTop = top + upper + gap;
    const auto half = std::max(0.0, (area.width - 2.0 * pad - gap) * 0.5);
    layoutCardFrame(l, 0U, {area.x + pad, lowerTop, half, std::max(0.0, bottom - lowerTop)}, true);
    layoutCardFrame(l, 2U, {area.x + pad + half + gap, lowerTop, half, std::max(0.0, bottom - lowerTop)}, true);
    const ui::Rect output{area.x + pad, area.bottom() - pad - outputHeight, std::max(0.0, area.width - 2.0 * pad), outputHeight};
    layoutCardFrame(l, 3U, output, true);
    layoutOutputRow(l, l.body[3], false);
  }
  layoutSource(l, l.body[0]);
  layoutResonance(l, l.body[1]);
  layoutNoise(l, l.body[2]);
  return l;
}

// Chips for the poses around the selected one, with paging arrows when they do not all fit and
// the add (duplicate) chip at the end.
struct Chips final {
  std::vector<std::pair<std::size_t, ui::Rect>> chips;
  ui::Rect previous, next, add;
};

std::string poseLabel(const voice_design::VoicePose& pose) {
  return pose.style == "neutral" || pose.style.empty() ? pose.phone : pose.phone + " \u00B7 " + pose.style;
}

double chipWidth(const voice_design::VoicePose& pose) {
  return std::clamp(18.0 + 7.5 * static_cast<double>(poseLabel(pose).size()), 30.0, 110.0);
}

Chips layoutChips(ui::Rect strip, const VoiceRecipe& recipe, std::size_t selected) {
  Chips out;
  if (!usable(strip) || recipe.poses.empty()) return out;
  constexpr double gap = 4.0;
  out.add = {strip.right() - 28.0, strip.y, 28.0, strip.height};
  double total = 0.0;
  for (const auto& pose : recipe.poses) total += chipWidth(pose) + gap;
  auto room = out.add.x - gap - strip.x;
  std::size_t first = 0U;
  std::size_t last = recipe.poses.size();
  if (total > room) {
    out.previous = {strip.x, strip.y, 24.0, strip.height};
    out.next = {out.add.x - gap - 24.0, strip.y, 24.0, strip.height};
    room = out.next.x - gap - out.previous.right() - gap;
    // Grow a window around the selected pose until it is full.
    first = std::min(selected, recipe.poses.size() - 1U);
    last = first + 1U;
    double used = chipWidth(recipe.poses[first]) + gap;
    bool grew = true;
    while (grew) {
      grew = false;
      if (last < recipe.poses.size() && used + chipWidth(recipe.poses[last]) + gap <= room) {
        used += chipWidth(recipe.poses[last]) + gap;
        ++last;
        grew = true;
      }
      if (first > 0U && used + chipWidth(recipe.poses[first - 1U]) + gap <= room) {
        used += chipWidth(recipe.poses[first - 1U]) + gap;
        --first;
        grew = true;
      }
    }
  }
  auto x = usable(out.previous) ? out.previous.right() + gap : strip.x;
  for (auto i = first; i < last; ++i) {
    const auto w = chipWidth(recipe.poses[i]);
    if (x + w > (usable(out.next) ? out.next.x - gap : out.add.x - gap) + 0.01) break;
    out.chips.emplace_back(i, ui::Rect{x, strip.y, w, strip.height});
    x += w + gap;
  }
  return out;
}

// Formant handles inside the plot: frequency on x, the resonance's own gain on y.
struct Handle final {
  std::size_t band{0U};
  ui::Point center;
  ui::Rect hit;
};

std::vector<Handle> layoutHandles(ui::Rect plot, const voice_design::VoicePose& pose) {
  std::vector<Handle> out;
  if (!usable(plot)) return out;
  for (std::size_t i = 0U; i < pose.formants.size(); ++i) {
    const auto& band = pose.formants[i];
    if (band.frequencyHz < kMinHz || band.frequencyHz > kMaxHz) continue;
    const ui::Point center{hzToX(plot, band.frequencyHz), dbToY(plot, band.gainDb)};
    const auto half = kHandleHit * 0.5;
    ui::Rect hit{center.x - half, center.y - half, kHandleHit, kHandleHit};
    hit.x = std::clamp(hit.x, plot.x, std::max(plot.x, plot.right() - hit.width));
    hit.y = std::clamp(hit.y, plot.y, std::max(plot.y, plot.bottom() - hit.height));
    out.push_back({i, center, hit});
  }
  return out;
}

// Frication rows for the selected pose's style, as the Voice Designer lists them.
struct NoiseRow final {
  std::size_t index{0U};
  ui::Rect row, label;
  std::array<ui::Rect, 3U> slider{};
};

std::vector<std::size_t> fricationsFor(const VoiceRecipe& recipe, std::size_t pose) {
  std::vector<std::size_t> out;
  if (pose >= recipe.poses.size()) return out;
  for (std::size_t i = 0U; i < recipe.frications.size(); ++i)
    if (recipe.frications[i].style == recipe.poses[pose].style) out.push_back(i);
  return out;
}

std::vector<NoiseRow> layoutNoiseRows(ui::Rect list, const std::vector<std::size_t>& indices,
                                      std::size_t scroll) {
  std::vector<NoiseRow> out;
  if (!usable(list)) return out;
  const auto visible = static_cast<std::size_t>(std::max(0.0, std::floor((list.height - 16.0) / kRowHeight)));
  const auto first = std::min(scroll, indices.size() > visible ? indices.size() - visible : 0U);
  const auto labelWidth = std::min(48.0, list.width * 0.2);
  const auto sliderWidth = std::max(0.0, (list.width - labelWidth - 12.0) / 3.0);
  for (std::size_t k = 0U; k < visible && first + k < indices.size(); ++k) {
    NoiseRow row;
    row.index = indices[first + k];
    row.row = {list.x, list.y + 16.0 + kRowHeight * static_cast<double>(k), list.width, kRowHeight - 2.0};
    row.label = {row.row.x, row.row.y, labelWidth, row.row.height};
    for (std::size_t f = 0U; f < 3U; ++f)
      row.slider[f] = {row.label.right() + 4.0 + (sliderWidth + 4.0) * static_cast<double>(f), row.row.y,
                       sliderWidth, row.row.height};
    out.push_back(row);
  }
  return out;
}

// The honest panel's one action in a host without a designer.
ui::Rect honestButton(ui::Rect area) {
  return {area.x + 28.0, area.y + 84.0, std::min(200.0, std::max(0.0, area.width - 56.0)), 32.0};
}

// ---- Card overflow actions ----------------------------------------------------------------------

enum class Action : std::uint8_t {
  New, Open, Save, SaveAs, Seed,
  DuplicatePose, RemovePose,
  AddFrication, RemoveFrication, FricationSeed,
  PinReference, ClearReference, Stop,
};

struct MenuItem final {
  Action action;
  const char* id;
  const char* label;
};

std::vector<MenuItem> menuFor(Card card) {
  switch (card) {
    case Card::Source:
      return {{Action::New, "new", "New starter voice"}, {Action::Open, "open", "Open recipe\u2026"},
              {Action::Save, "save", "Save"}, {Action::SaveAs, "save-as", "Save As\u2026"},
              {Action::Seed, "seed", "Voice seed\u2026"}};
    case Card::Resonance:
      return {{Action::DuplicatePose, "duplicate-pose", "Duplicate pose\u2026"},
              {Action::RemovePose, "remove-pose", "Remove pose"}};
    case Card::Noise:
      return {{Action::AddFrication, "add-frication", "Add frication\u2026"},
              {Action::RemoveFrication, "remove-frication", "Remove frication"},
              {Action::FricationSeed, "frication-seed", "Frication seed\u2026"}};
    case Card::Output:
      return {{Action::PinReference, "pin-reference", "Keep B as reference A"},
              {Action::ClearReference, "clear-reference", "Clear reference A"},
              {Action::Stop, "stop", "Stop audition"}};
  }
  return {};
}

std::vector<ui::Rect> menuRows(ui::Rect area, ui::Rect anchor, std::size_t count) {
  constexpr double kMenuRow = 26.0;
  const auto width = std::min(210.0, area.width - 8.0);
  const auto height = 12.0 + kMenuRow * static_cast<double>(count);
  auto x = std::clamp(anchor.right() - width, area.x + 4.0, area.right() - 4.0 - width);
  auto y = anchor.bottom() + 4.0;
  if (y + height > area.bottom() - 4.0) y = std::max(area.y + 4.0, anchor.y - 4.0 - height);
  std::vector<ui::Rect> rows;
  rows.push_back({x, y, width, height});  // the menu panel itself
  for (std::size_t i = 0U; i < count; ++i)
    rows.push_back({x + 6.0, y + 6.0 + kMenuRow * static_cast<double>(i), width - 12.0, kMenuRow});
  return rows;
}

// ---- The workspace ------------------------------------------------------------------------------

enum class Target : std::uint8_t { Source, Nasal, Formant, Noise };

struct Drag final {
  Target target{Target::Source};
  std::size_t index{0U};
  std::size_t field{0U};  // formant: 0 frequency/gain, 1 bandwidth; noise: 0..2
  std::uint64_t epoch{0U};
  std::size_t pose{0U};
  ui::Point start;
  bool fine{false};
  VoiceRecipe base;
};

std::string cardId(std::size_t i) { return std::string{kPrefix} + kCardIds[i]; }

class VoiceWorkspaceImpl final : public VoiceWorkspace {
public:
  [[nodiscard]] std::string_view idPrefix() const noexcept override { return kPrefix; }

  void setHost(ShellVoiceHost host) override { host_ = std::move(host); }
  void setPortrait(std::shared_ptr<const paint::Image> portrait) override {
    portrait_ = std::move(portrait);
  }
  [[nodiscard]] const std::string& message() const noexcept override { return message_; }

  bool poll() override {
    level_ = host_.level ? host_.level() : std::nullopt;
    auto* s = sessionIfOpened();
    if (s == nullptr) return level_.has_value();
    const auto wasBusy = s->busy();
    const auto polled = s->poll();
    if (!polled) static_cast<void>(note(polled));
    else if (wasBusy && !s->busy() && !fileDone_.empty()) message_ = fileDone_;
    if (!s->busy()) fileDone_.clear();
    if (pendingPlay_ && !s->auditionBusy()) {
      pendingPlay_ = false;
      if (s->auditionAudio()) static_cast<void>(note(startPlayback(s->auditionAudio(), "B")));
    }
    return s->busy() || s->auditionBusy() || pendingPlay_ || level_.has_value();
  }

  core::Result<void> undo(bool redo) override {
    auto* s = session();
    if (s == nullptr || s->model() == nullptr)
      return core::failure(core::ErrorCode::InvalidState, "No voice recipe is open");
    if (drag_) return core::failure(core::ErrorCode::Conflict, "Finish the drag before undo or redo");
    const auto revision = s->model()->revision();
    return note(redo ? s->redo(s->epoch(), revision) : s->undo(s->epoch(), revision));
  }

  void paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController& controller,
             const EditorSceneState& state, ui::Rect area) const override;

  core::Result<void> pointerDown(NativeEditorController& controller, const PointerEvent& event,
                                 ui::Rect area) override;
  core::Result<void> pointerMove(NativeEditorController& controller, const PointerEvent& event,
                                 ui::Rect area) override {
    static_cast<void>(controller);
    static_cast<void>(area);
    if (!drag_) return core::success();
    return dragTo(event.position);
  }
  core::Result<void> pointerUp(NativeEditorController& controller, const PointerEvent& event,
                               ui::Rect area) override {
    static_cast<void>(controller);
    static_cast<void>(event);
    static_cast<void>(area);
    if (!drag_) return core::success();
    const auto drag = std::move(*drag_);
    drag_.reset();
    auto* s = session();
    if (s == nullptr || s->model() == nullptr || s->epoch() != drag.epoch ||
        !s->model()->gestureActive())
      return core::success();
    // One gesture is one session edit: the commit is the only undo step it leaves.
    return note(s->endGesture(drag.epoch, s->model()->revision(), true));
  }

  bool scroll(NativeEditorController& controller, ui::Point anchor, double deltaX, double deltaY,
              ui::Rect area) override;

  void semantics(const NativeEditorController& controller, const EditorSceneState& state,
                 ui::Rect area, std::vector<SemanticNode>& out) const override;
  core::Result<void> perform(NativeEditorController& controller, std::string_view id,
                             SemanticAction action) override;

  void cancelGestures(NativeEditorController& controller) override {
    static_cast<void>(controller);
    scrollAccumulator_ = 0.0;
    if (!drag_) return;
    const auto drag = std::move(*drag_);
    drag_.reset();
    auto* s = session();
    // Escape or capture loss: the session restores the recipe the gesture started from.
    if (s != nullptr && s->model() != nullptr && s->epoch() == drag.epoch && s->model()->gestureActive())
      static_cast<void>(s->endGesture(drag.epoch, s->model()->revision(), false));
  }
  [[nodiscard]] bool gestureActive() const noexcept override { return drag_.has_value(); }
  [[nodiscard]] bool dismissTransient() override {
    if (!menu_) return false;
    focusRequest_.clear();
    menu_.reset();
    return true;
  }
  [[nodiscard]] std::string takeFocusRequest() override {
    auto request = std::move(focusRequest_);
    focusRequest_.clear();
    return request;
  }

private:
  VoiceDesignerSession* session() const {
    if (!host_.designer) return nullptr;
    auto* s = host_.designer();
    if (s != nullptr) opened_ = true;
    return s;
  }
  // The session only once VOICE has asked for it: polling never creates one.
  VoiceDesignerSession* sessionIfOpened() const { return opened_ ? session() : nullptr; }

  const VoiceRecipe* recipe() const {
    auto* s = session();
    return s != nullptr && s->model() != nullptr ? &s->model()->recipe() : nullptr;
  }
  std::size_t pose() const {
    auto* s = session();
    return s == nullptr ? 0U : s->auditionPose();
  }
  std::optional<std::size_t> selectedFrication() const {
    const auto* r = recipe();
    if (r == nullptr) return std::nullopt;
    const auto list = fricationsFor(*r, pose());
    if (list.empty()) return std::nullopt;
    if (std::find(list.begin(), list.end(), fricationSelection_) != list.end()) return fricationSelection_;
    return list.front();
  }
  bool playing() const { return level_.has_value(); }

  Card shownView() const { return view_; }
  VoiceLayout layoutFor(ui::Rect area) const { return solveVoiceLayout(area, view_); }
  bool cardShown(const VoiceLayout& l, Card card) const {
    return usable(l.card[static_cast<std::size_t>(card)]);
  }

  core::Result<void> note(core::Result<void> result) {
    if (!result) {
      message_ = result.error().message;
      if (!result.error().context.empty()) message_ += ": " + result.error().context;
    }
    return result;
  }

  core::Result<void> unavailable() const {
    return core::failure(core::ErrorCode::Unsupported, host_.unavailable);
  }

  core::Result<void> beginDrag(Target target, std::size_t index, std::size_t field, ui::Point at,
                               bool fine) {
    auto* s = session();
    if (s == nullptr || s->model() == nullptr)
      return core::failure(core::ErrorCode::InvalidState, "No voice recipe is open");
    const auto begun = s->beginGesture(s->epoch(), s->model()->revision());
    if (!begun) return note(begun);
    drag_ = Drag{.target = target, .index = index, .field = field, .epoch = s->epoch(),
                 .pose = s->auditionPose(), .start = at, .fine = fine, .base = s->model()->recipe()};
    return core::success();
  }

  // Applies the drag at the pointer as the gesture's preview recipe.
  core::Result<void> dragTo(ui::Point p) {
    auto* s = session();
    if (s == nullptr || s->model() == nullptr || !drag_ || s->epoch() != drag_->epoch) return core::success();
    auto desired = drag_->base;
    const auto& d = *drag_;
    const auto l = layoutFor(lastArea_);
    switch (d.target) {
      case Target::Source: {
        const auto& spec = kSourceKnobs[d.index];
        const auto scale = d.fine ? 0.1 : 1.0;
        const auto value = sourceValue(d.base, d.index) +
                           (d.start.y - p.y) / kKnobTravel * (spec.maximum - spec.minimum) * scale;
        setSourceValue(desired, d.index, snapped(value, spec));
        break;
      }
      case Target::Nasal: {
        if (d.pose >= desired.poses.size()) return core::success();
        const auto scale = d.fine ? 0.1 : 1.0;
        const auto value = d.base.poses[d.pose].nasalCoupling +
                           (d.start.y - p.y) / kKnobTravel * (kNasalKnob.maximum - kNasalKnob.minimum) * scale;
        desired.poses[d.pose].nasalCoupling = snapped(value, kNasalKnob);
        break;
      }
      case Target::Formant: {
        if (d.pose >= desired.poses.size() || d.index >= desired.poses[d.pose].formants.size())
          return core::success();
        auto& bands = desired.poses[d.pose].formants;
        auto& band = bands[d.index];
        const auto& start = d.base.poses[d.pose].formants[d.index];
        if (d.field == 1U) {
          const auto octaves = (p.x - d.start.x) / kBandwidthOctavePixels;
          band.bandwidthHz = std::round(std::clamp(start.bandwidthHz * std::pow(2.0, octaves), 10.0, 5000.0));
        } else {
          // Resonances stay ordered: a handle stops just short of its neighbours.
          const auto lower = d.index > 0U ? bands[d.index - 1U].frequencyHz + 1.0 : 50.0;
          const auto upper = d.index + 1U < bands.size() ? bands[d.index + 1U].frequencyHz - 1.0 : 16000.0;
          const auto startX = hzToX(l.plot, start.frequencyHz);
          const auto hz = xToHz(l.plot, startX + (p.x - d.start.x));
          band.frequencyHz = std::round(std::clamp(hz, std::max(lower, kMinHz), std::min(upper, kMaxHz)));
          const auto startY = dbToY(l.plot, start.gainDb);
          const auto db = yToDb(l.plot, startY + (p.y - d.start.y));
          band.gainDb = std::round(std::clamp(db, kMinDb, kMaxDb) * 10.0) / 10.0;
        }
        break;
      }
      case Target::Noise: {
        if (d.index >= desired.frications.size()) return core::success();
        auto& source = desired.frications[d.index].source;
        const auto rows = layoutNoiseRows(l.list, fricationsFor(d.base, d.pose), noiseScroll_);
        const auto row = std::find_if(rows.begin(), rows.end(), [&](const NoiseRow& r) { return r.index == d.index; });
        if (row == rows.end()) return core::success();
        const auto track = row->slider[d.field];
        const auto fraction = track.width > 0.0 ? (p.x - track.x) / track.width : 0.0;
        const auto [lo, hi] = noiseRange(source, d.field);
        const auto value = std::clamp(noiseAt(d.field, fraction), lo, hi);
        if (d.field == 0U) source.centerHz = value;
        else if (d.field == 1U) source.bandwidthHz = value;
        else source.gain = value;
        break;
      }
    }
    return note(s->updateGesture(d.epoch, s->model()->revision(), std::move(desired)));
  }

  // A keyboard or accessibility step: one validated session edit, so one undo step.
  core::Result<void> step(Target target, std::size_t index, std::size_t field, int steps) {
    auto* s = session();
    if (s == nullptr || s->model() == nullptr)
      return core::failure(core::ErrorCode::InvalidState, "No voice recipe is open");
    auto desired = s->model()->recipe();
    const auto p = s->auditionPose();
    const auto k = static_cast<double>(steps);
    switch (target) {
      case Target::Source:
        setSourceValue(desired, index, snapped(sourceValue(desired, index) + k * kSourceKnobs[index].step, kSourceKnobs[index]));
        break;
      case Target::Nasal:
        if (p >= desired.poses.size()) return core::success();
        desired.poses[p].nasalCoupling = snapped(desired.poses[p].nasalCoupling + k * kNasalKnob.step, kNasalKnob);
        break;
      case Target::Formant: {
        if (p >= desired.poses.size() || index >= desired.poses[p].formants.size()) return core::success();
        auto& bands = desired.poses[p].formants;
        const auto lower = index > 0U ? bands[index - 1U].frequencyHz + 1.0 : 50.0;
        const auto upper = index + 1U < bands.size() ? bands[index + 1U].frequencyHz - 1.0 : 16000.0;
        bands[index].frequencyHz = std::clamp(bands[index].frequencyHz + 10.0 * k, lower, upper);
        break;
      }
      case Target::Noise: {
        if (index >= desired.frications.size()) return core::success();
        auto& source = desired.frications[index].source;
        const auto [lo, hi] = noiseRange(source, field);
        if (field == 2U) source.gain = std::clamp(std::round((source.gain + 0.005 * k) * 1000.0) / 1000.0, lo, hi);
        else {
          auto& value = field == 0U ? source.centerHz : source.bandwidthHz;
          value = std::clamp(std::round(value * std::pow(2.0, k / 12.0)), lo, hi);
        }
        break;
      }
    }
    return note(s->edit(s->epoch(), s->model()->revision(), std::move(desired)));
  }

  core::Result<void> selectPose(std::size_t index) {
    auto* s = session();
    if (s == nullptr || s->model() == nullptr)
      return core::failure(core::ErrorCode::InvalidState, "No voice recipe is open");
    noiseScroll_ = 0U;
    return note(s->selectAudition(s->epoch(), s->model()->revision(), index, s->auditionPitch()));
  }

  core::Result<void> startPlayback(const std::shared_ptr<const voicebank::AudioBuffer>& audio,
                                   std::string_view which) {
    if (!host_.play) return core::failure(core::ErrorCode::Unsupported, host_.playUnavailable);
    auto played = host_.play(audio);
    if (played) {
      message_ = std::string{"Playing "} + std::string{which} + " (not approved)";
      level_ = 0.0F;
    }
    return played;
  }

  core::Result<void> play() {
    auto* s = session();
    if (s == nullptr || s->model() == nullptr)
      return core::failure(core::ErrorCode::InvalidState, "No voice recipe is open");
    if (!host_.play) return note(core::failure(core::ErrorCode::Unsupported, host_.playUnavailable));
    if (playing()) {
      if (host_.stop) host_.stop();
      level_.reset();
      message_ = "Stopped";
      return core::success();
    }
    if (useReference_) {
      if (!s->auditionReference())
        return note(core::failure(core::ErrorCode::InvalidState, "Keep a rendered B as reference A first"));
      return note(startPlayback(s->auditionReference()->audio, "A"));
    }
    if (s->auditionAudio()) return note(startPlayback(s->auditionAudio(), "B"));
    const auto begun = s->beginAudition();
    if (begun) {
      pendingPlay_ = true;
      message_ = "Rendering the audition\u2026";
    }
    return note(begun);
  }

  core::Result<void> run(NativeEditorController& controller, Action action);

  core::Result<void> pressMenu(ui::Point p, ui::Rect area) {
    const auto card = *menu_;
    const auto items = menuFor(card);
    const auto rows = menuRows(area, layoutFor(area).more[static_cast<std::size_t>(card)], items.size());
    menu_.reset();
    for (std::size_t i = 0U; i < items.size(); ++i) {
      if (!contains(rows[i + 1U], p)) continue;
      auto* controller = controller_;
      if (controller == nullptr) return core::success();
      return run(*controller, items[i].action);
    }
    return core::success();
  }

  bool actionEnabled(Action action) const {
    auto* s = session();
    const auto* r = recipe();
    switch (action) {
      case Action::New:
      case Action::Open: return s != nullptr && !s->busy();
      case Action::Save:
      case Action::SaveAs:
      case Action::Seed: return r != nullptr && !s->busy();
      case Action::DuplicatePose: return r != nullptr;
      case Action::RemovePose: return r != nullptr && r->poses.size() > 1U;
      case Action::AddFrication: return r != nullptr;
      case Action::RemoveFrication:
      case Action::FricationSeed: return selectedFrication().has_value();
      case Action::PinReference: return s != nullptr && static_cast<bool>(s->auditionAudio());
      case Action::ClearReference: return s != nullptr && s->auditionReference().has_value();
      case Action::Stop: return playing();
    }
    return false;
  }

  void paintHonest(Canvas2D& c, const DesignTokens& t, ui::Rect area) const;
  void paintHero(Canvas2D& c, const DesignTokens& t, const VoiceLayout& l) const;
  void paintSource(Canvas2D& c, const DesignTokens& t, const VoiceLayout& l, const VoiceRecipe* r) const;
  void paintResonance(Canvas2D& c, const DesignTokens& t, const VoiceLayout& l, const VoiceRecipe* r) const;
  void paintNoise(Canvas2D& c, const DesignTokens& t, const VoiceLayout& l, const VoiceRecipe* r) const;
  void paintOutput(Canvas2D& c, const DesignTokens& t, const VoiceLayout& l) const;
  void paintKnob(Canvas2D& c, const DesignTokens& t, ui::Rect cell, std::string_view label,
                 std::string_view value, double fraction, bool enabled, bool active) const;

  std::string recipeIdentity() const {
    auto* s = session();
    if (s == nullptr || s->model() == nullptr) return "No voice recipe is open";
    const auto name = s->path().empty() ? std::string{"Unsaved draft"} : s->path().filename().string();
    return s->model()->recipe().id + "  \u2022  " + name + (s->model()->dirty() ? "  \u2022  Edited" : "");
  }
  std::string auditionStatus() const {
    auto* s = session();
    if (s == nullptr || s->model() == nullptr) return "No voice to audition";
    if (s->auditionBusy()) return "Rendering B\u2026";
    if (!message_.empty()) return message_;
    return s->auditionAudio() ? "B rendered for this edit" : "B not rendered";
  }
  std::string singerState() const {
    auto* s = session();
    if (playing()) return "Listening";
    if (s != nullptr && s->auditionBusy()) return "Rendering";
    return "Idle";
  }
  // Level as a ring fraction: -60 dBFS (and silence) is empty, 0 dBFS is full.
  double levelFraction() const {
    if (!level_ || *level_ <= 0.0F) return 0.0;
    return std::clamp((20.0 * std::log10(static_cast<double>(*level_)) + 60.0) / 60.0, 0.0, 1.0);
  }
  std::string levelText() const {
    if (!level_) return "Nothing playing";
    if (*level_ <= 1e-6F) return "Playing, silent block";
    return format("%.1f dBFS", 20.0 * std::log10(static_cast<double>(*level_)));
  }

  ShellVoiceHost host_;
  std::shared_ptr<const paint::Image> portrait_;
  mutable bool opened_{false};
  Card view_{Card::Resonance};
  std::optional<Card> menu_;
  std::optional<Drag> drag_;
  bool pendingPlay_{false};
  bool useReference_{false};
  std::optional<float> level_;
  std::size_t fricationSelection_{0U};
  std::size_t noiseScroll_{0U};
  double scrollAccumulator_{0.0};
  std::string message_;
  // What a finished background open or save reports.
  std::string fileDone_;
  std::string focusRequest_;
  ui::Rect lastArea_;
  NativeEditorController* controller_{nullptr};
};

// ---- Paint --------------------------------------------------------------------------------------

void VoiceWorkspaceImpl::paintKnob(Canvas2D& c, const DesignTokens& t, ui::Rect cell,
                                   std::string_view label, std::string_view value, double fraction,
                                   bool enabled, bool active) const {
  if (!usable(cell)) return;
  const auto labelRect = ui::Rect{cell.x + 2.0, cell.y + 2.0, std::max(0.0, cell.width - 4.0), 14.0};
  c.text(labelRect, label,
         fitted(c, label, style(FontRole::UiSemibold, t.type.smallLabel, 1.0, TextAlign::Center, true),
                labelRect.width),
         active ? t.color.accent : t.color.textSecondary);
  const auto valueRect = ui::Rect{cell.x + 2.0, cell.bottom() - 16.0, std::max(0.0, cell.width - 4.0), 15.0};
  const auto radius = std::clamp(std::min(cell.width * 0.5 - 6.0, (valueRect.y - labelRect.bottom()) * 0.5 - 2.0), 6.0, 26.0);
  const ui::Point center{cell.x + cell.width * 0.5, labelRect.bottom() + (valueRect.y - labelRect.bottom()) * 0.5};
  constexpr auto start = 0.75 * kPi;
  constexpr auto sweep = 1.5 * kPi;
  Path track;
  track.arc(center, radius, start, sweep);
  c.stroke(track, t.color.knobTrack, StrokeStyle{2.5});
  if (enabled && fraction > 1e-4) {
    Path arc;
    arc.arc(center, radius, start, sweep * fraction);
    c.save();
    c.setGlow(withAlpha(t.color.accent, 0.9), 5.0);
    c.stroke(arc, t.color.accent, StrokeStyle{2.5});
    c.restore();
  }
  c.save();
  c.setAlpha(enabled ? 1.0 : 0.4);
  c.fill(Path::circle(center, std::max(2.0, radius - 4.0)),
         RadialGradient{{center.x - 3.0, center.y - 4.0}, radius,
                        {{0.0, t.color.knobBodyInner}, {1.0, t.color.knobBodyOuter}}});
  const auto angle = start + sweep * fraction;
  Path pointer;
  pointer.moveTo({center.x + std::cos(angle) * radius * 0.35, center.y + std::sin(angle) * radius * 0.35})
      .lineTo({center.x + std::cos(angle) * (radius - 3.0), center.y + std::sin(angle) * (radius - 3.0)});
  c.stroke(pointer, t.color.knobPointer, StrokeStyle{1.6});
  c.restore();
  c.text(valueRect, value,
         fitted(c, value, style(FontRole::UiMedium, t.type.label, 0.0, TextAlign::Center), valueRect.width),
         enabled ? t.color.textPrimary : t.color.textDisabled);
}

void VoiceWorkspaceImpl::paintHonest(Canvas2D& c, const DesignTokens& t, ui::Rect area) const {
  const auto panel = inset(area, 12.0, 12.0);
  c.text({panel.x + 16.0, panel.y + 16.0, panel.width - 32.0, 24.0}, "Voice",
         style(FontRole::UiSemibold, t.type.panelTitle, t.type.panelTitleTracking, TextAlign::Left, true),
         t.color.textPrimary);
  c.text({panel.x + 16.0, panel.y + 48.0, panel.width - 32.0, 22.0}, host_.unavailable,
         fitted(c, host_.unavailable, style(FontRole::Ui, t.type.body), panel.width - 32.0),
         t.color.textSecondary);
  const auto b = honestButton(area);
  button(c, t, b, "Open voice browser", true);
}

void VoiceWorkspaceImpl::paintHero(Canvas2D& c, const DesignTokens& t, const VoiceLayout& l) const {
  const auto ring = l.ring;
  if (!usable(ring)) return;
  const ui::Point center{ring.x + ring.width * 0.5, ring.y + ring.height * 0.5};
  const auto radius = ring.width * 0.5;
  const auto small = ring.width < 60.0;
  const auto inner = small ? radius - 4.0 : radius - 14.0;
  c.fill(Path::circle(center, inner),
         RadialGradient{center, inner, {{0.0, t.color.surfaceRaised}, {1.0, t.color.surfaceSunken}}});
  c.stroke(Path::circle(center, inner), withAlpha(t.color.accent, playing() ? 0.9 : 0.45), StrokeStyle{1.2});
  if (portrait_) {
    c.save();
    c.clipPath(Path::circle(center, inner));
    c.drawImage(*portrait_, {center.x - inner, center.y - inner, inner * 2.0, inner * 2.0},
                playing() ? 1.0 : 0.7);
    c.restore();
  }
  // The ring is the measured audition level; nothing lights while nothing plays.
  const auto lit = levelFraction();
  const auto ticks = small ? 24 : 64;
  for (int i = 0; i < ticks; ++i) {
    const auto a = -kPi / 2.0 + i * 2.0 * kPi / ticks;
    const auto on = static_cast<double>(i) < lit * ticks;
    auto color = t.mode == DesignMode::Scene ? t.trackColors[static_cast<std::size_t>(i / 3) % 4U] : t.color.accent;
    color = withAlpha(color, on ? 1.0 : 0.16);
    Path tick;
    const auto outer = radius - 1.0;
    const auto innerTick = small ? radius - 3.5 : radius - 10.0;
    tick.moveTo({center.x + std::cos(a) * innerTick, center.y + std::sin(a) * innerTick})
        .lineTo({center.x + std::cos(a) * outer, center.y + std::sin(a) * outer});
    c.save();
    if (on) c.setGlow(withAlpha(color, 0.9), 5.0);
    c.stroke(tick, color, StrokeStyle{t.mode == DesignMode::Scene ? 2.6 : 1.4, false});
    c.restore();
  }
  if (usable(l.heroCaption)) {
    const auto state = singerState();
    c.text({l.heroCaption.x, l.heroCaption.y, l.heroCaption.width, 18.0}, state,
           style(FontRole::UiSemibold, t.type.label, 1.6, TextAlign::Center, true),
           playing() ? t.color.accent : t.color.textSecondary);
    c.text({l.heroCaption.x, l.heroCaption.y + 20.0, l.heroCaption.width, 16.0}, levelText(),
           style(FontRole::Ui, t.type.smallLabel, 0.0, TextAlign::Center), t.color.textSecondary);
  }
}

void VoiceWorkspaceImpl::paintSource(Canvas2D& c, const DesignTokens& t, const VoiceLayout& l,
                                     const VoiceRecipe* r) const {
  if (usable(l.knobCaption[0]))
    c.text(l.knobCaption[0], "Phonation", style(FontRole::UiSemibold, t.type.smallLabel, 1.4, TextAlign::Left, true),
           t.color.textSecondary);
  if (usable(l.knobCaption[1]))
    c.text(l.knobCaption[1], "Modulation", style(FontRole::UiSemibold, t.type.smallLabel, 1.4, TextAlign::Left, true),
           t.color.textSecondary);
  for (std::size_t i = 0U; i < kSourceKnobs.size(); ++i) {
    const auto& spec = kSourceKnobs[i];
    const auto value = r != nullptr ? sourceValue(*r, i) : spec.minimum;
    paintKnob(c, t, l.knob[i], spec.label, r != nullptr ? sourceText(i, value) : "\u2014",
              (value - spec.minimum) / (spec.maximum - spec.minimum), r != nullptr,
              drag_ && drag_->target == Target::Source && drag_->index == i);
  }
}

void VoiceWorkspaceImpl::paintResonance(Canvas2D& c, const DesignTokens& t, const VoiceLayout& l,
                                        const VoiceRecipe* r) const {
  const auto p = pose();
  if (r != nullptr && p < r->poses.size()) {
    const auto chips = layoutChips(l.chipStrip, *r, p);
    for (const auto& [index, rect] : chips.chips) {
      const auto selected = index == p;
      if (selected) {
        c.save();
        c.setGlow(withAlpha(t.color.accent, 0.6), 6.0);
        c.fill(Path::capsule(rect), withAlpha(t.color.accent, 0.22));
        c.restore();
      }
      c.stroke(Path::capsule(rect), selected ? t.color.accent : withAlpha(t.color.border, 0.9), StrokeStyle{1.0});
      const auto label = poseLabel(r->poses[index]);
      c.text(rect, label, fitted(c, label, style(FontRole::UiSemibold, t.type.label, 0.4, TextAlign::Center), rect.width - 6.0),
             selected ? t.color.accent : t.color.textPrimary);
    }
    if (usable(chips.previous)) button(c, t, chips.previous, "\u2039", p > 0U);
    if (usable(chips.next)) button(c, t, chips.next, "\u203A", p + 1U < r->poses.size());
    button(c, t, chips.add, "+", true);
  } else {
    c.text(l.chipStrip, "No poses", style(FontRole::Ui, t.type.label), t.color.textDisabled);
  }
  // The spectral envelope editor.
  sunken(c, t, l.envelope, 6.0);
  const auto plot = l.plot;
  if (!usable(plot)) return;
  const auto labels = l.envelope.height >= 72.0;
  c.save();
  c.clipRect(l.envelope);
  for (const double hz : {100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0}) {
    const auto x = hzToX(plot, hz);
    Path line;
    line.moveTo({x, plot.y}).lineTo({x, plot.bottom()});
    c.stroke(line, withAlpha(t.color.gridWeak, 0.8), StrokeStyle{1.0});
    if (labels)
      c.text({x - 20.0, plot.bottom() + 1.0, 40.0, 14.0}, hz >= 1000.0 ? format("%.0fk", hz / 1000.0) : format("%.0f", hz),
             style(FontRole::Ui, t.type.rulerMicro, 0.0, TextAlign::Center), t.color.textSecondary);
  }
  for (const double db : {-36.0, -24.0, -12.0, 0.0, 12.0}) {
    const auto y = dbToY(plot, db);
    Path line;
    line.moveTo({plot.x, y}).lineTo({plot.right(), y});
    c.stroke(line, withAlpha(db == 0.0 ? t.color.gridStrong : t.color.gridWeak, 0.8), StrokeStyle{1.0});
    if (labels)
      c.text({l.envelope.x + 2.0, y - 7.0, 30.0, 14.0}, format("%+.0f", db),
             style(FontRole::Ui, t.type.rulerMicro, 0.0, TextAlign::Right), t.color.textSecondary);
  }
  if (r != nullptr && p < r->poses.size()) {
    const auto& shownPose = r->poses[p];
    Path curve;
    Path fill;
    const auto steps = std::max(16, static_cast<int>(plot.width / 2.0));
    for (int i = 0; i <= steps; ++i) {
      const auto x = plot.x + plot.width * static_cast<double>(i) / static_cast<double>(steps);
      const auto y = dbToY(plot, responseDb(shownPose, xToHz(plot, x)));
      if (i == 0) {
        curve.moveTo({x, y});
        fill.moveTo({x, plot.bottom()}).lineTo({x, y});
      } else {
        curve.lineTo({x, y});
        fill.lineTo({x, y});
      }
    }
    fill.lineTo({plot.right(), plot.bottom()}).close();
    c.fill(fill, LinearGradient{{plot.x, plot.y}, {plot.x, plot.bottom()},
                                {{0.0, withAlpha(t.color.accentCurve, 0.30)}, {1.0, withAlpha(t.color.accentCurve, 0.02)}}});
    c.save();
    c.setGlow(withAlpha(t.color.accentCurve, 0.8), 6.0);
    c.stroke(curve, t.color.accentCurve, StrokeStyle{2.0});
    c.restore();
    for (const auto& h : layoutHandles(plot, shownPose)) {
      const auto& band = shownPose.formants[h.band];
      const auto active = drag_ && drag_->target == Target::Formant && drag_->index == h.band;
      // The bandwidth as a bar across the handle.
      Path bw;
      bw.moveTo({hzToX(plot, std::max(kMinHz, band.frequencyHz - band.bandwidthHz * 0.5)), h.center.y})
          .lineTo({hzToX(plot, std::min(kMaxHz, band.frequencyHz + band.bandwidthHz * 0.5)), h.center.y});
      c.stroke(bw, withAlpha(t.color.accent, active ? 0.9 : 0.5), StrokeStyle{3.0});
      c.save();
      c.setGlow(withAlpha(t.color.accent, active ? 1.0 : 0.6), active ? 10.0 : 6.0);
      c.fill(Path::circle(h.center, kHandleRadius), active ? t.color.accent : t.color.surfaceRaised);
      c.restore();
      c.stroke(Path::circle(h.center, kHandleRadius), t.color.accent, StrokeStyle{1.6});
      c.text({h.center.x - 14.0, h.center.y - kHandleRadius - 15.0, 28.0, 13.0}, "F" + std::to_string(h.band + 1U),
             style(FontRole::UiSemibold, t.type.rulerMicro, 0.4, TextAlign::Center), t.color.textPrimary);
    }
  } else {
    c.text(plot, "No voice recipe is open", style(FontRole::Ui, t.type.label, 0.0, TextAlign::Center), t.color.textDisabled);
  }
  c.restore();
  const auto nasal = r != nullptr && p < r->poses.size() ? r->poses[p].nasalCoupling : 0.0;
  paintKnob(c, t, l.nasal, kNasalKnob.label, r != nullptr ? format("%.2f", nasal) : "\u2014", nasal,
            r != nullptr, drag_ && drag_->target == Target::Nasal);
}

void VoiceWorkspaceImpl::paintNoise(Canvas2D& c, const DesignTokens& t, const VoiceLayout& l,
                                    const VoiceRecipe* r) const {
  if (usable(l.list)) {
    const ui::Rect head{l.list.x, l.list.y, l.list.width, 14.0};
    const auto labelWidth = std::min(48.0, l.list.width * 0.2);
    const auto sliderWidth = std::max(0.0, (l.list.width - labelWidth - 12.0) / 3.0);
    for (std::size_t f = 0U; f < 3U; ++f)
      c.text({head.x + labelWidth + 4.0 + (sliderWidth + 4.0) * static_cast<double>(f), head.y, sliderWidth, head.height},
             kNoiseFieldLabels[f], style(FontRole::UiSemibold, t.type.rulerMicro, 1.0, TextAlign::Left, true),
             t.color.textSecondary);
  }
  const auto selected = selectedFrication();
  if (r == nullptr) {
    c.text({l.list.x, l.list.y + 18.0, l.list.width, 20.0}, "No voice recipe is open", style(FontRole::Ui, t.type.label),
           t.color.textDisabled);
  } else {
    const auto indices = fricationsFor(*r, pose());
    if (indices.empty())
      c.text({l.list.x, l.list.y + 18.0, l.list.width, 20.0}, "No frication for this style", style(FontRole::Ui, t.type.label),
             t.color.textDisabled);
    for (const auto& row : layoutNoiseRows(l.list, indices, noiseScroll_)) {
      const auto& source = r->frications[row.index];
      const auto isSelected = selected == row.index;
      if (isSelected) c.fill(Path::roundedRect(row.row, 5.0), withAlpha(t.color.accent, 0.12));
      c.text(inset(row.label, 4.0, 0.0), source.phone, style(FontRole::UiSemibold, t.type.label),
             isSelected ? t.color.accent : t.color.textPrimary);
      for (std::size_t f = 0U; f < 3U; ++f) {
        const auto cell = row.slider[f];
        const auto value = noiseValue(source.source, f);
        const auto text = noiseText(f, value);
        c.text({cell.x, cell.y, cell.width, 15.0}, text, fitted(c, text, style(FontRole::UiMedium, t.type.smallLabel), cell.width),
               t.color.textPrimary);
        const ui::Rect track{cell.x, cell.bottom() - 7.0, cell.width, 4.0};
        c.fill(Path::capsule(track), t.color.knobTrack);
        const auto fraction = noiseFraction(f, value);
        c.fill(Path::capsule({track.x, track.y, std::max(4.0, track.width * fraction), track.height}), t.color.accent);
        c.fill(Path::circle({track.x + track.width * fraction, track.y + 2.0}, 4.0), t.color.knobPointer);
      }
    }
  }
  c.text(l.seedCaption, "Seed", style(FontRole::UiSemibold, t.type.smallLabel, 1.2, TextAlign::Left, true), t.color.textSecondary);
  sunken(c, t, l.seed, 5.0);
  const auto seedText = r != nullptr && selected ? std::to_string(r->frications[*selected].source.seed) : std::string{"\u2014"};
  c.text(inset(l.seed, 8.0, 0.0), seedText, fitted(c, seedText, style(FontRole::Mono, t.type.label), l.seed.width - 16.0),
         selected ? t.color.textPrimary : t.color.textDisabled);
}

void VoiceWorkspaceImpl::paintOutput(Canvas2D& c, const DesignTokens& t, const VoiceLayout& l) const {
  auto* s = session();
  const auto ready = s != nullptr && s->model() != nullptr;
  if (!ready && l.mode == Mode::Tabbed) {
    button(c, t, l.newButton, "New voice", s != nullptr);
    button(c, t, l.openButton, "Open\u2026", s != nullptr);
    c.text(l.caption, message_.empty() ? std::string{"No voice recipe is open"} : message_,
           style(FontRole::Ui, t.type.smallLabel), t.color.textSecondary);
    return;
  }
  const auto label = playing() ? "Stop" : (s != nullptr && s->auditionBusy()) ? "Rendering" : "Play";
  button(c, t, l.play, label, ready && static_cast<bool>(host_.play), playing());
  button(c, t, l.abA, "A", ready && s->auditionReference().has_value(), useReference_);
  button(c, t, l.abB, "B", ready, !useReference_);
  if (!usable(l.level)) return;
  const auto status = !host_.play ? host_.playUnavailable : auditionStatus();
  c.text({l.level.x + 8.0, l.level.y, l.level.width - 8.0, l.level.height - 8.0}, status,
         fitted(c, status, style(FontRole::Ui, t.type.smallLabel), l.level.width - 8.0), t.color.textSecondary);
  const ui::Rect meter{l.level.x + 8.0, l.level.bottom() - 6.0, std::max(0.0, l.level.width - 8.0), 4.0};
  c.fill(Path::capsule(meter), t.color.knobTrack);
  const auto fraction = levelFraction();
  if (fraction > 0.0)
    c.fill(Path::capsule({meter.x, meter.y, std::max(4.0, meter.width * fraction), meter.height}),
           fraction > 0.95 ? t.color.meterHigh : fraction > 0.8 ? t.color.meterMid : t.color.meterLow);
}

void VoiceWorkspaceImpl::paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController& controller,
                               const EditorSceneState& state, ui::Rect area) const {
  static_cast<void>(controller);
  static_cast<void>(state);
  c.fill(Path::roundedRect(area, t.shape.card), t.color.canvas);
  glassPanel(c, t, area, t.shape.card);
  auto* s = session();
  if (s == nullptr) {
    paintHonest(c, t, area);
    return;
  }
  const auto l = layoutFor(area);
  const auto* r = recipe();
  if (l.mode != Mode::Tabbed) {
    c.text(l.identity, recipeIdentity(),
           fitted(c, recipeIdentity(), style(FontRole::UiSemibold, t.type.body), l.identity.width),
           t.color.textPrimary);
    if (r == nullptr) {
      button(c, t, l.newButton, "New starter voice", !s->busy());
      button(c, t, l.openButton, "Open\u2026", !s->busy());
    } else if (!message_.empty()) {
      const ui::Rect line{l.newButton.x, l.header.y, l.header.right() - l.newButton.x, l.header.height};
      c.text(line, message_, fitted(c, message_, style(FontRole::Ui, t.type.smallLabel, 0.0, TextAlign::Right), line.width),
             t.color.textSecondary);
    }
    paintHero(c, t, l);
    if (usable(l.rail)) {
      // The signal rail: lit when the audition B is rendered for the recipe as it is now.
      const auto current = s->auditionAudio() != nullptr;
      const auto color = current ? t.color.accent : withAlpha(t.color.textDisabled, 0.8);
      Path rail;
      rail.moveTo({l.rail.x, l.rail.y}).lineTo({l.rail.right(), l.rail.y});
      c.save();
      if (current) c.setGlow(withAlpha(t.color.accent, 0.7), 6.0);
      c.stroke(rail, color, StrokeStyle{1.5, true, s->auditionBusy() ? std::vector<double>{6.0, 5.0} : std::vector<double>{}});
      for (std::size_t i = 0U; i < 3U; ++i) {
        const auto x = l.card[i].x + l.card[i].width * 0.5;
        Path stub;
        stub.moveTo({x, l.card[i].bottom()}).lineTo({x, l.rail.y});
        c.stroke(stub, color, StrokeStyle{1.5});
        c.fill(Path::circle({x, l.rail.y}, 4.0), color);
      }
      // Into OUTPUT's port at its header.
      Path port;
      port.moveTo({l.card[3].x, l.rail.y}).lineTo({l.card[3].x, l.card[3].y + 16.0});
      c.stroke(port, color, StrokeStyle{1.5});
      c.fill(Path::circle({l.card[3].x, l.card[3].y + 16.0}, 4.0), color);
      c.restore();
    }
  } else {
    for (std::size_t i = 0U; i < l.view.size(); ++i) {
      const auto active = static_cast<std::size_t>(view_) == i;
      const auto r0 = l.view[i];
      if (active) c.fill(Path::roundedRect(r0, 6.0), withAlpha(t.color.accent, 0.2));
      c.stroke(Path::roundedRect(r0, 6.0), active ? t.color.accent : withAlpha(t.color.border, 0.9), StrokeStyle{1.0});
      c.text(r0, kCardTitles[i], fitted(c, kCardTitles[i], style(FontRole::UiSemibold, t.type.smallLabel, 1.0, TextAlign::Center, true), r0.width - 8.0),
             active ? t.color.accent : t.color.textSecondary);
    }
    if (l.identity.width >= 40.0)
      c.text(l.identity, recipeIdentity(), fitted(c, recipeIdentity(), style(FontRole::Ui, t.type.smallLabel), l.identity.width),
             t.color.textSecondary);
    paintHero(c, t, l);
  }
  for (std::size_t i = 0U; i < 4U; ++i) {
    if (!usable(l.card[i])) continue;
    if (l.mode != Mode::Tabbed) {
      glassPanel(c, t, l.card[i], t.shape.card);
      const auto lit = i == 3U ? playing() : r != nullptr;
      c.save();
      if (lit) c.setGlow(t.color.accent, 7.0);
      c.fill(Path::circle({l.card[i].x + 16.0, l.card[i].y + 16.0}, 3.5), lit ? t.color.accent : t.color.textDisabled);
      c.restore();
      c.text({l.card[i].x + 26.0, l.card[i].y + 6.0, l.card[i].width - 70.0, 20.0}, kCardTitles[i],
             style(FontRole::UiSemibold, t.type.panelTitle, t.type.panelTitleTracking, TextAlign::Left, true),
             t.color.textPrimary);
    } else if (i == 3U) {
      sunken(c, t, l.card[i], 8.0);
    }
    overflowButton(c, t, l.more[i], menu_ && static_cast<std::size_t>(*menu_) == i);
  }
  if (cardShown(l, Card::Source)) paintSource(c, t, l, r);
  if (cardShown(l, Card::Resonance)) paintResonance(c, t, l, r);
  if (cardShown(l, Card::Noise)) paintNoise(c, t, l, r);
  paintOutput(c, t, l);
  if (menu_) {
    const auto items = menuFor(*menu_);
    const auto rows = menuRows(area, l.more[static_cast<std::size_t>(*menu_)], items.size());
    c.save();
    c.setGlow(withAlpha(t.color.accent, 0.28), 12.0);
    c.fill(Path::roundedRect(rows[0], 9.0), t.color.surfaceRaised);
    c.restore();
    c.stroke(Path::roundedRect(rows[0], 9.0), t.color.borderStrong, StrokeStyle{1.0});
    for (std::size_t i = 0U; i < items.size(); ++i)
      c.text(inset(rows[i + 1U], 10.0, 0.0), items[i].label, style(FontRole::UiSemibold, t.type.label),
             actionEnabled(items[i].action) ? t.color.textPrimary : t.color.textDisabled);
  }
}

// ---- Input --------------------------------------------------------------------------------------

core::Result<void> VoiceWorkspaceImpl::pointerDown(NativeEditorController& controller,
                                                   const PointerEvent& event, ui::Rect area) {
  focusRequest_.clear();
  controller_ = &controller;
  lastArea_ = area;
  if (event.button != PointerButton::Left || drag_) return core::success();
  const auto p = event.position;
  auto* s = session();
  if (s == nullptr) {
    if (!contains(honestButton(area), p)) return core::success();
    focusRequest_ = "shell.voice.browser";
    controller.showVoicebankBrowser();
    return core::success();
  }
  if (menu_) return pressMenu(p, area);
  const auto l = layoutFor(area);
  for (std::size_t i = 0U; i < 4U; ++i) {
    if (!usable(l.more[i]) || !contains(l.more[i], p)) continue;
    focusRequest_ = cardId(i) + ".more";
    menu_ = static_cast<Card>(i);
    return core::success();
  }
  if (l.mode == Mode::Tabbed) {
    for (std::size_t i = 0U; i < l.view.size(); ++i) {
      if (!contains(l.view[i], p)) continue;
      focusRequest_ = std::string{"shell.voice.view."} + kCardIds[i];
      view_ = static_cast<Card>(i);
      return core::success();
    }
  }
  const auto* r = recipe();
  if (r == nullptr) {
    if (contains(l.newButton, p)) {
      focusRequest_ = "shell.voice.new";
      return run(controller, Action::New);
    }
    if (contains(l.openButton, p)) {
      focusRequest_ = "shell.voice.open";
      return run(controller, Action::Open);
    }
    return core::success();
  }
  if (contains(l.play, p)) {
    focusRequest_ = "shell.voice.play";
    return play();
  }
  if (contains(l.abA, p) || contains(l.abB, p)) {
    useReference_ = contains(l.abA, p);
    focusRequest_ = useReference_ ? "shell.voice.ab.a" : "shell.voice.ab.b";
    if (playing() && host_.stop) host_.stop();
    level_.reset();
    return play();
  }
  if (cardShown(l, Card::Source)) {
    for (std::size_t i = 0U; i < l.knob.size(); ++i) {
      if (!contains(l.knob[i], p)) continue;
      focusRequest_ = std::string{"shell.voice.knob."} + kSourceKnobs[i].id;
      return beginDrag(Target::Source, i, 0U, p, event.modifiers.shift);
    }
  }
  if (cardShown(l, Card::Resonance)) {
    const auto current = pose();
    const auto chips = layoutChips(l.chipStrip, *r, current);
    for (const auto& [index, rect] : chips.chips) {
      if (!contains(rect, p)) continue;
      focusRequest_ = "shell.voice.pose." + std::to_string(index);
      return selectPose(index);
    }
    if (usable(chips.previous) && contains(chips.previous, p)) {
      focusRequest_ = "shell.voice.pose.previous";
      return current > 0U ? selectPose(current - 1U) : core::success();
    }
    if (usable(chips.next) && contains(chips.next, p)) {
      focusRequest_ = "shell.voice.pose.next";
      return current + 1U < r->poses.size() ? selectPose(current + 1U) : core::success();
    }
    if (contains(chips.add, p)) {
      focusRequest_ = "shell.voice.pose.add";
      return run(controller, Action::DuplicatePose);
    }
    if (current < r->poses.size()) {
      // The nearest handle under the press, so overlapping hit areas pick the closer formant.
      std::optional<Handle> best;
      double bestDistance = 0.0;
      for (const auto& h : layoutHandles(l.plot, r->poses[current])) {
        if (!contains(h.hit, p)) continue;
        const auto dx = h.center.x - p.x;
        const auto dy = h.center.y - p.y;
        const auto distance = dx * dx + dy * dy;
        if (!best || distance < bestDistance) {
          best = h;
          bestDistance = distance;
        }
      }
      if (best) {
        focusRequest_ = "shell.voice.formant." + std::to_string(best->band + 1U);
        return beginDrag(Target::Formant, best->band, event.modifiers.shift ? 1U : 0U, p, false);
      }
    }
    if (contains(l.envelope, p)) {
      focusRequest_ = "shell.voice.envelope";
      return core::success();
    }
    if (contains(l.nasal, p)) {
      focusRequest_ = "shell.voice.nasal";
      return beginDrag(Target::Nasal, 0U, 0U, p, event.modifiers.shift);
    }
  }
  if (cardShown(l, Card::Noise)) {
    for (const auto& row : layoutNoiseRows(l.list, fricationsFor(*r, pose()), noiseScroll_)) {
      for (std::size_t f = 0U; f < 3U; ++f) {
        if (!contains(row.slider[f], p)) continue;
        fricationSelection_ = row.index;
        focusRequest_ = "shell.voice.frication." + std::to_string(row.index) + "." + kNoiseFieldIds[f];
        auto begun = beginDrag(Target::Noise, row.index, f, p, false);
        if (!begun) return begun;
        return dragTo(p);
      }
      if (contains(row.row, p)) {
        fricationSelection_ = row.index;
        focusRequest_ = "shell.voice.frication." + std::to_string(row.index);
        return core::success();
      }
    }
    if (contains(l.seed, p)) {
      focusRequest_ = "shell.voice.frication.seed";
      return run(controller, Action::FricationSeed);
    }
  }
  return core::success();
}

bool VoiceWorkspaceImpl::scroll(NativeEditorController& controller, ui::Point anchor, double deltaX,
                                double deltaY, ui::Rect area) {
  static_cast<void>(controller);
  static_cast<void>(deltaX);
  if (drag_ || menu_ || recipe() == nullptr) return false;
  const auto l = layoutFor(area);
  lastArea_ = area;
  const auto notches = [&]() {
    scrollAccumulator_ += deltaY;
    const auto steps = static_cast<int>(scrollAccumulator_ / kScrollStep);
    scrollAccumulator_ -= steps * kScrollStep;
    return steps;
  };
  if (cardShown(l, Card::Source))
    for (std::size_t i = 0U; i < l.knob.size(); ++i)
      if (contains(l.knob[i], anchor)) {
        if (const auto steps = notches(); steps != 0) static_cast<void>(step(Target::Source, i, 0U, -steps));
        return true;
      }
  if (cardShown(l, Card::Resonance) && contains(l.nasal, anchor)) {
    if (const auto steps = notches(); steps != 0) static_cast<void>(step(Target::Nasal, 0U, 0U, -steps));
    return true;
  }
  if (cardShown(l, Card::Noise) && contains(l.list, anchor)) {
    if (const auto steps = notches(); steps != 0) {
      const auto count = fricationsFor(*recipe(), pose()).size();
      const auto next = static_cast<long long>(noiseScroll_) + steps;
      noiseScroll_ = static_cast<std::size_t>(std::clamp<long long>(next, 0, static_cast<long long>(count)));
    }
    return true;
  }
  return false;
}

core::Result<void> VoiceWorkspaceImpl::run(NativeEditorController& controller, Action action) {
  static_cast<void>(controller);
  auto* s = session();
  if (s == nullptr) return unavailable();
  if (!actionEnabled(action))
    return note(core::failure(core::ErrorCode::InvalidState, "That action is not available for this voice now"));
  const auto dialogMissing = [] {
    return core::failure(core::ErrorCode::Unsupported, "This host has no dialog for that choice");
  };
  const auto replaceAllowed = [&]() -> core::Result<bool> {
    if (s->model() == nullptr || !s->model()->dirty()) return true;
    if (!host_.confirmDiscard)
      return core::failure<bool>(core::ErrorCode::Conflict, "Save or discard the current voice first");
    return host_.confirmDiscard();
  };
  const auto epoch = s->epoch();
  const auto revision = s->model() != nullptr ? s->model()->revision() : 0U;
  switch (action) {
    case Action::New: {
      const auto allowed = replaceAllowed();
      if (!allowed) return note(core::Result<void>{allowed.error()});
      if (!allowed.value()) return core::success();
      auto created = s->createJapaneseStarter(true);
      if (created) message_ = "New voice from the Japanese starter (screening defaults, unqualified)";
      return note(std::move(created));
    }
    case Action::Open: {
      if (!host_.choosePath) return note(dialogMissing());
      const auto chosen = host_.choosePath(false, s->path());
      if (!chosen) return note(core::Result<void>{chosen.error()});
      if (!chosen.value()) return core::success();
      const auto allowed = replaceAllowed();
      if (!allowed) return note(core::Result<void>{allowed.error()});
      if (!allowed.value()) return core::success();
      auto opened = s->beginOpen(*chosen.value(), true);
      if (opened) {
        message_ = "Opening " + chosen.value()->filename().string() + "\u2026";
        fileDone_ = "Opened " + chosen.value()->filename().string();
      }
      return note(std::move(opened));
    }
    case Action::Save:
      if (!s->path().empty()) {
        auto saved = s->beginSave(s->path());
        if (saved) {
          message_ = "Saving\u2026";
          fileDone_ = "Saved " + s->path().filename().string();
        }
        return note(std::move(saved));
      }
      [[fallthrough]];
    case Action::SaveAs: {
      if (!host_.choosePath) return note(dialogMissing());
      const auto chosen = host_.choosePath(true, s->path());
      if (!chosen) return note(core::Result<void>{chosen.error()});
      if (!chosen.value()) return core::success();
      if (s->epoch() != epoch || s->model() == nullptr || s->model()->revision() != revision)
        return note(core::failure(core::ErrorCode::Conflict, "The voice changed while choosing a file"));
      auto saved = s->beginSave(*chosen.value());
      if (saved) {
        message_ = "Saving " + chosen.value()->filename().string() + "\u2026";
        fileDone_ = "Saved " + chosen.value()->filename().string();
      }
      return note(std::move(saved));
    }
    case Action::Seed: {
      if (!host_.chooseSeed) return note(dialogMissing());
      const auto seed = host_.chooseSeed(std::to_string(s->model()->recipe().seed), false);
      if (!seed) return note(core::Result<void>{seed.error()});
      if (!seed.value()) return core::success();
      return note(s->setSeed(epoch, revision, *seed.value()));
    }
    case Action::DuplicatePose: {
      if (!host_.choosePoseIdentity) return note(dialogMissing());
      const auto source = s->auditionPose();
      const auto identity = host_.choosePoseIdentity(false);
      if (!identity) return note(core::Result<void>{identity.error()});
      if (!identity.value()) return core::success();
      return note(s->duplicatePose(epoch, revision, identity.value()->first, identity.value()->second, source));
    }
    case Action::RemovePose: return note(s->removePose(epoch, revision));
    case Action::AddFrication: {
      if (!host_.choosePoseIdentity) return note(dialogMissing());
      const auto identity = host_.choosePoseIdentity(true);
      if (!identity) return note(core::Result<void>{identity.error()});
      if (!identity.value()) return core::success();
      auto added = s->addFrication(epoch, revision, identity.value()->first, identity.value()->second);
      if (added && s->model() != nullptr && !s->model()->recipe().frications.empty())
        fricationSelection_ = s->model()->recipe().frications.size() - 1U;
      return note(std::move(added));
    }
    case Action::RemoveFrication: return note(s->removeFrication(epoch, revision, *selectedFrication()));
    case Action::FricationSeed: {
      if (!host_.chooseSeed) return note(dialogMissing());
      const auto index = *selectedFrication();
      const auto seed = host_.chooseSeed(std::to_string(s->model()->recipe().frications[index].source.seed), true);
      if (!seed) return note(core::Result<void>{seed.error()});
      if (!seed.value()) return core::success();
      return note(s->setFricationSeed(epoch, revision, index, *seed.value()));
    }
    case Action::PinReference: {
      auto pinned = s->pinAuditionReference(epoch, revision);
      if (pinned) message_ = "B kept as reference A";
      return note(std::move(pinned));
    }
    case Action::ClearReference:
      s->clearAuditionReference();
      useReference_ = false;
      message_ = "Reference A cleared";
      return core::success();
    case Action::Stop:
      if (host_.stop) host_.stop();
      level_.reset();
      message_ = "Stopped";
      return core::success();
  }
  return core::success();
}

// ---- Accessibility ------------------------------------------------------------------------------

SemanticNode knobNode(std::string id, std::string name, ui::Rect bounds, bool enabled, double value,
                      const KnobSpec& spec, std::string text, std::string description) {
  return SemanticNode{
      .id = std::move(id), .role = SemanticRole::Slider, .name = std::move(name),
      .value = enabled ? std::move(text) : std::string{"No voice recipe is open"}, .bounds = bounds,
      .enabled = enabled,
      .actions = enabled ? std::vector<SemanticAction>{SemanticAction::Increment, SemanticAction::Decrement,
                                                       SemanticAction::SetFocus}
                         : std::vector<SemanticAction>{SemanticAction::SetFocus},
      .description = std::move(description),
      .numericValue = value, .numericMinimum = spec.minimum, .numericMaximum = spec.maximum,
      .numericStep = spec.step};
}

void VoiceWorkspaceImpl::semantics(const NativeEditorController& controller, const EditorSceneState& state,
                                   ui::Rect area, std::vector<SemanticNode>& out) const {
  static_cast<void>(controller);
  static_cast<void>(state);
  const auto focusable = std::vector<SemanticAction>{SemanticAction::SetFocus};
  const auto pressable = std::vector<SemanticAction>{SemanticAction::Activate, SemanticAction::SetFocus};
  auto* s = session();
  if (s == nullptr) {
    out.push_back(SemanticNode{.id = "shell.voice.unavailable", .role = SemanticRole::Status, .name = "Voice design",
                   .value = host_.unavailable, .bounds = inset(area, 12.0, 12.0), .actions = focusable});
    out.push_back(SemanticNode{.id = "shell.voice.browser", .role = SemanticRole::Button, .name = "Open voice browser",
                   .bounds = honestButton(area), .actions = pressable,
                   .description = "Chooses an installed voice for the selected track"});
    return;
  }
  const auto l = layoutFor(area);
  if (menu_) {
    // The open menu is modal: its button and its items are all that is published.
    const auto i = static_cast<std::size_t>(*menu_);
    out.push_back(SemanticNode{.id = cardId(i) + ".more", .role = SemanticRole::Button,
                   .name = std::string{kCardTitles[i]} + " actions", .value = "Open", .bounds = l.more[i],
                   .actions = pressable});
    const auto items = menuFor(*menu_);
    const auto rows = menuRows(area, l.more[i], items.size());
    for (std::size_t k = 0U; k < items.size(); ++k) {
      const auto enabled = actionEnabled(items[k].action);
      out.push_back(SemanticNode{.id = std::string{"shell.voice.menu."} + items[k].id, .role = SemanticRole::Button,
                     .name = items[k].label, .bounds = rows[k + 1U], .enabled = enabled,
                     .actions = enabled ? pressable : focusable});
    }
    return;
  }
  const auto* r = recipe();
  const auto p = pose();
  const auto hasPose = r != nullptr && p < r->poses.size();
  if (l.identity.width >= 40.0)
    out.push_back(SemanticNode{.id = "shell.voice.recipe", .role = SemanticRole::Status, .name = "Voice recipe",
                   .value = recipeIdentity(), .bounds = l.identity, .actions = focusable,
                   .description = message_});
  if (r == nullptr) {
    out.push_back(SemanticNode{.id = "shell.voice.new", .role = SemanticRole::Button, .name = "New starter voice",
                   .bounds = l.newButton, .enabled = !s->busy(), .actions = pressable,
                   .description = "Japanese starter recipe: screening defaults, not a qualified singer"});
    out.push_back(SemanticNode{.id = "shell.voice.open", .role = SemanticRole::Button, .name = "Open voice recipe",
                   .bounds = l.openButton, .enabled = !s->busy(), .actions = pressable});
  }
  if (l.mode == Mode::Tabbed) {
    for (std::size_t i = 0U; i < l.view.size(); ++i)
      out.push_back(SemanticNode{.id = std::string{"shell.voice.view."} + kCardIds[i], .role = SemanticRole::Tab,
                     .name = std::string{kCardTitles[i]} + " module", .bounds = l.view[i],
                     .selected = static_cast<std::size_t>(view_) == i, .actions = pressable});
  }
  out.push_back(SemanticNode{.id = "shell.voice.singer", .role = SemanticRole::Status, .name = "Singer",
                 .value = singerState() + ", " + levelText(), .bounds = l.ring, .actions = focusable,
                 .description = "The ring shows the measured audition output level"});
  for (std::size_t i = 0U; i < 4U; ++i) {
    if (!usable(l.card[i])) continue;
    std::string value = r == nullptr ? std::string{"No voice recipe is open"} : std::string{};
    if (r != nullptr && i == 1U && hasPose) value = "Pose " + poseLabel(r->poses[p]);
    if (i == 3U) value = auditionStatus();
    out.push_back(SemanticNode{.id = cardId(i), .role = SemanticRole::Panel, .name = std::string{kCardTitles[i]} + " module",
                   .value = value, .bounds = l.card[i]});
    if (usable(l.more[i]))
      out.push_back(SemanticNode{.id = cardId(i) + ".more", .role = SemanticRole::Button,
                     .name = std::string{kCardTitles[i]} + " actions", .value = "Closed", .bounds = l.more[i],
                     .actions = pressable});
  }
  if (cardShown(l, Card::Source))
    for (std::size_t i = 0U; i < kSourceKnobs.size(); ++i) {
      const auto& spec = kSourceKnobs[i];
      const auto value = r != nullptr ? sourceValue(*r, i) : spec.minimum;
      out.push_back(knobNode(std::string{"shell.voice.knob."} + spec.id, spec.label, l.knob[i], r != nullptr, value, spec,
                             sourceText(i, value), "Drag vertically; Shift for fine steps; Escape cancels"));
    }
  if (cardShown(l, Card::Resonance)) {
    if (hasPose) {
      const auto chips = layoutChips(l.chipStrip, *r, p);
      for (const auto& [index, rect] : chips.chips)
        out.push_back(SemanticNode{.id = "shell.voice.pose." + std::to_string(index), .role = SemanticRole::RadioButton,
                       .name = "Pose " + poseLabel(r->poses[index]), .bounds = rect, .selected = index == p,
                       .actions = pressable});
      if (usable(chips.previous))
        out.push_back(SemanticNode{.id = "shell.voice.pose.previous", .role = SemanticRole::Button, .name = "Previous pose",
                       .bounds = chips.previous, .enabled = p > 0U, .actions = pressable});
      if (usable(chips.next))
        out.push_back(SemanticNode{.id = "shell.voice.pose.next", .role = SemanticRole::Button, .name = "Next pose",
                       .bounds = chips.next, .enabled = p + 1U < r->poses.size(), .actions = pressable});
      out.push_back(SemanticNode{.id = "shell.voice.pose.add", .role = SemanticRole::Button, .name = "Duplicate pose",
                     .bounds = chips.add, .actions = pressable,
                     .description = "Copies the selected pose under a new phone and style"});
    }
    SemanticNode envelope{.id = "shell.voice.envelope", .role = SemanticRole::Lane, .name = "Spectral envelope",
                          .value = hasPose ? std::to_string(r->poses[p].formants.size()) + " resonances, 80 Hz to 8 kHz"
                                           : std::string{"No voice recipe is open"},
                          .bounds = l.plot, .enabled = hasPose, .actions = focusable,
                          .description = "Drag a formant to move its frequency and gain; Shift-drag sets its "
                                         "bandwidth; Escape cancels"};
    if (hasPose) {
      const auto& shown = r->poses[p];
      for (const auto& h : layoutHandles(l.plot, shown)) {
        const auto& band = shown.formants[h.band];
        const auto lower = h.band > 0U ? shown.formants[h.band - 1U].frequencyHz + 1.0 : 50.0;
        const auto upper = h.band + 1U < shown.formants.size() ? shown.formants[h.band + 1U].frequencyHz - 1.0 : 16000.0;
        envelope.children.push_back(SemanticNode{
            .id = "shell.voice.formant." + std::to_string(h.band + 1U), .role = SemanticRole::Slider,
            .name = "F" + std::to_string(h.band + 1U),
            .value = format("%.0f Hz", band.frequencyHz) + ", " + format("%+.1f dB", band.gainDb) + ", bandwidth " +
                     format("%.0f Hz", band.bandwidthHz),
            .bounds = h.hit,
            .actions = {SemanticAction::Increment, SemanticAction::Decrement, SemanticAction::SetFocus},
            .description = "Increment and Decrement move its frequency by 10 Hz",
            .numericValue = band.frequencyHz, .numericMinimum = lower, .numericMaximum = upper,
            .numericStep = 10.0});
      }
    }
    out.push_back(std::move(envelope));
    const auto nasal = hasPose ? r->poses[p].nasalCoupling : 0.0;
    out.push_back(knobNode("shell.voice.nasal", "Nasal coupling", l.nasal, hasPose, nasal, kNasalKnob,
                           format("%.2f", nasal), "Coupling of the selected pose's nasal branch"));
  }
  if (cardShown(l, Card::Noise)) {
    const auto selected = selectedFrication();
    if (r != nullptr)
      for (const auto& row : layoutNoiseRows(l.list, fricationsFor(*r, p), noiseScroll_)) {
        const auto& source = r->frications[row.index];
        const auto base = "shell.voice.frication." + std::to_string(row.index);
        out.push_back(SemanticNode{.id = base, .role = SemanticRole::RadioButton, .name = "Frication " + source.phone,
                       .bounds = row.label, .selected = selected == row.index, .actions = pressable});
        for (std::size_t f = 0U; f < 3U; ++f) {
          const auto value = noiseValue(source.source, f);
          const auto [lo, hi] = noiseRange(source.source, f);
          out.push_back(SemanticNode{.id = base + "." + kNoiseFieldIds[f], .role = SemanticRole::Slider,
                         .name = source.phone + " " + kNoiseFieldLabels[f],
                         .value = f == 2U ? format("%.3f", value) : format("%.0f Hz", value), .bounds = row.slider[f],
                         .actions = {SemanticAction::Increment, SemanticAction::Decrement, SemanticAction::SetFocus},
                         .numericValue = value, .numericMinimum = lo, .numericMaximum = hi,
                         .numericStep = f == 2U ? 0.005 : value * (std::pow(2.0, 1.0 / 12.0) - 1.0)});
        }
      }
    out.push_back(SemanticNode{.id = "shell.voice.frication.seed", .role = SemanticRole::TextField, .name = "Frication seed",
                   .value = r != nullptr && selected ? std::to_string(r->frications[*selected].source.seed) : std::string{},
                   .bounds = l.seed, .enabled = selected.has_value(),
                   .actions = selected ? std::vector<SemanticAction>{SemanticAction::Activate, SemanticAction::EditText,
                                                                     SemanticAction::SetFocus}
                                       : focusable,
                   .editableValue = r != nullptr && selected ? std::to_string(r->frications[*selected].source.seed) : std::string{},
                   .description = "The exact unsigned 64-bit seed of the selected frication"});
  }
  if (r != nullptr) {
    const auto canPlay = static_cast<bool>(host_.play);
    out.push_back(SemanticNode{.id = "shell.voice.play", .role = SemanticRole::Button,
                   .name = playing() ? "Stop audition" : useReference_ ? "Play reference A" : "Play B",
                   .value = auditionStatus(), .bounds = l.play, .enabled = canPlay,
                   .actions = canPlay ? pressable : focusable,
                   .description = canPlay ? "A one-second sustained audition of the selected pose; not an approval"
                                          : host_.playUnavailable});
    out.push_back(SemanticNode{.id = "shell.voice.ab.a", .role = SemanticRole::RadioButton, .name = "Reference A",
                   .bounds = l.abA, .enabled = s->auditionReference().has_value(), .selected = useReference_,
                   .actions = s->auditionReference().has_value() ? pressable : focusable,
                   .description = s->auditionReference() ? "The kept reference" : "Keep a rendered B as A first"});
    out.push_back(SemanticNode{.id = "shell.voice.ab.b", .role = SemanticRole::RadioButton, .name = "Current B",
                   .bounds = l.abB, .selected = !useReference_, .actions = pressable});
    out.push_back(SemanticNode{.id = "shell.voice.level", .role = SemanticRole::Status, .name = "Audition level",
                   .value = levelText(), .bounds = l.level, .actions = focusable});
  }
}

core::Result<void> VoiceWorkspaceImpl::perform(NativeEditorController& controller, std::string_view id,
                                               SemanticAction action) {
  controller_ = &controller;
  const auto unsupported = [] {
    return core::failure(core::ErrorCode::Unsupported, "This element does not support that action");
  };
  const auto activate = action == SemanticAction::Activate || action == SemanticAction::Toggle;
  const auto steps = action == SemanticAction::Increment ? 1 : action == SemanticAction::Decrement ? -1 : 0;
  if (id == "shell.voice.browser") {
    if (!activate) return unsupported();
    controller.showVoicebankBrowser();
    return core::success();
  }
  if (session() == nullptr) return unavailable();
  if (id.ends_with(".more")) {
    for (std::size_t i = 0U; i < 4U; ++i)
      if (id == cardId(i) + ".more" && activate) {
        menu_ = menu_ == static_cast<Card>(i) ? std::nullopt : std::optional{static_cast<Card>(i)};
        return core::success();
      }
    return unsupported();
  }
  if (id.starts_with("shell.voice.menu.")) {
    if (!activate || !menu_) return unsupported();
    const auto name = id.substr(std::string_view{"shell.voice.menu."}.size());
    const auto items = menuFor(*menu_);
    for (const auto& item : items)
      if (name == item.id) {
        menu_.reset();
        return run(controller, item.action);
      }
    return unsupported();
  }
  if (id.starts_with("shell.voice.view.")) {
    if (!activate) return unsupported();
    for (std::size_t i = 0U; i < 3U; ++i)
      if (id.substr(std::string_view{"shell.voice.view."}.size()) == kCardIds[i]) view_ = static_cast<Card>(i);
    return core::success();
  }
  if (id == "shell.voice.new" && activate) return run(controller, Action::New);
  if (id == "shell.voice.open" && activate) return run(controller, Action::Open);
  if (id == "shell.voice.play" && activate) return play();
  if ((id == "shell.voice.ab.a" || id == "shell.voice.ab.b") && activate) {
    useReference_ = id == "shell.voice.ab.a";
    if (playing() && host_.stop) host_.stop();
    level_.reset();
    return play();
  }
  if (id.starts_with("shell.voice.knob.")) {
    const auto name = id.substr(std::string_view{"shell.voice.knob."}.size());
    for (std::size_t i = 0U; i < kSourceKnobs.size(); ++i)
      if (name == kSourceKnobs[i].id && steps != 0) return step(Target::Source, i, 0U, steps);
    return unsupported();
  }
  if (id == "shell.voice.nasal") return steps != 0 ? step(Target::Nasal, 0U, 0U, steps) : unsupported();
  if (id == "shell.voice.pose.previous" || id == "shell.voice.pose.next") {
    if (!activate) return unsupported();
    const auto* r = recipe();
    const auto current = pose();
    if (r == nullptr) return unsupported();
    if (id.ends_with("previous")) return current > 0U ? selectPose(current - 1U) : core::success();
    return current + 1U < r->poses.size() ? selectPose(current + 1U) : core::success();
  }
  if (id == "shell.voice.pose.add") return activate ? run(controller, Action::DuplicatePose) : unsupported();
  if (id.starts_with("shell.voice.pose.")) {
    if (!activate) return unsupported();
    const auto text = id.substr(std::string_view{"shell.voice.pose."}.size());
    std::size_t index = 0U;
    for (const auto ch : text) {
      if (ch < '0' || ch > '9') return unsupported();
      index = index * 10U + static_cast<std::size_t>(ch - '0');
    }
    return selectPose(index);
  }
  if (id.starts_with("shell.voice.formant.")) {
    if (steps == 0) return unsupported();
    const auto text = id.substr(std::string_view{"shell.voice.formant."}.size());
    std::size_t number = 0U;
    for (const auto ch : text) {
      if (ch < '0' || ch > '9') return unsupported();
      number = number * 10U + static_cast<std::size_t>(ch - '0');
    }
    if (number == 0U) return unsupported();
    return step(Target::Formant, number - 1U, 0U, steps);
  }
  if (id == "shell.voice.frication.seed") {
    if (!activate && action != SemanticAction::EditText) return unsupported();
    return run(controller, Action::FricationSeed);
  }
  if (id.starts_with("shell.voice.frication.")) {
    auto rest = id.substr(std::string_view{"shell.voice.frication."}.size());
    std::size_t index = 0U;
    std::size_t used = 0U;
    while (used < rest.size() && rest[used] >= '0' && rest[used] <= '9') {
      index = index * 10U + static_cast<std::size_t>(rest[used] - '0');
      ++used;
    }
    if (used == 0U) return unsupported();
    rest = rest.substr(used);
    if (rest.empty()) {
      if (!activate) return unsupported();
      fricationSelection_ = index;
      return core::success();
    }
    for (std::size_t f = 0U; f < 3U; ++f)
      if (rest.substr(1U) == kNoiseFieldIds[f] && steps != 0) {
        fricationSelection_ = index;
        return step(Target::Noise, index, f, steps);
      }
    return unsupported();
  }
  return unsupported();
}

}  // namespace

std::unique_ptr<VoiceWorkspace> makeVoiceWorkspace() { return std::make_unique<VoiceWorkspaceImpl>(); }

}  // namespace seam::native_ui::design
