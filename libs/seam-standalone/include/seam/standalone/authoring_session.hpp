#pragma once

#include "seam/authoring/authoring_runtime.hpp"
#include "seam/authoring/autosave_service.hpp"
#include "seam/authoring/media_import_service.hpp"
#include "seam/authoring/interchange_service.hpp"
#include "seam/authoring/project_lifecycle.hpp"
#include "seam/character/performance.hpp"
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
  bool bindFirstAvailableVoicebank{false};
  bool allowDevelopmentVoicebanks{false};
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
  // Import preparation is side-effect free for the active document.  The
  // caller presents the returned bounded loss report, then explicitly accepts
  // the draft through acceptInterchangeImport().
  [[nodiscard]] core::Result<authoring::InterchangeImportDraft>
  prepareInterchangeImport(const std::filesystem::path& path,
                           authoring::InterchangeImportRequest request = {}) const;
  [[nodiscard]] core::Result<void> acceptInterchangeImport(
      authoring::InterchangeImportDraft draft);
  [[nodiscard]] core::Result<authoring::InterchangeExportReceipt>
  exportInterchange(authoring::InterchangeExportRequest request) const;
  [[nodiscard]] core::Result<authoring::InterchangeExportDraft>
  prepareInterchangeExport(authoring::InterchangeExportRequest request) const;
  [[nodiscard]] core::Result<void> recoverProject(
      authoring::AutosaveService& autosave,
      const authoring::RecoveryCandidate& candidate);
  [[nodiscard]] core::Result<authoring::MediaImportResult> importBackingMedia(
      const std::filesystem::path& sourcePath, authoring::MediaImportMode mode,
      std::string trackName, time::Tick startTick);
  [[nodiscard]] core::Result<void> relinkBackingMedia(
      domain::TrackId trackId, const std::filesystem::path& sourcePath);

  // The performance read model for the phrase the current render published. Evaluation happens on
  // the calling thread and only once per published request, so the render callback never publishes
  // presentation state from a worker. It is absent whenever no published render is bound to a
  // complete identity, which is what keeps the dock from drawing a mouth for audio nobody can hear.
  // The generation counter changes on every evaluation, so a host binds the dock once per published
  // phrase instead of comparing whole snapshots every frame.
  [[nodiscard]] const character::CharacterPerformanceSnapshot* characterPerformance();
  [[nodiscard]] std::uint64_t characterPerformanceGeneration() const noexcept {
    return characterPerformanceGeneration_;
  }
  [[nodiscard]] const std::string& characterPerformanceDiagnostic() const noexcept {
    return characterPerformanceDiagnostic_;
  }
  // Whether the audible render the dock is following still describes the project. The dock keeps
  // showing the phrase a listener is hearing; this is how a host says that the project has moved on
  // since that phrase was rendered.
  [[nodiscard]] bool characterPerformanceStale() const noexcept;
  // The dock's frame for one transport position, or nothing when no phrase is bound. The mapping is
  // the project's own tempo map at the published render's sample rate, so a seek or a loop lands on
  // the frame the audio itself is at.
  [[nodiscard]] std::optional<character::CharacterPerformanceFrame> characterPerformanceFrameAt(
      time::Tick position) const noexcept;

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
  std::optional<character::CharacterPerformanceSnapshot> characterPerformance_;
  bool characterPerformanceEvaluated_{false};
  std::uint64_t characterPerformanceRequest_{0U};
  std::uint64_t characterPerformanceRevision_{0U};
  bool characterPerformanceAudition_{false};
  domain::TrackId characterPerformanceSelectedTrack_{};
  domain::RegionId characterPerformanceSelectedRegion_{};
  std::uint64_t characterPerformanceGeneration_{0U};
  std::string characterPerformanceDiagnostic_;
};

}  // namespace seam::standalone
