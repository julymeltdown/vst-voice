#pragma once

#include "seam/authoring/authoring_runtime.hpp"
#include "seam/authoring/autosave_service.hpp"
#include "seam/authoring/project_lifecycle.hpp"
#include "seam/core/result.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/voicebank/catalog.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace seam::standalone {

struct AuthoringSessionConfig final {
  std::filesystem::path cacheRoot;
  std::vector<voicebank::VoicebankSearchRoot> voicebankRoots;
  std::uint32_t sampleRate{48000U};
  std::uint8_t outputChannels{2U};
  bool bindFirstAvailableVoicebank{true};
};

class AuthoringSession final {
public:
  static core::Result<std::unique_ptr<AuthoringSession>> create(
      AuthoringSessionConfig config,
      native_ui::EditorHostCallbacks callbacks = {});
  ~AuthoringSession();

  AuthoringSession(const AuthoringSession&) = delete;
  AuthoringSession& operator=(const AuthoringSession&) = delete;

  [[nodiscard]] authoring::AuthoringRuntime& runtime() noexcept {
    return *runtime_;
  }
  [[nodiscard]] const authoring::AuthoringRuntime& runtime() const noexcept {
    return *runtime_;
  }
  [[nodiscard]] native_ui::NativeEditorController& controller() noexcept {
    return *controller_;
  }
  [[nodiscard]] const native_ui::NativeEditorController& controller() const noexcept {
    return *controller_;
  }
  [[nodiscard]] domain::TrackId trackId() const noexcept { return trackId_; }
  [[nodiscard]] domain::RegionId regionId() const noexcept { return regionId_; }
  [[nodiscard]] voicebank::VoicebankResolution voicebankResolution() const;

  [[nodiscard]] core::Result<void> createNewProject(
      authoring::NewProjectRequest request);
  [[nodiscard]] core::Result<authoring::OpenProjectResult> openProject(
      const std::filesystem::path& path);
  [[nodiscard]] core::Result<void> saveProject();
  [[nodiscard]] core::Result<void> saveProjectAs(
      const std::filesystem::path& path);
  [[nodiscard]] core::Result<void> recoverProject(
      authoring::AutosaveService& autosave,
      const authoring::RecoveryCandidate& candidate);

private:
  AuthoringSession(std::unique_ptr<authoring::AuthoringRuntime> runtime,
                   domain::TrackId trackId,
                   domain::RegionId regionId,
                   native_ui::EditorHostCallbacks callbacks);

  static domain::Project makeUntitledProject(
      application::ProjectFactory& factory,
      domain::TrackId& trackId,
      domain::RegionId& regionId,
      std::uint32_t sampleRate,
      std::uint8_t outputChannels);
  core::Result<void> initialize(bool bindFirstAvailableVoicebank);
  core::Result<void> bindInitialVoicebank();
  [[nodiscard]] core::Result<void> rebindAfterProjectReplacement();
  void configureController();
  void onDocumentChanged();
  void onRenderCompleted();

  std::unique_ptr<authoring::AuthoringRuntime> runtime_;
  std::unique_ptr<native_ui::NativeEditorController> controller_;
  native_ui::EditorHostCallbacks externalCallbacks_;
  domain::TrackId trackId_{};
  domain::RegionId regionId_{};
};

}  // namespace seam::standalone
