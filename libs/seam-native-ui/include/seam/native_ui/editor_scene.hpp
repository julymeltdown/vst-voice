#pragma once

#include "seam/character/character.hpp"
#include "seam/domain/project.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/native_ui/editor_frame_layout.hpp"
#include "seam/native_ui/voice_identity.hpp"
#include "seam/native_ui/editor_interaction_state.hpp"
#include "seam/native_ui/render_status_panel.hpp"
#include "seam/native_ui/arrangement_panel.hpp"
#include "seam/native_ui/track_inspector.hpp"
#include "seam/authoring/diagnostic.hpp"
#include "seam/authoring/export_service.hpp"
#include "seam/authoring/audio_settings.hpp"
#include "seam/authoring/voicebank_browser.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include "seam/ui/piano_roll_model.hpp"
#include "seam/ui/sample_microscope_model.hpp"

#include <cstdint>
#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace seam::native_ui {

struct EditorSceneTheme final {
  Color background{16, 15, 19, 255};
  Color toolbarTop{38, 34, 41, 255};
  Color toolbarBottom{27, 25, 30, 255};
  Color panel{24, 22, 28, 255};
  Color keyboardWhite{48, 45, 53, 255};
  Color keyboardBlack{34, 31, 39, 255};
  Color gridStrong{70, 62, 74, 255};
  Color gridWeak{41, 37, 45, 255};
  Color note{74, 56, 78, 255};
  Color noteAlternate{80, 61, 86, 255};
  Color noteSelected{139, 76, 105, 255};
  Color noteStroke{119, 96, 128, 255};
  Color noteSelectedStroke{237, 175, 205, 255};
  Color focusRing{255, 221, 101, 255};
  Color primaryText{241, 235, 242, 255};
  Color secondaryText{170, 159, 171, 255};
  Color accent{170, 77, 116, 255};
  Color accentSecondary{110, 90, 134, 255};
  Color playhead{98, 192, 190, 255};
  Color selection{155, 88, 124, 58};
  Color phoneme{70, 67, 87, 255};
  Color unit{56, 72, 78, 255};
  Color automation{186, 104, 145, 255};
  Color lyricEditorBackground{12, 11, 14, 238};
  Color transportPlaying{83, 42, 61, 255};
  Color transportIdle{31, 29, 35, 255};
  Color transportDisabled{24, 22, 28, 255};
  Color phonemeLaneBackground{22, 20, 26, 255};
  Color unitLaneBackground{20, 23, 27, 255};
  Color seamLaneBackground{27, 21, 25, 255};
  Color automationLaneBackground{25, 21, 27, 255};
  Color seamRail{49, 39, 47, 255};
  Color characterBackground{18, 16, 21, 255};
  Color diagnosticCritical{130, 42, 58, 255};
  Color diagnosticWarning{104, 76, 38, 255};
  Color diagnosticInfo{86, 45, 68, 255};
  Color microscopeOverlay{11, 10, 14, 248};
  Color microscopeBorder{182, 104, 145, 255};
  Color microscopeWaveBackground{25, 23, 30, 255};
  Color microscopeSpectrogramBackground{16, 18, 22, 255};
  Color microscopeSpectrogramLow{28, 24, 38, 255};
  Color microscopeSpectrogramHigh{210, 112, 178, 255};
  Color microscopeWaveform{150, 102, 136, 255};
  Color microscopeMarker{94, 192, 190, 220};
  Color microscopeMarkerLabel{180, 210, 205, 255};
  Color microscopePitchMark{235, 175, 205, 255};
  Color microscopePitchMarkUnlocked{118, 96, 142, 170};
  Color runtimeOverlayBackground{20, 18, 24, 230};
  Color runtimeOverlayBorder{201, 190, 201, 255};
  Color runtimeOverlayText{229, 218, 228, 255};
  Color runtimeOverlaySecondaryText{160, 150, 164, 255};
  Color runtimeOverlayAccent{168, 82, 120, 255};
  Color runtimeOverlayMeter{58, 52, 62, 255};
  Color runtimeOverlayHost{111, 193, 190, 255};
  Color runtimeOverlayReady{153, 178, 169, 255};
  Color runtimeOverlayError{205, 126, 126, 255};
};

struct EditorSceneState final {
  struct OverlapDetailMember final {
    domain::NoteId noteId;
    std::string lyric;
    std::uint8_t midiKey{0U};
    bool selected{false};
  };
  struct OverlapDetail final {
    std::size_t groupIndex{0U};
    std::vector<OverlapDetailMember> members;
  };

  std::string projectName{"Project SEAM"};
  std::uint64_t revision{0};
  bool playing{false};
  bool loopEnabled{false};
  bool loopAvailable{false};
  bool dirty{false};
  bool audioDeviceOnline{false};
  std::string audioBackend{"OFFLINE"};
  double tempoBpm{120.0};
  RenderStatusView renderStatus;
  double logicalWidth{1440.0};
  double logicalHeight{900.0};
  std::optional<ui::Rect> boxSelection;
  std::optional<ui::Rect> focusedElementBounds;
  std::optional<ui::Rect> lyricEditor;
  std::string compositionPreview;
  double playheadPixel{0.0};

  phonemizer::Result phonemes;
  std::vector<domain::UnitSelectionOverride> unitOverrides;
  std::vector<domain::SeamOverride> seamOverrides;
  std::optional<domain::PhonemeKey> selectedSeam;
  bool seamPreviewAlternate{false};
  std::vector<domain::PitchAutomationPoint> pitchAutomation;
  std::array<domain::TechnicalLanePresentation, 4U> technicalLanes{};
  std::array<bool, 4U> technicalLaneAvailable{};
  std::optional<std::array<double, 4U>> technicalLaneHeightsOverride;

  domain::CharacterDisplayMode characterMode{domain::CharacterDisplayMode::Minimal};
  character::State characterState{character::State::Neutral};
  std::string characterName;
  std::string characterStyle;
  const PixelSurface* characterPortrait{nullptr};
  bool voicebankBrowserVisible{false};
  std::vector<authoring::VoicebankCard> voicebankCards;
  std::vector<ArrangementTrackItem> arrangementTracks;
  TrackInspectorSnapshot inspector;
  std::vector<authoring::Diagnostic> diagnostics;
  authoring::ExportProgress exportProgress;
  std::optional<authoring::ExportResult> lastExport;
  struct SampleMicroscopeView final {
    const ui::SampleMicroscopeModel* model{nullptr};
    std::string unitId;
    std::string destinationContext;
    bool canPlay{false};
  };
  std::optional<SampleMicroscopeView> sampleMicroscope;
  struct AudioDeviceOption final {
    std::string id;
    std::string name;
    bool physical{false};
    bool selected{false};
  };
  struct AudioSettingsView final {
    bool visible{false};
    authoring::AudioSettings current;
    std::vector<AudioDeviceOption> devices;
    std::uint64_t underflowFrames{0U};
    std::uint64_t xruns{0U};
    std::string diagnostic;
  } audioSettings;
  std::size_t selectedNoteCount{0U};
  std::optional<domain::NoteId> hoveredNote;
  std::optional<domain::NoteId> focusedNote;
  std::optional<EditorDetail> detail;
  std::optional<OverlapDetail> overlapDetail;
  VoiceIdentityView voiceIdentity;
  std::optional<double> dockWidthOverride;
};

struct EditorSceneLayout final {
  struct TechnicalLaneGeometry final {
    double pianoBottom{0.0};
    double phonemeTop{0.0};
    double phonemeHeight{0.0};
    double unitTop{0.0};
    double unitHeight{0.0};
    double seamTop{0.0};
    double seamHeight{0.0};
    double pitchTop{0.0};
    double pitchHeight{0.0};
    double bottom{0.0};
  };

  double toolbarHeight{64.0};
  double rulerHeight{34.0};
  double statusHeight{28.0};
  double keyboardWidth{76.0};
  double minimumTimelineWidth{180.0};
  double phonemeLaneHeight{42.0};
  double unitLaneHeight{60.0};
  double seamLaneHeight{54.0};
  double automationLaneHeight{72.0};
  double characterDockWidth{238.0};
  double characterDockPadding{12.0};
  double characterDockPortraitTopInset{12.0};
  double characterDockPortraitMinimumHeight{160.0};
  double characterDockPortraitBottomReserve{164.0};
  double characterDockPortraitScale{1.0};
  double characterDockTextTopGap{30.0};
  double characterDockTextInsetX{14.0};
  double characterDockNameToRoleAdvance{18.0};
  double characterDockRoleToStateAdvance{16.0};
  double characterDockStateToModeAdvance{16.0};
  double characterDockNameFontSize{9.0};
  double characterDockDetailFontSize{7.0};
  double characterDockDividerStrokeWidth{1.0};
  double characterDockPortraitBorderWidth{1.0};
  double laneLabelX{8.0};
  double laneLabelBaselineOffset{12.0};
  double laneLabelFontSize{11.0};
  double laneInstructionInsetX{8.0};
  double laneInstructionBottomPadding{10.0};
  double laneInstructionFontSize{10.0};
  double compactLaneInstructionMinimumHeight{42.0};
  double phonemeLaneContentTopInset{4.0};
  double phonemeLaneContentBottomInset{8.0};
  double technicalLaneDividerStrokeWidth{1.0};
  double technicalLaneItemStrokeWidth{1.0};
  double phonemeTextInsetX{5.0};
  double phonemeTextBaselineOffset{9.0};
  double phonemeTextFontSize{11.0};
  double unitTextInsetX{5.0};
  double unitLabelBaselineOffset{7.0};
  double unitRendererBaselineOffset{22.0};
  double unitTextFontSize{11.0};
  double unitCardTopPadding{5.0};
  double unitCardBottomPadding{20.0};
  double unitCardMinimumWidth{3.0};
  double unitTextBottomPadding{1.0};
  double pitchEmptyTextInsetX{8.0};
  double pitchEmptyTextBaselineOffset{26.0};
  double pitchEmptyTextFontSize{8.0};
  double pitchEmptyTextBottomPadding{1.0};
  double compactLaneBottomPadding{4.0};
  double automationCenterFraction{0.5};
  double pitchAutomationCentsRange{600.0};
  double pitchAutomationVerticalScale{0.38};
  double automationGridStrokeWidth{0.5};
  double automationCurveStrokeWidth{1.5};
  double automationPointHalfSize{2.5};
  double seamRailInset{5.0};
  double seamRailBottomPadding{20.0};
  double seamRailWidth{10.0};
  double seamRailMinHeight{1.0};
  double laneLabelBottomPadding{4.0};
  std::size_t gridQuartersPerBar{4U};
  double gridStrongStrokeWidth{1.0};
  double gridWeakStrokeWidth{0.5};
  double gridBarLabelInsetX{5.0};
  double gridBarLabelBaselineOffset{10.0};
  double gridBarLabelFontSize{8.0};
  double keyboardLabelInsetX{12.0};
  double keyboardLabelBaselineOffset{5.0};
  double keyboardLabelFontSize{8.0};
  double keyboardGridStrokeWidth{0.5};
  double keyboardDividerStrokeWidth{1.0};
  double noteMinimumLabelWidth{34.0};
  double noteLabelInsetX{6.0};
  double noteLabelBaselineOffset{5.0};
  double noteLabelHorizontalPadding{12.0};
  double noteTextCharacterWidth{5.0};
  double noteFontSize{12.0};
  double noteStrokeWidth{1.0};
  double overlapBadgeWidth{30.0};
  double overlapBadgeHeight{18.0};
  double overlapBadgeGap{3.0};
  double overlapDetailWidth{360.0};
  double overlapDetailTitleHeight{20.0};
  double overlapDetailRowHeight{16.0};
  double overlapDetailGap{6.0};
  double boxSelectionStrokeWidth{1.0};
  double playheadStrokeWidth{1.0};
  double lyricEditorInsetX{8.0};
  double lyricEditorBaselineOffset{7.0};
  double lyricEditorFontSize{11.0};
  double lyricEditorBorderWidth{2.0};
  double focusRingInset{2.0};
  double focusRingStrokeWidth{2.0};

  double toolbarTitleX{20.0};
  double toolbarTitleBaseline{17.0};
  double toolbarSubtitleBaseline{40.0};
  double toolbarTitleFontSize{18.0};
  double toolbarSubtitleFontSize{12.0};
  double toolbarControlTop{14.0};
  double toolbarControlHeight{36.0};
  double transportX{330.0};
  double transportWidth{102.0};
  double transportTextInsetX{15.0};
  double transportTextBaseline{25.0};
  double transportFontSize{11.0};
  double controlStrokeWidth{1.0};
  double controlVisibilityPadding{20.0};
  double stopX{448.0};
  double stopWidth{72.0};
  double stopTextX{463.0};
  double stopFontSize{9.0};
  double bpmX{536.0};
  double bpmWidth{104.0};
  double bpmTextX{551.0};
  double batchLyricsWidth{112.0};
  double batchLyricsFontSize{8.0};
  double loopWidth{72.0};
  double loopFontSize{8.0};
  double compactToolbarGap{4.0};
  double compactTransportWidth{72.0};
  double compactStopWidth{56.0};
  double compactBpmWidth{92.0};
  double compactToolbarRightInset{8.0};
  double compactToolbarTitleReserve{204.0};
  double projectVisibilityThreshold{720.0};
  double projectMinimumX{700.0};
  double projectRightInset{430.0};
  double projectTitleBaseline{16.0};
  double projectRevisionBaseline{39.0};
  double projectTitleFontSize{11.0};
  double projectRevisionFontSize{8.0};
  double projectMinimumTextWidth{48.0};
  double portraitSide{48.0};
  double portraitRightInset{62.0};
  double portraitTop{8.0};
  double dirtyIndicatorRightInset{22.0};
  double dirtyIndicatorTop{17.0};
  double dirtyIndicatorSize{8.0};
  double imageScale{1.0};

  double panelDividerStrokeWidth{1.0};
  double panelTitleInsetX{12.0};
  double panelTitleBaseline{16.0};
  double panelTitleFontSize{10.0};
  double panelInstructionFontSize{8.0};
  double panelInstructionBaselineOffset{28.0};
  double panelSecondaryInstructionFontSize{6.0};
  double panelRowTextInsetX{8.0};
  double panelRowTextBaselineOffset{11.0};
  double panelRowTextWidthInset{16.0};
  double panelRowTextCharacterWidth{4.5};
  double arrangementActionTop{5.0};
  double arrangementActionWidth{24.0};
  double arrangementActionGap{3.0};
  double arrangementActionRightInset{8.0};
  double arrangementActionTextInsetX{7.0};
  double arrangementActionTextBaselineOffset{14.0};
  double arrangementActionFontSize{9.0};
  double trackListTop{34.0};
  double trackRowInsetX{6.0};
  double trackRowTopOffset{-3.0};
  double trackRowHeight{22.0};
  double trackTextBaselineOffset{3.0};
  double trackTextFontSize{10.0};
  double trackTextInsetX{12.0};
  double trackTextWidthInset{24.0};
  double trackRowAdvance{24.0};
  double regionTextInsetX{22.0};
  double regionTextBaselineOffset{2.0};
  double regionTextFontSize{9.0};
  double regionTextWidthInset{34.0};
  double regionAdvance{19.0};
  double regionBottomPadding{1.0};
  double inspectorHeight{140.0};
  double inspectorDividerInset{8.0};
  double inspectorTextInsetX{12.0};
  double inspectorNameBaseline{20.0};
  double inspectorLabelBaseline{4.0};
  double inspectorFieldAdvance{14.0};
  double inspectorNameToFirstFieldAdvance{16.0};
  double inspectorTextWidthInset{24.0};
  double panelTextCharacterWidth{5.0};
  double inspectorFontSize{9.0};
  double inspectorNameFontSize{10.0};
  double secondaryTextCharacterWidth{4.5};
  double audioSettingsRowTop{48.0};
  double audioSettingsRowHeight{28.0};
  double audioSettingsRowGap{4.0};
  double audioSettingsInsetX{10.0};
  double audioSettingsFontSize{7.0};
  double audioSettingsStatsTopGap{4.0};
  double audioSettingsStatsBottomInset{28.0};
  double audioSettingsStatsHeight{18.0};
  double audioSettingsDiagnosticVisibilityInset{14.0};
  double audioSettingsDiagnosticBaselineInset{10.0};
  double voicebankCardTop{42.0};
  double voicebankCardHeight{86.0};
  double voicebankCardGap{8.0};
  double voicebankCardInsetX{10.0};
  double voicebankCardTextInsetX{18.0};
  double voicebankCardTitleBaseline{14.0};
  double voicebankCardDetailAdvance{13.0};
  double voicebankCardFontSize{7.0};
  double voicebankCardDiagnosticMinimumHeight{72.0};

  double statusHorizontalPadding{14.0};
  double statusGap{12.0};
  double statusAudioFraction{0.25};
  double statusRenderFraction{0.52};
  double statusRenderMaxWidth{320.0};
  double statusTextBaseline{8.0};
  double statusFontSize{11.0};
  double diagnosticStripHeight{64.0};
  double diagnosticTextInsetX{14.0};
  double diagnosticTitleTop{8.0};
  double diagnosticImpactTop{32.0};
  double diagnosticTitleFontSize{13.0};
  double diagnosticImpactFontSize{11.0};
  double diagnosticActionWidth{112.0};
  double diagnosticActionHeight{28.0};
  double diagnosticActionGap{8.0};
  double diagnosticMoreRightInset{90.0};
  double diagnosticMoreFontSize{11.0};
  double voiceIdentityWidth{148.0};
  double voiceIdentityRightInset{74.0};
  double voiceIdentityTitleTop{6.0};
  double voiceIdentityTitleHeight{16.0};
  double voiceIdentityStateTop{25.0};
  double voiceIdentityStateHeight{12.0};
  double voiceIdentityTitleFontSize{12.0};
  double voiceIdentityStateFontSize{10.0};
  double exportStripHeight{28.0};
  double exportProgressBarHeight{4.0};
  double exportTextInsetX{14.0};
  double exportTextBaseline{10.0};
  double exportFontSize{7.0};
  double exportCancelWidth{72.0};
  double exportCancelHeight{16.0};
  double exportCancelRightInset{14.0};
  double exportCancelTop{7.0};
  double microscopePanelInsetX{52.0};
  double microscopePanelTop{74.0};
  double microscopePanelBottomInset{52.0};
  double microscopePlotInsetX{90.0};
  double microscopePlotTop{118.0};
  double microscopePlotGap{12.0};
  double microscopePlotMinimumWidth{340.0};
  double microscopeWaveformMinimumHeight{120.0};
  double microscopeWaveformHeightAvailableInset{98.0};
  double microscopeWaveformHeightFraction{0.42};
  double microscopeSpectrogramMinimumHeight{120.0};
  double microscopeSpectrogramBottomInset{76.0};
  double microscopeHeaderInsetX{20.0};
  double microscopeTitleBaselineOffset{17.0};
  double microscopeContextBaselineOffset{34.0};
  double microscopeCloseRightInset{190.0};
  double microscopeCloseMinimumX{180.0};
  double microscopeCloseTop{6.0};
  double microscopeCloseHeight{24.0};
  double microscopeCloseRightGap{8.0};
  double microscopeTitleMinimumWidth{120.0};
  double microscopeTitleRightGap{84.0};
  double microscopeContextMinimumWidth{140.0};
  double microscopeContextRightInset{160.0};
  double microscopeTitleFontSize{11.0};
  double microscopeContextFontSize{7.0};
  double microscopeCloseFontSize{7.0};
  double microscopeBorderWidth{2.0};
  double microscopePlotBorderWidth{1.0};
  double microscopePlotLabelInsetX{6.0};
  double microscopePlotLabelBaselineOffset{12.0};
  double microscopePlotLabelFontSize{6.0};
  double microscopeWaveformStrokeWidth{1.0};
  double microscopeSpectrogramGridStrokeWidth{0.5};
  double microscopeMarkerStrokeWidth{1.0};
  double microscopeMarkerLabelInsetX{2.0};
  double microscopeMarkerLabelBaselineOffset{4.0};
  double microscopeMarkerLabelFontSize{6.0};
  double microscopePitchMarkWidth{0.5};
  std::size_t microscopeSpectrogramMaxColumns{280U};
  std::size_t microscopeSpectrogramMaxBins{180U};
  std::size_t microscopeSpectrogramGridDivisions{4U};

  double runtimeOverlayTop{60.0};
  double runtimeOverlayHeight{32.0};
  double runtimeOverlayRightInset{30.0};
  double runtimeOverlayMinLeft{520.0};
  double runtimeOverlayWidth{330.0};
  double runtimeOverlayTitleInsetX{10.0};
  double runtimeOverlayTitleBaseline{70.0};
  double runtimeOverlayDetailBaseline{86.0};
  double runtimeOverlayTitleFontSize{10.0};
  double runtimeOverlayDetailFontSize{9.0};
  double runtimeOverlayMeterInsetX{112.0};
  double runtimeOverlayMeterTop{72.0};
  double runtimeOverlayMeterWidth{190.0};
  double runtimeOverlayMeterHeight{7.0};
  double runtimeOverlayHostX{330.0};
  double runtimeOverlayHostBaseline{90.0};
  double runtimeOverlayHostFontSize{9.0};
  double phase12BOverlayX{330.0};
  double phase12BOverlayTop{60.0};
  double phase12BOverlayWidth{360.0};
  double phase12BOverlayHeight{32.0};
  double phase12BTitleInsetX{10.0};
  double phase12BTitleBaseline{70.0};
  double phase12BDetailBaseline{82.0};
  double phase12BTitleFontSize{10.0};
  double phase12BDetailFontSize{9.0};
  double phase12BHandleInset{2.0};
  double phase12BHandleWidth{4.0};
  double phase12BSelectionStroke{2.0};
  double phase12BAlternativeInset{30.0};
  double phase12BAlternativeFontSize{8.0};
  double phase12BTrackControlWidth{85.0};
  double phase12BRegionControlWidth{85.0};
  double phase12BMuteControlWidth{65.0};
  double phase12BSoloControlWidth{65.0};
  double phase12BOutputControlWidth{60.0};

  [[nodiscard]] double runtimeOverlayTopPosition() const noexcept {
    return std::max(toolbarHeight, runtimeOverlayTop);
  }
  [[nodiscard]] double phase12BOverlayTopPosition() const noexcept {
    return std::max(toolbarHeight, phase12BOverlayTop);
  }
  [[nodiscard]] ui::Rect runtimeOverlayBoundsForWidth(
      double width) const noexcept {
    const auto safeWidth = std::max(0.0, width);
    const auto right = std::max(0.0, safeWidth - runtimeOverlayRightInset);
    const auto boundedWidth = std::min(runtimeOverlayWidth, right);
    const auto maxLeft = std::max(0.0, right - boundedWidth);
    const auto desiredLeft = safeWidth - runtimeOverlayRightInset -
                             runtimeOverlayWidth;
    const auto left = std::clamp(std::max(runtimeOverlayMinLeft, desiredLeft),
                                 0.0, maxLeft);
    return ui::Rect{left, runtimeOverlayTopPosition(), boundedWidth,
                    runtimeOverlayHeight};
  }
  [[nodiscard]] ui::Rect runtimeOverlayMeterBoundsForWidth(
      double width) const noexcept {
    const auto overlay = runtimeOverlayBoundsForWidth(width);
    const auto left = std::min(overlay.right(),
                               overlay.x + runtimeOverlayMeterInsetX);
    const auto available = std::max(0.0, overlay.right() - left);
    return ui::Rect{left, overlay.y + runtimeOverlayMeterTop - runtimeOverlayTop,
                    std::min(runtimeOverlayMeterWidth, available),
                    std::min(runtimeOverlayMeterHeight,
                             std::max(0.0, overlay.bottom() -
                                              (overlay.y + runtimeOverlayMeterTop -
                                               runtimeOverlayTop)))};
  }
  [[nodiscard]] ui::Rect phase12BOverlayBoundsForWidth(
      double width) const noexcept {
    const auto safeWidth = std::max(0.0, width);
    const auto boundedWidth = std::min(phase12BOverlayWidth, safeWidth);
    const auto maxLeft = std::max(0.0, safeWidth - boundedWidth);
    return ui::Rect{std::clamp(phase12BOverlayX, 0.0, maxLeft),
                    phase12BOverlayTopPosition(), boundedWidth,
                    phase12BOverlayHeight};
  }
  [[nodiscard]] double phase12BOverlayScaleForWidth(
      double width) const noexcept {
    if (phase12BOverlayWidth <= 0.0) return 0.0;
    return phase12BOverlayBoundsForWidth(width).width / phase12BOverlayWidth;
  }
  [[nodiscard]] double contentTop() const noexcept {
    return toolbarHeight + rulerHeight;
  }
  [[nodiscard]] bool compactToolbar(double width) const noexcept {
    return width < projectVisibilityThreshold;
  }
  [[nodiscard]] std::optional<ui::Rect> voiceIdentityBoundsForWidth(
      double width) const noexcept {
    if (compactToolbar(width)) return std::nullopt;
    const auto left = width - voiceIdentityRightInset - voiceIdentityWidth;
    if (left < bpmBoundsForWidth(width).right() + compactToolbarGap) {
      return std::nullopt;
    }
    return ui::Rect{left, voiceIdentityTitleTop, voiceIdentityWidth,
                    voiceIdentityTitleHeight};
  }
  [[nodiscard]] std::optional<ui::Rect> projectHeaderBoundsForWidth(
      double width, bool portraitVisible) const noexcept {
    if (width < projectVisibilityThreshold) return std::nullopt;
    const auto left = std::max(projectMinimumX, width - projectRightInset);
    auto right = portraitVisible
                     ? width - portraitRightInset - compactToolbarGap
                     : width - toolbarTitleX;
    if (const auto identity = voiceIdentityBoundsForWidth(width);
        identity.has_value()) {
      right = std::min(right, identity->x - compactToolbarGap);
    }
    if (right - left < projectMinimumTextWidth) return std::nullopt;
    return ui::Rect{left, projectTitleBaseline, right - left,
                    projectRevisionBaseline - projectTitleBaseline +
                        projectRevisionFontSize};
  }
  [[nodiscard]] std::optional<ui::Rect> compactProjectTitleBoundsForWidth(
      double width) const noexcept {
    if (!compactToolbar(width)) return std::nullopt;
    const auto right = transportBoundsForWidth(width).x - compactToolbarGap;
    const auto available = right - toolbarTitleX;
    if (available < projectMinimumTextWidth) return std::nullopt;
    return ui::Rect{toolbarTitleX,
                    toolbarSubtitleBaseline - toolbarSubtitleFontSize,
                    available, toolbarSubtitleFontSize + 1.0};
  }
  [[nodiscard]] ui::Rect transportBoundsForWidth(double width) const noexcept {
    if (!compactToolbar(width)) {
      return ui::Rect{transportX, toolbarControlTop, transportWidth,
                      toolbarControlHeight};
    }
    const auto total = compactTransportWidth + compactToolbarGap +
                       compactStopWidth + compactToolbarGap + compactBpmWidth;
    const auto left = std::max(toolbarTitleX + compactToolbarTitleReserve,
                               width - compactToolbarRightInset - total);
    return ui::Rect{left, toolbarControlTop, compactTransportWidth,
                    toolbarControlHeight};
  }
  [[nodiscard]] ui::Rect stopBoundsForWidth(double width) const noexcept {
    if (!compactToolbar(width)) {
      return ui::Rect{stopX, toolbarControlTop, stopWidth,
                      toolbarControlHeight};
    }
    const auto transport = transportBoundsForWidth(width);
    return ui::Rect{transport.right() + compactToolbarGap, toolbarControlTop,
                    compactStopWidth, toolbarControlHeight};
  }
  [[nodiscard]] ui::Rect bpmBoundsForWidth(double width) const noexcept {
    if (!compactToolbar(width)) {
      return ui::Rect{bpmX, toolbarControlTop, bpmWidth,
                      toolbarControlHeight};
    }
    const auto stop = stopBoundsForWidth(width);
    return ui::Rect{stop.right() + compactToolbarGap, toolbarControlTop,
                    compactBpmWidth, toolbarControlHeight};
  }
  [[nodiscard]] ui::Rect batchLyricsBoundsForWidth(
      double width, bool portraitVisible = true) const noexcept {
    if (compactToolbar(width)) return ui::Rect{};
    const auto bpm = bpmBoundsForWidth(width);
    const auto left = bpm.right() + compactToolbarGap;
    const auto project = projectHeaderBoundsForWidth(width, portraitVisible);
    if (project.has_value() && left + batchLyricsWidth + compactToolbarGap >
                                   project->x) {
      return ui::Rect{};
    }
    const auto identity = voiceIdentityBoundsForWidth(width);
    const auto rightLimit = identity.has_value()
                                ? identity->x - compactToolbarGap
                                : width - compactToolbarRightInset;
    if (left + batchLyricsWidth > rightLimit) {
      return ui::Rect{};
    }
    return ui::Rect{left, toolbarControlTop, batchLyricsWidth,
                    toolbarControlHeight};
  }
  [[nodiscard]] ui::Rect loopBoundsForWidth(
      double width, bool portraitVisible = true) const noexcept {
    if (compactToolbar(width)) return ui::Rect{};
    const auto batch = batchLyricsBoundsForWidth(width, portraitVisible);
    if (batch.width <= 0.0) return ui::Rect{};
    const auto left = batch.right() + compactToolbarGap;
    const auto project = projectHeaderBoundsForWidth(width, portraitVisible);
    if (project.has_value() && left + loopWidth + compactToolbarGap > project->x) {
      return ui::Rect{};
    }
    const auto identity = voiceIdentityBoundsForWidth(width);
    const auto rightLimit = identity.has_value()
                                ? identity->x - compactToolbarGap
                                : width - compactToolbarRightInset;
    if (left + loopWidth > rightLimit) {
      return ui::Rect{};
    }
    return ui::Rect{left, toolbarControlTop, loopWidth, toolbarControlHeight};
  }
  [[nodiscard]] double diagnosticHeight(bool visible) const noexcept {
    return visible ? diagnosticStripHeight : 0.0;
  }
  [[nodiscard]] ui::Rect diagnosticBounds(double width, double height,
                                          bool exportVisible) const noexcept {
    return ui::Rect{0.0,
                    height - statusHeight - exportHeight(exportVisible) -
                        diagnosticStripHeight,
                    std::max(0.0, width), diagnosticStripHeight};
  }
  [[nodiscard]] ui::Rect diagnosticActionBounds(double width, double height,
                                                bool exportVisible,
                                                std::size_t actionCount,
                                                std::size_t actionIndex) const noexcept {
    if (actionCount == 0U || actionIndex >= actionCount) return {};
    const auto panel = diagnosticBounds(width, height, exportVisible);
    const auto actionsWidth = static_cast<double>(actionCount) *
                                  diagnosticActionWidth +
                              static_cast<double>(actionCount - 1U) *
                                  diagnosticActionGap;
    return ui::Rect{
        std::max(diagnosticTextInsetX,
                 width - diagnosticTextInsetX - actionsWidth +
                     static_cast<double>(actionIndex) *
                         (diagnosticActionWidth + diagnosticActionGap)),
        panel.y + std::max(0.0, (panel.height - diagnosticActionHeight) * 0.5),
        std::max(0.0, std::min(diagnosticActionWidth,
                               width - diagnosticTextInsetX -
                                   std::max(diagnosticTextInsetX,
                                            width - diagnosticTextInsetX - actionsWidth +
                                                static_cast<double>(actionIndex) *
                                                    (diagnosticActionWidth +
                                                     diagnosticActionGap)))),
        std::min(diagnosticActionHeight, panel.height)};
  }
  [[nodiscard]] double exportHeight(bool visible) const noexcept {
    return visible ? exportStripHeight : 0.0;
  }
  [[nodiscard]] ui::Rect exportCancelBounds(double width,
                                             double height) const noexcept {
    const auto top = height - statusHeight - exportStripHeight;
    const auto left = std::max(
        exportTextInsetX,
        width - exportCancelRightInset - exportCancelWidth);
    return ui::Rect{
        left,
        top + exportCancelTop,
        std::max(0.0, std::min(exportCancelWidth,
                               width - exportCancelRightInset - left)),
        std::min(exportCancelHeight,
                 std::max(0.0, exportStripHeight - exportCancelTop))};
  }
  [[nodiscard]] double minimumPianoHeight(double logicalHeight) const noexcept {
    return std::clamp((logicalHeight - 320.0) * 0.3 + 52.0, 52.0, 100.0);
  }
  [[nodiscard]] double laneScaleForHeight(double logicalHeight,
                                          double bottomInset = 0.0) const noexcept {
    const auto available = logicalHeight - statusHeight - bottomInset -
                           contentTop() - minimumPianoHeight(logicalHeight);
    if (available >= lanesHeight()) return 1.0;
    if (available <= 0.0) return 0.0;
    return available / lanesHeight();
  }
  [[nodiscard]] double lanesHeightForHeight(
      double logicalHeight, double bottomInset = 0.0) const noexcept {
    return lanesHeight() * laneScaleForHeight(logicalHeight, bottomInset);
  }
  [[nodiscard]] double phonemeLaneHeightForHeight(
      double logicalHeight, double bottomInset = 0.0) const noexcept {
    return phonemeLaneHeight * laneScaleForHeight(logicalHeight, bottomInset);
  }
  [[nodiscard]] double unitLaneHeightForHeight(
      double logicalHeight, double bottomInset = 0.0) const noexcept {
    return unitLaneHeight * laneScaleForHeight(logicalHeight, bottomInset);
  }
  [[nodiscard]] double seamLaneHeightForHeight(
      double logicalHeight, double bottomInset = 0.0) const noexcept {
    return seamLaneHeight * laneScaleForHeight(logicalHeight, bottomInset);
  }
  [[nodiscard]] double automationLaneHeightForHeight(
      double logicalHeight, double bottomInset = 0.0) const noexcept {
    return automationLaneHeight * laneScaleForHeight(logicalHeight, bottomInset);
  }
  [[nodiscard]] double pianoBottom(double logicalHeight,
                                   double bottomInset = 0.0) const noexcept {
    return std::max(contentTop() + minimumPianoHeight(logicalHeight),
                    logicalHeight - statusHeight - bottomInset -
                        lanesHeightForHeight(logicalHeight, bottomInset));
  }
  [[nodiscard]] double pianoContentHeight(
      double logicalHeight, double bottomInset = 0.0) const noexcept {
    return std::max(1.0, pianoBottom(logicalHeight, bottomInset) - contentTop());
  }
  [[nodiscard]] double lanesHeight() const noexcept {
    return phonemeLaneHeight + unitLaneHeight + seamLaneHeight + automationLaneHeight;
  }
  [[nodiscard]] bool unitInstructionVisible(double laneHeight) const noexcept {
    return laneHeight >= compactLaneInstructionMinimumHeight;
  }
  [[nodiscard]] bool seamInstructionVisible(double laneHeight) const noexcept {
    return laneHeight >= compactLaneInstructionMinimumHeight;
  }
  [[nodiscard]] double unitCardBottomPaddingForHeight(
      double laneHeight) const noexcept {
    return unitInstructionVisible(laneHeight)
               ? unitCardBottomPadding
               : std::min(unitCardBottomPadding, compactLaneBottomPadding);
  }
  [[nodiscard]] double seamRailBottomPaddingForHeight(
      double laneHeight) const noexcept {
    return seamInstructionVisible(laneHeight)
               ? seamRailBottomPadding
               : std::min(seamRailBottomPadding, compactLaneBottomPadding);
  }
  [[nodiscard]] double phonemeContentTop(double laneTop) const noexcept {
    return laneTop + phonemeLaneContentTopInset;
  }
  [[nodiscard]] double phonemeContentHeight(double laneHeight) const noexcept {
    return std::max(1.0, laneHeight - phonemeLaneContentTopInset -
                              phonemeLaneContentBottomInset);
  }
  [[nodiscard]] double unitContentTop(double laneTop) const noexcept {
    return laneTop + unitCardTopPadding;
  }
  [[nodiscard]] double unitContentHeight(double laneHeight) const noexcept {
    return std::max(1.0, laneHeight - unitCardTopPadding -
                              unitCardBottomPaddingForHeight(laneHeight));
  }
  [[nodiscard]] double gridStrokeWidth(bool strong) const noexcept {
    return strong ? gridStrongStrokeWidth : gridWeakStrokeWidth;
  }
  [[nodiscard]] std::optional<ui::Rect> noteLabelBounds(
      const ui::Rect& noteBounds) const noexcept {
    if (noteBounds.width <= noteMinimumLabelWidth) return std::nullopt;
    return ui::Rect{noteBounds.x + noteLabelInsetX,
                    noteBounds.y + noteLabelBaselineOffset,
                    std::max(1.0, noteBounds.width - noteLabelHorizontalPadding),
                    noteFontSize};
  }
  [[nodiscard]] ui::Rect noteDetailBounds(
      const ui::Rect& noteBounds, double contentRight) const noexcept {
    const auto available = std::max(1.0, contentRight - keyboardWidth);
    const auto width = std::min(available, 360.0);
    const auto left = std::clamp(noteBounds.x, keyboardWidth,
                                 std::max(keyboardWidth, contentRight - width));
    const auto top = std::max(contentTop(), noteBounds.y - 24.0);
    return ui::Rect{left, top, width, 20.0};
  }
  [[nodiscard]] ui::Rect overlapBadgeBounds(
      const ui::Rect& groupBounds, double contentRight) const noexcept {
    const auto width = std::min(overlapBadgeWidth,
                                std::max(1.0, contentRight - keyboardWidth));
    const auto left = std::clamp(groupBounds.right() - width, keyboardWidth,
                                 std::max(keyboardWidth, contentRight - width));
    const auto above = groupBounds.y - overlapBadgeHeight - overlapBadgeGap;
    const auto top = above >= contentTop()
                         ? above
                         : groupBounds.bottom() + overlapBadgeGap;
    return ui::Rect{left, top, width, overlapBadgeHeight};
  }
  [[nodiscard]] ui::Rect overlapDetailBounds(
      const ui::Rect& groupBounds, double contentRight,
      std::size_t memberCount) const noexcept {
    const auto available = std::max(1.0, contentRight - keyboardWidth);
    const auto width = std::min(available, overlapDetailWidth);
    const auto height = overlapDetailTitleHeight +
                        static_cast<double>(memberCount) * overlapDetailRowHeight;
    const auto left = std::clamp(groupBounds.x, keyboardWidth,
                                 std::max(keyboardWidth, contentRight - width));
    const auto above = groupBounds.y - height - overlapDetailGap;
    const auto top = above >= contentTop()
                         ? above
                         : groupBounds.bottom() + overlapDetailGap;
    return ui::Rect{left, top, width, height};
  }
  [[nodiscard]] ui::Rect overlapDetailRowBounds(
      const ui::Rect& detailBounds, std::size_t index) const noexcept {
    return ui::Rect{detailBounds.x,
                    detailBounds.y + overlapDetailTitleHeight +
                        static_cast<double>(index) * overlapDetailRowHeight,
                    detailBounds.width, overlapDetailRowHeight};
  }
  [[nodiscard]] ui::Rect arrangementActionBoundsForWidth(
      double logicalWidth, std::size_t index) const noexcept {
    constexpr std::size_t actionCount = 5U;
    if (index >= actionCount) return {};
    const auto actionRight = logicalWidth - arrangementActionRightInset;
    const auto actionLeft =
        actionRight - static_cast<double>(actionCount - index) *
                           arrangementActionWidth -
        static_cast<double>(actionCount - index - 1U) *
            arrangementActionGap;
    return ui::Rect{actionLeft, toolbarHeight + arrangementActionTop,
                    arrangementActionWidth, trackRowHeight};
  }
  [[nodiscard]] TechnicalLaneGeometry technicalLaneGeometry(
      double logicalHeight, double bottomInset = 0.0) const noexcept {
    const auto piano = pianoBottom(logicalHeight, bottomInset);
    const auto phoneme = phonemeLaneHeightForHeight(logicalHeight, bottomInset);
    const auto unit = unitLaneHeightForHeight(logicalHeight, bottomInset);
    const auto seam = seamLaneHeightForHeight(logicalHeight, bottomInset);
    const auto pitch = automationLaneHeightForHeight(logicalHeight, bottomInset);
    return TechnicalLaneGeometry{
        .pianoBottom = piano,
        .phonemeTop = piano,
        .phonemeHeight = phoneme,
        .unitTop = piano + phoneme,
        .unitHeight = unit,
        .seamTop = piano + phoneme + unit,
        .seamHeight = seam,
        .pitchTop = piano + phoneme + unit + seam,
        .pitchHeight = pitch,
        .bottom = logicalHeight - statusHeight - bottomInset,
    };
  }
  [[nodiscard]] ui::Rect characterDockPortraitBounds(
      double editorRight, double contentBottom,
      double logicalWidth) const noexcept {
    const auto width = std::max(0.0, logicalWidth - editorRight);
    const auto portraitHeight = std::max(
        characterDockPortraitMinimumHeight,
        contentBottom - toolbarHeight - characterDockPortraitBottomReserve);
    return ui::Rect{editorRight + characterDockPadding,
                    toolbarHeight + characterDockPortraitTopInset,
                    std::max(0.0, width - characterDockPadding * 2.0),
                    portraitHeight};
  }
  [[nodiscard]] double characterDockMetadataTop(
      const ui::Rect& portraitBounds) const noexcept {
    return portraitBounds.bottom() + characterDockTextTopGap;
  }
  [[nodiscard]] ui::Rect microscopePanelBounds(double logicalWidth,
                                               double logicalHeight) const noexcept {
    return ui::Rect{microscopePanelInsetX, microscopePanelTop,
                    std::max(1.0, logicalWidth - microscopePanelInsetX * 2.0),
                    std::max(1.0, logicalHeight - microscopePanelTop -
                                      microscopePanelBottomInset)};
  }
  [[nodiscard]] ui::Rect microscopeWaveformBounds(
      double logicalWidth, double logicalHeight) const noexcept {
    const auto plotWidth = std::max(microscopePlotMinimumWidth,
                                    logicalWidth - microscopePlotInsetX * 2.0);
    const auto plotHeight = std::max(
        microscopeWaveformMinimumHeight,
        (logicalHeight - microscopePanelTop -
         microscopeWaveformHeightAvailableInset) *
            microscopeWaveformHeightFraction);
    return ui::Rect{microscopePlotInsetX, microscopePlotTop, plotWidth,
                    plotHeight};
  }
  [[nodiscard]] ui::Rect microscopeSpectrogramBounds(
      double logicalWidth, double logicalHeight) const noexcept {
    const auto waveform = microscopeWaveformBounds(logicalWidth, logicalHeight);
    return ui::Rect{waveform.x, waveform.bottom() + microscopePlotGap,
                    waveform.width,
                    std::max(microscopeSpectrogramMinimumHeight,
                             logicalHeight - waveform.bottom() -
                                 microscopeSpectrogramBottomInset)};
  }
  [[nodiscard]] ui::Rect microscopeCloseBounds(double logicalWidth,
                                                double logicalHeight) const noexcept {
    const auto panel = microscopePanelBounds(logicalWidth, logicalHeight);
    const auto maximumX = std::max(
        panel.x, panel.right() - microscopeCloseRightGap - 1.0);
    const auto closeX = std::clamp(
        std::max(microscopeCloseMinimumX,
                 logicalWidth - microscopeCloseRightInset),
        panel.x, maximumX);
    return ui::Rect{closeX, panel.y + microscopeCloseTop,
                    std::max(1.0, panel.right() - closeX -
                                      microscopeCloseRightGap),
                    microscopeCloseHeight};
  }
};

[[nodiscard]] TechnicalLaneHeights resolveEditorTechnicalLaneHeights(
    const EditorSceneState& state, const EditorSceneLayout& layout,
    double contentBottom) noexcept;
[[nodiscard]] bool editorDockVisible(const EditorSceneState& state) noexcept;
[[nodiscard]] double resolveEditorDockWidth(
    const EditorSceneState& state, const EditorSceneLayout& layout) noexcept;

class EditorScenePainter final {
public:
  explicit EditorScenePainter(EditorSceneTheme theme = {}) noexcept
      : theme_(theme) {}

  [[nodiscard]] EditorSceneLayout layout() const noexcept { return layout_; }
  [[nodiscard]] EditorSceneTheme theme() const noexcept { return theme_; }
  void paint(RasterCanvas& canvas, ui::PianoRollModel& model,
             const EditorSceneState& state) const noexcept;

private:
  void paintToolbar(RasterCanvas& canvas, const EditorSceneState& state) const noexcept;
  void paintGrid(RasterCanvas& canvas, const ui::PianoRollModel& model,
                 double editorRight, double contentBottom) const noexcept;
  void paintKeyboard(RasterCanvas& canvas, const ui::PianoRollModel& model,
                     double contentBottom) const noexcept;
  void paintNotes(RasterCanvas& canvas,
                  const ui::PianoRollModel& model,
                  const EditorSceneState& state) const noexcept;
  void paintEmptyPianoRoll(RasterCanvas& canvas, double editorRight,
                           double pianoBottom) const noexcept;
  void paintTechnicalLanes(RasterCanvas& canvas, const ui::PianoRollModel& model,
                           const EditorSceneState& state,
                           double editorRight) const noexcept;
  void paintCharacter(RasterCanvas& canvas, const EditorSceneState& state,
                      double editorRight, double contentBottom) const noexcept;
  void paintArrangement(RasterCanvas& canvas, const EditorSceneState& state,
                        double editorRight, double contentBottom) const noexcept;
  void paintVoicebankBrowser(RasterCanvas& canvas,
                             const EditorSceneState& state,
                             double editorRight, double contentBottom) const noexcept;
  void paintAudioSettings(RasterCanvas& canvas,
                          const EditorSceneState& state,
                          double editorRight, double contentBottom) const noexcept;
  void paintDiagnostics(RasterCanvas& canvas,
                        const EditorSceneState& state) const noexcept;
  void paintExportProgress(RasterCanvas& canvas,
                           const EditorSceneState& state) const noexcept;
  void paintSampleMicroscope(RasterCanvas& canvas,
                             const EditorSceneState& state) const noexcept;
  void paintStatus(RasterCanvas& canvas, const ui::PianoRollModel& model,
                   const EditorSceneState& state) const noexcept;

  EditorSceneTheme theme_;
  EditorSceneLayout layout_;
};

}  // namespace seam::native_ui
