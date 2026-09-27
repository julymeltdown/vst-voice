#include "seam/native_ui/editor_scene.hpp"

#include "seam/native_ui/editor_frame_layout.hpp"
#include "seam/native_ui/editor_label_policy.hpp"
#include "seam/native_ui/diagnostic_presentation.hpp"

#include "seam/time/tick.hpp"
#include "seam/text/unicode.hpp"
#include "seam/ui/phoneme_lane_model.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <numbers>
#include <sstream>
#include <string_view>
#include <utility>

namespace seam::native_ui {
namespace {

struct VibratoEnvelopeGeometry final {
  double activeFrames{0.0};
  double periodFrames{0.0};
  double activeWidth{0.0};
  double baseline{0.0};
  double amplitude{0.0};
  double fadeIn{0.0};
  double fadeOut{0.0};
  double onsetX{0.0};
};

std::optional<VibratoEnvelopeGeometry> vibratoGeometry(
    const domain::Note& note, time::Tick regionStart,
    const time::TempoMap& tempoMap, ui::Rect bounds) noexcept {
  const auto& vibrato = note.vibrato;
  if (!vibrato.enabled || bounds.width < 18.0 || bounds.height < 5.0)
    return std::nullopt;
  constexpr double sampleRate = 48000.0;
  const auto startFrame = tempoMap.sampleFrameAt(regionStart + note.startTick, sampleRate);
  const auto endFrame = tempoMap.sampleFrameAt(regionStart + note.endTick(), sampleRate);
  const auto durationFrames = static_cast<double>(endFrame) - static_cast<double>(startFrame);
  const auto activeFraction = 1.0 - static_cast<double>(vibrato.startFraction);
  if (!std::isfinite(durationFrames) || durationFrames <= 0.0 ||
      activeFraction <= 0.0)
    return std::nullopt;
  const auto activeFrames = durationFrames * activeFraction;
  const auto periodFrames = static_cast<double>(vibrato.periodMilliseconds) * 48.0;
  if (!std::isfinite(activeFrames) || activeFrames <= 0.0 ||
      !std::isfinite(periodFrames) || periodFrames <= 0.0) return std::nullopt;
  const auto activeWidth = bounds.width * activeFraction;
  if (activeWidth < 8.0) return std::nullopt;
  return VibratoEnvelopeGeometry{
      .activeFrames = activeFrames,
      .periodFrames = periodFrames,
      .activeWidth = activeWidth,
      .baseline = bounds.y + bounds.height * 0.69,
      .amplitude = bounds.height * 0.28 *
          static_cast<double>(vibrato.depthCents) / 200.0,
      .fadeIn = activeFrames * static_cast<double>(vibrato.fadeInFraction),
      .fadeOut = activeFrames * static_cast<double>(vibrato.fadeOutFraction),
      .onsetX = bounds.x + bounds.width * static_cast<double>(vibrato.startFraction),
  };
}

}  // namespace

std::optional<VibratoHandlePoints> vibratoHandlePositions(
    const domain::Note& note, time::Tick regionStart,
    const time::TempoMap& tempoMap, ui::Rect screenBounds) noexcept {
  const auto geometry = vibratoGeometry(note, regionStart, tempoMap, screenBounds);
  if (!geometry) return std::nullopt;
  const auto pointAt = [&](double fraction) {
    const auto position = geometry->activeFrames * fraction;
    const auto fadeIn = geometry->fadeIn > 0.0
        ? std::min(1.0, position / geometry->fadeIn) : 1.0;
    const auto fadeOut = geometry->fadeOut > 0.0
        ? std::min(1.0, (geometry->activeFrames - position) / geometry->fadeOut)
        : 1.0;
    const auto envelope = std::max(0.0, std::min(fadeIn, fadeOut));
    const auto phase = position / geometry->periodFrames +
        static_cast<double>(note.vibrato.phaseTurns);
    return ui::Point{
        geometry->onsetX + geometry->activeWidth * fraction,
        geometry->baseline - geometry->amplitude * envelope *
            std::sin(2.0 * std::numbers::pi_v<double> * phase),
    };
  };
  std::optional<ui::Point> fadeIn;
  if (geometry->fadeIn > 0.0)
    fadeIn = pointAt(geometry->fadeIn / geometry->activeFrames);
  else if (geometry->activeWidth > 0.0)
    fadeIn = ui::Point{geometry->onsetX, screenBounds.y + 1.5};
  std::optional<ui::Point> fadeOut;
  if (geometry->fadeOut > 0.0)
    fadeOut = pointAt(1.0 - geometry->fadeOut / geometry->activeFrames);
  else if (geometry->activeWidth > 0.0)
    fadeOut = ui::Point{geometry->onsetX + geometry->activeWidth,
                        screenBounds.y + 1.5};
  std::optional<ui::Point> period;
  std::optional<ui::Point> phaseHandle;
  const auto periodFraction = geometry->periodFrames / geometry->activeFrames;
  if (geometry->activeFrames / 48.0 >= 5.0 && periodFraction <= 1.0 &&
      geometry->activeWidth * periodFraction >= 9.0) {
    period = pointAt(periodFraction);
    const auto phaseTurns = static_cast<double>(note.vibrato.phaseTurns);
    if (phaseTurns > 0.001) {
      phaseHandle = ui::Point{
          geometry->onsetX + geometry->activeWidth * periodFraction * phaseTurns,
          screenBounds.y + 1.5,
      };
    }
  }
  return VibratoHandlePoints{
      .onset = {geometry->onsetX, geometry->baseline},
      .depth = pointAt(0.25),
      .fadeIn = fadeIn,
      .fadeOut = fadeOut,
      .period = period,
      .phase = phaseHandle,
  };
}

TechnicalLaneHeights resolveEditorTechnicalLaneHeights(
    const EditorSceneState& state, const EditorSceneLayout& layout,
    double contentBottom) noexcept {
  auto technical = resolveTechnicalLaneHeights(TechnicalLaneLayoutInput{
      .presentation = state.technicalLanes,
      .populated = {
          !state.phonemes.tokens.empty() || state.technicalLaneAvailable[0U],
          !state.unitOverrides.empty() || state.technicalLaneAvailable[1U],
          !state.seamOverrides.empty() || state.technicalLaneAvailable[2U],
          !state.pitchAutomation.empty() || state.technicalLaneAvailable[3U] ||
              state.expressionLabelVisible(),
      },
      .previewHeights = { layout.phonemeLaneHeight, layout.unitLaneHeight,
                          layout.seamLaneHeight, layout.automationLaneHeight },
      .contentTop = layout.contentTop(),
      .contentBottom = contentBottom,
  });
  if (!state.technicalLaneHeightsOverride.has_value()) return technical;
  technical.values = *state.technicalLaneHeightsOverride;
  const auto used = std::accumulate(technical.values.begin(),
                                    technical.values.end(), 0.0);
  technical.pianoBottom = std::max(layout.contentTop(), contentBottom - used);
  return technical;
}

bool editorDockVisible(const EditorSceneState& state) noexcept {
  // Dock presence is decided by the package and the display mode, through the predicate both surfaces
  // share. The previous test asked whether a portrait had decoded, and the portrait is published for the
  // current render status, so a singer whose state artwork changed could make the dock appear or vanish
  // while nothing about the creator's intent had changed.
  const auto characterFull =
      state.characterMode == domain::CharacterDisplayMode::Full &&
      state.voiceIdentity.characterActive && state.characterDockReserved;
  const auto arrangementVisible =
      !state.recoverySupport.visible && !state.voicebankBrowserVisible &&
      !state.audioSettings.visible &&
      state.characterMode == domain::CharacterDisplayMode::Off &&
      !state.arrangementTracks.empty();
  return state.recoverySupport.visible || state.audioSettings.visible ||
         characterFull || arrangementVisible || state.voicebankBrowserVisible;
}

double resolveEditorDockWidth(const EditorSceneState& state,
                              const EditorSceneLayout& layout) noexcept {
  if (state.dockWidthOverride.has_value()) {
    return std::clamp(*state.dockWidthOverride, 0.0, layout.characterDockWidth);
  }
  return editorDockVisible(state) ? layout.characterDockWidth : 0.0;
}

std::optional<double> resolveArrangementInspectorTop(const EditorSceneState& state,
    const EditorSceneLayout& layout, double contentBottom) noexcept {
  if (!state.inspector.valid) return std::nullopt;
  double y = layout.toolbarHeight + layout.trackListTop;
  for (const auto& track : state.arrangementTracks) {
    if (y + layout.trackRowHeight > contentBottom) break;
    y += layout.trackRowAdvance;
    for (std::size_t i = 0U; i < track.regions.size(); ++i) {
      if (y + layout.regionAdvance - layout.regionBottomPadding > contentBottom) break;
      y += layout.regionAdvance;
    }
  }
  if (y + layout.inspectorHeight + layout.inspectorDividerInset > contentBottom) return std::nullopt;
  return contentBottom - layout.inspectorHeight;
}

void paintEditorUnavailable(RasterCanvas& canvas, const EditorSceneTheme& theme) noexcept {
  canvas.clear(theme.background);
  const auto width = canvas.logicalWidth();
  const auto height = canvas.logicalHeight();
  const ui::Rect title{24.0, std::max(24.0, height * 0.5 - 30.0), std::max(1.0, width - 48.0), 20.0};
  canvas.drawText(title, kEditorUnavailableTitle, theme.primaryText, 14.0);
  canvas.drawText(ui::Rect{title.x, title.bottom() + 8.0, title.width, 18.0},
                  kEditorUnavailableDetail, theme.secondaryText, 11.0);
}

}  // namespace seam::native_ui
