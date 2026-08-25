#include "seam/native_ui/new_project_dialog.hpp"

#include <algorithm>

namespace seam::native_ui {

NewProjectDialogModel::NewProjectDialogModel(
    std::vector<voicebank::VoicebankCandidate> candidates)
    : candidates_(std::move(candidates)) {}

core::Result<void> NewProjectDialogModel::selectVoicebank(std::size_t index) {
  if (index >= candidates_.size()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Voicebank selection index is out of range");
  }
  if (!createInitialVocalTrack_) {
    return core::failure(core::ErrorCode::Conflict,
                         "A voicebank requires an initial vocal track");
  }
  selectedVoicebank_ = candidates_[index];
  return core::success();
}

core::Result<void> NewProjectDialogModel::selectVoicebank(
    std::string_view id, std::string_view version,
    std::string_view contentHash) {
  const auto iterator = std::find_if(
      candidates_.begin(), candidates_.end(),
      [id, version, contentHash](const auto& candidate) {
        return candidate.manifest.id == id &&
               candidate.manifest.version == version &&
               candidate.contentHash == contentHash;
      });
  if (iterator == candidates_.end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Requested voicebank candidate was not found");
  }
  return selectVoicebank(static_cast<std::size_t>(
      std::distance(candidates_.begin(), iterator)));
}

core::Result<authoring::NewProjectRequest> NewProjectDialogModel::submit() const {
  if (cancelled_) {
    return core::failure<authoring::NewProjectRequest>(
        core::ErrorCode::Conflict, "New project dialog was cancelled");
  }
  return authoring::NewProjectRequest{
      .name = name_,
      .tempoBpm = tempoBpm_,
      .meterNumerator = meterNumerator_,
      .meterDenominator = meterDenominator_,
      .sampleRate = sampleRate_,
      .outputChannels = outputChannels_,
      .createInitialVocalTrack = createInitialVocalTrack_,
      .initialVoicebank = selectedVoicebank_,
      .projectPath = projectPath_,
  };
}

}
