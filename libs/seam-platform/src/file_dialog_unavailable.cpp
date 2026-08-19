#include "seam/platform/file_dialog.hpp"

#include <cstdlib>

#if !defined(_WIN32) && !defined(__APPLE__)
namespace seam::platform {
namespace {

class UnavailableFileDialog final : public IFileDialog {
public:
  core::Result<std::optional<std::filesystem::path>> choose(
      const FileDialogRequest&) override {
    if (const auto* injected = std::getenv("SEAM_FILE_DIALOG_PATH");
        injected != nullptr && *injected != '\0') {
      return std::optional<std::filesystem::path>{
          std::filesystem::path{injected}};
    }
    return core::failure<std::optional<std::filesystem::path>>(
        core::ErrorCode::Unsupported,
        "A native file dialog is unavailable on this platform",
        "Set SEAM_FILE_DIALOG_PATH only for deterministic alpha tests");
  }
};

}  // namespace

std::unique_ptr<IFileDialog> createNativeFileDialog() {
  return std::make_unique<UnavailableFileDialog>();
}

}  // namespace seam::platform
#endif
