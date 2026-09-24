#pragma once

#include "seam/authoring/project_document.hpp"
#include "seam/authoring/diagnostic.hpp"
#include "seam/authoring/render_coordinator.hpp"
#include "seam/authoring/technical_edit_controller.hpp"
#include "seam/authoring/transport_controller.hpp"
#include "seam/authoring/voicebank_session.hpp"
#include "seam/core/result.hpp"

#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace seam::authoring {

struct AuthoringRuntimeConfig final {
  std::filesystem::path cacheRoot;
  std::vector<voicebank::VoicebankSearchRoot> voicebankRoots;
  std::uint32_t previewSampleRate{48000U};
  std::uint8_t outputChannels{2U};
  bool allowDevelopmentVoicebanks{false};
  bool enableTransport{true};
};

class AuthoringRuntime final {
public:
  struct AudiblePublication final {
    std::shared_ptr<const PublishedProjectAudio> audio;
    bool performanceAudition{false};
    bool stale{false};
  };
  AuthoringRuntime(std::unique_ptr<ProjectDocument> document,
                   AuthoringRuntimeConfig config);
  ~AuthoringRuntime();

  AuthoringRuntime(const AuthoringRuntime&) = delete;
  AuthoringRuntime& operator=(const AuthoringRuntime&) = delete;

  [[nodiscard]] core::Result<void> initialize();
  void shutdown() noexcept;

  [[nodiscard]] ProjectDocument& document() noexcept { return *document_; }
  [[nodiscard]] const ProjectDocument& document() const noexcept {
    return *document_;
  }
  [[nodiscard]] VoicebankSession& voicebanks() noexcept { return voicebanks_; }
  [[nodiscard]] const VoicebankSession& voicebanks() const noexcept {
    return voicebanks_;
  }
  [[nodiscard]] AuthoringRenderCoordinator& renderer() noexcept {
    return renderer_;
  }
  [[nodiscard]] const AuthoringRenderCoordinator& renderer() const noexcept {
    return renderer_;
  }
  [[nodiscard]] TransportController& transport() noexcept { return transport_; }
  [[nodiscard]] const TransportController& transport() const noexcept {
    return transport_;
  }
  [[nodiscard]] TechnicalEditController& technicalEdits() noexcept {
    return technicalEdits_;
  }
  [[nodiscard]] const TechnicalEditController& technicalEdits() const noexcept {
    return technicalEdits_;
  }
  [[nodiscard]] std::vector<Diagnostic> diagnostics() const {
    std::lock_guard lock(diagnosticsMutex_);
    return diagnostics_;
  }
  void clearDiagnostics() noexcept {
    std::lock_guard lock(diagnosticsMutex_);
    diagnostics_.clear();
  }

  [[nodiscard]] core::Result<void> selectTrack(domain::TrackId trackId);
  [[nodiscard]] core::Result<void> selectRegion(domain::RegionId regionId);
  [[nodiscard]] domain::TrackId selectedTrack() const noexcept {
    return selectedTrack_;
  }
  [[nodiscard]] domain::RegionId selectedRegion() const noexcept {
    return selectedRegion_;
  }

  [[nodiscard]] core::Result<void> execute(
      std::unique_ptr<application::ICommand> command);
  [[nodiscard]] core::Result<void> executePerformanceResult(
      const application::PerformanceJobContext& context,
      std::unique_ptr<application::ICommand> command);
  [[nodiscard]] core::Result<void> undo();
  [[nodiscard]] core::Result<void> redo();
  [[nodiscard]] core::Result<void> previewSeam(domain::PhonemeKey key,
                                               bool alternate);
  // Render one alternate accepted-take selection from a copy of the current project. The document,
  // undo stack, autosave and export input are never changed by auditioning. The transport retains its
  // playhead while switching the published timeline. Owner-thread requests; publication is async.
  [[nodiscard]] core::Result<void> auditionPerformance(
      domain::RegionId regionId,
      std::vector<domain::AcceptedPerformanceSelection> accepted);
  [[nodiscard]] core::Result<void> stopPerformanceAudition();
  [[nodiscard]] bool performanceAuditionActive() const noexcept {
    return performanceAuditionActive_.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool performanceAuditionReady() const noexcept {
    return performanceAuditionReady_.load(std::memory_order_acquire);
  }
  [[nodiscard]] AudiblePublication audiblePublication() const;
  [[nodiscard]] bool seamPreviewActive() const noexcept {
    return seamPreviewActive_.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool seamPreviewReady() const noexcept {
    return seamPreviewReady_.load(std::memory_order_acquire);
  }

  [[nodiscard]] core::Result<void> setPreviewSampleRate(std::uint32_t sampleRate);
  [[nodiscard]] core::Result<void> reconfigureAudio(
      std::uint32_t sampleRate, std::uint8_t outputChannels,
      std::size_t blockFrames);
  void setRenderQuality(rendering::RenderQuality quality);
  [[nodiscard]] rendering::RenderQuality renderQuality() const noexcept {
    return renderQuality_;
  }
  // Optional tempo map that replaces the document's map for subsequent renders. A host
  // that owns timing (Follow Host) supplies an acquired map here, so the render follows
  // the host's own events instead of one instantaneous BPM. The substituted project is
  // what gets rendered and what the render identity covers, so audio can never be
  // attributed to a map it was not rendered with. Owner-thread only, like every other
  // render setting.
  void setTempoMapOverride(std::optional<time::TempoMap> map);
  void setCompletionCallback(std::function<void()> callback);
  // Owner-thread resource/document invalidation. Retain historical PCM but
  // revoke current measurement authority and every pending debounce request.
  void invalidatePreview();
  void requestPreview(
      bool immediate = false,
      application::CommandImpact impact = application::CommandImpact{
          .scope = application::CommandAudioImpact::ProjectAudio,
          .projectWide = true});
  void handleDocumentChanged();

private:
  [[nodiscard]] core::Result<void> afterCommandExecution(
      core::Result<void> result, application::CommandImpact impact);
  struct PreviewRequest final {
    domain::Project project;
    std::vector<rendering::TrackSingerSource> voicebanks;
    domain::TrackId activeTrack;
    domain::RegionId activeRegion;
    std::uint64_t revision{0U};
    std::uint32_t sampleRate{48000U};
    rendering::RenderQuality quality{rendering::RenderQuality::Preview};
    application::CommandImpact impact{
        .scope = application::CommandAudioImpact::ProjectAudio,
        .projectWide = true};
  };

  [[nodiscard]] std::optional<PreviewRequest> makePreviewRequest(
      application::CommandImpact impact) const;
  void submitPreview(PreviewRequest request, bool immediate);
  void previewWorkerLoop(std::stop_token stopToken);
  [[nodiscard]] TechnicalRenderView currentTechnicalRenderView() const;
  [[nodiscard]] std::pair<domain::TrackId, domain::RegionId>
  firstRenderableSelection(const domain::Project& project,
                           const std::vector<TrackVoicebankState>& states) const;
  void publishCompletedAudio();
  void publishCompletedSeamPreview();
  void publishCompletedPerformanceAudition();
  void recordDiagnostic(const core::Error& error);
  void recordRenderFailure(RenderFailureKind failure, std::string message);
  void clearRenderDiagnostics() noexcept;

  std::unique_ptr<ProjectDocument> document_;
  AuthoringRuntimeConfig config_;
  VoicebankSession voicebanks_;
  AuthoringRenderCoordinator renderer_;
  AuthoringRenderCoordinator seamPreviewRenderer_;
  AuthoringRenderCoordinator performanceAuditionRenderer_;
  TransportController transport_;
  domain::TrackId selectedTrack_{};
  domain::RegionId selectedRegion_{};
  TechnicalEditController technicalEdits_;
  std::uint32_t previewSampleRate_{48000U};
  rendering::RenderQuality renderQuality_{rendering::RenderQuality::Preview};
  // Absent means the document's own tempo map is authoritative for rendering.
  std::optional<time::TempoMap> tempoMapOverride_;
  mutable std::mutex callbackMutex_;
  std::function<void()> completionCallback_;
  mutable std::mutex technicalViewMutex_;
  std::vector<TechnicalUnitView> lastTechnicalUnits_;
  mutable std::mutex previewMutex_;
  std::condition_variable_any previewCondition_;
  std::optional<PreviewRequest> pendingPreview_;
  std::chrono::steady_clock::time_point previewDeadline_{};
  std::uint64_t previewGeneration_{0U};
  std::jthread previewWorker_;
  std::atomic<bool> seamPreviewActive_{false};
  std::atomic<bool> seamPreviewReady_{false};
  mutable std::mutex performanceAuditionMutex_;
  std::atomic<bool> performanceAuditionActive_{false};
  std::atomic<bool> performanceAuditionReady_{false};
  bool initialized_{false};
  mutable std::mutex diagnosticsMutex_;
  std::vector<Diagnostic> diagnostics_;
};

}  // namespace seam::authoring
