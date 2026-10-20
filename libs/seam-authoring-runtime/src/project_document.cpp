#include "seam/authoring/project_document.hpp"

#include "seam/core/sha256.hpp"

#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <utility>
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace seam::authoring {
namespace {

// 32 hex digits that differ between documents, sessions and machines.
std::string freshAutosaveKey() {
  static std::atomic<std::uint64_t> counter{0U};
  std::random_device device;
  std::string seed = std::to_string(device()) + ':' + std::to_string(device()) + ':' +
                     std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) +
                     ':' + std::to_string(counter.fetch_add(1U, std::memory_order_relaxed));
#if !defined(_WIN32)
  seed += ':' + std::to_string(static_cast<long long>(::getpid()));
#endif
  return core::sha256Hex(seed).substr(0U, 32U);
}

}  // namespace

ProjectDocument::ProjectDocument(domain::Project project,
                                 application::ProjectFactory factory,
                                 core::ILogger* logger)
    : factory_(factory.nextIdValue()),
      session_(std::move(project), logger) {
  factory_.synchronizeWith(session_.project());
  identity_.lastSavedRevision = session_.revision();
  identity_.unsavedAutosaveKey = freshAutosaveKey();
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

core::Result<void> ProjectDocument::executePerformanceResult(
    const application::PerformanceJobContext& context,
    std::unique_ptr<application::ICommand> command) {
  const auto result = session_.executePerformanceResult(context, std::move(command));
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
  identity_.unsavedAutosaveKey = freshAutosaveKey();
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
  // Keep writing into the recovered autosave's directory, so its older generations are pruned
  // together with the new ones instead of lingering beside them.
  if (const auto directory = autosavePath.parent_path().filename().string(); !directory.empty())
    identity_.unsavedAutosaveKey = directory;
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
