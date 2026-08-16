#include "seam/application/editor_session.hpp"

namespace seam::application {

EditorSession::EditorSession(domain::Project project, core::ILogger* logger)
    : project_(std::move(project)), logger_(logger != nullptr ? logger : &fallbackLogger_) {}

void EditorSession::log(core::LogLevel level, std::string_view message) {
  logger_->write(level, "editor-session", message);
}

core::Result<void> EditorSession::execute(std::unique_ptr<ICommand> command) {
  if (!command) {
    return core::failure(core::ErrorCode::InvalidArgument, "Command must not be null");
  }
  const auto result = command->apply(project_);
  if (!result) {
    log(core::LogLevel::Error, result.error().message);
    return result;
  }
  const auto validation = project_.validate();
  if (!validation) {
    static_cast<void>(command->revert(project_));
    log(core::LogLevel::Error, validation.error().message);
    return validation;
  }

  undo_.push_back(std::move(command));
  redo_.clear();
  incrementRevision();
  return core::success();
}

core::Result<void> EditorSession::undo() {
  if (undo_.empty()) {
    return core::failure(core::ErrorCode::Conflict, "There is no command to undo");
  }
  auto command = std::move(undo_.back());
  undo_.pop_back();
  const auto result = command->revert(project_);
  if (!result) {
    undo_.push_back(std::move(command));
    return result;
  }
  const auto validation = project_.validate();
  if (!validation) {
    static_cast<void>(command->apply(project_));
    undo_.push_back(std::move(command));
    log(core::LogLevel::Error, validation.error().message);
    return validation;
  }
  redo_.push_back(std::move(command));
  incrementRevision();
  return core::success();
}

core::Result<void> EditorSession::redo() {
  if (redo_.empty()) {
    return core::failure(core::ErrorCode::Conflict, "There is no command to redo");
  }
  auto command = std::move(redo_.back());
  redo_.pop_back();
  const auto result = command->apply(project_);
  if (!result) {
    redo_.push_back(std::move(command));
    return result;
  }
  const auto validation = project_.validate();
  if (!validation) {
    static_cast<void>(command->revert(project_));
    redo_.push_back(std::move(command));
    log(core::LogLevel::Error, validation.error().message);
    return validation;
  }
  undo_.push_back(std::move(command));
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
}

}  // namespace seam::application
