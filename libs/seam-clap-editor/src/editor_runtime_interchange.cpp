#include "seam/clap_editor/editor_runtime.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/authoring/interchange_service.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <utility>

// The interchange command surface and its host handoffs live apart from the general
// editor adapter so neither file has to carry the whole adapter to stay coherent.
namespace seam::clap_editor {

core::Result<authoring::InterchangeImportDraft>
EditorRuntime::prepareInterchangeImport(const std::filesystem::path& source,
                                        authoring::InterchangeImportRequest request) const {
  using Output = authoring::InterchangeImportDraft;
  std::lock_guard lock(mutex_);
  if (authoring_ == nullptr) {
    return core::failure<Output>(core::ErrorCode::InvalidState,
                                 "Interchange import requires an initialized editor session");
  }
  // The draft is built with a factory seeded past this document's identifiers, so accepting it
  // cannot allocate an id the live project already used. Import never touches the live document:
  // a rejected conversion has to leave the current song exactly as it was.
  application::ProjectFactory draftFactory{
      authoring_->document().factory().nextIdValue()};
  return authoring::InterchangeService{}.importFile(source, draftFactory, std::move(request));
}

core::Result<void> EditorRuntime::acceptInterchangeImport(
    authoring::InterchangeImportDraft draft) {
  // Repairing through replaceProject is deliberate: it is the same adoption path a host-provided
  // project takes, so an imported score is refreshed, rebound and repainted exactly like one, and
  // an imported document cannot silently inherit the previous session's bounce authority.
  auto replaced = replaceProject(std::move(draft.project));
  if (!replaced) return replaced;
  dirty_ = authoring_->document().dirty();
  controller_->setDirty(dirty_);
  authoring_->handleDocumentChanged();
  requestRepaint();
  return core::success();
}

core::Result<authoring::InterchangeExportReceipt> EditorRuntime::exportInterchange(
    authoring::InterchangeExportRequest request) const {
  std::lock_guard lock(mutex_);
  if (authoring_ == nullptr) {
    return core::failure<authoring::InterchangeExportReceipt>(
        core::ErrorCode::InvalidState,
        "Interchange export requires an initialized editor session");
  }
  return authoring::InterchangeService{}.exportFile(
      authoring_->document().session().project(), std::move(request));
}


void EditorRuntime::setInterchangeImportHandoff(InterchangePathHandoff callback) {
  std::lock_guard lock(mutex_);
  interchangeImportHandoff_ = std::move(callback);
}

void EditorRuntime::setInterchangeExportHandoff(InterchangePathHandoff callback) {
  std::lock_guard lock(mutex_);
  interchangeExportHandoff_ = std::move(callback);
}

void EditorRuntime::setInterchangeReviewHandoff(
    std::function<core::Result<bool>(const authoring::InterchangeImportDraft&)> callback) {
  std::lock_guard lock(mutex_);
  interchangeReviewHandoff_ = std::move(callback);
}

core::Result<void> EditorRuntime::requestInterchangeImport() {
  InterchangePathHandoff chooser;
  std::function<core::Result<bool>(const authoring::InterchangeImportDraft&)> review;
  {
    std::lock_guard lock(mutex_);
    chooser = interchangeImportHandoff_;
    review = interchangeReviewHandoff_;
  }
  // A surface that offers the action without a chooser would otherwise report success while doing
  // nothing, so an unconnected handoff is an explicit state error.
  if (!chooser) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Interchange import requires a host file chooser");
  }
  const auto chosen = chooser();
  if (!chosen) return core::Result<void>{chosen.error()};
  if (!chosen.value().has_value()) return core::success();
  // The imported document is named after its source file, the same way the standalone surface names
  // one. A default request would carry an empty project name, which the converters correctly refuse.
  authoring::InterchangeImportRequest request;
  request.projectName = chosen.value()->stem().string();
  auto draft = prepareInterchangeImport(*chosen.value(), std::move(request));
  if (!draft) return core::Result<void>{draft.error()};
  // A conversion that lost information must be reviewed by a human before it replaces the song. A
  // surface with no review connected therefore refuses a lossy draft rather than adopting it
  // silently: absence of a review UI is never approval of a lossy import.
  const auto lost = std::any_of(draft.value().issues.begin(), draft.value().issues.end(),
                                [](const authoring::InterchangeIssue& issue) { return issue.loss; });
  if (!review && lost) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Interchange import reported conversion losses; a review surface is required "
                         "before a lossy score can replace the current document");
  }
  // The conversion is reviewed before the live document is touched. A decline is not an error: it
  // is the creator keeping the song they already had.
  if (review) {
    const auto accepted = review(draft.value());
    if (!accepted) return core::Result<void>{accepted.error()};
    if (!accepted.value()) return core::success();
  }
  return acceptInterchangeImport(std::move(draft).value());
}

core::Result<void> EditorRuntime::requestInterchangeExport() {
  InterchangePathHandoff chooser;
  {
    std::lock_guard lock(mutex_);
    chooser = interchangeExportHandoff_;
  }
  if (!chooser) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Interchange export requires a host file chooser");
  }
  const auto chosen = chooser();
  if (!chosen) return core::Result<void>{chosen.error()};
  if (!chosen.value().has_value()) return core::success();
  const auto& destination = *chosen.value();
  const auto extension = destination.extension().string();
  authoring::InterchangeExportRequest request;
  request.destination = destination;
  if (extension == ".ustx") request.format = authoring::InterchangeFormat::Ustx;
  else if (extension == ".mid" || extension == ".midi") request.format = authoring::InterchangeFormat::Smf;
  else
    return core::failure(core::ErrorCode::Unsupported,
                         "Score export requires a .ustx, .mid or .midi destination");
  // Exporting the whole selected track is the useful default in a DAW session, and it is the same
  // scope the standalone surface uses.
  if (trackId_.valid()) request.trackId = trackId_;
  if (regionId_.valid()) request.regionId = regionId_;
  const auto exported = exportInterchange(std::move(request));
  if (!exported) return core::Result<void>{exported.error()};
  requestRepaint();
  return core::success();
}

}  // namespace seam::clap_editor
