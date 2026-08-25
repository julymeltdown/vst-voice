#include "test_framework.hpp"
#include "test_support.hpp"
#include "test_support.hpp"

#include "seam/application/note_commands.hpp"
#include "seam/platform/application_menu.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/standalone/application_controller.hpp"
#include "seam/standalone/authoring_session.hpp"
#include "seam/voicebank/wav.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
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
  bool openedAudioSettings = false;
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
          .openAudioSettings = [&openedAudioSettings] {
            openedAudioSettings = true;
            return seam::core::success();
          },
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
  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::OpenAudioSettings));
  CHECK(openedAudioSettings);
}

TEST_CASE("standalone_new_project_request_can_commit_an_explicit_native_path") {
  const auto root = seam::test::support::temporaryDirectory("standalone-new-project-path");
  auto session = makeSession(root);
  const auto path = root / "new-song.seam";
  CHECK(session->createNewProject(seam::authoring::NewProjectRequest{
      .name = "New Song",
      .tempoBpm = 128.0,
      .sampleRate = 48000U,
      .outputChannels = 2U,
      .createInitialVocalTrack = false,
      .initialVoicebank = std::nullopt,
      .projectPath = path,
  }));
  CHECK(std::filesystem::exists(path));
  CHECK(session->runtime().document().identity().projectPath == path);
  CHECK(!session->runtime().document().dirty());
  CHECK(session->runtime().document().session().project().vocalTracks().empty());
}

TEST_CASE("startup open-file replaces the provisional Untitled document") {
  const auto root = seam::test::support::temporaryDirectory("standalone-startup-open");
  const auto target = root / "startup-target.seam";
  {
    auto source = makeSession(root);
    addNote(*source);
    CHECK(source->saveProjectAs(target));
  }

  auto session = makeSession(root);
  auto dialog = std::make_unique<FakeDialog>();
  auto prompt = std::make_unique<FakePrompt>();
  auto* promptPtr = prompt.get();
  auto controller = seam::standalone::StandaloneApplicationController::create(
      *session, std::move(dialog), std::move(prompt),
      seam::standalone::StandaloneApplicationControllerConfig{
          .autosaveRoot = root / "autosaves",
          .recentProjectsPath = root / "recent.json",
          .defaultNewProject = {},
          .stateChanged = {},
      });
  CHECK(controller);
  CHECK(controller.value()->openRecent(target));
  CHECK(promptPtr->names.empty());
  CHECK(session->runtime().document().identity().projectPath == target);
}

TEST_CASE("standalone_application_controller_imports_project_owned_backing_audio") {
  const auto root = seam::test::support::temporaryDirectory("standalone-import-audio");
  auto session = makeSession(root);
  auto dialog = std::make_unique<FakeDialog>();
  auto* dialogPtr = dialog.get();
  auto prompt = std::make_unique<FakePrompt>();
  const auto projectPath = root / "song.seam";
  const auto mediaPath = root / "backing.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(
      mediaPath, 48000U, seam::test::support::sineWave(48000U, 220.0, 0.1)));
  dialogPtr->responses = {projectPath, mediaPath};
  auto controller = seam::standalone::StandaloneApplicationController::create(
      *session, std::move(dialog), std::move(prompt),
      seam::standalone::StandaloneApplicationControllerConfig{
          .autosaveRoot = root / "autosaves",
          .recentProjectsPath = root / "recent.json",
          .defaultNewProject = {},
          .stateChanged = {},
      });
  CHECK(controller);
  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::SaveProjectAs));
  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::ImportAudio));
  const auto& audio = session->runtime().document().session().project().audioTracks();
  CHECK(audio.size() == 1U);
  CHECK(audio.front().mediaOwnership == seam::domain::MediaOwnership::ProjectCopy);
  CHECK(!audio.front().mediaHash.empty());
  CHECK(std::filesystem::exists(root / "song.seam.media"));

  const auto originalHash = audio.front().mediaHash;
  const auto relinkPath = root / "relinked-backing.wav";
  std::filesystem::copy_file(mediaPath, relinkPath,
                              std::filesystem::copy_options::overwrite_existing);
  dialogPtr->responses.push_back(relinkPath);
  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::RelinkBackingAudio));
  CHECK(dialogPtr->requests.size() == 3U);
  CHECK(dialogPtr->requests.back().purpose ==
        seam::platform::FileDialogPurpose::RelinkMedia);
  CHECK(audio.front().mediaHash == originalHash);
  CHECK(audio.front().mediaOwnership == seam::domain::MediaOwnership::ProjectCopy);
  CHECK(std::filesystem::path{audio.front().mediaPath}.is_relative());
  CHECK(std::filesystem::exists(
      projectPath.parent_path() / std::filesystem::path{audio.front().mediaPath}));
}

TEST_CASE("standalone_application_controller_export_set_cancel_is_side_effect_free") {
  const auto root = seam::test::support::temporaryDirectory("standalone-export-cancel");
  auto session = makeSession(root);
  auto dialog = std::make_unique<FakeDialog>();
  auto* dialogPtr = dialog.get();
  dialogPtr->responses = {std::nullopt};
  auto prompt = std::make_unique<FakePrompt>();
  auto controller = seam::standalone::StandaloneApplicationController::create(
      *session, std::move(dialog), std::move(prompt),
      seam::standalone::StandaloneApplicationControllerConfig{
          .autosaveRoot = root / "autosaves",
          .recentProjectsPath = root / "recent.json",
          .defaultNewProject = {},
          .stateChanged = {},
      });
  CHECK(controller);
  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::ExportSet));
  CHECK(dialogPtr->requests.size() == 1U);
  CHECK(dialogPtr->requests.front().purpose ==
        seam::platform::FileDialogPurpose::ExportSet);
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    CHECK(entry.path().filename().string().find("staging") ==
          std::string::npos);
  }
}

TEST_CASE("standalone export failure retains an actionable progress diagnostic") {
  const auto root = seam::test::support::temporaryDirectory(
      "standalone-export-failure-diagnostic");
  auto session = makeSession(root);
  addNote(*session);
  auto dialog = std::make_unique<FakeDialog>();
  auto prompt = std::make_unique<FakePrompt>();
  auto controller = seam::standalone::StandaloneApplicationController::create(
      *session, std::move(dialog), std::move(prompt),
      seam::standalone::StandaloneApplicationControllerConfig{
          .autosaveRoot = root / "autosaves",
          .recentProjectsPath = root / "recent.json",
          .defaultNewProject = {},
          .stateChanged = {},
          .progressChanged = {},
      });
  CHECK(controller);

  const auto settings = seam::authoring::ExportSettings{
      .sampleRate = 48000U,
      .channels = 2U,
      .format = seam::voicebank::WavSampleFormat::Pcm24,
      .includeMaster = true,
      .includeStems = false,
      .replaceExisting = false,
  };
  CHECK(controller.value()->exportSet(root / "successful-export", settings));
  CHECK(controller.value()->lastExport().has_value());

  const auto destination = root / "existing-export";
  CHECK(std::filesystem::create_directories(destination));
  const auto result = controller.value()->exportSet(destination, settings);
  CHECK(!result);
  CHECK(!controller.value()->lastExport().has_value());
  const auto progress = controller.value()->exportProgress().progress();
  CHECK(progress.state == seam::authoring::ExportState::Failed);
  CHECK(progress.currentOutput.rfind(
            "Export destination already contains an export set", 0U) == 0U);
  CHECK(progress.currentOutput.find(destination.string()) != std::string::npos);
}

TEST_CASE("standalone_application_controller_exports_off_thread_and_protects_quit") {
  const auto root = seam::test::support::temporaryDirectory("standalone-export-async");
  auto session = makeSession(root);
  addNote(*session);
  auto dialog = std::make_unique<FakeDialog>();
  auto prompt = std::make_unique<FakePrompt>();
  bool quit = false;
  auto controller = seam::standalone::StandaloneApplicationController::create(
      *session, std::move(dialog), std::move(prompt),
      seam::standalone::StandaloneApplicationControllerConfig{
          .autosaveRoot = root / "autosaves",
          .recentProjectsPath = root / "recent.json",
          .defaultNewProject = {},
          .stateChanged = {},
          .progressChanged = {},
      },
      [&quit] { quit = true; });
  CHECK(controller);

  const auto destination = root / "async-export";
  CHECK(controller.value()->startExportSet(
      destination,
      seam::authoring::ExportSettings{
          .sampleRate = 48000U,
          .channels = 2U,
          .format = seam::voicebank::WavSampleFormat::Pcm24,
          .includeMaster = true,
          .includeStems = false,
          .replaceExisting = false,
      }));
  const auto closeWhileRunning = controller.value()->requestClose();
  if (controller.value()->exportInProgress()) {
    CHECK(!closeWhileRunning);
    CHECK(!quit);
    controller.value()->cancelExport();
  }
  for (std::size_t attempt = 0U;
       attempt < 500U && controller.value()->exportInProgress(); ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  CHECK(!controller.value()->exportInProgress());
  CHECK(controller.value()->lastExport().has_value());
}

TEST_CASE("standalone single-file export publishes its committed result") {
  const auto root = seam::test::support::temporaryDirectory("standalone-export-audio-result");
  auto session = makeSession(root);
  addNote(*session);
  auto dialog = std::make_unique<FakeDialog>();
  auto* dialogPtr = dialog.get();
  const auto destination = root / "master.wav";
  dialogPtr->responses = {destination};
  auto prompt = std::make_unique<FakePrompt>();
  auto controller = seam::standalone::StandaloneApplicationController::create(
      *session, std::move(dialog), std::move(prompt),
      seam::standalone::StandaloneApplicationControllerConfig{
          .autosaveRoot = root / "autosaves",
          .recentProjectsPath = root / "recent.json",
          .defaultNewProject = {},
          .stateChanged = {},
      });
  CHECK(controller);
  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::ExportAudio));
  CHECK(dialogPtr->requests.size() == 1U);
  CHECK(dialogPtr->requests.front().purpose ==
        seam::platform::FileDialogPurpose::ExportAudio);
  const auto result = controller.value()->lastExport();
  CHECK(result.has_value());
  CHECK(result->state == seam::authoring::ExportState::Committed);
  CHECK(result->masterPath == destination);
  CHECK(std::filesystem::exists(destination));
  CHECK(std::filesystem::exists(result->receiptPath));
  const auto progress = controller.value()->exportProgress().progress();
  CHECK(progress.state == seam::authoring::ExportState::Committed);
  CHECK(progress.completedFiles == 1U);
  CHECK(progress.totalFiles == 1U);
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
