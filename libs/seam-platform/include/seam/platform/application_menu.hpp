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
  ImportAudio,
  InstallVoicebank,
  RelinkVoicebank,
  RelinkBackingAudio,
  OpenAudioSettings,
  ExportSet,
  ExportAudio,
  Quit,
  Undo,
  Redo,
  TogglePlayback,
  StopPlayback,
  ToggleLoop,
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

struct VoicebankMenuItem final {
  std::string id;
  std::string version;
  std::string contentHash;
  std::string displayName;
  std::string trustLabel;
  bool selectable{false};
  bool selected{false};
};

struct DocumentationMenuItem final {
  std::string id;
  std::string displayName;
  std::filesystem::path path;
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
  [[nodiscard]] virtual std::vector<VoicebankMenuItem> voicebanks() const {
    return {};
  }
  [[nodiscard]] virtual core::Result<void> selectVoicebank(
      std::string_view, std::string_view, std::string_view) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Voicebank selection is not supported");
  }
  [[nodiscard]] virtual std::vector<DocumentationMenuItem> documentation()
      const {
    return {};
  }
  [[nodiscard]] virtual core::Result<void> openDocumentation(
      std::string_view) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Offline documentation is not supported");
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
[[nodiscard]] core::Result<void> openDocumentationPath(
    const std::filesystem::path& path);
[[nodiscard]] core::Result<void> openExternalPath(
    const std::filesystem::path& path);
[[nodiscard]] core::Result<void> copyTextToClipboard(std::string_view text);
[[nodiscard]] core::Result<bool> requestEulaAcceptance(
    const std::filesystem::path& path);

}  // namespace seam::platform
