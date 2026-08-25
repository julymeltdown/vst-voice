#include "seam/platform/application_menu.hpp"

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

namespace seam::platform {
namespace {

class Win32ApplicationMenu final : public IApplicationMenu {
public:
  core::Result<void> install(IApplicationCommandDispatcher& dispatcher) override {
    dispatcher_ = &dispatcher;
    return core::success();
  }
  void refresh() noexcept override {}
  void uninstall() noexcept override { dispatcher_ = nullptr; }

private:
  IApplicationCommandDispatcher* dispatcher_{nullptr};
};

class Win32UnsavedPrompt final : public IUnsavedChangesPrompt {
public:
  core::Result<UnsavedDecision> choose(std::string_view projectName) override {
    const std::string message = "Save changes to \"" + std::string{projectName} +
                                "\" before closing?";
    const auto response = MessageBoxA(nullptr, message.c_str(), "Project SEAM",
                                      MB_YESNOCANCEL | MB_ICONWARNING |
                                          MB_SETFOREGROUND);
    if (response == IDYES) return UnsavedDecision::Save;
    if (response == IDNO) return UnsavedDecision::Discard;
    return UnsavedDecision::Cancel;
  }
};

}  // namespace

std::unique_ptr<IApplicationMenu> createNativeApplicationMenu() {
  return std::make_unique<Win32ApplicationMenu>();
}

std::unique_ptr<IUnsavedChangesPrompt> createNativeUnsavedChangesPrompt() {
  return std::make_unique<Win32UnsavedPrompt>();
}

core::Result<void> openDocumentationPath(const std::filesystem::path& path) {
  if (path.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Documentation path is empty");
  }
  const auto result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr,
                                    nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<std::intptr_t>(result) <= 32) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to open offline documentation", path.string());
  }
  return core::success();
}

core::Result<void> openExternalPath(const std::filesystem::path& path) {
  if (path.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "External path is empty");
  }
  const auto result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr,
                                    nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<std::intptr_t>(result) <= 32) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to open external path", path.string());
  }
  return core::success();
}

core::Result<void> copyTextToClipboard(std::string_view text) {
  constexpr std::size_t kMaxClipboardBytes = 1U << 20U;
  if (text.size() > kMaxClipboardBytes ||
      text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Diagnostic text is too large for the clipboard");
  }
  const auto sourceLength = static_cast<int>(text.size());
  const auto wideLength = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), sourceLength, nullptr, 0);
  if (wideLength <= 0) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Diagnostic text is not valid UTF-8");
  }
  std::wstring converted(static_cast<std::size_t>(wideLength), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                          sourceLength, converted.data(), wideLength) <= 0) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Diagnostic text is not valid UTF-8");
  }
  if (!OpenClipboard(nullptr)) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to open the system clipboard");
  }
  if (!EmptyClipboard()) {
    CloseClipboard();
    return core::failure(core::ErrorCode::IoError,
                         "Unable to clear the system clipboard");
  }
  const auto bytes = (converted.size() + 1U) * sizeof(wchar_t);
  HGLOBAL storage = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (storage == nullptr) {
    CloseClipboard();
    return core::failure(core::ErrorCode::IoError,
                         "Unable to allocate clipboard storage");
  }
  auto* destination = static_cast<wchar_t*>(GlobalLock(storage));
  if (destination == nullptr) {
    GlobalFree(storage);
    CloseClipboard();
    return core::failure(core::ErrorCode::IoError,
                         "Unable to lock clipboard storage");
  }
  std::copy(converted.begin(), converted.end(), destination);
  destination[converted.size()] = L'\0';
  GlobalUnlock(storage);
  if (SetClipboardData(CF_UNICODETEXT, storage) == nullptr) {
    GlobalFree(storage);
    CloseClipboard();
    return core::failure(core::ErrorCode::IoError,
                         "Unable to publish clipboard text");
  }
  CloseClipboard();
  return core::success();
}

core::Result<bool> requestEulaAcceptance(const std::filesystem::path& path) {
  if (path.empty()) {
    return core::failure<bool>(core::ErrorCode::InvalidArgument,
                               "EULA path is empty");
  }
  const auto opened = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr,
                                    nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<std::intptr_t>(opened) <= 32) {
    return core::failure<bool>(core::ErrorCode::IoError,
                               "Unable to open the bundled EULA",
                               path.string());
  }
  const auto response = MessageBoxW(
      nullptr,
      L"Review the bundled Project SEAM External Beta EULA, then choose Yes to accept it.\n\nAcceptance is stored locally as the document version, SHA-256 digest, and UTC timestamp.",
      L"Project SEAM External Beta EULA", MB_YESNO | MB_ICONWARNING |
                                              MB_SETFOREGROUND);
  return response == IDYES;
}

}  // namespace seam::platform
#endif
