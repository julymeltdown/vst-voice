#pragma once

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/core/logger.hpp"
#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace seam::authoring {

struct DocumentIdentity final {
  std::optional<std::filesystem::path> projectPath;
  std::optional<std::filesystem::path> autosavePath;
  std::uint64_t lastSavedRevision{0U};
  bool dirty{false};
};

class ProjectDocument final {
public:
  ProjectDocument(domain::Project project,
                  application::ProjectFactory factory,
                  core::ILogger* logger = nullptr);

  [[nodiscard]] application::EditorSession& session() noexcept {
    return session_;
  }
  [[nodiscard]] const application::EditorSession& session() const noexcept {
    return session_;
  }
  [[nodiscard]] application::ProjectFactory& factory() noexcept {
    return factory_;
  }
  [[nodiscard]] const application::ProjectFactory& factory() const noexcept {
    return factory_;
  }
  [[nodiscard]] const DocumentIdentity& identity() const noexcept {
    return identity_;
  }

  [[nodiscard]] core::Result<void> execute(
      std::unique_ptr<application::ICommand> command);
  [[nodiscard]] core::Result<void> undo();
  [[nodiscard]] core::Result<void> redo();
  [[nodiscard]] core::Result<void> replaceProject(domain::Project project);

  void markSaved(std::filesystem::path path) noexcept;
  void markRecovered(std::filesystem::path autosavePath) noexcept;
  void synchronizeDirtyState() noexcept { updateDirtyFromRevision(); }
  [[nodiscard]] bool dirty() const noexcept { return identity_.dirty; }

private:
  void updateDirtyFromRevision() noexcept;

  application::ProjectFactory factory_;
  application::EditorSession session_;
  DocumentIdentity identity_;
};

}  // namespace seam::authoring
