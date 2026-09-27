#include "seam/native_ui/design/shell_overlays.hpp"
#include "seam/native_ui/design/shell_strings.hpp"

#include "seam/native_ui/diagnostic_presentation.hpp"
#include "seam/native_ui/tempo_meter_model.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <map>
#include <string>

namespace seam::native_ui::design {
namespace {

using paint::Canvas2D;
using paint::FontRole;
using paint::LinearGradient;
using paint::Path;
using paint::StrokeStyle;
using paint::TextAlign;
using paint::TextStyle;

constexpr Color kWhite{255, 255, 255, 255};
constexpr Color kBlack{0, 0, 0, 255};

TextStyle style(FontRole role, double size, double tracking = 0.0,
                TextAlign align = TextAlign::Left, bool upper = false) {
  return TextStyle{role, size, tracking, align, upper};
}

// The smallest card an overlay can present: one row of controls and a title, which the 480x320
// window's 240x110 body panel still holds.
constexpr double kMinimumPanelWidth = 240.0;
constexpr double kMinimumPanelHeight = 106.0;
// The card's inner left inset; the header's own is 32, which is too generous at the minimum size.
constexpr double kPanelInset = 20.0;

// Fits a card inside the panel, preferring its natural size, never exceeding the panel, and centred
// in it. At the minimum window the card is the panel itself.
ui::Rect fitPanel(ui::Rect panel, double naturalWidth, double naturalHeight) {
  const auto width = std::min(panel.width, naturalWidth);
  const auto height = std::min(panel.height, naturalHeight);
  if (width < kMinimumPanelWidth || height < kMinimumPanelHeight) return {};
  return {panel.x + (panel.width - width) * 0.5, panel.y + (panel.height - height) * 0.5, width,
          height};
}

// The card material the shell's own export and inspector cards use, so a re-homed overlay reads as
// the same surface instead of a panel bolted onto the design.
void card(Canvas2D& c, const DesignTokens& t, ui::Rect r, double radius, double alpha = 0.97) {
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

// The marker light, title and hairline rule the shell's cards carry.
void cardHeader(Canvas2D& c, const DesignTokens& t, ui::Rect panel, std::string_view title) {
  c.save();
  c.setGlow(t.color.accent, 7.0);
  c.fill(Path::circle({panel.x + 20.0, panel.y + 20.0}, 3.5), t.color.accent);
  c.restore();
  c.text({panel.x + 32.0, panel.y + 10.0, std::max(1.0, panel.width - 56.0), 20.0}, title,
         style(FontRole::UiSemibold, t.type.panelTitle, t.type.panelTitleTracking, TextAlign::Left,
               true),
         t.color.textPrimary);
  Path rule;
  rule.moveTo({panel.x + 16.0, panel.y + 38.0}).lineTo({panel.right() - 16.0, panel.y + 38.0});
  if (t.mode == DesignMode::Emo) {
    c.stroke(rule, withAlpha(t.color.textPrimary, 0.16), StrokeStyle{1.0, true, {5.0, 4.0}});
  } else {
    c.stroke(rule,
             LinearGradient{{panel.x, 0.0}, {panel.right(), 0.0},
                            {{0.0, withAlpha(t.color.accent, 0.55)},
                             {1.0, withAlpha(t.color.accentCurve, 0.35)}}},
             StrokeStyle{1.0});
  }
}

const ui::Rect* findControl(const std::vector<OverlayControl>& controls, std::string_view id) {
  for (const auto& control : controls)
    if (control.id == id) return &control.bounds;
  return nullptr;
}

// A text field in the lyric field's skin: a sunken well with the accent ring, its caption at the
// left in the secondary colour and the text as typed after it. Both elide inside the well; the
// field's node carries the complete text.
void paintOverlayTextField(Canvas2D& c, const DesignTokens& t, ui::Rect r, std::string_view caption,
                           std::string_view text, bool active) {
  if (r.width <= 0.0 || r.height <= 0.0) return;
  const auto p = Path::roundedRect(r, 6.0);
  c.fill(p, t.color.surfaceSunken);
  c.save();
  if (active) c.setGlow(t.color.accent, 8.0);
  c.stroke(p, active ? t.color.accent : withAlpha(t.color.border, 0.95), StrokeStyle{1.5});
  c.restore();
  const auto captionStyle = style(FontRole::UiSemibold, t.type.smallLabel, 0.6, TextAlign::Left, true);
  const auto captionWidth =
      caption.empty() ? 0.0 : std::min(c.measure(caption, captionStyle) + 4.0, r.width * 0.42);
  if (captionWidth > 0.0)
    c.text({r.x + 8.0, r.y, captionWidth, r.height}, caption, captionStyle, t.color.textSecondary);
  const auto textX = r.x + 8.0 + (captionWidth > 0.0 ? captionWidth + 8.0 : 0.0);
  c.text({textX, r.y, std::max(1.0, r.right() - 8.0 - textX), r.height}, text,
         style(FontRole::Ui, t.type.lyric), t.color.textPrimary);
}

// Maps a plot the controller measured in the classic layout onto the rectangle the card draws it
// in, one axis at a time: the controller's own plot geometry is linear in x and y, so a point the
// shell maps back lands where the classic plot had it, and a drag reaches the same value.
class AxisMap final {
public:
  AxisMap(ui::Rect source, ui::Rect destination) : source_(source), destination_(destination) {}
  [[nodiscard]] ui::Point map(ui::Point legacy) const {
    return {destination_.x + (legacy.x - source_.x) * sx(), destination_.y + (legacy.y - source_.y) * sy()};
  }
  [[nodiscard]] ui::Point unmap(ui::Point shell) const {
    return {source_.x + (shell.x - destination_.x) / sx(), source_.y + (shell.y - destination_.y) / sy()};
  }

private:
  [[nodiscard]] double sx() const {
    return source_.width > 0.0 ? destination_.width / source_.width : 1.0;
  }
  [[nodiscard]] double sy() const {
    return source_.height > 0.0 ? destination_.height / source_.height : 1.0;
  }
  ui::Rect source_;
  ui::Rect destination_;
};

// "C4" for MIDI 60.
std::string noteName(std::int32_t midi) {
  static constexpr std::array<const char*, 12U> kNames{"C", "C#", "D", "D#", "E", "F",
                                                       "F#", "G", "G#", "A", "A#", "B"};
  const auto clamped = std::clamp(midi, 0, 127);
  return std::string{kNames[static_cast<std::size_t>(clamped % 12)]} + std::to_string(clamped / 12 - 1);
}

// Equal cells in a grid, so no control stretches and none overlaps its neighbour.
std::vector<ui::Rect> grid(ui::Rect panel, double top, double height, std::size_t count,
                           std::size_t perRow, double gap) {
  std::vector<ui::Rect> out;
  constexpr double kSide = 16.0;
  const auto width = std::max(
      1.0, (panel.width - 2.0 * kSide - gap * static_cast<double>(perRow - 1U)) /
               static_cast<double>(perRow));
  for (std::size_t i = 0U; i < count; ++i) {
    const auto column = static_cast<double>(i % perRow);
    const auto row = static_cast<double>(i / perRow);
    out.push_back({panel.x + kSide + column * (width + gap), top + row * (height + 8.0), width,
                   height});
  }
  return out;
}

bool parseIndex(std::string_view text, std::size_t& index) {
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), index);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

// The details toggle's label follows what it will do, as the classic button did.
std::string detailsVisibleLabel(const EditorSceneState& state) {
  return state.sampleMicroscope.has_value() && state.sampleMicroscope->detailsVisible ? tr(Str::Waveform)
                                                                                     : tr(Str::Details);
}

// Applies an overlay node's own activation: a controller id is dispatched by the controller, which
// is the one code path the classic painter's pointer handling used. A shell-owned id is refused
// here (the overlay names only controller nodes).
core::Result<void> activateControllerNode(NativeEditorController& controller, std::string_view id) {
  if (id.starts_with("shell."))
    return core::failure(core::ErrorCode::NotFound, tr(Str::UnknownOverlayControl));
  return controller.dispatchAccessibility(id, SemanticAction::Activate);
}

// Maps a rectangle the controller measured in its own window coordinates onto a card the shell
// draws, scaling uniformly and centring, so the analysis fills the card's content area and its
// plots keep their relative geometry at any window size.
class FitMap final {
public:
  FitMap(ui::Rect source, ui::Rect destination) : source_(source), destination_(destination) {
    scale_ = source.width > 0.0 && source.height > 0.0
                 ? std::min(destination.width / source.width, destination.height / source.height)
                 : 1.0;
    origin_ = {destination.x + (destination.width - source.width * scale_) * 0.5,
               destination.y + (destination.height - source.height * scale_) * 0.5};
  }
  [[nodiscard]] ui::Rect map(ui::Rect legacy) const {
    return {origin_.x + (legacy.x - source_.x) * scale_,
            origin_.y + (legacy.y - source_.y) * scale_, legacy.width * scale_,
            legacy.height * scale_};
  }
  // The controller's own point under a point of the card.
  [[nodiscard]] ui::Point unmap(ui::Point card) const {
    return {source_.x + (card.x - origin_.x) / scale_, source_.y + (card.y - origin_.y) / scale_};
  }
  [[nodiscard]] double scale() const noexcept { return scale_; }
  [[nodiscard]] ui::Rect source() const noexcept { return source_; }
  [[nodiscard]] ui::Rect destination() const noexcept { return destination_; }

private:
  ui::Rect source_;
  ui::Rect destination_;
  double scale_{1.0};
  ui::Point origin_{};
};

// Where the microscope's two plots go inside its card, and how to reach them.
FitMap microscopeMap(const ui::SampleMicroscopeModel& model, ui::Rect card) {
  const auto wave = model.waveformBounds();
  const auto spectrogram = model.spectrogramBounds();
  return FitMap{ui::Rect{wave.x, wave.y, wave.width,
                         std::max(1.0, spectrogram.bottom() - wave.y)},
                ui::Rect{card.x + 16.0, card.y + 58.0, std::max(1.0, card.width - 32.0),
                         std::max(1.0, card.height - 74.0)}};
}

// ---- Sample microscope -------------------------------------------------------------------------

class SampleMicroscopeOverlay final : public ShellOverlay {
public:
  [[nodiscard]] OverlayKind kind() const noexcept override { return OverlayKind::SampleMicroscope; }
  [[nodiscard]] std::string_view idPrefix() const noexcept override {
    return "shell.overlay.microscope.";
  }
  [[nodiscard]] bool wanted(const NativeEditorController& controller,
                            const EditorSceneState& state) const noexcept override {
    return state.sampleMicroscope.has_value() && state.sampleMicroscope->model != nullptr &&
           controller.sampleMicroscopeOpen();
  }
  [[nodiscard]] ui::Rect panel(const NativeEditorController&, const EditorSceneState&,
                               const SingLayout& layout, ui::Rect slot) const override {
    if (slot.width <= 0.0 || slot.height <= 0.0) return {};
    // A large sheet: most of the body, never the whole window, so the header stays reachable.
    (void)layout;
    return fitPanel(slot, std::max(slot.width * 0.94, 420.0), std::max(slot.height * 0.94, 260.0));
  }
  [[nodiscard]] std::string title(const NativeEditorController& controller,
                                  const EditorSceneState&) const override {
    const auto& unit = controller.sampleMicroscopeUnitId();
    return unit.empty() ? std::string{tr(Str::Sample)} : tr(Str::Sample2) + unit;
  }
  [[nodiscard]] std::vector<OverlayControl> controls(const NativeEditorController&,
                                                     const EditorSceneState&,
                                                     const SingLayout&, ui::Rect panel) const override;
  void paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController& controller,
             const EditorSceneState& state, const SingLayout& layout, ui::Rect panel,
             const std::vector<OverlayControl>& controls) const override;
  core::Result<void> perform(NativeEditorController& controller, std::string_view id,
                             SemanticAction action) const override;
  bool key(NativeEditorController& controller, std::string_view focusedId,
           const KeyEvent& event) const override;
  core::Result<void> close(NativeEditorController& controller) const override {
    controller.closeSampleMicroscope();
    return core::success();
  }
  // A press on either plot reaches the controller at the point it measured there: a marker or a
  // pitch mark starts the same drag the classic microscope started, and a double-click auditions
  // the unit (or closes the microscope when the host cannot play one), as it always did.
  OverlayPress press(NativeEditorController& controller, const EditorSceneState& state,
                     const SingLayout& layout, ui::Rect panel,
                     const PointerEvent& event) const override;
  core::Result<void> drag(NativeEditorController& controller, const OverlayGesture& gesture,
                          const PointerEvent& event, bool release) const override {
    auto legacy = event;
    legacy.position = FitMap{gesture.source, gesture.destination}.unmap(event.position);
    return release ? controller.pointerUp(legacy) : controller.pointerMove(legacy);
  }
  bool back(NativeEditorController& controller) const override {
    // As the classic microscope bound Escape: the details page returns to the waveform first.
    if (!controller.sceneState().sampleMicroscope.has_value() ||
        !controller.sceneState().sampleMicroscope->detailsVisible)
      return false;
    static_cast<void>(controller.dispatchAccessibility("microscope.details", SemanticAction::Activate));
    return true;
  }
};

std::vector<OverlayControl> SampleMicroscopeOverlay::controls(
    const NativeEditorController&, const EditorSceneState& state, const SingLayout&,
    ui::Rect panel) const {
  std::vector<OverlayControl> out;
  if (panel.width <= 0.0) return out;
  // The header controls sit at the card's top right; a card too narrow for both puts Details on a
  // second row, so no control is ever clipped at the minimum window.
  constexpr double kHeaderButton = 26.0;
  const auto width = std::min(92.0, std::max(64.0, (panel.width - 2.0 * kPanelInset - 8.0) * 0.5));
  const auto right = panel.right() - kPanelInset;
  out.push_back({"microscope.close", {right - width, panel.y + 8.0, width, kHeaderButton}, tr(Str::Close)});
  const auto detailsX = right - 2.0 * width - 8.0;
  out.push_back({"microscope.details",
                 {detailsX >= panel.x + kPanelInset ? detailsX : panel.x + kPanelInset,
                  detailsX >= panel.x + kPanelInset ? panel.y + 8.0 : panel.y + 38.0, width,
                  kHeaderButton},
                 detailsVisibleLabel(state)});
  if (!state.sampleMicroscope.has_value() || !state.sampleMicroscope->detailsVisible) return out;
  // The pager sits along the card's bottom, matching the classic details page buttons.
  const auto top = std::max(panel.y + 58.0, panel.bottom() - 32.0);
  out.push_back({"microscope.previous", {panel.x + kPanelInset, top, 88.0, 26.0}, tr(Str::Previous),
                 SemanticRole::Button, state.sampleMicroscope->detailsPage > 0U});
  out.push_back({"microscope.next", {panel.x + kPanelInset + 96.0, top, 88.0, 26.0}, tr(Str::Next),
                 SemanticRole::Button,
                 state.sampleMicroscope->detailsPage + 1U < state.sampleMicroscope->detailsPageCount});
  return out;
}

void SampleMicroscopeOverlay::paint(Canvas2D& c, const DesignTokens& t,
                                    const NativeEditorController&, const EditorSceneState& state,
                                    const SingLayout&, ui::Rect panel,
                                    const std::vector<OverlayControl>& controls) const {
  if (!state.sampleMicroscope.has_value() || state.sampleMicroscope->model == nullptr) return;
  const auto& view = *state.sampleMicroscope;
  const auto& model = *view.model;
  c.save();
  c.clipRect(panel);
  const auto caption = view.destinationContext.empty() ? std::string{tr(Str::DestinationUnknown)}
                                                       : view.destinationContext;
  c.text({panel.x + kPanelInset, panel.y + 34.0, std::max(1.0, panel.width - 2.0 * kPanelInset - 168.0), 18.0}, caption,
         style(FontRole::Ui, t.type.smallLabel), t.color.textSecondary);
  if (const auto* close = findControl(controls, "microscope.close"))
    paintOverlayControl(c, t, *close, tr(Str::Close), SemanticRole::Button, true, false, false);
  if (const auto* details = findControl(controls, "microscope.details"))
    paintOverlayControl(c, t, *details, detailsVisibleLabel(state), SemanticRole::Button, true,
                        false, false);

  if (view.detailsVisible) {
    // The captured selection and destination, exactly the lines the controller publishes.
    const ui::Rect body{panel.x + 16.0, panel.y + 56.0, std::max(1.0, panel.width - 32.0),
                        std::max(1.0, panel.height - 98.0)};
    sunken(c, t, body, 8.0);
    constexpr double kLine = 17.0;
    for (std::size_t i = 0U; i < view.detailsLines.size(); ++i) {
      const auto top = body.y + 8.0 + static_cast<double>(i) * kLine;
      if (top + kLine > body.bottom() - 6.0) break;
      std::string_view line{view.detailsLines[i]};
      while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.remove_suffix(1U);
      c.text({body.x + 8.0, top, std::max(1.0, body.width - 16.0), kLine}, line,
             style(FontRole::Mono, t.type.rulerMicro + 1.0), t.color.textPrimary);
    }
    const auto* previous = findControl(controls, "microscope.previous");
    const auto* next = findControl(controls, "microscope.next");
    if (previous != nullptr)
      paintOverlayControl(c, t, *previous, tr(Str::Previous), SemanticRole::Button,
                          view.detailsPage > 0U, false, false);
    if (next != nullptr) {
      paintOverlayControl(c, t, *next, tr(Str::Next), SemanticRole::Button,
                          view.detailsPage + 1U < view.detailsPageCount, false, false);
      c.text({next->right() + 12.0, next->y, std::max(1.0, panel.right() - next->right() - 28.0),
              next->height},
             tr(Str::Page) + std::to_string(view.detailsPage + 1U) + " / " +
                 std::to_string(view.detailsPageCount),
             style(FontRole::Ui, t.type.smallLabel), t.color.textSecondary);
    }
  } else {
    // The model measured its plots in the controller's own window coordinates; the card maps them
    // onto its content area, so the analysis fills the sheet at every window size.
    const auto fit = microscopeMap(model, panel);
    const auto wave = fit.map(model.waveformBounds());
    const auto spectrogram = fit.map(model.spectrogramBounds());
    sunken(c, t, wave, 6.0);
    sunken(c, t, spectrogram, 6.0);
    const auto plotLabel = style(FontRole::UiSemibold, t.type.smallLabel, 1.0, TextAlign::Left, true);
    c.text({wave.x + 8.0, wave.y + 4.0, std::max(1.0, wave.width - 16.0), 14.0}, tr(Str::Waveform),
           plotLabel, t.color.textSecondary);
    c.save();
    c.clipRect(panel);
    c.clipRect(wave);
    const auto center = wave.y + wave.height * 0.5;
    for (const auto& column : model.waveform()) {
      const auto x = fit.map({column.x, 0.0, 0.0, 0.0}).x;
      Path stem;
      stem.moveTo({x, center - column.maximum * wave.height * 0.42})
          .lineTo({x, center - column.minimum * wave.height * 0.42});
      c.stroke(stem, withAlpha(t.color.accentCurve, 0.9), StrokeStyle{1.0});
    }
    c.restore();
    c.save();
    c.clipRect(spectrogram);
    c.fill(Path::rect(spectrogram), withAlpha(t.color.canvas, 0.5));
    const auto& data = model.spectrogram();
    if (data.columns > 0U && data.bins > 0U && data.decibels.size() == data.columns * data.bins) {
      const auto columnStep =
          std::max<std::size_t>(1U, data.columns / std::max<std::size_t>(1U, 420U));
      const auto binStep = std::max<std::size_t>(1U, data.bins / std::max<std::size_t>(1U, 160U));
      const auto logBins =
          std::log1p(static_cast<double>(std::max<std::size_t>(1U, data.bins - 1U)));
      for (std::size_t column = 0U; column < data.columns; column += columnStep) {
        for (std::size_t bin = 0U; bin < data.bins; bin += binStep) {
          const auto db = std::clamp((data.at(column, bin) + 90.0F) / 84.0F, 0.0F, 1.0F);
          if (db < 0.04F) continue;
          const auto intensity = std::pow(std::clamp((db - 0.04F) / 0.96F, 0.0F, 1.0F), 0.7F);
          const auto x = spectrogram.x + static_cast<double>(column) /
                                             static_cast<double>(data.columns) * spectrogram.width;
          const auto lower = std::log1p(static_cast<double>(bin)) / logBins;
          const auto upper =
              std::log1p(static_cast<double>(std::min(data.bins - 1U, bin + binStep))) / logBins;
          const auto y = spectrogram.bottom() - upper * spectrogram.height;
          const auto cellHeight = std::max(1.0, (upper - lower) * spectrogram.height);
          // A conic heat colormap per mode: EMO runs ember to bone, SCENE runs violet to lime.
          const auto heat =
              t.mode == DesignMode::Emo
                  ? Color{static_cast<std::uint8_t>(40.0 + 200.0 * intensity),
                          static_cast<std::uint8_t>(12.0 + 130.0 * intensity * intensity),
                          static_cast<std::uint8_t>(24.0 + 70.0 * intensity * intensity), 255}
                  : Color{static_cast<std::uint8_t>(40.0 + 150.0 * (1.0 - intensity)),
                          static_cast<std::uint8_t>(30.0 + 215.0 * intensity),
                          static_cast<std::uint8_t>(80.0 + 160.0 * (1.0 - intensity)), 255};
          c.fill(Path::rect({x, y,
                             std::max(1.0, spectrogram.width * static_cast<double>(columnStep) /
                                               static_cast<double>(data.columns)),
                             cellHeight}),
                 heat);
        }
      }
    }
    c.text({spectrogram.x + 8.0, spectrogram.y + 4.0, std::max(1.0, spectrogram.width - 16.0), 14.0},
           tr(Str::Spectrogram), plotLabel, t.color.textSecondary);
    // Markers and pitch marks span both plots, as they do in the classic painter.
    for (const auto& marker : model.markers()) {
      const auto x = fit.map({marker.x, 0.0, 0.0, 0.0}).x;
      Path line;
      line.moveTo({x, wave.y}).lineTo({x, spectrogram.bottom()});
      c.stroke(line,
               marker.kind == ui::AcousticMarkerKind::VowelOnset ? t.color.accentTime
                                                                 : t.color.accent,
               StrokeStyle{1.0});
    }
    for (const auto& mark : model.pitchMarks()) {
      const auto x = fit.map({mark.x, 0.0, 0.0, 0.0}).x;
      Path line;
      line.moveTo({x, wave.y}).lineTo({x, wave.bottom()});
      c.stroke(line, mark.locked ? t.color.accentCurve : t.color.accentAlt2, StrokeStyle{1.0});
    }
    c.restore();
  }
  c.restore();
}

OverlayPress SampleMicroscopeOverlay::press(NativeEditorController& controller,
                                            const EditorSceneState& state, const SingLayout&,
                                            ui::Rect panel, const PointerEvent& event) const {
  if (!state.sampleMicroscope.has_value() || state.sampleMicroscope->model == nullptr ||
      state.sampleMicroscope->detailsVisible)
    return {};
  const auto& model = *state.sampleMicroscope->model;
  const auto fit = microscopeMap(model, panel);
  const auto wave = fit.map(model.waveformBounds());
  const auto spectrogram = fit.map(model.spectrogramBounds());
  if (!wave.contains(event.position) && !spectrogram.contains(event.position)) return {};
  auto legacy = event;
  legacy.position = fit.unmap(event.position);
  OverlayPress out{.handled = true, .result = controller.pointerDown(legacy)};
  if (out.result && controller.pointerGestureActive())
    out.gesture = OverlayGesture{fit.source(), fit.destination()};
  return out;
}

core::Result<void> SampleMicroscopeOverlay::perform(NativeEditorController& controller,
                                                    std::string_view id,
                                                    SemanticAction action) const {
  if (action != SemanticAction::Activate && action != SemanticAction::Toggle)
    return core::failure(core::ErrorCode::Unsupported, tr(Str::ThisControlOnlyActivates));
  // The pager goes through the controller's own ids; Close and Details are the same commands the
  // classic painter called for those rectangles.
  return activateControllerNode(controller, id);
}

bool SampleMicroscopeOverlay::key(NativeEditorController& controller, std::string_view focusedId,
                                  const KeyEvent& event) const {
  if (event.key == NativeKey::Enter || event.key == NativeKey::Space) {
    static_cast<void>(perform(controller, focusedId, SemanticAction::Activate));
    return true;
  }
  if (event.key == NativeKey::D) {
    // D toggles the details page, as it did on the classic microscope.
    static_cast<void>(controller.dispatchAccessibility("microscope.details", SemanticAction::Activate));
    return true;
  }
  if (event.key == NativeKey::Left || event.key == NativeKey::Right) {
    static_cast<void>(controller.dispatchAccessibility(
        event.key == NativeKey::Left ? "microscope.previous" : "microscope.next",
        SemanticAction::Activate));
    return true;
  }
  return false;
}

// ---- Phoneme review ----------------------------------------------------------------------------

class PhonemeReviewOverlay final : public ShellOverlay {
public:
  [[nodiscard]] OverlayKind kind() const noexcept override { return OverlayKind::PhonemeReview; }
  [[nodiscard]] std::string_view idPrefix() const noexcept override {
    return "shell.overlay.phoneme.";
  }
  [[nodiscard]] bool wanted(const NativeEditorController&, const EditorSceneState& state) const
      noexcept override {
    return state.phonemeReview.visible;
  }
  [[nodiscard]] ui::Rect panel(const NativeEditorController&, const EditorSceneState&,
                               const SingLayout& layout, ui::Rect slot) const override {
    if (slot.width <= 0.0 || slot.height <= 0.0) return {};
    // Anchored to the phoneme lane strip, opening upward from it, clamped into the body slot.
    const auto width = std::min(slot.width, std::min(560.0, layout.width - 32.0));
    const auto height = std::min(slot.height, std::max(180.0, std::min(240.0, slot.height)));
    if (width < kMinimumPanelWidth || height < kMinimumPanelHeight) return {};
    const auto anchor = layout.laneReviewButton.width > 0.0 ? layout.laneReviewButton
                                                           : layout.laneTabs;
    return {std::clamp(anchor.x, slot.x, std::max(slot.x, slot.right() - width)),
            std::clamp(anchor.y - height - 6.0, slot.y,
                       std::max(slot.y, slot.bottom() - height)),
            width, height};
  }
  [[nodiscard]] std::string title(const NativeEditorController&,
                                  const EditorSceneState&) const override {
    return tr(Str::PhonemeReview);
  }
  [[nodiscard]] std::vector<OverlayControl> controls(const NativeEditorController&,
                                                     const EditorSceneState& state,
                                                     const SingLayout&,
                                                     ui::Rect panel) const override {
    std::vector<OverlayControl> out;
    if (panel.width <= 0.0) return out;
    static constexpr std::array<const char*, 6U> kIds{
        "phoneme.review.action.0", "phoneme.review.action.1", "phoneme.review.action.2",
        "phoneme.review.action.3", "phoneme.review.action.4", "phoneme.review.action.5"};
    static constexpr std::array<Str, 6U> kNames{Str::PreviousEdit, Str::NextEdit, Str::Close,
                                                        Str::PreviousSound, Str::NextSound,
                                                        Str::ApplyBinding};
    const auto cells = grid(panel, panel.bottom() - 72.0, 28.0, 6U, 3U, 8.0);
    for (std::size_t i = 0U; i < cells.size(); ++i)
      out.push_back({kIds[i], cells[i], tr(kNames[i]), SemanticRole::Button,
                     i < state.phonemeReview.enabled.size() && state.phonemeReview.enabled[i]});
    return out;
  }
  [[nodiscard]] std::string openerId(const NativeEditorController&,
                                     const EditorSceneState&) const override {
    return "shell.lane.review";
  }
  void paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController&,
             const EditorSceneState& state, const SingLayout&, ui::Rect panel,
             const std::vector<OverlayControl>& controls) const override;
  core::Result<void> perform(NativeEditorController& controller, std::string_view id,
                             SemanticAction action) const override;
  bool key(NativeEditorController& controller, std::string_view focusedId,
           const KeyEvent& event) const override;
  core::Result<void> close(NativeEditorController& controller) const override {
    // Action 2 is the review's own Close, the command the classic panel ran for that button.
    return controller.activatePhonemeReview(2U);
  }
};

void PhonemeReviewOverlay::paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController&,
                                 const EditorSceneState& state, const SingLayout&, ui::Rect panel,
                                 const std::vector<OverlayControl>& controls) const {
  const auto& view = state.phonemeReview;
  c.save();
  c.clipRect(panel);
  const auto row = [&](double y, std::string_view label, const std::string& text, Color color) {
    c.text({panel.x + kPanelInset, y, 72.0, 18.0}, label,
           style(FontRole::UiSemibold, t.type.smallLabel, t.type.labelTracking, TextAlign::Left,
                 true),
           t.color.textSecondary);
    c.text({panel.x + 108.0, y, std::max(1.0, panel.width - 140.0), 18.0}, text,
           style(FontRole::Mono, t.type.smallLabel), color);
  };
  row(panel.y + 46.0, tr(Str::Source), view.source, t.color.textPrimary);
  row(panel.y + 64.0, tr(Str::Target), view.target, t.color.textPrimary);
  row(panel.y + 82.0, tr(Str::Status), view.status,
      view.available ? t.color.textSecondary : t.color.warning);
  static constexpr std::array<Str, 6U> kNames{Str::PreviousEdit, Str::NextEdit, Str::Close,
                                                      Str::PreviousSound, Str::NextSound,
                                                      Str::ApplyBinding};
  for (std::size_t i = 0U; i < controls.size() && i < kNames.size(); ++i) {
    const auto enabled = i < view.enabled.size() && view.enabled[i];
    paintOverlayControl(c, t, controls[i].bounds, tr(kNames[i]), SemanticRole::Button, enabled, false,
                        false);
  }
  c.restore();
}

core::Result<void> PhonemeReviewOverlay::perform(NativeEditorController& controller,
                                                 std::string_view id, SemanticAction action) const {
  if (action != SemanticAction::Activate && action != SemanticAction::Toggle)
    return core::failure(core::ErrorCode::Unsupported, tr(Str::ThisControlOnlyActivates));
  return activateControllerNode(controller, id);
}

bool PhonemeReviewOverlay::key(NativeEditorController& controller, std::string_view focusedId,
                               const KeyEvent& event) const {
  if (event.key == NativeKey::Enter || event.key == NativeKey::Space) {
    static_cast<void>(perform(controller, focusedId, SemanticAction::Activate));
    return true;
  }
  if (event.key == NativeKey::Left || event.key == NativeKey::Right) {
    // The review's own previous/next actions, as the classic panel bound them.
    static_cast<void>(controller.activatePhonemeReview(event.key == NativeKey::Left ? 0U : 1U));
    return true;
  }
  return false;
}

// ---- Time map ----------------------------------------------------------------------------------

class TimeMapOverlay final : public ShellOverlay {
public:
  [[nodiscard]] OverlayKind kind() const noexcept override { return OverlayKind::TimeMap; }
  [[nodiscard]] std::string_view idPrefix() const noexcept override {
    return "shell.overlay.time-map.";
  }
  [[nodiscard]] bool wanted(const NativeEditorController&, const EditorSceneState& state) const
      noexcept override {
    return state.timeMapVisible;
  }
  [[nodiscard]] ui::Rect panel(const NativeEditorController&, const EditorSceneState&,
                               const SingLayout&, ui::Rect panel) const override {
    // A popover from the transport display, centred over the body like a sheet.
    return timeMapPanelBounds(panel);
  }
  [[nodiscard]] std::string title(const NativeEditorController&,
                                  const EditorSceneState&) const override {
    return tr(Str::TempoMeterEvents);
  }
  [[nodiscard]] std::vector<OverlayControl> controls(const NativeEditorController&,
                                                     const EditorSceneState& state,
                                                     const SingLayout&, ui::Rect panel) const override;
  [[nodiscard]] std::string openerId(const NativeEditorController&,
                                     const EditorSceneState&) const override {
    return "shell.ruler.time-map";
  }
  void paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController&,
             const EditorSceneState& state, const SingLayout&, ui::Rect panel,
             const std::vector<OverlayControl>& controls) const override;
  core::Result<void> perform(NativeEditorController& controller, std::string_view id,
                             SemanticAction action) const override;
  bool key(NativeEditorController& controller, std::string_view focusedId,
           const KeyEvent& event) const override;
  core::Result<void> close(NativeEditorController& controller) const override {
    // Action 5 is the panel's own Close, the command the classic surface ran for that button.
    return controller.timeMapPanelAction(5U);
  }
  bool back(NativeEditorController& controller) const override {
    // An open event field is cancelled first, as the classic panel's Escape cancelled it, and the
    // map stays open.
    if (controller.textFieldView().kind != NativeEditorController::TextFieldView::Kind::TimeMap)
      return false;
    controller.cancelTextComposition();
    return true;
  }
  core::Result<void> setValue(NativeEditorController& controller, std::string_view id,
                              std::string_view value) const override {
    // The event field's value goes through the controller's own value path for that field, which
    // validates and commits it exactly as typing and Enter would.
    const auto field = controller.textFieldView();
    if (field.kind != NativeEditorController::TextFieldView::Kind::TimeMap || id != field.inputId)
      return core::failure(core::ErrorCode::Unsupported, tr(Str::ThisControlHasNoEditableValue));
    return controller.setAccessibilityValue(id, value);
  }
};

std::vector<OverlayControl> TimeMapOverlay::controls(const NativeEditorController& controller,
                                                     const EditorSceneState& state,
                                                     const SingLayout&, ui::Rect panel) const {
  std::vector<OverlayControl> out;
  if (panel.width <= 0.0) return out;
  const auto field = controller.textFieldView();
  const auto editing = field.kind == NativeEditorController::TextFieldView::Kind::TimeMap;
  if (editing) {
    // The event field, where the classic panel drew it over its first row; the host's text input
    // client is placed on the same rectangle (timeMapFieldPlacement).
    const auto placement = timeMapFieldPlacement(panel);
    OverlayControl input{field.inputId, placement.input, field.inputName, SemanticRole::TextField,
                         true, false, false};
    input.value = field.text;
    input.description = field.error;
    input.editable = true;
    out.push_back(std::move(input));
    out.push_back({field.cancelId, placement.cancel, field.cancelName});
  }
  static constexpr std::array<const char*, 8U> kIds{
      "time-map-action.0", "time-map-action.1", "time-map-action.2", "time-map-action.3",
      "time-map-action.4", "time-map-action.5", "time-map-action.6", "time-map-action.7"};
  // A short card keeps the eight actions in two rows of four; a tall one separates the rows above
  // them. The rows come first in Tab order, as the classic panel listed them.
  constexpr double kActionHeight = 26.0;
  const auto actionsHeight = 2.0 * (kActionHeight + 8.0) - 8.0;
  const auto actionsTop = panel.bottom() - 12.0 - actionsHeight;
  // Each row is a 24-point hit target with a 2-point gap.
  constexpr double kRowHeight = 26.0;
  // The prompt (or the event field) sits above the rows, never under them.
  const auto rowsTop = panel.y + (editing ? 76.0 : 62.0);
  const auto capacity = actionsTop > rowsTop
                            ? static_cast<std::size_t>((actionsTop - rowsTop) / kRowHeight)
                            : 0U;
  for (std::size_t i = 0U; i < state.timeMapRows.size() && i < capacity; ++i) {
    const auto selected = state.timeMapSelectedRow == i;
    // While the event field is open the rows and actions are disabled, as the classic panel
    // disabled them; the row's full text is its value.
    OverlayControl row{"time-map-row." + std::to_string(i),
                       {panel.x + kPanelInset, rowsTop + static_cast<double>(i) * kRowHeight,
                        std::max(1.0, panel.width - 2.0 * kPanelInset), kRowHeight - 2.0},
                       tr(Str::EventRow) + std::to_string(i + 1U), SemanticRole::Button, !editing,
                       selected};
    row.value = state.timeMapRows[i];
    out.push_back(std::move(row));
  }
  static constexpr std::array<Str, 8U> kNames{
      Str::PreviousPage, Str::NextPage, Str::EditSelectedEvent, Str::RemoveSelectedEvent,
      Str::RefreshEvents, Str::CloseTimeMaps, Str::AddTempo, Str::AddMeter};
  const auto cells = grid(panel, actionsTop, kActionHeight, 8U, 4U, 6.0);
  for (std::size_t i = 0U; i < cells.size(); ++i)
    out.push_back({kIds[i], cells[i], tr(kNames[i]), SemanticRole::Button, !editing});
  return out;
}

void TimeMapOverlay::paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController&,
                           const EditorSceneState& state, const SingLayout&, ui::Rect panel,
                           const std::vector<OverlayControl>& controls) const {
  c.save();
  c.clipRect(panel);
  const auto editing = !controls.empty() && controls.front().role == SemanticRole::TextField;
  if (editing) {
    const auto& input = controls.front();
    paintOverlayTextField(c, t, input.bounds, input.name, input.value, true);
    if (controls.size() > 1U)
      paintOverlayControl(c, t, controls[1].bounds, tr(Str::Cancel), SemanticRole::Button, true, false,
                          false);
  } else {
    const auto prompt =
        !state.timeMapPrompt.empty()
            ? state.timeMapPrompt
            : state.timeMapStale
                  ? std::string{tr(Str::ChangedRefreshBeforeEditing)}
                  : std::string{tr(Str::ArrowsSelectEnterEditDeleteRemove)};
    c.text({panel.x + kPanelInset, panel.y + 42.0,
            std::max(1.0, panel.width - 2.0 * kPanelInset), 18.0},
           prompt, style(FontRole::Ui, t.type.smallLabel),
           state.timeMapStale ? t.color.warning : t.color.textSecondary);
  }
  for (std::size_t i = 0U; i < state.timeMapRows.size(); ++i) {
    const auto* bounds = findControl(controls, "time-map-row." + std::to_string(i));
    if (bounds == nullptr) continue;
    const auto selected = state.timeMapSelectedRow == i;
    if (selected) c.fill(Path::roundedRect(*bounds, 4.0), withAlpha(t.color.accent, 0.18));
    c.text({bounds->x + 6.0, bounds->y, std::max(1.0, bounds->width - 12.0), bounds->height},
           state.timeMapRows[i], style(FontRole::Mono, t.type.smallLabel),
           selected ? t.color.accent : t.color.textPrimary);
  }
  static constexpr std::array<Str, 8U> kLabels{
      Str::Previous, Str::Next, Str::Edit, Str::Remove, Str::Refresh, Str::Close, Str::AddTempo, Str::AddMeter};
  for (std::size_t i = 0U; i < kLabels.size(); ++i) {
    const auto* bounds = findControl(controls, "time-map-action." + std::to_string(i));
    if (bounds == nullptr) continue;
    paintOverlayControl(c, t, *bounds, tr(kLabels[i]), SemanticRole::Button, !editing, false, false);
  }
  c.restore();
}

core::Result<void> TimeMapOverlay::perform(NativeEditorController& controller, std::string_view id,
                                           SemanticAction action) const {
  const auto field = controller.textFieldView();
  if (field.kind == NativeEditorController::TextFieldView::Kind::TimeMap) {
    // The event field takes its text from the host's input client; activating it does nothing
    // more, and its Cancel is the classic panel's own cancel node.
    if (id == field.inputId)
      return action == SemanticAction::EditText || action == SemanticAction::Activate
                 ? core::success()
                 : core::failure(core::ErrorCode::Unsupported, tr(Str::TheFieldTakesText));
    if (id == field.cancelId && (action == SemanticAction::Activate || action == SemanticAction::Toggle))
      return controller.dispatchAccessibility(id, SemanticAction::Activate);
  }
  if (action != SemanticAction::Activate && action != SemanticAction::Toggle)
    return core::failure(core::ErrorCode::Unsupported, tr(Str::ThisControlOnlyActivates));
  constexpr std::string_view kRow{"time-map-row."};
  constexpr std::string_view kAction{"time-map-action."};
  if (id.starts_with(kRow)) {
    std::size_t index = 0U;
    if (!parseIndex(id.substr(kRow.size()), index)) return core::failure(
        core::ErrorCode::InvalidArgument, tr(Str::InvalidTimeMapRow));
    return controller.selectTimeMapRow(index);
  }
  if (id.starts_with(kAction)) {
    std::size_t index = 0U;
    if (!parseIndex(id.substr(kAction.size()), index) || index > 7U)
      return core::failure(core::ErrorCode::InvalidArgument, tr(Str::InvalidTimeMapAction));
    return controller.timeMapPanelAction(index);
  }
  // Close is the panel's own action 5, the same command the classic surface ran.
  return core::failure(core::ErrorCode::NotFound, tr(Str::UnknownOverlayControl));
}

bool TimeMapOverlay::key(NativeEditorController& controller, std::string_view focusedId,
                         const KeyEvent& event) const {
  switch (event.key) {
    case NativeKey::Enter:
      // As the classic panel bound Enter: a focused row is selected and then edited, a focused
      // action runs, and with neither the selected event is edited.
      if (focusedId.starts_with("time-map-row.")) {
        if (perform(controller, focusedId, SemanticAction::Activate))
          static_cast<void>(controller.timeMapPanelAction(2U));
      } else if (focusedId.starts_with("time-map-action.")) {
        static_cast<void>(perform(controller, focusedId, SemanticAction::Activate));
      } else {
        static_cast<void>(controller.timeMapPanelAction(2U));
      }
      return true;
    case NativeKey::Space:
      static_cast<void>(perform(controller, focusedId, SemanticAction::Activate));
      return true;
    case NativeKey::Up:
    case NativeKey::Down:
      static_cast<void>(controller.navigateTimeMapRow(event.key == NativeKey::Down ? 1 : -1));
      return true;
    case NativeKey::Left:
    case NativeKey::Right:
      static_cast<void>(controller.timeMapPanelAction(event.key == NativeKey::Left ? 0U : 1U));
      return true;
    case NativeKey::Delete:
    case NativeKey::Backspace:
      static_cast<void>(controller.timeMapPanelAction(3U));
      return true;
    case NativeKey::R:
      static_cast<void>(controller.timeMapPanelAction(4U));
      return true;
    case NativeKey::N:
      static_cast<void>(controller.timeMapPanelAction(event.modifiers.shift ? 7U : 6U));
      return true;
    default: return false;
  }
}

// ---- Recovery / support ------------------------------------------------------------------------

class RecoverySupportOverlay final : public ShellOverlay {
public:
  [[nodiscard]] OverlayKind kind() const noexcept override { return OverlayKind::RecoverySupport; }
  [[nodiscard]] std::string_view idPrefix() const noexcept override {
    return "shell.overlay.support.";
  }
  [[nodiscard]] bool wanted(const NativeEditorController&, const EditorSceneState& state) const
      noexcept override {
    return state.recoverySupport.visible;
  }
  [[nodiscard]] ui::Rect panel(const NativeEditorController&, const EditorSceneState&,
                               const SingLayout&, ui::Rect panel) const override {
    if (panel.width <= 0.0 || panel.height <= 0.0) return {};
    // A settings sheet: the centred card, sized to its content.
    return fitPanel(panel, std::min(panel.width, 560.0), std::min(panel.height, 420.0));
  }
  [[nodiscard]] std::string title(const NativeEditorController&,
                                  const EditorSceneState& state) const override {
    return state.recoverySupport.mode == RecoverySupportMode::Preview ? tr(Str::SupportReportPreview)
                                                                     : tr(Str::LocalSupportReports);
  }
  [[nodiscard]] std::vector<OverlayControl> controls(const NativeEditorController&,
                                                     const EditorSceneState& state,
                                                     const SingLayout&, ui::Rect panel) const override;
  [[nodiscard]] std::string openerId(const NativeEditorController&,
                                     const EditorSceneState&) const override {
    return "shell.settings";
  }
  void paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController&,
             const EditorSceneState& state, const SingLayout&, ui::Rect panel,
             const std::vector<OverlayControl>& controls) const override;
  core::Result<void> perform(NativeEditorController& controller, std::string_view id,
                             SemanticAction action) const override;
  bool key(NativeEditorController& controller, std::string_view focusedId,
           const KeyEvent& event) const override;
  core::Result<void> close(NativeEditorController& controller) const override {
    // The classic surface's own close: the host's support view goes empty.
    controller.setRecoverySupportView({});
    return core::success();
  }
};

std::vector<OverlayControl> RecoverySupportOverlay::controls(const NativeEditorController&,
                                                             const EditorSceneState& state,
                                                             const SingLayout&,
                                                             ui::Rect panel) const {
  std::vector<OverlayControl> out;
  if (panel.width <= 0.0) return out;
  out.push_back({"support.track.previous", {panel.x + kPanelInset, panel.y + 40.0, 68.0, 24.0},
                 tr(Str::PreviousVocalTrack)});
  out.push_back({"support.track.next", {panel.x + 106.0, panel.y + 40.0, 68.0, 24.0},
                 tr(Str::NextVocalTrack)});
  constexpr double kItem = 60.0;
  const auto top = panel.y + 72.0;
  const auto selectable = state.recoverySupport.mode == RecoverySupportMode::Reports;
  for (std::size_t i = state.recoverySupport.firstVisibleItem;
       i < state.recoverySupport.items.size(); ++i) {
    const auto y = top + static_cast<double>(i - state.recoverySupport.firstVisibleItem) * kItem;
    if (y + kItem - 8.0 > panel.bottom() - 8.0) break;
    const auto& item = state.recoverySupport.items[i];
    out.push_back({"support.item." + std::to_string(i),
                   {panel.x + kPanelInset, y, std::max(1.0, panel.width - 2.0 * kPanelInset),
                    kItem - 10.0},
                   item.name, selectable ? SemanticRole::Button : SemanticRole::Status, true,
                   item.selected, selectable});
  }
  return out;
}

void RecoverySupportOverlay::paint(Canvas2D& c, const DesignTokens& t,
                                   const NativeEditorController&, const EditorSceneState& state,
                                   const SingLayout&, ui::Rect panel,
                                   const std::vector<OverlayControl>& controls) const {
  const auto& support = state.recoverySupport;
  c.save();
  c.clipRect(panel);
  const auto preview = support.mode == RecoverySupportMode::Preview;
  const auto summary = preview ? tr(Str::Candidate) + support.candidateId
                               : std::to_string(support.reportCount) + tr(Str::OwnedReport) +
                                     (support.reportCount == 1U ? "" : "s");
  c.text({panel.x + kPanelInset, panel.y + 44.0, std::max(1.0, panel.width - 2.0 * kPanelInset), 16.0}, summary,
         style(FontRole::Ui, t.type.smallLabel), t.color.textPrimary);
  if (preview) {
    const auto sha = support.archiveSha256.substr(
        0U, std::min<std::size_t>(12U, support.archiveSha256.size()));
    c.text({panel.x + kPanelInset, panel.y + 60.0, std::max(1.0, panel.width - 2.0 * kPanelInset), 14.0},
           tr(Str::ZIP) + std::to_string(support.archiveBytes) + tr(Str::BSHA256) + sha,
           style(FontRole::Mono, t.type.rulerMicro), t.color.textSecondary);
  }
  if (const auto* previous = findControl(controls, "support.track.previous"))
    paintOverlayControl(c, t, *previous, tr(Str::Prev), SemanticRole::Button, true, false, false);
  if (const auto* next = findControl(controls, "support.track.next"))
    paintOverlayControl(c, t, *next, tr(Str::Next), SemanticRole::Button, true, false, false);
  for (std::size_t i = support.firstVisibleItem; i < support.items.size(); ++i) {
    const auto* bounds = findControl(controls, "support.item." + std::to_string(i));
    if (bounds == nullptr) continue;
    const auto& item = support.items[i];
    const auto selectable = support.mode == RecoverySupportMode::Reports;
    c.fill(Path::roundedRect(*bounds, 6.0),
           item.selected ? withAlpha(t.color.accent, 0.22) : withAlpha(t.color.surfaceSunken, 0.85));
    c.stroke(Path::roundedRect(*bounds, 6.0),
             item.selected ? t.color.accent : withAlpha(t.color.border, 0.9), StrokeStyle{1.0});
    c.text({bounds->x + 10.0, bounds->y + 4.0, std::max(1.0, bounds->width - 20.0), 16.0},
           item.name, style(FontRole::UiSemibold, t.type.smallLabel, 0.0, TextAlign::Left, false),
           item.selected ? t.color.accent : t.color.textPrimary);
    c.text({bounds->x + 10.0, bounds->y + 21.0, std::max(1.0, bounds->width - 20.0), 14.0},
           item.detail, style(FontRole::Ui, t.type.rulerMicro), t.color.textSecondary);
    c.text({bounds->x + 10.0, bounds->y + 36.0, std::max(1.0, bounds->width - 20.0), 12.0},
           std::to_string(item.bytes) + tr(Str::B) +
               (selectable ? std::string{} : item.included ? tr(Str::Included) : tr(Str::Excluded)),
           style(FontRole::Mono, t.type.rulerMicro), t.color.textDisabled);
  }
  c.restore();
}

core::Result<void> RecoverySupportOverlay::perform(NativeEditorController& controller,
                                                   std::string_view id, SemanticAction action) const {
  if (action != SemanticAction::Activate && action != SemanticAction::Toggle)
    return core::failure(core::ErrorCode::Unsupported, tr(Str::ThisControlOnlyActivates));
  constexpr std::string_view kItem{"support.item."};
  if (id.starts_with(kItem)) {
    std::size_t index = 0U;
    if (!parseIndex(id.substr(kItem.size()), index))
      return core::failure(core::ErrorCode::InvalidArgument, tr(Str::InvalidSupportItem));
    return controller.selectSupportReport(index);
  }
  return activateControllerNode(controller, id);
}

bool RecoverySupportOverlay::key(NativeEditorController& controller, std::string_view focusedId,
                                 const KeyEvent& event) const {
  if (event.key == NativeKey::Enter || event.key == NativeKey::Space) {
    static_cast<void>(perform(controller, focusedId, SemanticAction::Activate));
    return true;
  }
  if (event.key == NativeKey::Left || event.key == NativeKey::Right) {
    // The panel's own track navigation, as the classic surface bound Left and Right.
    static_cast<void>(controller.dispatchAccessibility(
        event.key == NativeKey::Left ? "support.track.previous" : "support.track.next",
        SemanticAction::Activate));
    return true;
  }
  return false;
}

// ---- Overlap detail ----------------------------------------------------------------------------

class OverlapDetailOverlay final : public ShellOverlay {
public:
  [[nodiscard]] OverlayKind kind() const noexcept override { return OverlayKind::OverlapDetail; }
  [[nodiscard]] std::string_view idPrefix() const noexcept override {
    return "shell.overlay.overlap.";
  }
  [[nodiscard]] bool wanted(const NativeEditorController&, const EditorSceneState& state) const
      noexcept override {
    return state.overlapDetail.has_value();
  }
  // The +N badge the shell paints for a note with an overlap indicator, in shell coordinates. The
  // popover anchors to the badge of the group's own member, so it opens where the creator clicked.
  [[nodiscard]] ui::Rect badge(const NativeEditorController& controller,
                               const EditorSceneState& state,
                               const SingLayout& layout) const override {
    if (!state.overlapDetail.has_value()) return {};
    for (const auto& note : controller.pianoRoll().visibleNotes()) {
      if (!note.drawsOverlapIndicator || note.overlapGroup != state.overlapDetail->groupIndex)
        continue;
      const ui::Rect painted{note.bounds.x, note.bounds.y + layout.grid.y, note.bounds.width,
                             note.bounds.height};
      return {std::min(painted.right() + 3.0, layout.grid.right() - 30.0), painted.y - 2.0, 28.0,
              18.0};
    }
    return {};
  }
  [[nodiscard]] ui::Rect panel(const NativeEditorController& controller, const EditorSceneState& state,
                               const SingLayout& layout, ui::Rect slot) const override {
    if (!state.overlapDetail.has_value() || slot.width <= 0.0 || slot.height <= 0.0) return {};
    // The popover is as tall as its rows; a group taller than the body is capped, so no row is
    // ever drawn off screen.
    const auto width = std::min(slot.width, std::min(std::max(260.0, slot.width * 0.5), 360.0));
    const auto height = std::min(
        slot.height, 26.0 + static_cast<double>(state.overlapDetail->members.size()) * 26.0 + 8.0);
    if (width < kMinimumPanelWidth || height < 44.0) return {};
    auto anchor = badge(controller, state, layout);
    if (anchor.width <= 0.0) anchor = ui::Rect{slot.x, slot.y, 0.0, 0.0};
    const auto above = anchor.y - height - 6.0;
    const auto top = above >= slot.y ? above : std::min(slot.bottom() - height, anchor.bottom() + 6.0);
    return {std::clamp(anchor.x, slot.x, std::max(slot.x, slot.right() - width)),
            std::clamp(top, slot.y, std::max(slot.y, slot.bottom() - height)), width, height};
  }
  [[nodiscard]] std::string title(const NativeEditorController&,
                                  const EditorSceneState& state) const override {
    const auto count = state.overlapDetail.has_value() ? state.overlapDetail->members.size() : 0U;
    return std::to_string(count) + tr(Str::OverlappingNotes2);
  }
  [[nodiscard]] std::vector<OverlayControl> controls(const NativeEditorController&,
                                                     const EditorSceneState& state,
                                                     const SingLayout&, ui::Rect panel) const override {
    std::vector<OverlayControl> out;
    if (!state.overlapDetail.has_value()) return out;
    for (std::size_t i = 0U; i < state.overlapDetail->members.size(); ++i) {
      const auto& member = state.overlapDetail->members[i];
      const auto lyric = member.lyric.empty() ? std::string{tr(Str::NoLyric)} : member.lyric;
      const ui::Rect row{panel.x + 12.0, panel.y + 26.0 + static_cast<double>(i) * 26.0,
                         std::max(1.0, panel.width - 24.0), 24.0};
      // A row that would end below the card is not laid out at all: its node and hit rectangle
      // would otherwise lie outside the popover the creator sees.
      if (row.bottom() > panel.bottom() - 4.0) break;
      out.push_back({"overlap-note-row." + std::to_string(i),
                     row,
                     tr(Str::OverlapNote) + std::to_string(i + 1U) + ": " + lyric + tr(Str::MIDI) +
                         std::to_string(member.midiKey),
                     SemanticRole::Button, true, member.selected});
    }
    return out;
  }
  [[nodiscard]] std::string openerId(const NativeEditorController&,
                                     const EditorSceneState& state) const override {
    // The badge the popover was opened from, so Escape returns focus to it.
    return state.overlapDetail.has_value()
               ? "shell.note.overlap." + std::to_string(state.overlapDetail->groupIndex)
               : std::string{};
  }
  void paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController&,
             const EditorSceneState& state, const SingLayout&, ui::Rect panel,
             const std::vector<OverlayControl>& controls) const override;
  core::Result<void> perform(NativeEditorController& controller, std::string_view id,
                             SemanticAction action) const override;
  bool key(NativeEditorController& controller, std::string_view focusedId,
           const KeyEvent& event) const override;
  core::Result<void> close(NativeEditorController& controller) const override {
    // The detail popover closes through the controller, the same state the classic painter read.
    return controller.closeOverlapDetail();
  }
};

void OverlapDetailOverlay::paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController&,
                                 const EditorSceneState& state, const SingLayout&, ui::Rect panel,
                                 const std::vector<OverlayControl>& controls) const {
  if (!state.overlapDetail.has_value()) return;
  c.save();
  c.clipRect(panel);
  const auto& detail = *state.overlapDetail;
  for (std::size_t i = 0U; i < detail.members.size(); ++i) {
    const auto* bounds = findControl(controls, "overlap-note-row." + std::to_string(i));
    if (bounds == nullptr) continue;
    const auto& member = detail.members[i];
    if (member.selected) c.fill(Path::roundedRect(*bounds, 4.0), withAlpha(t.color.accent, 0.18));
    const auto lyric = member.lyric.empty() ? std::string{tr(Str::NoLyric)} : member.lyric;
    c.text({bounds->x + 6.0, bounds->y, std::max(1.0, bounds->width - 66.0), bounds->height}, lyric,
           style(FontRole::Ui, t.type.smallLabel),
           member.selected ? t.color.accent : t.color.textPrimary);
    c.text({bounds->right() - 60.0, bounds->y, 54.0, bounds->height},
           tr(Str::MIDI2) + std::to_string(member.midiKey),
           style(FontRole::Mono, t.type.rulerMicro, 0.0, TextAlign::Right),
           t.color.textSecondary);
  }
  c.restore();
}

core::Result<void> OverlapDetailOverlay::perform(NativeEditorController& controller,
                                                 std::string_view id, SemanticAction action) const {
  if (action != SemanticAction::Activate && action != SemanticAction::Toggle)
    return core::failure(core::ErrorCode::Unsupported, tr(Str::ThisControlOnlyActivates));
  constexpr std::string_view kRow{"overlap-note-row."};
  if (!id.starts_with(kRow)) return core::failure(core::ErrorCode::NotFound, tr(Str::UnknownControl));
  std::size_t index = 0U;
  if (!parseIndex(id.substr(kRow.size()), index))
    return core::failure(core::ErrorCode::InvalidArgument, tr(Str::InvalidOverlapRow));
  return controller.selectOverlapMemberRow(index);
}

bool OverlapDetailOverlay::key(NativeEditorController& controller, std::string_view focusedId,
                              const KeyEvent& event) const {
  if (event.key == NativeKey::Enter || event.key == NativeKey::Space) {
    static_cast<void>(perform(controller, focusedId, SemanticAction::Activate));
    return true;
  }
  return false;
}

// ---- Diagnostics popover -----------------------------------------------------------------------

// Every active diagnostic is a block: its title and impact on one row (a status node), and below
// it the recovery actions the controller registered for that diagnostic, so an action of the second
// or tenth issue is as reachable as the first one's. A popover too short for every block pages
// through them; the page is the popover's own presentation and restarts whenever it opens.
constexpr double kDiagnosticRow = 30.0;
constexpr double kDiagnosticActions = 26.0;
constexpr double kDiagnosticBlock = kDiagnosticRow + 4.0 + kDiagnosticActions + 8.0;
constexpr double kDiagnosticsTop = 48.0;
constexpr double kDiagnosticsPager = 34.0;

class DiagnosticsOverlay final : public ShellOverlay {
public:
  [[nodiscard]] OverlayKind kind() const noexcept override { return OverlayKind::Diagnostics; }
  [[nodiscard]] std::string_view idPrefix() const noexcept override {
    return "shell.overlay.diagnostics.";
  }
  [[nodiscard]] bool wanted(const NativeEditorController&, const EditorSceneState& state) const
      noexcept override {
    return !state.diagnostics.empty();
  }
  [[nodiscard]] ui::Rect panel(const NativeEditorController&, const EditorSceneState& state,
                               const SingLayout& layout, ui::Rect slot) const override {
    if (slot.width <= 0.0 || slot.height <= 0.0) return {};
    // A popover above the status bar, its right edge under the header's settings control, as tall
    // as its blocks (up to the body).
    const auto natural = kDiagnosticsTop +
                         static_cast<double>(state.diagnostics.size()) * kDiagnosticBlock + 4.0;
    const auto width = std::min(slot.width, 460.0);
    const auto height = std::min(slot.height, std::min(natural, 560.0));
    if (width < kMinimumPanelWidth || height < kMinimumPanelHeight) return {};
    const auto right = std::max(slot.x, std::min(layout.settings.right(), slot.right()));
    return {std::max(slot.x, right - width), std::max(slot.y, slot.bottom() - height - 2.0), width,
            height};
  }
  [[nodiscard]] std::string title(const NativeEditorController&,
                                  const EditorSceneState& state) const override {
    return state.diagnostics.size() == 1U
               ? std::string{tr(Str::Diagnostics)}
               : tr(Str::Diagnostics2) + std::to_string(state.diagnostics.size()) + tr(Str::Issues);
  }
  [[nodiscard]] std::vector<OverlayControl> controls(const NativeEditorController&,
                                                     const EditorSceneState& state,
                                                     const SingLayout&, ui::Rect panel) const override;
  [[nodiscard]] std::string openerId(const NativeEditorController&,
                                     const EditorSceneState&) const override {
    // The DIAGNOSTICS opener beside the toast, so Escape returns focus to the control that opened
    // the popover rather than to the read-only status bar.
    return "shell.diagnostics.open";
  }
  void presented() const override { first_ = 0U; }
  void paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController&,
             const EditorSceneState& state, const SingLayout&, ui::Rect panel,
             const std::vector<OverlayControl>& controls) const override;
  core::Result<void> perform(NativeEditorController& controller, std::string_view id,
                             SemanticAction action) const override;
  bool key(NativeEditorController& controller, std::string_view focusedId,
           const KeyEvent& event) const override;
  core::Result<void> close(NativeEditorController& controller) const override {
    // The popover is the status bar's own diagnostic summary expanded; nothing to close in the
    // controller, so the shell drops its own presentation of the popover.
    static_cast<void>(controller);
    return core::success();
  }

private:
  // How many blocks one page holds, and whether the popover needs its pager at all.
  [[nodiscard]] static bool paged(const EditorSceneState& state, ui::Rect panel) {
    return kDiagnosticsTop + static_cast<double>(state.diagnostics.size()) * kDiagnosticBlock +
               4.0 >
           panel.height + 0.5;
  }
  [[nodiscard]] static std::size_t perPage(const EditorSceneState& state, ui::Rect panel) {
    const auto pager = paged(state, panel) ? kDiagnosticsPager : 0.0;
    const auto room = panel.height - kDiagnosticsTop - pager - 4.0;
    return std::max<std::size_t>(1U, static_cast<std::size_t>(std::max(0.0, room) / kDiagnosticBlock));
  }
  [[nodiscard]] std::size_t first(const EditorSceneState& state, ui::Rect panel) const {
    const auto page = perPage(state, panel);
    const auto count = state.diagnostics.size();
    const auto last = count == 0U ? 0U : ((count - 1U) / page) * page;
    return std::min((first_ / page) * page, last);
  }
  // The page a pager button moved to. The popover holds no document state, only where it is.
  mutable std::size_t first_{0U};
  // How many blocks the last layout put on a page, so a pager press moves by one page.
  mutable std::size_t pageHint_{1U};
};

std::vector<OverlayControl> DiagnosticsOverlay::controls(const NativeEditorController&,
                                                         const EditorSceneState& state,
                                                         const SingLayout&, ui::Rect panel) const {
  std::vector<OverlayControl> out;
  if (panel.width <= 0.0 || state.diagnostics.empty()) return out;
  const auto page = perPage(state, panel);
  pageHint_ = page;
  const auto start = first(state, panel);
  const auto width = std::max(1.0, panel.width - 2.0 * kPanelInset);
  for (std::size_t i = start; i < state.diagnostics.size() && i < start + page; ++i) {
    const auto top = panel.y + kDiagnosticsTop + static_cast<double>(i - start) * kDiagnosticBlock;
    const auto& diagnostic = state.diagnostics[i];
    const auto presentation = presentDiagnostic(diagnostic);
    // The row keeps the controller's own diagnostic id, so its impact and technical detail are
    // published with it.
    out.push_back({"diagnostic." + std::to_string(i) + "." + diagnostic.code,
                   {panel.x + kPanelInset, top, width, kDiagnosticRow},
                   presentation.title, SemanticRole::Status, true, false, false});
    const auto count = presentation.primaryActionKinds.size();
    if (count == 0U) continue;
    const auto cells = grid(panel, top + kDiagnosticRow + 4.0, kDiagnosticActions, count, count, 8.0);
    for (std::size_t a = 0U; a < cells.size(); ++a)
      out.push_back({"diagnostic-action." + std::to_string(i) + "." +
                         std::string{authoring::toString(presentation.primaryActionKinds[a])},
                     cells[a], diagnosticActionLabel(presentation.primaryActionKinds[a])});
  }
  if (paged(state, panel)) {
    const auto top = panel.bottom() - kDiagnosticsPager + 2.0;
    out.push_back({"shell.overlay.diagnostics.previous",
                   {panel.x + kPanelInset, top, 88.0, 26.0}, tr(Str::PreviousIssues),
                   SemanticRole::Button, start > 0U});
    out.push_back({"shell.overlay.diagnostics.next",
                   {panel.x + kPanelInset + 96.0, top, 88.0, 26.0}, tr(Str::NextIssues),
                   SemanticRole::Button, start + page < state.diagnostics.size()});
  }
  return out;
}

void DiagnosticsOverlay::paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController&,
                               const EditorSceneState& state, const SingLayout&, ui::Rect panel,
                               const std::vector<OverlayControl>& controls) const {
  if (state.diagnostics.empty()) return;
  c.save();
  c.clipRect(panel);
  for (const auto& control : controls) {
    if (control.role == SemanticRole::Status) {
      // A block's row: the severity bar, the title and what it affects.
      const auto dot = control.id.find('.', std::string_view{"diagnostic."}.size());
      std::size_t index = 0U;
      if (dot == std::string::npos ||
          !parseIndex(std::string_view{control.id}.substr(11U, dot - 11U), index) ||
          index >= state.diagnostics.size())
        continue;
      const auto& diagnostic = state.diagnostics[index];
      const auto presentation = presentDiagnostic(diagnostic);
      const auto color = diagnostic.severity == authoring::DiagnosticSeverity::Critical
                             ? t.color.error
                             : diagnostic.severity == authoring::DiagnosticSeverity::Warning
                                   ? t.color.warning
                                   : t.color.info;
      const auto& r = control.bounds;
      c.fill(Path::capsule({r.x, r.y + 3.0, 4.0, r.height - 6.0}), color);
      c.text({r.x + 12.0, r.y, std::max(1.0, r.width - 12.0), 16.0}, presentation.title,
             style(FontRole::UiSemibold, t.type.label), t.color.textPrimary);
      c.text({r.x + 12.0, r.y + 16.0, std::max(1.0, r.width - 12.0), 14.0}, presentation.impact,
             style(FontRole::Ui, t.type.smallLabel), t.color.textSecondary);
      continue;
    }
    paintOverlayControl(c, t, control.bounds, control.name, SemanticRole::Button, control.enabled,
                        false, false);
  }
  if (paged(state, panel)) {
    const auto page = perPage(state, panel);
    const auto start = first(state, panel);
    const auto end = std::min(start + page, state.diagnostics.size());
    const auto top = panel.bottom() - kDiagnosticsPager + 2.0;
    c.text({panel.x + kPanelInset + 196.0, top, std::max(1.0, panel.width - 2.0 * kPanelInset - 196.0),
            26.0},
           std::to_string(start + 1U) + tr(Str::Text6) + std::to_string(end) + tr(Str::Of) +
               std::to_string(state.diagnostics.size()),
           style(FontRole::Ui, t.type.smallLabel), t.color.textSecondary);
  }
  c.restore();
}

core::Result<void> DiagnosticsOverlay::perform(NativeEditorController& controller,
                                               std::string_view id, SemanticAction action) const {
  if (action != SemanticAction::Activate && action != SemanticAction::Toggle)
    return core::failure(core::ErrorCode::Unsupported, tr(Str::ThisControlOnlyActivates));
  if (id == "shell.overlay.diagnostics.previous" || id == "shell.overlay.diagnostics.next") {
    // The pager moves by what the popover showed last; clamping happens when it lays out again.
    const auto state = controller.sceneState();
    const auto step = std::max<std::size_t>(1U, pageHint_);
    if (id.ends_with("previous")) first_ = first_ >= step ? first_ - step : 0U;
    else if (first_ + step < state.diagnostics.size()) first_ += step;
    return core::success();
  }
  return activateControllerNode(controller, id);
}

bool DiagnosticsOverlay::key(NativeEditorController& controller, std::string_view focusedId,
                             const KeyEvent& event) const {
  if (event.key == NativeKey::Enter || event.key == NativeKey::Space) {
    static_cast<void>(perform(controller, focusedId, SemanticAction::Activate));
    return true;
  }
  return false;
}

// ---- Replacement review ------------------------------------------------------------------------

// The review panel the controller shows for every review it runs (lyric replacement and
// distribution, find, note cleanup, clear vibrato/dynamics, the vibrato, dynamics and style
// inspectors, Japanese reading), presented as a sheet dropping from the SINGER card's side of the
// body. Its rows, actions, navigation and plot handles keep the controller's own ids (which carry
// the review's interaction prefix), so every command is the controller's accessibility command and
// a stale id from an earlier review is refused there as before.
struct ReviewGeometry final {
  ui::Rect status;
  std::vector<ui::Rect> rows;       // the laid-out rows, from firstRow on
  std::size_t firstRow{0U};
  bool rowPager{false};
  ui::Rect rowsUp, rowsDown;
  std::array<ui::Rect, 4U> navigation{};
  ui::Rect plot;                    // the dynamics plot, empty when the card cannot hold one
  std::array<ui::Rect, 6U> actions{};
};

class ReplacementReviewOverlay final : public ShellOverlay {
public:
  [[nodiscard]] OverlayKind kind() const noexcept override { return OverlayKind::ReplacementReview; }
  [[nodiscard]] std::string_view idPrefix() const noexcept override { return "shell.overlay.review."; }
  [[nodiscard]] bool wanted(const NativeEditorController&, const EditorSceneState& state) const
      noexcept override {
    return state.replacementReview.visible;
  }
  [[nodiscard]] ui::Rect panel(const NativeEditorController&, const EditorSceneState& state,
                               const SingLayout&, ui::Rect slot) const override {
    if (slot.width <= 0.0 || slot.height <= 0.0) return {};
    // A sheet from the SINGER card: right-aligned in the body under the header, as tall as the body
    // for the docked inspectors and sized to its content for the reviews.
    const auto docked = state.replacementReview.dockedInspector;
    const auto width = std::min(slot.width, docked ? 400.0 : 560.0);
    const auto height = docked ? slot.height : std::min(slot.height, 440.0);
    if (width < kMinimumPanelWidth || height < kMinimumPanelHeight) return {};
    return {slot.right() - width, slot.y, width, height};
  }
  [[nodiscard]] std::string title(const NativeEditorController& controller,
                                  const EditorSceneState&) const override {
    // The controller names each review on its own panel node.
    const auto& root = controller.accessibilityTree().root();
    if (root.id == controller.replacementReviewSemanticPrefix() + "panel" && !root.name.empty())
      return root.name;
    return tr(Str::Review);
  }
  [[nodiscard]] std::vector<OverlayControl> controls(const NativeEditorController& controller,
                                                     const EditorSceneState& state,
                                                     const SingLayout&, ui::Rect panel) const override;
  void presented() const override {
    firstRow_ = 0U;
    rowsKey_.clear();
  }
  void paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController& controller,
             const EditorSceneState& state, const SingLayout&, ui::Rect panel,
             const std::vector<OverlayControl>& controls) const override;
  core::Result<void> perform(NativeEditorController& controller, std::string_view id,
                             SemanticAction action) const override;
  bool key(NativeEditorController& controller, std::string_view focusedId,
           const KeyEvent& event) const override;
  core::Result<void> close(NativeEditorController& controller) const override {
    // The classic panel's own Escape: from a detail page it returns to the list, otherwise it
    // cancels the review (replacementReviewAction 3 or 4, as the controller decides).
    return controller.keyDown(KeyEvent{.key = NativeKey::Escape});
  }
  OverlayPress press(NativeEditorController& controller, const EditorSceneState& state,
                     const SingLayout& layout, ui::Rect panel,
                     const PointerEvent& event) const override;
  core::Result<void> drag(NativeEditorController& controller, const OverlayGesture& gesture,
                          const PointerEvent& event, bool release) const override {
    auto legacy = event;
    legacy.position = AxisMap{gesture.source, gesture.destination}.unmap(event.position);
    return release ? controller.pointerUp(legacy) : controller.pointerMove(legacy);
  }
  bool scroll(NativeEditorController& controller, const EditorSceneState& state, const SingLayout&,
              ui::Rect panel, ui::Point anchor, double deltaX, double deltaY,
              InputModifiers modifiers) const override {
    const auto& view = state.replacementReview;
    followRows(controller, view);
    const auto g = geometry(view, panel);
    if (view.dynamicsPlot && contains(g.plot, anchor)) {
      // The plot pans and zooms through the controller's own wheel handling, at the pointer.
      controller.scroll(deltaX, deltaY,
                        AxisMap{view.dynamicsPlot->bounds, g.plot}.unmap(anchor), modifiers);
      return true;
    }
    return false;
  }

private:
  [[nodiscard]] static bool contains(ui::Rect r, ui::Point p) noexcept {
    return r.width > 0.0 && r.height > 0.0 && p.x >= r.x && p.x < r.right() && p.y >= r.y &&
           p.y < r.bottom();
  }
  [[nodiscard]] ReviewGeometry geometry(const ReplacementReviewView& view, ui::Rect panel) const;
  // The card's row pager starts again at the top of each page of rows the controller shows (its
  // page, its list or a detail). The review prefix without its counters names that page: the
  // interaction id the controller renews on every action and on a draft field, and the selected
  // field, change without changing the rows.
  void followRows(const NativeEditorController& controller, const ReplacementReviewView& view) const {
    std::string key;
    for (const auto ch : controller.replacementReviewSemanticPrefix())
      if (ch < '0' || ch > '9') key.push_back(ch);
    key += "#" + std::to_string(view.page);
    if (key == rowsKey_) return;
    firstRow_ = 0U;
    rowsKey_ = std::move(key);
  }
  // The rows a short card shows start here; the controller pages its rows by six, and this pages
  // within the controller's page when fewer fit.
  mutable std::size_t firstRow_{0U};
  mutable std::string rowsKey_;
  // The last first row the layout can show, as last laid out; the pager never counts past it.
  mutable std::size_t lastFirstRow_{0U};
};

ReviewGeometry ReplacementReviewOverlay::geometry(const ReplacementReviewView& view,
                                                ui::Rect panel) const {
  ReviewGeometry g;
  if (panel.width <= 0.0) return g;
  const auto inner = std::max(1.0, panel.width - 2.0 * kPanelInset);
  const auto compact = panel.height < 300.0;
  // Actions along the bottom: two rows of three, or one row of six on a short card.
  const auto actionHeight = compact ? 24.0 : 26.0;
  const auto actionsTop = panel.bottom() - 10.0 - (compact ? actionHeight : 2.0 * actionHeight + 6.0);
  const auto cells = grid(panel, actionsTop, actionHeight, 6U, compact ? 6U : 3U, 6.0);
  for (std::size_t i = 0U; i < cells.size() && i < g.actions.size(); ++i) g.actions[i] = cells[i];
  // The status line also carries the row pager's buttons, so even compact it keeps them at the
  // 24-point minimum, as it does the rows.
  g.status = {panel.x + kPanelInset, panel.y + 42.0, inner, compact ? 24.0 : 34.0};
  const auto top = g.status.bottom() + 6.0;
  const auto bottom = actionsTop - 8.0;
  const auto rowHeight = 24.0;
  const auto rowStride = rowHeight + 2.0;
  auto rowsBottom = bottom;
  if (view.dynamicsPlot) {
    // Two point rows, the navigation row and the plot below them.
    const auto rowsSpace = static_cast<double>(view.rows.size()) * rowStride;
    const auto navTop = top + rowsSpace + 2.0;
    const auto navCells = grid(panel, navTop, 24.0, 4U, 4U, 6.0);
    if (navTop + 24.0 <= bottom) {
      for (std::size_t i = 0U; i < 4U; ++i) g.navigation[i] = navCells[i];
      // Two caption lines above the curve (what is drawn, and the visible range).
      const auto plotTop = navTop + 24.0 + 26.0;
      if (bottom - plotTop >= 40.0)
        g.plot = {panel.x + kPanelInset, plotTop, inner, bottom - plotTop};
      rowsBottom = navTop - 2.0;
    }
  }
  const auto capacity = static_cast<std::size_t>(std::max(0.0, rowsBottom - top + 2.0) / rowStride);
  g.rowPager = view.rows.size() > capacity && capacity > 0U;
  if (g.rowPager) {
    // The status line gives room at its right end for the row pager.
    g.status.width = std::max(1.0, inner - 64.0);
    g.rowsUp = {g.status.right() + 8.0, g.status.y, 26.0, g.status.height};
    g.rowsDown = {g.rowsUp.right() + 4.0, g.status.y, 26.0, g.status.height};
    g.firstRow = std::min(firstRow_, view.rows.size() - capacity);
  }
  // The counter is what the card shows, so a pager press always moves the visible rows.
  firstRow_ = g.firstRow;
  lastFirstRow_ = g.rowPager ? view.rows.size() - capacity : 0U;
  for (std::size_t i = g.firstRow; i < view.rows.size() && i < g.firstRow + capacity; ++i)
    g.rows.push_back({panel.x + kPanelInset, top + static_cast<double>(i - g.firstRow) * rowStride,
                      inner, rowHeight});
  return g;
}

std::vector<OverlayControl> ReplacementReviewOverlay::controls(
    const NativeEditorController& controller, const EditorSceneState& state, const SingLayout&,
    ui::Rect panel) const {
  std::vector<OverlayControl> out;
  const auto& view = state.replacementReview;
  if (panel.width <= 0.0 || !view.visible) return out;
  const auto prefix = controller.replacementReviewSemanticPrefix();
  followRows(controller, view);
  const auto g = geometry(view, panel);
  OverlayControl status{prefix + "status", g.status, tr(Str::ReviewStatusAndCounts),
                        SemanticRole::Status, true, false, false};
  status.value = view.status + " / " + view.summary;
  out.push_back(std::move(status));
  if (g.rowPager) {
    const auto capacity = g.rows.size();
    out.push_back({"shell.overlay.review.rows-up", g.rowsUp, tr(Str::EarlierRows), SemanticRole::Button,
                   g.firstRow > 0U});
    out.push_back({"shell.overlay.review.rows-down", g.rowsDown, tr(Str::LaterRows), SemanticRole::Button,
                   g.firstRow + capacity < view.rows.size()});
  }
  for (std::size_t k = 0U; k < g.rows.size(); ++k) {
    const auto i = g.firstRow + k;
    OverlayControl row{prefix + "row." + std::to_string(i), g.rows[k],
                       tr(Str::ReviewRow) + std::to_string(i + 1U),
                       view.rowsInspectable ? SemanticRole::Button : SemanticRole::Status, true,
                       false, view.rowsInspectable};
    row.value = view.rows[i];
    out.push_back(std::move(row));
  }
  if (view.dynamicsPlot) {
    const auto& plot = *view.dynamicsPlot;
    static constexpr std::array<Str, 4U> kNames{
        Str::ZoomInDynamics, Str::ZoomOutDynamics, Str::FitRegionDynamics,
        Str::MeasuredOutputNextChannelThenReturn};
    for (std::size_t i = 0U; i < 4U; ++i) {
      if (g.navigation[i].width <= 0.0) continue;
      out.push_back({prefix + "zoom." + std::to_string(i), g.navigation[i], tr(kNames[i]),
                     SemanticRole::Button, plot.editable && (i != 3U || plot.measurementAvailable)});
    }
    if (g.plot.width > 0.0) {
      // The handles first, so a press on one reaches it rather than the curve under it.
      const AxisMap map{plot.bounds, g.plot};
      for (const auto& handle : plot.handles) {
        if (handle.pageRow >= view.rows.size()) continue;
        const auto at = map.map(handle.position);
        const ui::Rect r{std::clamp(at.x - 6.0, g.plot.x, g.plot.right() - 12.0),
                         std::clamp(at.y - 6.0, g.plot.y, g.plot.bottom() - 12.0), 12.0, 12.0};
        OverlayControl point{prefix + "point." + std::to_string(handle.pageRow), r,
                             tr(Str::DynamicsPoint) + std::to_string(handle.pageRow + 1U),
                             SemanticRole::Button, plot.editable};
        point.value = view.rows[handle.pageRow];
        out.push_back(std::move(point));
      }
      out.push_back({prefix + "curve", g.plot, tr(Str::RegionDynamics), SemanticRole::Status, true, false,
                     false});
    }
  }
  for (std::size_t i = 0U; i < view.enabled.size(); ++i)
    out.push_back({prefix + "action." + std::to_string(i), g.actions[i],
                   view.labels[i] == nullptr ? std::string{} : std::string{view.labels[i]},
                   SemanticRole::Button, view.enabled[i]});
  return out;
}

void ReplacementReviewOverlay::paint(Canvas2D& c, const DesignTokens& t,
                                     const NativeEditorController&, const EditorSceneState& state,
                                     const SingLayout&, ui::Rect panel,
                                     const std::vector<OverlayControl>& controls) const {
  const auto& view = state.replacementReview;
  const auto g = geometry(view, panel);
  c.save();
  c.clipRect(panel);
  // Status and summary (one line on a short card), elided; the status node carries both whole.
  if (g.status.height >= 30.0) {
    c.text({g.status.x, g.status.y, g.status.width, 16.0}, view.status,
           style(FontRole::UiSemibold, t.type.label), t.color.textPrimary);
    c.text({g.status.x, g.status.y + 18.0, g.status.width, 16.0}, view.summary,
           style(FontRole::Ui, t.type.smallLabel), t.color.textSecondary);
  } else {
    c.text(g.status, view.status + " / " + view.summary, style(FontRole::Ui, t.type.smallLabel),
           t.color.textPrimary);
  }
  for (const auto& control : controls) {
    const auto& r = control.bounds;
    if (control.id.ends_with(".status") || control.id.ends_with(".curve")) continue;
    if (control.id.find(".row.") != std::string::npos) {
      std::string_view text{control.value};
      while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.remove_suffix(1U);
      if (view.rowsInspectable) {
        c.fill(Path::roundedRect(r, 4.0), withAlpha(t.color.surfaceSunken, 0.85));
        c.stroke(Path::roundedRect(r, 4.0), withAlpha(t.color.border, 0.9), StrokeStyle{1.0});
      }
      c.text({r.x + 6.0, r.y, std::max(1.0, r.width - 12.0), r.height}, text,
             style(FontRole::Mono, t.type.smallLabel), t.color.textPrimary);
      continue;
    }
    if (control.id.find(".point.") != std::string::npos) continue;
    std::string label = control.name;
    if (control.id.ends_with("rows-up")) label = tr(Str::Text7);
    if (control.id.ends_with("rows-down")) label = tr(Str::Text8);
    if (control.id.find(".zoom.") != std::string::npos) {
      static constexpr std::array<Str, 4U> kShort{Str::Zoom, Str::Zoom2, Str::Fit, Str::Measure};
      const auto index = static_cast<std::size_t>(control.id.back() - '0');
      label = index < 4U ? tr(kShort[index]) : label;
      if (index == 3U && view.dynamicsPlot && view.dynamicsPlot->measuredMode)
        label = tr(Str::Ch) + std::to_string(view.dynamicsPlot->measuredChannel);
    }
    paintOverlayControl(c, t, r, label, SemanticRole::Button, control.enabled, false, false);
  }
  if (view.dynamicsPlot && g.plot.width > 0.0) {
    const auto& plot = *view.dynamicsPlot;
    const AxisMap map{plot.bounds, g.plot};
    const auto caption = style(FontRole::Ui, t.type.rulerMicro);
    c.text({g.plot.x, g.plot.y - 24.0, g.plot.width, 11.0},
           plot.measuredMode ? plot.measurementLabel
                             : std::string{tr(Str::ScoreGrayDraftPinkTargetCyan)},
           caption, t.color.textSecondary);
    c.text({g.plot.x, g.plot.y - 12.0, g.plot.width, 11.0},
           tr(Str::Ticks) + std::to_string(plot.startTick) + ".." + std::to_string(plot.endTick) +
               (plot.measuredMode ? tr(Str::DBFS96) +
                                        std::to_string(static_cast<int>(plot.measuredCeilingDb))
                                  : std::string{tr(Str::Gain03981)}),
           caption, t.color.textSecondary);
    sunken(c, t, g.plot, 6.0);
    c.save();
    c.clipRect(g.plot);
    const auto polyline = [&](const std::vector<ui::Point>& points, Color color, double width) {
      if (points.size() < 2U) return;
      Path path;
      path.moveTo(map.map(points.front()));
      for (std::size_t i = 1U; i < points.size(); ++i) path.lineTo(map.map(points[i]));
      c.stroke(path, color, StrokeStyle{width});
    };
    if (plot.measuredMode) {
      for (const auto& point : plot.measured) {
        const auto at = map.map(point);
        c.fill(Path::rect({at.x, at.y, 1.5, 1.5}), t.color.success);
      }
    } else {
      const auto unity = plot.bounds.y + plot.bounds.height * (1.0 - 1.0 / domain::kMaximumDynamicsGain);
      Path line;
      line.moveTo(map.map({plot.bounds.x, unity})).lineTo(map.map({plot.bounds.right(), unity}));
      c.stroke(line, withAlpha(t.color.border, 0.9), StrokeStyle{1.0, true, {4.0, 3.0}});
      polyline(plot.score, t.color.textSecondary, 3.0);
      polyline(plot.draft, t.color.accent, 1.2);
      for (const auto& point : plot.target) {
        const auto at = map.map(point);
        c.fill(Path::rect({at.x, at.y, 1.5, 1.5}), t.color.accentCurve);
      }
      for (const auto& point : plot.selectedGenerated) {
        const auto at = map.map(point);
        c.stroke(Path::rect({at.x - 1.5, at.y - 1.5, 3.5, 3.5}), t.color.warning, StrokeStyle{1.0});
      }
      for (const auto& control : controls) {
        if (control.id.find(".point.") == std::string::npos) continue;
        const auto& r = control.bounds;
        c.fill(Path::roundedRect({r.x + 3.0, r.y + 3.0, 6.0, 6.0}, 1.5),
               plot.editable ? t.color.accent : t.color.textSecondary);
      }
      if (plot.candidate) {
        const auto at = map.map(*plot.candidate);
        c.stroke(Path::rect({at.x - 4.0, at.y - 4.0, 8.0, 8.0}), t.color.accentTime, StrokeStyle{2.0});
      }
    }
    c.restore();
  }
  c.restore();
}

OverlayPress ReplacementReviewOverlay::press(NativeEditorController& controller,
                                             const EditorSceneState& state, const SingLayout&,
                                             ui::Rect panel, const PointerEvent& event) const {
  const auto& view = state.replacementReview;
  if (!view.dynamicsPlot || !view.dynamicsPlot->editable) return {};
  followRows(controller, view);
  const auto g = geometry(view, panel);
  if (g.plot.width <= 0.0) return {};
  const auto& plot = *view.dynamicsPlot;
  const AxisMap map{plot.bounds, g.plot};
  // A handle's own rectangle presses the handle itself, wherever in it the press landed; the rest
  // of the plot is mapped back to the classic plot, where the controller decides what it hit.
  auto legacy = event;
  auto inPlot = contains(g.plot, event.position);
  for (const auto& handle : plot.handles) {
    const auto at = map.map(handle.position);
    const ui::Rect r{std::clamp(at.x - 6.0, g.plot.x, g.plot.right() - 12.0),
                     std::clamp(at.y - 6.0, g.plot.y, g.plot.bottom() - 12.0), 12.0, 12.0};
    if (!contains(r, event.position)) continue;
    legacy.position = handle.position;
    inPlot = true;
    break;
  }
  if (!inPlot) return {};
  if (legacy.position.x == event.position.x && legacy.position.y == event.position.y)
    legacy.position = map.unmap(event.position);
  OverlayPress out{.handled = true, .result = controller.pointerDown(legacy)};
  if (out.result && controller.pointerGestureActive())
    out.gesture = OverlayGesture{plot.bounds, g.plot};
  return out;
}

core::Result<void> ReplacementReviewOverlay::perform(NativeEditorController& controller,
                                                     std::string_view id,
                                                     SemanticAction action) const {
  if (action != SemanticAction::Activate && action != SemanticAction::Toggle)
    return core::failure(core::ErrorCode::Unsupported, tr(Str::ThisControlOnlyActivates));
  if (id == "shell.overlay.review.rows-up" || id == "shell.overlay.review.rows-down") {
    if (id.ends_with("up")) firstRow_ = firstRow_ > 0U ? firstRow_ - 1U : 0U;
    else if (firstRow_ < lastFirstRow_) ++firstRow_;
    return core::success();
  }
  // Rows, actions, navigation and points: the controller's own accessibility command, which checks
  // the review's current prefix and whether that node can act now.
  return activateControllerNode(controller, id);
}

bool ReplacementReviewOverlay::key(NativeEditorController& controller, std::string_view focusedId,
                                   const KeyEvent& event) const {
  if (event.key == NativeKey::Enter || event.key == NativeKey::Space) {
    static_cast<void>(perform(controller, focusedId, SemanticAction::Activate));
    return true;
  }
  // The dynamics inspector's own keys (zoom, pan, fit), which the controller ignores for the
  // other reviews, exactly as the classic panel forwarded them.
  if (event.key == NativeKey::Plus || event.key == NativeKey::Minus || event.key == NativeKey::Left ||
      event.key == NativeKey::Right || event.key == NativeKey::R) {
    static_cast<void>(controller.keyDown(event));
    return true;
  }
  return false;
}

// ---- Audio settings ----------------------------------------------------------------------------

// The audio device settings (section 7.4 moves them into MIX as a card; the header's settings and
// the MIX device card's Settings open the same sheet). Every change is the controller's own
// command, which builds the request from the current settings and applies it through the host's
// applyAudioSettings, recording its refusal as the diagnostic shown here.
constexpr double kDeviceRow = 28.0;
constexpr double kDeviceStride = 32.0;

class AudioSettingsOverlay final : public ShellOverlay {
public:
  [[nodiscard]] OverlayKind kind() const noexcept override { return OverlayKind::AudioSettings; }
  [[nodiscard]] std::string_view idPrefix() const noexcept override { return "shell.overlay.audio."; }
  [[nodiscard]] bool wanted(const NativeEditorController&, const EditorSceneState& state) const
      noexcept override {
    return state.audioSettings.visible;
  }
  [[nodiscard]] ui::Rect panel(const NativeEditorController&, const EditorSceneState& state,
                               const SingLayout&, ui::Rect slot) const override {
    if (slot.width <= 0.0 || slot.height <= 0.0) return {};
    const auto natural = 44.0 + 20.0 +
                         static_cast<double>(state.audioSettings.devices.size()) * kDeviceStride +
                         8.0 + 48.0 + 10.0 + 38.0 + 12.0;
    return fitPanel(slot, std::min(slot.width, 520.0), std::min(slot.height, natural));
  }
  [[nodiscard]] std::string title(const NativeEditorController&,
                                  const EditorSceneState& state) const override {
    return state.audioSettings.reported ? tr(Str::AudioSettings) : tr(Str::AudioSettingsNotReportedYet);
  }
  [[nodiscard]] std::vector<OverlayControl> controls(const NativeEditorController&,
                                                     const EditorSceneState& state,
                                                     const SingLayout&, ui::Rect panel) const override;
  [[nodiscard]] std::string openerId(const NativeEditorController&,
                                     const EditorSceneState&) const override {
    return "shell.settings";
  }
  void presented() const override { firstDevice_ = 0U; }
  void paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController&,
             const EditorSceneState& state, const SingLayout&, ui::Rect panel,
             const std::vector<OverlayControl>& controls) const override;
  core::Result<void> perform(NativeEditorController& controller, std::string_view id,
                             SemanticAction action) const override;
  bool key(NativeEditorController& controller, std::string_view focusedId,
           const KeyEvent& event) const override;
  core::Result<void> close(NativeEditorController& controller) const override {
    controller.closeAudioSettings();
    return core::success();
  }
  bool scroll(NativeEditorController& controller, const EditorSceneState& state,
              const SingLayout& layout, ui::Rect panel, ui::Point, double, double deltaY,
              InputModifiers) const override {
    // The list's current layout bounds the scroll: it stops at the last device row that fills it.
    static_cast<void>(controls(controller, state, layout, panel));
    if (deltaY < 0.0 && firstDevice_ < lastFirstDevice_) ++firstDevice_;
    else if (deltaY > 0.0 && firstDevice_ > 0U) --firstDevice_;
    return true;
  }

private:
  mutable std::size_t firstDevice_{0U};
  // The last first device the list can show, as last laid out; paging never counts past it.
  mutable std::size_t lastFirstDevice_{0U};
};

std::vector<OverlayControl> AudioSettingsOverlay::controls(const NativeEditorController&,
                                                           const EditorSceneState& state,
                                                           const SingLayout&, ui::Rect panel) const {
  std::vector<OverlayControl> out;
  if (panel.width <= 0.0) return out;
  const auto& audio = state.audioSettings;
  const auto inner = std::max(1.0, panel.width - 2.0 * kPanelInset);
  const ui::Rect close{panel.right() - kPanelInset - 72.0, panel.y + 8.0, 72.0, 26.0};
  out.push_back({"shell.overlay.audio.close", close, tr(Str::CloseAudioSettings)});
  // From the bottom up: the measured counts and the last refusal, then the three stepped settings.
  // A short card (the minimum window) drops the list caption and tightens the rows, so at least
  // one device row and its pager always fit.
  const auto compact = panel.height < 200.0;
  const auto statsHeight = compact ? 20.0 : 38.0;
  const ui::Rect stats{panel.x + kPanelInset, panel.bottom() - 10.0 - statsHeight, inner, statsHeight};
  const auto fieldHeight = compact ? 36.0 : 48.0;
  const auto fieldsTop = stats.y - 8.0 - fieldHeight;
  const auto cells = grid(panel, fieldsTop, fieldHeight, 3U, 3U, 8.0);
  // The devices fill what is left under the header; a list longer than that pages.
  const auto rowHeight = compact ? 24.0 : kDeviceRow;
  const auto stride = compact ? 26.0 : kDeviceStride;
  const auto listTop = panel.y + (compact ? 42.0 : 64.0);
  const auto capacity = static_cast<std::size_t>(
      std::max(0.0, fieldsTop - 8.0 - listTop + (stride - rowHeight)) / stride);
  const auto paged = audio.devices.size() > capacity && capacity > 0U;
  const auto first = paged ? std::min(firstDevice_, audio.devices.size() - capacity) : 0U;
  // The counter is what the list shows, so a pager press or a scroll always moves the rows.
  firstDevice_ = first;
  lastFirstDevice_ = paged ? audio.devices.size() - capacity : 0U;
  if (paged) {
    // Beside the caption, or in the header left of Close when the caption is dropped.
    const auto pagerRight = compact ? close.x - 8.0 : panel.right() - kPanelInset;
    const auto pagerY = compact ? panel.y + 12.0 : panel.y + 42.0;
    out.push_back({"shell.overlay.audio.devices-up", {pagerRight - 58.0, pagerY, 26.0, 18.0},
                   tr(Str::EarlierDevices), SemanticRole::Button, first > 0U});
    out.push_back({"shell.overlay.audio.devices-down", {pagerRight - 26.0, pagerY, 26.0, 18.0},
                   tr(Str::LaterDevices), SemanticRole::Button, first + capacity < audio.devices.size()});
  }
  for (std::size_t i = first; i < audio.devices.size() && i < first + capacity; ++i) {
    const auto& device = audio.devices[i];
    OverlayControl row{"audio.device." + std::to_string(i),
                       {panel.x + kPanelInset, listTop + static_cast<double>(i - first) * stride,
                        inner, rowHeight},
                       device.name.empty() ? device.id : device.name, SemanticRole::Button, true,
                       device.selected};
    row.value = device.physical ? tr(Str::PhysicalDevice) : tr(Str::FallbackDevice);
    row.description = tr(Str::DeviceId) + device.id;
    out.push_back(std::move(row));
  }
  static constexpr std::array<const char*, 3U> kIds{"audio.sample-rate", "audio.block-frames",
                                                    "audio.channels"};
  static constexpr std::array<Str, 3U> kNames{Str::SampleRate, Str::BlockSize, Str::OutputChannels};
  const std::array<std::string, 3U> values{
      std::to_string(audio.current.sampleRate) + tr(Str::Hz),
      std::to_string(audio.current.blockFrames) + tr(Str::Frames),
      std::to_string(audio.current.outputChannels) + tr(Str::Channels)};
  for (std::size_t i = 0U; i < 3U; ++i) {
    OverlayControl field{kIds[i], cells[i], tr(kNames[i])};
    field.value = values[i];
    field.description = tr(Str::ActivateOrIncrementForTheNext);
    field.adjustable = true;
    out.push_back(std::move(field));
  }
  OverlayControl counts{"audio.diagnostics", stats, tr(Str::AudioDiagnostics), SemanticRole::Status, true,
                        false, false};
  counts.value = tr(Str::Underflow) + std::to_string(audio.underflowFrames) + tr(Str::XRun) +
                 std::to_string(audio.xruns) + (audio.diagnostic.empty() ? "" : " / " + audio.diagnostic);
  out.push_back(std::move(counts));
  return out;
}

void AudioSettingsOverlay::paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController&,
                                 const EditorSceneState& state, const SingLayout&, ui::Rect panel,
                                 const std::vector<OverlayControl>& controls) const {
  const auto& audio = state.audioSettings;
  c.save();
  c.clipRect(panel);
  const auto labelStyle =
      style(FontRole::UiSemibold, t.type.smallLabel, t.type.labelTracking, TextAlign::Left, true);
  if (panel.height >= 200.0)
    c.text({panel.x + kPanelInset, panel.y + 42.0, std::max(1.0, panel.width - 2.0 * kPanelInset - 70.0), 18.0},
           audio.reported ? std::string{tr(Str::OutputDevice)} : std::string{tr(Str::OutputDeviceNotReportedYet)},
           labelStyle, audio.reported ? t.color.textSecondary : t.color.warning);
  for (const auto& control : controls) {
    const auto& r = control.bounds;
    if (control.id == "shell.overlay.audio.close") {
      paintOverlayControl(c, t, r, tr(Str::Close), SemanticRole::Button, true, false, false);
    } else if (control.id.ends_with("devices-up") || control.id.ends_with("devices-down")) {
      paintOverlayControl(c, t, r, control.id.ends_with("up") ? tr(Str::Text7) : tr(Str::Text8),
                          SemanticRole::Button, control.enabled, false, false);
    } else if (control.id.starts_with("audio.device.")) {
      paintOverlayControl(c, t, r, {}, SemanticRole::Button, true, control.selected, false);
      c.text({r.x + 10.0, r.y, std::max(1.0, r.width - 130.0), r.height}, control.name,
             style(FontRole::Ui, t.type.label), control.selected ? t.color.accent : t.color.textPrimary);
      c.text({r.right() - 118.0, r.y, 110.0, r.height}, control.value,
             style(FontRole::Ui, t.type.smallLabel, 0.0, TextAlign::Right), t.color.textSecondary);
    } else if (control.id == "audio.diagnostics") {
      const auto counts = tr(Str::UnderflowFrames) + std::to_string(audio.underflowFrames) + tr(Str::XRuns) +
                          std::to_string(audio.xruns);
      if (r.height >= 30.0) {
        c.text({r.x, r.y, r.width, 16.0}, counts, style(FontRole::Mono, t.type.smallLabel),
               t.color.textSecondary);
        if (!audio.diagnostic.empty())
          c.text({r.x, r.y + 18.0, r.width, 16.0}, audio.diagnostic, style(FontRole::Ui, t.type.smallLabel),
                 t.color.warning);
      } else {
        c.text(r, audio.diagnostic.empty() ? counts : counts + tr(Str::Text4) + audio.diagnostic,
               style(FontRole::Ui, t.type.smallLabel),
               audio.diagnostic.empty() ? t.color.textSecondary : t.color.warning);
      }
    } else {
      // A stepped setting: its name above, its value below, the whole cell one button.
      paintOverlayControl(c, t, r, {}, SemanticRole::Button, true, false, false);
      c.text({r.x + 8.0, r.y + 4.0, std::max(1.0, r.width - 16.0), 12.0}, control.name, labelStyle,
             t.color.textSecondary);
      c.text({r.x + 8.0, r.bottom() - 20.0, std::max(1.0, r.width - 16.0), 16.0}, control.value,
             style(FontRole::Mono, t.type.label), t.color.textPrimary);
    }
  }
  c.restore();
}

core::Result<void> AudioSettingsOverlay::perform(NativeEditorController& controller,
                                                 std::string_view id, SemanticAction action) const {
  const auto activate = action == SemanticAction::Activate || action == SemanticAction::Toggle;
  using Field = NativeEditorController::AudioSettingsField;
  const auto field = id == "audio.sample-rate"    ? std::optional<Field>{Field::SampleRate}
                     : id == "audio.block-frames" ? std::optional<Field>{Field::BlockFrames}
                     : id == "audio.channels"     ? std::optional<Field>{Field::Channels}
                                                  : std::nullopt;
  if (field.has_value()) {
    // Activate steps forward, as the classic field did; Increment and Decrement step either way,
    // as the classic arrows did.
    if (activate || action == SemanticAction::Increment) return controller.cycleAudioSettings(*field, 1);
    if (action == SemanticAction::Decrement) return controller.cycleAudioSettings(*field, -1);
    return core::failure(core::ErrorCode::Unsupported, tr(Str::ThisSettingSteps));
  }
  if (!activate) return core::failure(core::ErrorCode::Unsupported, tr(Str::ThisControlOnlyActivates));
  if (id == "shell.overlay.audio.close") {
    controller.closeAudioSettings();
    return core::success();
  }
  if (id == "shell.overlay.audio.devices-up") {
    if (firstDevice_ > 0U) --firstDevice_;
    return core::success();
  }
  if (id == "shell.overlay.audio.devices-down") {
    if (firstDevice_ < lastFirstDevice_) ++firstDevice_;
    return core::success();
  }
  constexpr std::string_view kDevice{"audio.device."};
  if (id.starts_with(kDevice)) {
    std::size_t index = 0U;
    if (!parseIndex(id.substr(kDevice.size()), index))
      return core::failure(core::ErrorCode::InvalidArgument, tr(Str::AudioDeviceAccessibilityIndexIsInvalid));
    return controller.selectAudioDevice(index);
  }
  return core::failure(core::ErrorCode::NotFound, tr(Str::UnknownAudioSettingsControl));
}

bool AudioSettingsOverlay::key(NativeEditorController& controller, std::string_view focusedId,
                               const KeyEvent& event) const {
  using Field = NativeEditorController::AudioSettingsField;
  switch (event.key) {
    case NativeKey::Enter:
    case NativeKey::Space:
      static_cast<void>(perform(controller, focusedId, SemanticAction::Activate));
      return true;
    // The classic panel's keys: Left and Right step the sample rate, Up and Down the block size,
    // Shift with Up and Down the channels, and I closes it.
    case NativeKey::Left:
    case NativeKey::Right:
      static_cast<void>(controller.cycleAudioSettings(Field::SampleRate, event.key == NativeKey::Right ? 1 : -1));
      return true;
    case NativeKey::Up:
    case NativeKey::Down:
      static_cast<void>(controller.cycleAudioSettings(
          event.modifiers.shift ? Field::Channels : Field::BlockFrames, event.key == NativeKey::Up ? 1 : -1));
      return true;
    case NativeKey::I:
      controller.closeAudioSettings();
      return true;
    default: return false;
  }
}

// ---- Voice browser -----------------------------------------------------------------------------

// The installed singers and voicebanks as a large sheet ("Change voice" on the SINGER card and
// VOICE's "Open voice browser"). Choosing a card asks the host to replace the selected track's
// voicebank; Refresh and Install are the host's own commands; each is the command the classic
// browser's pointer, keys (R, O, V) and accessibility nodes ran.
constexpr double kCardHeight = 76.0;
constexpr double kCardGap = 8.0;
constexpr double kCardMinimumWidth = 250.0;

struct BrowserGeometry final {
  bool toolbarInHeader{true};
  std::array<ui::Rect, 3U> toolbar{};  // Refresh, Install, Close
  std::size_t columns{1U};
  std::size_t perPage{1U};
  std::size_t first{0U};
  std::vector<ui::Rect> cards;         // from first on
  bool paged{false};
  ui::Rect previous, next, pageLabel;
  ui::Rect empty;
};

BrowserGeometry browserGeometry(std::size_t count, std::size_t requestedFirst, ui::Rect panel) {
  BrowserGeometry g;
  if (panel.width <= 0.0) return g;
  const auto inner = std::max(1.0, panel.width - 2.0 * kPanelInset);
  // The three commands sit in the header when there is room beside the title, else in a row under it.
  constexpr double kButton = 76.0;
  g.toolbarInHeader = panel.width >= 3.0 * kButton + 12.0 + 2.0 * kPanelInset + 110.0;
  const auto toolbarY = g.toolbarInHeader ? panel.y + 8.0 : panel.y + 42.0;
  for (std::size_t i = 0U; i < 3U; ++i)
    g.toolbar[i] = {panel.right() - kPanelInset - static_cast<double>(3U - i) * kButton -
                        static_cast<double>(2U - i) * 6.0,
                    toolbarY, kButton, 26.0};
  const auto top = g.toolbarInHeader ? panel.y + 46.0 : panel.y + 76.0;
  g.columns = std::max<std::size_t>(1U, static_cast<std::size_t>((inner + kCardGap) / (kCardMinimumWidth + kCardGap)));
  const auto cardWidth = (inner - kCardGap * static_cast<double>(g.columns - 1U)) / static_cast<double>(g.columns);
  const auto rowsWithout = static_cast<std::size_t>(std::max(0.0, panel.bottom() - 10.0 - top + kCardGap) /
                                                    (kCardHeight + kCardGap));
  g.paged = count > rowsWithout * g.columns;
  const auto bottom = panel.bottom() - 10.0 - (g.paged ? 34.0 : 0.0);
  const auto rows = std::max<std::size_t>(
      1U, static_cast<std::size_t>(std::max(0.0, bottom - top + kCardGap) / (kCardHeight + kCardGap)));
  g.perPage = rows * g.columns;
  g.first = count == 0U ? 0U : std::min((requestedFirst / g.perPage) * g.perPage, ((count - 1U) / g.perPage) * g.perPage);
  for (std::size_t i = g.first; i < count && i < g.first + g.perPage; ++i) {
    const auto k = i - g.first;
    const ui::Rect r{panel.x + kPanelInset + static_cast<double>(k % g.columns) * (cardWidth + kCardGap),
                     top + static_cast<double>(k / g.columns) * (kCardHeight + kCardGap), cardWidth,
                     kCardHeight};
    if (r.bottom() > bottom + 0.5) break;
    g.cards.push_back(r);
  }
  if (g.paged) {
    const auto y = panel.bottom() - 36.0;
    g.previous = {panel.x + kPanelInset, y, 88.0, 26.0};
    g.next = {panel.x + kPanelInset + 96.0, y, 88.0, 26.0};
    g.pageLabel = {panel.x + kPanelInset + 196.0, y, std::max(1.0, inner - 196.0), 26.0};
  }
  g.empty = {panel.x + kPanelInset, top, inner, std::max(0.0, bottom - top)};
  return g;
}

bool cardSelected(const EditorSceneState& state, const authoring::VoicebankCard& card) {
  return state.inspector.valid && state.inspector.vocal && state.inspector.voicebank.id == card.id &&
         state.inspector.voicebank.version == card.version &&
         state.inspector.voicebank.contentHash == card.contentHash;
}

std::string cardRange(const authoring::VoicebankCard& card) {
  if (card.rootPitchLayers.empty()) return tr(Str::RangeUnknown);
  const auto [low, high] = std::minmax_element(card.rootPitchLayers.begin(), card.rootPitchLayers.end());
  return *low == *high ? noteName(*low) : noteName(*low) + tr(Str::Text6) + noteName(*high);
}

std::string cardFeatures(const authoring::VoicebankCard& card) {
  std::string features;
  if (card.hasSustain) features += tr(Str::Sustain);
  if (card.hasRelease) features += tr(Str::Release);
  if (card.hasBreath) features += tr(Str::Breath2);
  if (features.empty()) return tr(Str::NoReleaseSustainData);
  features.pop_back();
  return features;
}

class VoicebankBrowserOverlay final : public ShellOverlay {
public:
  [[nodiscard]] OverlayKind kind() const noexcept override { return OverlayKind::VoicebankBrowser; }
  [[nodiscard]] std::string_view idPrefix() const noexcept override {
    return "shell.overlay.voicebank.";
  }
  [[nodiscard]] bool wanted(const NativeEditorController&, const EditorSceneState& state) const
      noexcept override {
    return state.voicebankBrowserVisible;
  }
  [[nodiscard]] ui::Rect panel(const NativeEditorController&, const EditorSceneState&,
                               const SingLayout&, ui::Rect slot) const override {
    if (slot.width <= 0.0 || slot.height <= 0.0) return {};
    // A large sheet: most of the body, never the header.
    return fitPanel(slot, std::max(slot.width * 0.94, 420.0), std::max(slot.height * 0.94, 260.0));
  }
  [[nodiscard]] std::string title(const NativeEditorController&,
                                  const EditorSceneState& state) const override {
    return state.voicebankCards.size() == 1U
               ? std::string{tr(Str::Voices1Installed)}
               : tr(Str::Voices) + std::to_string(state.voicebankCards.size()) + tr(Str::Installed);
  }
  [[nodiscard]] std::vector<OverlayControl> controls(const NativeEditorController& controller,
                                                     const EditorSceneState& state,
                                                     const SingLayout&, ui::Rect panel) const override;
  [[nodiscard]] std::string openerId(const NativeEditorController&,
                                     const EditorSceneState&) const override {
    return "shell.change-voice";
  }
  void paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController& controller,
             const EditorSceneState& state, const SingLayout&, ui::Rect panel,
             const std::vector<OverlayControl>& controls) const override;
  core::Result<void> perform(NativeEditorController& controller, std::string_view id,
                             SemanticAction action) const override;
  bool key(NativeEditorController& controller, std::string_view focusedId,
           const KeyEvent& event) const override;
  core::Result<void> close(NativeEditorController& controller) const override {
    controller.closeVoicebankBrowser();
    return core::success();
  }
  bool scroll(NativeEditorController& controller, const EditorSceneState& state, const SingLayout&,
              ui::Rect panel, ui::Point, double, double deltaY, InputModifiers) const override {
    const auto g = browserGeometry(state.voicebankCards.size(), controller.voicebankBrowserFirstCard(), panel);
    if (!g.paged || deltaY == 0.0) return true;
    page(controller, g, deltaY < 0.0 ? 1 : -1);
    return true;
  }

private:
  static void page(NativeEditorController& controller, const BrowserGeometry& g, int direction) {
    if (direction < 0) controller.setVoicebankBrowserFirstCard(g.first >= g.perPage ? g.first - g.perPage : 0U);
    else controller.setVoicebankBrowserFirstCard(g.first + g.perPage);
  }
  // The page the last layout used, so a pager command moves by exactly one page.
  mutable BrowserGeometry last_;
};

std::vector<OverlayControl> VoicebankBrowserOverlay::controls(const NativeEditorController& controller,
                                                              const EditorSceneState& state,
                                                              const SingLayout&, ui::Rect panel) const {
  std::vector<OverlayControl> out;
  if (panel.width <= 0.0) return out;
  const auto& cards = state.voicebankCards;
  const auto g = browserGeometry(cards.size(), controller.voicebankBrowserFirstCard(), panel);
  last_ = g;
  for (std::size_t k = 0U; k < g.cards.size(); ++k) {
    const auto i = g.first + k;
    const auto& card = cards[i];
    const auto selected = cardSelected(state, card);
    OverlayControl control{"voicebank.card." + std::to_string(i), g.cards[k], card.displayName,
                           SemanticRole::Button, card.selectable, selected};
    control.value = card.version + " / " + card.trustLabel + (selected ? tr(Str::Selected) : "");
    control.description = (card.language.empty() ? std::string{tr(Str::LanguageUnknown)} : card.language) +
                          " / " + cardRange(card) + " / " + std::to_string(card.styles.size()) +
                          tr(Str::StylesUnits) + std::to_string(card.enabledUnitCount) + tr(Str::Enabled) +
                          std::to_string(card.disabledUnitCount) + tr(Str::Disabled) + cardFeatures(card) +
                          tr(Str::Hash) + card.contentHashAbbreviation;
    for (const auto& diagnostic : card.diagnostics) control.description += " / " + diagnostic;
    if (!card.selectable) control.description += tr(Str::NotSelectableNotTrusted);
    out.push_back(std::move(control));
  }
  if (g.paged) {
    out.push_back({"shell.overlay.voicebank.previous", g.previous, tr(Str::PreviousVoices),
                   SemanticRole::Button, g.first > 0U});
    out.push_back({"shell.overlay.voicebank.next", g.next, tr(Str::NextVoices), SemanticRole::Button,
                   g.first + g.perPage < cards.size()});
  }
  static constexpr std::array<const char*, 3U> kIds{"shell.overlay.voicebank.refresh",
                                                    "shell.overlay.voicebank.install",
                                                    "shell.overlay.voicebank.close"};
  static constexpr std::array<Str, 3U> kNames{Str::RefreshInstalledVoices,
                                                      Str::InstallAVoicebank, Str::CloseVoiceBrowser};
  for (std::size_t i = 0U; i < 3U; ++i) out.push_back({kIds[i], g.toolbar[i], tr(kNames[i])});
  return out;
}

void VoicebankBrowserOverlay::paint(Canvas2D& c, const DesignTokens& t,
                                    const NativeEditorController& controller,
                                    const EditorSceneState& state, const SingLayout&, ui::Rect panel,
                                    const std::vector<OverlayControl>& controls) const {
  const auto& cards = state.voicebankCards;
  const auto g = browserGeometry(cards.size(), controller.voicebankBrowserFirstCard(), panel);
  c.save();
  c.clipRect(panel);
  static constexpr std::array<Str, 3U> kLabels{Str::Refresh, Str::Install, Str::Close};
  for (std::size_t i = 0U; i < 3U; ++i)
    paintOverlayControl(c, t, g.toolbar[i], tr(kLabels[i]), SemanticRole::Button, true, false, false);
  if (cards.empty()) {
    c.text({g.empty.x, g.empty.y, g.empty.width, 20.0}, tr(Str::NoInstalledVoicebanks),
           style(FontRole::UiSemibold, t.type.label), t.color.textPrimary);
    c.text({g.empty.x, g.empty.y + 22.0, g.empty.width, 18.0},
           tr(Str::InstallABankThenRefresh), style(FontRole::Ui, t.type.smallLabel),
           t.color.textSecondary);
  }
  for (const auto& control : controls) {
    if (!control.id.starts_with("voicebank.card.")) continue;
    std::size_t index = 0U;
    if (!parseIndex(std::string_view{control.id}.substr(15U), index) || index >= cards.size()) continue;
    const auto& card = cards[index];
    const auto& r = control.bounds;
    const auto p = Path::roundedRect(r, t.shape.control + 2.0);
    if (control.selected) {
      c.save();
      c.setGlow(withAlpha(t.color.accent, 0.5), 10.0);
      c.fill(p, withAlpha(t.color.accent, 0.16));
      c.restore();
      c.stroke(p, t.color.accent, StrokeStyle{1.2});
    } else {
      c.fill(p, withAlpha(t.color.surfaceSunken, 0.9));
      c.stroke(p, card.selectable ? withAlpha(t.color.border, 0.95) : withAlpha(t.color.warning, 0.8),
               StrokeStyle{1.0});
    }
    const auto x = r.x + 12.0;
    const auto w = std::max(1.0, r.width - 24.0);
    c.text({x, r.y + 6.0, w, 18.0}, card.displayName + "  " + card.version,
           style(FontRole::UiSemibold, t.type.label),
           control.selected ? t.color.accent : card.selectable ? t.color.textPrimary : t.color.textDisabled);
    c.text({x, r.y + 25.0, w, 14.0},
           (card.language.empty() ? std::string{tr(Str::Text2)} : card.language) + tr(Str::Text4) + cardRange(card) +
               tr(Str::Text4) + card.trustLabel,
           style(FontRole::Ui, t.type.smallLabel), card.selectable ? t.color.textSecondary : t.color.warning);
    c.text({x, r.y + 40.0, w, 14.0},
           std::to_string(card.styles.size()) + tr(Str::Styles) + std::to_string(card.enabledUnitCount) +
               tr(Str::Units) + cardFeatures(card),
           style(FontRole::Ui, t.type.smallLabel), t.color.textSecondary);
    const auto last = !card.diagnostics.empty() ? card.diagnostics.front()
                      : control.selected       ? std::string{tr(Str::SelectedForThisTrack)}
                                               : tr(Str::Hash2) + card.contentHashAbbreviation;
    c.text({x, r.y + 55.0, w, 14.0}, last, style(FontRole::Ui, t.type.rulerMicro + 1.0),
           !card.diagnostics.empty() ? t.color.warning : control.selected ? t.color.accent : t.color.textDisabled);
  }
  if (g.paged) {
    paintOverlayControl(c, t, g.previous, tr(Str::Previous), SemanticRole::Button, g.first > 0U, false, false);
    paintOverlayControl(c, t, g.next, tr(Str::Next), SemanticRole::Button, g.first + g.perPage < cards.size(),
                        false, false);
    c.text(g.pageLabel,
           std::to_string(g.first + 1U) + tr(Str::Text6) +
               std::to_string(std::min(g.first + g.perPage, cards.size())) + tr(Str::Of) +
               std::to_string(cards.size()),
           style(FontRole::Ui, t.type.smallLabel), t.color.textSecondary);
  }
  c.restore();
}

core::Result<void> VoicebankBrowserOverlay::perform(NativeEditorController& controller,
                                                    std::string_view id, SemanticAction action) const {
  if (action != SemanticAction::Activate && action != SemanticAction::Toggle)
    return core::failure(core::ErrorCode::Unsupported, tr(Str::ThisControlOnlyActivates));
  if (id == "shell.overlay.voicebank.refresh") return controller.refreshVoicebanks();
  if (id == "shell.overlay.voicebank.install") return controller.openVoicebankInstaller();
  if (id == "shell.overlay.voicebank.close") {
    controller.closeVoicebankBrowser();
    return core::success();
  }
  if (id == "shell.overlay.voicebank.previous" || id == "shell.overlay.voicebank.next") {
    page(controller, last_, id.ends_with("next") ? 1 : -1);
    return core::success();
  }
  constexpr std::string_view kCard{"voicebank.card."};
  if (id.starts_with(kCard)) {
    std::size_t index = 0U;
    if (!parseIndex(id.substr(kCard.size()), index))
      return core::failure(core::ErrorCode::InvalidArgument, tr(Str::VoicebankAccessibilityIndexIsInvalid));
    return controller.selectVoicebankCard(index);
  }
  return core::failure(core::ErrorCode::NotFound, tr(Str::UnknownVoiceBrowserControl));
}

bool VoicebankBrowserOverlay::key(NativeEditorController& controller, std::string_view focusedId,
                                  const KeyEvent& event) const {
  switch (event.key) {
    case NativeKey::Enter:
    case NativeKey::Space:
      static_cast<void>(perform(controller, focusedId, SemanticAction::Activate));
      return true;
    // The classic browser's keys: R refreshes, O opens the installer, V closes.
    case NativeKey::R: static_cast<void>(controller.refreshVoicebanks()); return true;
    case NativeKey::O: static_cast<void>(controller.openVoicebankInstaller()); return true;
    case NativeKey::V: controller.closeVoicebankBrowser(); return true;
    case NativeKey::Left:
    case NativeKey::Right:
      if (last_.paged) page(controller, last_, event.key == NativeKey::Right ? 1 : -1);
      return true;
    default: return false;
  }
}

// ---- Classic-only text fields ------------------------------------------------------------------

// The fields a classic surface owned, as an inline field card in the shell: the tempo or meter
// entry opened from the transport display, the bounded field (phone hint, find/replace query, a
// review's draft field) and a track or region name. The text arrives through the host's text input
// client exactly as before (the shell only moves the client to this card), and Enter, Tab and
// Escape are the controller's own commit and cancel.
TextInputAnchor fieldAnchor(NativeEditorController::TextFieldView::Kind kind) {
  using Kind = NativeEditorController::TextFieldView::Kind;
  switch (kind) {
    case Kind::Transport: return TextInputAnchor::Transport;
    case Kind::Rename: return TextInputAnchor::ArrangementField;
    case Kind::TimeMap: return TextInputAnchor::TimeMapPanel;
    case Kind::Bounded:
    case Kind::None: return TextInputAnchor::BoundedField;
  }
  return TextInputAnchor::BoundedField;
}

class TextFieldOverlay final : public ShellOverlay {
public:
  [[nodiscard]] OverlayKind kind() const noexcept override { return OverlayKind::TextField; }
  [[nodiscard]] std::string_view idPrefix() const noexcept override { return "shell.overlay.field."; }
  [[nodiscard]] bool wanted(const NativeEditorController& controller, const EditorSceneState&) const
      noexcept override {
    using Kind = NativeEditorController::TextFieldView::Kind;
    const auto field = controller.textFieldView().kind;
    return field == Kind::Transport || field == Kind::Bounded || field == Kind::Rename;
  }
  [[nodiscard]] ui::Rect panel(const NativeEditorController& controller, const EditorSceneState&,
                               const SingLayout& layout, ui::Rect) const override {
    return textFieldPlacement(fieldAnchor(controller.textFieldView().kind), layout).panel;
  }
  [[nodiscard]] std::string title(const NativeEditorController& controller,
                                  const EditorSceneState&) const override {
    return controller.textFieldView().label;
  }
  [[nodiscard]] std::vector<OverlayControl> controls(const NativeEditorController& controller,
                                                     const EditorSceneState&, const SingLayout& layout,
                                                     ui::Rect) const override {
    std::vector<OverlayControl> out;
    const auto field = controller.textFieldView();
    const auto placement = textFieldPlacement(fieldAnchor(field.kind), layout);
    if (placement.panel.width <= 0.0) return out;
    OverlayControl input{field.inputId.empty() ? std::string{"shell.overlay.field.input"} : field.inputId,
                         placement.input, field.inputName, SemanticRole::TextField, true, false, false};
    input.value = field.text;
    input.description = field.error;
    input.editable = true;
    out.push_back(std::move(input));
    out.push_back({field.cancelId.empty() ? std::string{"shell.overlay.field.cancel"} : field.cancelId,
                   placement.cancel, field.cancelName});
    return out;
  }
  void paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController& controller,
             const EditorSceneState&, const SingLayout&, ui::Rect panel,
             const std::vector<OverlayControl>& controls) const override {
    if (controls.size() < 2U) return;
    const auto field = controller.textFieldView();
    c.save();
    c.clipRect(panel);
    paintOverlayTextField(c, t, controls[0].bounds, {}, controls[0].value, true);
    paintOverlayControl(c, t, controls[1].bounds, tr(Str::Cancel), SemanticRole::Button, true, false, false);
    const auto& input = controls[0].bounds;
    c.text({input.x, input.bottom() + 6.0, std::max(1.0, panel.right() - kPanelInset - input.x), 16.0},
           field.error.empty() ? field.inputName : field.error, style(FontRole::Ui, t.type.smallLabel),
           field.error.empty() ? t.color.textSecondary : t.color.warning);
    c.restore();
  }
  core::Result<void> perform(NativeEditorController& controller, std::string_view id,
                             SemanticAction action) const override {
    const auto field = controller.textFieldView();
    const auto inputId = field.inputId.empty() ? std::string{"shell.overlay.field.input"} : field.inputId;
    if (id == inputId)
      return action == SemanticAction::EditText || action == SemanticAction::Activate
                 ? core::success()
                 : core::failure(core::ErrorCode::Unsupported, tr(Str::TheFieldTakesText));
    if (action != SemanticAction::Activate && action != SemanticAction::Toggle)
      return core::failure(core::ErrorCode::Unsupported, tr(Str::ThisControlOnlyActivates));
    // The controller's own cancel node where it publishes one, else its cancel command (which is
    // all that node runs).
    if (!field.cancelId.empty() && id == field.cancelId)
      return controller.dispatchAccessibility(id, SemanticAction::Activate);
    if (id == "shell.overlay.field.cancel") {
      controller.cancelTextComposition();
      return core::success();
    }
    return core::failure(core::ErrorCode::NotFound, tr(Str::UnknownFieldControl));
  }
  bool key(NativeEditorController&, std::string_view, const KeyEvent&) const override {
    // Typing reaches the host's text input client, never this handler; Enter, Tab and Escape are
    // routed by the shell to the controller's own commit and cancel.
    return false;
  }
  core::Result<void> close(NativeEditorController& controller) const override {
    controller.cancelTextComposition();
    return core::success();
  }
  core::Result<void> setValue(NativeEditorController& controller, std::string_view id,
                              std::string_view value) const override {
    const auto field = controller.textFieldView();
    // The bounded field and the transport's tempo or meter keep the controller's own value path
    // (the ids its classic tree published); a name field commits through the same commit its
    // Enter runs.
    if (!field.inputId.empty()) {
      if (id != field.inputId)
        return core::failure(core::ErrorCode::Unsupported, tr(Str::ThisControlHasNoEditableValue));
      return controller.setAccessibilityValue(id, value);
    }
    if (id != "shell.overlay.field.input")
      return core::failure(core::ErrorCode::Unsupported, tr(Str::ThisControlHasNoEditableValue));
    if (value.size() > 4096U) return core::failure(core::ErrorCode::InvalidArgument, tr(Str::TheNameIsTooLong));
    const auto decoded = domain::fromUtf8(std::string{value});
    if (!decoded) return core::Result<void>{decoded.error()};
    return controller.commitTextComposition(decoded.value());
  }
};

// ---- SINGER card menu --------------------------------------------------------------------------

// One entry of the singer menu: the controller's own public command, run exactly as its existing
// opener runs it. The menu adds no behavior of its own; a command the controller refuses reports
// its own reason, which the menu keeps on the item (disabled) until it opens again.
// The name and description are table keys, read through tr() each time the menu is laid out, so
// the menu follows the installed string table.
struct SingerMenuItem final {
  std::string_view key;
  Str name;
  Str description;
  core::Result<void> (*run)(NativeEditorController&);
};

const std::array<SingerMenuItem, 9U>& singerMenuItems() {
  static const std::array<SingerMenuItem, 9U> items{{
      {"replacement-review", Str::ReplacementReview,
       Str::FindLyricsAndReviewEveryReplacement,
       [](NativeEditorController& c) { return c.beginReplacementInput(); }},
      {"dynamics", Str::DynamicsInspector, Str::ReviewAndDrawTheRegionS,
       [](NativeEditorController& c) { return c.openDynamicsInspector(); }},
      {"vibrato", Str::VibratoInspector, Str::ReviewAndEditTheVibratoOf,
       [](NativeEditorController& c) { return c.openVibratoInspector(); }},
      {"style", Str::StyleCoverage, Str::ReviewWhichStylesTheSelectedSinger,
       [](NativeEditorController& c) { return c.openStyleCoverageSheet(); }},
      {"japanese-reading", Str::JapaneseReading,
       Str::ResolveTheKanaReadingOfThe,
       [](NativeEditorController& c) { return c.openJapaneseReadingReview(); }},
      {"phoneme-review", Str::PhonemeReview, Str::ReviewPhonemeBindingsAndRetainedRender,
       [](NativeEditorController& c) { return c.openPhonemeReview(); }},
      {"change-voice", Str::ChangeVoice, Str::OpenTheVoiceBrowserAndChoose,
       [](NativeEditorController& c) -> core::Result<void> {
         c.showVoicebankBrowser();
         return core::success();
       }},
      {"install-voicebank", Str::InstallOrRelinkVoicebank,
       Str::OpenTheVoicebankInstallerToAdd,
       [](NativeEditorController& c) { return c.openVoicebankInstaller(); }},
      {"refresh-voicebanks", Str::RescanVoicebanks, Str::RescanTheInstalledVoicebanks,
       [](NativeEditorController& c) { return c.refreshVoicebanks(); }},
  }};
  return items;
}

constexpr std::string_view kSingerMenuPrefix{"shell.overlay.singer-menu."};
// The card header's rule sits 38 points down; items start below it, 26-point rows on a 28 pitch.
constexpr double kSingerMenuTop = 46.0;
constexpr double kSingerMenuRow = 26.0;
constexpr double kSingerMenuPitch = 28.0;
constexpr double kSingerMenuBottom = 10.0;
constexpr double kSingerMenuInset = 12.0;
constexpr double kSingerMenuColumn = 236.0;
constexpr double kSingerMenuMinColumn = 132.0;

struct SingerMenuGeometry final {
  ui::Rect panel;
  std::size_t rows{0U};
  double columnWidth{0.0};
};

// A popover under the ⋯ button (above it when the body has no room below), its right edge on the
// button's, clamped into the overlay slot. A slot too short for one column takes two or three, so
// every item stays on screen at the minimum window; a slot that holds none places no menu.
SingerMenuGeometry singerMenuGeometry(ui::Rect anchor, ui::Rect slot) {
  const auto count = singerMenuItems().size();
  if (anchor.width <= 0.0 || slot.width <= 0.0 || slot.height <= 0.0) return {};
  for (std::size_t columns = 1U; columns <= 3U; ++columns) {
    const auto rows = (count + columns - 1U) / columns;
    const auto height = kSingerMenuTop + static_cast<double>(rows) * kSingerMenuPitch -
                        (kSingerMenuPitch - kSingerMenuRow) + kSingerMenuBottom;
    const auto gaps = 8.0 * static_cast<double>(columns - 1U);
    const auto width = std::min(
        slot.width, 2.0 * kSingerMenuInset + gaps + static_cast<double>(columns) * kSingerMenuColumn);
    const auto columnWidth =
        (width - 2.0 * kSingerMenuInset - gaps) / static_cast<double>(columns);
    if (height > slot.height || width < kMinimumPanelWidth || columnWidth < kSingerMenuMinColumn)
      continue;
    auto top = anchor.bottom() + 6.0;
    if (top + height > slot.bottom()) {
      const auto above = anchor.y - 6.0 - height;
      top = above >= slot.y ? above : slot.bottom() - height;
    }
    top = std::clamp(top, slot.y, std::max(slot.y, slot.bottom() - height));
    const auto left =
        std::clamp(anchor.right() - width, slot.x, std::max(slot.x, slot.right() - width));
    return {{left, top, width, height}, rows, columnWidth};
  }
  return {};
}

class SingerMenuOverlay final : public ShellOverlay {
public:
  [[nodiscard]] OverlayKind kind() const noexcept override { return OverlayKind::SingerMenu; }
  [[nodiscard]] std::string_view idPrefix() const noexcept override { return kSingerMenuPrefix; }
  // The menu is the shell's own presentation: the shell's flag decides whether it is up.
  [[nodiscard]] bool wanted(const NativeEditorController&, const EditorSceneState&) const
      noexcept override {
    return true;
  }
  [[nodiscard]] ui::Rect panel(const NativeEditorController&, const EditorSceneState&,
                               const SingLayout& layout, ui::Rect slot) const override {
    return singerMenuGeometry(layout.singerMenu, slot).panel;
  }
  [[nodiscard]] std::string title(const NativeEditorController&,
                                  const EditorSceneState&) const override {
    return tr(Str::Singer);
  }
  [[nodiscard]] std::vector<OverlayControl> controls(const NativeEditorController&,
                                                     const EditorSceneState&,
                                                     const SingLayout& layout,
                                                     ui::Rect panel) const override {
    std::vector<OverlayControl> out;
    const auto g = singerMenuGeometry(layout.singerMenu, panel);
    if (panel.width <= 0.0 || g.rows == 0U) return out;
    const auto& items = singerMenuItems();
    for (std::size_t i = 0U; i < items.size(); ++i) {
      const auto& item = items[i];
      const auto column = static_cast<double>(i / g.rows);
      const auto row = static_cast<double>(i % g.rows);
      const auto refused = refusals_.find(item.key);
      const auto enabled = refused == refusals_.end();
      out.push_back(OverlayControl{
          .id = std::string{kSingerMenuPrefix} + std::string{item.key},
          .bounds = {panel.x + kSingerMenuInset + column * (g.columnWidth + 8.0),
                     panel.y + kSingerMenuTop + row * kSingerMenuPitch, g.columnWidth,
                     kSingerMenuRow},
          .name = std::string{tr(item.name)},
          .role = SemanticRole::Button,
          .enabled = enabled,
          .activatable = enabled,
          .description = enabled ? std::string{tr(item.description)} : refused->second,
      });
    }
    return out;
  }
  [[nodiscard]] std::string openerId(const NativeEditorController&,
                                     const EditorSceneState&) const override {
    return std::string{kSingerMenuButtonId};
  }
  // Refusals belong to one opening of the menu: the next one asks the controller again.
  void presented() const override { refusals_.clear(); }
  void paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController&,
             const EditorSceneState&, const SingLayout&, ui::Rect panel,
             const std::vector<OverlayControl>& controls) const override {
    c.save();
    c.clipRect(panel);
    const auto labelStyle = style(FontRole::UiSemibold, t.type.smallLabel, 0.2);
    const auto reasonStyle = style(FontRole::Ui, t.type.smallLabel);
    for (const auto& control : controls) {
      const auto& r = control.bounds;
      const auto p = Path::roundedRect(r, t.shape.control);
      c.fill(p, control.enabled ? withAlpha(t.color.surfaceSunken, 0.9)
                                : withAlpha(t.color.surface, 0.6));
      c.stroke(p, withAlpha(t.color.border, 0.95), StrokeStyle{1.0});
      // Both texts elide inside the row; the item's node carries the whole name and reason.
      const auto inner = std::max(1.0, r.width - 20.0);
      if (control.enabled) {
        c.text({r.x + 10.0, r.y, inner, r.height}, control.name, labelStyle, t.color.textPrimary);
        continue;
      }
      const auto labelWidth = std::min(c.measure(control.name, labelStyle) + 4.0, inner * 0.55);
      c.text({r.x + 10.0, r.y, labelWidth, r.height}, control.name, labelStyle,
             t.color.textDisabled);
      const auto reasonX = r.x + 10.0 + labelWidth + 8.0;
      if (r.right() - 10.0 - reasonX >= 12.0)
        c.text({reasonX, r.y, r.right() - 10.0 - reasonX, r.height}, control.description,
               reasonStyle, t.color.warning);
    }
    c.restore();
  }
  core::Result<void> perform(NativeEditorController& controller, std::string_view id,
                             SemanticAction action) const override {
    if (action != SemanticAction::Activate && action != SemanticAction::Toggle)
      return core::failure(core::ErrorCode::Unsupported, tr(Str::ThisMenuItemOnlyActivates));
    if (!id.starts_with(kSingerMenuPrefix))
      return core::failure(core::ErrorCode::NotFound, tr(Str::UnknownSingerMenuItem));
    const auto key = id.substr(kSingerMenuPrefix.size());
    for (const auto& item : singerMenuItems()) {
      if (item.key != key) continue;
      if (const auto refused = refusals_.find(key); refused != refusals_.end())
        return core::failure(core::ErrorCode::Conflict, refused->second);
      auto result = item.run(controller);
      if (!result)
        refusals_.insert_or_assign(std::string{key}, result.error().message.empty()
                                                         ? std::string{tr(Str::TheSingerRefusedThisCommand)}
                                                         : result.error().message);
      return result;
    }
    return core::failure(core::ErrorCode::NotFound, tr(Str::UnknownSingerMenuItem));
  }
  // Enter and Space run the focused item through the shell, which closes the menu when the command
  // ran; arrows walk the items there too. The menu has no keys of its own.
  bool key(NativeEditorController&, std::string_view, const KeyEvent&) const override {
    return false;
  }
  core::Result<void> close(NativeEditorController& controller) const override {
    // Nothing to close in the controller: the shell drops its own presentation of the menu.
    static_cast<void>(controller);
    return core::success();
  }

private:
  // Why the controller refused an item during this opening of the menu, by item key.
  mutable std::map<std::string, std::string, std::less<>> refusals_;
};

}  // namespace

// ---- Shared paint and lookup -------------------------------------------------------------------

const SemanticNode* overlayControllerNode(const AccessibilityTree& tree, std::string_view id) noexcept {
  const auto search = [id](const SemanticNode& node, const auto& self) -> const SemanticNode* {
    for (const auto& child : node.children) {
      if (child.id == id) return &child;
      if (const auto* found = self(child, self); found != nullptr) return found;
    }
    return nullptr;
  };
  return search(tree.root(), search);
}

const SemanticNode* overlayControllerNodeEndingWith(const AccessibilityTree& tree,
                                                    std::string_view suffix) noexcept {
  const auto search = [suffix](const SemanticNode& node, const auto& self) -> const SemanticNode* {
    for (const auto& child : node.children) {
      if (child.id.ends_with(suffix)) return &child;
      if (const auto* found = self(child, self); found != nullptr) return found;
    }
    return nullptr;
  };
  return search(tree.root(), search);
}

void paintOverlayControl(paint::Canvas2D& c, const DesignTokens& t, ui::Rect r,
                         std::string_view label, SemanticRole role, bool enabled, bool selected,
                         bool focused) {
  if (r.width <= 0.0 || r.height <= 0.0) return;
  const auto radius = t.shape.control;
  const auto p = Path::roundedRect(r, radius);
  if (selected) {
    c.save();
    c.setGlow(withAlpha(t.color.accent, 0.6), 8.0);
    c.fill(p, withAlpha(t.color.accent, 0.20));
    c.restore();
    c.stroke(p, t.color.accent, StrokeStyle{1.0});
  } else if (role == SemanticRole::Status || role == SemanticRole::Panel) {
    sunken(c, t, r, radius);
  } else {
    const auto fill = enabled ? withAlpha(t.color.surfaceSunken, 0.9)
                              : withAlpha(t.color.surface, 0.6);
    c.fill(p, fill);
    c.stroke(p, withAlpha(t.color.border, 0.95), StrokeStyle{1.0});
  }
  if (focused) {
    c.save();
    c.setGlow(t.color.focusRing, 6.0);
    c.stroke(Path::roundedRect({r.x - 2.0, r.y - 2.0, r.width + 4.0, r.height + 4.0}, radius + 2.0),
             t.color.focusRing, StrokeStyle{2.0});
    c.restore();
  }
  if (label.empty()) return;
  const auto textStyle = role == SemanticRole::Button
                             ? style(FontRole::UiSemibold, t.type.smallLabel, 0.6, TextAlign::Center,
                                     true)
                             : style(FontRole::Ui, t.type.smallLabel, 0.0, TextAlign::Center);
  // The label elides inside its own rectangle; the full name stays on the accessible node.
  c.text({r.x + 6.0, r.y, std::max(1.0, r.width - 12.0), r.height}, label, textStyle,
         enabled ? t.color.textPrimary : t.color.textDisabled);
}

ui::Rect paintShellOverlay(const ShellOverlay& overlay, paint::Canvas2D& c, const DesignTokens& t,
                           const NativeEditorController& controller,
                           const EditorSceneState& state, const SingLayout& layout, ui::Rect slot) {
  const auto panel = overlay.panel(controller, state, layout, slot);
  if (panel.width <= 0.0 || panel.height <= 0.0) return {};
  // A dimmed field over everything the overlay covers, so the score reads as inert behind it.
  c.save();
  c.setAlpha(0.55);
  c.fill(Path::rect({0.0, 0.0, c.width(), c.height()}), kBlack);
  c.restore();
  c.save();
  c.setGlow(withAlpha(t.color.accent, 0.30), 18.0);
  card(c, t, panel, t.shape.hero);
  c.restore();
  cardHeader(c, t, panel, overlay.title(controller, state));
  const auto controls = overlay.controls(controller, state, layout, panel);
  overlay.paint(c, t, controller, state, layout, panel, controls);
  return panel;
}

std::unique_ptr<ShellOverlay> makeSampleMicroscopeOverlay() {
  return std::make_unique<SampleMicroscopeOverlay>();
}
std::unique_ptr<ShellOverlay> makePhonemeReviewOverlay() {
  return std::make_unique<PhonemeReviewOverlay>();
}
std::unique_ptr<ShellOverlay> makeTimeMapOverlay() { return std::make_unique<TimeMapOverlay>(); }
std::unique_ptr<ShellOverlay> makeRecoverySupportOverlay() {
  return std::make_unique<RecoverySupportOverlay>();
}
std::unique_ptr<ShellOverlay> makeOverlapDetailOverlay() {
  return std::make_unique<OverlapDetailOverlay>();
}
std::unique_ptr<ShellOverlay> makeDiagnosticsOverlay() {
  return std::make_unique<DiagnosticsOverlay>();
}

ui::Rect timeMapPanelBounds(ui::Rect slot) {
  if (slot.width <= 0.0 || slot.height <= 0.0) return {};
  // A popover from the transport display, centred over the body like a sheet.
  return fitPanel(slot, std::min(slot.width, 520.0), std::max(slot.height * 0.9, 260.0));
}

TextFieldPlacement timeMapFieldPlacement(ui::Rect timeMapPanel) {
  if (timeMapPanel.width <= 0.0) return {};
  // Where the prompt line was, above the event rows.
  const auto inner = std::max(1.0, timeMapPanel.width - 2.0 * kPanelInset);
  const auto cancel = std::min(88.0, inner * 0.3);
  return {timeMapPanel,
          {timeMapPanel.x + kPanelInset, timeMapPanel.y + 42.0, std::max(1.0, inner - cancel - 8.0), 28.0},
          {timeMapPanel.right() - kPanelInset - cancel, timeMapPanel.y + 42.0, cancel, 28.0}};
}

TextFieldPlacement textFieldPlacement(TextInputAnchor anchor, const SingLayout& layout) {
  const auto slot = layout.overlay;
  if (slot.width <= 0.0 || slot.height <= 0.0) return {};
  const auto width = std::min(slot.width, 420.0);
  const auto height = std::min(slot.height, 112.0);
  if (width < kMinimumPanelWidth || height < kMinimumPanelHeight) return {};
  // The tempo or meter field drops from the transport display it edits; every other field is a
  // field card at the top of the body, centred.
  const auto x = anchor == TextInputAnchor::Transport && layout.tempoReadout.width > 0.0
                     ? std::clamp(layout.tempoReadout.x + layout.tempoReadout.width * 0.5 - width * 0.5,
                                  slot.x, slot.right() - width)
                     : slot.x + (slot.width - width) * 0.5;
  const ui::Rect panel{x, slot.y, width, height};
  const auto inner = width - 2.0 * kPanelInset;
  const auto cancel = std::min(88.0, inner * 0.3);
  return {panel,
          {panel.x + kPanelInset, panel.y + 46.0, std::max(1.0, inner - cancel - 8.0), 30.0},
          {panel.right() - kPanelInset - cancel, panel.y + 46.0, cancel, 30.0}};
}

std::unique_ptr<ShellOverlay> makeReplacementReviewOverlay() {
  return std::make_unique<ReplacementReviewOverlay>();
}
std::unique_ptr<ShellOverlay> makeAudioSettingsOverlay() {
  return std::make_unique<AudioSettingsOverlay>();
}
std::unique_ptr<ShellOverlay> makeVoicebankBrowserOverlay() {
  return std::make_unique<VoicebankBrowserOverlay>();
}
std::unique_ptr<ShellOverlay> makeTextFieldOverlay() { return std::make_unique<TextFieldOverlay>(); }
std::unique_ptr<ShellOverlay> makeSingerMenuOverlay() {
  return std::make_unique<SingerMenuOverlay>();
}

std::vector<std::string> singerMenuItemIds() {
  std::vector<std::string> out;
  for (const auto& item : singerMenuItems())
    out.push_back(std::string{kSingerMenuPrefix} + std::string{item.key});
  return out;
}

}  // namespace seam::native_ui::design
