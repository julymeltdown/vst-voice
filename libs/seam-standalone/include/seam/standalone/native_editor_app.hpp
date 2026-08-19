#pragma once

#include "seam/core/result.hpp"
#include "seam/native_ui/character_presentation.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/native_window.hpp"
#include "seam/platform/application_menu.hpp"
#include "seam/platform/audio_device.hpp"
#include "seam/platform/multichannel_ring_buffer_processor.hpp"
#include "seam/standalone/application_controller.hpp"
#include "seam/standalone/authoring_session.hpp"
#include "seam/distribution/signing.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>
#include <string>

namespace seam::standalone {

struct NativeEditorAppConfig final {
  AuthoringSessionConfig authoring;
  std::filesystem::path characterPackage{"assets/character-01"};
  std::filesystem::path applicationSupportRoot;
  std::vector<distribution::Ed25519PublicKey> trustedVoicebankKeys;
  std::optional<distribution::Ed25519PublicKey> developmentTrustRoot;
  bool allowDevelopmentVoicebanks{false};
  std::size_t audioBlockFrames{256U};
  bool forceThreadedAudio{false};
  bool startPaused{false};
};

class NativeEditorApp final : public native_ui::INativeWindowClient {
public:
  static core::Result<std::unique_ptr<NativeEditorApp>> create(
      NativeEditorAppConfig config);
  ~NativeEditorApp() override;

  NativeEditorApp(const NativeEditorApp&) = delete;
  NativeEditorApp& operator=(const NativeEditorApp&) = delete;

  void setWindow(native_ui::INativeWindow& window) noexcept;
  void shutdownAudio() noexcept;

  [[nodiscard]] AuthoringSession& authoring() noexcept { return *authoring_; }
  [[nodiscard]] const AuthoringSession& authoring() const noexcept {
    return *authoring_;
  }
  [[nodiscard]] platform::AudioDeviceInfo audioInfo() const;
  [[nodiscard]] platform::AudioDeviceStats audioStats() const noexcept;
  [[nodiscard]] platform::MultichannelRingProcessorStats processorStats() const noexcept;
  [[nodiscard]] const std::string& lastError() const noexcept { return lastError_; }

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
  [[nodiscard]] bool requestClose() noexcept override;
  [[nodiscard]] bool wantsClose() const noexcept override;

private:
  explicit NativeEditorApp(NativeEditorAppConfig config)
      : config_(std::move(config)) {}
  core::Result<void> initialize();
  core::Result<void> initializeAudio();
  void record(const core::Result<void>& result) noexcept;

  NativeEditorAppConfig config_;
  std::unique_ptr<AuthoringSession> authoring_;
  std::unique_ptr<StandaloneApplicationController> applicationController_;
  std::unique_ptr<platform::IApplicationMenu> applicationMenu_;
  native_ui::CharacterPresentation character_;
  native_ui::EditorScenePainter painter_;
  std::unique_ptr<platform::MultichannelRingBufferAudioProcessor> processor_;
  std::unique_ptr<platform::IAudioDevice> audioDevice_;
  native_ui::INativeWindow* window_{nullptr};
  std::atomic<bool> closeRequested_{false};
  std::string lastError_;
};

}  // namespace seam::standalone
