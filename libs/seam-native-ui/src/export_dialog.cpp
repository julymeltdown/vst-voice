#include "seam/native_ui/export_dialog.hpp"

#include <algorithm>
#include <filesystem>

namespace seam::native_ui {

bool ExportDialogModel::canExport() const noexcept {
  return issues_.empty() ||
         std::none_of(issues_.begin(), issues_.end(),
                      [](const auto& issue) { return issue.blocking; });
}

core::Result<void> ExportDialogModel::preflight(
    const domain::Project& project) {
  issues_.clear();
  if (destination_.empty()) {
    issues_.push_back(ExportPreflightIssue{true, "DESTINATION_EMPTY",
                                           "Choose an export destination"});
  }
  if (!settings_.includeMaster && !settings_.includeStems) {
    issues_.push_back(ExportPreflightIssue{true, "NO_OUTPUTS",
                                           "Select a master or at least one stem"});
  }
  if (settings_.channels != project.routing().deviceOutputChannels) {
    issues_.push_back(ExportPreflightIssue{
        true, "CHANNEL_MISMATCH",
        "Export channels must match the project output routing"});
  }
  const auto validation = project.validate();
  if (!validation) {
    issues_.push_back(ExportPreflightIssue{true, "PROJECT_INVALID",
                                           validation.error().message});
  }
  if (!destination_.empty() && std::filesystem::exists(destination_) &&
      !settings_.replaceExisting) {
    issues_.push_back(ExportPreflightIssue{
        true, "DESTINATION_EXISTS", "Destination already contains an export set"});
  }
  return canExport() ? core::success()
                     : core::failure(core::ErrorCode::InvalidArgument,
                                     "Export preflight has blocking issues");
}

}
