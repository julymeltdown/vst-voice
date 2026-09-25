#include "test_framework.hpp"

#include "seam/native_ui/design/design_tokens.hpp"
#include "seam/native_ui/design/sing_layout.hpp"
#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"

#include <array>
#include <cmath>
#include <string_view>

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
    CHECK(layout.singerChange.width > 0.0);
    CHECK(layout.rackArea.right() <= size[0]);
    CHECK(layout.transport.x > layout.modeSwitch.right());
    if (size[0] < 1100.0) CHECK(layout.rack == RackPresentation::Rail);
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
