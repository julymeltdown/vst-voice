#pragma once

#include "seam/authoring/autosave_service.hpp"
#include "seam/authoring/export_service.hpp"
#include "seam/authoring/neural_resource_registry.hpp"
#include "seam/authoring/neural_selection.hpp"
#include "seam/authoring/project_lifecycle.hpp"
#include "seam/authoring/recent_projects.hpp"
#include "seam/authoring/voicebank_browser.hpp"
#include "seam/authoring/voicebank_installer_service.hpp"
#include "seam/voicebank/coverage.hpp"
#include "seam/core/result.hpp"
#include "seam/platform/application_menu.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/standalone/authoring_session.hpp"
#include "seam/native_ui/export_progress_panel.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <stop_token>
#include <thread>
#include <vector>

namespace seam::standalone {

struct StandaloneApplicationControllerConfig final {
  std::filesystem::path autosaveRoot;
  std::filesystem::path recentProjectsPath;
  std::filesystem::path voicebankInstallRoot{};
  std::filesystem::path manualsRoot{};
  std::vector<distribution::Ed25519PublicKey> trustedVoicebankKeys{};
  std::optional<distribution::Ed25519PublicKey> developmentTrustRoot{};
  bool allowDevelopmentVoicebanks{false};
  std::function<core::Result<std::optional<authoring::NewProjectRequest>>()> requestNewProject;
  authoring::NewProjectRequest defaultNewProject{
      .name = "Untitled",
      .tempoBpm = 120.0,
      .sampleRate = 48000U,
      .outputChannels = 2U,
      .initialVoicebank = std::nullopt,
  };
  std::function<void()> stateChanged;
  std::function<void()> progressChanged;
  std::function<core::Result<void>()> openAudioSettings;
  std::function<core::Result<void>()> editPronunciationHint;
  std::function<core::Result<void>()> findReplaceLyrics;
  std::function<core::Result<void>()> findNotes;
  std::function<core::Result<void>()> findActiveDiagnostics;
  std::function<core::Result<void>()> findNextNote;
  std::function<core::Result<void>()> findPreviousNote;
  std::function<core::Result<void>()> clearSelectedVibrato;
  std::function<core::Result<void>()> editSelectedVibrato;
  std::function<core::Result<void>()> editRegionDynamics;
  std::function<core::Result<void>()> nudgeFormantUp;
  std::function<core::Result<void>()> nudgeFormantDown;
  std::function<core::Result<void>()> resetRegionFormantCurve;
  std::function<core::Result<void>()> nudgeBreathinessUp;
  std::function<core::Result<void>()> nudgeBreathinessDown;
  std::function<core::Result<void>()> resetRegionBreathinessCurve;
  std::function<core::Result<void>()> nudgeTensionUp;
  std::function<core::Result<void>()> nudgeTensionDown;
  std::function<core::Result<void>()> resetRegionTensionCurve;
  std::function<core::Result<void>()> nudgeAirinessUp;
  std::function<core::Result<void>()> nudgeAirinessDown;
  std::function<core::Result<void>()> resetRegionAirinessCurve;
  std::function<core::Result<void>()> editTrackStyle;
  std::function<core::Result<void>()> editJapaneseReading;
  // The callback owns the bounded conversion-review surface. Returning false
  // cancels without replacing the current document.
  std::function<core::Result<bool>(const authoring::InterchangeImportDraft&)>
      reviewInterchangeImport;
  std::function<core::Result<void>()> removeSelectedOverlaps;
  std::function<core::Result<void>()> closeSelectedGaps;
  std::function<core::Result<void>()> autoLegatoSelectedNotes;
  std::function<core::Result<void>()> clearRegionDynamicsCurve;
  // Application-owned neural execution. A surface that ships a signed neural
  // deployment fills both fields: the descriptor and release key select the
  // first-party helper, and the root is where installed bundles are indexed. A
  // project may then select an installed bundle by identity. Without them a neural
  // track still fails as a saved selection this installation cannot resolve, and
  // no other voice is ever substituted for it.
  std::optional<authoring::NeuralSelectionSurface> neuralSelection;
  std::filesystem::path neuralResourceRoot{};
  std::size_t neuralMaximumResources{64U};
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
  [[nodiscard]] core::Result<void> createNewProject(
      authoring::NewProjectRequest request);
  [[nodiscard]] core::Result<authoring::ExportResult> exportSet(
      const std::filesystem::path& destination,
      authoring::ExportSettings settings = {});
  [[nodiscard]] core::Result<void> startExportSet(
      const std::filesystem::path& destination,
      authoring::ExportSettings settings = {});
  void cancelExport() noexcept;
  [[nodiscard]] bool exportInProgress() const noexcept;
  [[nodiscard]] const native_ui::ExportProgressPanelModel& exportProgress()
      const noexcept {
    return exportProgress_;
  }
  [[nodiscard]] std::optional<authoring::ExportResult> lastExport()
      const noexcept {
    std::lock_guard lock(exportMutex_);
    return lastExport_;
  }
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
  // Installed neural singers this surface can run, in registry order. Empty when
  // the surface ships no verified deployment, which is the honest answer rather
  // than a list of bundles nothing could execute.
  [[nodiscard]] std::vector<platform::NeuralResourceMenuItem> neuralResources() const override;
  // Selects or clears the neural singer of the selected track. The identity is
  // resolved through the installed index before the edit, so a selection this
  // installation cannot run is never saved into a project.
  [[nodiscard]] core::Result<void> selectNeuralResource(
      std::string_view id,std::string_view version,std::string_view contentHash) override;
  [[nodiscard]] core::Result<void> clearNeuralResource() override;
  // Performance proposals recorded for the selected region that still await a
  // decision. An accepted take is listed with its state so a surface can show the
  // current choice; a take that was already rejected is not offered again.
  [[nodiscard]] std::vector<platform::PerformanceTakeMenuItem> performanceTakes()
      const override;
  // Decides one recorded proposal by identity. Accepting selects the take over its
  // own captured span for every channel it carries; rejecting records the decision
  // on the take. Neither action edits the take's musical payload.
  [[nodiscard]] core::Result<void> acceptPerformanceTake(
      std::string_view id,
      platform::PerformanceEditScope scope,
      std::vector<std::string> channels) override;
  [[nodiscard]] core::Result<void> rejectPerformanceTake(
      std::string_view id) override;
  // Alternate-take comparison: the candidate take is applied while the previous
  // selection state is held, so the creator can play one passage from one playhead
  // and swap between the two states. Every swap is an ordinary undoable edit.
  [[nodiscard]] core::Result<void> beginPerformanceComparison(
      std::string_view id,
      platform::PerformanceEditScope scope,
      std::vector<std::string> channels) override;
  [[nodiscard]] core::Result<void> proposeAutomaticPerformance(
      platform::PerformanceEditScope scope,
      std::vector<std::string> channels) override;
  [[nodiscard]] core::Result<void> swapPerformanceComparison() override;
  [[nodiscard]] core::Result<void> endPerformanceComparison() override;
  [[nodiscard]] std::optional<platform::PerformanceComparisonMenuItem>
  performanceComparison() const override;
  // Runs the production automatic-performance backend on the selected region and
  // adopts the result as a Proposed take. Acceptance stays a separate action, and
  // an edit that moved the material on refuses instead of publishing.
  // Runs the production automatic-performance backend on the selected region, or on
  // the span the creator selected when asked for that scope, and adopts the result as
  // a Proposed take. Acceptance stays a separate action, and an edit that moved the
  // material on refuses instead of publishing.
  [[nodiscard]] core::Result<void> proposeAutomaticPerformance(
      platform::PerformanceEditScope scope = platform::PerformanceEditScope::Whole);

  [[nodiscard]] const std::vector<authoring::VoicebankCard>& voicebankCards()
      const noexcept { return voicebankBrowser_.cards(); }
  [[nodiscard]] core::Result<authoring::VoicebankInstallResult> installVoicebank(
      const std::filesystem::path& packagePath,
      authoring::ExistingVoicebankDecision decision =
          authoring::ExistingVoicebankDecision::Reject);
  [[nodiscard]] core::Result<voicebank::VoicebankResolution> relinkVoicebank(
      domain::TrackId trackId, voicebank::VoicebankSearchRoot root);
  [[nodiscard]] core::Result<void> relinkVoicebankFromDialog();
  [[nodiscard]] core::Result<void> selectProceduralRecipeFromDialog(bool relink = false);
  [[nodiscard]] core::Result<void> relinkBackingMediaFromDialog();
  [[nodiscard]] core::Result<void> replaceVoicebank(
      domain::TrackId trackId, std::string_view id, std::string_view version,
      std::string_view contentHash);
  [[nodiscard]] std::vector<platform::DocumentationMenuItem> documentation()
      const override;
  [[nodiscard]] core::Result<void> openDocumentation(
      std::string_view id) override;
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
  [[nodiscard]] core::Result<void> openInterchangePath(
      const std::filesystem::path& path);
  [[nodiscard]] core::Result<void> exportScoreFromDialog();
  [[nodiscard]] core::Result<void> recordCurrentProject();
  [[nodiscard]] core::Result<void> exportAudio();
  [[nodiscard]] core::Result<void> exportSetFromDialog(bool bakeCandidates = false);
  struct ExportRequest final {
    domain::Project project;
    std::vector<rendering::TrackSingerSource> voicebanks;
    domain::TrackId activeTrack;
    domain::RegionId activeRegion;
    std::uint64_t revision{0U};
    std::filesystem::path destination;
    authoring::ExportSettings settings;
  };
  [[nodiscard]] core::Result<ExportRequest> makeExportRequest(
      const std::filesystem::path& destination,
      authoring::ExportSettings settings) const;
  [[nodiscard]] core::Result<authoring::ExportResult> runExport(
      ExportRequest request, std::stop_token stopToken);
  [[nodiscard]] core::Result<void> refreshVoicebankBrowser();
  [[nodiscard]] std::optional<voicebank::VoicebankCandidate> findCandidate(
      std::string_view id, std::string_view version,
      std::string_view contentHash) const;
  void notifyStateChanged() const;
  void notifyProgressChanged() const;
  [[nodiscard]] authoring::NewProjectRequest defaultNewProject() const;
  // Adds the selected neural source for one track, replacing any resolved bank
  // source for the same track. A track whose saved selection cannot be selected
  // fails the render: a different voice is never substituted for it.
  [[nodiscard]] core::Result<void> appendNeuralSource(
      domain::TrackId trackId,const domain::NeuralResourceReference& reference,
      std::vector<rendering::TrackSingerSource>& sources) const;

  AuthoringSession& session_;
  std::unique_ptr<platform::IFileDialog> fileDialog_;
  std::unique_ptr<platform::IUnsavedChangesPrompt> unsavedPrompt_;
  StandaloneApplicationControllerConfig config_;
  std::function<void()> requestQuit_;
  authoring::AutosaveService autosave_;
  authoring::ExportService exportService_;
  authoring::RecentProjectsStore recentProjects_;
  authoring::VoicebankBrowserModel voicebankBrowser_;
  std::unique_ptr<authoring::VoicebankInstallerService> voicebankInstaller_;
  std::optional<authoring::NeuralSelectionService> neuralSelection_;
  std::optional<authoring::NeuralResourceRegistry> neuralResources_;
  std::uint64_t automaticProposalCounter_{0U};
  // Fixed so the same material and take identity always produce the same proposal;
  // the counter is what makes successive proposals distinct takes.
  std::uint64_t automaticProposalSeed_{1U};
  // The accepted selections one decision over a take would introduce, using the
  // same span and channel rule for accepting and for comparing.
  [[nodiscard]] core::Result<std::vector<domain::AcceptedPerformanceSelection>>
  performanceTakeSelections(domain::RegionId regionId, std::string_view id,
      platform::PerformanceEditScope scope,
      const std::vector<std::string>& channels) const;
  // The span the creator's current note selection covers inside one region.
  [[nodiscard]] core::Result<domain::PerformanceTimeRange> selectedNotesRange(
      domain::RegionId regionId) const;
  // Applies an exact accepted-selection list as one undoable edit.
  [[nodiscard]] core::Result<void> applyAcceptedSelections(
      domain::RegionId regionId,
      const std::vector<domain::AcceptedPerformanceSelection>& selections);
  // Held alternate-take comparison. Session state only: it names a take and the two
  // selection states, and the project remains the single owner of what is applied.
  struct PerformanceComparisonState final {
    domain::RegionId regionId;
    std::string takeId;
    std::string label;
    std::vector<domain::AcceptedPerformanceSelection> previous;
    std::vector<domain::AcceptedPerformanceSelection> candidate;
    bool candidateApplied{false};
  };
  std::optional<PerformanceComparisonState> performanceComparison_;
  native_ui::ExportProgressPanelModel exportProgress_;
  std::optional<authoring::ExportResult> lastExport_;
  std::stop_source exportStopSource_;
  mutable std::mutex exportMutex_;
  std::jthread exportWorker_;
  bool exportRunning_{false};
};

}  // namespace seam::standalone
