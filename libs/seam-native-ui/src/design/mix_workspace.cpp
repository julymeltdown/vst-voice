#include "seam/native_ui/design/shell_workspace.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
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

// Panel geometry, in logical points.
constexpr double kPad = 12.0;
constexpr double kHeader = 40.0;
constexpr double kBottomPad = 8.0;
constexpr double kGap = 8.0;
constexpr double kScrollBand = 10.0;
// A regular strip needs this height for a usable vertical fader (60 points of travel).
constexpr double kRegularStripHeight = 264.0;
constexpr double kRegularStripWidth = 100.0;
constexpr double kCompactStripWidth = 128.0;
constexpr double kDeviceCardHeight = 112.0;

// The fader reads and writes the full range the mix command accepts. The curve spends most of its
// travel near unity, as a console fader does.
constexpr double kGainMinimum = -120.0;
constexpr double kGainMaximum = 24.0;
constexpr double kGainStep = 1.0;
constexpr double kPanStep = 0.05;
constexpr std::array<std::pair<double, double>, 7U> kFaderCurve{{{0.0, -120.0},
                                                                 {0.06, -60.0},
                                                                 {0.25, -30.0},
                                                                 {0.5, -12.0},
                                                                 {0.75, 0.0},
                                                                 {0.9, 12.0},
                                                                 {1.0, 24.0}}};
// Vertical drag distance that sweeps the pan knob across its whole range (the ArcKnob rule).
constexpr double kPanDragPoints = 240.0;

std::string format(const char* pattern, double value) {
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), pattern, value);
  return buffer;
}

bool contains(ui::Rect r, ui::Point p) noexcept {
  return r.width > 0.0 && r.height > 0.0 && p.x >= r.x && p.y >= r.y && p.x < r.right() &&
         p.y < r.bottom();
}

ui::Rect intersection(ui::Rect a, ui::Rect b) noexcept {
  const auto x = std::max(a.x, b.x);
  const auto y = std::max(a.y, b.y);
  const auto right = std::min(a.right(), b.right());
  const auto bottom = std::min(a.bottom(), b.bottom());
  if (right <= x || bottom <= y) return {};
  return {x, y, right - x, bottom - y};
}

TextStyle style(FontRole role, double size, double tracking = 0.0,
                TextAlign align = TextAlign::Left, bool upper = false) {
  return TextStyle{role, size, tracking, align, upper};
}

// Tightens tracking, then steps the size down to the 11-point floor for essential text; anything
// still too long is ellipsized by the canvas inside its box.
TextStyle fitted(Canvas2D& c, std::string_view text, TextStyle s, double width) {
  if (width <= 0.0 || c.measure(text, s) <= width) return s;
  s.tracking = std::min(s.tracking, 0.4);
  while (s.size > 11.0 && c.measure(text, s) > width) s.size = std::max(11.0, s.size - 0.5);
  return s;
}

// The SING rack card material: raised gradient, hairline border, top highlight.
void card(Canvas2D& c, const DesignTokens& t, ui::Rect r, double radius, double alpha = 0.92) {
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
  if (r.width <= 0.0 || r.height <= 0.0) return;
  const auto p = Path::roundedRect(r, radius);
  c.fill(p, LinearGradient{{r.x, r.y}, {r.x, r.bottom()},
                           {{0.0, t.color.surfaceSunken}, {1.0, t.color.surface}}});
  c.stroke(p, withAlpha(t.color.border, 0.9), StrokeStyle{1.0});
}

// The EXPORT/rack card header: status dot, caps title and the mode's divider (EMO stitches, SCENE
// gradient rule). The title keeps to its own box so nothing in the header can overlap it.
void header(Canvas2D& c, const DesignTokens& t, ui::Rect area, ui::Rect title, std::string_view text,
            bool lit) {
  c.save();
  if (lit) c.setGlow(t.color.accent, 7.0);
  c.fill(Path::circle({area.x + 20.0, area.y + 19.0}, 3.5),
         lit ? t.color.accent : t.color.textDisabled);
  c.restore();
  c.text(title, text,
         style(FontRole::UiSemibold, t.type.panelTitle, t.type.panelTitleTracking, TextAlign::Left,
               true),
         t.color.textPrimary);
  Path rule;
  rule.moveTo({area.x + 16.0, area.y + kHeader - 5.0})
      .lineTo({area.right() - 16.0, area.y + kHeader - 5.0});
  if (t.mode == DesignMode::Emo) {
    c.stroke(rule, withAlpha(t.color.textPrimary, 0.16), StrokeStyle{1.0, true, {5.0, 4.0}});
  } else {
    c.stroke(rule,
             LinearGradient{{area.x, 0.0}, {area.right(), 0.0},
                            {{0.0, withAlpha(t.color.accent, 0.55)},
                             {1.0, withAlpha(t.color.accentCurve, 0.35)}}},
             StrokeStyle{1.0});
  }
}

// ---- Model ----------------------------------------------------------------------------------

struct StripModel final {
  domain::TrackId id;
  std::string name;
  bool vocal{true};
  float gainDb{0.0F};
  float pan{0.0F};
  bool muted{false};
  bool solo{false};
  domain::TrackOutputRoute route;
  std::string routeName;
  bool selected{false};
};

std::string busName(const domain::ProjectRouting& routing, domain::BusId id) {
  const auto* bus = routing.findBus(id);
  return bus != nullptr ? bus->name : std::string{"Missing bus"};
}

// Every project track in arrangement order (vocal tracks, then audio tracks), read from the
// document itself.
std::vector<StripModel> stripModels(const NativeEditorController& controller) {
  const auto& project = controller.project();
  const auto selected = controller.selectedTrack();
  std::vector<StripModel> strips;
  strips.reserve(project.vocalTracks().size() + project.audioTracks().size());
  for (const auto& track : project.vocalTracks())
    strips.push_back(StripModel{.id = track.id, .name = track.name, .vocal = true,
                                .gainDb = track.gainDb, .pan = track.pan, .muted = track.muted,
                                .solo = track.solo, .route = track.outputRoute,
                                .routeName = busName(project.routing(), track.outputRoute.bus),
                                .selected = track.id == selected});
  for (const auto& track : project.audioTracks())
    strips.push_back(StripModel{.id = track.id, .name = track.name, .vocal = false,
                                .gainDb = track.gainDb, .pan = track.pan, .muted = track.muted,
                                .solo = track.solo, .route = track.outputRoute,
                                .routeName = busName(project.routing(), track.outputRoute.bus),
                                .selected = track.id == selected});
  return strips;
}

double faderPosition(double gainDb) noexcept {
  const auto db = std::clamp(gainDb, kGainMinimum, kGainMaximum);
  for (std::size_t i = 1U; i < kFaderCurve.size(); ++i) {
    const auto [p1, d1] = kFaderCurve[i];
    if (db > d1) continue;
    const auto [p0, d0] = kFaderCurve[i - 1U];
    return p0 + (p1 - p0) * (db - d0) / (d1 - d0);
  }
  return 1.0;
}

double faderGain(double position) noexcept {
  const auto p = std::clamp(position, 0.0, 1.0);
  for (std::size_t i = 1U; i < kFaderCurve.size(); ++i) {
    const auto [p1, d1] = kFaderCurve[i];
    if (p > p1) continue;
    const auto [p0, d0] = kFaderCurve[i - 1U];
    return d0 + (d1 - d0) * (p - p0) / (p1 - p0);
  }
  return kGainMaximum;
}

double roundTo(double value, double quantum) noexcept {
  return std::round(value / quantum) * quantum;
}

std::string gainText(double gainDb) {
  const auto shown = roundTo(gainDb, 0.1);
  return format("%.1f dB", shown == 0.0 ? 0.0 : shown);
}

std::string panShort(double pan) {
  const auto percent = static_cast<int>(std::lround(pan * 100.0));
  if (percent == 0) return "C";
  return (percent < 0 ? "L" : "R") + std::to_string(std::abs(percent));
}

std::string panSpoken(double pan) {
  const auto percent = static_cast<int>(std::lround(pan * 100.0));
  if (percent == 0) return "Center";
  return std::string{percent < 0 ? "Left " : "Right "} + std::to_string(std::abs(percent)) + "%";
}

// ---- Layout ---------------------------------------------------------------------------------

struct StripGeometry final {
  ui::Rect strip, colorBar, name, kind, pan, panValue, mute, solo, fader, faderValue, route;
  bool horizontalFader{false};
};

struct MixLayout final {
  bool compact{false};
  ui::Rect title, body, viewport, scrollBand, master, device, settings;
  double stripWidth{0.0};
  double contentWidth{0.0};
  double maxOffset{0.0};
  double offset{0.0};
  std::vector<StripGeometry> strips;

  [[nodiscard]] bool scrolls() const noexcept { return maxOffset > 0.0; }
  // A strip control is reachable only where the strip viewport shows it.
  [[nodiscard]] ui::Rect visible(ui::Rect r) const noexcept { return intersection(r, viewport); }
};

StripGeometry stripGeometry(ui::Rect s, bool compact) {
  StripGeometry g;
  g.strip = s;
  const auto ix = s.x + 8.0;
  const auto iw = s.width - 16.0;
  g.colorBar = {ix, s.y + 3.0, iw, 3.0};
  if (compact) {
    // Name; pan knob, its value, mute and solo on one row; a horizontal fader with its readout;
    // the output route.
    g.name = {ix, s.y + 8.0, iw, 16.0};
    g.pan = {ix, s.y + 28.0, 30.0, 30.0};
    g.solo = {ix + iw - 22.0, s.y + 31.0, 22.0, 24.0};
    g.mute = {g.solo.x - 24.0, s.y + 31.0, 22.0, 24.0};
    g.panValue = {g.pan.right() + 4.0, s.y + 28.0, g.mute.x - 4.0 - (g.pan.right() + 4.0), 30.0};
    g.faderValue = {ix + iw - 50.0, s.y + 62.0, 50.0, 22.0};
    g.fader = {ix, s.y + 62.0, g.faderValue.x - 4.0 - ix, 22.0};
    g.route = {ix, s.y + 88.0, iw, 22.0};
    g.horizontalFader = true;
    return g;
  }
  g.name = {ix, s.y + 10.0, iw, 18.0};
  g.kind = {ix, s.y + 28.0, iw, 14.0};
  g.pan = {s.x + (s.width - 44.0) * 0.5, s.y + 46.0, 44.0, 44.0};
  g.panValue = {ix, g.pan.bottom() + 2.0, iw, 14.0};
  const auto half = (iw - 4.0) * 0.5;
  g.mute = {ix, s.y + 112.0, half, 24.0};
  g.solo = {ix + half + 4.0, s.y + 112.0, half, 24.0};
  g.route = {ix, s.bottom() - 8.0 - 26.0, iw, 26.0};
  g.faderValue = {ix, g.route.y - 6.0 - 16.0, iw, 16.0};
  const auto faderTop = s.y + 144.0;
  g.fader = {s.x + (s.width - 28.0) * 0.5, faderTop, 28.0,
             std::max(0.0, g.faderValue.y - 4.0 - faderTop)};
  return g;
}

MixLayout mixLayout(ui::Rect area, std::size_t count, double offset) {
  MixLayout l;
  l.body = {area.x + kPad, area.y + kHeader, std::max(0.0, area.width - 2.0 * kPad),
            std::max(0.0, area.height - kHeader - kBottomPad)};
  l.compact = l.body.height - kScrollBand < kRegularStripHeight;
  const auto masterWidth = l.compact ? 116.0 : 132.0;
  if (l.compact) {
    // The device card folds into a header button so the strips keep the body.
    const auto width = std::clamp(area.width - 2.0 * kPad - 88.0, 0.0, 220.0);
    l.settings = {area.right() - kPad - width, area.y + 7.0, width, 24.0};
    l.device = l.settings;
    l.master = {l.body.right() - masterWidth, l.body.y, masterWidth, l.body.height};
    l.title = {area.x + 32.0, area.y + 8.0, std::max(0.0, l.settings.x - 8.0 - (area.x + 32.0)),
               22.0};
  } else {
    l.device = {l.body.right() - masterWidth, l.body.bottom() - kDeviceCardHeight, masterWidth,
                kDeviceCardHeight};
    l.settings = {l.device.x + 10.0, l.device.bottom() - 36.0, l.device.width - 20.0, 26.0};
    l.master = {l.device.x, l.body.y, masterWidth,
                std::max(0.0, l.body.height - kDeviceCardHeight - kGap)};
    l.title = {area.x + 32.0, area.y + 8.0, std::max(0.0, area.width - 32.0 - kPad), 22.0};
  }
  l.viewport = {l.body.x, l.body.y, std::max(0.0, l.master.x - kGap - l.body.x), l.body.height};
  l.stripWidth = l.compact ? kCompactStripWidth : kRegularStripWidth;
  const auto n = static_cast<double>(count);
  l.contentWidth = count == 0U ? 0.0 : n * l.stripWidth + (n - 1.0) * kGap;
  if (l.contentWidth > l.viewport.width + 0.5) {
    // Strips that do not all fit scroll by whole strips: the viewport holds a whole number of
    // them, widened to fill it, so no strip (or its text) is ever cut at the edge.
    const auto shown = std::max(1.0, std::floor((l.viewport.width + kGap) / (l.stripWidth + kGap)));
    l.stripWidth = std::max(l.stripWidth, (l.viewport.width - (shown - 1.0) * kGap) / shown);
    const auto pitch = l.stripWidth + kGap;
    l.contentWidth = n * l.stripWidth + (n - 1.0) * kGap;
    l.maxOffset = std::max(0.0, (n - shown) * pitch);
    l.offset = std::clamp(std::round(offset / pitch) * pitch, 0.0, l.maxOffset);
  }
  const auto stripHeight = l.viewport.height - (l.scrolls() ? kScrollBand : 0.0);
  if (l.scrolls())
    l.scrollBand = {l.viewport.x, l.viewport.bottom() - kScrollBand, l.viewport.width, kScrollBand};
  l.strips.reserve(count);
  for (std::size_t i = 0U; i < count; ++i) {
    const ui::Rect s{l.viewport.x + static_cast<double>(i) * (l.stripWidth + kGap) - l.offset,
                     l.viewport.y, l.stripWidth, stripHeight};
    l.strips.push_back(stripGeometry(s, l.compact));
  }
  return l;
}

ui::Rect scrollThumb(const MixLayout& l) noexcept {
  if (!l.scrolls() || l.contentWidth <= 0.0) return {};
  const auto track = l.scrollBand.width;
  const auto width = std::max(24.0, track * l.viewport.width / l.contentWidth);
  const auto x = l.scrollBand.x + (track - width) * (l.offset / l.maxOffset);
  return {x, l.scrollBand.y + 3.0, width, 4.0};
}

// ---- Ids ------------------------------------------------------------------------------------

enum class Control : std::uint8_t { Strip, Gain, Pan, Mute, Solo, Route, Settings, Scroll, Master, Device };

constexpr std::string_view kPrefix = "shell.mix.";
constexpr std::string_view kTrackPrefix = "shell.mix.track.";

std::string_view controlSuffix(Control control) noexcept {
  switch (control) {
    case Control::Gain: return ".gain";
    case Control::Pan: return ".pan";
    case Control::Mute: return ".mute";
    case Control::Solo: return ".solo";
    case Control::Route: return ".route";
    default: return "";
  }
}

std::string trackNodeId(domain::TrackId id, Control control) {
  return std::string{kTrackPrefix} + id.toString() + std::string{controlSuffix(control)};
}

struct ParsedId final {
  Control control{Control::Strip};
  std::string track;  // hex id, for track controls
};

std::optional<ParsedId> parseId(std::string_view id) {
  if (id == "shell.mix.audio-settings") return ParsedId{Control::Settings, {}};
  if (id == "shell.mix.scroll") return ParsedId{Control::Scroll, {}};
  if (id == "shell.mix.master") return ParsedId{Control::Master, {}};
  if (id == "shell.mix.device") return ParsedId{Control::Device, {}};
  if (!id.starts_with(kTrackPrefix)) return std::nullopt;
  auto rest = id.substr(kTrackPrefix.size());
  const auto dot = rest.find('.');
  const auto hex = rest.substr(0U, dot);
  if (dot == std::string_view::npos) return ParsedId{Control::Strip, std::string{hex}};
  const auto suffix = rest.substr(dot);
  for (const auto control : {Control::Gain, Control::Pan, Control::Mute, Control::Solo, Control::Route})
    if (suffix == controlSuffix(control)) return ParsedId{control, std::string{hex}};
  return std::nullopt;
}

// ---- Commands -------------------------------------------------------------------------------

// One mix edit is one command: the track is selected through the controller's own selection
// first, and an edit that changes nothing is not committed at all.
core::Result<void> commitMix(NativeEditorController& controller, const StripModel& strip, float gainDb,
                             float pan, bool muted, bool solo) {
  if (gainDb == strip.gainDb && pan == strip.pan && muted == strip.muted && solo == strip.solo)
    return core::success();
  if (controller.selectedTrack() != strip.id) {
    auto selected = controller.selectTrack(strip.id);
    if (!selected) return selected;
  }
  return controller.setSelectedTrackMix(gainDb, pan, muted, solo);
}

core::Result<void> cycleRoute(NativeEditorController& controller, const StripModel& strip) {
  const auto& buses = controller.project().routing().buses;
  if (buses.size() < 2U)
    return core::failure(core::ErrorCode::Unsupported, "The project has only one output bus");
  const auto current = std::find_if(buses.begin(), buses.end(),
                                    [&strip](const auto& bus) { return bus.id == strip.route.bus; });
  const auto index = current == buses.end()
                         ? 0U
                         : (static_cast<std::size_t>(current - buses.begin()) + 1U) % buses.size();
  const auto& bus = buses[index];
  auto route = strip.route;
  route.bus = bus.id;
  if (route.matrix.destinationChannels != bus.channelCount) {
    if (bus.channelCount == 2U) route.matrix = domain::RoutingMatrix::monoToStereo(strip.pan);
    else if (bus.channelCount == 1U) route.matrix = domain::RoutingMatrix::identity(1U);
  }
  if (controller.selectedTrack() != strip.id) {
    auto selected = controller.selectTrack(strip.id);
    if (!selected) return selected;
  }
  return controller.setSelectedTrackRoute(std::move(route));
}

float nudgedGain(float gainDb, double step) {
  return static_cast<float>(
      std::clamp(roundTo(static_cast<double>(gainDb) + step, 0.1), kGainMinimum, kGainMaximum));
}

float nudgedPan(float pan, double step) {
  return static_cast<float>(std::clamp(roundTo(static_cast<double>(pan) + step, 0.01), -1.0, 1.0));
}

// ---- Device ---------------------------------------------------------------------------------

struct DeviceSummary final {
  bool online{false};
  std::string name;
  std::string format;
};

DeviceSummary deviceSummary(const EditorSceneState& state) {
  DeviceSummary d;
  d.online = state.audioDeviceOnline;
  for (const auto& device : state.audioSettings.devices)
    if (device.selected) d.name = device.name;
  if (d.name.empty()) d.name = d.online ? state.audioBackend : std::string{"Audio offline"};
  const auto& current = state.audioSettings.current;
  const auto khz = static_cast<double>(current.sampleRate) / 1000.0;
  d.format = (std::fmod(khz, 1.0) == 0.0 ? format("%.0f", khz) : format("%.1f", khz)) + " kHz \u00b7 " +
             std::to_string(current.blockFrames) + " frames";
  return d;
}

// ---- Workspace ------------------------------------------------------------------------------

class MixWorkspace final : public ShellWorkspace {
public:
  [[nodiscard]] std::string_view idPrefix() const noexcept override { return kPrefix; }

  void paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController& controller,
             const EditorSceneState& state, ui::Rect area) const override {
    lastArea_ = area;
    const auto strips = stripModels(controller);
    const auto l = mixLayout(area, strips.size(), offset_);
    card(c, t, area, t.shape.card, 0.97);
    const auto anySolo = std::any_of(strips.begin(), strips.end(), [](const auto& s) { return s.solo; });
    header(c, t, area, l.title, "Mix", !strips.empty());
    c.save();
    c.clipRect(area);
    c.save();
    c.clipRect(l.viewport);
    for (std::size_t i = 0U; i < strips.size(); ++i) {
      const auto& g = l.strips[i];
      if (intersection(g.strip, l.viewport).width <= 0.0) continue;
      paintStrip(c, t, g, preview(strips[i]), t.trackColors[i % t.trackColors.size()], l.compact,
                 anySolo, controller.project().routing().buses.size() > 1U);
    }
    c.restore();
    if (strips.empty())
      c.text({l.viewport.x, l.viewport.y + 8.0, l.viewport.width, 20.0}, "This project has no tracks",
             style(FontRole::Ui, t.type.body), t.color.textSecondary);
    if (l.scrolls()) {
      c.fill(Path::capsule({l.scrollBand.x, l.scrollBand.y + 3.0, l.scrollBand.width, 4.0}),
             withAlpha(t.color.textPrimary, 0.08));
      c.fill(Path::capsule(scrollThumb(l)), withAlpha(t.color.accent, 0.75));
    }
    paintMaster(c, t, controller, l, strips.size());
    paintDevice(c, t, state, l);
    c.restore();
  }

  core::Result<void> pointerDown(NativeEditorController& controller, const PointerEvent& event,
                                 ui::Rect area) override {
    gesture_.reset();
    focusRequest_.clear();
    lastArea_ = area;
    if (event.button != PointerButton::Left) return core::success();
    const auto p = event.position;
    const auto strips = stripModels(controller);
    const auto l = mixLayout(area, strips.size(), offset_);
    offset_ = l.offset;
    const auto begin = [&](Control control, ui::Rect target, domain::TrackId track, double value) {
      gesture_ = Gesture{.control = control, .track = track, .target = target, .start = p,
                         .startValue = value, .value = value, .fine = event.modifiers.shift,
                         .revision = controller.documentRevision()};
    };
    if (contains(l.settings, p)) {
      focusRequest_ = "shell.mix.audio-settings";
      begin(Control::Settings, l.settings, {}, 0.0);
      return core::success();
    }
    if (l.scrolls() && contains(l.scrollBand, p)) {
      focusRequest_ = "shell.mix.scroll";
      begin(Control::Scroll, l.scrollBand, {}, l.offset);
      return core::success();
    }
    if (contains(l.master, p)) {
      focusRequest_ = "shell.mix.master";
      return core::success();
    }
    if (!l.compact && contains(l.device, p)) {
      focusRequest_ = "shell.mix.device";
      return core::success();
    }
    for (std::size_t i = 0U; i < strips.size(); ++i) {
      const auto& g = l.strips[i];
      const auto& s = strips[i];
      if (!contains(l.visible(g.strip), p)) continue;
      focusRequest_ = trackNodeId(s.id, Control::Strip);
      const auto hit = [&](ui::Rect r) { return contains(l.visible(r), p); };
      if (hit(g.mute)) {
        focusRequest_ = trackNodeId(s.id, Control::Mute);
        begin(Control::Mute, l.visible(g.mute), s.id, 0.0);
      } else if (hit(g.solo)) {
        focusRequest_ = trackNodeId(s.id, Control::Solo);
        begin(Control::Solo, l.visible(g.solo), s.id, 0.0);
      } else if (hit(g.route)) {
        focusRequest_ = trackNodeId(s.id, Control::Route);
        begin(Control::Route, l.visible(g.route), s.id, 0.0);
      } else if (hit(g.pan)) {
        focusRequest_ = trackNodeId(s.id, Control::Pan);
        // Double-click returns the knob to center, as one command.
        if (event.clickCount >= 2) return commitMix(controller, s, s.gainDb, 0.0F, s.muted, s.solo);
        begin(Control::Pan, l.visible(g.pan), s.id, s.pan);
      } else if (hit(g.fader)) {
        focusRequest_ = trackNodeId(s.id, Control::Gain);
        if (event.clickCount >= 2) return commitMix(controller, s, 0.0F, s.pan, s.muted, s.solo);
        begin(Control::Gain, l.visible(g.fader), s.id, s.gainDb);
        gesture_->horizontal = g.horizontalFader;
        gesture_->travel = std::max(1.0, (g.horizontalFader ? g.fader.width : g.fader.height) - 12.0);
      }
      return core::success();
    }
    return core::success();
  }

  core::Result<void> pointerMove(NativeEditorController& controller, const PointerEvent& event,
                                 ui::Rect area) override {
    if (!gesture_) return core::success();
    auto& g = *gesture_;
    const auto p = event.position;
    // Changing Shift mid-drag re-anchors the gesture, so the value never jumps.
    if (event.modifiers.shift != g.fine &&
        (g.control == Control::Gain || g.control == Control::Pan)) {
      g.start = p;
      g.startValue = g.value;
      g.fine = event.modifiers.shift;
    }
    const auto scale = g.fine ? 0.1 : 1.0;
    switch (g.control) {
      case Control::Gain: {
        const auto delta = (g.horizontal ? p.x - g.start.x : g.start.y - p.y) / g.travel * scale;
        g.value = roundTo(faderGain(faderPosition(g.startValue) + delta), 0.1);
        g.moved = true;
        break;
      }
      case Control::Pan:
        g.value = std::clamp(roundTo(g.startValue + (g.start.y - p.y) / kPanDragPoints * 2.0 * scale, 0.01),
                             -1.0, 1.0);
        g.moved = true;
        break;
      case Control::Scroll: {
        const auto l = mixLayout(area, stripModels(controller).size(), g.startValue);
        const auto thumb = scrollThumb(l);
        const auto free = std::max(1.0, l.scrollBand.width - thumb.width);
        offset_ = std::clamp(g.startValue + (p.x - g.start.x) / free * l.maxOffset, 0.0, l.maxOffset);
        break;
      }
      default: break;
    }
    return core::success();
  }

  core::Result<void> pointerUp(NativeEditorController& controller, const PointerEvent& event,
                               ui::Rect area) override {
    lastArea_ = area;
    if (!gesture_) return core::success();
    const auto g = *gesture_;
    gesture_.reset();
    const auto inside = contains(g.target, event.position);
    if (g.control == Control::Settings) {
      if (inside) controller.showAudioSettings();
      return core::success();
    }
    if (g.control == Control::Scroll) return core::success();
    // The release commits only against the document the gesture began on.
    if (controller.documentRevision() != g.revision) return core::success();
    const auto strip = findStrip(controller, g.track);
    if (!strip) return core::success();
    const auto& s = *strip;
    switch (g.control) {
      case Control::Mute:
        return inside ? commitMix(controller, s, s.gainDb, s.pan, !s.muted, s.solo) : core::success();
      case Control::Solo:
        return inside ? commitMix(controller, s, s.gainDb, s.pan, s.muted, !s.solo) : core::success();
      case Control::Route: return inside ? cycleRoute(controller, s) : core::success();
      case Control::Gain:
        if (!g.moved) return core::success();
        return commitMix(controller, s, static_cast<float>(g.value), s.pan, s.muted, s.solo);
      case Control::Pan:
        if (!g.moved) return core::success();
        return commitMix(controller, s, s.gainDb, static_cast<float>(g.value), s.muted, s.solo);
      default: return core::success();
    }
  }

  bool scroll(NativeEditorController& controller, ui::Point anchor, double deltaX, double deltaY,
              ui::Rect area) override {
    static_cast<void>(anchor);
    if (gesture_) return false;
    lastArea_ = area;
    const auto l = mixLayout(area, stripModels(controller).size(), offset_);
    if (!l.scrolls()) return false;
    // A trackpad scrolls sideways; a plain wheel moves the strips too (down = later tracks).
    const auto delta = std::abs(deltaX) >= std::abs(deltaY) ? deltaX : -deltaY;
    // The offset accumulates freely; the layout shows the nearest whole strip.
    offset_ = std::clamp(offset_ + delta, 0.0, l.maxOffset);
    return mixLayout(area, stripModels(controller).size(), offset_).offset != l.offset;
  }

  void semantics(const NativeEditorController& controller, const EditorSceneState& state, ui::Rect area,
                 std::vector<SemanticNode>& out) const override {
    lastArea_ = area;
    const auto strips = stripModels(controller);
    const auto l = mixLayout(area, strips.size(), offset_);
    const auto multipleBuses = controller.project().routing().buses.size() > 1U;
    out.push_back(SemanticNode{.id = "shell.mix.panel", .role = SemanticRole::Panel, .name = "Mix",
                               .value = std::to_string(strips.size()) +
                                        (strips.size() == 1U ? " track" : " tracks"),
                               .bounds = area, .actions = {SemanticAction::SetFocus}});
    std::size_t firstShown = strips.size();
    std::size_t lastShown = 0U;
    for (std::size_t i = 0U; i < strips.size(); ++i) {
      const auto& g = l.strips[i];
      const auto s = preview(strips[i]);
      const auto stripBounds = l.visible(g.strip);
      if (stripBounds.width <= 0.0) continue;
      firstShown = std::min(firstShown, i);
      lastShown = std::max(lastShown, i);
      const auto& name = s.name;
      std::string summary = s.vocal ? "Vocal track" : "Audio track";
      if (s.selected) summary += ", selected";
      out.push_back(SemanticNode{.id = trackNodeId(s.id, Control::Strip), .role = SemanticRole::Panel,
                                 .name = name, .value = summary, .bounds = stripBounds,
                                 .selected = s.selected, .actions = {SemanticAction::SetFocus}});
      const auto publish = [&](SemanticNode node, ui::Rect r) {
        node.bounds = l.visible(r);
        if (node.bounds.width > 0.0 && node.bounds.height > 0.0) out.push_back(std::move(node));
      };
      publish(SemanticNode{.id = trackNodeId(s.id, Control::Gain), .role = SemanticRole::Slider,
                           .name = name + " gain", .value = gainText(s.gainDb),
                           .actions = {SemanticAction::Increment, SemanticAction::Decrement,
                                       SemanticAction::SetFocus},
                           .description = "Fader; double-click for 0 dB",
                           .numericValue = roundTo(s.gainDb, 0.1),
                           .numericMinimum = kGainMinimum, .numericMaximum = kGainMaximum,
                           .numericStep = kGainStep},
              g.fader);
      publish(SemanticNode{.id = trackNodeId(s.id, Control::Pan), .role = SemanticRole::Slider,
                           .name = name + " pan", .value = panSpoken(s.pan),
                           .actions = {SemanticAction::Increment, SemanticAction::Decrement,
                                       SemanticAction::SetFocus},
                           .description = "Double-click to center",
                           .numericValue = std::round(s.pan * 100.0), .numericMinimum = -100.0,
                           .numericMaximum = 100.0, .numericStep = kPanStep * 100.0},
              g.pan);
      for (const auto control : {Control::Mute, Control::Solo}) {
        const auto on = control == Control::Mute ? s.muted : s.solo;
        publish(SemanticNode{.id = trackNodeId(s.id, control), .role = SemanticRole::CheckBox,
                             .name = name + (control == Control::Mute ? " mute" : " solo"),
                             .value = on ? "On" : "Off", .selected = on,
                             .actions = {SemanticAction::Toggle, SemanticAction::Activate,
                                         SemanticAction::SetFocus}},
                control == Control::Mute ? g.mute : g.solo);
      }
      publish(SemanticNode{.id = trackNodeId(s.id, Control::Route), .role = SemanticRole::Button,
                           .name = name + " output", .value = s.routeName, .enabled = multipleBuses,
                           .actions = multipleBuses
                                          ? std::vector<SemanticAction>{SemanticAction::Activate,
                                                                        SemanticAction::SetFocus}
                                          : std::vector<SemanticAction>{SemanticAction::SetFocus},
                           .description = multipleBuses ? "Sends the track to the next output bus"
                                                        : "The project has only one output bus"},
              g.route);
    }
    if (l.scrolls())
      out.push_back(SemanticNode{
          .id = "shell.mix.scroll", .role = SemanticRole::Slider, .name = "Track strips",
          .value = "Tracks " + std::to_string(firstShown + 1U) + " to " + std::to_string(lastShown + 1U) +
                   " of " + std::to_string(strips.size()),
          .bounds = l.scrollBand,
          .actions = {SemanticAction::Increment, SemanticAction::Decrement, SemanticAction::SetFocus},
          .numericValue = l.offset, .numericMinimum = 0.0, .numericMaximum = l.maxOffset,
          .numericStep = l.stripWidth + kGap});
    out.push_back(SemanticNode{.id = "shell.mix.master", .role = SemanticRole::Status,
                               .name = "Master", .value = masterSummary(controller),
                               .bounds = l.master, .actions = {SemanticAction::SetFocus}});
    const auto device = deviceSummary(state);
    if (!l.compact)
      out.push_back(SemanticNode{.id = "shell.mix.device", .role = SemanticRole::Status,
                                 .name = "Audio device",
                                 .value = device.name + ", " + device.format,
                                 .bounds = l.device, .actions = {SemanticAction::SetFocus}});
    out.push_back(SemanticNode{.id = "shell.mix.audio-settings", .role = SemanticRole::Button,
                               .name = "Audio settings", .value = device.name,
                               .bounds = l.settings,
                               .actions = {SemanticAction::Activate, SemanticAction::SetFocus},
                               .description = "Opens the audio device settings"});
  }

  core::Result<void> perform(NativeEditorController& controller, std::string_view id,
                             SemanticAction action) override {
    const auto parsed = parseId(id);
    const auto unsupported = [] {
      return core::failure(core::ErrorCode::Unsupported, "This element does not support that action");
    };
    if (!parsed) return unsupported();
    const auto activate = action == SemanticAction::Activate || action == SemanticAction::Toggle;
    switch (parsed->control) {
      case Control::Settings:
        if (!activate) return unsupported();
        controller.showAudioSettings();
        return core::success();
      case Control::Scroll: {
        if (action != SemanticAction::Increment && action != SemanticAction::Decrement)
          return unsupported();
        const auto l = mixLayout(lastArea_, stripModels(controller).size(), offset_);
        const auto step = (l.stripWidth + kGap) * (action == SemanticAction::Increment ? 1.0 : -1.0);
        offset_ = std::clamp(l.offset + step, 0.0, l.maxOffset);
        return core::success();
      }
      case Control::Master:
      case Control::Device:
      case Control::Strip: return unsupported();
      default: break;
    }
    const auto strip = findStrip(controller, parsed->track);
    if (!strip) return core::failure(core::ErrorCode::NotFound, "The track is no longer in the project");
    const auto& s = *strip;
    const auto direction = action == SemanticAction::Increment   ? 1.0
                           : action == SemanticAction::Decrement ? -1.0
                                                                 : 0.0;
    switch (parsed->control) {
      case Control::Gain:
        if (direction == 0.0) return unsupported();
        return commitMix(controller, s, nudgedGain(s.gainDb, direction * kGainStep), s.pan, s.muted, s.solo);
      case Control::Pan:
        if (direction == 0.0) return unsupported();
        return commitMix(controller, s, s.gainDb, nudgedPan(s.pan, direction * kPanStep), s.muted, s.solo);
      case Control::Mute:
        if (!activate) return unsupported();
        return commitMix(controller, s, s.gainDb, s.pan, !s.muted, s.solo);
      case Control::Solo:
        if (!activate) return unsupported();
        return commitMix(controller, s, s.gainDb, s.pan, s.muted, !s.solo);
      case Control::Route:
        if (action != SemanticAction::Activate) return unsupported();
        return cycleRoute(controller, s);
      default: return unsupported();
    }
  }

  bool key(NativeEditorController& controller, std::string_view focusedId, const KeyEvent& event) override {
    // Shift-arrows make fine steps on a focused fader or pan knob; plain arrows reach the same
    // Increment/Decrement actions through the shell.
    const auto parsed = parseId(focusedId);
    if (!parsed || !event.modifiers.shift ||
        (parsed->control != Control::Gain && parsed->control != Control::Pan))
      return false;
    const auto up = event.key == NativeKey::Up || event.key == NativeKey::Right;
    const auto down = event.key == NativeKey::Down || event.key == NativeKey::Left;
    if (!up && !down) return false;
    const auto strip = findStrip(controller, parsed->track);
    if (!strip) return true;
    const auto& s = *strip;
    const auto sign = up ? 1.0 : -1.0;
    if (parsed->control == Control::Gain)
      static_cast<void>(commitMix(controller, s, nudgedGain(s.gainDb, sign * 0.1), s.pan, s.muted, s.solo));
    else
      static_cast<void>(commitMix(controller, s, s.gainDb, nudgedPan(s.pan, sign * 0.01), s.muted, s.solo));
    return true;
  }

  void cancelGestures(NativeEditorController& controller) override {
    static_cast<void>(controller);
    if (gesture_ && gesture_->control == Control::Scroll) offset_ = gesture_->startValue;
    gesture_.reset();
  }

  [[nodiscard]] bool gestureActive() const noexcept override { return gesture_.has_value(); }

  [[nodiscard]] std::string takeFocusRequest() override { return std::exchange(focusRequest_, {}); }

private:
  struct Gesture final {
    Control control{Control::Gain};
    domain::TrackId track;
    ui::Rect target;
    ui::Point start;
    double startValue{0.0};
    double value{0.0};
    bool fine{false};
    std::uint64_t revision{0U};
    bool moved{false};
    bool horizontal{false};
    double travel{1.0};
  };

  static std::optional<StripModel> findStrip(const NativeEditorController& controller, domain::TrackId id) {
    for (auto& strip : stripModels(controller))
      if (strip.id == id) return strip;
    return std::nullopt;
  }

  static std::optional<StripModel> findStrip(const NativeEditorController& controller, std::string_view hex) {
    for (auto& strip : stripModels(controller))
      if (strip.id.toString() == hex) return strip;
    return std::nullopt;
  }

  // What the strip shows while a drag previews a value that is not committed yet.
  StripModel preview(StripModel strip) const {
    if (!gesture_ || gesture_->track != strip.id || !gesture_->moved) return strip;
    if (gesture_->control == Control::Gain) strip.gainDb = static_cast<float>(gesture_->value);
    if (gesture_->control == Control::Pan) strip.pan = static_cast<float>(gesture_->value);
    return strip;
  }

  static std::string masterSummary(const NativeEditorController& controller) {
    const auto& routing = controller.project().routing();
    const auto* bus = routing.findBus(routing.masterBus);
    std::string text = bus != nullptr ? bus->name + " bus, " +
                                            (bus->channelCount == 2U ? std::string{"stereo"}
                                             : bus->channelCount == 1U
                                                 ? std::string{"mono"}
                                                 : std::to_string(bus->channelCount) + " channels") +
                                            ", " + gainText(bus->gainDb)
                                      : std::string{"No master bus"};
    // No measured output level reaches the editor, so no meter is shown.
    return text + ". Output level is not measured here.";
  }

  void paintStrip(Canvas2D& c, const DesignTokens& t, const StripGeometry& g, const StripModel& s,
                  Color trackColor, bool compact, bool anySolo, bool routable) const {
    const auto body = Path::roundedRect(g.strip, 10.0);
    // Selected strip: accent border and glow (ModuleCard focus treatment).
    if (s.selected) {
      c.save();
      c.setGlow(withAlpha(t.color.accent, 0.35), t.light.glowMedium);
      c.stroke(body, withAlpha(t.color.accent, 0.6), StrokeStyle{1.0});
      c.restore();
    }
    card(c, t, g.strip, 10.0, 0.94);
    if (s.selected) c.stroke(body, withAlpha(t.color.accent, 0.6), StrokeStyle{1.0});
    // A track silenced by mute or by another track's solo reads dimmer.
    const auto audible = !s.muted && (!anySolo || s.solo);
    c.save();
    if (audible) c.setGlow(withAlpha(trackColor, 0.7), 5.0);
    c.fill(Path::capsule(g.colorBar), withAlpha(trackColor, audible ? 1.0 : 0.4));
    c.restore();
    const auto nameStyle = style(FontRole::UiSemibold, t.type.label);
    c.text(g.name, s.name, fitted(c, s.name, nameStyle, g.name.width),
           audible ? t.color.textPrimary : t.color.textSecondary);
    if (!compact)
      c.text(g.kind, s.vocal ? "Vocal" : "Audio",
             style(FontRole::UiMedium, t.type.smallLabel, t.type.labelTracking, TextAlign::Left, true),
             t.color.textSecondary);

    paintPan(c, t, g, s, compact);
    paintToggle(c, t, g.mute, "M", s.muted, t.color.warning);
    paintToggle(c, t, g.solo, "S", s.solo, t.color.accent);
    paintFader(c, t, g, s, audible);

    // Output route.
    sunken(c, t, g.route, 6.0);
    const auto routeText = "\u2192 " + s.routeName;
    const ui::Rect routeLabel{g.route.x + 6.0, g.route.y, g.route.width - 12.0, g.route.height};
    c.text(routeLabel, routeText,
           fitted(c, routeText, style(FontRole::UiMedium, t.type.smallLabel), routeLabel.width),
           routable ? t.color.textPrimary : t.color.textSecondary);
  }

  static void paintToggle(Canvas2D& c, const DesignTokens& t, ui::Rect r, std::string_view label,
                          bool on, Color onColor) {
    const auto p = Path::roundedRect(r, t.shape.control);
    if (on) {
      c.save();
      c.setGlow(withAlpha(onColor, 0.6), t.light.glowSmall);
      c.fill(p, withAlpha(onColor, 0.26));
      c.restore();
      c.stroke(p, onColor, StrokeStyle{1.0});
    } else {
      c.fill(p, withAlpha(t.color.surfaceSunken, 0.9));
      c.stroke(p, withAlpha(t.color.border, 0.95), StrokeStyle{1.0});
    }
    c.text(r, label, style(FontRole::UiBold, t.type.label, 0.0, TextAlign::Center),
           on ? t.color.textPrimary : t.color.textSecondary);
  }

  static void paintPan(Canvas2D& c, const DesignTokens& t, const StripGeometry& g, const StripModel& s,
                       bool compact) {
    // ArcKnob: 270-degree sweep, bipolar fill from the top (center pan).
    const ui::Point kc{g.pan.x + g.pan.width * 0.5, g.pan.y + g.pan.height * 0.5};
    const auto r = g.pan.width * 0.5 - 2.0;
    constexpr auto start = 0.75 * kPi;
    constexpr auto sweep = 1.5 * kPi;
    const auto stroke = compact ? 2.5 : 3.0;
    Path track;
    track.arc(kc, r, start, sweep);
    c.stroke(track, t.color.knobTrack, StrokeStyle{stroke});
    const auto fraction = std::clamp((static_cast<double>(s.pan) + 1.0) * 0.5, 0.0, 1.0);
    if (std::abs(fraction - 0.5) > 1e-4) {
      Path arc;
      arc.arc(kc, r, start + sweep * 0.5, sweep * (fraction - 0.5));
      c.save();
      c.setGlow(withAlpha(t.color.accent, 0.9), t.light.glowSmall);
      c.stroke(arc,
               t.mode == DesignMode::Scene
                   ? LinearGradient{{kc.x - r, kc.y}, {kc.x + r, kc.y},
                                    {{0.0, t.color.accent}, {1.0, t.color.accentAlt1}}}
                   : LinearGradient{{kc.x - r, kc.y}, {kc.x + r, kc.y},
                                    {{0.0, t.color.accentDeep}, {1.0, t.color.accent}}},
               StrokeStyle{stroke});
      c.restore();
    }
    const auto bodyRadius = r - (compact ? 4.5 : 6.0);
    c.fill(Path::circle(kc, bodyRadius),
           RadialGradient{{kc.x - bodyRadius * 0.3, kc.y - bodyRadius * 0.4}, r,
                          {{0.0, t.color.knobBodyInner}, {1.0, t.color.knobBodyOuter}}});
    c.stroke(Path::circle(kc, bodyRadius), withAlpha(kWhite, 0.08), StrokeStyle{1.0});
    const auto angle = start + sweep * fraction;
    if (t.mode == DesignMode::Emo) {
      Path pointer;
      pointer.moveTo({kc.x + std::cos(angle) * bodyRadius * 0.35, kc.y + std::sin(angle) * bodyRadius * 0.35})
          .lineTo({kc.x + std::cos(angle) * bodyRadius * 0.9, kc.y + std::sin(angle) * bodyRadius * 0.9});
      c.stroke(pointer, t.color.knobPointer, StrokeStyle{2.0});
    } else {
      c.fill(Path::circle({kc.x + std::cos(angle) * r, kc.y + std::sin(angle) * r}, compact ? 2.4 : 3.0),
             t.color.knobPointer);
    }
    const auto text = panShort(s.pan);
    const auto valueStyle =
        style(FontRole::UiSemibold, t.type.smallLabel, 0.6, compact ? TextAlign::Left : TextAlign::Center);
    c.text(g.panValue, text, fitted(c, text, valueStyle, g.panValue.width), t.color.textSecondary);
  }

  static void paintFader(Canvas2D& c, const DesignTokens& t, const StripGeometry& g, const StripModel& s,
                         bool audible) {
    const auto f = g.fader;
    if (f.width <= 0.0 || f.height <= 0.0) return;
    const auto position = faderPosition(s.gainDb);
    const auto unity = faderPosition(0.0);
    ui::Rect rail;
    ui::Rect thumb;
    ui::Rect fill;
    double unityAt = 0.0;
    if (g.horizontalFader) {
      const auto travel = f.width - 12.0;
      const auto x = f.x + 6.0 + travel * position;
      rail = {f.x + 6.0, f.y + f.height * 0.5 - 2.5, travel, 5.0};
      fill = {rail.x, rail.y, x - rail.x, rail.height};
      thumb = {x - 5.0, f.y + 2.0, 10.0, f.height - 4.0};
      unityAt = f.x + 6.0 + travel * unity;
    } else {
      const auto travel = f.height - 12.0;
      const auto y = f.bottom() - 6.0 - travel * position;
      rail = {f.x + f.width * 0.5 - 3.0, f.y + 6.0, 6.0, travel};
      fill = {rail.x, y, rail.width, rail.bottom() - y};
      thumb = {f.x + 1.0, y - 6.0, f.width - 2.0, 12.0};
      unityAt = f.bottom() - 6.0 - travel * unity;
    }
    sunken(c, t, rail, 2.5);
    // Unity mark.
    Path tick;
    if (g.horizontalFader)
      tick.moveTo({unityAt, f.y + 1.0}).lineTo({unityAt, f.bottom() - 1.0});
    else
      tick.moveTo({f.x, unityAt}).lineTo({f.x + 5.0, unityAt}).moveTo({f.right() - 5.0, unityAt}).lineTo({f.right(), unityAt});
    c.stroke(tick, withAlpha(t.color.textPrimary, 0.35), StrokeStyle{1.0});
    if (fill.width > 0.5 && fill.height > 0.5) {
      c.save();
      if (audible) c.setGlow(withAlpha(t.color.accent, 0.6), t.light.glowSmall);
      c.fill(Path::roundedRect(fill, 2.5),
             audible ? LinearGradient{{fill.x, fill.bottom()}, {fill.right(), fill.y},
                                      {{0.0, t.color.accentDeep}, {1.0, t.color.accent}}}
                     : LinearGradient{{fill.x, fill.bottom()}, {fill.right(), fill.y},
                                      {{0.0, t.color.textDisabled}, {1.0, t.color.textDisabled}}});
      c.restore();
    }
    const auto cap = Path::roundedRect(thumb, 3.0);
    c.fill(cap, LinearGradient{{thumb.x, thumb.y}, {thumb.x, thumb.bottom()},
                               {{0.0, t.color.knobBodyInner}, {1.0, t.color.knobBodyOuter}}});
    c.stroke(cap, withAlpha(kWhite, 0.14), StrokeStyle{1.0});
    Path grip;
    if (g.horizontalFader)
      grip.moveTo({thumb.x + thumb.width * 0.5, thumb.y + 3.0}).lineTo({thumb.x + thumb.width * 0.5, thumb.bottom() - 3.0});
    else
      grip.moveTo({thumb.x + 4.0, thumb.y + thumb.height * 0.5}).lineTo({thumb.right() - 4.0, thumb.y + thumb.height * 0.5});
    c.stroke(grip, t.color.knobPointer, StrokeStyle{1.5});
    const auto text = gainText(s.gainDb);
    const auto valueStyle = style(FontRole::UiSemibold, t.type.smallLabel, 0.0,
                                  g.horizontalFader ? TextAlign::Right : TextAlign::Center);
    c.text(g.faderValue, text, fitted(c, text, valueStyle, g.faderValue.width), t.color.textPrimary);
  }

  void paintMaster(Canvas2D& c, const DesignTokens& t, const NativeEditorController& controller,
                   const MixLayout& l, std::size_t trackCount) const {
    const auto m = l.master;
    if (m.width <= 0.0 || m.height <= 0.0) return;
    card(c, t, m, 10.0, 0.94);
    const auto& routing = controller.project().routing();
    const auto* bus = routing.findBus(routing.masterBus);
    const auto x = m.x + 10.0;
    const auto w = m.width - 20.0;
    c.text({x, m.y + 8.0, w, 14.0}, "Master",
           style(FontRole::UiSemibold, t.type.smallLabel, t.type.labelTracking, TextAlign::Left, true),
           t.color.accent);
    const auto name = bus != nullptr ? bus->name : std::string{"No master bus"};
    c.text({x, m.y + 24.0, w, 16.0}, name, fitted(c, name, style(FontRole::UiSemibold, t.type.label), w),
           t.color.textPrimary);
    if (bus != nullptr) {
      const auto info = (bus->channelCount == 2U ? std::string{"Stereo"} : std::to_string(bus->channelCount) + " ch") +
                        " \u00b7 " + gainText(bus->gainDb);
      c.text({x, m.y + 42.0, w, 16.0}, info, fitted(c, info, style(FontRole::Ui, t.type.smallLabel), w),
             t.color.textSecondary);
    }
    // The editor receives no measured output level, so the meter well stays empty and says so.
    const ui::Rect well{x, m.y + (l.compact ? 62.0 : 66.0), w,
                        std::max(0.0, m.bottom() - 10.0 - (m.y + (l.compact ? 62.0 : 66.0)))};
    if (well.height >= 20.0) {
      sunken(c, t, well, 6.0);
      c.text({well.x + 4.0, well.y + well.height * 0.5 - (l.compact ? 8.0 : 16.0), well.width - 8.0, 16.0},
             "No level meter", style(FontRole::UiMedium, t.type.smallLabel, 0.0, TextAlign::Center),
             t.color.textSecondary);
      if (!l.compact && well.height >= 48.0)
        c.text({well.x + 4.0, well.y + well.height * 0.5 + 2.0, well.width - 8.0, 16.0},
               std::to_string(trackCount) + (trackCount == 1U ? " track" : " tracks"),
               style(FontRole::Ui, t.type.smallLabel, 0.0, TextAlign::Center), t.color.textDisabled);
    }
  }

  static void paintDevice(Canvas2D& c, const DesignTokens& t, const EditorSceneState& state,
                          const MixLayout& l) {
    const auto d = deviceSummary(state);
    const auto dotColor = d.online ? t.color.success : t.color.textDisabled;
    if (l.compact) {
      const auto b = l.settings;
      if (b.width <= 0.0) return;
      sunken(c, t, b, t.shape.control);
      c.fill(Path::circle({b.x + 11.0, b.y + b.height * 0.5}, 3.0), dotColor);
      const auto text = d.name + " \u00b7 Settings\u2026";
      const ui::Rect label{b.x + 20.0, b.y, b.width - 26.0, b.height};
      c.text(label, text, fitted(c, text, style(FontRole::UiMedium, t.type.smallLabel), label.width),
             t.color.textPrimary);
      return;
    }
    const auto r = l.device;
    card(c, t, r, 10.0, 0.94);
    const auto x = r.x + 10.0;
    const auto w = r.width - 20.0;
    c.text({x, r.y + 8.0, w, 14.0}, "Audio device",
           style(FontRole::UiSemibold, t.type.smallLabel, t.type.labelTracking, TextAlign::Left, true),
           t.color.textSecondary);
    c.fill(Path::circle({x + 3.0, r.y + 34.0}, 3.0), dotColor);
    c.text({x + 10.0, r.y + 26.0, w - 10.0, 16.0}, d.name,
           fitted(c, d.name, style(FontRole::UiSemibold, t.type.smallLabel), w - 10.0), t.color.textPrimary);
    c.text({x, r.y + 44.0, w, 16.0}, d.format, fitted(c, d.format, style(FontRole::Ui, t.type.smallLabel), w),
           t.color.textSecondary);
    const auto b = l.settings;
    c.fill(Path::capsule(b), withAlpha(t.color.accent, 0.18));
    c.stroke(Path::capsule(b), withAlpha(t.color.accent, 0.8), StrokeStyle{1.0});
    c.text(b, "Settings\u2026", style(FontRole::UiBold, t.type.smallLabel, 1.0, TextAlign::Center, true),
           t.color.textPrimary);
  }

  // Strip scroll offset in points; view state only, never part of the document.
  double offset_{0.0};
  std::optional<Gesture> gesture_;
  std::string focusRequest_;
  // The body rectangle of the last paint, semantics pass or gesture, for accessibility scrolling.
  mutable ui::Rect lastArea_;
};

}  // namespace

std::unique_ptr<ShellWorkspace> makeMixWorkspace() { return std::make_unique<MixWorkspace>(); }

}  // namespace seam::native_ui::design
