#pragma once

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/core/result.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/arrangement_panel.hpp"
#include "seam/native_ui/accessibility_tree.hpp"
#include "seam/native_ui/diagnostic_panel.hpp"
#include "seam/native_ui/track_inspector.hpp"
#include "seam/ui/text_composition_model.hpp"
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

struct TextInputRequest final {
  domain::LyricTokenId lyricId;
  ui::Rect logicalBounds;
  std::u32string currentText;
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
  std::function<void()> documentChanged;
  std::function<core::Result<void>()> stopPlaying;
  std::function<void()> cancelExport;
  std::function<core::Result<void>(time::Tick)> seekTick;
  std::function<core::Result<void>(time::Tick, time::Tick)> setLoopTicks;
  std::function<core::Result<void>()> toggleLoop;
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
  std::function<core::Result<void>(const authoring::Diagnostic&,
                                   authoring::DiagnosticAction)> diagnosticAction;
  std::function<void()> viewChanged;
  std::function<core::Result<void>(authoring::AudioSettings)>
      applyAudioSettings;
};

class NativeEditorController final {
public:
  enum class SeamPreset { Clean, Character, PhaseAligned };

  NativeEditorController(application::EditorSession& session,
                         application::ProjectFactory& factory,
                         domain::RegionId regionId,
                         EditorHostCallbacks callbacks = {});

  [[nodiscard]] ui::PianoRollModel& pianoRoll() noexcept { return pianoRoll_; }
  [[nodiscard]] const ui::PianoRollModel& pianoRoll() const noexcept {
    return pianoRoll_;
  }
  [[nodiscard]] EditorSceneState sceneState() const;
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
  [[nodiscard]] core::Result<void> setAccessibilityValue(
      std::string_view id, std::string_view value);
  void setDiagnostics(std::vector<authoring::Diagnostic> diagnostics);
  [[nodiscard]] const DiagnosticPanelModel& diagnosticPanel() const noexcept {
    return diagnosticPanel_;
  }
  [[nodiscard]] core::Result<void> activateDiagnostic(
      std::size_t index, authoring::DiagnosticAction action) const;
  void dismissDiagnostic(std::size_t index);
  [[nodiscard]] TrackInspectorSnapshot trackInspector() const noexcept {
    return TrackInspectorModel::snapshot(session_.project(), selectedTrackId_);
  }
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
                            domain::Language language = domain::Language::Unspecified);

  void resize(double logicalWidth, double logicalHeight) noexcept;
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
  void setRenderStatus(RenderStatusView status) noexcept;
  void setExportProgress(authoring::ExportProgress progress) noexcept {
    exportProgress_ = std::move(progress);
  }
  void setLastExport(std::optional<authoring::ExportResult> result) {
    lastExport_ = std::move(result);
  }
  [[nodiscard]] const RenderStatusPanelModel& renderStatus() const noexcept {
    return renderStatus_;
  }
  void setDirty(bool dirty) noexcept;
  void setPlayheadPixel(double value) noexcept;
  void setCharacterMetadata(std::string name, std::string style) {
    characterName_ = std::move(name);
    characterStyle_ = std::move(style);
  }
  void setCharacterPortrait(const PixelSurface* portrait) noexcept {
    characterPortrait_ = portrait;
  }
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
  };

  [[nodiscard]] ui::Point modelPoint(ui::Point windowPoint) const noexcept;
  [[nodiscard]] std::optional<ui::Rect> noteWindowBounds(domain::NoteId noteId) const;
  [[nodiscard]] std::optional<domain::PitchAutomationPoint> pitchPointAt(
      ui::Point point, double automationTop, double automationHeight) const;
  void repaint() const;
  void finishTextInput() const;
  [[nodiscard]] core::Result<void> rebuildSampleMicroscope();
  [[nodiscard]] core::Result<domain::SeamOverride> selectedSeamValue() const;
  [[nodiscard]] core::Result<void> commitSeam(domain::SeamOverride value);
  [[nodiscard]] core::Result<domain::UnitSelectionOverride>
  selectedUnitValue() const;
  [[nodiscard]] core::Result<void> commitUnitSelection(
      domain::UnitSelectionOverride value);
  [[nodiscard]] core::Result<void> reorderSelectedTrackBy(int direction);
  void markDocumentChanged();
  [[nodiscard]] core::Result<void> navigateLyricEdit(int direction);
  [[nodiscard]] core::Result<void> beginBatchLyricEdit();
  [[nodiscard]] core::Result<void> cycleSelectedTrackRoute();
  enum class AudioSettingsField { SampleRate, BlockFrames, Channels };
  [[nodiscard]] core::Result<void> cycleAudioSettings(
      AudioSettingsField field, int direction);
  [[nodiscard]] core::Result<void> selectAudioDevice(std::size_t index);

  application::EditorSession& session_;
  application::ProjectFactory& factory_;
  domain::TrackId selectedTrackId_{};
  domain::RegionId regionId_;
  ui::PianoRollModel pianoRoll_;
  ui::TextCompositionModel composition_;
  EditorSceneLayout layout_;
  EditorHostCallbacks callbacks_;
  DragMode dragMode_{DragMode::None};
  ui::Point dragStart_;
  ui::Point dragCurrent_;
  bool dragAdditive_{false};
  std::optional<domain::PhonemeKey> dragPhoneme_;
  bool dragPhonemeStart_{true};
  std::optional<time::Tick> dragPitchTick_;
  std::optional<ui::AcousticMarkerKind> dragMicroscopeMarker_;
  std::optional<std::size_t> dragMicroscopePitchMark_;
  std::optional<domain::TrackId> renameTrackTarget_;
  std::optional<domain::RegionId> renameRegionTarget_;
  bool batchLyricTarget_{false};
  std::optional<domain::PhonemeKey> seamTarget_;
  std::optional<domain::PhonemeKey> unitTarget_;
  bool seamPreviewAlternate_{false};
  std::optional<time::Tick> loopAnchorTick_;
  bool playing_{false};
  bool loopEnabled_{false};
  bool dirty_{false};
  bool audioOnline_{false};
  std::string audioBackend_{"OFFLINE"};
  RenderStatusPanelModel renderStatus_;
  std::chrono::steady_clock::time_point voiceCompleteUntil_{};
  authoring::ExportProgress exportProgress_;
  std::optional<authoring::ExportResult> lastExport_;
  double logicalWidth_{1440.0};
  double logicalHeight_{900.0};
  double playheadPixel_{0.0};
  std::string characterName_;
  std::string characterStyle_;
  const PixelSurface* characterPortrait_{nullptr};
  std::optional<VoiceIdentityInput::CharacterBinding> characterBinding_;
  bool voicebankBrowserVisible_{false};
  std::vector<authoring::VoicebankCard> voicebankCards_;
  EditorSceneState::AudioSettingsView audioSettings_;
  ArrangementPanelModel arrangementPanel_;
  AccessibilityTree accessibilityTree_;
  DiagnosticPanelModel diagnosticPanel_;
  std::optional<voicebank::Unit> microscopeUnit_;
  voicebank::AudioBuffer microscopeAudio_;
  ui::SampleMicroscopeModel microscope_;
  std::string microscopeUnitId_;
  std::string microscopeDestinationContext_;
  std::optional<domain::PhonemeKey> microscopeKey_;
};

}  // namespace seam::native_ui
