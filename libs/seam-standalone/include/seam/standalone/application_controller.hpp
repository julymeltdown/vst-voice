#pragma once

#include "seam/authoring/autosave_service.hpp"
#include "seam/authoring/project_lifecycle.hpp"
#include "seam/authoring/recent_projects.hpp"
#include "seam/authoring/voicebank_browser.hpp"
#include "seam/authoring/voicebank_installer_service.hpp"
#include "seam/voicebank/coverage.hpp"
#include "seam/core/result.hpp"
#include "seam/platform/application_menu.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/standalone/authoring_session.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace seam::standalone {

struct StandaloneApplicationControllerConfig final {
  std::filesystem::path autosaveRoot;
  std::filesystem::path recentProjectsPath;
  std::filesystem::path voicebankInstallRoot{};
  std::vector<distribution::Ed25519PublicKey> trustedVoicebankKeys{};
  std::optional<distribution::Ed25519PublicKey> developmentTrustRoot{};
  bool allowDevelopmentVoicebanks{false};
  authoring::NewProjectRequest defaultNewProject{
      .name = "Untitled",
      .tempoBpm = 120.0,
      .sampleRate = 48000U,
      .outputChannels = 2U,
      .initialVoicebank = std::nullopt,
  };
  std::function<void()> stateChanged;
};

class StandaloneApplicationController final
    : public platform::IApplicationCommandDispatcher {
public:
  static core::Result<std::unique_ptr<StandaloneApplicationController>> create(
      AuthoringSession& session,
      std::unique_ptr<platform::IFileDialog> fileDialog,
      std::unique_ptr<platform::IUnsavedChangesPrompt> unsavedPrompt,
      StandaloneApplicationControllerConfig config,
      std::function<void()> requestQuit = {});
  ~StandaloneApplicationController();

  StandaloneApplicationController(const StandaloneApplicationController&) = delete;
  StandaloneApplicationController& operator=(
      const StandaloneApplicationController&) = delete;

  [[nodiscard]] core::Result<void> dispatch(
      platform::ApplicationCommand command) override;
  [[nodiscard]] core::Result<bool> requestClose();
  [[nodiscard]] std::vector<platform::RecentProjectMenuItem> recentProjects()
      const override;
  [[nodiscard]] core::Result<void> openRecentProject(
      const std::filesystem::path& path) override;
  [[nodiscard]] std::vector<platform::RecoveryMenuItem> recoveryItems()
      const override;
  [[nodiscard]] core::Result<void> recoverAutosave(
      const std::filesystem::path& metadataPath) override;
  [[nodiscard]] std::vector<platform::VoicebankMenuItem> voicebanks() const override;
  [[nodiscard]] core::Result<void> selectVoicebank(
      std::string_view id, std::string_view version,
      std::string_view contentHash) override;

  [[nodiscard]] const std::vector<authoring::VoicebankCard>& voicebankCards()
      const noexcept { return voicebankBrowser_.cards(); }
  [[nodiscard]] core::Result<authoring::VoicebankInstallResult> installVoicebank(
      const std::filesystem::path& packagePath,
      authoring::ExistingVoicebankDecision decision =
          authoring::ExistingVoicebankDecision::Reject);
  [[nodiscard]] core::Result<voicebank::VoicebankResolution> relinkVoicebank(
      domain::TrackId trackId, voicebank::VoicebankSearchRoot root);
  [[nodiscard]] core::Result<void> replaceVoicebank(
      domain::TrackId trackId, std::string_view id, std::string_view version,
      std::string_view contentHash);
  [[nodiscard]] core::Result<voicebank::VoicebankCoverageReport>
  selectedRegionCoverage() const;
  [[nodiscard]] core::Result<void> onDocumentChanged(
      std::chrono::steady_clock::time_point now =
          std::chrono::steady_clock::now());
  [[nodiscard]] core::Result<void> tickAutosave(
      std::chrono::steady_clock::time_point now =
          std::chrono::steady_clock::now());
  [[nodiscard]] core::Result<std::vector<authoring::RecoveryCandidate>>
  recoveryCandidates() const;
  [[nodiscard]] core::Result<void> recover(
      const authoring::RecoveryCandidate& candidate);
  [[nodiscard]] core::Result<void> openRecent(
      const std::filesystem::path& path);

  [[nodiscard]] authoring::AutosaveService& autosave() noexcept {
    return autosave_;
  }
  [[nodiscard]] const authoring::RecentProjectsStore& recentProjectStore()
      const noexcept {
    return recentProjects_;
  }

private:
  StandaloneApplicationController(
      AuthoringSession& session,
      std::unique_ptr<platform::IFileDialog> fileDialog,
      std::unique_ptr<platform::IUnsavedChangesPrompt> unsavedPrompt,
      StandaloneApplicationControllerConfig config,
      std::function<void()> requestQuit);

  [[nodiscard]] core::Result<void> initialize();
  [[nodiscard]] core::Result<bool> confirmDestructiveAction();
  [[nodiscard]] core::Result<bool> chooseAndSaveAs();
  [[nodiscard]] core::Result<void> openPath(
      const std::filesystem::path& path);
  [[nodiscard]] core::Result<void> recordCurrentProject();
  [[nodiscard]] core::Result<void> refreshVoicebankBrowser();
  [[nodiscard]] std::optional<voicebank::VoicebankCandidate> findCandidate(
      std::string_view id, std::string_view version,
      std::string_view contentHash) const;
  void notifyStateChanged() const;
  [[nodiscard]] authoring::NewProjectRequest defaultNewProject() const;

  AuthoringSession& session_;
  std::unique_ptr<platform::IFileDialog> fileDialog_;
  std::unique_ptr<platform::IUnsavedChangesPrompt> unsavedPrompt_;
  StandaloneApplicationControllerConfig config_;
  std::function<void()> requestQuit_;
  authoring::AutosaveService autosave_;
  authoring::RecentProjectsStore recentProjects_;
  authoring::VoicebankBrowserModel voicebankBrowser_;
  std::unique_ptr<authoring::VoicebankInstallerService> voicebankInstaller_;
};

}  // namespace seam::standalone
