// Bundled UI fonts (assets/fonts) and the kit tooltip's timer, placement and paint.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/native_ui/design/design_tokens.hpp"
#include "seam/native_ui/design/tooltip.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

namespace {

using namespace seam::native_ui::design;
using seam::native_ui::Color;
using seam::native_ui::PixelSurface;
using seam::native_ui::paint::FontRole;
using seam::ui::Rect;
using namespace std::chrono_literals;

bool systemFacesOnly() {
  const char* value = std::getenv("SEAM_UI_FONTS");
  return value != nullptr && std::string_view{value} == "system";
}

bool overlaps(Rect a, Rect b) {
  return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

bool inside(Rect box, Rect window, double margin) {
  return box.x >= window.x + margin - 0.01 && box.y >= window.y + margin - 0.01 &&
         box.right() <= window.right() - margin + 0.01 &&
         box.bottom() <= window.bottom() - margin + 0.01;
}

// Measures with the real backend, so wrapping sees the faces the shell draws with.
struct Metrics final {
  PixelSurface surface{1U, 1U};
  std::unique_ptr<seam::native_ui::paint::Canvas2D> canvas{
      seam::native_ui::paint::makeCanvas(surface, 1.0)};
  TooltipMeasure measure() {
    return [this](std::string_view text, const seam::native_ui::paint::TextStyle& style) {
      return canvas != nullptr ? canvas->measure(text, style)
                               : 7.0 * static_cast<double>(text.size());
    };
  }
};

TooltipSubject subject(std::string id, std::string text = "Explains the control") {
  return TooltipSubject{std::move(id), Rect{100.0, 100.0, 24.0, 24.0}, std::move(text)};
}

}  // namespace

TEST_CASE("bundled fonts: every role draws with its pinned OFL face, or the system face when told") {
  if (!seam::native_ui::paint::vectorBackendAvailable()) return;
  const auto& fonts = seam::native_ui::paint::bundledFonts();
  static constexpr std::array<std::pair<FontRole, std::string_view>, 7U> kFaces{{
      {FontRole::Ui, "Barlow-Regular"},
      {FontRole::UiMedium, "Barlow-Medium"},
      {FontRole::UiSemibold, "Barlow-SemiBold"},
      {FontRole::UiBold, "Barlow-Bold"},
      {FontRole::Mono, "DMMono-Medium"},
      {FontRole::Display, "BarlowCondensed-SemiBold"},
      {FontRole::DisplayRounded, "Fredoka"},
  }};
  if (systemFacesOnly()) {
    // SEAM_UI_FONTS=system: nothing is registered and every role still has a face.
    CHECK(fonts.directory.empty());
    for (const auto& [role, face] : kFaces) {
      const auto name = seam::native_ui::paint::fontFaceName(role);
      CHECK(!name.empty());
      CHECK(!name.starts_with(face));
    }
    return;
  }
  CHECK(!fonts.directory.empty());
  CHECK(fonts.refused.empty());
  for (const auto& [role, face] : kFaces) {
    CHECK(fonts.face[static_cast<std::size_t>(role)].starts_with(face));
    CHECK(seam::native_ui::paint::fontFaceName(role).starts_with(face));
  }
  // The looks point their headings and display text at their own faces.
  CHECK(tokensFor(DesignMode::Emo).type.heading == FontRole::Display);
  CHECK(tokensFor(DesignMode::Scene).type.heading == FontRole::DisplayRounded);
  CHECK(tokensFor(DesignMode::Scene, Contrast::High).type.display == FontRole::DisplayRounded);
}

TEST_CASE("bundled fonts: a face that differs from its manifest or leaves the directory is refused") {
  if (!seam::native_ui::paint::vectorBackendAvailable()) return;
  const auto dir = seam::test::support::temporaryDirectory("bundled-fonts");
  std::filesystem::create_directories(dir / "barlow");
  {
    std::ofstream face(dir / "barlow" / "Barlow-Regular.ttf", std::ios::binary);
    face << "not a font";
  }
  {
    std::ofstream manifest(dir / "manifest.json");
    manifest << R"({"schemaVersion": 1, "faces": [
      {"file": "barlow/Barlow-Regular.ttf", "postScriptName": "Barlow-Regular", "roles": ["Ui"],
       "sha256": "95aa02c7c43096e0dd44d787ba6216864a67157e402adab59b35572e0c1577ea"},
      {"file": "../outside.ttf", "postScriptName": "Outside", "roles": ["Mono"], "sha256": "00"}]})";
  }
  const auto result = seam::native_ui::paint::registerBundledFonts(dir);
  CHECK(result.refused.size() == 2U);
  CHECK(result.face[static_cast<std::size_t>(FontRole::Ui)].empty());
  CHECK(result.face[static_cast<std::size_t>(FontRole::Mono)].empty());
  const auto missing = seam::native_ui::paint::registerBundledFonts(dir / "none");
  CHECK(missing.refused.size() == 1U);
  std::filesystem::remove_all(dir);
}

TEST_CASE("tooltip timer: the delay, refresh without restart, handoff and dismissal") {
  const auto t0 = TooltipTimer::Clock::time_point{} + 1h;
  TooltipTimer timer;
  timer.hover(subject("a"), t0);
  CHECK(timer.shown(t0 + kTooltipDelay - 1ms) == nullptr);
  CHECK(timer.showsAt() == t0 + kTooltipDelay);
  // The same target again (the layout moved it) keeps the running delay and takes the new rect.
  auto moved = subject("a");
  moved.target.x = 140.0;
  timer.hover(moved, t0 + 300ms);
  const auto* shown = timer.shown(t0 + kTooltipDelay);
  CHECK(shown != nullptr && shown->id == "a" && shown->target.x == 140.0);
  // Straight from a shown tip to another target: the next one shows at once.
  timer.hover(subject("b"), t0 + 700ms);
  CHECK(timer.shown(t0 + 700ms) != nullptr && timer.shown(t0 + 700ms)->id == "b");
  // Leaving for longer than the handoff window brings the delay back.
  timer.hover(std::nullopt, t0 + 800ms);
  CHECK(timer.shown(t0 + 800ms) == nullptr);
  timer.hover(subject("c"), t0 + 800ms + kTooltipHandoff + 1ms);
  CHECK(timer.shown(t0 + 900ms + kTooltipHandoff) == nullptr);
  const auto cShows = t0 + 800ms + kTooltipHandoff + 1ms + kTooltipDelay;
  CHECK(timer.shown(cShows) != nullptr);
  // Dismissed (Escape, a press): hidden while the target stays, and no handoff to the next.
  timer.dismiss();
  CHECK(timer.shown(cShows + 1s) == nullptr);
  CHECK(!timer.showsAt().has_value());
  timer.hover(subject("c"), cShows + 1s);
  CHECK(timer.shown(cShows + 2s) == nullptr);
  timer.hover(subject("d"), cShows + 2s);
  CHECK(timer.shown(cShows + 2s) == nullptr);
  CHECK(timer.shown(cShows + 2s + kTooltipDelay) != nullptr);
  // Nothing to say, nothing shown.
  TooltipTimer silent;
  silent.hover(subject("e", ""), t0);
  CHECK(silent.shown(t0 + 1h) == nullptr);
}

TEST_CASE("tooltip timer: keyboard focus shows its own tip until the pointer picks another target") {
  const auto t0 = TooltipTimer::Clock::time_point{} + 1h;
  TooltipTimer timer;
  timer.hover(subject("pointer"), t0);
  timer.focus(subject("focus"), t0 + 100ms);
  CHECK(timer.fromKeyboard());
  CHECK(timer.shown(t0 + 100ms + kTooltipDelay) != nullptr);
  CHECK(timer.shown(t0 + 100ms + kTooltipDelay)->id == "focus");
  // The pointer leaving its target does not take the keyboard's tip away.
  timer.hover(std::nullopt, t0 + 1s);
  CHECK(timer.shown(t0 + 1s)->id == "focus");
  // A new pointer target does.
  timer.hover(subject("pointer2"), t0 + 2s);
  CHECK(!timer.fromKeyboard());
  CHECK(timer.shown(t0 + 2s + kTooltipDelay)->id == "pointer2");
}

TEST_CASE("tooltip placement stays inside the window and clear of its target at every size") {
  Metrics metrics;
  const auto measure = metrics.measure();
  const auto& t = tokensFor(DesignMode::Emo);
  const std::string shortText = "Output device and playback settings";
  const std::string longText =
      "The playhead is outside the selected region, so the expression channels cannot be edited "
      "here; move the playhead into the region or select the region that holds it, then try the "
      "knob again. Supercalifragilisticexpialidociousnesswithoutanyspacesatall stays whole.";
  for (const auto size : std::array<std::array<double, 2U>, 8U>{{{480.0, 320.0},
                                                                  {720.0, 480.0},
                                                                  {1024.0, 640.0},
                                                                  {1280.0, 800.0},
                                                                  {1600.0, 900.0},
                                                                  {1920.0, 1080.0},
                                                                  {2560.0, 1440.0},
                                                                  {3840.0, 2160.0}}}) {
    const Rect window{0.0, 0.0, size[0], size[1]};
    std::vector<Rect> targets;
    for (const auto fx : {0.0, 0.25, 0.5, 0.75, 1.0})
      for (const auto fy : {0.0, 0.3, 0.5, 0.7, 1.0})
        targets.push_back({(size[0] - 32.0) * fx, (size[1] - 32.0) * fy, 32.0, 32.0});
    targets.push_back({0.0, 0.0, size[0], 44.0});                   // a full-width header strip
    targets.push_back({0.0, size[1] - 28.0, size[0], 28.0});        // the status bar
    targets.push_back({size[0] - 60.0, 0.0, 60.0, size[1]});        // a tall right rail
    for (const auto& target : targets) {
      for (const auto* text : {&shortText, &longText}) {
        const auto layout = layoutTooltip(*text, target, window, t, measure);
        CHECK(!layout.empty());
        if (layout.empty()) continue;
        CHECK(inside(layout.box, window, kTooltipMargin));
        CHECK(!overlaps(layout.box, target));
        CHECK(layout.lines.size() <= kTooltipMaxLines);
        CHECK(layout.box.width <= kTooltipMaxWidth + 0.01);
      }
    }
  }
  // Long words break at characters and a short text stays on one line.
  const auto lines = wrapTooltipText(longText, 200.0, tooltipTextStyle(t), measure, 20U);
  CHECK(lines.size() > 3U);
  for (const auto& line : lines) CHECK(measure(line, tooltipTextStyle(t)) <= 200.0 + 0.01);
  CHECK(wrapTooltipText(shortText, 300.0, tooltipTextStyle(t), measure).size() == 1U);
  // Hangul wraps at characters without splitting a UTF-8 sequence.
  for (const auto& line : wrapTooltipText("\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80", 12.0,
                                          tooltipTextStyle(t), measure))
    CHECK(line.size() % 3U == 0U);
}

TEST_CASE("tooltip paint: standard glow, and High Contrast opaque with no glow") {
  if (!seam::native_ui::paint::vectorBackendAvailable()) return;
  Metrics metrics;
  for (const auto contrast : {Contrast::Standard, Contrast::High}) {
    const auto& t = tokensFor(DesignMode::Scene, contrast);
    PixelSurface surface{400U, 200U};
    surface.clear(t.color.canvas);
    auto canvas = seam::native_ui::paint::makeCanvas(surface, 1.0);
    if (canvas == nullptr) return;
    const auto layout = layoutTooltip("Switch the workspace or the look", {40.0, 20.0, 32.0, 32.0},
                                      {0.0, 0.0, 400.0, 200.0}, t, metrics.measure());
    CHECK(!layout.empty());
    paintTooltip(*canvas, t, layout);
    canvas->flush();
    // Four points below the card: glow reaches it in Standard, nothing does in High Contrast.
    const auto x = static_cast<std::uint32_t>(layout.box.x + layout.box.width * 0.5);
    const auto y = static_cast<std::uint32_t>(layout.box.bottom() + 4.0);
    const auto below = surface.pixels()[static_cast<std::size_t>(y) * surface.width() + x];
    const auto same = below == t.color.canvas.bgra();
    CHECK(contrast == Contrast::High ? same : !same);
  }
}
