#pragma once

#include "seam/authoring/export_service.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace seam::native_ui {

enum class ExportScope { Project, Loop, Selection };

struct ExportPreflightIssue final {
  bool blocking{true};
  std::string code;
  std::string message;
};

class ExportDialogModel final {
public:
  void setDestination(std::filesystem::path destination) {
    destination_ = std::move(destination);
  }
  void setScope(ExportScope scope) noexcept { scope_ = scope; }
  void setSettings(authoring::ExportSettings settings) noexcept {
    settings_ = settings;
  }
  void setReplaceExisting(bool replace) noexcept {
    settings_.replaceExisting = replace;
  }
  [[nodiscard]] const std::filesystem::path& destination() const noexcept {
    return destination_;
  }
  [[nodiscard]] ExportScope scope() const noexcept { return scope_; }
  [[nodiscard]] const authoring::ExportSettings& settings() const noexcept {
    return settings_;
  }
  [[nodiscard]] const std::vector<ExportPreflightIssue>& issues() const noexcept {
    return issues_;
  }
  [[nodiscard]] bool canExport() const noexcept;
  [[nodiscard]] core::Result<void> preflight(
      const domain::Project& project);

private:
  std::filesystem::path destination_;
  ExportScope scope_{ExportScope::Project};
  authoring::ExportSettings settings_;
  std::vector<ExportPreflightIssue> issues_;
};

}
