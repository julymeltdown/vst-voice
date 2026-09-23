#include "seam/standalone/native_project_dialog.hpp"

namespace seam::standalone {
namespace {

class UnavailableNativeNewProjectDialog final
    : public INativeNewProjectDialog {
public:
  core::Result<std::optional<authoring::NewProjectRequest>> choose(
      NativeNewProjectDialogConfig) override {
    return core::failure<std::optional<authoring::NewProjectRequest>>(
        core::ErrorCode::Unsupported,
        "Native New Project form is unavailable on this platform");
  }
};

class UnavailableNativeInterchangeReviewDialog final
    : public INativeInterchangeReviewDialog {
public:
  core::Result<bool> review(const authoring::InterchangeImportDraft&) override {
    return core::failure<bool>(core::ErrorCode::Unsupported,
        "Native interchange review is unavailable on this platform");
  }
  core::Result<bool> reviewExport(const authoring::InterchangeExportDraft&) override {
    return core::failure<bool>(core::ErrorCode::Unsupported,
        "Native interchange export review is unavailable on this platform");
  }
};

}

std::unique_ptr<INativeNewProjectDialog> createNativeNewProjectDialog() {
  return std::make_unique<UnavailableNativeNewProjectDialog>();
}

std::unique_ptr<INativeInterchangeReviewDialog>
createNativeInterchangeReviewDialog() {
  return std::make_unique<UnavailableNativeInterchangeReviewDialog>();
}

}
