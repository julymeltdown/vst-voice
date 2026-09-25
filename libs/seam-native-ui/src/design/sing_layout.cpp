#include "seam/native_ui/design/sing_layout.hpp"

#include <algorithm>
#include <cmath>

namespace seam::native_ui::design {

double singRackWidth(double width) noexcept {
  if (!(width >= 1100.0)) return 56.0;
  return std::clamp(width * 0.275, 320.0, 440.0);
}

SingLayout solveSingLayout(double width, double height) noexcept {
  SingLayout l;
  const auto W = std::isfinite(width) ? std::max(width, 480.0) : 1600.0;
  const auto H = std::isfinite(height) ? std::max(height, 320.0) : 900.0;
  l.width = W;
  l.height = H;
  l.compactHeader = H < 720.0;
  const auto headerHeight = l.compactHeader ? 64.0 : 80.0;
  const auto laneHeight = l.compactHeader ? 112.0 : 148.0;

  l.header = {kSingEdge, kSingEdge, W - 2.0 * kSingEdge, headerHeight};
  l.status = {kSingEdge, H - kSingEdge - kSingStatusHeight, W - 2.0 * kSingEdge,
              kSingStatusHeight};
  const auto bodyTop = l.header.bottom() + kSingGap;
  const auto bodyBottom = l.status.y - kSingGap;

  const auto rackWidth = singRackWidth(W);
  l.rack = rackWidth >= 320.0 ? RackPresentation::Full : RackPresentation::Rail;
  l.rackArea = {W - kSingEdge - rackWidth, bodyTop, rackWidth,
                std::max(0.0, bodyBottom - bodyTop)};

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
    // Rail: the singer stays present as a small portrait; tapping it opens the voice browser.
    l.singer = {l.rackArea.x, l.rackArea.y, l.rackArea.width, std::min(l.rackArea.height, 72.0)};
    l.portraitRing = {l.rackArea.x + (l.rackArea.width - 44.0) * 0.5, l.rackArea.y + 12.0, 44.0,
                      44.0};
    l.singerChange = l.portraitRing;
  }

  // Header, right to left so the transport and meter keep their size before tabs shrink.
  l.wordmark = {l.header.x + 16.0, l.header.y + (headerHeight - 48.0) * 0.5, 208.0, 48.0};
  l.settings = {l.header.right() - 56.0, l.header.y + (headerHeight - 32.0) * 0.5, 32.0, 32.0};
  l.outputMeterVisible = W >= 1180.0;
  const auto meterWidth = l.outputMeterVisible ? 160.0 : 0.0;
  l.outputMeter = {l.settings.x - 32.0 - meterWidth, l.header.y + (headerHeight - 44.0) * 0.5,
                   meterWidth, 44.0};
  const auto transportWidth = std::clamp(W * 0.27, 300.0, 432.0);
  const auto meterLeft = l.outputMeterVisible ? l.outputMeter.x - 24.0 : l.settings.x - 24.0;
  l.transport = {meterLeft - transportWidth, l.outputMeter.y, transportWidth, 44.0};
  l.modeSwitch = {l.transport.x - 28.0 - 132.0, l.header.y + (headerHeight - 28.0) * 0.5, 132.0,
                  28.0};
  const auto tabsLeft = l.wordmark.right() + 24.0;
  const auto tabsWidth = std::clamp(l.modeSwitch.x - 24.0 - tabsLeft, 0.0, 400.0);
  l.workspaceTabs = {tabsLeft, l.header.y + 8.0, tabsWidth, headerHeight - 16.0};
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
