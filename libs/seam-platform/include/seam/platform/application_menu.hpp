#pragma once

#include "seam/core/result.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace seam::platform {

enum class ApplicationCommand {
  NewProject,
  OpenProject,
  RecoverLatestAutosave,
  SaveProject,
  SaveProjectAs,
  ExportAudio,
  Quit,
  Undo,
  Redo,
  TogglePlayback,
};

struct RecentProjectMenuItem final {
  std::filesystem::path path;
  std::string displayName;
  bool missing{false};
};

struct RecoveryMenuItem final {
  std::filesystem::path metadataPath;
  std::string displayName;
};

class IApplicationCommandDispatcher {
public:
  virtual ~IApplicationCommandDispatcher() = default;
  [[nodiscard]] virtual core::Result<void> dispatch(
      ApplicationCommand command) = 0;
  [[nodiscard]] virtual std::vector<RecentProjectMenuItem> recentProjects()
      const { return {}; }
  [[nodiscard]] virtual core::Result<void> openRecentProject(
      const std::filesystem::path&) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Recent projects are not supported");
  }
  [[nodiscard]] virtual std::vector<RecoveryMenuItem> recoveryItems() const {
    return {};
  }
  [[nodiscard]] virtual core::Result<void> recoverAutosave(
      const std::filesystem::path&) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Autosave recovery is not supported");
  }
};

class IApplicationMenu {
public:
  virtual ~IApplicationMenu() = default;
  [[nodiscard]] virtual core::Result<void> install(
      IApplicationCommandDispatcher& dispatcher) = 0;
  virtual void refresh() noexcept = 0;
  virtual void uninstall() noexcept = 0;
};

enum class UnsavedDecision { Save, Discard, Cancel };

class IUnsavedChangesPrompt {
public:
  virtual ~IUnsavedChangesPrompt() = default;
  [[nodiscard]] virtual core::Result<UnsavedDecision> choose(
      std::string_view projectName) = 0;
};

[[nodiscard]] std::unique_ptr<IApplicationMenu> createNativeApplicationMenu();
[[nodiscard]] std::unique_ptr<IUnsavedChangesPrompt>
createNativeUnsavedChangesPrompt();

}  // namespace seam::platform
