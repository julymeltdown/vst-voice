#include "test_framework.hpp"

#include "seam/platform/application_menu.hpp"
#include "seam/platform/application_paths.hpp"
#include "seam/platform/file_dialog.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakeDialog final : public seam::platform::IFileDialog {
public:
  seam::core::Result<std::optional<std::filesystem::path>> choose(
      const seam::platform::FileDialogRequest& request) override {
    requests.push_back(request);
    return response;
  }

  std::vector<seam::platform::FileDialogRequest> requests;
  std::optional<std::filesystem::path> response;
};

class FakeDispatcher final : public seam::platform::IApplicationCommandDispatcher {
public:
  seam::core::Result<void> dispatch(
      seam::platform::ApplicationCommand command) override {
    commands.push_back(command);
    return seam::core::success();
  }

  std::vector<seam::platform::ApplicationCommand> commands;
};

class FakeMenu final : public seam::platform::IApplicationMenu {
public:
  seam::core::Result<void> install(
      seam::platform::IApplicationCommandDispatcher& dispatcher) override {
    dispatcher_ = &dispatcher;
    return seam::core::success();
  }
  void refresh() noexcept override {}
  void uninstall() noexcept override { dispatcher_ = nullptr; }
  void trigger(seam::platform::ApplicationCommand command) {
    if (dispatcher_ != nullptr) CHECK(dispatcher_->dispatch(command));
  }

private:
  seam::platform::IApplicationCommandDispatcher* dispatcher_{nullptr};
};

}  // namespace

TEST_CASE("file_dialog_contract_preserves_purpose_filters_and_suggested_name") {
  FakeDialog dialog;
  dialog.response = std::filesystem::path{"/tmp/曲 프로젝트.seam"};
  const seam::platform::FileDialogRequest request{
      .purpose = seam::platform::FileDialogPurpose::SaveProject,
      .title = "Save Project",
      .initialDirectory = "/tmp",
      .suggestedName = "曲 프로젝트.seam",
      .extensions = {"seam"},
  };

  const auto selected = dialog.choose(request);
  CHECK(selected);
  CHECK(selected.value().has_value());
  CHECK(*selected.value() == *dialog.response);
  CHECK(dialog.requests.size() == 1U);
  CHECK(dialog.requests.front().purpose ==
        seam::platform::FileDialogPurpose::SaveProject);
  CHECK(dialog.requests.front().extensions == std::vector<std::string>{"seam"});
}

TEST_CASE("application_menu_routes_commands_through_dispatcher") {
  FakeDispatcher dispatcher;
  FakeMenu menu;
  CHECK(menu.install(dispatcher));
  menu.trigger(seam::platform::ApplicationCommand::NewProject);
  menu.trigger(seam::platform::ApplicationCommand::OpenProject);
  menu.trigger(seam::platform::ApplicationCommand::RecoverLatestAutosave);
  menu.trigger(seam::platform::ApplicationCommand::SaveProject);
  menu.trigger(seam::platform::ApplicationCommand::SaveProjectAs);
  menu.trigger(seam::platform::ApplicationCommand::ExportAudio);
  menu.trigger(seam::platform::ApplicationCommand::Quit);
  menu.trigger(seam::platform::ApplicationCommand::Undo);
  menu.trigger(seam::platform::ApplicationCommand::Redo);
  menu.trigger(seam::platform::ApplicationCommand::TogglePlayback);
  CHECK(dispatcher.commands.size() == 10U);
  CHECK(dispatcher.commands.front() ==
        seam::platform::ApplicationCommand::NewProject);
  CHECK(dispatcher.commands.back() ==
        seam::platform::ApplicationCommand::TogglePlayback);
  menu.uninstall();
}

TEST_CASE("unsaved_prompt_contract_returns_explicit_decision") {
  class Prompt final : public seam::platform::IUnsavedChangesPrompt {
  public:
    seam::core::Result<seam::platform::UnsavedDecision> choose(
        std::string_view projectName) override {
      observed = std::string{projectName};
      return seam::platform::UnsavedDecision::Cancel;
    }
    std::string observed;
  } prompt;

  const auto decision = prompt.choose("未保存 프로젝트");
  CHECK(decision);
  CHECK(decision.value() == seam::platform::UnsavedDecision::Cancel);
  CHECK(prompt.observed == "未保存 프로젝트");
}

#ifndef __APPLE__
#ifndef _WIN32
TEST_CASE("headless_native_file_dialog_returns_structured_unsupported_error") {
  auto dialog = seam::platform::createNativeFileDialog();
  const auto selected = dialog->choose(seam::platform::FileDialogRequest{
      .purpose = seam::platform::FileDialogPurpose::OpenProject,
      .title = "Open",
      .initialDirectory = {},
      .suggestedName = {},
      .extensions = {"seam"},
  });
  CHECK(!selected);
  CHECK(selected.error().code == seam::core::ErrorCode::Unsupported);
}
#endif
#endif

TEST_CASE("application_support_directory_is_absolute_and_product_scoped") {
  const auto path = seam::platform::applicationSupportDirectory();
  CHECK(path);
  CHECK(path.value().is_absolute());
  CHECK(path.value().filename() == "project-seam" ||
        path.value().filename() == "ProjectSEAM");
}
