#include "test_framework.hpp"
#include "test_support.hpp"
#include "test_support.hpp"

#include "seam/application/note_commands.hpp"
#include "seam/build/version.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/platform/application_menu.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/standalone/application_controller.hpp"
#include "seam/standalone/authoring_session.hpp"
#include "seam/synthesis/performance_compiler.hpp"
#include "seam/voicebank/wav.hpp"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
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
    if (onChoose) onChoose(request);
    if (responses.empty()) return std::optional<std::filesystem::path>{};
    auto response = responses.front();
    responses.erase(responses.begin());
    return response;
  }

  std::vector<seam::platform::FileDialogRequest> requests;
  std::vector<std::optional<std::filesystem::path>> responses;
  std::function<void(const seam::platform::FileDialogRequest&)> onChoose;
};

class FakePrompt final : public seam::platform::IUnsavedChangesPrompt {
public:
  seam::core::Result<seam::platform::UnsavedDecision> choose(
      std::string_view name) override {
    names.emplace_back(name);
    if (onChoose) onChoose();
    if (decisions.empty()) return seam::platform::UnsavedDecision::Cancel;
    const auto result = decisions.front();
    decisions.erase(decisions.begin());
    return result;
  }

  std::vector<std::string> names;
  std::vector<seam::platform::UnsavedDecision> decisions;
  std::function<void()> onChoose;
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

std::filesystem::path makeInterchangeFixture(
    const std::filesystem::path& root, seam::authoring::InterchangeFormat format) {
  seam::application::ProjectFactory factory{800000U};
  auto project = factory.createProject("External melody");
  const auto track = factory.addVocalTrack(project, "Lead");
  const auto region = factory.addRegion(project, track, "Phrase",
      seam::time::Tick{0}, seam::time::Tick{960});
  auto* target = project.findRegion(region);
  CHECK(target != nullptr);
  auto [lyric, note] = factory.makeNote(seam::time::Tick{0},
      seam::time::Tick{480}, 67U, U"la");
  target->lyrics.push_back(std::move(lyric));
  target->notes.push_back(std::move(note));
  const auto path = root / (format == seam::authoring::InterchangeFormat::Ustx
                               ? "external.ustx" : "external.mid");
  CHECK(seam::authoring::InterchangeService{}.exportFile(project,
      {.format = format, .destination = path, .trackId = track, .regionId = region}));
  return path;
}

struct DocumentObservation final {
  std::string project;
  std::uint64_t revision;
  std::uint64_t nextId;
  seam::authoring::DocumentIdentity identity;
  bool canUndo;
  bool canRedo;
};

DocumentObservation observeDocument(const seam::standalone::AuthoringSession& session) {
  const auto& document = session.runtime().document();
  const auto encoded = seam::formats::ProjectJsonCodec{}.encode(document.session().project());
  CHECK(encoded);
  return {encoded.value(), document.session().revision(), document.factory().nextIdValue(),
          document.identity(), document.session().canUndo(), document.session().canRedo()};
}

void checkDocumentUnchanged(const seam::standalone::AuthoringSession& session,
                            const DocumentObservation& before) {
  const auto after = observeDocument(session);
  CHECK(after.project == before.project);
  CHECK(after.revision == before.revision);
  CHECK(after.nextId == before.nextId);
  CHECK(after.canUndo == before.canUndo);
  CHECK(after.canRedo == before.canRedo);
  CHECK(after.identity.projectPath == before.identity.projectPath);
  CHECK(after.identity.autosavePath == before.identity.autosavePath);
  CHECK(after.identity.recoveryOriginPath == before.identity.recoveryOriginPath);
  CHECK(after.identity.lastSavedRevision == before.identity.lastSavedRevision);
  CHECK(after.identity.baseProjectHash == before.identity.baseProjectHash);
  CHECK(after.identity.dirty == before.identity.dirty);
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

TEST_CASE("standalone records the renderer of a committed export and reports a changed one") {
  const auto root = seam::test::support::temporaryDirectory("standalone-renderer-provenance");
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
  if (!controller) return;

  // Nothing has been rendered, so nothing is recorded and the comparison says unknown rather than
  // claiming the sound already matches this build.
  const auto before = controller.value()->rendererProvenance();
  CHECK(before.state == seam::domain::RendererProvenance::Unknown);
  CHECK(before.recordedRenderAbi.empty());
  CHECK(before.recordedCompilerRevision == 0U);
  CHECK(before.difference.empty());
  CHECK(!before.currentRenderAbi.empty());

  // A committed export names the renderer that produced its audio, so the project records it.
  const auto settings = seam::authoring::ExportSettings{
      .sampleRate = 48000U,
      .channels = 2U,
      .format = seam::voicebank::WavSampleFormat::Pcm24,
      .includeMaster = true,
      .includeStems = false,
      .replaceExisting = false,
  };
  const auto exported = controller.value()->exportSet(root / "export", settings);
  CHECK(exported.hasValue());
  if (!exported) return;
  CHECK(exported.value().state == seam::authoring::ExportState::Committed);

  const auto& recorded =
      session->runtime().document().session().project().settings();
  CHECK(recorded.renderedRenderAbi == std::string{seam::build::kRenderAbiId});
  CHECK(recorded.renderedCompilerRevision ==
        seam::synthesis::kPerformanceCompilerRevision);

  const auto after = controller.value()->rendererProvenance();
  CHECK(after.state == seam::domain::RendererProvenance::Same);
  CHECK(after.difference.empty());

  // A project whose recorded sound came from other code reports the change and names the field, so a
  // creator can tell a renderer change from an accidental edit.
  session->runtime().document().session().project().settings().renderedRenderAbi =
      "seam-render-abi-0.0-r0";
  const auto changed = controller.value()->rendererProvenance();
  CHECK(changed.state == seam::domain::RendererProvenance::Changed);
  CHECK(changed.difference.find("renderAbi seam-render-abi-0.0-r0 -> ") == 0U);

  // Recording provenance is metadata, not music: it must not invalidate the audio it describes.
  const auto revisionBefore = session->runtime().document().session().revision();
  const auto identityBefore = controller.value()->lastExport();
  CHECK(controller.value()->recordExportedRendererProvenance());
  const auto restored = controller.value()->rendererProvenance();
  CHECK(restored.state == seam::domain::RendererProvenance::Same);
  CHECK(session->runtime().document().session().revision() == revisionBefore + 1U);
  const auto identityAfter = controller.value()->lastExport();
  CHECK(identityBefore.has_value() == identityAfter.has_value());
  if (identityBefore && identityAfter) {
    CHECK(identityBefore->masterSha256 == identityAfter->masterSha256);
  }

  // The background path reports a committed render for the owner thread instead of editing the
  // document on the worker, and applying it once is all a frame loop may do.
  session->runtime().document().session().project().settings().renderedRenderAbi.clear();
  session->runtime().document().session().project().settings().renderedCompilerRevision = 0U;
  CHECK(controller.value()->startExportSet(root / "async-export", settings));
  for (std::size_t attempt = 0U;
       attempt < 500U && controller.value()->exportInProgress(); ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  CHECK(!controller.value()->exportInProgress());
  const auto applied = controller.value()->applyPendingRendererProvenance();
  CHECK(applied.hasValue());
  if (!applied) return;
  CHECK(applied.value());
  CHECK(session->runtime().document().session().project().settings().renderedRenderAbi ==
        std::string{seam::build::kRenderAbiId});
  // Applying again is not a second edit: the pending record was consumed when it was applied.
  const auto repeated = controller.value()->applyPendingRendererProvenance();
  CHECK(repeated.hasValue());
  if (repeated) CHECK(!repeated.value());
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

TEST_CASE("standalone_harmony_menu_creates_editable_track_and_recovers_selection_on_undo") {
  const auto root = seam::test::support::temporaryDirectory("standalone-harmony-menu");
  auto session = makeSession(root);
  addNote(*session);
  seam::standalone::StandaloneApplicationControllerConfig config{};
  config.autosaveRoot = root / "autosaves";
  config.recentProjectsPath = root / "recent.json";
  auto controller = seam::standalone::StandaloneApplicationController::create(*session,
      std::make_unique<FakeDialog>(), std::make_unique<FakePrompt>(), config);
  CHECK(controller);
  if (!controller) return;
  const auto leadId = session->runtime().selectedTrack();
  const auto leadRegionId = session->runtime().selectedRegion();
  const auto& originalProject = session->runtime().document().session().project();
  const auto leadNoteId = originalProject.findRegion(leadRegionId)->notes.front().id;
  const auto original = originalProject;

  using seam::platform::HarmonyMenuRequest;
  using seam::platform::HarmonyScale;
  using seam::platform::PerformanceEditScope;
  CHECK(!controller.value()->createHarmonyTrack(HarmonyMenuRequest{
      .scope = PerformanceEditScope::Whole, .scale = HarmonyScale::Major,
      .tonicPitchClass = 0, .offset = 0}));
  CHECK(!controller.value()->createHarmonyTrack(HarmonyMenuRequest{
      .scope = PerformanceEditScope::Whole, .scale = HarmonyScale::NaturalMinor,
      .tonicPitchClass = 0, .offset = 2}));
  CHECK(!controller.value()->createHarmonyTrack(HarmonyMenuRequest{
      .scope = PerformanceEditScope::SelectedNotes, .scale = HarmonyScale::Chromatic,
      .tonicPitchClass = 0, .offset = 3}));
  CHECK(session->runtime().document().session().project() == original);

  session->runtime().document().session().selection().selectOnly(leadNoteId);
  CHECK(controller.value()->createHarmonyTrack(HarmonyMenuRequest{
      .scope = PerformanceEditScope::SelectedNotes, .scale = HarmonyScale::Chromatic,
      .tonicPitchClass = 0, .offset = 3}));
  const auto& withHarmony = session->runtime().document().session().project();
  CHECK(withHarmony.vocalTracks().size() == original.vocalTracks().size() + 1U);
  const auto harmonyId = withHarmony.vocalTracks().back().id;
  CHECK(session->runtime().selectedTrack() == harmonyId);
  CHECK(session->controller().selectedTrack() == harmonyId);
  CHECK(session->runtime().selectedRegion() ==
        withHarmony.vocalTracks().back().regions.front().id);
  CHECK(session->controller().selectedRegion() == session->runtime().selectedRegion());
  CHECK(withHarmony.vocalTracks().back().regions.front().notes.size() == 1U);
  CHECK(withHarmony.vocalTracks().back().regions.front().notes.front().midiKey == 67U);
  CHECK(withHarmony.vocalTracks().back().regions.front().notes.front().id != leadNoteId);
  CHECK(withHarmony.findVocalTrack(leadId) != nullptr);
  CHECK(session->runtime().document().factory().nextIdValue() >
        withHarmony.vocalTracks().back().regions.front().notes.front().id.value());
  CHECK(session->runtime().document().factory().nextIdValue() >
        withHarmony.vocalTracks().back().regions.front().lyrics.front().id.value());

  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::Undo));
  CHECK(session->runtime().document().session().project() == original);
  CHECK(session->runtime().selectedTrack() == leadId);
  CHECK(session->controller().selectedTrack() == leadId);
  CHECK(session->runtime().selectedRegion() == leadRegionId);
  CHECK(session->controller().selectedRegion() == leadRegionId);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::Redo));
  CHECK(session->runtime().document().session().project().findVocalTrack(harmonyId) != nullptr);

  const auto path = root / "harmony.seam";
  CHECK(session->saveProjectAs(path));
  CHECK(session->openProject(path));
  CHECK(session->runtime().document().session().project().findVocalTrack(harmonyId) != nullptr);
  const auto exported = controller.value()->exportSet(root / "harmony-export",
      seam::authoring::ExportSettings{.sampleRate = 48000U, .channels = 2U,
          .format = seam::voicebank::WavSampleFormat::Pcm16,
          .includeMaster = true, .includeStems = true});
  CHECK(exported);
  if (exported) {
    CHECK(exported.value().state == seam::authoring::ExportState::Committed);
    CHECK(exported.value().files.size() == 3U); // master, lead and harmony
    std::vector<std::string> stemHashes;
    for (const auto& file : exported.value().files) {
      CHECK(std::filesystem::exists(exported.value().setPath / file.path));
      CHECK(file.frames > 0U);
      if (file.path.parent_path().filename() == "stems") {
        const auto wav = seam::voicebank::readWav(exported.value().setPath / file.path);
        CHECK(wav);
        CHECK(wav.value().frameCount() == file.frames);
        CHECK(seam::voicebank::analyzeAudio(wav.value().interleaved).rms > 0.00001);
        stemHashes.push_back(file.sha256);
      }
    }
    CHECK(stemHashes.size() == 2U);
    CHECK(stemHashes[0] != stemHashes[1]);
  }
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
  const auto waitFor = [](const auto& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
    while (std::chrono::steady_clock::now() < deadline) {
      if (predicate()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return predicate();
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
  CHECK(waitFor([&] { return static_cast<bool>(session->runtime().renderer().acquireCurrent()); }));
  CHECK(session->runtime().transport().seek(seam::time::SampleFrame{2400}));
  CHECK(waitFor([&] { return session->runtime().transport().state().playhead == 2400; }));
  static_cast<void>(session->characterPerformance());
  const auto originalCharacterGeneration = session->characterPerformanceGeneration();
  const auto beforeComparison = observeDocument(*session);

  // Audition is a render of a project copy. It cannot alter the canonical selection,
  // serialized score, revision, dirty identity or undo/redo state.
  CHECK(controller.value()->beginPerformanceComparison(
      secondId,
      seam::platform::PerformanceEditScope::Whole, {}));
  checkDocumentUnchanged(*session, beforeComparison);
  const auto comparison = controller.value()->performanceComparison();
  CHECK(comparison.has_value());
  CHECK(comparison->takeId == secondId);
  CHECK(comparison->candidateApplied);
  CHECK(!comparison->label.empty());
  CHECK(waitFor([&] { return session->runtime().performanceAuditionReady(); }));
  CHECK(controller.value()->performanceComparison()->auditionReady);
  CHECK(!controller.value()->performanceComparison()->auditionFailed);
  const auto audibleCandidate = session->runtime().audiblePublication();
  CHECK(audibleCandidate.performanceAudition);
  CHECK(audibleCandidate.audio != nullptr);
  CHECK(audibleCandidate.audio->sourceProject != nullptr);
  static_cast<void>(session->characterPerformance());
  CHECK(session->characterPerformanceGeneration() > originalCharacterGeneration);
  const auto candidateCharacterGeneration = session->characterPerformanceGeneration();
  const auto acceptedSecond = audibleCandidate.audio->sourceProject
                                  ->findRegion(regionId)->performance.accepted;
  CHECK(acceptedSecond != acceptedFirst);
  for (const auto& selection : acceptedSecond) {
    CHECK(selection.takeId == secondId);
  }
  CHECK(performance().accepted == acceptedFirst);
  checkDocumentUnchanged(*session, beforeComparison);
  CHECK(waitFor([&] { return session->runtime().transport().state().playhead == 2400; }));

  // A save while the compared audio is playing still writes the canonical take.
  const auto savedPath = root / "saved-during-audition.seam";
  CHECK(session->saveProjectAs(savedPath));
  const auto saved = seam::formats::ProjectJsonCodec{}.load(savedPath);
  CHECK(saved);
  CHECK(saved.value().findRegion(regionId)->performance.accepted == acceptedFirst);
  CHECK(performance().accepted == acceptedFirst);
  const auto afterSave = observeDocument(*session);

  // Swapping restores canonical audio at the same playhead without inserting an undo entry.
  CHECK(controller.value()->swapPerformanceComparison());
  CHECK(performance().accepted == acceptedFirst);
  CHECK(!controller.value()->performanceComparison()->candidateApplied);
  CHECK(!controller.value()->performanceComparison()->auditionReady);
  CHECK(!controller.value()->performanceComparison()->auditionFailed);
  CHECK(!session->runtime().audiblePublication().performanceAudition);
  static_cast<void>(session->characterPerformance());
  CHECK(session->characterPerformanceGeneration() > candidateCharacterGeneration);
  checkDocumentUnchanged(*session, afterSave);
  CHECK(waitFor([&] { return session->runtime().transport().state().playhead == 2400; }));
  CHECK(controller.value()->swapPerformanceComparison());
  CHECK(waitFor([&] { return session->runtime().performanceAuditionReady(); }));
  CHECK(performance().accepted == acceptedFirst);
  CHECK(controller.value()->performanceComparison()->candidateApplied);
  checkDocumentUnchanged(*session, afterSave);
  CHECK(waitFor([&] { return session->runtime().transport().state().playhead == 2400; }));

  // Ending on an audible candidate is the one canonical decision and the one undoable edit.
  CHECK(controller.value()->endPerformanceComparison());
  CHECK(controller.value()->performanceComparison() == std::nullopt);
  CHECK(performance().accepted == acceptedSecond);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::Undo));
  CHECK(performance().accepted == acceptedFirst);
  CHECK(controller.value()->dispatch(seam::platform::ApplicationCommand::Redo));
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

  CHECK(waitFor([&] { return static_cast<bool>(session->runtime().renderer().acquireCurrent()); }));
  const auto beforeCancel = observeDocument(*session);
  CHECK(controller.value()->beginPerformanceComparison(
      firstId,
      seam::platform::PerformanceEditScope::Whole, {}));
  const auto nested = controller.value()->beginPerformanceComparison(
      secondId,
      seam::platform::PerformanceEditScope::Whole, {});
  CHECK(!nested);
  CHECK(nested.error().code == seam::core::ErrorCode::Conflict);
  CHECK(controller.value()->cancelPerformanceComparison());
  checkDocumentUnchanged(*session, beforeCancel);
  CHECK(!session->runtime().performanceAuditionActive());

  CHECK(controller.value()->beginPerformanceComparison(
      firstId, seam::platform::PerformanceEditScope::Whole, {}));
  CHECK(session->runtime().performanceAuditionActive());
  // A newer score edit ends the transient timeline even if its render finishes after the edit.
  // The held decision is stale and cannot later accept the old candidate over that work.
  addNote(*session);
  CHECK(!session->runtime().performanceAuditionActive());
  CHECK(!session->runtime().audiblePublication().performanceAudition);
  CHECK(controller.value()->performanceComparison() == std::nullopt);
  const auto staleEnd = controller.value()->endPerformanceComparison();
  CHECK(!staleEnd);
  CHECK(staleEnd.error().code == seam::core::ErrorCode::Conflict);
  CHECK(!session->runtime().performanceAuditionActive());
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

TEST_CASE("score export suggestion replaces only an existing score extension") {
  using namespace seam;
  const std::vector<std::pair<std::string, std::string>> cases{
      {"Imported.ustx", "Imported.ustx"},
      {"Imported.mid", "Imported.ustx"},
      {"Imported.MIDI", "Imported.ustx"},
      {"Song.v1", "Song.v1.ustx"},
  };
  for (const auto& [name, expected] : cases) {
    const auto root = test::support::temporaryDirectory("score-export-suggestion");
    auto session = makeSession(root);
    auto project = session->runtime().document().session().project();
    project.setName(name);
    CHECK(session->runtime().document().replaceProject(std::move(project)));
    auto dialog = std::make_unique<FakeDialog>();
    auto* dialogPtr = dialog.get();
    dialogPtr->responses = {std::nullopt};
    auto controller = standalone::StandaloneApplicationController::create(
        *session, std::move(dialog), std::make_unique<FakePrompt>(), {
          .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json"});
    CHECK(controller);
    CHECK(controller.value()->dispatch(platform::ApplicationCommand::ExportScore));
    CHECK(dialogPtr->requests.size() == 1U);
    CHECK(dialogPtr->requests.front().purpose == platform::FileDialogPurpose::ExportScore);
    CHECK(dialogPtr->requests.front().suggestedName == expected);
  }
}

TEST_CASE("standalone score export reviews losses before writing and rejects stale approval") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("standalone-export-review");
  auto session = makeSession(root);
  addNote(*session);
  const auto destination = root / "score.mid";
  const auto staleDestination = root / "stale.mid";
  auto dialog = std::make_unique<FakeDialog>();
  dialog->responses = {destination, destination, staleDestination};
  bool approve = false;
  bool mutateDuringReview = false;
  std::size_t reviews = 0U;
  auto controller = standalone::StandaloneApplicationController::create(
      *session, std::move(dialog), std::make_unique<FakePrompt>(), {
        .autosaveRoot = root / "autosaves",
        .recentProjectsPath = root / "recent.json",
        .reviewInterchangeExport = [&](const authoring::InterchangeExportDraft& draft)
            -> core::Result<bool> {
          ++reviews;
          CHECK(!std::filesystem::exists(draft.destination));
          CHECK(!draft.bytes.empty());
          CHECK(draft.contentHash.size() == 64U);
          if (mutateDuringReview) {
            auto current = session->runtime().document().session().project();
            CHECK(session->runtime().document().replaceProject(std::move(current)));
          }
          return approve;
        },
      });
  CHECK(controller);
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::ExportScore));
  CHECK(reviews == 1U);
  CHECK(!std::filesystem::exists(destination));

  approve = true;
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::ExportScore));
  CHECK(reviews == 2U);
  CHECK(std::filesystem::exists(destination));
  CHECK(std::filesystem::file_size(destination) > 14U);

  mutateDuringReview = true;
  const auto stale = controller.value()->dispatch(platform::ApplicationCommand::ExportScore);
  CHECK(!stale);
  CHECK(stale.error().code == core::ErrorCode::Conflict);
  CHECK(reviews == 3U);
  CHECK(!std::filesystem::exists(staleDestination));

  const auto unreviewedDestination = root / "unreviewed.mid";
  auto unreviewedDialog = std::make_unique<FakeDialog>();
  unreviewedDialog->responses = {unreviewedDestination};
  auto withoutReview = standalone::StandaloneApplicationController::create(
      *session, std::move(unreviewedDialog), std::make_unique<FakePrompt>(), {
        .autosaveRoot = root / "autosaves",
        .recentProjectsPath = root / "recent.json",
      });
  CHECK(withoutReview);
  const auto unreviewed = withoutReview.value()->dispatch(
      platform::ApplicationCommand::ExportScore);
  CHECK(!unreviewed);
  CHECK(unreviewed.error().code == core::ErrorCode::Unsupported);
  CHECK(!std::filesystem::exists(unreviewedDestination));
}

TEST_CASE("standalone interchange accepts reviewed USTX and MIDI as new unsaved documents") {
  using namespace seam;
  for (const auto format : {authoring::InterchangeFormat::Ustx, authoring::InterchangeFormat::Smf}) {
    const auto root = test::support::temporaryDirectory("standalone-interchange-accept");
    const auto source = makeInterchangeFixture(root, format);
    const auto sourceHash = core::sha256File(source); CHECK(sourceHash);
    auto session = makeSession(root);
    addNote(*session);
    const auto originalId = session->runtime().document().session().project().id();
    const auto expected = session->prepareInterchangeImport(source,
        {.format = format, .projectName = source.stem().string()}); CHECK(expected);
    const auto before = observeDocument(*session);
    const auto destination = root / "accepted.seam";
    auto dialog = std::make_unique<FakeDialog>();
    auto* dialogPtr = dialog.get();
    dialogPtr->responses = {source, destination};
    auto prompt = std::make_unique<FakePrompt>();
    prompt->decisions = {platform::UnsavedDecision::Discard};
    unsigned reviews = 0U;
    auto controller = standalone::StandaloneApplicationController::create(
        *session, std::move(dialog), std::move(prompt), {
          .autosaveRoot = root / "autosaves",
          .recentProjectsPath = root / "recent.json",
          .reviewInterchangeImport = [&](const authoring::InterchangeImportDraft& draft) -> core::Result<bool> {
            ++reviews;
            checkDocumentUnchanged(*session, before);
            CHECK(draft.format == format);
            CHECK(draft.sourcePath == std::filesystem::weakly_canonical(source));
            CHECK(draft.sourceHash == sourceHash.value());
            CHECK(draft.issues == expected.value().issues);
            CHECK(draft.project.vocalTracks().size() == 1U);
            CHECK(draft.project.vocalTracks().front().regions.front().notes.front().midiKey == 67U);
            return true;
          }});
    CHECK(controller);
    CHECK(controller.value()->dispatch(platform::ApplicationCommand::OpenExternalProject));
    CHECK(reviews == 1U);
    CHECK(dialogPtr->requests.front().purpose == platform::FileDialogPurpose::OpenScore);
    const auto& document = session->runtime().document();
    CHECK(document.session().project().id() != originalId);
    CHECK(document.dirty());
    CHECK(!document.identity().projectPath.has_value());
    CHECK(!document.identity().autosavePath.has_value());
    CHECK(!document.identity().recoveryOriginPath.has_value());
    CHECK(document.identity().baseProjectHash.empty());
    CHECK(!document.session().canUndo());
    CHECK(!document.session().canRedo());
    CHECK(document.session().project().findRegion(session->regionId()) != nullptr);
    CHECK(document.session().project().findRegion(session->regionId())->notes.front().midiKey == 67U);
    CHECK(session->controller().sceneState().dirty);
    CHECK(controller.value()->dispatch(platform::ApplicationCommand::SaveProjectAs));
    CHECK(document.identity().projectPath == destination);
    CHECK(!document.dirty());
    CHECK(std::filesystem::exists(destination));
    CHECK(core::sha256File(source).value() == sourceHash.value());
  }
}

TEST_CASE("standalone interchange review cancellation errors and missing surfaces preserve the document") {
  using namespace seam;
  for (const auto format : {authoring::InterchangeFormat::Ustx, authoring::InterchangeFormat::Smf}) {
    for (const int decision : {0, 1, 2}) {
      const auto root = test::support::temporaryDirectory("standalone-interchange-refusal");
      const auto source = makeInterchangeFixture(root, format);
      const auto sourceHash = core::sha256File(source); CHECK(sourceHash);
      auto session = makeSession(root);
      addNote(*session);
      const auto before = observeDocument(*session);
      auto dialog = std::make_unique<FakeDialog>(); dialog->responses = {source};
      auto prompt = std::make_unique<FakePrompt>();
      prompt->decisions = {platform::UnsavedDecision::Discard};
      standalone::StandaloneApplicationControllerConfig config{};
      config.autosaveRoot = root / "autosaves";
      config.recentProjectsPath = root / "recent.json";
      unsigned reviews = 0U;
      if (decision != 2) config.reviewInterchangeImport = [&](const auto&) -> core::Result<bool> {
        ++reviews;
        if (decision == 1) return core::failure<bool>(core::ErrorCode::IoError, "review failed");
        return false;
      };
      auto controller = standalone::StandaloneApplicationController::create(
          *session, std::move(dialog), std::move(prompt), std::move(config)); CHECK(controller);
      const auto imported = controller.value()->dispatch(platform::ApplicationCommand::OpenExternalProject);
      CHECK(imported.hasValue() == (decision == 0));
      if (decision == 1) CHECK(imported.error().code == core::ErrorCode::IoError);
      if (decision == 2) CHECK(imported.error().code == core::ErrorCode::Unsupported);
      CHECK(reviews == (decision == 2 ? 0U : 1U));
      checkDocumentUnchanged(*session, before);
      CHECK(core::sha256File(source).value() == sourceHash.value());
    }
  }
}

TEST_CASE("standalone interchange honors unsaved cancellation and cancelled save before opening") {
  using namespace seam;
  for (const auto decision : {platform::UnsavedDecision::Cancel, platform::UnsavedDecision::Save}) {
    const auto root = test::support::temporaryDirectory("standalone-interchange-prompt-cancel");
    auto session = makeSession(root); addNote(*session);
    const auto before = observeDocument(*session);
    auto dialog = std::make_unique<FakeDialog>(); auto* dialogPtr = dialog.get();
    dialogPtr->responses = {std::nullopt};
    auto prompt = std::make_unique<FakePrompt>(); prompt->decisions = {decision};
    unsigned reviews = 0U;
    auto controller = standalone::StandaloneApplicationController::create(
        *session, std::move(dialog), std::move(prompt), {
          .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json",
          .reviewInterchangeImport = [&](const auto&) -> core::Result<bool> { ++reviews; return true; }});
    CHECK(controller);
    CHECK(controller.value()->dispatch(platform::ApplicationCommand::OpenExternalProject));
    CHECK(reviews == 0U);
    CHECK(dialogPtr->requests.size() == (decision == platform::UnsavedDecision::Save ? 1U : 0U));
    if (!dialogPtr->requests.empty()) CHECK(dialogPtr->requests.front().purpose == platform::FileDialogPurpose::SaveProject);
    checkDocumentUnchanged(*session, before);
  }
}

TEST_CASE("standalone interchange captures its approval stamp after a successful unsaved save") {
  using namespace seam;
  for (const auto format : {authoring::InterchangeFormat::Ustx, authoring::InterchangeFormat::Smf}) {
    const auto root = test::support::temporaryDirectory("standalone-interchange-save-first");
    const auto source = makeInterchangeFixture(root, format);
    const auto sourceHash = core::sha256File(source); CHECK(sourceHash);
    auto session = makeSession(root); addNote(*session);
    const auto before = observeDocument(*session);
    const auto savedPath = root / "previous.seam";
    auto dialog = std::make_unique<FakeDialog>(); dialog->responses = {savedPath, source};
    auto prompt = std::make_unique<FakePrompt>(); prompt->decisions = {platform::UnsavedDecision::Save};
    auto controller = standalone::StandaloneApplicationController::create(
        *session, std::move(dialog), std::move(prompt), {
          .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json",
          .reviewInterchangeImport = [&](const auto&) -> core::Result<bool> {
            CHECK(session->runtime().document().identity().projectPath == savedPath);
            CHECK(!session->runtime().document().dirty());
            const auto saved = formats::ProjectJsonCodec{}.load(savedPath); CHECK(saved);
            CHECK(formats::ProjectJsonCodec{}.encode(saved.value()).value() == before.project);
            return true;
          }});
    CHECK(controller);
    CHECK(controller.value()->dispatch(platform::ApplicationCommand::OpenExternalProject));
    CHECK(session->runtime().document().dirty());
    CHECK(!session->runtime().document().identity().projectPath.has_value());
    CHECK(core::sha256File(source).value() == sourceHash.value());
  }
}

TEST_CASE("standalone interchange rejects stale review after edits undo replacement save or recovery") {
  using namespace seam;
  for (const auto format : {authoring::InterchangeFormat::Ustx, authoring::InterchangeFormat::Smf}) {
    for (const int mutation : {0, 1, 2, 3, 4}) {
      const auto root = test::support::temporaryDirectory("standalone-interchange-stale-review");
      const auto source = makeInterchangeFixture(root, format);
      const auto sourceHash = core::sha256File(source); CHECK(sourceHash);
      auto session = makeSession(root); addNote(*session);
      const auto originalId = session->runtime().document().session().project().id();
      std::optional<DocumentObservation> newer;
      auto dialog = std::make_unique<FakeDialog>(); dialog->responses = {source};
      auto prompt = std::make_unique<FakePrompt>(); prompt->decisions = {platform::UnsavedDecision::Discard};
      auto controller = standalone::StandaloneApplicationController::create(
          *session, std::move(dialog), std::move(prompt), {
            .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json",
            .reviewInterchangeImport = [&](const auto&) -> core::Result<bool> {
              auto& document = session->runtime().document();
              if (mutation == 0 || mutation == 1) {
                addNote(*session);
                if (mutation == 1) CHECK(document.undo());
              } else if (mutation == 2) {
                auto sameProject = document.session().project();
                CHECK(document.replaceProject(std::move(sameProject)));
                CHECK(document.session().project().id() == originalId);
              } else if (mutation == 3) {
                CHECK(session->saveProjectAs(root / "saved-during-review.seam"));
              } else {
                document.markRecovered(root / "recovered-during-review.seam");
              }
              newer = observeDocument(*session);
              return true;
            }});
      CHECK(controller);
      const auto imported = controller.value()->dispatch(platform::ApplicationCommand::OpenExternalProject);
      CHECK(!imported); CHECK(imported.error().code == core::ErrorCode::Conflict);
      CHECK(newer.has_value()); checkDocumentUnchanged(*session, *newer);
      CHECK(core::sha256File(source).value() == sourceHash.value());
    }
  }
}

TEST_CASE("standalone interchange detects file picker mutations but cancellation preserves newer edits") {
  using namespace seam;
  for (const auto format : {authoring::InterchangeFormat::Ustx, authoring::InterchangeFormat::Smf}) {
    for (const bool cancel : {false, true}) {
      const auto root = test::support::temporaryDirectory("standalone-interchange-stale-picker");
      const auto source = makeInterchangeFixture(root, format);
      const auto sourceHash = core::sha256File(source); CHECK(sourceHash);
      auto session = makeSession(root); addNote(*session);
      std::optional<DocumentObservation> newer;
      auto dialog = std::make_unique<FakeDialog>();
      dialog->responses = {cancel ? std::optional<std::filesystem::path>{} : std::optional{source}};
      dialog->onChoose = [&](const platform::FileDialogRequest& request) {
        CHECK(request.purpose == platform::FileDialogPurpose::OpenScore);
        addNote(*session); newer = observeDocument(*session);
      };
      auto prompt = std::make_unique<FakePrompt>(); prompt->decisions = {platform::UnsavedDecision::Discard};
      unsigned reviews = 0U;
      auto controller = standalone::StandaloneApplicationController::create(
          *session, std::move(dialog), std::move(prompt), {
            .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json",
            .reviewInterchangeImport = [&](const auto&) -> core::Result<bool> { ++reviews; return true; }});
      CHECK(controller);
      const auto imported = controller.value()->dispatch(platform::ApplicationCommand::OpenExternalProject);
      CHECK(imported.hasValue() == cancel);
      if (!cancel) CHECK(imported.error().code == core::ErrorCode::Conflict);
      CHECK(reviews == 0U); CHECK(newer.has_value());
      checkDocumentUnchanged(*session, *newer);
      CHECK(core::sha256File(source).value() == sourceHash.value());
    }
  }
}

TEST_CASE("standalone interchange review cancellation preserves edits made during the modal review") {
  using namespace seam;
  const auto root = test::support::temporaryDirectory("standalone-interchange-cancel-newer");
  const auto source = makeInterchangeFixture(root, authoring::InterchangeFormat::Ustx);
  auto session = makeSession(root); addNote(*session);
  std::optional<DocumentObservation> newer;
  auto dialog = std::make_unique<FakeDialog>(); dialog->responses = {source};
  auto prompt = std::make_unique<FakePrompt>(); prompt->decisions = {platform::UnsavedDecision::Discard};
  auto controller = standalone::StandaloneApplicationController::create(
      *session, std::move(dialog), std::move(prompt), {
        .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json",
        .reviewInterchangeImport = [&](const auto&) -> core::Result<bool> {
          addNote(*session); newer = observeDocument(*session); return false;
        }});
  CHECK(controller);
  CHECK(controller.value()->dispatch(platform::ApplicationCommand::OpenExternalProject));
  CHECK(newer.has_value()); checkDocumentUnchanged(*session, *newer);
}

TEST_CASE("standalone interchange malformed sources never invoke review or replace the document") {
  using namespace seam;
  for (const auto extension : {".ustx", ".mid"}) {
    const auto root = test::support::temporaryDirectory("standalone-interchange-malformed");
    const auto source = root / (std::string{"invalid"} + extension);
    CHECK(core::durableAtomicWriteText(source, "not an interchange document"));
    const auto sourceHash = core::sha256File(source); CHECK(sourceHash);
    auto session = makeSession(root); addNote(*session);
    const auto before = observeDocument(*session);
    auto dialog = std::make_unique<FakeDialog>(); dialog->responses = {source};
    auto prompt = std::make_unique<FakePrompt>(); prompt->decisions = {platform::UnsavedDecision::Discard};
    unsigned reviews = 0U;
    auto controller = standalone::StandaloneApplicationController::create(
        *session, std::move(dialog), std::move(prompt), {
          .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json",
          .reviewInterchangeImport = [&](const auto&) -> core::Result<bool> { ++reviews; return true; }});
    CHECK(controller);
    CHECK(!controller.value()->dispatch(platform::ApplicationCommand::OpenExternalProject));
    CHECK(reviews == 0U); checkDocumentUnchanged(*session, before);
    CHECK(core::sha256File(source).value() == sourceHash.value());
  }
}

TEST_CASE("standalone lifecycle rejects stale unsaved prompt decisions before replacement saving or quit") {
  using namespace seam;
  for (const auto command : {platform::ApplicationCommand::OpenExternalProject,
      platform::ApplicationCommand::NewProject, platform::ApplicationCommand::OpenProject,
      platform::ApplicationCommand::Quit}) {
    for (const auto decision : {platform::UnsavedDecision::Discard, platform::UnsavedDecision::Save}) {
      for (const bool replace : {false, true}) {
        const auto root = test::support::temporaryDirectory("lifecycle-stale-unsaved-prompt");
        auto session = makeSession(root); addNote(*session);
        const auto savedPath = root / "original.seam";
        CHECK(session->saveProjectAs(savedPath));
        const auto savedHash = core::sha256File(savedPath); CHECK(savedHash);
        addNote(*session);
        std::optional<DocumentObservation> newer;
        auto dialog = std::make_unique<FakeDialog>(); auto* dialogPtr = dialog.get();
        auto prompt = std::make_unique<FakePrompt>(); prompt->decisions = {decision};
        prompt->onChoose = [&] {
          if (replace) {
            auto sameProject = session->runtime().document().session().project();
            CHECK(session->runtime().document().replaceProject(std::move(sameProject)));
          } else addNote(*session);
          newer = observeDocument(*session);
        };
        bool quit = false;
        unsigned reviews = 0U;
        auto controller = standalone::StandaloneApplicationController::create(
            *session, std::move(dialog), std::move(prompt), {
              .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json",
              .reviewInterchangeImport = [&](const auto&) -> core::Result<bool> { ++reviews; return true; }},
            [&] { quit = true; }); CHECK(controller);
        const auto result = controller.value()->dispatch(command);
        CHECK(!result); CHECK(result.error().code == core::ErrorCode::Conflict);
        CHECK(!quit); CHECK(reviews == 0U); CHECK(dialogPtr->requests.empty());
        CHECK(newer.has_value()); checkDocumentUnchanged(*session, *newer);
        CHECK(core::sha256File(savedPath).value() == savedHash.value());
      }
    }
  }
}

TEST_CASE("standalone lifecycle unsaved prompt cancellation preserves newer document changes") {
  using namespace seam;
  for (const auto command : {platform::ApplicationCommand::OpenExternalProject,
      platform::ApplicationCommand::NewProject, platform::ApplicationCommand::OpenProject,
      platform::ApplicationCommand::Quit}) {
    const auto root = test::support::temporaryDirectory("lifecycle-cancel-unsaved-newer");
    auto session = makeSession(root); addNote(*session);
    std::optional<DocumentObservation> newer;
    auto dialog = std::make_unique<FakeDialog>(); auto* dialogPtr = dialog.get();
    auto prompt = std::make_unique<FakePrompt>(); prompt->decisions = {platform::UnsavedDecision::Cancel};
    prompt->onChoose = [&] { addNote(*session); newer = observeDocument(*session); };
    bool quit = false;
    auto controller = standalone::StandaloneApplicationController::create(
        *session, std::move(dialog), std::move(prompt), {
          .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json"},
        [&] { quit = true; }); CHECK(controller);
    CHECK(controller.value()->dispatch(command));
    CHECK(!quit); CHECK(dialogPtr->requests.empty()); CHECK(newer.has_value());
    checkDocumentUnchanged(*session, *newer);
  }
}

TEST_CASE("standalone lifecycle rejects stale Save As decisions before writing or replacing") {
  using namespace seam;
  for (const auto command : {platform::ApplicationCommand::SaveProjectAs,
      platform::ApplicationCommand::SaveProject, platform::ApplicationCommand::OpenExternalProject,
      platform::ApplicationCommand::NewProject, platform::ApplicationCommand::OpenProject,
      platform::ApplicationCommand::Quit}) {
    for (const int mutation : {0, 1, 2}) {
      const auto root = test::support::temporaryDirectory("lifecycle-stale-save-as");
      auto session = makeSession(root); addNote(*session);
      const auto destination = root / "must-not-change.seam";
      CHECK(core::durableAtomicWriteText(destination, "existing destination sentinel"));
      const auto destinationHash = core::sha256File(destination); CHECK(destinationHash);
      std::optional<DocumentObservation> newer;
      auto dialog = std::make_unique<FakeDialog>(); auto* dialogPtr = dialog.get();
      dialog->responses = {destination};
      dialog->onChoose = [&](const platform::FileDialogRequest& request) {
        // An implementation missing the guard can continue into the next Open
        // picker; mutate only at the Save As boundary under review here.
        if (request.purpose != platform::FileDialogPurpose::SaveProject) return;
        if (mutation == 0) addNote(*session);
        else if (mutation == 1) {
          auto sameProject = session->runtime().document().session().project();
          CHECK(session->runtime().document().replaceProject(std::move(sameProject)));
        } else CHECK(session->saveProjectAs(root / "saved-by-nested-action.seam"));
        newer = observeDocument(*session);
      };
      auto prompt = std::make_unique<FakePrompt>(); prompt->decisions = {platform::UnsavedDecision::Save};
      bool quit = false;
      auto controller = standalone::StandaloneApplicationController::create(
          *session, std::move(dialog), std::move(prompt), {
            .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json"},
          [&] { quit = true; }); CHECK(controller);
      const auto result = controller.value()->dispatch(command);
      CHECK(!result); CHECK(result.error().code == core::ErrorCode::Conflict);
      CHECK(!quit); CHECK(dialogPtr->requests.size() == 1U); CHECK(newer.has_value());
      checkDocumentUnchanged(*session, *newer);
      CHECK(core::sha256File(destination).value() == destinationHash.value());
      CHECK(!std::filesystem::exists(std::filesystem::path{destination.string() + ".bak"}));
    }
  }
}

TEST_CASE("standalone lifecycle cancelled Save As preserves newer edits and never quits") {
  using namespace seam;
  for (const auto command : {platform::ApplicationCommand::SaveProjectAs,
      platform::ApplicationCommand::OpenExternalProject, platform::ApplicationCommand::Quit}) {
    const auto root = test::support::temporaryDirectory("lifecycle-cancel-save-as-newer");
    auto session = makeSession(root); addNote(*session);
    std::optional<DocumentObservation> newer;
    auto dialog = std::make_unique<FakeDialog>(); dialog->responses = {std::nullopt};
    dialog->onChoose = [&](const platform::FileDialogRequest& request) {
      CHECK(request.purpose == platform::FileDialogPurpose::SaveProject);
      addNote(*session); newer = observeDocument(*session);
    };
    auto prompt = std::make_unique<FakePrompt>(); prompt->decisions = {platform::UnsavedDecision::Save};
    bool quit = false;
    auto controller = standalone::StandaloneApplicationController::create(
        *session, std::move(dialog), std::move(prompt), {
          .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json"},
        [&] { quit = true; }); CHECK(controller);
    CHECK(controller.value()->dispatch(command));
    CHECK(!quit); CHECK(newer.has_value()); checkDocumentUnchanged(*session, *newer);
  }
}

TEST_CASE("standalone lifecycle successful save allows legitimate close after identity changes") {
  using namespace seam;
  for (const bool alreadySaved : {false, true}) {
    const auto root = test::support::temporaryDirectory("lifecycle-save-close-control");
    auto session = makeSession(root); addNote(*session);
    const auto destination = root / "saved-on-close.seam";
    if (alreadySaved) { CHECK(session->saveProjectAs(destination)); addNote(*session); }
    const auto before = observeDocument(*session);
    auto dialog = std::make_unique<FakeDialog>(); auto* dialogPtr = dialog.get();
    dialog->responses = {destination};
    auto prompt = std::make_unique<FakePrompt>(); prompt->decisions = {platform::UnsavedDecision::Save};
    bool quit = false;
    auto controller = standalone::StandaloneApplicationController::create(
        *session, std::move(dialog), std::move(prompt), {
          .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json"},
        [&] { quit = true; }); CHECK(controller);
    CHECK(controller.value()->dispatch(platform::ApplicationCommand::Quit));
    CHECK(quit); CHECK(!session->runtime().document().dirty());
    CHECK(session->runtime().document().identity().projectPath == destination);
    CHECK(session->runtime().document().session().revision() == before.revision);
    CHECK(dialogPtr->requests.size() == (alreadySaved ? 0U : 1U));
    const auto saved = formats::ProjectJsonCodec{}.load(destination); CHECK(saved);
    CHECK(formats::ProjectJsonCodec{}.encode(saved.value()).value() == before.project);
  }
}

TEST_CASE("standalone lifecycle New and Open dialogs cannot consume earlier discard approval after edits") {
  using namespace seam;
  for (const auto command : {platform::ApplicationCommand::NewProject,
                            platform::ApplicationCommand::OpenProject}) {
    for (const bool cancel : {false, true}) {
      const auto root = test::support::temporaryDirectory("lifecycle-stale-following-dialog");
      auto session = makeSession(root); addNote(*session);
      const auto source = root / "other.seam";
      CHECK(formats::ProjectJsonCodec{}.save(session->runtime().document().session().project(), source));
      const auto sourceHash = core::sha256File(source); CHECK(sourceHash);
      std::optional<DocumentObservation> newer;
      auto dialog = std::make_unique<FakeDialog>();
      dialog->responses = {cancel ? std::optional<std::filesystem::path>{} : std::optional{source}};
      dialog->onChoose = [&](const auto&) { addNote(*session); newer = observeDocument(*session); };
      auto prompt = std::make_unique<FakePrompt>(); prompt->decisions = {platform::UnsavedDecision::Discard};
      auto controller = standalone::StandaloneApplicationController::create(
          *session, std::move(dialog), std::move(prompt), {
            .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json",
            .requestNewProject = [&]() -> core::Result<std::optional<authoring::NewProjectRequest>> {
              addNote(*session); newer = observeDocument(*session);
              if (cancel) return std::optional<authoring::NewProjectRequest>{};
              return std::optional{authoring::NewProjectRequest{.name = "Replacement"}};
            }}); CHECK(controller);
      const auto result = controller.value()->dispatch(command);
      CHECK(result.hasValue() == cancel);
      if (!cancel) CHECK(result.error().code == core::ErrorCode::Conflict);
      CHECK(newer.has_value()); checkDocumentUnchanged(*session, *newer);
      CHECK(core::sha256File(source).value() == sourceHash.value());
    }
  }
}

TEST_CASE("standalone lifecycle save notifications cannot turn newer edits into permission to quit") {
  using namespace seam;
  for (const bool alreadySaved : {false, true}) {
    const auto root = test::support::temporaryDirectory("lifecycle-stale-save-notification");
    auto session = makeSession(root); addNote(*session);
    const auto destination = root / "approved-save.seam";
    if (alreadySaved) { CHECK(session->saveProjectAs(destination)); addNote(*session); }
    const auto approved = observeDocument(*session);
    std::optional<DocumentObservation> newer;
    auto dialog = std::make_unique<FakeDialog>(); dialog->responses = {destination};
    auto prompt = std::make_unique<FakePrompt>(); prompt->decisions = {platform::UnsavedDecision::Save};
    bool quit = false;
    bool mutateOnNotification = false;
    auto controller = standalone::StandaloneApplicationController::create(
        *session, std::move(dialog), std::move(prompt), {
          .autosaveRoot = root / "autosaves", .recentProjectsPath = root / "recent.json",
          .stateChanged = [&] {
            if (!mutateOnNotification) return;
            mutateOnNotification = false;
            addNote(*session); newer = observeDocument(*session);
          }}, [&] { quit = true; }); CHECK(controller);
    mutateOnNotification = true;
    const auto result = controller.value()->dispatch(platform::ApplicationCommand::Quit);
    CHECK(!result); CHECK(result.error().code == core::ErrorCode::Conflict);
    CHECK(!quit); CHECK(newer.has_value()); checkDocumentUnchanged(*session, *newer);
    // The approved snapshot really was saved; only the subsequent stale close
    // is rejected. The newer edit is retained dirty and never silently saved.
    CHECK(session->runtime().document().dirty());
    const auto saved = formats::ProjectJsonCodec{}.load(destination); CHECK(saved);
    CHECK(formats::ProjectJsonCodec{}.encode(saved.value()).value() == approved.project);
  }
}
