#include "test_framework.hpp"

#include "seam/native_ui/design/design_tokens.hpp"
#include "seam/native_ui/design/sing_layout.hpp"
#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"

#include <array>
#include <cmath>
#include <string_view>

#if defined(__APPLE__)
#include "seam/native_ui/paint/presentation_color.hpp"

#include <CoreGraphics/CoreGraphics.h>

#include <cstdint>
#endif

namespace {

using seam::native_ui::Color;
using seam::native_ui::PixelSurface;
using namespace seam::native_ui::design;

struct ContractRegion final {
  std::string_view id;
  std::array<double, 4U> rect;
};

// Mirrors docs/design/ui-fidelity-contract-v1.json "canonical.regions"; the contract file itself is
// checked by scripts/verify_ui_fidelity_contract.py and tests/design/test_ui_fidelity_contract.py.
constexpr std::array<ContractRegion, 22U> kContract{{
    {"header", {16, 16, 1568, 80}},        {"wordmark", {32, 32, 208, 48}},
    {"workspaceTabs", {264, 24, 400, 64}}, {"modeSwitch", {720, 42, 132, 28}},
    {"transport", {880, 34, 432, 44}},     {"outputMeter", {1336, 34, 160, 44}},
    {"settings", {1528, 40, 32, 32}},      {"editor", {16, 108, 1112, 576}},
    {"tools", {24, 116, 1096, 28}},        {"ruler", {80, 148, 1040, 24}},
    {"keyboard", {24, 172, 56, 504}},      {"grid", {80, 172, 1040, 504}},
    {"lane", {16, 696, 1112, 148}},        {"laneTabs", {24, 704, 1096, 28}},
    {"lanePlot", {56, 740, 1064, 96}},     {"laneTimePlot", {80, 740, 1040, 96}},
    {"rack", {1144, 108, 440, 736}},       {"singer", {1144, 108, 440, 360}},
    {"portraitRing", {1220, 148, 288, 288}}, {"expression", {1144, 480, 440, 248}},
    {"style", {1144, 740, 440, 104}},      {"status", {16, 856, 1568, 28}},
}};

seam::ui::Rect regionOf(const SingLayout& l, std::string_view id) {
  if (id == "header") return l.header;
  if (id == "wordmark") return l.wordmark;
  if (id == "workspaceTabs") return l.workspaceTabs;
  if (id == "modeSwitch") return l.modeSwitch;
  if (id == "transport") return l.transport;
  if (id == "outputMeter") return l.outputMeter;
  if (id == "settings") return l.settings;
  if (id == "editor") return l.editor;
  if (id == "tools") return l.tools;
  if (id == "ruler") return l.ruler;
  if (id == "keyboard") return l.keyboard;
  if (id == "grid") return l.grid;
  if (id == "lane") return l.lane;
  if (id == "laneTabs") return l.laneTabs;
  if (id == "lanePlot") return l.lanePlot;
  if (id == "laneTimePlot") return l.laneTimePlot;
  if (id == "rack") return l.rackArea;
  if (id == "singer") return l.singer;
  if (id == "portraitRing") return l.portraitRing;
  if (id == "expression") return l.expression;
  if (id == "style") return l.style;
  return l.status;
}

bool near(double a, double b) { return std::abs(a - b) <= 2.0; }

}  // namespace

TEST_CASE("SING layout reproduces every canonical contract region at 1600x900") {
  const auto layout = solveSingLayout(1600.0, 900.0);
  CHECK(layout.rack == RackPresentation::Full);
  for (const auto& region : kContract) {
    const auto rect = regionOf(layout, region.id);
    CHECK(near(rect.x, region.rect[0]));
    CHECK(near(rect.y, region.rect[1]));
    CHECK(near(rect.width, region.rect[2]));
    CHECK(near(rect.height, region.rect[3]));
  }
  // Ruler, grid and lane share one time axis.
  CHECK(layout.ruler.x == layout.grid.x && layout.grid.x == layout.laneTimePlot.x);
  CHECK(layout.ruler.width == layout.grid.width && layout.grid.width == layout.laneTimePlot.width);
}

TEST_CASE("SING layout keeps the timeline usable and the singer present when narrow") {
  for (const auto size : std::array<std::array<double, 2U>, 4U>{
           {{1280.0, 800.0}, {1100.0, 720.0}, {1000.0, 700.0}, {800.0, 600.0}}}) {
    const auto layout = solveSingLayout(size[0], size[1]);
    CHECK(layout.grid.width >= 480.0 || size[0] < 800.0 + 1.0);
    CHECK(layout.grid.height > 120.0);
    CHECK(layout.portraitRing.width > 0.0);
    // The singer is always reachable: in the full rack by its card, compactly by the inspector
    // button (whose inspector carries Change voice).
    if (layout.rack == RackPresentation::Full) CHECK(layout.singerChange.width > 0.0);
    else CHECK(layout.inspectorButton.width == 44.0 && layout.singerChange.width == 0.0);
    CHECK(layout.rackArea.right() <= size[0]);
    CHECK(layout.transport.x > layout.modeSwitch.right());
    if (size[0] < 860.0) CHECK(layout.rack == RackPresentation::Drawer);
    else if (size[0] < 1100.0) CHECK(layout.rack == RackPresentation::Rail);
  }
}

TEST_CASE("compact widths follow section 3.4: a 44-point drawer below 860, a 56-point rail below 1100") {
  const auto drawer = solveSingLayout(720.0, 480.0);
  CHECK(drawer.rack == RackPresentation::Drawer);
  CHECK(drawer.rackArea.width == 44.0);
  CHECK(drawer.inspectorButton.x == drawer.rackArea.x && drawer.inspectorButton.width == 44.0 &&
        drawer.inspectorButton.height == 44.0);
  CHECK(!drawer.inspectorOpen && drawer.inspector.width == 0.0);
  const auto rail = solveSingLayout(1000.0, 700.0);
  CHECK(rail.rack == RackPresentation::Rail);
  CHECK(rail.rackArea.width == 56.0);
  CHECK(singRackWidth(859.0) == 44.0 && singRackWidth(860.0) == 56.0 && singRackWidth(1100.0) == 320.0);
  // The full rack never has an inspector, even when asked.
  const auto full = solveSingLayout(1600.0, 900.0, true);
  CHECK(full.rack == RackPresentation::Full && !full.inspectorOpen && full.inspector.width == 0.0);
}

TEST_CASE("compact headers keep a reachable workspace menu and never collide with transport") {
  for (const auto width : {480.0, 600.0, 720.0, 860.0, 1000.0}) {
    const auto l = solveSingLayout(width, 480.0);
    if (l.workspaceTabs.width > 0.0) continue;
    CHECK(l.workspaceMenuButton.width >= 24.0);
    CHECK(l.workspaceMenuButton.x >= l.header.x);
    CHECK(l.workspaceMenuButton.right() + 8.0 <=
          (l.modeSwitch.width > 0.0 ? l.modeSwitch.x : l.transport.x));
    CHECK(l.workspaceMenu.bottom() <= l.status.y);
    for (const auto row : l.workspaceMenuRow)
      CHECK(row.x >= l.workspaceMenu.x && row.right() <= l.workspaceMenu.right() &&
            row.y >= l.workspaceMenu.y && row.bottom() <= l.workspaceMenu.bottom());
    if (width < 720.0) {
      CHECK(l.wordmark.width == 0.0 && l.modeSwitch.width == 0.0);
      for (const auto row : l.modeMenuRow) CHECK(row.width > 0.0);
    }
  }
}

TEST_CASE("the open inspector holds the singer, all six knobs and the style inside the client") {
  const auto inside = [](seam::ui::Rect inner, seam::ui::Rect outer) {
    return inner.width > 0.0 && inner.height > 0.0 && inner.x >= outer.x - 0.5 &&
           inner.y >= outer.y - 0.5 && inner.right() <= outer.right() + 0.5 &&
           inner.bottom() <= outer.bottom() + 0.5;
  };
  const auto apart = [](seam::ui::Rect a, seam::ui::Rect b) {
    return a.right() <= b.x + 0.5 || b.right() <= a.x + 0.5 || a.bottom() <= b.y + 0.5 ||
           b.bottom() <= a.y + 0.5;
  };
  for (const auto size : std::array<std::array<double, 2U>, 6U>{
           {{480.0, 320.0}, {720.0, 480.0}, {800.0, 600.0}, {860.0, 640.0}, {1000.0, 700.0},
            {1280.0, 480.0}}}) {
    const auto l = solveSingLayout(size[0], size[1], true);
    CHECK(l.rack != RackPresentation::Full);
    CHECK(l.inspectorOpen);
    const seam::ui::Rect client{0.0, 0.0, size[0], size[1]};
    CHECK(inside(l.inspector, client));
    CHECK(l.inspector.bottom() <= l.status.y + 0.5);    // never over the status bar
    CHECK(apart(l.inspector, l.rackArea));               // the button stays visible beside it
    for (const auto part : {l.singer, l.portraitRing, l.singerChange, l.style, l.expression})
      CHECK(inside(part, l.inspector));
    CHECK(inside(l.portraitRing, l.singer) && inside(l.singerChange, l.singer));
    CHECK(apart(l.portraitRing, l.singerChange) && apart(l.style, l.singerChange));
    for (std::size_t i = 0U; i < l.knob.size(); ++i) {
      CHECK(inside(l.knob[i], l.expression));
      CHECK(l.knob[i].height >= 90.0 && l.knob[i].width >= 52.0);  // label, 52-pt dial, caption
      for (std::size_t j = i + 1U; j < l.knob.size(); ++j) CHECK(apart(l.knob[i], l.knob[j]));
    }
    // Two rows where the body allows, one row of six below that; the 720x480 minimum keeps two.
    if (size[0] == 720.0) CHECK(!l.knobsInOneRow);
    if (size[1] == 320.0) CHECK(l.knobsInOneRow);
  }
}

TEST_CASE("short and narrow SING layouts keep every control inside its parent without overlap") {
  const auto inside = [](seam::ui::Rect inner, seam::ui::Rect outer) {
    return inner.x >= outer.x - 0.5 && inner.y >= outer.y - 0.5 &&
           inner.right() <= outer.right() + 0.5 && inner.bottom() <= outer.bottom() + 0.5;
  };
  const auto apart = [](seam::ui::Rect a, seam::ui::Rect b) {
    return a.width <= 0.0 || b.width <= 0.0 || a.right() <= b.x + 0.5 || b.right() <= a.x + 0.5 ||
           a.bottom() <= b.y + 0.5 || b.bottom() <= a.y + 0.5;
  };
  for (const auto size : std::array<std::array<double, 2U>, 7U>{{{720.0, 480.0},
                                                                  {1280.0, 480.0},
                                                                  {1280.0, 700.0},
                                                                  {1000.0, 700.0},
                                                                  {900.0, 600.0},
                                                                  {1280.0, 800.0},
                                                                  {1600.0, 900.0}}}) {
    const auto l = solveSingLayout(size[0], size[1]);
    const seam::ui::Rect client{0.0, 0.0, size[0], size[1]};
    // Header children stay inside the header and never overlap one another.
    const std::array<seam::ui::Rect, 6U> header{l.wordmark, l.workspaceTabs, l.modeSwitch,
                                                l.transport, l.outputMeter, l.settings};
    for (std::size_t i = 0U; i < header.size(); ++i) {
      if (header[i].width > 0.0) CHECK(inside(header[i], l.header));
      for (std::size_t j = i + 1U; j < header.size(); ++j) CHECK(apart(header[i], header[j]));
    }
    // Body regions stay in the client, above the status bar, and apart.
    for (const auto r : {l.editor, l.lane, l.rackArea, l.status}) CHECK(inside(r, client));
    for (const auto r : {l.editor, l.lane, l.rackArea}) CHECK(r.bottom() <= l.status.y + 0.5);
    CHECK(apart(l.editor, l.lane));
    CHECK(apart(l.editor, l.rackArea));
    CHECK(apart(l.lane, l.rackArea));
    CHECK(l.grid.height > 60.0);
    CHECK(l.laneTimePlot.height > 20.0);
    // Every visible rack card and knob is inside the rack and reachable.
    CHECK(inside(l.portraitRing, l.rackArea));
    if (l.rack == RackPresentation::Full) {
      CHECK(inside(l.singerChange, l.rackArea));
      for (const auto card : {l.singer, l.expression, l.style}) CHECK(inside(card, l.rackArea));
      for (const auto knob : l.knob) CHECK(inside(knob, l.expression));
    } else {
      CHECK(inside(l.inspectorButton, l.rackArea));
    }
    // Transport parts are inside the transport.
    for (const auto part : {l.playButton, l.positionReadout, l.tempoReadout, l.meterReadout})
      CHECK(inside(part, l.transport));
  }
}

TEST_CASE("EMO and SCENE text roles meet contrast floors in both contrast settings") {
  for (const auto mode : {DesignMode::Emo, DesignMode::Scene}) {
    for (const auto contrast : {Contrast::Standard, Contrast::High}) {
      const auto& t = tokensFor(mode, contrast);
      CHECK(contrastRatio(t.color.textPrimary, t.color.canvas) >= 7.0);
      CHECK(contrastRatio(t.color.textPrimary, t.color.surface) >= 7.0);
      CHECK(contrastRatio(t.color.textSecondary, t.color.surface) >= 4.5);
      CHECK(t.type.label >= 10.0);
      CHECK(t.type.smallLabel >= 10.0);
    }
  }
}

TEST_CASE("the design shell is inactive until a production host activates it") {
  SingShell shell;
  CHECK(!shell.active());
  CHECK(!shell.enabled());
  CHECK(!shell.presentedLastFrame());
  shell.activate({}, DesignPreferences{.mode = DesignMode::Scene});
  CHECK(shell.active());
  CHECK(shell.mode() == DesignMode::Scene);
  CHECK(shell.enabled() == seam::native_ui::paint::vectorBackendAvailable());
}

TEST_CASE("vector canvas paints top-down in logical points at device scale") {
  if (!seam::native_ui::paint::vectorBackendAvailable()) return;
  PixelSurface surface{200U, 100U};
  surface.clear(Color{0, 0, 0, 255});
  auto canvas = seam::native_ui::paint::makeCanvas(surface, 2.0);
  CHECK(canvas != nullptr);
  if (!canvas) return;
  // A 10-point band at the top of a 100x50-point canvas covers the top 20 device rows.
  canvas->fill(seam::native_ui::paint::Path::rect({0.0, 0.0, 100.0, 10.0}), Color{255, 0, 0, 255});
  canvas->flush();
  const auto at = [&](std::uint32_t x, std::uint32_t y) {
    return surface.pixels()[static_cast<std::size_t>(y) * surface.width() + x];
  };
  CHECK(((at(100U, 5U) >> 16U) & 0xFFU) > 200U);
  CHECK(((at(100U, 18U) >> 16U) & 0xFFU) > 200U);
  CHECK(((at(100U, 30U) >> 16U) & 0xFFU) < 20U);
  CHECK(((at(100U, 95U) >> 16U) & 0xFFU) < 20U);
}

#if defined(__APPLE__)
TEST_CASE("frames are presented as sRGB so a wide-gamut display colour-matches them") {
  const auto space = seam::native_ui::paint::presentationColorSpace();
  CHECK(space != nullptr);
  const CFStringRef name = CGColorSpaceCopyName(space);
  const auto isSrgb = name != nullptr && CFStringCompare(name, kCGColorSpaceSRGB, 0) == kCFCompareEqualTo;
  if (name != nullptr) CFRelease(name);
  CHECK(isSrgb);

  // Present one pixel of pure sRGB red, exactly as the windows do, into a Display P3 bitmap.
  // Colour-matched, it lands inside P3's red (about 234, 51, 35); presented unmanaged it would stay
  // at P3's full, more saturated red (255, 0, 0).
  const auto info = static_cast<CGBitmapInfo>(
      static_cast<std::uint32_t>(kCGImageAlphaPremultipliedFirst) |
      static_cast<std::uint32_t>(kCGBitmapByteOrder32Little));
  std::array<std::uint8_t, 4U> red{0U, 0U, 255U, 255U};  // B, G, R, A in memory
  CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr, red.data(), red.size(), nullptr);
  CGImageRef image = CGImageCreate(1U, 1U, 8U, 32U, 4U, space, info, provider, nullptr, false,
                                   kCGRenderingIntentDefault);
  CGColorSpaceRef p3 = CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3);
  std::array<std::uint8_t, 4U> shown{};
  CGContextRef context = CGBitmapContextCreate(shown.data(), 1U, 1U, 8U, 4U, p3, info);
  CHECK(image != nullptr && context != nullptr);
  if (image != nullptr && context != nullptr) CGContextDrawImage(context, CGRectMake(0, 0, 1, 1), image);
  if (context != nullptr) CGContextRelease(context);
  if (image != nullptr) CGImageRelease(image);
  CGColorSpaceRelease(p3);
  CGDataProviderRelease(provider);
  CHECK(shown[2] >= 220U && shown[2] <= 245U);  // red
  CHECK(shown[1] >= 30U && shown[1] <= 70U);    // green
}
#endif
