#pragma once

#include "seam/application/command.hpp"
#include "seam/application/selection.hpp"
#include "seam/core/logger.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace seam::application {

class EditorSession final {
public:
  explicit EditorSession(domain::Project project, core::ILogger* logger = nullptr);

  [[nodiscard]] domain::Project& project() noexcept { return project_; }
  [[nodiscard]] const domain::Project& project() const noexcept { return project_; }
  [[nodiscard]] SelectionModel& selection() noexcept { return selection_; }
  [[nodiscard]] const SelectionModel& selection() const noexcept { return selection_; }
  [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

  [[nodiscard]] core::Result<void> execute(std::unique_ptr<ICommand> command);
  [[nodiscard]] core::Result<void> undo();
  [[nodiscard]] core::Result<void> redo();

  [[nodiscard]] bool canUndo() const noexcept { return !undo_.empty(); }
  [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }
  [[nodiscard]] std::string_view undoName() const noexcept;
  [[nodiscard]] std::string_view redoName() const noexcept;

  void clearHistory() noexcept;

private:
  void incrementRevision() noexcept { ++revision_; }
  void log(core::LogLevel level, std::string_view message);

  domain::Project project_;
  SelectionModel selection_;
  std::vector<std::unique_ptr<ICommand>> undo_;
  std::vector<std::unique_ptr<ICommand>> redo_;
  std::uint64_t revision_{0};
  core::NullLogger fallbackLogger_;
  core::ILogger* logger_;
};

}  // namespace seam::application
