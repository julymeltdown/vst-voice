#pragma once

#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/authoring/interchange_service.hpp"
#include "seam/authoring/authoring_runtime.hpp"
#include "seam/authoring/voicebank_browser.hpp"
#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/clap_editor/host_timeline.hpp"
#include "seam/clap_editor/host_tempo_map.hpp"
#include "seam/clap_editor/offline_render_session.hpp"
#include "seam/clap_editor/prepared_host_timeline.hpp"
#include "seam/native_ui/character_presentation.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/live_voice/voice_engine.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/project_renderer.hpp"
#include "seam/synthesis/unit_selection.hpp"
#include "seam/ui/phoneme_lane_model.hpp"
#include "seam/ui/sample_microscope_model.hpp"
#include "seam/ui/unit_lane_model.hpp"
#include "seam/voicebank/catalog.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace seam::clap_editor {

enum class PreviewStatus {
  Empty,
  Ready,
  VoicebankMissing,
  VoicebankVersionMismatch,
  VoicebankContentHashMissing,
  VoicebankContentMismatch,
  VoicebankUntrusted,
  Failed,
};

struct RenderedPreview final {
  std::uint32_t sampleRate{48000U};
  std::uint64_t revision{0U};
  PreviewStatus status{PreviewStatus::Empty};
  rendering::SharedPcmBuffer stereo;
  std::uint8_t channelCount{2U};
  rendering::SharedPcmBuffer interleaved;
  std::string diagnostic;
  std::string voicebankId;
  std::string voicebankVersion;
  std::string voicebankContentHash;
  std::vector<std::string> phraseContentHashes;
  std::vector<synthesis::UnitPlanEntry> unitPlan;
  std::size_t phraseCount{0U};
  std::size_t unitCount{0U};
  std::size_t fallbackCount{0U};
  std::size_t cacheHits{0U};
  std::size_t trackCount{0U};
  std::size_t regionCount{0U};
  // Populated only by non-realtime Final preparation. A read handle keeps
  // ownership in the publication slot; process() never copies this pointer.
  std::shared_ptr<const authoring::PublishedProjectAudio> offlineSource;
};

[[nodiscard]] std::string_view previewStatusName(PreviewStatus status) noexcept;


class RealtimePreviewPublication final {
public:
  class ReadHandle final {
  public:
    ReadHandle() = default;
    ReadHandle(const ReadHandle&) = delete;
    ReadHandle& operator=(const ReadHandle&) = delete;
    ReadHandle(ReadHandle&& other) noexcept;
    ReadHandle& operator=(ReadHandle&& other) noexcept;
    ~ReadHandle();

    [[nodiscard]] const RenderedPreview* get() const noexcept { return value_; }
    [[nodiscard]] const RenderedPreview* operator->() const noexcept { return value_; }
    [[nodiscard]] const RenderedPreview& operator*() const noexcept { return *value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return value_ != nullptr; }

  private:
    friend class RealtimePreviewPublication;
    ReadHandle(const RealtimePreviewPublication* owner, std::size_t slot,
               const RenderedPreview* value) noexcept;
    void release() noexcept;
    const RealtimePreviewPublication* owner_{nullptr};
    std::size_t slot_{0U};
    const RenderedPreview* value_{nullptr};
  };

  RealtimePreviewPublication();
  [[nodiscard]] ReadHandle acquire() const noexcept;
  [[nodiscard]] bool publish(RenderedPreview preview);

private:
  static constexpr std::size_t kSlotCount = 3U;
  struct Slot final {
    RenderedPreview preview;
    mutable std::atomic<std::uint32_t> readers{0U};
  };
  std::array<Slot, kSlotCount> slots_{};
  std::atomic<std::size_t> published_{0U};
  std::mutex writerMutex_;
};

struct RenderServiceStats final {
  std::uint64_t submitted{0U};
  std::uint64_t completed{0U};
  std::uint64_t cancelled{0U};
  std::uint64_t stale{0U};
};

struct LiveVoice final {
  bool active{false};
  bool releasing{false};
  std::int32_t noteId{-1};
  std::int16_t key{60};
  double samplePosition{0.0};
  double pitchRatio{1.0};
  float envelope{0.0F};
  float velocity{0.0F};
};

class EditorRuntime final {
public:
  explicit EditorRuntime(
      std::optional<domain::Project> project = std::nullopt,
      const std::filesystem::path& characterPackage = {},
      std::vector<voicebank::VoicebankSearchRoot> voicebankRoots = {});
  ~EditorRuntime();

  EditorRuntime(const EditorRuntime&) = delete;
  EditorRuntime& operator=(const EditorRuntime&) = delete;

  [[nodiscard]] native_ui::NativeEditorController& controller() noexcept {
    return *controller_;
  }
  [[nodiscard]] const native_ui::NativeEditorController& controller() const noexcept {
    return *controller_;
  }

  struct AccessibilitySnapshot final {
    std::vector<native_ui::SemanticNode> children;
    std::size_t virtualizedNoteCount{0U};
  };

  [[nodiscard]] AccessibilitySnapshot accessibilitySnapshot();
  [[nodiscard]] std::optional<native_ui::SemanticNode>
  accessibilityFocusedNode();
  [[nodiscard]] std::vector<native_ui::SemanticNode> accessibilityNotes(
      std::size_t offset, std::size_t limit) const;
  [[nodiscard]] core::Result<void> dispatchAccessibility(
      std::string_view id, native_ui::SemanticAction action);
  [[nodiscard]] core::Result<void> setAccessibilityValue(
      std::string_view id, std::string_view value);

  void setRepaintCallback(std::function<void()> callback);
  // Presents the EMO/SCENE SING workspace in this editor. The shipping plug-in enables it;
  // library tests keep the classic editor and never read the user's saved design preferences.
  void activateDesignShell();
  // Test and capture entry point: an explicit preference set, never the saved one.
  void activateDesignShell(native_ui::design::DesignPreferences preferences);
  // Abandons shell and editor pointer gestures without committing them (hide, capture loss).
  void cancelPointerGestures();
  // Invoked for a persistent project change, from the originating thread. Most
  // edits are detected by revision at repaint; direct persisted settings also
  // signal it explicitly. CLAP can request a main-thread host-state notification.
  void setPersistentStateChangeCallback(std::function<void()> callback);
  void setRenderReadyCallback(std::function<void()> callback);
  // Owner thread. The measured level of the plug-in's output as its audio thread last reported it,
  // or nothing while the host is not processing; the header meter paints it.
  void setOutputLevel(std::optional<native_ui::EditorSceneState::OutputLevel> level);
  // Called when the creator clears the meter's clip light, to reset the publisher's latch.
  void setOutputClipResetCallback(std::function<void()> callback);
  void setTextInputCallbacks(
      std::function<void(const native_ui::TextInputRequest&)> begin,
      std::function<void()> end);
  void setVoicebankInstallerHandoff(
      std::function<core::Result<void>()> callback);
  void setJapaneseReadingResourceResolver(
      std::function<core::Result<authoring::StagedJapaneseReadingResource>()>
          resolver);

  void resize(double logicalWidth, double logicalHeight) noexcept;
  void paint(native_ui::RasterCanvas& canvas) noexcept;
  void pointerDown(const native_ui::PointerEvent& event) noexcept;
  void pointerMove(const native_ui::PointerEvent& event) noexcept;
  void pointerUp(const native_ui::PointerEvent& event) noexcept;
  void scroll(double deltaX, double deltaY, ui::Point anchor,
              native_ui::InputModifiers modifiers) noexcept;
  void keyDown(const native_ui::KeyEvent& event) noexcept;
  void textComposition(std::u32string text,
                       ui::CompositionSelection selection) noexcept;
  void textCommit(std::u32string text) noexcept;
  void textCancel() noexcept;
  void tick() noexcept { requestRepaint(); }
  [[nodiscard]] bool textInputActive() const noexcept {
    return controller_ != nullptr && controller_->textInputActive();
  }

  [[nodiscard]] domain::Project projectCopy() const;
  [[nodiscard]] core::Result<void> replaceProject(domain::Project project);
  // The same interchange boundary the standalone surface uses, so an embedded session can open a
  // USTX/SMF score and write one back. Import returns an unsaved draft for review; only an explicit
  // acceptance replaces the live document, and export is create-new and never mutates it.
  [[nodiscard]] core::Result<authoring::InterchangeImportDraft> prepareInterchangeImport(
      const std::filesystem::path& source,
      authoring::InterchangeImportRequest request = {}) const;
  [[nodiscard]] core::Result<void> acceptInterchangeImport(
      authoring::InterchangeImportDraft draft);
  [[nodiscard]] core::Result<authoring::InterchangeExportReceipt> exportInterchange(
      authoring::InterchangeExportRequest request) const;
  // A plugin cannot own a file dialog, so the host supplies the path choice and the loss-review
  // decision, exactly as it already does for the voicebank installer. The lifecycle stays here so
  // the order "choose, convert, review, accept" is the same one the standalone surface runs and can
  // be exercised without a host.
  using InterchangePathHandoff =
      std::function<core::Result<std::optional<std::filesystem::path>>()>;
  void setInterchangeImportHandoff(InterchangePathHandoff callback);
  void setInterchangeExportHandoff(InterchangePathHandoff callback);
  void setInterchangeReviewHandoff(
      std::function<core::Result<bool>(const authoring::InterchangeImportDraft&)> callback);
  void setInterchangeExportReviewHandoff(
      std::function<core::Result<bool>(const authoring::InterchangeExportDraft&)> callback);
  // Keyboard commands cannot return a Result to their caller. The host presents failures through
  // this handoff; cancellation and a declined review do not report an error.
  using InterchangeErrorHandoff =
      std::function<void(std::string_view, const core::Error&)>;
  void setInterchangeErrorHandoff(InterchangeErrorHandoff callback);
  // Choose a source, convert it to a draft and ask the review handoff whether to accept it. A
  // declined or missing decision leaves the live song untouched.
  [[nodiscard]] core::Result<void> requestInterchangeImport();
  // Choose a destination and write the live project as USTX or SMF. Never mutates the project.
  [[nodiscard]] core::Result<void> requestInterchangeExport();
  [[nodiscard]] domain::RegionId regionId() const noexcept { return regionId_; }
  [[nodiscard]] domain::TrackId trackId() const noexcept { return trackId_; }
  [[nodiscard]] std::uint64_t revision() const noexcept;

  [[nodiscard]] std::vector<domain::TrackId> vocalTrackIds() const;
  [[nodiscard]] std::vector<domain::RegionId> regionIds(
      domain::TrackId trackId) const;
  [[nodiscard]] core::Result<void> selectTrack(domain::TrackId trackId);
  [[nodiscard]] core::Result<void> selectRegion(domain::RegionId regionId);
  [[nodiscard]] core::Result<void> setTrackMix(
      domain::TrackId trackId, float gainDb, float pan, bool muted, bool solo);
  [[nodiscard]] core::Result<void> configureOutputChannels(
      std::uint8_t channels);
  [[nodiscard]] core::Result<void> setHostStartOffset(time::Tick tick);

  [[nodiscard]] core::Result<void> refreshVoicebanks();
  [[nodiscard]] core::Result<void> addVoicebankSearchRoot(
      voicebank::VoicebankSearchRoot root);
  [[nodiscard]] core::Result<void> selectVoicebank(
      std::string_view id, std::string_view version,
      std::optional<std::string_view> contentHash = std::nullopt);
  [[nodiscard]] voicebank::VoicebankResolution voicebankResolution() const;
  [[nodiscard]] std::vector<voicebank::VoicebankCandidate> availableVoicebanks() const;

  void requestRender(std::uint32_t sampleRate);
  // Projection of the render state into the editor's panel. This is NOT owner-thread only:
  // the render worker reaches it through publishPreviewFromAuthoring, so it takes mutex_ to
  // read the controller_ that the owner thread reassigns. It is safe to call with mutex_
  // already held because that lock is recursive, and it has no path that re-enters itself.
  void refreshRenderStatusView();
  void setRenderQuality(rendering::RenderQuality quality);
  [[nodiscard]] rendering::RenderQuality renderQuality() const noexcept;
  void setOfflineTimingAuthority(OfflineTimingAuthority authority) noexcept;
  [[nodiscard]] OfflineTimingAuthority offlineTimingAuthority() const noexcept;
  // Non-realtime host entrypoint. It requests a Final render and waits only
  // outside the audio callback until the exact current revision is published.
  [[nodiscard]] core::Result<void> prepareOfflineRender(
      std::chrono::milliseconds timeout = std::chrono::seconds{30});
  [[nodiscard]] OfflineRenderView offlineRenderView() const;
  // What the editor's render status panel should show: the offline bounce when one has been
  // prepared or refused, and the ordinary render otherwise. A refused Follow Host bounce is
  // visible here, including the range that would have to be recaptured.
  [[nodiscard]] native_ui::RenderStatusView renderStatusView() const;
  [[nodiscard]] bool offlineRenderReady() const noexcept {
    return static_cast<bool>(acquireOfflineRenderedPreview());
  }
  [[nodiscard]] RealtimePreviewPublication::ReadHandle
  acquireOfflineRenderedPreview() const noexcept {
    if (!offlineAudioReady_.load(std::memory_order_acquire)) return {};
    auto captured = offlinePublication_.acquire();
    if (!captured || !captured->offlineSource ||
        !authoring_->renderer().matchesCurrent(*captured->offlineSource)) return {};
    return captured;
  }
  [[nodiscard]] std::shared_ptr<const RenderedPreview> renderedPreview() const;
  [[nodiscard]] RealtimePreviewPublication::ReadHandle
  acquireRenderedPreview() const noexcept {
    return previewPublication_.acquire();
  }
  [[nodiscard]] RenderServiceStats renderStats() const noexcept;

  [[nodiscard]] core::Result<void> movePhonemeBoundary(
      domain::PhonemeKey key, bool startBoundary,
      time::Microseconds offset);
  [[nodiscard]] core::Result<void> selectUnitVariant(
      domain::PhonemeKey key, std::string unitId,
      domain::UnitRendererKind renderer);
  [[nodiscard]] core::Result<void> cycleUnitVariant(domain::PhonemeKey key);
  [[nodiscard]] core::Result<void> cycleUnitRenderer(domain::PhonemeKey key);
  [[nodiscard]] core::Result<void> upsertPitchPoint(
      domain::PitchAutomationPoint point);
  [[nodiscard]] core::Result<void> movePitchPoint(
      time::Tick from, domain::PitchAutomationPoint point);
  [[nodiscard]] core::Result<void> removePitchPoint(time::Tick tick);
  [[nodiscard]] core::Result<void> cyclePitchInterpolation(time::Tick tick);
  [[nodiscard]] core::Result<void> openSampleMicroscope(
      domain::PhonemeKey key);
  void closeSampleMicroscope() noexcept;
  [[nodiscard]] bool sampleMicroscopeOpen() const noexcept;
  [[nodiscard]] const ui::SampleMicroscopeModel* sampleMicroscope() const noexcept;
  [[nodiscard]] std::optional<std::string> selectedUnitId() const;

  void setHostTimelineState(HostTimelineState state) noexcept;
  [[nodiscard]] HostTimelineState hostTimelineState() const noexcept;
  // The tempo history this host has actually reported. Follow Host final rendering is
  // authorized against this map's coverage, never against the instantaneous value.
  [[nodiscard]] HostTempoMap hostTempoMap() const;
  // The frozen authority the current Follow Host readiness rests on: what range the host
  // covered, which tempo and meter segments it stated there and the identity over both.
  // Empty when the authority is Fixed Audio, when nothing has been prepared, or when the
  // host has since reported different musical content.
  [[nodiscard]] std::optional<PreparedHostTimeline> preparedHostTimeline() const;

  void setLiveSampleRate(double sampleRate) noexcept {
    live_.setOutputSampleRate(sampleRate);
  }
  void noteOn(std::int32_t noteId, std::int16_t key, float velocity) noexcept {
    live_.noteOn(noteId, key, velocity);
  }
  void noteOff(std::int32_t noteId, std::int16_t key) noexcept {
    live_.noteOff(noteId, key);
  }
  void choke(std::int32_t noteId, std::int16_t key) noexcept {
    live_.choke(noteId, key);
  }
  void dispatchLiveEvent(const live_voice::LiveEvent& event) noexcept {
    live_.dispatchLiveEvent(event);
  }
  void renderLiveRange(float* const* outputs, std::uint32_t channels,
                       std::uint32_t beginFrame,
                       std::uint32_t endFrame) noexcept {
    live_.renderLiveRange(outputs, channels, beginFrame, endFrame);
  }
  [[nodiscard]] float renderLiveSample() noexcept { return live_.renderSample(); }
  void resetLive() noexcept { live_.reset(); }
  [[nodiscard]] std::size_t activeLiveVoiceCount() const noexcept {
    return live_.activeVoiceCount();
  }

  [[nodiscard]] core::Result<void> setPrimarySeamAmount(float value);
  [[nodiscard]] float primarySeamAmount() const noexcept;

private:
  [[nodiscard]] static domain::Project makeDefaultProject(
      application::ProjectFactory& factory, domain::RegionId& regionId);
  [[nodiscard]] static domain::RegionId firstRegionId(
      const domain::Project& project) noexcept;
  [[nodiscard]] static domain::TrackId firstTrackId(
      const domain::Project& project) noexcept;
  void rebuildController();
  void configureControllerCallbacks();
  void requestRepaint() const;
  void requestRenderAfterEdit();
  enum class ShellPointerPhase : std::uint8_t { Down, Move, Up };
  // Returns true when the SING shell presented the last frame and consumed the pointer event.
  bool routeShellPointerLocked(ShellPointerPhase phase, const native_ui::PointerEvent& event);
  bool shellKeyLocked(const native_ui::KeyEvent& event);
  void activateDesignShellWith(std::optional<native_ui::design::DesignPreferences> preferences);
  [[nodiscard]] native_ui::EditorSceneState sceneState();
  void refreshVoicebankResolutionLocked();
  void refreshAllVoicebankResolutionsLocked();
  void rebuildVoicebankCardsLocked();
  void refreshLiveResourceLocked();
  void rebuildTechnicalModelsLocked();
  [[nodiscard]] phonemizer::Result phonemesLocked() const;
  [[nodiscard]] core::Result<native_ui::SampleMicroscopeData> loadSampleMicroscope(
      domain::PhonemeKey key);
  [[nodiscard]] const ui::PhonemeVisual* phonemeVisualAt(
      ui::Point point) const noexcept;
  [[nodiscard]] const ui::UnitLaneVisual* unitVisualAt(
      ui::Point point) const noexcept;
  [[nodiscard]] std::optional<time::Tick> pitchPointAt(
      ui::Point point, double tolerance = 8.0) const noexcept;
  [[nodiscard]] time::Microseconds microsecondOffsetAt(
      domain::NoteId noteId, double x) const noexcept;
  void paintPhase12BOverlay(native_ui::RasterCanvas& canvas) noexcept;
  [[nodiscard]] core::Result<void> bindVoicebankLocked(
      const voicebank::VoicebankCandidate& candidate);
  void publishPreviewFromAuthoring();
  [[nodiscard]] static RenderedPreview makeRenderedPreview(
      const authoring::PublishedProjectAudio& audio);

  mutable std::recursive_mutex mutex_;
  bool createdDefault_{false};
  bool allowDevelopmentVoicebanks_{false};
  domain::TrackId trackId_{};
  domain::RegionId regionId_{};
  std::unique_ptr<authoring::AuthoringRuntime> authoring_;
  application::ProjectFactory& factory_;
  application::EditorSession& session_;
  authoring::VoicebankSession& voicebankSession_;
  authoring::VoicebankBrowserModel voicebankBrowser_;
  std::unique_ptr<native_ui::NativeEditorController> controller_;
  native_ui::EditorScenePainter painter_;
  native_ui::design::SingShell shell_;
  native_ui::CharacterPresentation character_;
  // Only the bounded performance model survives a repaint, never the published PCM.
  std::optional<std::uint64_t> characterPerformanceRequest_;
  mutable RealtimePreviewPublication previewPublication_;
  mutable RealtimePreviewPublication offlinePublication_;
  live_voice::VoiceEngine live_;
  voicebank::VoicebankResolution voicebankResolution_;
  std::function<void()> repaintCallback_;
  std::function<void()> persistentStateChangeCallback_;
  mutable std::uint64_t lastSignalledRevision_{0U};
  std::function<void()> renderReadyCallback_;
  std::function<void()> outputClipResetCallback_;
  // Runs the clip reset callback outside the lock (the controller calls it from an edit).
  void resetOutputClip();
  std::function<void(const native_ui::TextInputRequest&)> beginTextInput_;
  std::function<void()> endTextInput_;
  std::function<core::Result<void>()> voicebankInstallerHandoff_;
  std::function<core::Result<authoring::StagedJapaneseReadingResource>()>
      japaneseReadingResourceResolver_;
  double logicalWidth_{1100.0};
  double logicalHeight_{720.0};
  std::uint32_t renderSampleRate_{48000U};
  rendering::RenderQuality renderQuality_{rendering::RenderQuality::Preview};
  OfflineTimingAuthority offlineTimingAuthority_{OfflineTimingAuthority::FixedAudio};
  OfflineRenderSession offlineRender_;
  std::atomic<bool> offlineAudioReady_{false};
  bool dirty_{false};
  InterchangePathHandoff interchangeImportHandoff_;
  InterchangePathHandoff interchangeExportHandoff_;
  std::function<core::Result<bool>(const authoring::InterchangeImportDraft&)>
      interchangeReviewHandoff_;
  std::function<core::Result<bool>(const authoring::InterchangeExportDraft&)>
      interchangeExportReviewHandoff_;
  InterchangeErrorHandoff interchangeErrorHandoff_;
  ui::PhonemeLaneModel phonemeLane_;
  ui::UnitLaneModel unitLane_;
  std::optional<domain::PhonemeKey> selectedUnitKey_;
  std::optional<domain::PhonemeKey> draggingPhonemeKey_;
  bool draggingPhonemeStart_{false};
  std::optional<time::Tick> draggingPitchTick_;
  HostTimelineState hostTimelineState_{};
  // Everything this host has actually reported: tempo history, meter segments, loop state
  // and sample rate. Follow Host rendering is authorized against this capture's coverage
  // for the requested range, never against the instantaneous value above.
  HostTimelineCapture hostTimelineCapture_;
  std::optional<PreparedHostTimeline> preparedHostTimeline_;
  // Envelopes of the selected region's rendered audio for the SING notes. Declared last so its
  // workers stop before the state their repaint request reads is destroyed.
  native_ui::RegionEnvelopeCache waveforms_{[this] { requestRepaint(); }};
};

[[nodiscard]] core::Result<std::vector<std::byte>> encodeEditorState(
    const domain::Project& project);
[[nodiscard]] core::Result<domain::Project> decodeEditorState(
    std::span<const std::byte> bytes);

}  // namespace seam::clap_editor
