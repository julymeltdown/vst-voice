#include "seam/authoring/project_document.hpp"

#include <utility>

namespace seam::authoring {

ProjectDocument::ProjectDocument(domain::Project project,
                                 application::ProjectFactory factory,
                                 core::ILogger* logger)
    : factory_(factory.nextIdValue()),
      session_(std::move(project), logger) {
  factory_.synchronizeWith(session_.project());
  identity_.lastSavedRevision = session_.revision();
  identity_.dirty = false;
}

core::Result<void> ProjectDocument::execute(
    std::unique_ptr<application::ICommand> command) {
  const auto result = session_.execute(std::move(command));
  if (result) updateDirtyFromRevision();
  return result;
}

core::Result<void> ProjectDocument::undo() {
  const auto result = session_.undo();
  if (result) updateDirtyFromRevision();
  return result;
}

core::Result<void> ProjectDocument::redo() {
  const auto result = session_.redo();
  if (result) updateDirtyFromRevision();
  return result;
}

core::Result<void> ProjectDocument::replaceProject(domain::Project project) {
  const auto result = session_.replaceProject(std::move(project));
  if (!result) return result;

  factory_.synchronizeWith(session_.project());
  identity_.projectPath.reset();
  identity_.autosavePath.reset();
  identity_.recoveryOriginPath.reset();
  identity_.baseProjectHash.clear();
  identity_.dirty = true;
  return result;
}

void ProjectDocument::markSaved(std::filesystem::path path,
                                std::string durableProjectHash) noexcept {
  identity_.projectPath = std::move(path);
  identity_.autosavePath.reset();
  identity_.recoveryOriginPath.reset();
  identity_.lastSavedRevision = session_.revision();
  identity_.baseProjectHash = std::move(durableProjectHash);
  identity_.dirty = false;
}

void ProjectDocument::markRecovered(
    std::filesystem::path autosavePath) noexcept {
  markRecovered(std::move(autosavePath), identity_.projectPath,
                identity_.baseProjectHash);
}

void ProjectDocument::markRecovered(
    std::filesystem::path autosavePath,
    std::optional<std::filesystem::path> originalProjectPath,
    std::string durableProjectHash) noexcept {
  identity_.projectPath.reset();
  identity_.recoveryOriginPath = std::move(originalProjectPath);
  identity_.autosavePath = std::move(autosavePath);
  identity_.baseProjectHash = std::move(durableProjectHash);
  identity_.lastSavedRevision = session_.revision();
  identity_.dirty = true;
}

void ProjectDocument::updateDirtyFromRevision() noexcept {
  identity_.dirty = identity_.autosavePath.has_value() ||
                    session_.revision() != identity_.lastSavedRevision;
}

}  // namespace seam::authoring
