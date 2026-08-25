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

}

std::unique_ptr<INativeNewProjectDialog> createNativeNewProjectDialog() {
  return std::make_unique<UnavailableNativeNewProjectDialog>();
}

}
