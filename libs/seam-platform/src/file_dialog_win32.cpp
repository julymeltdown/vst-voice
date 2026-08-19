#include "seam/platform/file_dialog.hpp"

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <string>
#include <vector>

namespace seam::platform {
namespace {

using Microsoft::WRL::ComPtr;

std::wstring wide(std::string_view text) {
  if (text.empty()) return {};
  const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           text.data(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0);
  if (length <= 0) return {};
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                      static_cast<int>(text.size()), result.data(), length);
  return result;
}

std::vector<COMDLG_FILTERSPEC> filters(
    const std::vector<std::string>& extensions,
    std::vector<std::wstring>& storage) {
  std::vector<COMDLG_FILTERSPEC> result;
  storage.clear();
  storage.reserve(extensions.size() * 2U);
  for (const auto& extension : extensions) {
    storage.push_back(L"*." + wide(extension));
    storage.push_back(wide(extension) + L" files");
    result.push_back(COMDLG_FILTERSPEC{storage[storage.size() - 1U].c_str(),
                                      storage[storage.size() - 2U].c_str()});
  }
  return result;
}

core::Result<std::optional<std::filesystem::path>> resultPath(
    IFileDialog* dialog) {
  ComPtr<IShellItem> item;
  if (FAILED(dialog->GetResult(&item))) {
    return core::failure<std::optional<std::filesystem::path>>(
        core::ErrorCode::IoError, "Unable to read the selected Windows path");
  }
  PWSTR value = nullptr;
  const auto status = item->GetDisplayName(SIGDN_FILESYSPATH, &value);
  if (FAILED(status) || value == nullptr) {
    if (value != nullptr) CoTaskMemFree(value);
    return core::failure<std::optional<std::filesystem::path>>(
        core::ErrorCode::IoError, "Unable to resolve the selected Windows path");
  }
  std::filesystem::path path{value};
  CoTaskMemFree(value);
  return std::optional<std::filesystem::path>{std::move(path)};
}

class Win32FileDialog final : public IFileDialog {
public:
  core::Result<std::optional<std::filesystem::path>> choose(
      const FileDialogRequest& request) override {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                                        COINIT_DISABLE_OLE1DDE);
    const bool uninitialize = SUCCEEDED(initialized);
    const bool save = request.purpose == FileDialogPurpose::SaveProject ||
                      request.purpose == FileDialogPurpose::ExportAudio;
    ComPtr<IFileDialog> dialog;
    const auto created = save
        ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&dialog))
        : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&dialog));
    if (FAILED(created)) {
      if (uninitialize) CoUninitialize();
      return core::failure<std::optional<std::filesystem::path>>(
          core::ErrorCode::Unsupported,
          "Unable to create the native Windows file dialog");
    }
    const auto title = wide(request.title);
    if (!title.empty()) static_cast<void>(dialog->SetTitle(title.c_str()));
    const auto suggested = wide(request.suggestedName);
    if (!suggested.empty()) static_cast<void>(dialog->SetFileName(suggested.c_str()));
    if (!request.initialDirectory.empty()) {
      ComPtr<IShellItem> folder;
      if (SUCCEEDED(SHCreateItemFromParsingName(
              request.initialDirectory.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
        static_cast<void>(dialog->SetFolder(folder.Get()));
      }
    }
    std::vector<std::wstring> filterStorage;
    const auto specifications = filters(request.extensions, filterStorage);
    if (!specifications.empty()) {
      static_cast<void>(dialog->SetFileTypes(
          static_cast<UINT>(specifications.size()), specifications.data()));
    }
    const auto shown = dialog->Show(nullptr);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
      if (uninitialize) CoUninitialize();
      return std::optional<std::filesystem::path>{};
    }
    if (FAILED(shown)) {
      if (uninitialize) CoUninitialize();
      return core::failure<std::optional<std::filesystem::path>>(
          core::ErrorCode::IoError, "The native Windows file dialog failed");
    }
    auto selected = resultPath(dialog.Get());
    if (uninitialize) CoUninitialize();
    return selected;
  }
};

}  // namespace

std::unique_ptr<IFileDialog> createNativeFileDialog() {
  return std::make_unique<Win32FileDialog>();
}

}  // namespace seam::platform
#endif
