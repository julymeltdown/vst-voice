#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/note_commands.hpp"
#include "seam/platform/application_menu.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/standalone/application_controller.hpp"
#include "seam/standalone/authoring_session.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required
#endif

namespace {

class FakeDialog final : public seam::platform::IFileDialog {
public:
  seam::core::Result<std::optional<std::filesystem::path>> choose(
      const seam::platform::FileDialogRequest& request) override {
    requests.push_back(request);
    if (responses.empty()) return std::optional<std::filesystem::path>{};
    auto response = responses.front();
    responses.erase(responses.begin());
    return response;
  }

  std::vector<seam::platform::FileDialogRequest> requests;
  std::vector<std::optional<std::filesystem::path>> responses;
};

class FakePrompt final : public seam::platform::IUnsavedChangesPrompt {
public:
  seam::core::Result<seam::platform::UnsavedDecision> choose(
      std::string_view name) override {
    names.emplace_back(name);
    if (decisions.empty()) return seam::platform::UnsavedDecision::Cancel;
    const auto result = decisions.front();
    decisions.erase(decisions.begin());
    return result;
  }

  std::vector<std::string> names;
  std::vector<seam::platform::UnsavedDecision> decisions;
};

std::unique_ptr<seam::standalone::AuthoringSession> makeSession(
    const std::filesystem::path& root) {
  auto created = seam::standalone::AuthoringSession::create(
      seam::standalone::AuthoringSessionConfig{
          .cacheRoot = root / "cache",
          .voicebankRoots = {seam::voicebank::VoicebankSearchRoot{
              .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
              .kind = seam::voicebank::VoicebankRootKind::Development,
          }},
          .sampleRate = 48000U,
          .outputChannels = 2U,
          .bindFirstAvailableVoicebank = true,
          .allowDevelopmentVoicebanks = true,
      });
  CHECK(created);
  return std::move(created).value();
}

void addNote(seam::standalone::AuthoringSession& session) {
  auto [lyric, note] = session.runtime().document().factory().makeNote(
      seam::time::Tick{0}, seam::time::Tick{960}, 64U, U"こ",
      seam::domain::Language::Japanese);
  CHECK(session.runtime().execute(
      std::make_unique<seam::application::AddNoteCommand>(
          session.regionId(), std::move(lyric), std::move(note))));
}

}  // namespace

TEST_CASE("standalone_application_controller_executes_new_open_save_and_save_as_without_cli") {
  const auto root = seam::test::support::temporaryDirectory("standalone-lifecycle");
  auto session = makeSession(root);
  auto dialog = std::make_unique<FakeDialog>();
  auto* dialogPtr = dialog.get();
  auto prompt = std::make_unique<FakePrompt>();
  const auto firstPath = root / "最初 프로젝트.seam";
  const auto secondPath = root / "別名 프로젝트.seam";
  dialogPtr->responses = {firstPath, secondPath, firstPath};

  bool quit = false;
  auto controller = seam::standalone::StandaloneApplicationController::create(
      *session, std::move(dialog), std::move(prompt),
      seam::standalone::StandaloneApplicationControllerConfig{
          .autosaveRoot = root / "autosaves",
          .recentProjectsPath = root / "recent-projects.json",
          .defaultNewProject = seam::authoring::NewProjectRequest{
              .name = "Untitled",
              .tempoBpm = 120.0,
              .sampleRate = 48000U,
              .outputChannels = 2U,
              .initialVoicebank = std::nullopt,
          },
          .stateChanged = {},
      },
      [&quit] { quit = true; });
  CHECK(controller);

  addNote(*session);
  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::SaveProjectAs));
  CHECK(std::filesystem::exists(firstPath));
  CHECK(session->runtime().document().identity().projectPath == firstPath);
  CHECK(!session->runtime().document().dirty());

  addNote(*session);
  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::SaveProject));
  CHECK(!session->runtime().document().dirty());

  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::SaveProjectAs));
  CHECK(std::filesystem::exists(secondPath));
  CHECK(session->runtime().document().identity().projectPath == secondPath);

  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::OpenProject));
  CHECK(session->runtime().document().identity().projectPath == firstPath);
  CHECK(!session->runtime().document().dirty());
  CHECK(controller.value()->recentProjectStore().entries().size() == 2U);
  CHECK(!quit);
}

TEST_CASE("standalone_application_controller_respects_unsaved_cancel_and_discard") {
  const auto root = seam::test::support::temporaryDirectory("standalone-unsaved");
  auto session = makeSession(root);
  auto dialog = std::make_unique<FakeDialog>();
  auto prompt = std::make_unique<FakePrompt>();
  auto* promptPtr = prompt.get();
  promptPtr->decisions = {seam::platform::UnsavedDecision::Cancel,
                          seam::platform::UnsavedDecision::Discard};
  bool quit = false;
  auto controller = seam::standalone::StandaloneApplicationController::create(
      *session, std::move(dialog), std::move(prompt),
      seam::standalone::StandaloneApplicationControllerConfig{
          .autosaveRoot = root / "autosaves",
          .recentProjectsPath = root / "recent.json",
          .defaultNewProject = {
              .name = "New Song",
              .tempoBpm = 120.0,
              .sampleRate = 48000U,
              .outputChannels = 2U,
              .initialVoicebank = std::nullopt,
          },
          .stateChanged = {},
      },
      [&quit] { quit = true; });
  CHECK(controller);
  addNote(*session);
  const auto projectId = session->runtime().document().session().project().id();

  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::NewProject));
  CHECK(session->runtime().document().session().project().id() == projectId);
  CHECK(session->runtime().document().dirty());

  const auto close = controller.value()->requestClose();
  CHECK(close);
  CHECK(close.value());
  CHECK(quit);
}

TEST_CASE("standalone_application_controller_discovers_and_recovers_autosave_copy") {
  const auto root = seam::test::support::temporaryDirectory("standalone-recovery");
  auto session = makeSession(root);
  auto dialog = std::make_unique<FakeDialog>();
  auto prompt = std::make_unique<FakePrompt>();
  auto controller = seam::standalone::StandaloneApplicationController::create(
      *session, std::move(dialog), std::move(prompt),
      seam::standalone::StandaloneApplicationControllerConfig{
          .autosaveRoot = root / "autosaves",
          .recentProjectsPath = root / "recent.json",
          .defaultNewProject = {
              .name = "Recovered",
              .tempoBpm = 120.0,
              .sampleRate = 48000U,
              .outputChannels = 2U,
              .initialVoicebank = std::nullopt,
          },
          .stateChanged = {},
      });
  CHECK(controller);
  addNote(*session);
  CHECK(controller.value()->autosave().request(session->runtime().document()));
  CHECK(controller.value()->autosave().flush());
  const auto discovered = controller.value()->recoveryCandidates();
  CHECK(discovered);
  CHECK(discovered.value().size() == 1U);
  CHECK(discovered.value().front().recoverable);

  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::RecoverLatestAutosave));
  CHECK(session->runtime().document().dirty());
  CHECK(session->runtime().document().identity().autosavePath.has_value());
  CHECK(session->controller().sceneState().dirty);
}

TEST_CASE("standalone_application_controller_blocks_close_when_pending_autosave_failed") {
  const auto root = seam::test::support::temporaryDirectory("standalone-autosave-close-failure");
  const auto blockedAutosaveRoot = root / "autosaves-file";
  {
    std::ofstream output{blockedAutosaveRoot};
    CHECK(output.good());
    output << "not-a-directory";
  }
  auto session = makeSession(root);
  auto dialog = std::make_unique<FakeDialog>();
  auto prompt = std::make_unique<FakePrompt>();
  auto* promptPtr = prompt.get();
  promptPtr->decisions = {seam::platform::UnsavedDecision::Discard};
  bool quit = false;
  auto controller = seam::standalone::StandaloneApplicationController::create(
      *session, std::move(dialog), std::move(prompt),
      seam::standalone::StandaloneApplicationControllerConfig{
          .autosaveRoot = blockedAutosaveRoot,
          .recentProjectsPath = root / "recent.json",
          .defaultNewProject = {
              .name = "Unsaved",
              .tempoBpm = 120.0,
              .sampleRate = 48000U,
              .outputChannels = 2U,
              .initialVoicebank = std::nullopt,
          },
          .stateChanged = {},
      },
      [&quit] { quit = true; });
  CHECK(controller);
  addNote(*session);
  CHECK(controller.value()->autosave().request(session->runtime().document()));

  const auto close = controller.value()->requestClose();
  CHECK(!close);
  CHECK(!quit);
  CHECK(session->runtime().document().dirty());
}
