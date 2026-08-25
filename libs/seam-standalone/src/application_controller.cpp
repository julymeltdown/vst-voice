#include "seam/standalone/application_controller.hpp"

#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/native_ui/export_dialog.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace seam::standalone {
namespace {

std::filesystem::path initialDirectory(
    const authoring::ProjectDocument& document) {
  if (document.identity().projectPath.has_value()) {
    return document.identity().projectPath->parent_path();
  }
  return {};
}

std::string errorDescription(const core::Error& error) {
  if (error.context.empty()) return error.message;
  return error.message + " (" + error.context + ")";
}

struct DocumentationSpec final {
  std::string_view id;
  std::string_view title;
  std::string_view relativePath;
};

constexpr std::array kDocumentation{
    DocumentationSpec{"eula", "EULA", "EULA.md"},
    DocumentationSpec{"privacy", "Privacy Notice", "PRIVACY.md"},
    DocumentationSpec{"quick-start", "Quick Start", "QUICK_START.md"},
    DocumentationSpec{"manual", "User Manual", "USER_MANUAL.md"},
    DocumentationSpec{"limitations", "Known Limitations", "KNOWN_LIMITATIONS.md"},
    DocumentationSpec{"update", "Update and Rollback", "UPDATE_AND_ROLLBACK.md"},
    DocumentationSpec{"checklist", "Beta Tester Checklist", "BETA_TESTER_CHECKLIST.md"},
    DocumentationSpec{"support", "Support", "Support/SUPPORT.md"},
    DocumentationSpec{"security", "Security Response", "Support/SECURITY_RESPONSE.md"},
};

}  // namespace

core::Result<std::unique_ptr<StandaloneApplicationController>>
StandaloneApplicationController::create(
    AuthoringSession& session,
    std::unique_ptr<platform::IFileDialog> fileDialog,
    std::unique_ptr<platform::IUnsavedChangesPrompt> unsavedPrompt,
    StandaloneApplicationControllerConfig config,
    std::function<void()> requestQuit) {
  if (fileDialog == nullptr || unsavedPrompt == nullptr) {
    return core::failure<std::unique_ptr<StandaloneApplicationController>>(
        core::ErrorCode::InvalidArgument,
        "Standalone application services cannot be null");
  }
  if (config.autosaveRoot.empty() || config.recentProjectsPath.empty()) {
    return core::failure<std::unique_ptr<StandaloneApplicationController>>(
        core::ErrorCode::InvalidArgument,
        "Standalone project-lifecycle paths cannot be empty");
  }
  auto result = std::unique_ptr<StandaloneApplicationController>{
      new StandaloneApplicationController(
          session, std::move(fileDialog), std::move(unsavedPrompt),
          std::move(config), std::move(requestQuit))};
  auto initialized = result->initialize();
  if (!initialized) {
    return core::Result<std::unique_ptr<StandaloneApplicationController>>{
        initialized.error()};
  }
  return result;
}

StandaloneApplicationController::StandaloneApplicationController(
    AuthoringSession& session,
    std::unique_ptr<platform::IFileDialog> fileDialog,
    std::unique_ptr<platform::IUnsavedChangesPrompt> unsavedPrompt,
    StandaloneApplicationControllerConfig config,
    std::function<void()> requestQuit)
    : session_(session),
      fileDialog_(std::move(fileDialog)),
      unsavedPrompt_(std::move(unsavedPrompt)),
      config_(std::move(config)),
      requestQuit_(std::move(requestQuit)),
      autosave_(authoring::AutosaveConfig{
          .root = config_.autosaveRoot,
          .interval = std::chrono::seconds{60},
          .commandThreshold = 25U,
          .minimumCommandDelay = std::chrono::seconds{15},
          .maximumGenerations = 5U,
          .faultInjector = {},
          .wallClock = {},
      }),
      recentProjects_(config_.recentProjectsPath),
      voicebankBrowser_(config_.allowDevelopmentVoicebanks) {
  if (!config_.voicebankInstallRoot.empty()) {
    voicebankInstaller_ = std::make_unique<authoring::VoicebankInstallerService>(
        session_.runtime().voicebanks(), config_.voicebankInstallRoot,
        config_.developmentTrustRoot);
  }
}

StandaloneApplicationController::~StandaloneApplicationController() {
  cancelExport();
  if (exportWorker_.joinable()) exportWorker_.join();
  static_cast<void>(autosave_.flush());
  autosave_.shutdown();
}

core::Result<void> StandaloneApplicationController::initialize() {
  auto recent = recentProjects_.load();
  if (!recent) return recent;
  return refreshVoicebankBrowser();
}

core::Result<void> StandaloneApplicationController::refreshVoicebankBrowser() {
  auto refreshed = session_.runtime().voicebanks().refresh();
  if (!refreshed) return refreshed;
  const auto candidates = session_.runtime().voicebanks().candidates();
  voicebankBrowser_.rebuild(candidates);
  return core::success();
}

std::optional<voicebank::VoicebankCandidate>
StandaloneApplicationController::findCandidate(
    std::string_view id, std::string_view version,
    std::string_view contentHash) const {
  const auto candidates = session_.runtime().voicebanks().candidates();
  const auto iterator = std::find_if(
      candidates.begin(), candidates.end(),
      [id, version, contentHash](const auto& candidate) {
        return candidate.manifest.id == id &&
               candidate.manifest.version == version &&
               candidate.contentHash == contentHash;
      });
  if (iterator == candidates.end()) return std::nullopt;
  return *iterator;
}

authoring::NewProjectRequest
StandaloneApplicationController::defaultNewProject() const {
  return config_.defaultNewProject;
}

core::Result<bool>
StandaloneApplicationController::confirmDestructiveAction() {
  auto& document = session_.runtime().document();
  if (!document.dirty()) return true;
  auto decision = unsavedPrompt_->choose(
      document.session().project().name());
  if (!decision) return core::Result<bool>{decision.error()};
  if (decision.value() == platform::UnsavedDecision::Cancel) return false;
  if (decision.value() == platform::UnsavedDecision::Discard) return true;

  if (!document.identity().projectPath.has_value()) {
    return chooseAndSaveAs();
  }
  auto saved = session_.saveProject();
  if (!saved) return core::Result<bool>{saved.error()};
  auto recorded = recordCurrentProject();
  if (!recorded) return core::Result<bool>{recorded.error()};
  return true;
}

core::Result<bool> StandaloneApplicationController::chooseAndSaveAs() {
  const auto& document = session_.runtime().document();
  const auto selected = fileDialog_->choose(platform::FileDialogRequest{
      .purpose = platform::FileDialogPurpose::SaveProject,
      .title = "Save Project",
      .initialDirectory = initialDirectory(document),
      .suggestedName = document.session().project().name() + ".seam",
      .extensions = {"seam"},
  });
  if (!selected) return core::Result<bool>{selected.error()};
  if (!selected.value().has_value()) return false;
  auto saved = session_.saveProjectAs(*selected.value());
  if (!saved) return core::Result<bool>{saved.error()};
  auto recorded = recordCurrentProject();
  if (!recorded) return core::Result<bool>{recorded.error()};
  return true;
}

core::Result<void> StandaloneApplicationController::recordCurrentProject() {
  const auto& document = session_.runtime().document();
  if (!document.identity().projectPath.has_value()) return core::success();
  auto recorded = recentProjects_.record(
      *document.identity().projectPath,
      document.session().project().name());
  if (!recorded) return recorded;
  auto saved = recentProjects_.save();
  if (saved) notifyStateChanged();
  return saved;
}

core::Result<void> StandaloneApplicationController::openPath(
    const std::filesystem::path& path) {
  auto opened = session_.openProject(path);
  if (!opened) return core::Result<void>{opened.error()};
  auto recorded = recordCurrentProject();
  if (recorded) notifyStateChanged();
  return recorded;
}

core::Result<void> StandaloneApplicationController::openRecent(
    const std::filesystem::path& path) {
  const auto& document = session_.runtime().document();
  const auto& project = document.session().project();
  const auto provisionalUntitled =
      !document.identity().projectPath.has_value() &&
      project.name() == "Untitled" && document.session().revision() <= 1U &&
      project.audioTracks().empty() && project.vocalTracks().size() == 1U &&
      project.vocalTracks().front().regions.size() == 1U &&
      project.vocalTracks().front().regions.front().notes.empty();
  auto allowed = provisionalUntitled
                     ? core::Result<bool>{true}
                     : confirmDestructiveAction();
  if (!allowed) return core::Result<void>{allowed.error()};
  if (!allowed.value()) return core::success();
  return openPath(path);
}

core::Result<void> StandaloneApplicationController::dispatch(
    platform::ApplicationCommand command) {
  switch (command) {
    case platform::ApplicationCommand::NewProject: {
      auto allowed = confirmDestructiveAction();
      if (!allowed) return core::Result<void>{allowed.error()};
      if (!allowed.value()) return core::success();
      auto request = defaultNewProject();
      if (config_.requestNewProject) {
        auto chosen = config_.requestNewProject();
        if (!chosen) return core::Result<void>{chosen.error()};
        if (!chosen.value().has_value()) return core::success();
        request = std::move(*chosen.value());
      }
      return createNewProject(std::move(request));
    }
    case platform::ApplicationCommand::OpenProject: {
      auto allowed = confirmDestructiveAction();
      if (!allowed) return core::Result<void>{allowed.error()};
      if (!allowed.value()) return core::success();
      const auto selected = fileDialog_->choose(platform::FileDialogRequest{
          .purpose = platform::FileDialogPurpose::OpenProject,
          .title = "Open Project",
          .initialDirectory = initialDirectory(session_.runtime().document()),
          .suggestedName = {},
          .extensions = {"seam"},
      });
      if (!selected) return core::Result<void>{selected.error()};
      if (!selected.value().has_value()) return core::success();
      return openPath(*selected.value());
    }
    case platform::ApplicationCommand::RecoverLatestAutosave: {
      auto candidates = recoveryCandidates();
      if (!candidates) return core::Result<void>{candidates.error()};
      const auto iterator = std::find_if(
          candidates.value().rbegin(), candidates.value().rend(),
          [](const authoring::RecoveryCandidate& value) {
            return value.recoverable;
          });
      if (iterator == candidates.value().rend()) {
        return core::failure(core::ErrorCode::NotFound,
                             "No recoverable autosave is available");
      }
      return recover(*iterator);
    }
    case platform::ApplicationCommand::SaveProject: {
      if (!session_.runtime().document().identity().projectPath.has_value()) {
        auto saved = chooseAndSaveAs();
        if (!saved) return core::Result<void>{saved.error()};
        return core::success();
      }
      auto saved = session_.saveProject();
      if (!saved) return saved;
      return recordCurrentProject();
    }
    case platform::ApplicationCommand::SaveProjectAs: {
      auto saved = chooseAndSaveAs();
      if (!saved) return core::Result<void>{saved.error()};
      return core::success();
    }
    case platform::ApplicationCommand::ImportAudio: {
      const auto selected = fileDialog_->choose(platform::FileDialogRequest{
          .purpose = platform::FileDialogPurpose::ImportAudio,
          .title = "Import Backing Audio",
          .initialDirectory = initialDirectory(session_.runtime().document()),
          .suggestedName = {},
          .extensions = {"wav"},
      });
      if (!selected) return core::Result<void>{selected.error()};
      if (!selected.value().has_value()) return core::success();
      const auto mode = session_.runtime().document().identity().projectPath
                            .has_value()
                        ? authoring::MediaImportMode::Copy
                        : authoring::MediaImportMode::Reference;
      auto imported = session_.importBackingMedia(
          *selected.value(), mode, selected.value()->stem().string(),
          time::Tick{0});
      if (!imported) return core::Result<void>{imported.error()};
      notifyStateChanged();
      return core::success();
    }
    case platform::ApplicationCommand::InstallVoicebank: {
      if (voicebankInstaller_ == nullptr) {
        return core::failure(core::ErrorCode::InvalidState,
                             "Voicebank installation root is not configured");
      }
      const auto selected = fileDialog_->choose(platform::FileDialogRequest{
          .purpose = platform::FileDialogPurpose::InstallVoicebank,
          .title = "Install Voicebank",
          .initialDirectory = {},
          .suggestedName = {},
          .extensions = {"seambank"},
      });
      if (!selected) return core::Result<void>{selected.error()};
      if (!selected.value().has_value()) return core::success();
      auto installed = installVoicebank(*selected.value());
      if (!installed) return core::Result<void>{installed.error()};
      return core::success();
    }
    case platform::ApplicationCommand::RelinkVoicebank:
      return relinkVoicebankFromDialog();
    case platform::ApplicationCommand::RelinkBackingAudio:
      return relinkBackingMediaFromDialog();
    case platform::ApplicationCommand::OpenAudioSettings:
      if (!config_.openAudioSettings) {
        return core::failure(core::ErrorCode::Unsupported,
                             "Audio settings surface is not connected");
      }
      return config_.openAudioSettings();
    case platform::ApplicationCommand::ExportAudio:
      return exportAudio();
    case platform::ApplicationCommand::ExportSet:
      return exportSetFromDialog();
    case platform::ApplicationCommand::Quit: {
      auto close = requestClose();
      if (!close) return core::Result<void>{close.error()};
      return core::success();
    }
    case platform::ApplicationCommand::Undo: {
      auto result = session_.runtime().undo();
      if (result) {
        static_cast<void>(onDocumentChanged());
        notifyStateChanged();
      }
      return result;
    }
    case platform::ApplicationCommand::Redo: {
      auto result = session_.runtime().redo();
      if (result) {
        static_cast<void>(onDocumentChanged());
        notifyStateChanged();
      }
      return result;
    }
    case platform::ApplicationCommand::TogglePlayback: {
      const auto state = session_.runtime().transport().state();
      return state.playing ? session_.runtime().transport().pause()
                           : session_.runtime().transport().play();
    }
    case platform::ApplicationCommand::StopPlayback:
      return session_.runtime().transport().stop();
    case platform::ApplicationCommand::ToggleLoop: {
      const auto state = session_.runtime().transport().state();
      if (state.loop.enabled) {
        return session_.runtime().transport().setLoop(
            rendering::PlaybackLoop{.enabled = false});
      }
      if (state.timelineEnd <= time::SampleFrame{0}) {
        return core::failure(core::ErrorCode::Conflict,
                             "Loop requires a published audio timeline");
      }
      return session_.runtime().transport().setLoop(
          rendering::PlaybackLoop{.enabled = true,
                                  .startFrame = 0,
                                  .endFrame = state.timelineEnd});
    }
  }
  return core::failure(core::ErrorCode::Unsupported,
                       "Unknown application command");
}

core::Result<void> StandaloneApplicationController::createNewProject(
    authoring::NewProjectRequest request) {
  auto created = session_.createNewProject(std::move(request));
  if (created) notifyStateChanged();
  return created;
}

core::Result<StandaloneApplicationController::ExportRequest>
StandaloneApplicationController::makeExportRequest(
    const std::filesystem::path& destination,
    authoring::ExportSettings settings) const {
  auto project = session_.runtime().document().session().project();
  if (session_.runtime().document().identity().projectPath.has_value()) {
    const auto projectRoot = session_.runtime().document().identity().projectPath
                                 ->parent_path();
    for (auto& track : project.audioTracks()) {
      if (track.mediaOwnership == domain::MediaOwnership::ProjectCopy &&
          !track.mediaPath.empty() &&
          std::filesystem::path{track.mediaPath}.is_relative()) {
        track.mediaPath = (projectRoot / std::filesystem::path{track.mediaPath})
                              .lexically_normal()
                              .string();
      }
    }
  }
  const auto states = session_.runtime().voicebanks().resolveAll(project);
  std::vector<rendering::TrackVoicebankSource> sources;
  sources.reserve(states.size());
  for (const auto& state : states) {
    if (!state.resolution.resolved()) continue;
    sources.push_back(rendering::TrackVoicebankSource{
        .trackId = state.trackId,
        .manifest = state.resolution.candidate->manifest,
        .bankRoot = state.resolution.candidate->bankRoot,
        .contentHash = state.resolution.candidate->contentHash,
        .trust = state.resolution.candidate->trust,
    });
  }
  return ExportRequest{
      .project = std::move(project),
      .voicebanks = std::move(sources),
      .activeTrack = session_.runtime().selectedTrack(),
      .activeRegion = session_.runtime().selectedRegion(),
      .revision = session_.runtime().document().session().revision(),
      .destination = destination,
      .settings = settings,
  };
}

core::Result<authoring::ExportResult>
StandaloneApplicationController::runExport(ExportRequest request,
                                            std::stop_token stopToken) {
  auto exported = exportService_.exportSet(
      request.project, request.voicebanks, request.activeTrack,
      request.activeRegion, request.revision, request.destination,
      request.settings,
      [this](const authoring::ExportProgress& progress) {
        if (exportProgress_.cancelRequested()) {
          exportStopSource_.request_stop();
        }
        exportProgress_.update(progress);
        notifyProgressChanged();
      },
      stopToken);
  if (!exported) {
    exportProgress_.update(authoring::ExportProgress{
        .state = authoring::ExportState::Failed,
        .currentOutput = errorDescription(exported.error()),
        .completedFiles = 0U,
        .totalFiles = 0U,
    });
    notifyProgressChanged();
  }
  return exported;
}

core::Result<authoring::ExportResult>
StandaloneApplicationController::exportSet(
    const std::filesystem::path& destination,
    authoring::ExportSettings settings) {
  auto request = makeExportRequest(destination, settings);
  if (!request) {
    {
      std::lock_guard lock(exportMutex_);
      lastExport_.reset();
    }
    exportProgress_.reset();
    exportProgress_.update(authoring::ExportProgress{
        .state = authoring::ExportState::Failed,
        .currentOutput = errorDescription(request.error()),
    });
    notifyProgressChanged();
    return core::Result<authoring::ExportResult>{request.error()};
  }
  {
    std::lock_guard lock(exportMutex_);
    if (exportRunning_) {
      return core::failure<authoring::ExportResult>(
          core::ErrorCode::Conflict, "Another export is already in progress");
    }
    exportRunning_ = true;
    lastExport_.reset();
    exportStopSource_ = std::stop_source{};
  }
  exportProgress_.reset();
  auto exported = runExport(std::move(request.value()),
                            exportStopSource_.get_token());
  {
    std::lock_guard lock(exportMutex_);
    if (exported) lastExport_ = exported.value();
    exportRunning_ = false;
  }
  notifyProgressChanged();
  notifyStateChanged();
  return exported;
}

core::Result<void> StandaloneApplicationController::startExportSet(
    const std::filesystem::path& destination,
    authoring::ExportSettings settings) {
  auto request = makeExportRequest(destination, settings);
  if (!request) {
    {
      std::lock_guard lock(exportMutex_);
      lastExport_.reset();
    }
    exportProgress_.reset();
    exportProgress_.update(authoring::ExportProgress{
        .state = authoring::ExportState::Failed,
        .currentOutput = errorDescription(request.error()),
    });
    notifyProgressChanged();
    return core::Result<void>{request.error()};
  }
  {
    std::lock_guard lock(exportMutex_);
    if (exportRunning_) {
      return core::failure(core::ErrorCode::Conflict,
                           "Another export is already in progress");
    }
    exportRunning_ = true;
    lastExport_.reset();
    exportStopSource_ = std::stop_source{};
  }
  exportProgress_.reset();
  const auto stopToken = exportStopSource_.get_token();
  exportWorker_ = std::jthread(
      [this, request = std::move(request.value()), stopToken](
          std::stop_token workerToken) mutable {
        std::stop_callback forwardStop(workerToken, [this] {
          exportStopSource_.request_stop();
        });
        auto exported = runExport(std::move(request), stopToken);
        {
          std::lock_guard lock(exportMutex_);
          if (exported) lastExport_ = exported.value();
          exportRunning_ = false;
        }
        notifyProgressChanged();
      });
  notifyProgressChanged();
  return core::success();
}

bool StandaloneApplicationController::exportInProgress() const noexcept {
  std::lock_guard lock(exportMutex_);
  return exportRunning_;
}

void StandaloneApplicationController::cancelExport() noexcept {
  exportProgress_.requestCancel();
  {
    std::lock_guard lock(exportMutex_);
    exportWorker_.request_stop();
  }
  exportStopSource_.request_stop();
}

core::Result<void> StandaloneApplicationController::exportSetFromDialog() {
  const auto& document = session_.runtime().document();
  const auto selected = fileDialog_->choose(platform::FileDialogRequest{
      .purpose = platform::FileDialogPurpose::ExportSet,
      .title = "Choose Export Set Destination",
      .initialDirectory = initialDirectory(document),
      .suggestedName = document.session().project().name() + " Export",
      .extensions = {},
  });
  if (!selected) return core::Result<void>{selected.error()};
  if (!selected.value().has_value()) return core::success();

  const auto& project = document.session().project();
  authoring::ExportSettings settings{
      .sampleRate = session_.runtime().transport().sampleRate(),
      .channels = project.routing().deviceOutputChannels,
      .format = voicebank::WavSampleFormat::Pcm24,
      .includeMaster = true,
      .includeStems = !project.vocalTracks().empty() ||
                      !project.audioTracks().empty(),
      .replaceExisting = false,
  };
  native_ui::ExportDialogModel dialog;
  dialog.setDestination(*selected.value());
  dialog.setSettings(settings);
  if (!dialog.preflight(project)) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         dialog.issues().empty()
                             ? "Export preflight rejected the project"
                             : dialog.issues().front().message);
  }
  return startExportSet(*selected.value(), settings);
}

core::Result<void> StandaloneApplicationController::exportAudio() {
  const auto& document = session_.runtime().document();
  const auto selected = fileDialog_->choose(platform::FileDialogRequest{
      .purpose = platform::FileDialogPurpose::ExportAudio,
      .title = "Export Audio",
      .initialDirectory = initialDirectory(document),
      .suggestedName = document.session().project().name() + ".wav",
      .extensions = {"wav"},
  });
  if (!selected) return core::Result<void>{selected.error()};
  if (!selected.value().has_value()) return core::success();

  {
    std::lock_guard lock(exportMutex_);
    lastExport_.reset();
  }
  exportProgress_.reset();
  exportProgress_.update(authoring::ExportProgress{
      .state = authoring::ExportState::Preflight,
      .currentOutput = selected.value()->string(),
      .completedFiles = 0U,
      .totalFiles = 1U,
  });
  notifyProgressChanged();

  auto project = document.session().project();
  if (document.identity().projectPath.has_value()) {
    const auto projectRoot = document.identity().projectPath->parent_path();
    for (auto& track : project.audioTracks()) {
      if (track.mediaOwnership == domain::MediaOwnership::ProjectCopy &&
          !track.mediaPath.empty() &&
          std::filesystem::path{track.mediaPath}.is_relative()) {
        track.mediaPath =
            (projectRoot / std::filesystem::path{track.mediaPath})
                .lexically_normal()
                .string();
      }
    }
  }
  const auto states = session_.runtime().voicebanks().resolveAll(project);
  std::vector<rendering::TrackVoicebankSource> sources;
  sources.reserve(states.size());
  for (const auto& state : states) {
    if (!state.resolution.resolved()) continue;
    sources.push_back(rendering::TrackVoicebankSource{
        .trackId = state.trackId,
        .manifest = state.resolution.candidate->manifest,
        .bankRoot = state.resolution.candidate->bankRoot,
        .contentHash = state.resolution.candidate->contentHash,
        .trust = state.resolution.candidate->trust,
    });
  }
  const auto exported = exportService_.exportProject(
      project, sources, session_.runtime().selectedTrack(),
      session_.runtime().selectedRegion(), document.session().revision(),
      *selected.value());
  if (!exported) {
    exportProgress_.update(authoring::ExportProgress{
        .state = authoring::ExportState::Failed,
        .currentOutput = errorDescription(exported.error()),
        .completedFiles = 0U,
        .totalFiles = 1U,
    });
    notifyProgressChanged();
    return core::Result<void>{exported.error()};
  }
  if (exported.value().state != authoring::ExportState::Committed) {
    exportProgress_.update(authoring::ExportProgress{
        .state = exported.value().state,
        .currentOutput = exported.value().diagnostic.empty()
                             ? selected.value()->string()
                             : exported.value().diagnostic,
        .completedFiles = 0U,
        .totalFiles = 1U,
    });
    notifyProgressChanged();
    return core::failure(core::ErrorCode::Conflict,
                         exported.value().diagnostic.empty()
                             ? "Audio export did not commit"
                             : exported.value().diagnostic);
  }
  {
    std::lock_guard lock(exportMutex_);
    lastExport_ = exported.value();
  }
  exportProgress_.update(authoring::ExportProgress{
      .state = authoring::ExportState::Committed,
      .currentOutput = exported.value().masterPath.string(),
      .completedFiles = 1U,
      .totalFiles = 1U,
  });
  notifyProgressChanged();
  notifyStateChanged();
  return core::success();
}

std::vector<platform::VoicebankMenuItem>
StandaloneApplicationController::voicebanks() const {
  std::vector<platform::VoicebankMenuItem> result;
  const auto& cards = voicebankBrowser_.cards();
  result.reserve(cards.size());
  const auto* track = session_.runtime().document().session().project()
                          .findVocalTrack(session_.runtime().selectedTrack());
  for (const auto& card : cards) {
    const bool selected = track != nullptr && track->voicebank.id == card.id &&
                          track->voicebank.version == card.version &&
                          track->voicebank.contentHash == card.contentHash;
    result.push_back(platform::VoicebankMenuItem{
        .id = card.id,
        .version = card.version,
        .contentHash = card.contentHash,
        .displayName = card.displayName,
        .trustLabel = card.trustLabel,
        .selectable = card.selectable,
        .selected = selected,
    });
  }
  return result;
}

core::Result<void> StandaloneApplicationController::selectVoicebank(
    std::string_view id, std::string_view version,
    std::string_view contentHash) {
  auto selected = session_.runtime().voicebanks().selectTrackExact(
      session_.runtime().document(), session_.runtime().selectedTrack(), id,
      version, contentHash);
  if (!selected) return selected;
  session_.runtime().handleDocumentChanged();
  static_cast<void>(onDocumentChanged());
  notifyStateChanged();
  return core::success();
}

core::Result<authoring::VoicebankInstallResult>
StandaloneApplicationController::installVoicebank(
    const std::filesystem::path& packagePath,
    authoring::ExistingVoicebankDecision decision) {
  if (voicebankInstaller_ == nullptr) {
    return core::failure<authoring::VoicebankInstallResult>(
        core::ErrorCode::InvalidState,
        "Voicebank installation root is not configured");
  }
  auto result = voicebankInstaller_->install(authoring::VoicebankInstallRequest{
      .packagePath = packagePath,
      .trustedPublicKeys = config_.trustedVoicebankKeys,
      .useDevelopmentTrustRoot = config_.developmentTrustRoot.has_value(),
      .existingDecision = decision,
  });
  if (!result) return result;
  auto refreshed = refreshVoicebankBrowser();
  if (!refreshed) {
    return core::Result<authoring::VoicebankInstallResult>{refreshed.error()};
  }
  notifyStateChanged();
  return result;
}

core::Result<voicebank::VoicebankResolution>
StandaloneApplicationController::relinkVoicebank(
    domain::TrackId trackId, voicebank::VoicebankSearchRoot root) {
  auto result = session_.runtime().voicebanks().relinkTrack(
      session_.runtime().document().session().project(), trackId,
      std::move(root));
  if (!result) return result;
  auto refreshed = refreshVoicebankBrowser();
  if (!refreshed) {
    return core::Result<voicebank::VoicebankResolution>{refreshed.error()};
  }
  session_.runtime().handleDocumentChanged();
  static_cast<void>(onDocumentChanged());
  notifyStateChanged();
  return result;
}

core::Result<void> StandaloneApplicationController::relinkVoicebankFromDialog() {
  const auto& project = session_.runtime().document().session().project();
  const auto* track = project.findVocalTrack(session_.runtime().selectedTrack());
  if (track == nullptr) {
    return core::failure(core::ErrorCode::Conflict,
                         "Voicebank relink requires a selected vocal track");
  }
  const auto selected = fileDialog_->choose(platform::FileDialogRequest{
      .purpose = platform::FileDialogPurpose::RelinkVoicebank,
      .title = "Relink Voicebank Search Folder",
      .initialDirectory = {},
      .suggestedName = {},
      .extensions = {},
  });
  if (!selected) return core::Result<void>{selected.error()};
  if (!selected.value().has_value()) return core::success();
  const auto relinked = relinkVoicebank(
      track->id,
      voicebank::VoicebankSearchRoot{
          .path = *selected.value(),
          .kind = config_.allowDevelopmentVoicebanks
                      ? voicebank::VoicebankRootKind::Development
                      : voicebank::VoicebankRootKind::Installed,
      });
  if (!relinked) return core::Result<void>{relinked.error()};
  return core::success();
}

core::Result<void> StandaloneApplicationController::relinkBackingMediaFromDialog() {
  const auto& project = session_.runtime().document().session().project();
  domain::TrackId targetTrack = session_.runtime().selectedTrack();
  const auto isAudioTrack = [&project](domain::TrackId id) {
    return std::find_if(project.audioTracks().begin(), project.audioTracks().end(),
                        [id](const auto& track) { return track.id == id; }) !=
           project.audioTracks().end();
  };
  if (!isAudioTrack(targetTrack)) {
    if (project.audioTracks().empty()) {
      return core::failure(core::ErrorCode::NotFound,
                           "Backing media relink requires an audio track");
    }
    targetTrack = project.audioTracks().front().id;
  }
  const auto selected = fileDialog_->choose(platform::FileDialogRequest{
      .purpose = platform::FileDialogPurpose::RelinkMedia,
      .title = "Relink Backing Audio",
      .initialDirectory = initialDirectory(session_.runtime().document()),
      .suggestedName = {},
      .extensions = {"wav"},
  });
  if (!selected) return core::Result<void>{selected.error()};
  if (!selected.value().has_value()) return core::success();
  auto relinked = session_.relinkBackingMedia(targetTrack, *selected.value());
  if (!relinked) return relinked;
  notifyStateChanged();
  return core::success();
}

core::Result<void> StandaloneApplicationController::replaceVoicebank(
    domain::TrackId trackId, std::string_view id, std::string_view version,
    std::string_view contentHash) {
  const auto candidate = findCandidate(id, version, contentHash);
  if (!candidate.has_value()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Replacement voicebank candidate is not available");
  }
  if (candidate->trust == voicebank::VoicebankTrust::UntrustedInstalled ||
      (candidate->trust == voicebank::VoicebankTrust::DevelopmentFixture &&
       !config_.allowDevelopmentVoicebanks)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Replacement voicebank is not accepted by the trust policy");
  }
  auto replaced = session_.runtime().voicebanks().replaceTrackVoicebank(
      session_.runtime().document(), trackId, *candidate);
  if (!replaced) return replaced;
  session_.runtime().handleDocumentChanged();
  static_cast<void>(onDocumentChanged());
  notifyStateChanged();
  return core::success();
}

core::Result<voicebank::VoicebankCoverageReport>
StandaloneApplicationController::selectedRegionCoverage() const {
  const auto& runtime = session_.runtime();
  const auto& project = runtime.document().session().project();
  const auto* track = project.findVocalTrack(runtime.selectedTrack());
  const auto* region = project.findRegion(runtime.selectedRegion());
  if (track == nullptr || region == nullptr) {
    return core::failure<voicebank::VoicebankCoverageReport>(
        core::ErrorCode::NotFound,
        "Coverage analysis requires a selected vocal track and region");
  }
  const auto resolution = runtime.voicebanks().resolveTrack(
      project, track->id);
  if (!resolution.resolved()) {
    return core::failure<voicebank::VoicebankCoverageReport>(
        core::ErrorCode::NotFound, resolution.diagnostic);
  }
  if (resolution.candidate->manifest.language != domain::Language::Japanese) {
    return core::failure<voicebank::VoicebankCoverageReport>(
        core::ErrorCode::Unsupported,
        "Coverage analysis currently supports the Japanese phonemizer");
  }
  phonemizer::JapaneseKanaPhonemizer phonemizer;
  const auto phonemes = phonemizer.phonemize(*region);
  const auto style = resolution.candidate->manifest.styles.empty()
                         ? std::string{}
                         : resolution.candidate->manifest.styles.front();
  return voicebank::VoicebankCoverageAnalyzer::analyzeRegion(
      resolution.candidate->manifest, track->id, *region, phonemes.tokens,
      style);
}

std::vector<platform::RecentProjectMenuItem>
StandaloneApplicationController::recentProjects() const {
  std::vector<platform::RecentProjectMenuItem> result;
  result.reserve(recentProjects_.entries().size());
  for (const auto& entry : recentProjects_.entries()) {
    result.push_back(platform::RecentProjectMenuItem{
        .path = entry.path,
        .displayName = entry.displayName,
        .missing = entry.missing,
    });
  }
  return result;
}

std::vector<platform::DocumentationMenuItem>
StandaloneApplicationController::documentation() const {
  std::vector<platform::DocumentationMenuItem> result;
  if (config_.manualsRoot.empty()) return result;
  for (const auto& spec : kDocumentation) {
    const auto path = config_.manualsRoot / std::filesystem::path{spec.relativePath};
    std::error_code error;
    if (std::filesystem::is_regular_file(
            std::filesystem::symlink_status(path, error)) && !error) {
      result.push_back(platform::DocumentationMenuItem{
          .id = std::string{spec.id},
          .displayName = std::string{spec.title},
          .path = path});
    }
  }
  return result;
}

core::Result<void> StandaloneApplicationController::openDocumentation(
    std::string_view id) {
  if (config_.manualsRoot.empty()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Offline documentation root is unavailable");
  }
  const auto iterator = std::find_if(
      kDocumentation.begin(), kDocumentation.end(),
      [id](const DocumentationSpec& spec) { return spec.id == id; });
  if (iterator == kDocumentation.end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Requested offline document is unknown");
  }
  const auto path = config_.manualsRoot / std::filesystem::path{iterator->relativePath};
  std::error_code error;
  if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error)) ||
      error) {
    return core::failure(core::ErrorCode::NotFound,
                         "Requested offline document is unavailable", path.string());
  }
  return platform::openDocumentationPath(path);
}

core::Result<void> StandaloneApplicationController::openRecentProject(
    const std::filesystem::path& path) {
  return openRecent(path);
}

std::vector<platform::RecoveryMenuItem>
StandaloneApplicationController::recoveryItems() const {
  std::vector<platform::RecoveryMenuItem> result;
  auto candidates = recoveryCandidates();
  if (!candidates) return result;
  for (const auto& candidate : candidates.value()) {
    if (!candidate.recoverable) continue;
    result.push_back(platform::RecoveryMenuItem{
        .metadataPath = candidate.metadataPath,
        .displayName = "Project " + candidate.projectId + " — revision " +
                       std::to_string(candidate.revision),
    });
  }
  return result;
}

core::Result<void> StandaloneApplicationController::recoverAutosave(
    const std::filesystem::path& metadataPath) {
  auto candidates = recoveryCandidates();
  if (!candidates) return core::Result<void>{candidates.error()};
  const auto iterator = std::find_if(
      candidates.value().begin(), candidates.value().end(),
      [&metadataPath](const authoring::RecoveryCandidate& candidate) {
        return candidate.metadataPath == metadataPath;
      });
  if (iterator == candidates.value().end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "The selected autosave no longer exists",
                         metadataPath.string());
  }
  return recover(*iterator);
}

void StandaloneApplicationController::notifyStateChanged() const {
  if (config_.stateChanged) config_.stateChanged();
}

void StandaloneApplicationController::notifyProgressChanged() const {
  if (config_.progressChanged) config_.progressChanged();
}

core::Result<bool> StandaloneApplicationController::requestClose() {
  if (exportInProgress()) {
    return core::failure<bool>(
        core::ErrorCode::Conflict,
        "An export is in progress; cancel it before quitting");
  }
  auto allowed = confirmDestructiveAction();
  if (!allowed) return allowed;
  if (!allowed.value()) return false;
  auto flushed = autosave_.flush();
  if (!flushed) return core::Result<bool>{flushed.error()};
  if (requestQuit_) requestQuit_();
  return true;
}

core::Result<void> StandaloneApplicationController::onDocumentChanged(
    std::chrono::steady_clock::time_point now) {
  return autosave_.onSuccessfulCommand(session_.runtime().document(), now);
}

core::Result<void> StandaloneApplicationController::tickAutosave(
    std::chrono::steady_clock::time_point now) {
  return autosave_.tick(session_.runtime().document(), now);
}

core::Result<std::vector<authoring::RecoveryCandidate>>
StandaloneApplicationController::recoveryCandidates() const {
  return autosave_.discover();
}

core::Result<void> StandaloneApplicationController::recover(
    const authoring::RecoveryCandidate& candidate) {
  auto recovered = session_.recoverProject(autosave_, candidate);
  if (recovered) notifyStateChanged();
  return recovered;
}

}  // namespace seam::standalone
