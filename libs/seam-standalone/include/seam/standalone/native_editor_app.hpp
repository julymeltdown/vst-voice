#pragma once

#include "seam/core/result.hpp"
#include "seam/native_ui/character_presentation.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/native_window.hpp"
#include "seam/platform/application_menu.hpp"
#include "seam/platform/audio_device.hpp"
#include "seam/platform/audio_device_catalog.hpp"
#include "seam/platform/crash_capture.hpp"
#include "seam/platform/multichannel_ring_buffer_processor.hpp"
#include "seam/authoring/audio_settings_controller.hpp"
#include "seam/standalone/application_controller.hpp"
#include "seam/standalone/authoring_session.hpp"
#include "seam/standalone/production_configuration.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/authoring/support_bundle.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
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
  bool allowDevelopmentVoicebanks{false};
  std::size_t audioBlockFrames{256U};
  bool forceThreadedAudio{false};
  bool startPaused{true};
  std::function<std::unique_ptr<platform::IAudioDevice>()>
      systemAudioDeviceFactory;
  std::function<std::unique_ptr<platform::IAudioDevice>()>
      threadedAudioDeviceFactory;
  std::function<core::Result<std::optional<authoring::NewProjectRequest>>()> requestNewProject;
  std::filesystem::path manualsRoot;
};

class NativeEditorApp final : public native_ui::INativeWindowClient {
public:
  static core::Result<std::unique_ptr<NativeEditorApp>> create(
      NativeEditorAppConfig config);
  ~NativeEditorApp() override;

  NativeEditorApp(const NativeEditorApp&) = delete;
  NativeEditorApp& operator=(const NativeEditorApp&) = delete;

  void setWindow(native_ui::INativeWindow& window) noexcept;
  [[nodiscard]] core::Result<void> startAudioForPlayback();
  void stopAudioForPlayback() noexcept;
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
  [[nodiscard]] bool requestClose() noexcept override;
  [[nodiscard]] bool wantsClose() const noexcept override;

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
  void refreshCrashRecoveryContext();
  void setAudioUnavailable(const core::Error& error) noexcept;
  void clearAudioUnavailable() noexcept;
  void record(const core::Result<void>& result) noexcept;

  NativeEditorAppConfig config_;
  std::unique_ptr<AuthoringSession> authoring_;
  std::unique_ptr<StandaloneApplicationController> applicationController_;
  std::unique_ptr<platform::IApplicationMenu> applicationMenu_;
  native_ui::CharacterPresentation character_;
  native_ui::EditorScenePainter painter_;
  std::unique_ptr<platform::MultichannelRingBufferAudioProcessor> processor_;
  std::unique_ptr<platform::IAudioDevice> audioDevice_;
  std::unique_ptr<platform::IAudioDeviceCatalog> audioDeviceCatalog_;
  std::unique_ptr<platform::CrashCapture> crashCapture_;
  std::optional<platform::CrashMarker> startupCrashMarker_;
  std::optional<platform::CrashRecoveryContext> crashRecoveryContext_;
  std::unique_ptr<authoring::SupportBundleService> supportBundle_;
  std::optional<authoring::PreparedSupportBundle> pendingSupportBundle_;
  std::vector<authoring::SupportBundleRecord> supportReports_;
  std::size_t selectedSupportReportIndex_{0U};
  std::filesystem::path supportExportRoot_;
  std::unique_ptr<authoring::AudioSettingsController> audioSettings_;
  std::unique_ptr<authoring::AudioSettingsStore> audioSettingsStore_;
  std::filesystem::path recoveryRoot_;
  std::string startupDeviceId_;
  std::optional<authoring::Diagnostic> audioDiagnostic_;
  native_ui::INativeWindow* window_{nullptr};
  std::atomic<bool> closeRequested_{false};
  std::string lastError_;
};

}  // namespace seam::standalone
