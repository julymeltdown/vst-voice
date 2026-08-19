#include "seam/standalone/application_controller.hpp"

#include <algorithm>
#include <utility>

namespace seam::standalone {
namespace {

std::filesystem::path initialDirectory(
    const authoring::ProjectDocument& document) {
  if (document.identity().projectPath.has_value()) {
    return document.identity().projectPath->parent_path();
  }
  return {};
}

}  // namespace

core::Result<std::unique_ptr<StandaloneApplicationController>>
StandaloneApplicationController::create(
    AuthoringSession& session,
    std::unique_ptr<platform::IFileDialog> fileDialog,
    std::unique_ptr<platform::IUnsavedChangesPrompt> unsavedPrompt,
    StandaloneApplicationControllerConfig config,
    std::function<void()> requestQuit) {
  if (fileDialog == nullptr || unsavedPrompt == nullptr) {
    return core::failure<std::unique_ptr<StandaloneApplicationController>>(
        core::ErrorCode::InvalidArgument,
        "Standalone application services cannot be null");
  }
  if (config.autosaveRoot.empty() || config.recentProjectsPath.empty()) {
    return core::failure<std::unique_ptr<StandaloneApplicationController>>(
        core::ErrorCode::InvalidArgument,
        "Standalone project-lifecycle paths cannot be empty");
  }
  auto result = std::unique_ptr<StandaloneApplicationController>{
      new StandaloneApplicationController(
          session, std::move(fileDialog), std::move(unsavedPrompt),
          std::move(config), std::move(requestQuit))};
  auto initialized = result->initialize();
  if (!initialized) {
    return core::Result<std::unique_ptr<StandaloneApplicationController>>{
        initialized.error()};
  }
  return result;
}

StandaloneApplicationController::StandaloneApplicationController(
    AuthoringSession& session,
    std::unique_ptr<platform::IFileDialog> fileDialog,
    std::unique_ptr<platform::IUnsavedChangesPrompt> unsavedPrompt,
    StandaloneApplicationControllerConfig config,
    std::function<void()> requestQuit)
    : session_(session),
      fileDialog_(std::move(fileDialog)),
      unsavedPrompt_(std::move(unsavedPrompt)),
      config_(std::move(config)),
      requestQuit_(std::move(requestQuit)),
      autosave_(authoring::AutosaveConfig{
          .root = config_.autosaveRoot,
          .interval = std::chrono::seconds{60},
          .commandThreshold = 25U,
          .minimumCommandDelay = std::chrono::seconds{15},
          .maximumGenerations = 5U,
          .faultInjector = {},
          .wallClock = {},
      }),
      recentProjects_(config_.recentProjectsPath) {}

StandaloneApplicationController::~StandaloneApplicationController() {
  static_cast<void>(autosave_.flush());
  autosave_.shutdown();
}

core::Result<void> StandaloneApplicationController::initialize() {
  return recentProjects_.load();
}

authoring::NewProjectRequest
StandaloneApplicationController::defaultNewProject() const {
  auto request = config_.defaultNewProject;
  if (!request.initialVoicebank.has_value()) {
    const auto candidates = session_.runtime().voicebanks().candidates();
    if (!candidates.empty()) request.initialVoicebank = candidates.front();
  }
  return request;
}

core::Result<bool>
StandaloneApplicationController::confirmDestructiveAction() {
  auto& document = session_.runtime().document();
  if (!document.dirty()) return true;
  auto decision = unsavedPrompt_->choose(
      document.session().project().name());
  if (!decision) return core::Result<bool>{decision.error()};
  if (decision.value() == platform::UnsavedDecision::Cancel) return false;
  if (decision.value() == platform::UnsavedDecision::Discard) return true;

  if (!document.identity().projectPath.has_value()) {
    return chooseAndSaveAs();
  }
  auto saved = session_.saveProject();
  if (!saved) return core::Result<bool>{saved.error()};
  auto recorded = recordCurrentProject();
  if (!recorded) return core::Result<bool>{recorded.error()};
  return true;
}

core::Result<bool> StandaloneApplicationController::chooseAndSaveAs() {
  const auto& document = session_.runtime().document();
  const auto selected = fileDialog_->choose(platform::FileDialogRequest{
      .purpose = platform::FileDialogPurpose::SaveProject,
      .title = "Save Project",
      .initialDirectory = initialDirectory(document),
      .suggestedName = document.session().project().name() + ".seam",
      .extensions = {"seam"},
  });
  if (!selected) return core::Result<bool>{selected.error()};
  if (!selected.value().has_value()) return false;
  auto saved = session_.saveProjectAs(*selected.value());
  if (!saved) return core::Result<bool>{saved.error()};
  auto recorded = recordCurrentProject();
  if (!recorded) return core::Result<bool>{recorded.error()};
  return true;
}

core::Result<void> StandaloneApplicationController::recordCurrentProject() {
  const auto& document = session_.runtime().document();
  if (!document.identity().projectPath.has_value()) return core::success();
  auto recorded = recentProjects_.record(
      *document.identity().projectPath,
      document.session().project().name());
  if (!recorded) return recorded;
  auto saved = recentProjects_.save();
  if (saved) notifyStateChanged();
  return saved;
}

core::Result<void> StandaloneApplicationController::openPath(
    const std::filesystem::path& path) {
  auto opened = session_.openProject(path);
  if (!opened) return core::Result<void>{opened.error()};
  auto recorded = recordCurrentProject();
  if (recorded) notifyStateChanged();
  return recorded;
}

core::Result<void> StandaloneApplicationController::openRecent(
    const std::filesystem::path& path) {
  auto allowed = confirmDestructiveAction();
  if (!allowed) return core::Result<void>{allowed.error()};
  if (!allowed.value()) return core::success();
  return openPath(path);
}

core::Result<void> StandaloneApplicationController::dispatch(
    platform::ApplicationCommand command) {
  switch (command) {
    case platform::ApplicationCommand::NewProject: {
      auto allowed = confirmDestructiveAction();
      if (!allowed) return core::Result<void>{allowed.error()};
      if (!allowed.value()) return core::success();
      auto created = session_.createNewProject(defaultNewProject());
      if (created) notifyStateChanged();
      return created;
    }
    case platform::ApplicationCommand::OpenProject: {
      auto allowed = confirmDestructiveAction();
      if (!allowed) return core::Result<void>{allowed.error()};
      if (!allowed.value()) return core::success();
      const auto selected = fileDialog_->choose(platform::FileDialogRequest{
          .purpose = platform::FileDialogPurpose::OpenProject,
          .title = "Open Project",
          .initialDirectory = initialDirectory(session_.runtime().document()),
          .suggestedName = {},
          .extensions = {"seam"},
      });
      if (!selected) return core::Result<void>{selected.error()};
      if (!selected.value().has_value()) return core::success();
      return openPath(*selected.value());
    }
    case platform::ApplicationCommand::RecoverLatestAutosave: {
      auto candidates = recoveryCandidates();
      if (!candidates) return core::Result<void>{candidates.error()};
      const auto iterator = std::find_if(
          candidates.value().rbegin(), candidates.value().rend(),
          [](const authoring::RecoveryCandidate& value) {
            return value.recoverable;
          });
      if (iterator == candidates.value().rend()) {
        return core::failure(core::ErrorCode::NotFound,
                             "No recoverable autosave is available");
      }
      return recover(*iterator);
    }
    case platform::ApplicationCommand::SaveProject: {
      if (!session_.runtime().document().identity().projectPath.has_value()) {
        auto saved = chooseAndSaveAs();
        if (!saved) return core::Result<void>{saved.error()};
        return core::success();
      }
      auto saved = session_.saveProject();
      if (!saved) return saved;
      return recordCurrentProject();
    }
    case platform::ApplicationCommand::SaveProjectAs: {
      auto saved = chooseAndSaveAs();
      if (!saved) return core::Result<void>{saved.error()};
      return core::success();
    }
    case platform::ApplicationCommand::ExportAudio:
      return core::failure(core::ErrorCode::Unsupported,
                           "Audio export is scheduled for milestone U6");
    case platform::ApplicationCommand::Quit: {
      auto close = requestClose();
      if (!close) return core::Result<void>{close.error()};
      return core::success();
    }
    case platform::ApplicationCommand::Undo: {
      auto result = session_.runtime().undo();
      if (result) {
        static_cast<void>(onDocumentChanged());
        notifyStateChanged();
      }
      return result;
    }
    case platform::ApplicationCommand::Redo: {
      auto result = session_.runtime().redo();
      if (result) {
        static_cast<void>(onDocumentChanged());
        notifyStateChanged();
      }
      return result;
    }
    case platform::ApplicationCommand::TogglePlayback: {
      const auto state = session_.runtime().transport().state();
      return state.playing ? session_.runtime().transport().pause()
                           : session_.runtime().transport().play();
    }
  }
  return core::failure(core::ErrorCode::Unsupported,
                       "Unknown application command");
}

std::vector<platform::RecentProjectMenuItem>
StandaloneApplicationController::recentProjects() const {
  std::vector<platform::RecentProjectMenuItem> result;
  result.reserve(recentProjects_.entries().size());
  for (const auto& entry : recentProjects_.entries()) {
    result.push_back(platform::RecentProjectMenuItem{
        .path = entry.path,
        .displayName = entry.displayName,
        .missing = entry.missing,
    });
  }
  return result;
}

core::Result<void> StandaloneApplicationController::openRecentProject(
    const std::filesystem::path& path) {
  return openRecent(path);
}

std::vector<platform::RecoveryMenuItem>
StandaloneApplicationController::recoveryItems() const {
  std::vector<platform::RecoveryMenuItem> result;
  auto candidates = recoveryCandidates();
  if (!candidates) return result;
  for (const auto& candidate : candidates.value()) {
    if (!candidate.recoverable) continue;
    result.push_back(platform::RecoveryMenuItem{
        .metadataPath = candidate.metadataPath,
        .displayName = "Project " + candidate.projectId + " — revision " +
                       std::to_string(candidate.revision),
    });
  }
  return result;
}

core::Result<void> StandaloneApplicationController::recoverAutosave(
    const std::filesystem::path& metadataPath) {
  auto candidates = recoveryCandidates();
  if (!candidates) return core::Result<void>{candidates.error()};
  const auto iterator = std::find_if(
      candidates.value().begin(), candidates.value().end(),
      [&metadataPath](const authoring::RecoveryCandidate& candidate) {
        return candidate.metadataPath == metadataPath;
      });
  if (iterator == candidates.value().end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "The selected autosave no longer exists",
                         metadataPath.string());
  }
  return recover(*iterator);
}

void StandaloneApplicationController::notifyStateChanged() const {
  if (config_.stateChanged) config_.stateChanged();
}

core::Result<bool> StandaloneApplicationController::requestClose() {
  auto allowed = confirmDestructiveAction();
  if (!allowed) return allowed;
  if (!allowed.value()) return false;
  auto flushed = autosave_.flush();
  if (!flushed) return core::Result<bool>{flushed.error()};
  if (requestQuit_) requestQuit_();
  return true;
}

core::Result<void> StandaloneApplicationController::onDocumentChanged(
    std::chrono::steady_clock::time_point now) {
  return autosave_.onSuccessfulCommand(session_.runtime().document(), now);
}

core::Result<void> StandaloneApplicationController::tickAutosave(
    std::chrono::steady_clock::time_point now) {
  return autosave_.tick(session_.runtime().document(), now);
}

core::Result<std::vector<authoring::RecoveryCandidate>>
StandaloneApplicationController::recoveryCandidates() const {
  return autosave_.discover();
}

core::Result<void> StandaloneApplicationController::recover(
    const authoring::RecoveryCandidate& candidate) {
  auto recovered = session_.recoverProject(autosave_, candidate);
  if (recovered) notifyStateChanged();
  return recovered;
}

}  // namespace seam::standalone
