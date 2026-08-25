#pragma once

#include "seam/authoring/project_lifecycle.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace seam::native_ui {

class NewProjectDialogModel final {
public:
  explicit NewProjectDialogModel(
      std::vector<voicebank::VoicebankCandidate> candidates = {});

  void setName(std::string name) { name_ = std::move(name); }
  void setTempoBpm(double tempoBpm) noexcept { tempoBpm_ = tempoBpm; }
  void setMeter(std::uint8_t numerator, std::uint8_t denominator) noexcept {
    meterNumerator_ = numerator;
    meterDenominator_ = denominator;
  }
  void setSampleRate(std::uint32_t sampleRate) noexcept {
    sampleRate_ = sampleRate;
  }
  void setOutputChannels(std::uint8_t channels) noexcept {
    outputChannels_ = channels;
  }
  void setProjectPath(std::filesystem::path path) {
    projectPath_ = std::move(path);
  }
  void setCreateInitialVocalTrack(bool enabled) noexcept {
    createInitialVocalTrack_ = enabled;
    if (!enabled) selectedVoicebank_.reset();
  }

  [[nodiscard]] core::Result<void> selectVoicebank(std::size_t index);
  [[nodiscard]] core::Result<void> selectVoicebank(
      std::string_view id, std::string_view version,
      std::string_view contentHash);
  void clearVoicebank() noexcept { selectedVoicebank_.reset(); }
  void cancel() noexcept { cancelled_ = true; }
  void reopen() noexcept { cancelled_ = false; }

  [[nodiscard]] bool cancelled() const noexcept { return cancelled_; }
  [[nodiscard]] const std::vector<voicebank::VoicebankCandidate>& candidates()
      const noexcept {
    return candidates_;
  }
  [[nodiscard]] const std::optional<voicebank::VoicebankCandidate>&
      selectedVoicebank() const noexcept {
    return selectedVoicebank_;
  }
  [[nodiscard]] const std::optional<std::filesystem::path>& projectPath()
      const noexcept {
    return projectPath_;
  }
  [[nodiscard]] core::Result<authoring::NewProjectRequest> submit() const;

private:
  std::vector<voicebank::VoicebankCandidate> candidates_;
  std::string name_;
  double tempoBpm_{120.0};
  std::uint8_t meterNumerator_{4U};
  std::uint8_t meterDenominator_{4U};
  std::uint32_t sampleRate_{48000U};
  std::uint8_t outputChannels_{2U};
  bool createInitialVocalTrack_{true};
  bool cancelled_{false};
  std::optional<voicebank::VoicebankCandidate> selectedVoicebank_;
  std::optional<std::filesystem::path> projectPath_;
};

}
