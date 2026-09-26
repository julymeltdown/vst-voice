#include "seam/native_ui/design/shell_overlays.hpp"

#include "seam/native_ui/diagnostic_presentation.hpp"
#include "seam/native_ui/tempo_meter_model.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
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
  return state.sampleMicroscope.has_value() && state.sampleMicroscope->detailsVisible ? "Waveform"
                                                                                     : "Details";
}

// Applies an overlay node's own activation: a controller id is dispatched by the controller, which
// is the one code path the classic painter's pointer handling used. A shell-owned id is refused
// here (the overlay names only controller nodes).
core::Result<void> activateControllerNode(NativeEditorController& controller, std::string_view id) {
  if (id.starts_with("shell."))
    return core::failure(core::ErrorCode::NotFound, "Unknown overlay control");
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
  [[nodiscard]] double scale() const noexcept { return scale_; }

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
    return unit.empty() ? std::string{"Sample"} : "Sample / " + unit;
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
  out.push_back({"microscope.close", {right - width, panel.y + 8.0, width, kHeaderButton}, "Close"});
  const auto detailsX = right - 2.0 * width - 8.0;
  out.push_back({"microscope.details",
                 {detailsX >= panel.x + kPanelInset ? detailsX : panel.x + kPanelInset,
                  detailsX >= panel.x + kPanelInset ? panel.y + 8.0 : panel.y + 38.0, width,
                  kHeaderButton},
                 detailsVisibleLabel(state)});
  if (!state.sampleMicroscope.has_value() || !state.sampleMicroscope->detailsVisible) return out;
  // The pager sits along the card's bottom, matching the classic details page buttons.
  const auto top = std::max(panel.y + 58.0, panel.bottom() - 32.0);
  out.push_back({"microscope.previous", {panel.x + kPanelInset, top, 88.0, 26.0}, "Previous",
                 SemanticRole::Button, state.sampleMicroscope->detailsPage > 0U});
  out.push_back({"microscope.next", {panel.x + kPanelInset + 96.0, top, 88.0, 26.0}, "Next",
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
  const auto caption = view.destinationContext.empty() ? std::string{"Destination unknown"}
                                                       : view.destinationContext;
  c.text({panel.x + kPanelInset, panel.y + 34.0, std::max(1.0, panel.width - 2.0 * kPanelInset - 168.0), 18.0}, caption,
         style(FontRole::Ui, t.type.smallLabel), t.color.textSecondary);
  if (const auto* close = findControl(controls, "microscope.close"))
    paintOverlayControl(c, t, *close, "Close", SemanticRole::Button, true, false, false);
  if (const auto* details = findControl(controls, "microscope.details"))
    paintOverlayControl(c, t, *details, "Details", SemanticRole::Button, true, false, false);

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
      paintOverlayControl(c, t, *previous, "Previous", SemanticRole::Button,
                          view.detailsPage > 0U, false, false);
    if (next != nullptr) {
      paintOverlayControl(c, t, *next, "Next", SemanticRole::Button,
                          view.detailsPage + 1U < view.detailsPageCount, false, false);
      c.text({next->right() + 12.0, next->y, std::max(1.0, panel.right() - next->right() - 28.0),
              next->height},
             "Page " + std::to_string(view.detailsPage + 1U) + " / " +
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
    c.text({wave.x + 8.0, wave.y + 4.0, std::max(1.0, wave.width - 16.0), 14.0}, "Waveform",
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
           "Spectrogram", plotLabel, t.color.textSecondary);
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

core::Result<void> SampleMicroscopeOverlay::perform(NativeEditorController& controller,
                                                    std::string_view id,
                                                    SemanticAction action) const {
  if (action != SemanticAction::Activate && action != SemanticAction::Toggle)
    return core::failure(core::ErrorCode::Unsupported, "This control only activates");
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
    return "Phoneme review";
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
    static constexpr std::array<const char*, 6U> kNames{"Previous edit", "Next edit", "Close",
                                                        "Previous sound", "Next sound",
                                                        "Apply binding"};
    const auto cells = grid(panel, panel.bottom() - 72.0, 28.0, 6U, 3U, 8.0);
    for (std::size_t i = 0U; i < cells.size(); ++i)
      out.push_back({kIds[i], cells[i], kNames[i], SemanticRole::Button,
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
  row(panel.y + 46.0, "Source", view.source, t.color.textPrimary);
  row(panel.y + 64.0, "Target", view.target, t.color.textPrimary);
  row(panel.y + 82.0, "Status", view.status,
      view.available ? t.color.textSecondary : t.color.warning);
  static constexpr std::array<const char*, 6U> kNames{"Previous edit", "Next edit", "Close",
                                                      "Previous sound", "Next sound",
                                                      "Apply binding"};
  for (std::size_t i = 0U; i < controls.size() && i < kNames.size(); ++i) {
    const auto enabled = i < view.enabled.size() && view.enabled[i];
    paintOverlayControl(c, t, controls[i].bounds, kNames[i], SemanticRole::Button, enabled, false,
                        false);
  }
  c.restore();
}

core::Result<void> PhonemeReviewOverlay::perform(NativeEditorController& controller,
                                                 std::string_view id, SemanticAction action) const {
  if (action != SemanticAction::Activate && action != SemanticAction::Toggle)
    return core::failure(core::ErrorCode::Unsupported, "This control only activates");
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
    if (panel.width <= 0.0 || panel.height <= 0.0) return {};
    // A popover from the transport display, centred over the body like a sheet.
    return fitPanel(panel, std::min(panel.width, 520.0), std::max(panel.height * 0.9, 260.0));
  }
  [[nodiscard]] std::string title(const NativeEditorController&,
                                  const EditorSceneState&) const override {
    return "Tempo / meter events";
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
};

std::vector<OverlayControl> TimeMapOverlay::controls(const NativeEditorController&,
                                                     const EditorSceneState& state,
                                                     const SingLayout&, ui::Rect panel) const {
  std::vector<OverlayControl> out;
  if (panel.width <= 0.0) return out;
  static constexpr std::array<const char*, 8U> kIds{
      "time-map-action.0", "time-map-action.1", "time-map-action.2", "time-map-action.3",
      "time-map-action.4", "time-map-action.5", "time-map-action.6", "time-map-action.7"};
  // A short card keeps the eight actions in two rows of four; a tall one separates the rows above
  // them. The rows come first in Tab order, as the classic panel listed them.
  constexpr double kActionHeight = 26.0;
  const auto actionsHeight = 2.0 * (kActionHeight + 8.0) - 8.0;
  const auto actionsTop = panel.bottom() - 12.0 - actionsHeight;
  constexpr double kRowHeight = 20.0;
  const auto rowsTop = panel.y + 44.0;
  const auto capacity = actionsTop > rowsTop
                            ? static_cast<std::size_t>((actionsTop - rowsTop) / kRowHeight)
                            : 0U;
  for (std::size_t i = 0U; i < state.timeMapRows.size() && i < capacity; ++i) {
    const auto selected = state.timeMapSelectedRow == i;
    out.push_back({"time-map-row." + std::to_string(i),
                   {panel.x + kPanelInset, rowsTop + static_cast<double>(i) * kRowHeight,
                    std::max(1.0, panel.width - 2.0 * kPanelInset), kRowHeight - 2.0},
                   "Event row " + std::to_string(i + 1U), SemanticRole::Button, true, selected});
  }
  static constexpr std::array<const char*, 8U> kNames{
      "Previous page", "Next page", "Edit selected event", "Remove selected event",
      "Refresh events", "Close time maps", "Add tempo", "Add meter"};
  const auto cells = grid(panel, actionsTop, kActionHeight, 8U, 4U, 6.0);
  for (std::size_t i = 0U; i < cells.size(); ++i)
    out.push_back({kIds[i], cells[i], kNames[i]});
  return out;
}

void TimeMapOverlay::paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController&,
                           const EditorSceneState& state, const SingLayout&, ui::Rect panel,
                           const std::vector<OverlayControl>& controls) const {
  c.save();
  c.clipRect(panel);
  const auto prompt =
      !state.timeMapPrompt.empty()
          ? state.timeMapPrompt
          : state.timeMapStale
                ? std::string{"Changed: refresh before editing"}
                : std::string{"Arrows select / Enter edit / Delete remove / R refresh"};
  c.text({panel.x + kPanelInset, panel.y + 42.0, std::max(1.0, panel.width - 2.0 * kPanelInset), 18.0}, prompt,
         style(FontRole::Ui, t.type.smallLabel),
         state.timeMapStale ? t.color.warning : t.color.textSecondary);
  for (std::size_t i = 0U; i < state.timeMapRows.size(); ++i) {
    const auto* bounds = findControl(controls, "time-map-row." + std::to_string(i));
    if (bounds == nullptr) continue;
    const auto selected = state.timeMapSelectedRow == i;
    if (selected) c.fill(Path::roundedRect(*bounds, 4.0), withAlpha(t.color.accent, 0.18));
    c.text({bounds->x + 6.0, bounds->y, std::max(1.0, bounds->width - 12.0), bounds->height},
           state.timeMapRows[i], style(FontRole::Mono, t.type.smallLabel),
           selected ? t.color.accent : t.color.textPrimary);
  }
  static constexpr std::array<const char*, 8U> kLabels{
      "Previous", "Next", "Edit", "Remove", "Refresh", "Close", "Add tempo", "Add meter"};
  for (std::size_t i = 0U; i < kLabels.size(); ++i) {
    const auto* bounds = findControl(controls, "time-map-action." + std::to_string(i));
    if (bounds == nullptr) continue;
    paintOverlayControl(c, t, *bounds, kLabels[i], SemanticRole::Button, true, false, false);
  }
  c.restore();
}

core::Result<void> TimeMapOverlay::perform(NativeEditorController& controller, std::string_view id,
                                           SemanticAction action) const {
  if (action != SemanticAction::Activate && action != SemanticAction::Toggle)
    return core::failure(core::ErrorCode::Unsupported, "This control only activates");
  constexpr std::string_view kRow{"time-map-row."};
  constexpr std::string_view kAction{"time-map-action."};
  if (id.starts_with(kRow)) {
    std::size_t index = 0U;
    if (!parseIndex(id.substr(kRow.size()), index)) return core::failure(
        core::ErrorCode::InvalidArgument, "Invalid time-map row");
    return controller.selectTimeMapRow(index);
  }
  if (id.starts_with(kAction)) {
    std::size_t index = 0U;
    if (!parseIndex(id.substr(kAction.size()), index) || index > 7U)
      return core::failure(core::ErrorCode::InvalidArgument, "Invalid time-map action");
    return controller.timeMapPanelAction(index);
  }
  // Close is the panel's own action 5, the same command the classic surface ran.
  return core::failure(core::ErrorCode::NotFound, "Unknown overlay control");
}

bool TimeMapOverlay::key(NativeEditorController& controller, std::string_view focusedId,
                         const KeyEvent& event) const {
  switch (event.key) {
    case NativeKey::Enter:
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
    return state.recoverySupport.mode == RecoverySupportMode::Preview ? "Support report preview"
                                                                     : "Local support reports";
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
                 "Previous vocal track"});
  out.push_back({"support.track.next", {panel.x + 106.0, panel.y + 40.0, 68.0, 24.0},
                 "Next vocal track"});
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
  const auto summary = preview ? "Candidate " + support.candidateId
                               : std::to_string(support.reportCount) + " owned report" +
                                     (support.reportCount == 1U ? "" : "s");
  c.text({panel.x + kPanelInset, panel.y + 44.0, std::max(1.0, panel.width - 2.0 * kPanelInset), 16.0}, summary,
         style(FontRole::Ui, t.type.smallLabel), t.color.textPrimary);
  if (preview) {
    const auto sha = support.archiveSha256.substr(
        0U, std::min<std::size_t>(12U, support.archiveSha256.size()));
    c.text({panel.x + kPanelInset, panel.y + 60.0, std::max(1.0, panel.width - 2.0 * kPanelInset), 14.0},
           "ZIP " + std::to_string(support.archiveBytes) + " B / SHA-256 " + sha,
           style(FontRole::Mono, t.type.rulerMicro), t.color.textSecondary);
  }
  if (const auto* previous = findControl(controls, "support.track.previous"))
    paintOverlayControl(c, t, *previous, "Prev", SemanticRole::Button, true, false, false);
  if (const auto* next = findControl(controls, "support.track.next"))
    paintOverlayControl(c, t, *next, "Next", SemanticRole::Button, true, false, false);
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
           std::to_string(item.bytes) + " B" +
               (selectable ? std::string{} : item.included ? " / included" : " / excluded"),
           style(FontRole::Mono, t.type.rulerMicro), t.color.textDisabled);
  }
  c.restore();
}

core::Result<void> RecoverySupportOverlay::perform(NativeEditorController& controller,
                                                   std::string_view id, SemanticAction action) const {
  if (action != SemanticAction::Activate && action != SemanticAction::Toggle)
    return core::failure(core::ErrorCode::Unsupported, "This control only activates");
  constexpr std::string_view kItem{"support.item."};
  if (id.starts_with(kItem)) {
    std::size_t index = 0U;
    if (!parseIndex(id.substr(kItem.size()), index))
      return core::failure(core::ErrorCode::InvalidArgument, "Invalid support item");
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
        slot.height, 26.0 + static_cast<double>(state.overlapDetail->members.size()) * 20.0 + 8.0);
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
    return std::to_string(count) + " overlapping notes";
  }
  [[nodiscard]] std::vector<OverlayControl> controls(const NativeEditorController&,
                                                     const EditorSceneState& state,
                                                     const SingLayout&, ui::Rect panel) const override {
    std::vector<OverlayControl> out;
    if (!state.overlapDetail.has_value()) return out;
    for (std::size_t i = 0U; i < state.overlapDetail->members.size(); ++i) {
      const auto& member = state.overlapDetail->members[i];
      const auto lyric = member.lyric.empty() ? std::string{"(no lyric)"} : member.lyric;
      out.push_back({"overlap-note-row." + std::to_string(i),
                     {panel.x + 12.0, panel.y + 26.0 + static_cast<double>(i) * 20.0,
                      std::max(1.0, panel.width - 24.0), 18.0},
                     "Overlap note " + std::to_string(i + 1U) + ": " + lyric + " / MIDI " +
                         std::to_string(member.midiKey),
                     SemanticRole::Button, true, member.selected});
      if (panel.y + 26.0 + static_cast<double>(i + 1U) * 20.0 > panel.bottom() - 4.0) break;
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
    const auto lyric = member.lyric.empty() ? std::string{"(no lyric)"} : member.lyric;
    c.text({bounds->x + 6.0, bounds->y, std::max(1.0, bounds->width - 66.0), bounds->height}, lyric,
           style(FontRole::Ui, t.type.smallLabel),
           member.selected ? t.color.accent : t.color.textPrimary);
    c.text({bounds->right() - 60.0, bounds->y, 54.0, bounds->height},
           "MIDI " + std::to_string(member.midiKey),
           style(FontRole::Mono, t.type.rulerMicro, 0.0, TextAlign::Right),
           t.color.textSecondary);
  }
  c.restore();
}

core::Result<void> OverlapDetailOverlay::perform(NativeEditorController& controller,
                                                 std::string_view id, SemanticAction action) const {
  if (action != SemanticAction::Activate && action != SemanticAction::Toggle)
    return core::failure(core::ErrorCode::Unsupported, "This control only activates");
  constexpr std::string_view kRow{"overlap-note-row."};
  if (!id.starts_with(kRow)) return core::failure(core::ErrorCode::NotFound, "Unknown control");
  std::size_t index = 0U;
  if (!parseIndex(id.substr(kRow.size()), index))
    return core::failure(core::ErrorCode::InvalidArgument, "Invalid overlap row");
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
  [[nodiscard]] ui::Rect panel(const NativeEditorController&, const EditorSceneState&,
                               const SingLayout& layout, ui::Rect slot) const override {
    if (slot.width <= 0.0 || slot.height <= 0.0) return {};
    // A popover above the status bar, its right edge under the header's settings control.
    const auto width = std::min(slot.width, std::min(460.0, slot.width));
    const auto height = std::min(slot.height, std::min(180.0, slot.height));
    if (width < kMinimumPanelWidth || height < kMinimumPanelHeight) return {};
    const auto right = std::max(slot.x, std::min(layout.settings.right(), slot.right()));
    return {std::max(slot.x, right - width), std::max(slot.y, slot.bottom() - height - 2.0), width,
            height};
  }
  [[nodiscard]] std::string title(const NativeEditorController&,
                                  const EditorSceneState&) const override {
    return "Diagnostics";
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
};

std::vector<OverlayControl> DiagnosticsOverlay::controls(const NativeEditorController&,
                                                         const EditorSceneState& state,
                                                         const SingLayout&, ui::Rect panel) const {
  std::vector<OverlayControl> out;
  if (panel.width <= 0.0 || state.diagnostics.empty()) return out;
  // Every active diagnostic gets its own row and the recovery actions the controller registered for
  // it, so no issue the classic strip listed becomes unreachable here. Rows fill the card's body;
  // the first diagnostic's actions sit along the bottom, in the order the strip showed them.
  constexpr double kRowHeight = 26.0;
  constexpr double kActionHeight = 28.0;
  const auto actionsTop = panel.bottom() - kActionHeight - 10.0;
  const auto rowsTop = panel.y + 56.0;
  for (std::size_t i = 0U; i < state.diagnostics.size(); ++i) {
    const auto y = rowsTop + static_cast<double>(i) * kRowHeight;
    if (y + kRowHeight - 4.0 > actionsTop - 4.0) break;
    const auto& diagnostic = state.diagnostics[i];
    const auto presentation = presentDiagnostic(diagnostic);
    out.push_back({"diagnostic." + std::to_string(i) + "." + diagnostic.code,
                   {panel.x + kPanelInset, y, std::max(1.0, panel.width - 2.0 * kPanelInset),
                    kRowHeight - 4.0},
                   presentation.title, SemanticRole::Status, true, false, false});
  }
  const auto presentation = presentDiagnostic(state.diagnostics.front());
  const auto count = std::min<std::size_t>(2U, presentation.primaryActionKinds.size());
  if (count == 0U) return out;
  const auto cells = grid(panel, actionsTop, kActionHeight, count, count, 8.0);
  for (std::size_t i = 0U; i < cells.size(); ++i)
    out.push_back({std::string{"diagnostic-action.0."} +
                       std::string{authoring::toString(presentation.primaryActionKinds[i])},
                   cells[i], diagnosticActionLabel(presentation.primaryActionKinds[i])});
  return out;
}

void DiagnosticsOverlay::paint(Canvas2D& c, const DesignTokens& t, const NativeEditorController&,
                               const EditorSceneState& state, const SingLayout&, ui::Rect panel,
                               const std::vector<OverlayControl>& controls) const {
  if (state.diagnostics.empty()) return;
  c.save();
  c.clipRect(panel);
  const auto& diagnostic = state.diagnostics.front();
  const auto presentation = presentDiagnostic(diagnostic);
  const auto color = diagnostic.severity == authoring::DiagnosticSeverity::Critical
                         ? t.color.error
                         : diagnostic.severity == authoring::DiagnosticSeverity::Warning
                               ? t.color.warning
                               : t.color.info;
  auto title = presentation.title;
  if (state.diagnostics.size() > 1U)
    title += " +" + std::to_string(state.diagnostics.size() - 1U) + " more";
  c.fill(Path::capsule({panel.x + 24.0, panel.y + 16.0, 5.0, 18.0}), color);
  c.text({panel.x + kPanelInset + 6.0, panel.y + 14.0, std::max(1.0, panel.width - 2.0 * kPanelInset - 6.0), 20.0}, title,
         style(FontRole::UiSemibold, t.type.label), t.color.textPrimary);
  c.text({panel.x + kPanelInset + 6.0, panel.y + 36.0, std::max(1.0, panel.width - 2.0 * kPanelInset - 6.0), 16.0},
         presentation.impact, style(FontRole::Ui, t.type.smallLabel), t.color.textSecondary);
  c.text({panel.x + kPanelInset + 6.0, panel.y + 54.0, std::max(1.0, panel.width - 2.0 * kPanelInset - 6.0), 30.0},
         presentation.technicalDetail, style(FontRole::Mono, t.type.rulerMicro),
         t.color.textDisabled);
  for (const auto& control : controls) {
    std::string label;
    for (const auto action : presentation.primaryActionKinds)
      if (control.id.ends_with(std::string{authoring::toString(action)}))
        label = diagnosticActionLabel(action);
    paintOverlayControl(c, t, control.bounds, label, SemanticRole::Button, true, false, false);
  }
  c.restore();
}

core::Result<void> DiagnosticsOverlay::perform(NativeEditorController& controller,
                                               std::string_view id, SemanticAction action) const {
  if (action != SemanticAction::Activate && action != SemanticAction::Toggle)
    return core::failure(core::ErrorCode::Unsupported, "This control only activates");
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

}  // namespace seam::native_ui::design
