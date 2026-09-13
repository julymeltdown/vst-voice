#include "test_framework.hpp"
#include "test_support.hpp"
#include "test_support.hpp"

#include "seam/application/note_commands.hpp"
#include "seam/core/file_io.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/platform/application_menu.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/standalone/application_controller.hpp"
#include "seam/standalone/authoring_session.hpp"
#include "seam/voicebank/wav.hpp"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
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
  unsigned hintRequests = 0U;
  unsigned findRequests = 0U;
  unsigned vibratoRequests = 0U;
  unsigned cleanupRequests = 0U;
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
          .editPronunciationHint = [&hintRequests] {
            ++hintRequests;
            return hintRequests == 1U ? seam::core::success() :
                seam::core::failure(seam::core::ErrorCode::Conflict, "Select one note");
          },
          .findReplaceLyrics = [&findRequests] { ++findRequests; return seam::core::success(); },
          .findNotes = [&findRequests] { findRequests += 10U; return seam::core::success(); },
          .findActiveDiagnostics = [&findRequests] { findRequests += 10000U; return seam::core::success(); },
          .findNextNote = [&findRequests] { findRequests += 100U; return seam::core::success(); },
          .findPreviousNote = [&findRequests] { findRequests += 1000U; return seam::core::success(); },
          .clearSelectedVibrato = [&vibratoRequests] { ++vibratoRequests; return seam::core::success(); },
          .editSelectedVibrato = [&vibratoRequests] { vibratoRequests += 10U; return seam::core::success(); },
          .editRegionDynamics = [&vibratoRequests] { vibratoRequests += 100U; return seam::core::success(); },
          .editTrackStyle = [&vibratoRequests] { vibratoRequests += 1000U; return seam::core::success(); },
          .editJapaneseReading = [&vibratoRequests] { vibratoRequests += 10000U; return seam::core::success(); },
          .removeSelectedOverlaps = [&cleanupRequests] { ++cleanupRequests; return seam::core::success(); },
          .closeSelectedGaps = [&cleanupRequests] { cleanupRequests += 10U; return seam::core::success(); },
          .autoLegatoSelectedNotes = [&cleanupRequests] { cleanupRequests += 100U; return seam::core::success(); },
          .clearRegionDynamicsCurve = [&cleanupRequests] { cleanupRequests += 1000U; return seam::core::success(); },
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
  const auto revision = session->runtime().document().session().revision();
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::EditPronunciationHint));
  CHECK(!controller.value()->dispatch(seam::platform::ApplicationCommand::EditPronunciationHint));
  CHECK(hintRequests == 2U);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::FindReplaceLyrics));
  CHECK(findRequests == 1U);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::FindNotes)); CHECK(findRequests == 11U);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::FindNextNote)); CHECK(findRequests == 111U);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::FindPreviousNote)); CHECK(findRequests == 1111U);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::FindActiveDiagnostics)); CHECK(findRequests == 11111U);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::ClearSelectedVibrato));
  CHECK(vibratoRequests == 1U);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::EditSelectedVibrato)); CHECK(vibratoRequests == 11U);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::EditRegionDynamics)); CHECK(vibratoRequests == 111U);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::EditTrackStyle)); CHECK(vibratoRequests == 1111U);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::EditJapaneseReading)); CHECK(vibratoRequests == 11111U);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::RemoveSelectedOverlaps));
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::CloseSelectedGaps)); CHECK(cleanupRequests == 11U);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::AutoLegatoSelectedNotes)); CHECK(cleanupRequests == 111U);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::ClearRegionDynamicsCurve)); CHECK(cleanupRequests == 1111U);
  CHECK(session->runtime().document().session().revision() == revision);
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

TEST_CASE("standalone_controller_proposes_automatic_performance_as_a_proposal") {
  const auto root = seam::test::support::temporaryDirectory("standalone-proposal");
  auto session = makeSession(root);
  addNote(*session);
  bool quit = false;
  seam::standalone::StandaloneApplicationControllerConfig config{};
  config.autosaveRoot = root / "autosaves";
  config.recentProjectsPath = root / "recent.json";
  auto controller = seam::standalone::StandaloneApplicationController::create(*session,
      std::make_unique<FakeDialog>(), std::make_unique<FakePrompt>(), config,
      [&quit] { quit = true; });
  CHECK(controller);
  if (!controller) return;
  const auto regionId = session->regionId();
  const auto* region = session->runtime().document().session().project().findRegion(regionId);
  CHECK(region != nullptr);
  if (region == nullptr) return;
  const auto regionEnd = region->durationTick;
  const seam::domain::PerformanceTimeRange expectedRange{seam::time::Tick{0}, regionEnd};

  // The menu command runs the production backend and adopts the result as a
  // proposal. It must not accept it, and it must not touch unrelated state.
  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::ProposeAutomaticPerformance));
  const auto& afterFirst = session->runtime().document().session().project()
                               .findRegion(regionId)->performance;
  CHECK(afterFirst.takes.size() == 1U);
  CHECK(afterFirst.takes.front().state == seam::domain::PerformanceProposalState::Proposed);
  CHECK(afterFirst.takes.front().generatorId == "seam-phrase-proposal");
  CHECK(afterFirst.takes.front().range == expectedRange);
  CHECK(afterFirst.accepted.empty());

  // Asking again is a second, distinct proposal over the same material.
  CHECK(controller.value()->proposeAutomaticPerformance(seam::platform::PerformanceEditScope::Whole, {}));
  const auto& afterSecond = session->runtime().document().session().project()
                                .findRegion(regionId)->performance;
  CHECK(afterSecond.takes.size() == 2U);
  CHECK(afterSecond.takes[0].id != afterSecond.takes[1].id);
  CHECK(afterSecond.accepted.empty());
  CHECK(session->runtime().undo());
  CHECK(session->runtime().document().session().project().findRegion(regionId)
            ->performance.takes.size() == 1U);
}

TEST_CASE("standalone_controller_decides_a_performance_take_by_identity") {
  const auto root = seam::test::support::temporaryDirectory("standalone-decision");
  auto session = makeSession(root);
  addNote(*session);
  bool quit = false;
  seam::standalone::StandaloneApplicationControllerConfig config{};
  config.autosaveRoot = root / "autosaves";
  config.recentProjectsPath = root / "recent.json";
  auto controller = seam::standalone::StandaloneApplicationController::create(*session,
      std::make_unique<FakeDialog>(), std::make_unique<FakePrompt>(), config,
      [&quit] { quit = true; });
  CHECK(controller);
  if (!controller) return;
  const auto regionId = session->regionId();
  const auto performance = [&]() -> const seam::domain::RegionPerformanceState& {
    return session->runtime().document().session().project().findRegion(regionId)
        ->performance;
  };

  // Two proposals over the same material, so a decision has to name one of them.
  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::ProposeAutomaticPerformance));
  CHECK(controller.value()->proposeAutomaticPerformance(seam::platform::PerformanceEditScope::Whole, {}));
  const std::string firstId = performance().takes[0].id;
  const std::string secondId = performance().takes[1].id;
  CHECK(firstId != secondId);
  const auto offered = controller.value()->performanceTakes();
  CHECK(offered.size() == 2U);
  CHECK(offered[0].id == firstId);
  CHECK(offered[0].label.find(firstId) == std::string::npos);
  // A proposal is named from its own identity, not from its position, so a label
  // always identifies the generator, seed, span and channels that produced it.
  CHECK(offered[0].label.find("seed") != std::string::npos);
  CHECK(offered[0].label.find("pitch") != std::string::npos);
  CHECK(!offered[0].accepted);
  CHECK(!offered[1].accepted);

  // Choosing a take selects it over its own captured span on every channel it
  // carries, and leaves the other proposal exactly as it was.
  CHECK(controller.value()->acceptPerformanceTake(
      firstId,
      seam::platform::PerformanceEditScope::Whole, {}));
  CHECK(performance().accepted.size() == performance().takes[0].lanes.size());
  CHECK(performance().takes[1].state ==
        seam::domain::PerformanceProposalState::Proposed);
  std::vector<seam::domain::PerformanceChannel> selectedChannels;
  for (const auto& selection : performance().accepted) {
    CHECK(selection.takeId == firstId);
    CHECK(selection.sourceTickOffset == seam::time::Tick{0});
    selectedChannels.push_back(selection.channel);
  }
  for (const auto& lane : performance().takes[0].lanes) {
    CHECK(std::find(selectedChannels.begin(), selectedChannels.end(),
                    lane.channel) != selectedChannels.end());
  }
  const auto afterAccept = controller.value()->performanceTakes();
  CHECK(afterAccept.size() == 2U);
  CHECK(afterAccept[0].accepted);
  CHECK(!afterAccept[1].accepted);

  // The decision is an ordinary edit: undo restores the region without a choice.
  CHECK(session->runtime().undo());
  CHECK(performance().accepted.empty());
  CHECK(performance().takes.size() == 2U);

  // Rejecting records the decision on the take instead of deleting it, and the
  // rejected take is not offered again.
  CHECK(controller.value()->rejectPerformanceTake(secondId));
  CHECK(performance().takes.size() == 2U);
  CHECK(performance().takes[1].state ==
        seam::domain::PerformanceProposalState::Rejected);
  const auto afterReject = controller.value()->performanceTakes();
  CHECK(afterReject.size() == 1U);
  CHECK(afterReject[0].id == firstId);

  const auto repeated = controller.value()->rejectPerformanceTake(secondId);
  CHECK(!repeated);
  CHECK(repeated.error().code == seam::core::ErrorCode::Conflict);
  const auto unknown = controller.value()->acceptPerformanceTake(
      "take-not-here", seam::platform::PerformanceEditScope::Whole, {});
  CHECK(!unknown);
  CHECK(unknown.error().code == seam::core::ErrorCode::NotFound);

  // The decision is project state, not session state: a rejected take and the
  // accepted selection have to survive save and reopen, or the audit trail that
  // explains what the creator refused would end with the process.
  seam::formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(session->runtime().document().session().project());
  CHECK(encoded);
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value().findRegion(regionId)->performance == performance());
  CHECK(decoded.value().findRegion(regionId)->performance.takes[1].state ==
        seam::domain::PerformanceProposalState::Rejected);
}

TEST_CASE("standalone_controller_accepts_a_take_over_the_selected_notes_only") {
  const auto root = seam::test::support::temporaryDirectory("standalone-partial-decision");
  auto session = makeSession(root);
  addNote(*session);
  bool quit = false;
  seam::standalone::StandaloneApplicationControllerConfig config{};
  config.autosaveRoot = root / "autosaves";
  config.recentProjectsPath = root / "recent.json";
  auto controller = seam::standalone::StandaloneApplicationController::create(*session,
      std::make_unique<FakeDialog>(), std::make_unique<FakePrompt>(), config,
      [&quit] { quit = true; });
  CHECK(controller);
  if (!controller) return;
  const auto regionId = session->regionId();
  auto& editable = session->runtime().document().session();
  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::ProposeAutomaticPerformance));
  const auto takeId =
      editable.project().findRegion(regionId)->performance.takes.front().id;
  const auto note = editable.project().findRegion(regionId)->notes.front();
  const auto noteRange =
      seam::domain::PerformanceTimeRange{note.startTick, note.endTick()};
  const auto takeRange =
      editable.project().findRegion(regionId)->performance.takes.front().range;
  CHECK(takeRange.startTick <= noteRange.startTick);
  CHECK(noteRange.endTick <= takeRange.endTick);

  // Without a selection there is no span to act on, so the surface refuses instead
  // of silently choosing the whole take on the creator's behalf.
  editable.selection().clear();
  const auto none = controller.value()->acceptPerformanceTake(
      takeId,
      seam::platform::PerformanceEditScope::SelectedNotes, {});
  CHECK(!none);
  CHECK(none.error().code == seam::core::ErrorCode::Conflict);

  // The selected note is what the decision replaces: the accepted selection covers
  // exactly that span instead of the whole take the backend generated.
  editable.selection().selectOnly(note.id);
  CHECK(controller.value()->acceptPerformanceTake(
      takeId,
      seam::platform::PerformanceEditScope::SelectedNotes, {}));
  const auto& accepted =
      editable.project().findRegion(regionId)->performance.accepted;
  const auto laneCount =
      editable.project().findRegion(regionId)->performance.takes.front().lanes.size();
  CHECK(accepted.size() == laneCount);
  for (const auto& selection : accepted) {
    CHECK(selection.takeId == takeId);
    CHECK(selection.sourceTickOffset == seam::time::Tick{0});
    CHECK(std::get<seam::domain::PerformanceTimeRange>(selection.scope) == noteRange);
  }

  // A second note and a second proposal: deciding on the new note must not discard
  // what was already accepted for the first one, which is the whole point of a
  // range decision over a whole-take replacement.
  auto [lyric, second] = session->runtime().document().factory().makeNote(
      seam::time::Tick{960}, seam::time::Tick{960}, 62U, U"\u304d",
      seam::domain::Language::Japanese);
  CHECK(session->runtime().execute(std::make_unique<seam::application::AddNoteCommand>(
      regionId, std::move(lyric), std::move(second))));
  const auto secondNote = editable.project().findRegion(regionId)->notes.back().id;
  const auto secondRange = seam::domain::PerformanceTimeRange{
      seam::time::Tick{960}, seam::time::Tick{1920}};
  CHECK(controller.value()->proposeAutomaticPerformance(seam::platform::PerformanceEditScope::Whole, {}));
  const auto secondTakeId =
      editable.project().findRegion(regionId)->performance.takes.back().id;
  CHECK(secondTakeId != takeId);
  editable.selection().selectOnly(secondNote);
  CHECK(controller.value()->acceptPerformanceTake(
      secondTakeId,
      seam::platform::PerformanceEditScope::SelectedNotes, {}));
  const auto& merged =
      editable.project().findRegion(regionId)->performance.accepted;
  CHECK(merged.size() == laneCount * 2U);
  std::size_t firstNoteSelections{0U};
  std::size_t secondNoteSelections{0U};
  for (const auto& selection : merged) {
    const auto range = std::get<seam::domain::PerformanceTimeRange>(selection.scope);
    if (range == noteRange) {
      CHECK(selection.takeId == takeId);
      ++firstNoteSelections;
      continue;
    }
    CHECK(range == secondRange);
    CHECK(selection.takeId == secondTakeId);
    ++secondNoteSelections;
  }
  CHECK(firstNoteSelections == laneCount);
  CHECK(secondNoteSelections == laneCount);
}

TEST_CASE("standalone_controller_regenerates_only_the_selected_notes") {
  const auto root = seam::test::support::temporaryDirectory("standalone-partial-proposal");
  auto session = makeSession(root);
  addNote(*session);
  bool quit = false;
  seam::standalone::StandaloneApplicationControllerConfig config{};
  config.autosaveRoot = root / "autosaves";
  config.recentProjectsPath = root / "recent.json";
  auto controller = seam::standalone::StandaloneApplicationController::create(*session,
      std::make_unique<FakeDialog>(), std::make_unique<FakePrompt>(), config,
      [&quit] { quit = true; });
  CHECK(controller);
  if (!controller) return;
  const auto regionId = session->regionId();
  auto& editable = session->runtime().document().session();
  const auto performance = [&]() -> const seam::domain::RegionPerformanceState& {
    return editable.project().findRegion(regionId)->performance;
  };
  const auto regionDuration = editable.project().findRegion(regionId)->durationTick;
  auto [lyric, second] = session->runtime().document().factory().makeNote(
      seam::time::Tick{960}, seam::time::Tick{960}, 62U, U"\u304d",
      seam::domain::Language::Japanese);
  CHECK(session->runtime().execute(std::make_unique<seam::application::AddNoteCommand>(
      regionId, std::move(lyric), std::move(second))));
  const auto secondNote = editable.project().findRegion(regionId)->notes.back().id;
  const auto wholeRegion = seam::domain::PerformanceTimeRange{seam::time::Tick{0}, regionDuration};
  const auto secondSpan = seam::domain::PerformanceTimeRange{
      seam::time::Tick{960}, seam::time::Tick{1920}};

  // The whole-region command is unchanged.
  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::ProposeAutomaticPerformance));
  CHECK(performance().takes.size() == 1U);
  CHECK(performance().takes.back().range == wholeRegion);

  // Selecting one note and regenerating proposes over exactly that note, so a repair
  // pass never re-proposes material that already sounds right.
  editable.selection().selectOnly(secondNote);
  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::ProposeAutomaticPerformanceOverSelectedNotes));
  CHECK(performance().takes.size() == 2U);
  CHECK(performance().takes.back().range == secondSpan);
  CHECK(performance().takes.back().state ==
        seam::domain::PerformanceProposalState::Proposed);
  CHECK(performance().takes.front().range == wholeRegion);
  CHECK(performance().accepted.empty());

  // Without a selection there is nothing to regenerate, and nothing is published.
  editable.selection().clear();
  const auto none = controller.value()->dispatch(
      seam::platform::ApplicationCommand::ProposeAutomaticPerformanceOverSelectedNotes);
  CHECK(!none);
  CHECK(none.error().code == seam::core::ErrorCode::Conflict);
  CHECK(performance().takes.size() == 2U);
}

TEST_CASE("standalone_controller_decides_and_regenerates_one_channel_at_a_time") {
  const auto root = seam::test::support::temporaryDirectory("standalone-channel");
  auto session = makeSession(root);
  addNote(*session);
  bool quit = false;
  seam::standalone::StandaloneApplicationControllerConfig config{};
  config.autosaveRoot = root / "autosaves";
  config.recentProjectsPath = root / "recent.json";
  auto controller = seam::standalone::StandaloneApplicationController::create(*session,
      std::make_unique<FakeDialog>(), std::make_unique<FakePrompt>(), config,
      [&quit] { quit = true; });
  CHECK(controller);
  if (!controller) return;
  const auto regionId = session->regionId();
  auto& editable = session->runtime().document().session();
  const auto performance = [&]() -> const seam::domain::RegionPerformanceState& {
    return editable.project().findRegion(regionId)->performance;
  };

  // A channel-scoped regeneration produces a proposal that carries only that channel,
  // so the pitch a creator already likes is untouched by regenerating dynamics.
  const auto noteId = editable.project().findRegion(regionId)->notes.front().id;
  editable.selection().selectOnly(noteId);
  const std::vector<std::string> dynamicsOnly{"dynamics"};
  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::ProposeAutomaticPerformance));
  CHECK(controller.value()->proposeAutomaticPerformance(
      seam::platform::PerformanceEditScope::SelectedNotes, dynamicsOnly));
  CHECK(performance().takes.size() == 2U);
  const auto channelTake = performance().takes.back();
  CHECK(channelTake.lanes.size() == 1U);
  CHECK(channelTake.lanes.front().channel == seam::domain::PerformanceChannel::Dynamics);
  CHECK(performance().takes.front().lanes.size() == 4U);

  // The surface sees exactly the channels a take carries, so it cannot offer a
  // decision the backend never generated.
  const auto listed = controller.value()->performanceTakes();
  CHECK(listed.size() == 2U);
  const auto channelItem = std::find_if(listed.begin(), listed.end(),
      [&](const auto& item) { return item.id == channelTake.id; });
  CHECK(channelItem != listed.end());
  CHECK(channelItem->channels == dynamicsOnly);

  // Accepting the channel take for that one channel selects only it.
  CHECK(controller.value()->acceptPerformanceTake(channelTake.id,
      seam::platform::PerformanceEditScope::Whole, dynamicsOnly));
  CHECK(performance().accepted.size() == 1U);
  CHECK(performance().accepted.front().takeId == channelTake.id);
  CHECK(performance().accepted.front().channel ==
        seam::domain::PerformanceChannel::Dynamics);

  // A channel the take does not carry is refused instead of widening the decision.
  const std::vector<std::string> pitchOnly{"pitch"};
  const auto missing = controller.value()->acceptPerformanceTake(channelTake.id,
      seam::platform::PerformanceEditScope::Whole, pitchOnly);
  CHECK(!missing);
  CHECK(missing.error().code == seam::core::ErrorCode::Conflict);
  const std::vector<std::string> unknownChannel{"vibrato"};
  const auto unknown = controller.value()->acceptPerformanceTake(channelTake.id,
      seam::platform::PerformanceEditScope::Whole, unknownChannel);
  CHECK(!unknown);
  CHECK(unknown.error().code == seam::core::ErrorCode::InvalidArgument);
  const auto unsupported = controller.value()->proposeAutomaticPerformance(
      seam::platform::PerformanceEditScope::SelectedNotes, unknownChannel);
  CHECK(!unsupported);
  CHECK(unsupported.error().code == seam::core::ErrorCode::InvalidArgument);
  const std::vector<std::string> timingOnly{"timing"};
  const auto ungeneratable = controller.value()->proposeAutomaticPerformance(
      seam::platform::PerformanceEditScope::SelectedNotes, timingOnly);
  CHECK(!ungeneratable);
  CHECK(ungeneratable.error().code == seam::core::ErrorCode::Unsupported);
}

TEST_CASE("standalone_controller_compares_two_takes_from_one_playhead") {
  const auto root = seam::test::support::temporaryDirectory("standalone-comparison");
  auto session = makeSession(root);
  addNote(*session);
  bool quit = false;
  seam::standalone::StandaloneApplicationControllerConfig config{};
  config.autosaveRoot = root / "autosaves";
  config.recentProjectsPath = root / "recent.json";
  auto controller = seam::standalone::StandaloneApplicationController::create(*session,
      std::make_unique<FakeDialog>(), std::make_unique<FakePrompt>(), config,
      [&quit] { quit = true; });
  CHECK(controller);
  if (!controller) return;
  const auto regionId = session->regionId();
  const auto performance = [&]() -> const seam::domain::RegionPerformanceState& {
    return session->runtime().document().session().project().findRegion(regionId)
        ->performance;
  };
  CHECK(controller.value()->dispatch(
      seam::platform::ApplicationCommand::ProposeAutomaticPerformance));
  CHECK(controller.value()->proposeAutomaticPerformance(seam::platform::PerformanceEditScope::Whole, {}));
  const std::string firstId = performance().takes[0].id;
  const std::string secondId = performance().takes[1].id;
  CHECK(controller.value()->acceptPerformanceTake(
      firstId,
      seam::platform::PerformanceEditScope::Whole, {}));
  const auto acceptedFirst = performance().accepted;
  CHECK(!acceptedFirst.empty());
  CHECK(controller.value()->performanceComparison() == std::nullopt);

  // Comparing the second take applies it while the first state stays held, so both
  // sides exist at once and neither is a copy of the other.
  CHECK(controller.value()->beginPerformanceComparison(
      secondId,
      seam::platform::PerformanceEditScope::Whole, {}));
  const auto comparison = controller.value()->performanceComparison();
  CHECK(comparison.has_value());
  CHECK(comparison->takeId == secondId);
  CHECK(comparison->candidateApplied);
  CHECK(!comparison->label.empty());
  const auto acceptedSecond = performance().accepted;
  CHECK(acceptedSecond != acceptedFirst);
  for (const auto& selection : acceptedSecond) {
    CHECK(selection.takeId == secondId);
  }

  // Swapping is an ordinary undoable edit that restores the exact other side.
  CHECK(controller.value()->swapPerformanceComparison());
  CHECK(performance().accepted == acceptedFirst);
  CHECK(!controller.value()->performanceComparison()->candidateApplied);
  CHECK(controller.value()->swapPerformanceComparison());
  CHECK(performance().accepted == acceptedSecond);
  CHECK(controller.value()->performanceComparison()->candidateApplied);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::Undo));
  CHECK(performance().accepted == acceptedFirst);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::Redo));
  CHECK(performance().accepted == acceptedSecond);

  // Ending keeps whichever side is sounding instead of silently reverting it.
  CHECK(controller.value()->endPerformanceComparison());
  CHECK(controller.value()->performanceComparison() == std::nullopt);
  CHECK(performance().accepted == acceptedSecond);
  const auto lapsed = controller.value()->swapPerformanceComparison();
  CHECK(!lapsed);
  CHECK(lapsed.error().code == seam::core::ErrorCode::Conflict);

  const auto unknown = controller.value()->beginPerformanceComparison(
      "take-not-here", seam::platform::PerformanceEditScope::Whole, {});
  CHECK(!unknown);
  CHECK(unknown.error().code == seam::core::ErrorCode::NotFound);
  const auto alreadyAccepted = controller.value()->beginPerformanceComparison(
      secondId,
      seam::platform::PerformanceEditScope::Whole, {});
  CHECK(!alreadyAccepted);
  CHECK(alreadyAccepted.error().code == seam::core::ErrorCode::Conflict);

  CHECK(controller.value()->beginPerformanceComparison(
      firstId,
      seam::platform::PerformanceEditScope::Whole, {}));
  const auto nested = controller.value()->beginPerformanceComparison(
      secondId,
      seam::platform::PerformanceEditScope::Whole, {});
  CHECK(!nested);
  CHECK(nested.error().code == seam::core::ErrorCode::Conflict);
  CHECK(controller.value()->endPerformanceComparison());
}

TEST_CASE("standalone_controller_refuses_a_neural_deployment_it_cannot_verify") {
  const auto root = seam::test::support::temporaryDirectory("standalone-neural");
  auto session = makeSession(root);
  bool quit = false;

  // The surface declares a deployment signed by nobody. Selection must consult it
  // and refuse: a helper SEAM cannot verify may not be executed.
  static const char moduleAnchor{0};
  const auto descriptor = root / "neural-deployment.json";
  CHECK(seam::core::durableAtomicWriteText(descriptor,
      "{\"formatId\":\"com.project-seam.neural-deployment\",\"schemaVersion\":2}"));
  seam::authoring::NeuralSelectionSurface surface{};
  surface.deploymentDescriptor = descriptor;
  surface.buildId = "fixture-build";
  surface.platform = "macos-arm64";
  surface.surface = "standalone";
  surface.moduleAnchor = &moduleAnchor;
  surface.provenance = seam::rendering::NeuralRenderProvenance{
      .workerVersion = "seam.neural-worker.v1",
      .runtimeVersion = "onnxruntime-1.30.0",
      .provider = "CPUExecutionProvider"};
  surface.maximumBundleBytes = 1024U * 1024U;
  surface.maximumFrames = 96000U;
  surface.inferenceSteps = 10;
  surface.maximumResidentBytes = 256U * 1024U * 1024U;
  surface.maximumCpuTime = std::chrono::seconds{5};
  surface.helperTimeout = std::chrono::seconds{20};
  seam::standalone::StandaloneApplicationControllerConfig configured{};
  configured.autosaveRoot = root / "autosaves";
  configured.recentProjectsPath = root / "recent.json";
  configured.neuralSelection = surface;
  configured.neuralResourceRoot = root / "neural-resources";
  auto refused = seam::standalone::StandaloneApplicationController::create(*session,
      std::make_unique<FakeDialog>(), std::make_unique<FakePrompt>(), configured,
      [&quit] { quit = true; });
  // Construction initializes the surface, so an unusable deployment is refused
  // before the controller exists at all.
  CHECK(!refused);
  CHECK(!quit);

  // An installation that ships no neural helper still starts exactly as before, so
  // adding the surface configuration cannot break a build without one.
  seam::standalone::StandaloneApplicationControllerConfig plain{};
  plain.autosaveRoot = root / "autosaves";
  plain.recentProjectsPath = root / "recent.json";
  auto controller = seam::standalone::StandaloneApplicationController::create(*session,
      std::make_unique<FakeDialog>(), std::make_unique<FakePrompt>(), plain,
      [&quit] { quit = true; });
  CHECK(controller);
}
