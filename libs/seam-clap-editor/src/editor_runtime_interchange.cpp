#include "seam/clap_editor/editor_runtime.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/authoring/interchange_service.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <utility>

// The interchange command surface and its host handoffs live apart from the general
// editor adapter so neither file has to carry the whole adapter to stay coherent.
namespace seam::clap_editor {
namespace {

struct InterchangeDocumentStamp final {
  domain::ProjectId projectId{};
  std::uint64_t revision{0U};
  std::uint64_t nextId{0U};
  authoring::DocumentIdentity identity;
};

InterchangeDocumentStamp documentStamp(const authoring::ProjectDocument& document) {
  return {document.session().project().id(), document.session().revision(),
          document.factory().nextIdValue(), document.identity()};
}

bool matchesDocumentStamp(const authoring::ProjectDocument& document,
                          const InterchangeDocumentStamp& expected) {
  const auto current = documentStamp(document);
  const auto& before = expected.identity;
  const auto& after = current.identity;
  return current.projectId == expected.projectId &&
         current.revision == expected.revision &&
         current.nextId == expected.nextId &&
         after.projectPath == before.projectPath &&
         after.autosavePath == before.autosavePath &&
         after.recoveryOriginPath == before.recoveryOriginPath &&
         after.lastSavedRevision == before.lastSavedRevision &&
         after.baseProjectHash == before.baseProjectHash &&
         after.dirty == before.dirty;
}

core::Result<void> staleInterchangeApproval() {
  return core::failure(core::ErrorCode::Conflict,
      "The current editor document changed during score import; retry for the current document");
}

core::Result<void> staleInterchangeExport() {
  return core::failure(core::ErrorCode::Conflict,
      "The current editor document changed during score export; retry for the current document");
}

}  // namespace

core::Result<authoring::InterchangeImportDraft>
EditorRuntime::prepareInterchangeImport(const std::filesystem::path& source,
                                        authoring::InterchangeImportRequest request) const {
  using Output = authoring::InterchangeImportDraft;
  std::uint64_t nextId = 0U;
  {
    std::lock_guard lock(mutex_);
    if (authoring_ == nullptr) {
      return core::failure<Output>(core::ErrorCode::InvalidState,
                                   "Interchange import requires an initialized editor session");
    }
    nextId = authoring_->document().factory().nextIdValue();
  }
  // Parsing can take time and must not hold the editor lock. The request path
  // checks its original document stamp before adopting this independently
  // allocated draft; a rejected conversion never touches the live document.
  application::ProjectFactory draftFactory{nextId};
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

void EditorRuntime::setInterchangeExportReviewHandoff(
    std::function<core::Result<bool>(const authoring::InterchangeExportDraft&)> callback) {
  std::lock_guard lock(mutex_);
  interchangeExportReviewHandoff_ = std::move(callback);
}

void EditorRuntime::setInterchangeErrorHandoff(InterchangeErrorHandoff callback) {
  std::lock_guard lock(mutex_);
  interchangeErrorHandoff_ = std::move(callback);
}

core::Result<void> EditorRuntime::requestInterchangeImport() {
  InterchangePathHandoff chooser;
  std::function<core::Result<bool>(const authoring::InterchangeImportDraft&)> review;
  InterchangeDocumentStamp stamp;
  {
    std::lock_guard lock(mutex_);
    if (authoring_ == nullptr) {
      return core::failure(core::ErrorCode::InvalidState,
                           "Interchange import requires an initialized editor session");
    }
    chooser = interchangeImportHandoff_;
    review = interchangeReviewHandoff_;
    stamp = documentStamp(authoring_->document());
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
  {
    std::lock_guard lock(mutex_);
    if (authoring_ == nullptr || !matchesDocumentStamp(authoring_->document(), stamp)) {
      return staleInterchangeApproval();
    }
  }
  // The imported document is named after its source file, the same way the standalone surface names
  // one. A default request would carry an empty project name, which the converters correctly refuse.
  authoring::InterchangeImportRequest request;
  request.projectName = chosen.value()->stem().string();
  auto draft = prepareInterchangeImport(*chosen.value(), std::move(request));
  if (!draft) return core::Result<void>{draft.error()};
  {
    std::lock_guard lock(mutex_);
    if (authoring_ == nullptr || !matchesDocumentStamp(authoring_->document(), stamp)) {
      return staleInterchangeApproval();
    }
  }
  // Every import replaces the current song, even if conversion is lossless. A missing review
  // handoff must never count as approval of that replacement.
  if (!review) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Interchange import requires a review surface before the current "
                         "document can be replaced");
  }
  // The conversion is reviewed before the live document is touched. A decline is not an error: it
  // is the creator keeping the song they already had.
  const auto accepted = review(draft.value());
  if (!accepted) return core::Result<void>{accepted.error()};
  if (!accepted.value()) return core::success();
  // The check and replacement share one recursive editor lock. A host callback
  // cannot change the song between approval validation and adoption.
  std::lock_guard lock(mutex_);
  if (authoring_ == nullptr || !matchesDocumentStamp(authoring_->document(), stamp)) {
    return staleInterchangeApproval();
  }
  return acceptInterchangeImport(std::move(draft).value());
}

core::Result<void> EditorRuntime::requestInterchangeExport() {
  InterchangePathHandoff chooser;
  std::function<core::Result<bool>(const authoring::InterchangeExportDraft&)> review;
  InterchangeDocumentStamp stamp;
  {
    std::lock_guard lock(mutex_);
    if (authoring_ == nullptr) {
      return core::failure(core::ErrorCode::InvalidState,
                           "Interchange export requires an initialized editor session");
    }
    chooser = interchangeExportHandoff_;
    review = interchangeExportReviewHandoff_;
    stamp = documentStamp(authoring_->document());
  }
  if (!chooser) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Interchange export requires a host file chooser");
  }
  const auto chosen = chooser();
  if (!chosen) return core::Result<void>{chosen.error()};
  if (!chosen.value().has_value()) return core::success();
  const auto& destination = *chosen.value();
  auto extension = destination.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
  authoring::InterchangeExportRequest request;
  request.destination = destination;
  if (extension == ".ustx") request.format = authoring::InterchangeFormat::Ustx;
  else if (extension == ".mid" || extension == ".midi") request.format = authoring::InterchangeFormat::Smf;
  else
    return core::failure(core::ErrorCode::Unsupported,
                         "Score export requires a .ustx, .mid or .midi destination");
  // Export the complete score in both standalone and host surfaces. The chooser
  // may have run the host event loop, so revalidate the offered document before
  // preparing the whole-project conversion draft.
  authoring::InterchangeExportDraft draft;
  {
    std::lock_guard lock(mutex_);
    if (authoring_ == nullptr || !matchesDocumentStamp(authoring_->document(), stamp)) {
      return staleInterchangeExport();
    }
    auto prepared = authoring::InterchangeService{}.prepareExport(
        authoring_->document().session().project(), std::move(request));
    if (!prepared) return core::Result<void>{prepared.error()};
    draft = std::move(prepared).value();
  }
  if (!review) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Interchange export requires a review surface before writing");
  }
  const auto accepted = review(draft);
  if (!accepted) return core::Result<void>{accepted.error()};
  if (!accepted.value()) return core::success();
  std::lock_guard lock(mutex_);
  if (authoring_ == nullptr || !matchesDocumentStamp(authoring_->document(), stamp)) {
    return staleInterchangeExport();
  }
  const auto exported = authoring::InterchangeService{}.writeExport(draft);
  if (!exported) return core::Result<void>{exported.error()};
  requestRepaint();
  return core::success();
}

}  // namespace seam::clap_editor
