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
  [[nodiscard]] core::Result<void> undo();
  [[nodiscard]] core::Result<void> redo();
  [[nodiscard]] core::Result<void> previewSeam(domain::PhonemeKey key,
                                               bool alternate);
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
  void setCompletionCallback(std::function<void()> callback);
  void requestPreview(
      bool immediate = false,
      application::CommandImpact impact = application::CommandImpact{
          .scope = application::CommandAudioImpact::ProjectAudio,
          .projectWide = true});
  void handleDocumentChanged();

private:
  struct PreviewRequest final {
    domain::Project project;
    std::vector<rendering::TrackVoicebankSource> voicebanks;
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
  void recordDiagnostic(const core::Error& error);
  void recordRenderFailure(RenderFailureKind failure, std::string message);
  void clearRenderDiagnostics() noexcept;

  std::unique_ptr<ProjectDocument> document_;
  AuthoringRuntimeConfig config_;
  VoicebankSession voicebanks_;
  AuthoringRenderCoordinator renderer_;
  AuthoringRenderCoordinator seamPreviewRenderer_;
  TransportController transport_;
  domain::TrackId selectedTrack_{};
  domain::RegionId selectedRegion_{};
  TechnicalEditController technicalEdits_;
  std::uint32_t previewSampleRate_{48000U};
  rendering::RenderQuality renderQuality_{rendering::RenderQuality::Preview};
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
  bool initialized_{false};
  mutable std::mutex diagnosticsMutex_;
  std::vector<Diagnostic> diagnostics_;
};

}  // namespace seam::authoring
