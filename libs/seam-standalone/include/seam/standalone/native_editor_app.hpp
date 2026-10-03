#pragma once

#include "seam/core/result.hpp"
#include "seam/native_ui/candidate_audition_session.hpp"
#include "seam/native_ui/character_presentation.hpp"
#include "seam/native_ui/design/sing_shell.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/native_window.hpp"
#include "seam/platform/application_menu.hpp"
#include "seam/platform/audio_device.hpp"
#include "seam/platform/audio_device_catalog.hpp"
#include "seam/platform/output_level_meter.hpp"
#include "seam/platform/crash_capture.hpp"
#include "seam/platform/multichannel_ring_buffer_processor.hpp"
#include "seam/authoring/audio_settings_controller.hpp"
#include "seam/standalone/application_controller.hpp"
#include "seam/standalone/authoring_session.hpp"
#include "seam/standalone/native_project_dialog.hpp"
#include "seam/standalone/playback_device_policy.hpp"
#include "seam/standalone/production_configuration.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/authoring/support_bundle.hpp"

#include <atomic>
#include <filesystem>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include <string>

namespace seam::standalone {

struct NativeEditorAppConfig final {
  AuthoringSessionConfig authoring;
  ProductionRuntimeMode runtimeMode{ProductionRuntimeMode::Release};
  std::filesystem::path characterPackage;
  std::filesystem::path applicationSupportRoot;
  std::vector<distribution::Ed25519PublicKey> trustedVoicebankKeys;
  std::optional<distribution::Ed25519PublicKey> developmentTrustRoot;
  // The signed update channel, when this build is configured with one. Absent means no channel,
  // and the app reports its distribution authority as unknown rather than assumed good.
  std::filesystem::path updatePolicyPath;
  std::filesystem::path updateManifestPath;
  std::optional<distribution::Ed25519PublicKey> trustedUpdateRoot;
  bool allowDevelopmentVoicebanks{false};
  std::size_t audioBlockFrames{256U};
  bool forceThreadedAudio{false};
  bool startPaused{true};
  std::function<std::unique_ptr<platform::IAudioDevice>()>
      systemAudioDeviceFactory;
  std::function<std::unique_ptr<platform::IAudioDevice>()>
      threadedAudioDeviceFactory;
  // Optional host/test-owned device catalog. Without an override the app uses the system catalog,
  // whose enumeration is a set of platform property queries per device.
  std::function<std::unique_ptr<platform::IAudioDeviceCatalog>()>
      audioDeviceCatalogFactory;
  std::function<core::Result<std::optional<authoring::NewProjectRequest>>()> requestNewProject;
  // Optional host/test-owned review. Without an override the app uses its
  // real native conversion-review dialog, not an automatic approval.
  std::function<core::Result<bool>(const authoring::InterchangeImportDraft&)>
      reviewInterchangeImport;
  std::function<core::Result<bool>(const authoring::InterchangeExportDraft&)>
      reviewInterchangeExport;
  // Optional host-owned verified/staged Open JTalk resource. The app never
  // discovers a reader through PATH or a working directory.
  std::function<core::Result<authoring::StagedJapaneseReadingResource>()>
      prepareJapaneseReadingResource;
  std::filesystem::path manualsRoot;
  // The SING shell is always the editor surface. The shipping app reads and writes the user's
  // saved look (EMO/SCENE, contrast, motion); a host that leaves this off (tests) gets the default
  // look and never touches the user's preferences.
  bool persistDesignPreferences{false};
  // Test seams. The shipping app leaves these empty and uses the native file dialog, the native
  // unsaved-changes prompt and the saved design preferences.
  std::function<std::unique_ptr<platform::IFileDialog>()> fileDialogFactory;
  std::function<std::unique_ptr<platform::IUnsavedChangesPrompt>()> unsavedChangesPromptFactory;
  // An explicit look for the shell, neither read from nor written to the user's preferences.
  std::optional<native_ui::design::DesignPreferences> designPreferences;
  // The clock the shell animates against (the blink, the breath, the fades, the tooltips). Empty in
  // the shipping app, which uses the steady clock. A test moves it by hand, so that a blink cannot
  // fall among the frames it counts.
  std::function<std::chrono::steady_clock::time_point()> uiClock;
};

[[nodiscard]] NativeNewProjectSingerChoices makeNativeNewProjectSingerChoices(
    const std::vector<StandaloneApplicationController::InstalledSingerOffer>&
        offers,
    const std::vector<distribution::ProceduralCatalogueIssue>& issues = {},
    std::size_t omittedIssueCount = 0U,
    bool scanLimitReached = false);

class NativeEditorApp final : public native_ui::INativeWindowClient {
public:
  static core::Result<std::unique_ptr<NativeEditorApp>> create(
      NativeEditorAppConfig config);
  ~NativeEditorApp() override;

  NativeEditorApp(const NativeEditorApp&) = delete;
  NativeEditorApp& operator=(const NativeEditorApp&) = delete;

  void setWindow(native_ui::INativeWindow& window) noexcept;
  // Stops every background source of repaint requests (envelope workers) and forgets the window,
  // so the window can be destroyed before this app: after this returns no thread touches it.
  void detachWindow() noexcept;
  // Writes the UI-fidelity evidence of the last presented frame into dir: geometry.json and
  // semantic-bounds.json from the shell's own snapshot, performance.json with measured paint
  // durations and this process's memory footprint. Fails when the shell did not present.
  // Its device scale is the last painted canvas's pixels per logical point.
  [[nodiscard]] core::Result<void> writeUiEvidence(const std::filesystem::path& dir);
  [[nodiscard]] core::Result<void> startAudioForPlayback();
  // Stops the device, once it says that it has stopped. A device that does not say so stays the
  // consumer of the transport's ring and keeps running(); the error says that, and asking again
  // tries again.
  [[nodiscard]] core::Result<void> stopAudioForPlayback() noexcept;
  void shutdownAudio() noexcept;
  [[nodiscard]] core::Result<void> openProject(
      const std::filesystem::path& path);
  void openProjectPath(const std::filesystem::path& path) noexcept override;
  [[nodiscard]] std::optional<std::filesystem::path> documentPath()
      const noexcept override;

  [[nodiscard]] AuthoringSession& authoring() noexcept { return *authoring_; }
  [[nodiscard]] const AuthoringSession& authoring() const noexcept {
    return *authoring_;
  }
  [[nodiscard]] platform::AudioDeviceInfo audioInfo() const;
  [[nodiscard]] core::Result<platform::AudioDeviceCatalogSnapshot>
  enumerateAudioDevices();
  // The list the settings sheet shows, rebuilt only when the catalog or the settings have moved on
  // (see publishedAudioDevices_).
  [[nodiscard]] const std::vector<native_ui::EditorSceneState::AudioDeviceOption>&
  audioDeviceList(const authoring::AudioSettings& settings);
  [[nodiscard]] core::Result<authoring::AudioSettings> audioSettings() const;
  [[nodiscard]] core::Result<authoring::AudioSettings> applyAudioSettings(
      authoring::AudioSettings requested);
  [[nodiscard]] platform::AudioDeviceStats audioStats() const noexcept;
  [[nodiscard]] platform::MultichannelRingProcessorStats processorStats() const noexcept;
  [[nodiscard]] const std::string& lastError() const noexcept { return lastError_; }
  [[nodiscard]] const std::optional<platform::CrashMarker>& startupCrashMarker()
      const noexcept {
    return startupCrashMarker_;
  }

  void paint(native_ui::RasterCanvas& canvas) noexcept override;
  // The design shell's damage when it presented the frame, else everything.
  [[nodiscard]] native_ui::FrameDamage paintFrame(native_ui::RasterCanvas& canvas) noexcept override;
  void resized(double logicalWidth, double logicalHeight,
               double scale) noexcept override;
  void pointerDown(const native_ui::PointerEvent& event) noexcept override;
  void pointerMove(const native_ui::PointerEvent& event) noexcept override;
  void pointerUp(const native_ui::PointerEvent& event) noexcept override;
  void scroll(double deltaX, double deltaY, ui::Point anchor,
              native_ui::InputModifiers modifiers) noexcept override;
  void keyDown(const native_ui::KeyEvent& event) noexcept override;
  void textComposition(std::u32string text,
                       ui::CompositionSelection selection) noexcept override;
  void textCommit(std::u32string text) noexcept override;
  void textCancel() noexcept override;
  [[nodiscard]] const native_ui::AccessibilityTree* accessibilityTree()
      const noexcept override;
  [[nodiscard]] core::Result<void> dispatchAccessibility(
      std::string_view id, native_ui::SemanticAction action) noexcept override;
  [[nodiscard]] core::Result<void> setAccessibilityValue(
      std::string_view id, std::string_view value) override;
  // A command as the application menu sends it, through the same dispatch: for a host that has no
  // menu in front of it, and for a test that drives the Transport menu's Play, Stop and Loop.
  [[nodiscard]] core::Result<void> dispatchApplicationCommand(platform::ApplicationCommand command);
  [[nodiscard]] bool requestClose() noexcept override;
  [[nodiscard]] bool wantsClose() const noexcept override;
  // The shell's deadline (a waiting tooltip, the idle breath) and the owner thread's time-driven work
  // that a frame does: the autosave tick of a document that has unsaved changes, and the next try at
  // a start of the audio device that failed (see DeviceStartRetry).
  [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> nextFrameDue()
      const noexcept override;

private:
  explicit NativeEditorApp(NativeEditorAppConfig config)
      : config_(std::move(config)) {}
  core::Result<void> initialize();
  core::Result<void> initializeAudio();
  [[nodiscard]] core::Result<void> restartAudio(
      const authoring::AudioSettings& settings);
  [[nodiscard]] core::Result<void> handleDiagnosticAction(
      const authoring::Diagnostic& diagnostic,
      authoring::DiagnosticAction action);
  [[nodiscard]] core::Result<void> refreshSupportReports(
      const std::optional<std::filesystem::path>& preferred = std::nullopt);
  [[nodiscard]] core::Result<void> selectSupportReport(std::size_t index);
  void refreshDistributionAuthority();
  void refreshCrashRecoveryContext();
  void setAudioUnavailable(const core::Error& error) noexcept;
  // A device that would not say that it stopped is a different fact from one that could not be opened:
  // it is still running, so it gets its own notice and the audio state is left as it is.
  void setAudioStopRefused(const core::Error& error) noexcept;
  // Either notice goes through here, so the two codes share one construction and one panel rebuild.
  void raiseAudioNotice(std::string_view code, std::string_view messageKey,
                        const core::Error& error) noexcept;
  void clearAudioNotice() noexcept;
  // Tells the transport whether the audio device, its consumer, runs (see
  // TransportController::setConsumerRunning). The transport cannot see the device, and what it does
  // at the end of a song depends on whether the device is there to play it out. Called when the app
  // starts or stops the device (startAudioForPlayback, stopAudioForPlayback), as restartAudio
  // returns, and once per painted frame for a device that stops on its own. A device that has just
  // stopped on its own and a render that lands before the next frame still meet: the window is one
  // frame, and nothing here closes it.
  void reportConsumerToTransport() noexcept;
  // The clock that the UI reads: the configured one, or the steady clock.
  [[nodiscard]] std::chrono::steady_clock::time_point uiNow() const;
  void record(const core::Result<void>& result) noexcept;
  // Background threads (render completion, envelope workers) ask for a repaint only through here,
  // under windowMutex_, so detachWindow() is a real barrier.
  void requestWindowRepaint() const noexcept;
  // VOICE's host side: the Voice Designer session, the Voicebank Studio's dialogs and an audition
  // output on the system device.
  [[nodiscard]] native_ui::design::ShellVoiceHost makeVoiceHost();

  NativeEditorAppConfig config_;
  std::unique_ptr<AuthoringSession> authoring_;
  // Which published performance the dock is currently bound to. Binding copies a phrase-sized read
  // model, so it happens once per published render rather than once per painted frame.
  std::uint64_t boundPerformanceGeneration_{0U};
  std::unique_ptr<StandaloneApplicationController> applicationController_;
  std::unique_ptr<platform::IApplicationMenu> applicationMenu_;
  native_ui::CharacterPresentation character_;
  // The EMO/SCENE SING shell: the one editor surface. Where it cannot present (no vector backend)
  // the window shows native_ui::paintEditorUnavailable.
  native_ui::design::SingShell shell_;
  // Whether the last paint was the design shell's frame (its damage is then meaningful).
  bool shellPresentedFrame_{false};
  // Measures the blocks the device receives (see MultichannelRingBufferAudioProcessor). Declared
  // before the processor and device so it outlives the audio thread that writes it.
  platform::OutputLevelMeter outputMeter_;
  std::unique_ptr<platform::MultichannelRingBufferAudioProcessor> processor_;
  std::unique_ptr<platform::IAudioDevice> audioDevice_;
  std::unique_ptr<platform::IAudioDeviceCatalog> audioDeviceCatalog_;
  // What the last frame published for the voicebank cards and the audio device list. A window that
  // paints at the display's rate while the device plays would otherwise rebuild both on every
  // frame: the cards are copied field by field out of the browser model, and the device list is a
  // fresh catalog enumeration whose HAL property queries are a quarter of the paint time in the
  // measured profile. Both are rebuilt only when what they would publish differs from what the last
  // frame published, which is the same rule setDiagnostics already uses for the notices.
  std::optional<std::vector<authoring::VoicebankCard>> publishedVoicebankCards_;
  std::optional<std::vector<native_ui::EditorSceneState::AudioDeviceOption>> publishedAudioDevices_;
  // What publishedAudioDevices_ was built from: the settings that chose the row and the device the
  // platform reported when it was built. A device the platform takes away changes the second, so
  // the list is rebuilt and the fallback row that names it comes back.
  authoring::AudioSettings audioDeviceListSettings_;
  platform::AudioDeviceInfo audioDeviceListDevice_;
  std::unique_ptr<platform::CrashCapture> crashCapture_;
  std::optional<platform::CrashMarker> startupCrashMarker_;
  std::optional<platform::CrashRecoveryContext> crashRecoveryContext_;
  std::unique_ptr<authoring::SupportBundleService> supportBundle_;
  std::optional<authoring::PreparedSupportBundle> pendingSupportBundle_;
  std::vector<authoring::SupportBundleRecord> supportReports_;
  std::size_t selectedSupportReportIndex_{0U};
  std::filesystem::path supportExportRoot_;
  std::filesystem::path updatePolicyPath_;
  std::filesystem::path updateManifestPath_;
  std::optional<distribution::Ed25519PublicKey> trustedUpdateRoot_;
  std::unique_ptr<authoring::AudioSettingsController> audioSettings_;
  std::unique_ptr<authoring::AudioSettingsStore> audioSettingsStore_;
  std::filesystem::path recoveryRoot_;
  std::string startupDeviceId_;
  std::optional<authoring::Diagnostic> audioDiagnostic_;
  // The audio notice above says that the device did not stop (see stopAudioForPlayback), and goes
  // when it has.
  bool audioStopFailed_{false};
  // The hold that a start of the audio device that failed puts on the next one: no frame asks the
  // device again before it is over, and nextFrameDue() wakes a still window for it.
  DeviceStartRetry deviceRetry_;
  // When the transport report was last seen unsettled, so a window whose feeder has stopped is asked
  // for its settling frame at a bound rather than at the display's rate (see kTransportSettleWait).
  std::optional<std::chrono::steady_clock::time_point> unsettledSince_;
  // A notice that this project's recorded sound came from different renderer code than this build
  // runs. Held beside the audio notice rather than inside the document, because it is a disclosure
  // about the document, not a property of it, and it must disappear when the creator dismisses it.
  std::optional<authoring::Diagnostic> rendererChangedDiagnostic_;
  // The difference the creator already dismissed. Dismissal is keyed to that exact description, so
  // re-raising the notice every frame would be a notice nobody can close, while a genuinely new
  // difference still speaks up.
  std::string dismissedRendererDifference_;
  native_ui::INativeWindow* window_{nullptr};
  mutable std::mutex windowMutex_;
  // Wall time of each paint() call, most recent last (bounded).
  std::vector<double> paintMillis_;
  double lastPaintScale_{1.0};
  std::atomic<bool> closeRequested_{false};
  std::string lastError_;
  // VOICE: the Voice Designer session (created the first time VOICE asks for it) and the output
  // its auditions play through.
  std::unique_ptr<native_ui::VoiceDesignerSession> voiceDesigner_;
  native_ui::CandidateAuditionSession voiceAudition_;
  // Envelopes of the selected region's rendered audio for the SING notes. Declared last so its
  // workers stop before anything their repaint request touches is destroyed.
  native_ui::RegionEnvelopeCache waveforms_{[this] {
    requestWindowRepaint();
  }};
};

}  // namespace seam::standalone
