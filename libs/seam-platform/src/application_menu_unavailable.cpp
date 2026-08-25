#include "seam/platform/application_menu.hpp"

#if !defined(_WIN32) && !defined(__APPLE__)
namespace seam::platform {
namespace {

class NoopApplicationMenu final : public IApplicationMenu {
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

class ConservativeUnsavedPrompt final : public IUnsavedChangesPrompt {
public:
  core::Result<UnsavedDecision> choose(std::string_view) override {
    return UnsavedDecision::Cancel;
  }
};

}  // namespace

std::unique_ptr<IApplicationMenu> createNativeApplicationMenu() {
  return std::make_unique<NoopApplicationMenu>();
}

std::unique_ptr<IUnsavedChangesPrompt> createNativeUnsavedChangesPrompt() {
  return std::make_unique<ConservativeUnsavedPrompt>();
}

core::Result<void> openDocumentationPath(const std::filesystem::path&) {
  return core::failure(core::ErrorCode::Unsupported,
                       "Offline documentation opening is unavailable");
}

core::Result<void> openExternalPath(const std::filesystem::path&) {
  return core::failure(core::ErrorCode::Unsupported,
                       "External path opening is unavailable");
}

core::Result<void> copyTextToClipboard(std::string_view) {
  return core::failure(core::ErrorCode::Unsupported,
                       "Clipboard access is unavailable");
}

core::Result<bool> requestEulaAcceptance(const std::filesystem::path&) {
  return core::failure<bool>(core::ErrorCode::Unsupported,
                             "EULA acceptance prompt is unavailable");
}

}  // namespace seam::platform
#endif
