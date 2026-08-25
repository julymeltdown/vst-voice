#include "seam/application/editor_session.hpp"

namespace seam::application {

EditorSession::EditorSession(domain::Project project, core::ILogger* logger)
    : project_(std::move(project)), logger_(logger != nullptr ? logger : &fallbackLogger_) {}

void EditorSession::log(core::LogLevel level, std::string_view message) {
  logger_->write(level, "editor-session", message);
}

core::Result<void> EditorSession::execute(std::unique_ptr<ICommand> command) {
  if (health_ == SessionHealth::RecoveryRequired) {
    return core::failure(core::ErrorCode::Conflict,
                         "Editor session requires explicit history recovery");
  }
  if (!command) {
    return core::failure(core::ErrorCode::InvalidArgument, "Command must not be null");
  }

  const auto impact = command->impact();

  const auto transactionSnapshot = project_;
  const auto result = command->apply(project_);
  if (!result) {
    project_ = transactionSnapshot;
    log(core::LogLevel::Error, result.error().message);
    return result;
  }
  const auto validation = project_.validate();
  if (!validation) {
    project_ = transactionSnapshot;
    log(core::LogLevel::Error, validation.error().message);
    return validation;
  }

  undo_.push_back(std::move(command));
  redo_.clear();
  lastImpact_ = impact;
  incrementRevision();
  return core::success();
}

core::Result<void> EditorSession::undo() {
  if (undo_.empty()) {
    return core::failure(core::ErrorCode::Conflict, "There is no command to undo");
  }

  const auto transactionSnapshot = project_;
  const auto impact = undo_.back()->impact();
  const auto result = undo_.back()->revert(project_);
  if (!result) {
    project_ = transactionSnapshot;
    undo_.clear();
    redo_.clear();
    health_ = SessionHealth::RecoveryRequired;
    log(core::LogLevel::Error,
        "Undo failed; project was restored and command history was invalidated");
    return result;
  }
  const auto validation = project_.validate();
  if (!validation) {
    project_ = transactionSnapshot;
    undo_.clear();
    redo_.clear();
    health_ = SessionHealth::RecoveryRequired;
    log(core::LogLevel::Error,
        "Undo validation failed; project was restored and command history was invalidated");
    return validation;
  }

  auto command = std::move(undo_.back());
  undo_.pop_back();
  redo_.push_back(std::move(command));
  lastImpact_ = impact;
  incrementRevision();
  return core::success();
}

core::Result<void> EditorSession::redo() {
  if (redo_.empty()) {
    return core::failure(core::ErrorCode::Conflict, "There is no command to redo");
  }

  const auto transactionSnapshot = project_;
  const auto impact = redo_.back()->impact();
  const auto result = redo_.back()->apply(project_);
  if (!result) {
    project_ = transactionSnapshot;
    undo_.clear();
    redo_.clear();
    health_ = SessionHealth::RecoveryRequired;
    log(core::LogLevel::Error,
        "Redo failed; project was restored and command history was invalidated");
    return result;
  }
  const auto validation = project_.validate();
  if (!validation) {
    project_ = transactionSnapshot;
    undo_.clear();
    redo_.clear();
    health_ = SessionHealth::RecoveryRequired;
    log(core::LogLevel::Error,
        "Redo validation failed; project was restored and command history was invalidated");
    return validation;
  }

  auto command = std::move(redo_.back());
  redo_.pop_back();
  undo_.push_back(std::move(command));
  lastImpact_ = impact;
  incrementRevision();
  return core::success();
}

core::Result<void> EditorSession::replaceProject(domain::Project project) {
  const auto validation = project.validate();
  if (!validation) {
    log(core::LogLevel::Error, validation.error().message);
    return validation;
  }

  project_ = std::move(project);
  selection_.clear();
  undo_.clear();
  redo_.clear();
  health_ = SessionHealth::Ready;
  lastImpact_ = CommandImpact{
      .scope = CommandAudioImpact::ProjectAudio,
      .projectWide = true,
      .trackIds = {},
      .regionIds = {},
      .noteIds = {},
      .lyricIds = {},
  };
  incrementRevision();
  return core::success();
}

std::string_view EditorSession::undoName() const noexcept {
  return undo_.empty() ? std::string_view{} : undo_.back()->name();
}

std::string_view EditorSession::redoName() const noexcept {
  return redo_.empty() ? std::string_view{} : redo_.back()->name();
}

void EditorSession::clearHistory() noexcept {
  undo_.clear();
  redo_.clear();
  health_ = SessionHealth::Ready;
}

}  // namespace seam::application
