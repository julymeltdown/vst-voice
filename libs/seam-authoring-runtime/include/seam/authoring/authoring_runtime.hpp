#pragma once

#include "seam/authoring/project_document.hpp"
#include "seam/authoring/render_coordinator.hpp"
#include "seam/authoring/technical_edit_controller.hpp"
#include "seam/authoring/transport_controller.hpp"
#include "seam/authoring/voicebank_session.hpp"
#include "seam/core/result.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
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

  [[nodiscard]] core::Result<void> setPreviewSampleRate(std::uint32_t sampleRate);
  void setRenderQuality(rendering::RenderQuality quality) noexcept;
  [[nodiscard]] rendering::RenderQuality renderQuality() const noexcept {
    return renderQuality_;
  }
  void setCompletionCallback(std::function<void()> callback);
  void requestPreview();
  void handleDocumentChanged();

private:
  [[nodiscard]] TechnicalRenderView currentTechnicalRenderView() const;
  [[nodiscard]] std::pair<domain::TrackId, domain::RegionId>
  firstRenderableSelection(const domain::Project& project,
                           const std::vector<TrackVoicebankState>& states) const;
  void publishCompletedAudio();

  std::unique_ptr<ProjectDocument> document_;
  AuthoringRuntimeConfig config_;
  VoicebankSession voicebanks_;
  AuthoringRenderCoordinator renderer_;
  TransportController transport_;
  domain::TrackId selectedTrack_{};
  domain::RegionId selectedRegion_{};
  TechnicalEditController technicalEdits_;
  std::uint32_t previewSampleRate_{48000U};
  rendering::RenderQuality renderQuality_{rendering::RenderQuality::Preview};
  mutable std::mutex callbackMutex_;
  std::function<void()> completionCallback_;
  bool initialized_{false};
};

}  // namespace seam::authoring
