#include "seam/platform/application_menu.hpp"

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>

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

}  // namespace seam::platform
#endif
