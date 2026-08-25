#pragma once

#include "seam/core/result.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace seam::platform {

enum class FileDialogPurpose {
  OpenProject,
  SaveProject,
  ImportAudio,
  InstallVoicebank,
  RelinkVoicebank,
  RelinkMedia,
  ExportSet,
  ExportAudio,
};

struct FileDialogRequest final {
  FileDialogPurpose purpose{FileDialogPurpose::OpenProject};
  std::string title;
  std::filesystem::path initialDirectory;
  std::string suggestedName;
  std::vector<std::string> extensions;
};

class IFileDialog {
public:
  virtual ~IFileDialog() = default;
  [[nodiscard]] virtual core::Result<std::optional<std::filesystem::path>> choose(
      const FileDialogRequest& request) = 0;
};

[[nodiscard]] std::unique_ptr<IFileDialog> createNativeFileDialog();

}  // namespace seam::platform
