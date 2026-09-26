#include "seam/native_ui/design/sing_layout.hpp"

#include <algorithm>
#include <cmath>

namespace seam::native_ui::design {

double singRackWidth(double width) noexcept {
  if (!(width >= kSingDrawerWidth)) return 44.0;
  if (!(width >= kSingRailWidth)) return 56.0;
  return std::clamp(width * 0.275, 320.0, 440.0);
}

namespace {

// The singer inspector of the compact presentations: a panel beside the rack column holding a
// singer row (portrait, voice, style line, Change voice) and the expression card. Knob cells are
// 96 points tall (label, 52-point dial, caption, then a clear gap before the next row's label; the
// 720x480 minimum still fits two rows); a window too short for two rows gets one row of six, and a
// panel taller than the body may rise over the header rather than clip.
void layoutInspector(SingLayout& l, double bodyTop, double bodyBottom) {
  constexpr double kPad = 12.0;
  constexpr double kSingerRow = 56.0;
  constexpr double kHeader = 44.0;
  constexpr double kCell = 96.0;
  const auto width = std::min(340.0, std::max(0.0, l.rackArea.x - 8.0 - kSingEdge));
  const auto twoRows = kPad + kSingerRow + kSingGap + kHeader + 2.0 * kCell + kPad;
  const auto oneRow = kPad + kSingerRow + kSingGap + kHeader + kCell + kPad;
  const auto available = bodyBottom - bodyTop;
  l.knobsInOneRow = available < twoRows;
  const auto height = l.knobsInOneRow ? oneRow : twoRows;
  const auto top = std::max(kSingEdge, std::min(bodyTop, bodyBottom - height));
  l.inspector = {l.rackArea.x - 8.0 - width, top, width, height};
  const auto inner = ui::Rect{l.inspector.x + kPad, l.inspector.y + kPad, width - 2.0 * kPad,
                              height - 2.0 * kPad};
  l.singer = {inner.x, inner.y, inner.width, kSingerRow};
  l.portraitRing = {l.singer.x, l.singer.y + 6.0, 44.0, 44.0};
  l.singerChange = {l.singer.right() - 100.0, l.singer.y + 6.0, 100.0, 22.0};
  l.style = {l.portraitRing.right() + 12.0, l.singer.y + 34.0,
             l.singer.right() - l.portraitRing.right() - 12.0, 18.0};
  l.expression = {inner.x, l.singer.bottom() + kSingGap, inner.width,
                  inner.bottom() - (l.singer.bottom() + kSingGap)};
  const auto columns = l.knobsInOneRow ? 6.0 : 3.0;
  const auto cellWidth = l.expression.width / columns;
  for (std::size_t i = 0U; i < l.knob.size(); ++i) {
    const auto index = static_cast<double>(i);
    const auto column = l.knobsInOneRow ? index : static_cast<double>(i % 3U);
    const auto row = l.knobsInOneRow ? 0.0 : static_cast<double>(i / 3U);
    l.knob[i] = {l.expression.x + column * cellWidth, l.expression.y + kHeader + row * kCell,
                 cellWidth, kCell};
  }
}

}  // namespace

SingLayout solveSingLayout(double width, double height, bool inspectorOpen) noexcept {
  SingLayout l;
  const auto W = std::isfinite(width) ? std::max(width, 480.0) : 1600.0;
  const auto H = std::isfinite(height) ? std::max(height, 320.0) : 900.0;
  l.width = W;
  l.height = H;
  l.compactHeader = H < 720.0;
  const auto headerHeight = l.compactHeader ? 64.0 : 80.0;

  l.header = {kSingEdge, kSingEdge, W - 2.0 * kSingEdge, headerHeight};
  l.status = {kSingEdge, H - kSingEdge - kSingStatusHeight, W - 2.0 * kSingEdge,
              kSingStatusHeight};
  const auto bodyTop = l.header.bottom() + kSingGap;
  const auto bodyBottom = l.status.y - kSingGap;
  const auto bodyHeight = std::max(0.0, bodyBottom - bodyTop);
  // The lane gives height back to the grid on short windows instead of pushing it off screen.
  const auto laneHeight = l.compactHeader ? std::clamp(bodyHeight * 0.34, 72.0, 112.0) : 148.0;

  // The full rack needs its three cards at their minimum heights; anything shorter or narrower
  // collapses to the portrait rail rather than clipping cards under the status bar.
  const auto fullRackHeight = 180.0 + 248.0 + 104.0 + 2.0 * kSingGap;
  const auto fullRack = singRackWidth(W) >= 320.0 && bodyHeight >= fullRackHeight;
  const auto drawer = W < kSingDrawerWidth;
  const auto rackWidth = fullRack ? singRackWidth(W) : drawer ? 44.0 : 56.0;
  l.rack = fullRack ? RackPresentation::Full
           : drawer ? RackPresentation::Drawer
                    : RackPresentation::Rail;
  l.rackArea = {W - kSingEdge - rackWidth, bodyTop, rackWidth, bodyHeight};

  const auto editorRight = l.rackArea.x - kSingRackGap;
  l.lane = {kSingEdge, bodyBottom - laneHeight, std::max(0.0, editorRight - kSingEdge),
            laneHeight};
  l.editor = {kSingEdge, bodyTop, l.lane.width,
              std::max(0.0, l.lane.y - kSingGap - bodyTop)};

  l.tools = {l.editor.x + 8.0, l.editor.y + 8.0, std::max(0.0, l.editor.width - 16.0), 28.0};
  const auto gridLeft = l.editor.x + 64.0;
  const auto gridRight = l.editor.right() - 8.0;
  l.ruler = {gridLeft, l.tools.bottom() + 4.0, std::max(0.0, gridRight - gridLeft), 24.0};
  const auto gridTop = l.ruler.bottom();
  const auto gridBottom = l.editor.bottom() - 8.0;
  l.keyboard = {l.editor.x + 8.0, gridTop, gridLeft - (l.editor.x + 8.0),
                std::max(0.0, gridBottom - gridTop)};
  l.grid = {gridLeft, gridTop, l.ruler.width, l.keyboard.height};

  l.laneTabs = {l.lane.x + 8.0, l.lane.y + 8.0, std::max(0.0, l.lane.width - 16.0), 28.0};
  const auto plotTop = l.laneTabs.bottom() + 8.0;
  l.lanePlot = {l.lane.x + 40.0, plotTop, std::max(0.0, l.lane.right() - 8.0 - (l.lane.x + 40.0)),
                std::max(0.0, l.lane.bottom() - 8.0 - plotTop)};
  // The lane's 24-point value gutter sits left of the shared musical axis.
  l.laneTimePlot = {l.grid.x, l.lanePlot.y, l.grid.width, l.lanePlot.height};

  if (l.rack == RackPresentation::Full) {
    const auto expressionHeight = 248.0;
    const auto styleHeight = 104.0;
    const auto singerHeight = std::max(
        180.0, l.rackArea.height - expressionHeight - styleHeight - 2.0 * kSingGap);
    l.singer = {l.rackArea.x, l.rackArea.y, l.rackArea.width, singerHeight};
    l.expression = {l.rackArea.x, l.singer.bottom() + kSingGap, l.rackArea.width,
                    expressionHeight};
    l.style = {l.rackArea.x, l.expression.bottom() + kSingGap, l.rackArea.width, styleHeight};
    const auto ring = std::clamp(std::min(l.singer.width - 152.0, l.singer.height - 72.0), 120.0,
                                 288.0);
    l.portraitRing = {l.singer.x + (l.singer.width - ring) * 0.5, l.singer.y + 40.0, ring, ring};
    l.singerChange = {l.singer.right() - 116.0, l.singer.bottom() - 30.0, 100.0, 22.0};
    const auto columns = 3.0;
    const auto cellWidth = (l.expression.width - 32.0) / columns;
    const auto top = l.expression.y + 44.0;
    const auto cellHeight = (l.expression.height - 52.0) / 2.0;
    for (std::size_t i = 0U; i < l.knob.size(); ++i) {
      const auto column = static_cast<double>(i % 3U);
      const auto row = static_cast<double>(i / 3U);
      l.knob[i] = {l.expression.x + 16.0 + column * cellWidth, top + row * cellHeight, cellWidth,
                   cellHeight};
    }
  } else {
    // Rail or drawer: the singer stays present as a 44-point portrait button that opens the
    // inspector. The drawer button is the whole 44-point column; the rail centres it.
    const auto inset = l.rack == RackPresentation::Drawer ? 0.0 : (l.rackArea.width - 44.0) * 0.5;
    l.inspectorButton = {l.rackArea.x + inset, l.rackArea.y + (inset > 0.0 ? 12.0 : 0.0), 44.0,
                         44.0};
    l.inspectorOpen = inspectorOpen;
    if (inspectorOpen) {
      layoutInspector(l, bodyTop, bodyBottom);
    } else {
      l.portraitRing = l.inspectorButton;
    }
  }

  // Header, right to left so the transport and meter keep their size before tabs shrink. Narrow
  // headers shrink the wordmark, then drop tab labels, then drop the tabs; nothing overlaps.
  const auto wordmarkWidth = W >= 1180.0 ? 208.0 : 132.0;
  l.wordmark = {l.header.x + 16.0, l.header.y + (headerHeight - 48.0) * 0.5, wordmarkWidth, 48.0};
  l.settings = {l.header.right() - 56.0, l.header.y + (headerHeight - 32.0) * 0.5, 32.0, 32.0};
  l.outputMeterVisible = W >= 1180.0;
  const auto meterWidth = l.outputMeterVisible ? 160.0 : 0.0;
  l.outputMeter = {l.settings.x - 32.0 - meterWidth, l.header.y + (headerHeight - 44.0) * 0.5,
                   meterWidth, 44.0};
  const auto transportWidth = std::clamp(W * 0.27, 248.0, 432.0);
  const auto meterLeft = l.outputMeterVisible ? l.outputMeter.x - 24.0 : l.settings.x - 24.0;
  l.transport = {meterLeft - transportWidth, l.outputMeter.y, transportWidth, 44.0};
  const auto switchWidth = W >= 900.0 ? 132.0 : 120.0;
  const auto switchGap = W >= 900.0 ? 28.0 : 16.0;
  if (W >= 720.0)
    l.modeSwitch = {l.transport.x - switchGap - switchWidth,
                    l.header.y + (headerHeight - 28.0) * 0.5, switchWidth, 28.0};
  else
    l.wordmark.width = 0.0;  // The menu carries the identity and appearance at minimum width.
  if (l.modeSwitch.x < l.wordmark.right() + 12.0)
    l.wordmark.width = std::max(0.0, l.modeSwitch.x - 12.0 - l.wordmark.x);
  const auto tabsLeft = l.wordmark.width > 0.0 ? l.wordmark.right() + 24.0 : l.header.x + 16.0;
  auto tabsWidth = std::clamp(l.modeSwitch.x - 24.0 - tabsLeft, 0.0, 400.0);
  if (tabsWidth < 5.0 * 36.0) tabsWidth = 0.0;
  l.workspaceTabs = {tabsLeft, l.header.y + 8.0, tabsWidth, headerHeight - 16.0};
  if (tabsWidth == 0.0) {
    const auto right = l.modeSwitch.width > 0.0 ? l.modeSwitch.x - 12.0 : l.transport.x - 12.0;
    const auto buttonWidth = std::clamp(right - tabsLeft, 0.0, W < 720.0 ? 92.0 : 120.0);
    l.workspaceMenuButton = {tabsLeft, l.header.y + (headerHeight - 32.0) * 0.5,
                             buttonWidth, 32.0};
    const auto includeModes = l.modeSwitch.width <= 0.0;
    constexpr double kRow = 28.0;
    const auto workspaceRows = static_cast<double>(l.workspaceMenuRow.size());
    const auto rows = workspaceRows + (includeModes ? 1.0 : 0.0);
    const auto menuWidth = std::min(220.0, W - 2.0 * kSingEdge);
    const auto menuX = std::min(tabsLeft, W - kSingEdge - menuWidth);
    l.workspaceMenu = {menuX, l.header.bottom() + 4.0, menuWidth, 16.0 + rows * kRow};
    for (std::size_t i = 0U; i < l.workspaceMenuRow.size(); ++i)
      l.workspaceMenuRow[i] = {menuX + 8.0, l.workspaceMenu.y + 8.0 + kRow * static_cast<double>(i),
                                menuWidth - 16.0, kRow};
    if (includeModes) {
      const auto half = (menuWidth - 16.0) * 0.5;
      for (std::size_t i = 0U; i < l.modeMenuRow.size(); ++i)
        l.modeMenuRow[i] = {menuX + 8.0 + half * static_cast<double>(i),
                             l.workspaceMenu.y + 8.0 + kRow * workspaceRows, half, kRow};
    }
  }
  l.workspaceLabelsVisible = tabsWidth >= 320.0;
  const auto tabWidth = tabsWidth / 5.0;
  for (std::size_t i = 0U; i < l.workspaceTab.size(); ++i)
    l.workspaceTab[i] = {l.workspaceTabs.x + tabWidth * static_cast<double>(i), l.workspaceTabs.y,
                         tabWidth, l.workspaceTabs.height};

  const auto readoutTop = l.transport.y + 4.0;
  l.playButton = {l.transport.x + 6.0, l.transport.y + 6.0, 32.0, 32.0};
  const auto innerLeft = l.playButton.right() + 10.0;
  const auto inner = l.transport.right() - 8.0 - innerLeft;
  l.positionReadout = {innerLeft, readoutTop, inner * 0.50, 36.0};
  l.tempoReadout = {l.positionReadout.right(), readoutTop, inner * 0.30, 36.0};
  l.meterReadout = {l.tempoReadout.right(), readoutTop, inner * 0.20, 36.0};

  l.trackLabel = {l.tools.x + 4.0, l.tools.y + 2.0, 132.0, 24.0};
  l.classicToggle = {l.tools.right() - 96.0, l.tools.y + 2.0, 92.0, 24.0};
  l.gridLabel = {l.classicToggle.x - 104.0, l.tools.y + 2.0, 96.0, 24.0};
  return l;
}

}  // namespace seam::native_ui::design
