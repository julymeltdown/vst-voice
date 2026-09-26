#pragma once

#include "seam/application/editor_session.hpp"
#include "seam/application/tempo_commands.hpp"
#include "seam/native_ui/tempo_meter_model.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/core/result.hpp"
#include "seam/authoring/technical_edit_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/editor_interaction_state.hpp"
#include "seam/native_ui/arrangement_panel.hpp"
#include "seam/native_ui/accessibility_tree.hpp"
#include "seam/native_ui/diagnostic_panel.hpp"
#include "seam/native_ui/track_inspector.hpp"
#include "seam/ui/text_composition_model.hpp"
#include "seam/ui/lyric_replacement_job.hpp"
#include "seam/ui/vibrato_clear_preview.hpp"
#include "seam/ui/vibrato_model.hpp"
#include "seam/ui/note_cleanup_preview.hpp"
#include "seam/ui/dynamics_clear_preview.hpp"
#include "seam/ui/dynamics_lane_model.hpp"
#include "seam/ui/dynamics_plot_viewport.hpp"
#include "seam/authoring/audio_measurement_job.hpp"
#include "seam/authoring/japanese_reading_job.hpp"
#include "seam/ui/note_search_navigation.hpp"
#include "seam/ui/note_search_job.hpp"
#include "seam/native_ui/diagnostic_search_job.hpp"
#include "seam/native_ui/vibrato_inspector.hpp"
#include "seam/native_ui/style_coverage_sheet.hpp"
#include "seam/text/unicode.hpp"
#include "seam/ui/sample_microscope_model.hpp"

#include <functional>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace seam::native_ui {

enum class PointerButton { NoButton, Left, Middle, Right };

enum class NativeKey {
  Unknown,
  Space,
  Enter,
  Tab,
  Escape,
  Delete,
  Backspace,
  Left,
  Right,
  Up,
  Down,
  L,
  Z,
  Y,
  C,
  B,
  D,
  E,
  N,
  O,
  P,
  Q,
  S,
  R,
  X,
  A,
  V,
  I,
  Plus,
  Minus,
};

struct InputModifiers final {
  bool shift{false};
  bool control{false};
  bool alt{false};
  bool command{false};

  [[nodiscard]] bool primaryShortcut() const noexcept {
    return control || command;
  }
};

struct PointerEvent final {
  ui::Point position;
  PointerButton button{PointerButton::NoButton};
  InputModifiers modifiers;
  int clickCount{1};
};

struct KeyEvent final {
  NativeKey key{NativeKey::Unknown};
  InputModifiers modifiers;
  bool repeat{false};
};

// Which geometry a text field's bounds were computed in. A presenting shell moves only NoteGrid
// anchors (note bounds from the shared piano-roll viewport); ClassicSurface anchors belong to a
// classic panel, which takes over the frame while the field is open.
enum class TextInputAnchor : std::uint8_t { ClassicSurface, NoteGrid };

struct TextInputRequest final {
  domain::LyricTokenId lyricId;
  ui::Rect logicalBounds;
  std::u32string currentText;
  TextInputAnchor anchor{TextInputAnchor::ClassicSurface};
};

struct SampleMicroscopeData final {
  voicebank::Unit unit;
  voicebank::AudioBuffer audio;
  std::string destinationContext;
};

struct EditorHostCallbacks final {
  std::function<void()> requestRepaint;
  std::function<void(const TextInputRequest&)> beginTextInput;
  std::function<void()> endTextInput;
  std::function<core::Result<void>(bool)> setPlaying;
  // Keeps host-owned render/voicebank selection aligned with the editor's
  // selected vocal track. Selection is view state and must not dirty a project.
  std::function<core::Result<void>(domain::TrackId)> selectTrack;
  std::function<void()> documentChanged;
  // Resolve controls against the selected installed singer. When absent, the
  // editor uses the conservative carrier-wide capability table.
  std::function<core::Result<void>(domain::TrackId, synthesis::RendererControl)>
      validateSingerControl;
  std::function<core::Result<void>()> stopPlaying;
  std::function<void()> cancelExport;
  std::function<core::Result<void>(time::Tick)> seekTick;
  std::function<core::Result<void>(time::Tick, time::Tick)> setLoopTicks;
  std::function<core::Result<void>()> toggleLoop;
  // Chooses what a final bounce is timed against: true follows the host's own timing, false
  // uses the score's tempo map. A surface that cannot offer the choice leaves this empty, and
  // the control is then not shown at all rather than shown doing nothing.
  std::function<core::Result<void>(bool)> setBounceTiming;
  std::function<void()> cancelRender;
  std::function<void()> retryRender;
  std::function<core::Result<void>(domain::PhonemeKey)> cycleUnitVariant;
  std::function<core::Result<void>(domain::PhonemeKey)> cycleUnitRenderer;
  std::function<core::Result<void>(domain::PitchAutomationPoint)> upsertPitchPoint;
  std::function<core::Result<void>(domain::PhonemeKey, bool,
                                   time::Microseconds)> movePhonemeBoundary;
  std::function<core::Result<void>(time::Tick,
                                   domain::PitchAutomationPoint)>
      movePitchPoint;
  std::function<core::Result<void>(time::Tick)> removePitchPoint;
  std::function<core::Result<void>(time::Tick)> cyclePitchInterpolation;
  std::function<core::Result<void>(domain::PhonemeKey, bool)> previewSeam;
  std::function<core::Result<SampleMicroscopeData>(domain::PhonemeKey)>
      loadSampleMicroscope;
  std::function<core::Result<void>(domain::PhonemeKey,
                                   const voicebank::Unit&)>
      microscopeUnitChanged;
  std::function<core::Result<void>(const voicebank::Unit&,
                                   const voicebank::AudioBuffer&)>
      playMicroscopeSample;
  std::function<core::Result<void>(std::string_view, std::string_view,
                                   std::string_view)>
      selectVoicebank;
  std::function<core::Result<void>()> refreshVoicebanks;
  std::function<core::Result<void>()> openVoicebankInstaller;
  std::function<core::Result<void>(const authoring::Diagnostic&,
                                   authoring::DiagnosticAction)> diagnosticAction;
  std::function<core::Result<void>(std::size_t)> selectSupportReport;
  std::function<void()> viewChanged;
  std::function<core::Result<void>(authoring::AudioSettings)>
      applyAudioSettings;
  std::function<std::chrono::steady_clock::time_point()> uiClock;
  std::function<bool()> reduceMotionEnabled;
  std::function<core::Result<authoring::PhonemeBindingReview>()> reviewPhonemeBindings;
  std::function<core::Result<void>(const domain::PhonemeOverride&, domain::PhonemeKey,
                                  std::string_view)> rebindPhonemeOverride;
  std::function<core::Result<authoring::RetainedRenderEditReview>()> reviewRenderEdits;
  std::function<core::Result<void>(const authoring::RetainedRenderEditReview&,
      const domain::UnitSelectionOverride&, domain::PhonemeKey)> rebindUnitOverride;
  std::function<core::Result<void>(const authoring::RetainedRenderEditReview&,
      const domain::SeamOverride&, domain::PhonemeKey)> rebindSeamOverride;
  std::function<core::Result<authoring::StagedJapaneseReadingResource>()>
      prepareJapaneseReadingResource;
  // Clears the clip latch of the host's output level publisher (the meter's clip light).
  std::function<void()> resetOutputClip;
};

class NativeEditorController final {
public:
  enum class SeamPreset { Clean, Character, PhaseAligned };

  NativeEditorController(application::EditorSession& session,
                         application::ProjectFactory& factory,
                         domain::RegionId regionId,
                         EditorHostCallbacks callbacks = {});
  [[nodiscard]] std::uint64_t instanceSerial() const noexcept { return instanceSerial_; }

  [[nodiscard]] ui::PianoRollModel& pianoRoll() noexcept { return pianoRoll_; }
  [[nodiscard]] const ui::PianoRollModel& pianoRoll() const noexcept {
    return pianoRoll_;
  }
  [[nodiscard]] EditorSceneState sceneState() const;
  [[nodiscard]] core::Result<void> openPhonemeReview();
  [[nodiscard]] core::Result<void> activatePhonemeReview(std::size_t action);
  [[nodiscard]] bool playing() const noexcept { return playing_; }
  [[nodiscard]] bool textInputActive() const noexcept { return composition_.active(); }
  [[nodiscard]] const ArrangementPanelModel& arrangementPanel() const noexcept {
    return arrangementPanel_;
  }
  [[nodiscard]] const AccessibilityTree& accessibilityTree() const noexcept {
    return accessibilityTree_;
  }
  void rebuildAccessibilityTree();
  [[nodiscard]] core::Result<void> dispatchAccessibility(
      std::string_view id, SemanticAction action);
  // The one controller focus transition: a successful focus move to anything other than a vibrato
  // handle ends the vibrato handle subfocus, whichever path (Tab, pointer, assistive, shell) moved it.
  void accessibilityFocusMoved(std::string_view id) noexcept;
  [[nodiscard]] core::Result<void> setAccessibilityValue(
      std::string_view id, std::string_view value);
  void setDiagnostics(std::vector<authoring::Diagnostic> diagnostics);
  [[nodiscard]] const DiagnosticPanelModel& diagnosticPanel() const noexcept {
    return diagnosticPanel_;
  }
  [[nodiscard]] core::Result<void> activateDiagnostic(
      std::size_t index, authoring::DiagnosticAction action) const;
  void dismissDiagnostic(std::size_t index);
  void setRecoverySupportView(RecoverySupportView view);
  [[nodiscard]] const RecoverySupportPanelModel& recoverySupportPanel()
      const noexcept {
    return recoverySupportPanel_;
  }
  [[nodiscard]] core::Result<void> selectSupportReport(std::size_t index);
  [[nodiscard]] TrackInspectorSnapshot trackInspector() const noexcept {
    return TrackInspectorModel::snapshot(session_.project(), selectedTrackId_, playheadTick_);
  }
  // Read-only document view for surfaces that show every track at once (the MIX workspace).
  // Edits still go through the controller's commands.
  [[nodiscard]] const domain::Project& project() const noexcept { return session_.project(); }
  [[nodiscard]] domain::TrackId selectedTrack() const noexcept {
    return selectedTrackId_;
  }
  [[nodiscard]] domain::RegionId selectedRegion() const noexcept {
    return regionId_;
  }
  [[nodiscard]] bool sampleMicroscopeOpen() const noexcept {
    return microscopeUnit_.has_value();
  }
  [[nodiscard]] const ui::SampleMicroscopeModel* sampleMicroscope() const noexcept {
    return microscopeUnit_.has_value() ? &microscope_ : nullptr;
  }
  [[nodiscard]] const std::string& sampleMicroscopeUnitId() const noexcept {
    return microscopeUnitId_;
  }
  [[nodiscard]] const std::string& sampleMicroscopeDestination() const noexcept {
    return microscopeDestinationContext_;
  }
  [[nodiscard]] core::Result<void> openSampleMicroscope(domain::PhonemeKey key);
  void closeSampleMicroscope() noexcept;

  [[nodiscard]] core::Result<void> selectTrack(domain::TrackId trackId);
  [[nodiscard]] core::Result<void> selectAdjacentVocalTrack(int direction);
  [[nodiscard]] core::Result<void> beginTempoEdit(time::Tick tick = time::Tick{0});
  [[nodiscard]] core::Result<void> beginHintEdit(domain::NoteId noteId);
  [[nodiscard]] core::Result<void> beginSelectedHintEdit();
  [[nodiscard]] core::Result<void> openReplacementReview(std::string query, std::string replacement);
  [[nodiscard]] core::Result<void> openDistributionReview(std::string text);
  [[nodiscard]] core::Result<void> openClearVibratoReview();
  [[nodiscard]] core::Result<void> openClearDynamicsReview();
  [[nodiscard]] core::Result<void> openDynamicsInspector();
  // The timbral channels share one drawn lane. Opening it selects the channel and captures a draft;
  // applying a gesture commits exactly one undoable edit. A singer that cannot render the channel
  // still shows what is stored, with the refusal beside it.
  [[nodiscard]] core::Result<void> openExpressionLane(ui::ExpressionChannel channel);
  [[nodiscard]] core::Result<void> cycleExpressionLane(int direction);
  [[nodiscard]] ui::ExpressionChannel selectedExpressionChannel() const noexcept {
    return expressionChannel_;
  }
  [[nodiscard]] float expressionValueAtPlayhead() const;
  [[nodiscard]] core::Result<void> nudgeExpressionLane(int steps);
  [[nodiscard]] core::Result<void> resetExpressionLaneDraft();
  [[nodiscard]] bool expressionLaneOpen() const noexcept { return expressionLaneVisible_; }
  [[nodiscard]] core::Result<void> closeExpressionLane();
  // The selected channel's curve edited in value space by a surface that draws its own graph (the
  // TUNE workspace). A press grabs the stored region-local point `grab` (Shift-style `erase` removes
  // it at once) or inserts one at `songTick`, snapped and clamped into the region as a lane click
  // is; a drag moves it; the release commits one undoable edit, and cancelPointerGesture restores
  // the curve as it was before the press.
  [[nodiscard]] core::Result<void> pressExpressionPoint(std::optional<time::Tick> grab,
                                                        time::Tick songTick, float amount,
                                                        bool erase = false);
  [[nodiscard]] core::Result<void> dragExpressionPoint(time::Tick songTick, float amount);
  [[nodiscard]] core::Result<void> releaseExpressionPoint();
  // The selected region's pitch points edited in value space by a surface that draws its own pitch
  // graph (the TUNE workspace). Ticks are region-local and snapped on the song grid as a lane click
  // is; values are cents, clamped to the stored range. Every edit reaches the host through the same
  // callbacks as the SING lane, so validation, undo and render invalidation are identical there.
  // A press grabs the stored point at `grab` or starts a new one at `regionTick`; a drag moves it
  // without touching the project; the release makes exactly one host call (upsertPitchPoint for a
  // new point, movePitchPoint for a moved one, none for a grab released in place), and
  // cancelPointerGesture drops the gesture with nothing committed. A host without the callback a
  // command needs refuses it with a reason; pitchEditRefusal() says why a surface cannot edit.
  struct PitchPointGesture final {
    std::optional<time::Tick> source;    // the grabbed stored point; empty for a new point
    domain::PitchAutomationPoint point;  // where the release puts it
  };
  [[nodiscard]] std::string pitchEditRefusal() const;
  [[nodiscard]] core::Result<void> pressPitchPoint(std::optional<time::Tick> grab,
                                                   time::Tick regionTick, float cents);
  [[nodiscard]] core::Result<void> dragPitchPoint(time::Tick regionTick, float cents);
  [[nodiscard]] core::Result<void> releasePitchPoint();
  [[nodiscard]] const std::optional<PitchPointGesture>& pitchPointGesture() const noexcept {
    return pitchGesture_;
  }
  // One host command each: remove the stored point, cycle its interpolation, or move its value by
  // `cents` in place (clamped; a step that changes nothing commits nothing).
  [[nodiscard]] core::Result<void> removePitchPointAt(time::Tick regionTick);
  [[nodiscard]] core::Result<void> cyclePitchInterpolationAt(time::Tick regionTick);
  [[nodiscard]] core::Result<void> nudgePitchPointAt(time::Tick regionTick, float cents);
  // Replaces the given vibrato fields on every selected note of the region as one undoable edit
  // (the vibrato inspector's apply, without its text fields).
  [[nodiscard]] core::Result<void> applyVibratoToSelection(const ui::VibratoFields& patch);
  [[nodiscard]] core::Result<void> openStyleCoverageSheet();
  [[nodiscard]] core::Result<void> openJapaneseReadingReview();
  void setJapaneseReadingResourceResolver(std::function<core::Result<authoring::StagedJapaneseReadingResource>()> resolver) {
    japaneseReadingResourceResolver_ = std::move(resolver);
  }
  void setStyleBankResolver(std::function<voicebank::VoicebankResolution(domain::TrackId)> resolver) {
    styleSnapshotResolver_ = {}; styleBankResolver_ = std::move(resolver);
  }
  void setStyleBankSnapshotResolver(std::function<authoring::VoicebankSnapshotPtr(domain::TrackId)> resolver) {
    styleBankResolver_ = {}; styleSnapshotResolver_ = std::move(resolver);
  }
  // Bound host coordinator must outlive this controller. Workers retain only
  // immutable captures, never this non-owning pointer.
  void setMeasurementCoordinator(const authoring::AuthoringRenderCoordinator& source) noexcept {
    measurementJob_.cancel(); measurementWindow_.reset(); measurementCoordinator_ = &source;
  }
  [[nodiscard]] core::Result<void> openNoteCleanupReview(ui::NoteCleanupKind kind);
  [[nodiscard]] core::Result<void> beginReplacementInput();
  [[nodiscard]] core::Result<void> beginFindInput();
  [[nodiscard]] core::Result<void> openVibratoInspector();
  [[nodiscard]] core::Result<void> beginDiagnosticFindInput();
  [[nodiscard]] core::Result<void> openDiagnosticFindReview(std::string query);
  [[nodiscard]] core::Result<void> repeatFind(bool backwards = false);
  [[nodiscard]] bool findPreparing() const noexcept { return findJob_.preparing() || diagnosticFindJob_.preparing(); }
  [[nodiscard]] core::Result<void> openFindReview(std::string query, ui::NoteSearchField field = ui::NoteSearchField::Lyric);
  void pollReplacementReview();
  [[nodiscard]] bool replacementReviewOpen() const noexcept { return replacementOpen_ || replacementInput_.has_value(); }
  [[nodiscard]] core::Result<void> replacementReviewAction(std::size_t action);
  [[nodiscard]] core::Result<void> openReplacementRow(std::size_t pageRow);
  [[nodiscard]] core::Result<void> beginMeterEdit(time::Tick tick = time::Tick{0});
  [[nodiscard]] core::Result<TempoMeterModel> timeMapEvents() const;
  [[nodiscard]] core::Result<void> openTimeMapPanel();
  [[nodiscard]] core::Result<void> timeMapPanelAction(std::size_t action);
  [[nodiscard]] core::Result<void> beginSelectedTimeMapEdit(const TempoMeterModel& model);
  [[nodiscard]] core::Result<void> removeSelectedTimeMapEvent(const TempoMeterModel& model);
  [[nodiscard]] core::Result<void> editTempo(std::uint64_t expectedRevision, time::Tick tick,
                                            std::optional<double> bpm);
  [[nodiscard]] core::Result<void> editMeter(std::uint64_t expectedRevision, time::Tick tick,
      std::optional<application::EditMeterCommand::Signature> signature);
  [[nodiscard]] core::Result<void> selectRegion(domain::RegionId regionId);
  [[nodiscard]] core::Result<domain::TrackId> addVocalTrack(
      std::string name);
  [[nodiscard]] core::Result<domain::RegionId> addVocalRegion(
      std::string name, time::Tick start, time::Tick duration);
  [[nodiscard]] core::Result<void> removeSelectedTrack();
  [[nodiscard]] core::Result<void> renameSelectedTrack(std::string name);
  [[nodiscard]] core::Result<void> beginSelectedTrackRename();
  [[nodiscard]] core::Result<void> reorderSelectedTrack(
      std::size_t destinationIndex);
  [[nodiscard]] core::Result<void> renameSelectedRegion(std::string name);
  [[nodiscard]] core::Result<void> beginSelectedRegionRename();
  [[nodiscard]] core::Result<void> setSelectedSeamAmount(float value);
  [[nodiscard]] core::Result<void> setSelectedSeamOverlap(
      time::Microseconds value);
  [[nodiscard]] core::Result<void> setSelectedSeamPhaseReset(float value);
  [[nodiscard]] core::Result<void> setSelectedSeamEnvelopeBlend(float value);
  [[nodiscard]] core::Result<void> cycleSelectedSeamCurve();
  [[nodiscard]] core::Result<void> applySelectedSeamPreset(SeamPreset preset);
  [[nodiscard]] core::Result<void> resetSelectedSeam();
  [[nodiscard]] core::Result<void> toggleSelectedSeamPreview();
  [[nodiscard]] core::Result<void> setSelectedUnitLoopPrint(float value);
  [[nodiscard]] core::Result<void> setSelectedUnitSourcePitchResidual(float value);
  [[nodiscard]] core::Result<void> splitSelectedRegion(time::Tick splitTick);
  [[nodiscard]] core::Result<void> duplicateSelectedTrack();
  [[nodiscard]] core::Result<void> duplicateSelectedRegion();
  [[nodiscard]] core::Result<void> copySelectedRegionToTrack(
      domain::TrackId targetTrackId);
  [[nodiscard]] core::Result<void> deleteSelectedRegion();
  [[nodiscard]] core::Result<void> moveSelectedRegion(time::Tick newStart);
  [[nodiscard]] core::Result<void> resizeSelectedRegion(time::Tick newDuration);
  [[nodiscard]] core::Result<void> setSelectedTrackMix(
      float gainDb, float pan, bool muted, bool solo);
  // Mix and routing edits addressed to any track; the editor selection is left as it is.
  [[nodiscard]] core::Result<void> setTrackMix(
      domain::TrackId trackId, float gainDb, float pan, bool muted, bool solo);
  [[nodiscard]] core::Result<void> setTrackRoute(
      domain::TrackId trackId, domain::TrackOutputRoute route);
  [[nodiscard]] core::Result<void> setSelectedTrackVoicebank(
      domain::VoicebankReference voicebank);
  [[nodiscard]] core::Result<void> setSelectedTrackRoute(
      domain::TrackOutputRoute route);
  [[nodiscard]] core::Result<domain::NoteId> duplicateSelectedNotes();
  [[nodiscard]] core::Result<void> quantizeSelectedNotes(time::Tick grid);
  [[nodiscard]] core::Result<void> setSelectedNotesSlur(bool enabled);
  [[nodiscard]] core::Result<void> setSelectedNotesMelisma();
  [[nodiscard]] core::Result<ui::LyricDistributionReport>
  distributeSelectedLyrics(std::u32string text,
                            std::optional<domain::Language> language = std::nullopt);

  void resize(double logicalWidth, double logicalHeight) noexcept;
  // A hosting shell (the EMO/SCENE SING workspace) owns every surface around the ruler and the
  // piano roll. While a hosted grid is set, pointer input reaches only the ruler and the piano roll
  // above pianoBottom (window coordinates); the legacy dock, diagnostic strip, export strip and
  // technical lanes cannot be hit by points the shell forwards. std::nullopt restores the legacy
  // window geometry.
  struct HostedGeometry final {
    // Window y where the forwarded piano roll ends (legacy window coordinates).
    double pianoBottom{0.0};
    // The hosted expression lane sits directly below pianoBottom with this height; the phoneme,
    // unit and seam lanes are not hosted.
    double laneHeight{0.0};
    // Fraction of the hosted lane height an expression curve spans (the classic lane uses
    // EditorSceneLayout::pitchAutomationVerticalScale). Paint and hit-testing share it.
    static constexpr double kExpressionVerticalScale = 0.84;
    friend bool operator==(const HostedGeometry&, const HostedGeometry&) = default;
  };
  void setHostedGrid(std::optional<HostedGeometry> geometry) noexcept { hosted_ = geometry; }
  [[nodiscard]] std::optional<HostedGeometry> hostedGrid() const noexcept { return hosted_; }
  // Abandons any pointer gesture in progress without committing it: note move/resize previews,
  // box selection, vibrato/pitch/phoneme/microscope drags and an expression-point drag (whose
  // draft is restored to its state before the gesture). Used on Escape, surface switches, resizes
  // and capture loss.
  void cancelPointerGesture();
  // True while a surface the SING shell does not host is open: voice browser, audio settings,
  // support panel, replacement review, tempo/meter map or its input, hint/replacement input,
  // sample microscope, phoneme review, or a track/region rename field (anchored in the classic
  // arrangement dock). Mirrors SingShell::legacySurfaceRequired(sceneState()) without building
  // the scene state.
  [[nodiscard]] bool legacyModalSurfaceActive() const;
  [[nodiscard]] std::uint64_t documentRevision() const noexcept;
  [[nodiscard]] bool pointerGestureActive() const noexcept;
  [[nodiscard]] core::Result<void> pointerDown(const PointerEvent& event);
  [[nodiscard]] core::Result<void> pointerMove(const PointerEvent& event);
  [[nodiscard]] core::Result<void> pointerUp(const PointerEvent& event);
  [[nodiscard]] core::Result<void> keyDown(const KeyEvent& event);
  void scroll(double deltaX, double deltaY, ui::Point anchor,
              InputModifiers modifiers) noexcept;

  [[nodiscard]] core::Result<void> beginLyricEdit(domain::NoteId noteId);
  [[nodiscard]] core::Result<void> updateTextComposition(
      std::u32string text, ui::CompositionSelection selection);
  [[nodiscard]] core::Result<void> commitTextComposition(std::u32string text);
  void cancelTextComposition() noexcept;

  void setAudioState(bool online, std::string backend);
  void setPlaying(bool playing) noexcept { playing_ = playing; }
  void setLoopEnabled(bool enabled) noexcept { loopEnabled_ = enabled; }
  void setBounceFollowHost(bool followHost) noexcept { bounceFollowHost_ = followHost; }
  void setRenderStatus(RenderStatusView status) noexcept;
  void setExportProgress(authoring::ExportProgress progress) noexcept {
    exportProgress_ = std::move(progress);
  }
  [[nodiscard]] const authoring::ExportProgress& exportProgress() const noexcept {
    return exportProgress_;
  }
  void setLastExport(std::optional<authoring::ExportResult> result) {
    lastExport_ = std::move(result);
  }
  [[nodiscard]] const RenderStatusPanelModel& renderStatus() const noexcept {
    return renderStatus_;
  }
  void setDirty(bool dirty) noexcept;
  void setPlayheadPixel(double value) noexcept;
  // Where the transport is, in musical time. The host already computes this to place the playhead, and
  // an edit that lands on the playhead needs the tick rather than the pixel.
  void setPlayheadTick(time::Tick tick) noexcept { playheadTick_ = tick; }
  [[nodiscard]] time::Tick playheadTick() const noexcept { return playheadTick_; }
  // The formant channel's editing surface. A singer whose carrier cannot move its own resonances
  // refuses the edit and says which resource change would allow it; a refused edit never touches a
  // curve that is already stored.
  [[nodiscard]] core::Result<void> nudgeFormantShift(int steps);
  [[nodiscard]] core::Result<void> resetFormantCurve();
  [[nodiscard]] float formantShiftAtPlayhead() const noexcept;
  // The breathiness channel's editing surface. It is the same capability decision one layer down: a
  // carrier that does not generate the excitation has nothing to rebalance, so it refuses by name and
  // leaves the stored curve alone.
  [[nodiscard]] core::Result<void> nudgeBreathiness(int steps);
  [[nodiscard]] core::Result<void> resetBreathinessCurve();
  [[nodiscard]] float breathinessAtPlayhead() const noexcept;
  // And the third source-side channel. Tension is a spectrum rather than a level, so it is exposed and
  // refused exactly like its neighbours instead of being folded into a gain.
  [[nodiscard]] core::Result<void> nudgeTension(int steps);
  [[nodiscard]] core::Result<void> resetTensionCurve();
  [[nodiscard]] float tensionAtPlayhead() const noexcept;
  // And the fourth source-side channel. Airiness is a band of the source's own noise rather than a
  // balance, so it is exposed as its own curve beside breathiness instead of being folded into it.
  [[nodiscard]] core::Result<void> nudgeAiriness(int steps);
  [[nodiscard]] core::Result<void> resetAirinessCurve();
  [[nodiscard]] float airinessAtPlayhead() const noexcept;
  // The coupled channel: a gender nudge moves the tract and the source together, so it is refused by any
  // carrier that owns only one of them.
  [[nodiscard]] core::Result<void> nudgeGender(int steps);
  [[nodiscard]] core::Result<void> resetGenderCurve();
  [[nodiscard]] float genderAtPlayhead() const noexcept;
  // The roughness channel: a subharmonic lock on the source the carrier generates, so a carrier without
  // its own excitation refuses it.
  [[nodiscard]] core::Result<void> nudgeGrowl(int steps);
  [[nodiscard]] core::Result<void> resetGrowlCurve();
  [[nodiscard]] float growlAtPlayhead() const noexcept;
  void setCharacterMetadata(std::string name, std::string style) {
    characterName_ = std::move(name);
    characterStyle_ = std::move(style);
  }
  void setCharacterPortrait(const PixelSurface* portrait) noexcept {
    characterPortrait_ = portrait;
  }
  // The dock's performance read model for the phrase that is published. The controller adds the
  // host's reduced-motion preference when it builds the scene, because that setting belongs to the
  // presentation rather than to the phrase.
  void setCharacterPerformance(EditorSceneState::CharacterPerformanceView view) {
    characterPerformance_ = view;
  }
  void clearCharacterPerformance() noexcept { characterPerformance_.reset(); }
  void setCharacterBinding(VoiceIdentityInput::CharacterBinding binding) {
    characterBinding_ = std::move(binding);
  }
  void setVoicebankCards(std::vector<authoring::VoicebankCard> cards) {
    voicebankCards_ = std::move(cards);
  }
  [[nodiscard]] bool voicebankBrowserVisible() const noexcept {
    return voicebankBrowserVisible_;
  }
  void showVoicebankBrowser() noexcept {
    voicebankBrowserVisible_ = true;
    audioSettings_.visible = false;
    repaint();
  }
  void setAudioSettings(
      authoring::AudioSettings settings,
      std::vector<EditorSceneState::AudioDeviceOption> devices,
      std::uint64_t underflowFrames, std::uint64_t xruns) {
    audioSettings_.current = std::move(settings);
    audioSettings_.devices = std::move(devices);
    audioSettings_.underflowFrames = underflowFrames;
    audioSettings_.xruns = xruns;
    audioSettings_.reported = true;
  }
  // The host's measured output level (UI thread, from what its audio thread published). An empty
  // value means nothing is measured, and meters show their empty scale.
  void setOutputLevel(std::optional<EditorSceneState::OutputLevel> level) {
    outputLevel_ = std::move(level);
  }
  // The creator acknowledged a clip: the shown flag clears now and the host's latch is reset,
  // so the next measured reading does not raise it again from the old sample.
  void resetOutputClip() {
    if (outputLevel_.has_value()) outputLevel_->clipped = false;
    if (callbacks_.resetOutputClip) callbacks_.resetOutputClip();
    repaint();
  }
  void showAudioSettings() noexcept {
    audioSettings_.visible = true;
    voicebankBrowserVisible_ = false;
    repaint();
  }
  [[nodiscard]] bool audioSettingsVisible() const noexcept {
    return audioSettings_.visible;
  }

private:
  [[nodiscard]] core::Result<void> dispatchAccessibilityAction(
      std::string_view id, SemanticAction action);
  enum class DragMode {
    None,
    MoveNotes,
    ResizeNotes,
    BoxSelect,
    RulerSeek,
    MovePhonemeBoundary,
    MovePitchPoint,
    MicroscopeMarker,
    MicroscopePitchMark,
    MoveExpressionPoint,
    EditPitchPoint,
  };
  struct VibratoHandleDrag final {
    domain::NoteId noteId;
    VibratoHandleKind kind{VibratoHandleKind::Onset};
    ui::Rect bounds;
    ui::Point handleStart;
    domain::NoteVibrato source;
    domain::NoteVibrato preview;
    std::uint64_t revision{0U};
    double logicalWidth{0.0};
    double logicalHeight{0.0};
    double activeDurationMilliseconds{0.0};
  };

  [[nodiscard]] ui::Point modelPoint(ui::Point windowPoint) const noexcept;
  // Window x of tick zero's column origin: the legacy keyboard edge, or the hosted viewport's.
  [[nodiscard]] double timelineOriginX() const noexcept;
  // Region automation is region-local; the timeline and the playhead are absolute song ticks.
  [[nodiscard]] time::Tick automationOriginTick() const noexcept;
  [[nodiscard]] double laneX(time::Tick regionTick) const noexcept;
  [[nodiscard]] time::Tick laneTickAt(double x) const;
  [[nodiscard]] time::Tick regionPlayheadClamped() const noexcept;
  // The region-local playhead for an edit "at the playhead"; refused while the playhead is outside
  // the region (reads may clamp for display, writes never move to the region edge).
  [[nodiscard]] core::Result<time::Tick> regionPlayheadForEdit() const;
  [[nodiscard]] std::optional<ui::Rect> noteWindowBounds(domain::NoteId noteId) const;
  [[nodiscard]] std::optional<domain::PitchAutomationPoint> pitchPointAt(
      ui::Point point, double automationTop, double automationHeight) const;
  void repaint() const;
  void finishTextInput() const;
  [[nodiscard]] core::Result<void> rebuildSampleMicroscope();
  [[nodiscard]] core::Result<void> rebuildMicroscopeDetails();
  [[nodiscard]] core::Result<void> microscopeDetailsAction(std::size_t action);
  [[nodiscard]] core::Result<domain::SeamOverride> selectedSeamValue() const;
  [[nodiscard]] core::Result<void> commitSeam(domain::SeamOverride value);
  [[nodiscard]] core::Result<domain::UnitSelectionOverride>
  selectedUnitValue() const;
  [[nodiscard]] core::Result<void> commitUnitSelection(
      domain::UnitSelectionOverride value);
  [[nodiscard]] core::Result<void> reorderSelectedTrackBy(int direction);
  void markDocumentChanged();
  // The timbral channels are stored through the session's performance-result path, which the
  // authoring runtime does not observe. Their commands therefore have to announce their own edit or
  // the project changes while the renderer is never asked to follow: the creator's nudge would be
  // saved and never heard. One place, so no future channel can be added without it.
  [[nodiscard]] core::Result<void> commitTimbralEdit(core::Result<void> result);
  [[nodiscard]] std::string timeMapSemanticPrefix() const;
  [[nodiscard]] std::string hintSemanticPrefix() const;
  [[nodiscard]] std::string replacementSemanticPrefix() const;
  [[nodiscard]] ReplacementReviewView replacementReviewView() const;
  [[nodiscard]] core::Result<void> openLyricReview(std::string query, std::string replacement, bool distribution);
  [[nodiscard]] core::Result<void> refreshClearVibratoReview();
  [[nodiscard]] core::Result<void> refreshClearDynamicsReview();
  [[nodiscard]] core::Result<void> beginDynamicsFieldInput(bool tick);
  [[nodiscard]] core::Result<domain::DynamicsAutomationPoint> dynamicsPointValue() const;
  [[nodiscard]] core::Result<void> dragDynamicsPoint(ui::Point position);
  [[nodiscard]] core::Result<void> dragVibratoHandle(ui::Point position);
  [[nodiscard]] core::Result<void> finishVibratoHandleDrag();
  [[nodiscard]] std::vector<VibratoHandleKind> availableVibratoHandles() const;
  [[nodiscard]] core::Result<void> focusVibratoHandle(int direction);
  [[nodiscard]] core::Result<void> adjustFocusedVibratoHandle(int direction);
  [[nodiscard]] core::Result<void> navigateDynamics(ui::DynamicsPlotViewport::Action action, double anchor = 0.5);
  [[nodiscard]] core::Result<void> cycleMeasuredChannel();
  void pollAudioMeasurement();
  [[nodiscard]] core::Result<void> revealFindNote(domain::NoteId noteId);
  [[nodiscard]] core::Result<void> beginSearchInput(bool findOnly, bool diagnostics);
  [[nodiscard]] core::Result<void> beginVibratoFieldInput(VibratoField field);
  [[nodiscard]] core::Result<void> refreshNoteCleanupReview(ui::NoteCleanupKind kind);
  [[nodiscard]] core::Result<void> navigateLyricEdit(int direction);
  [[nodiscard]] core::Result<void> beginBatchLyricEdit();
  [[nodiscard]] core::Result<void> cycleSelectedTrackRoute();
  enum class AudioSettingsField { SampleRate, BlockFrames, Channels };
  [[nodiscard]] core::Result<void> cycleAudioSettings(
      AudioSettingsField field, int direction);
  [[nodiscard]] core::Result<void> selectAudioDevice(std::size_t index);
  [[nodiscard]] std::chrono::steady_clock::time_point uiNow() const noexcept;
  [[nodiscard]] bool reduceMotionEnabled() const noexcept;
  void beginLayoutTransition(const EditorSceneState& fromState);
  void applyLayoutTransition(EditorSceneState& state) const;
  void syncInteractionToAccessibilityFocus();

  struct LayoutTransitionState final {
    std::array<double, 4U> fromLaneHeights{};
    double fromDockWidth{0.0};
    std::chrono::steady_clock::time_point startedAt{};
  };

  const std::uint64_t instanceSerial_;
  application::EditorSession& session_;
  application::ProjectFactory& factory_;
  domain::TrackId selectedTrackId_{};
  domain::RegionId regionId_;
  ui::PianoRollModel pianoRoll_;
  ui::TextCompositionModel composition_;
  EditorSceneLayout layout_;
  EditorHostCallbacks callbacks_;
  DragMode dragMode_{DragMode::None};
  std::optional<VibratoHandleDrag> vibratoHandleDrag_;
  std::optional<VibratoHandleKind> vibratoKeyboardFocus_;
  ui::Point dragStart_;
  ui::Point dragCurrent_;
  bool dragAdditive_{false};
  std::optional<domain::PhonemeKey> dragPhoneme_;
  bool dragPhonemeStart_{true};
  std::optional<time::Tick> dragPitchTick_;
  std::optional<ui::AcousticMarkerKind> dragMicroscopeMarker_;
  std::optional<std::size_t> dragMicroscopePitchMark_;
  std::optional<domain::TrackId> renameTrackTarget_;
  std::optional<TempoEditContext> tempoEdit_;
  struct HintEditContext {
    domain::ProjectId projectId;
    domain::RegionId regionId;
    domain::NoteId noteId;
    std::uint64_t revision;
    domain::Language language;
    std::optional<std::string> before;
  };
  std::optional<HintEditContext> hintEdit_;
  std::string hintEditError_;
  std::uint64_t hintInteraction_{0U};
  struct ReplacementInput final {
    application::PerformanceJobContext context;
    domain::RegionId regionId;
    std::uint64_t revision;
    std::optional<std::string> query;
    bool findOnly{false};
    bool diagnostics{false};
    std::optional<VibratoField> vibratoField{};
    std::optional<bool> dynamicsTickField{};
  };
  std::optional<ReplacementInput> replacementInput_;
  std::uint64_t replacementInputSerial_{0U};
  std::string replacementInputError_;
  ui::LyricReplacementJob replacementJob_;
  std::optional<VibratoInspectorDraft> vibratoDraft_;
  std::optional<StyleCoverageSheet> styleDraft_;
  bool styleIssues_{false};
  bool styleBlendMode_{false};
  bool styleBlendChoosingSecondary_{false};
  std::optional<std::size_t> styleIssue_;
  std::vector<std::string> styleDetailLines_;
  std::function<voicebank::VoicebankResolution(domain::TrackId)> styleBankResolver_;
  std::function<authoring::VoicebankSnapshotPtr(domain::TrackId)> styleSnapshotResolver_;
  [[nodiscard]] bool styleSourceCurrent() const;
  std::function<core::Result<authoring::StagedJapaneseReadingResource>()> japaneseReadingResourceResolver_;
  authoring::JapaneseReadingJob japaneseReadingJob_;
  std::optional<phonemizer::JapaneseReadingIdentity> japaneseReadingIdentity_;
  bool japaneseReadingMode_{false};
  std::optional<std::size_t> japaneseReadingDetail_;
  std::vector<std::string> japaneseReadingDetailLines_;
  std::optional<ui::DynamicsLaneModel> dynamicsDraft_;
  // The channel lane is persistent: the selected channel stays drawn after a gesture is committed,
  // and a draft exists only while a gesture is in progress.
  ui::ExpressionChannel expressionChannel_{ui::ExpressionChannel::Formant};
  bool expressionLaneVisible_{false};
  std::optional<ui::ExpressionLaneModel> expressionDraft_;
  std::optional<time::Tick> expressionDragTick_;
  [[nodiscard]] core::Result<ui::ExpressionLaneModel*> ensureExpressionDraft();
  [[nodiscard]] core::Result<void> commitExpressionDraft();
  [[nodiscard]] core::Result<void> beginExpressionGesture(ui::Point position,
                                                          double automationTop,
                                                          double automationHeight,
                                                          const PointerEvent& event);
  [[nodiscard]] core::Result<void> updateExpressionGesture(ui::Point position,
                                                           double automationTop,
                                                           double automationHeight);
  [[nodiscard]] core::Result<void> endExpressionGesture();
  [[nodiscard]] std::optional<time::Tick> expressionPointAt(ui::Point point,
                                                            double automationTop,
                                                            double automationHeight) const;
  [[nodiscard]] time::Tick expressionTickAt(double x) const;
  [[nodiscard]] float expressionAmountAt(double y, double automationTop,
                                         double automationHeight) const;
  struct DynamicsPointEdit final {
    std::optional<time::Tick> source;
    std::string tickText{"0"};
    std::string gainText{"1"};
  };
  std::optional<DynamicsPointEdit> dynamicsPointEdit_;
  bool dynamicsGainDragging_{false};
  ui::Rect dynamicsDragBounds_{};
  bool dynamicsTimeDragging_{false};
  ui::DynamicsPlotViewport::Range dynamicsDragRange_{0, 1};
  ui::DynamicsPlotViewport dynamicsViewport_;
  double dynamicsScrollRemainder_{0.0};
  bool dynamicsScrollZoom_{false};
  const authoring::AuthoringRenderCoordinator* measurementCoordinator_{nullptr};
  authoring::AudioMeasurementJob measurementJob_;
  struct MeasurementWindow final {
    std::shared_ptr<const authoring::RenderPublicationIdentity> identity;
    std::uint64_t requestId;
    domain::RegionId region;
    std::size_t first, count;
    friend bool operator==(const MeasurementWindow&, const MeasurementWindow&) = default;
  };
  std::optional<MeasurementWindow> measurementWindow_;
  std::size_t measuredChannel_{0U}; // Zero is controls; other values are one-based output channels.
  std::string measurementStatus_{"Waiting for a current render"};
  VibratoField selectedVibratoField_{VibratoField::Enabled};
  bool replacementOpen_{false};
  bool replacementDependencies_{false};
  bool replacementDistribution_{false};
  std::optional<ui::VibratoClearPreview> clearVibrato_;
  std::optional<ui::DynamicsClearPreview> clearDynamics_;
  std::optional<ui::NoteCleanupPreview> noteCleanup_;
  bool findMode_{false};
  ui::NoteSearchJob findJob_;
  bool diagnosticFindMode_{false};
  DiagnosticSearchJob diagnosticFindJob_;
  std::optional<DiagnosticSearchReview> diagnosticFindReview_;
  ui::NoteSearchField findField_{ui::NoteSearchField::Lyric};
  std::optional<ui::NoteSearchNavigation> findNavigation_;
  std::optional<std::size_t> findDetailIndex_;
  std::size_t replacementPage_{0U};
  std::uint64_t replacementInteraction_{0U};
  domain::RegionId replacementRegion_;
  std::string replacementQuery_, replacementText_, replacementError_;
  std::string replacementErrorContext_;
  struct ReplacementDetail final {
    domain::LyricTokenId lyricId;
    std::array<std::string, 2U> text;
    std::array<std::vector<seam::text::Utf8LineRange>, 2U> lines;
    std::size_t side{0U}, page{0U};
  };
  std::optional<ReplacementDetail> replacementDetail_;
  std::optional<TempoMeterModel> timeMapPanel_;
  std::size_t timeMapPage_{0U};
  std::uint64_t timeMapInteraction_{0U};
  std::optional<domain::RegionId> renameRegionTarget_;
  struct BatchLyricContext final {
    application::PerformanceJobContext context;
    domain::RegionId regionId;
    std::uint64_t revision;
    std::vector<domain::NoteId> notes;
  };
  std::optional<BatchLyricContext> batchLyricTarget_;
  std::optional<domain::PhonemeKey> seamTarget_;
  std::optional<domain::PhonemeKey> unitTarget_;
  std::optional<authoring::PhonemeBindingReview> phonemeReview_;
  std::optional<authoring::RetainedRenderEditReview> renderEditReview_;
  [[nodiscard]] std::size_t reviewEditCount() const;
  [[nodiscard]] domain::PhonemeKey reviewEditKey() const;
  [[nodiscard]] const std::vector<domain::PhonemeToken>& reviewTargets() const;
  std::size_t reviewedEdit_{0U};
  std::optional<std::size_t> reviewedTarget_;
  std::string reviewStatus_;
  bool seamPreviewAlternate_{false};
  std::optional<time::Tick> loopAnchorTick_;
  bool playing_{false};
  bool loopEnabled_{false};
  // Mirrors the project's bounce timing authority so the toolbar control reports what the
  // project actually says; the surface sets it whenever it adopts a project or changes it.
  bool bounceFollowHost_{false};
  bool dirty_{false};
  bool audioOnline_{false};
  std::string audioBackend_{"OFFLINE"};
  RenderStatusPanelModel renderStatus_;
  std::chrono::steady_clock::time_point voiceCompleteUntil_{};
  authoring::ExportProgress exportProgress_;
  std::optional<authoring::ExportResult> lastExport_;
  double logicalWidth_{1440.0};
  double logicalHeight_{900.0};
  std::optional<HostedGeometry> hosted_;
  std::optional<std::vector<ui::ExpressionPoint>> expressionGestureSnapshot_;
  // A TUNE pitch-point gesture in progress (DragMode::EditPitchPoint) and the document and region
  // it began on; its release commits only against those.
  std::optional<PitchPointGesture> pitchGesture_;
  std::uint64_t pitchGestureRevision_{0U};
  domain::RegionId pitchGestureRegion_{};
  double playheadPixel_{0.0};
  time::Tick playheadTick_{0};
  std::string characterName_;
  std::string characterStyle_;
  const PixelSurface* characterPortrait_{nullptr};
  std::optional<EditorSceneState::CharacterPerformanceView> characterPerformance_;
  std::optional<VoiceIdentityInput::CharacterBinding> characterBinding_;
  bool voicebankBrowserVisible_{false};
  std::vector<authoring::VoicebankCard> voicebankCards_;
  EditorSceneState::AudioSettingsView audioSettings_;
  std::optional<EditorSceneState::OutputLevel> outputLevel_;
  ArrangementPanelModel arrangementPanel_;
  AccessibilityTree accessibilityTree_;
  DiagnosticPanelModel diagnosticPanel_;
  RecoverySupportPanelModel recoverySupportPanel_;
  std::optional<voicebank::Unit> microscopeUnit_;
  voicebank::AudioBuffer microscopeAudio_;
  ui::SampleMicroscopeModel microscope_;
  std::string microscopeUnitId_;
  std::string microscopeDestinationContext_;
  std::string microscopeDetailsText_;
  std::vector<text::Utf8LineRange> microscopeDetailsLines_;
  std::size_t microscopeDetailsPage_{0U};
  std::size_t microscopeDetailsRows_{1U};
  bool microscopeDetailsVisible_{false};
  std::optional<domain::PhonemeKey> microscopeKey_;
  EditorInteractionState interaction_;
  std::optional<EditorSceneState::OverlapDetail> overlapDetail_;
  std::optional<LayoutTransitionState> layoutTransition_;
};

}  // namespace seam::native_ui
