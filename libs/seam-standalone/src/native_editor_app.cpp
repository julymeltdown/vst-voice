#include "seam/standalone/native_editor_app.hpp"

#include "seam/build/version.hpp"
#include "seam/standalone/update_controller.hpp"
#include "seam/core/sha256.hpp"
#include "seam/distribution/procedural_package.hpp"
#include "seam/platform/accessibility_preferences.hpp"
#include "seam/platform/application_paths.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/standalone/eula_acceptance.hpp"
#include "seam/standalone/native_project_dialog.hpp"
#include "seam/standalone/playback_device_policy.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/native_ui/design/shell_evidence.hpp"
#include "seam/native_ui/paint/canvas2d.hpp"

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>

namespace seam::standalone {

NativeNewProjectSingerChoices makeNativeNewProjectSingerChoices(
    const std::vector<StandaloneApplicationController::InstalledSingerOffer>&
        offers,
    const std::vector<distribution::ProceduralCatalogueIssue>& issues,
    std::size_t omittedIssueCount,
    bool scanLimitReached) {
  NativeNewProjectSingerChoices choices;
  for (const auto& offer : offers) {
    const auto& candidate = offer.candidate;
    if (!offer.selectable) {
      auto label = "Unavailable — " + candidate.manifest.displayName + " (" +
          candidate.manifest.id + " " + candidate.manifest.version + ")";
      auto detail = offer.reason.empty()
          ? std::string{"This installed singer is not trusted or renderable by this build."}
          : offer.reason;
      choices.unavailable.push_back(NativeNewProjectUnavailableSingerOption{
          .label = std::move(label), .detail = std::move(detail)});
      continue;
    }
    for (const auto& style : candidate.manifest.styles) {
      auto label = candidate.manifest.displayName + " — " + style + " (" +
          candidate.manifest.language + ")";
      if (!offer.reviewed) label += " — unreviewed";
      choices.selectable.push_back(NativeNewProjectSingerOption{
          .label = std::move(label),
          .reference = domain::ProceduralRecipeReference{
              .resource = candidate.renderIdentity,
              .path = (candidate.resourceRoot / candidate.manifest.recipeEntry).string(),
              .style = style,
              .installation = distribution::proceduralInstallationReference(candidate)}});
    }
  }
  for (const auto& issue : issues) {
    const auto relativePath = issue.packagePath.lexically_relative(issue.root);
    const bool escapesRoot = !relativePath.empty() && *relativePath.begin() == "..";
    const bool isRootIssue =
        issue.packagePath.lexically_normal() == issue.root.lexically_normal();
    auto location = relativePath.empty() || relativePath == "." || escapesRoot
        ? issue.packagePath.filename().string()
        : relativePath.generic_string();
    if (location.empty()) location = "configured singer folder";
    auto detail = issue.detail.empty()
        ? std::string{"This package could not be loaded safely."}
        : issue.detail;
    choices.unavailable.push_back(NativeNewProjectUnavailableSingerOption{
        .label = isRootIssue ? "Unavailable — Singer catalogue root"
                             : "Unavailable — Package at " + location,
        .detail = std::move(detail)});
  }
  if (omittedIssueCount > 0U) {
    choices.unavailable.push_back(NativeNewProjectUnavailableSingerOption{
        .label = "Unavailable — Additional package scan issues",
        .detail = std::to_string(omittedIssueCount) +
            " more package issue(s) were found; only the first 64 are listed."});
  }
  if (scanLimitReached) {
    choices.unavailable.push_back(NativeNewProjectUnavailableSingerOption{
        .label = "Unavailable — Catalogue scan incomplete",
        .detail = "The installed singer catalogue reached its 8192 package-folder scan limit; "
                  "some installed resources may not be listed."});
  }
  return choices;
}

namespace {

native_ui::RenderStatusState renderStatusState(
    authoring::RenderState state) noexcept {
  switch (state) {
    case authoring::RenderState::Idle:
      return native_ui::RenderStatusState::Idle;
    case authoring::RenderState::Queued:
      return native_ui::RenderStatusState::Queued;
    case authoring::RenderState::Rendering:
      return native_ui::RenderStatusState::Rendering;
    case authoring::RenderState::Ready:
      return native_ui::RenderStatusState::Ready;
    case authoring::RenderState::Stale:
      return native_ui::RenderStatusState::Stale;
    case authoring::RenderState::Cancelled:
      return native_ui::RenderStatusState::Cancelled;
    case authoring::RenderState::Failed:
      return native_ui::RenderStatusState::Failed;
  }
  return native_ui::RenderStatusState::Failed;
}

std::string timestampNow() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

native_ui::RecoverySupportView supportPreviewView(
    const authoring::PreparedSupportBundle& prepared,
    bool crashMarkerAvailable) {
  const auto& preview = prepared.preview();
  native_ui::RecoverySupportView view{
      .visible = true,
      .mode = native_ui::RecoverySupportMode::Preview,
      .crashMarkerAvailable = crashMarkerAvailable,
      .candidateId = preview.candidateId,
      .archiveBytes = preview.archiveBytes,
      .archiveSha256 = preview.archiveSha256,
      .status = "Review every entry before confirming export",
  };
  view.items.reserve(preview.entries.size());
  for (const auto& entry : preview.entries) {
    auto detail = std::string{entry.kind ==
                                      authoring::SupportBundleEntryKind::Generated
                                  ? "PUBLIC"
                                  : "RESTRICTED"} +
                  " / " + (entry.included ? "INCLUDED" : "EXCLUDED");
    if (entry.requiresConsent) {
      detail = std::string{"RESTRICTED / "} +
               (entry.consented ? "CONSENTED" : "CONSENT NEEDED");
    }
    view.items.push_back(native_ui::RecoverySupportItemView{
        .name = entry.path,
        .detail = std::move(detail),
        .bytes = entry.bytes,
        .sha256 = entry.sha256,
        .included = entry.included,
    });
  }
  return view;
}

native_ui::RecoverySupportView supportReportsView(
    const std::vector<authoring::SupportBundleRecord>& reports,
    std::size_t selectedIndex, bool crashMarkerAvailable) {
  native_ui::RecoverySupportView view{
      .visible = !reports.empty(),
      .mode = native_ui::RecoverySupportMode::Reports,
      .crashMarkerAvailable = crashMarkerAvailable,
      .reportCount = static_cast<std::uint32_t>(reports.size()),
      .status = "Select a local report to reveal or delete",
  };
  view.items.reserve(reports.size());
  for (std::size_t index = 0U; index < reports.size(); ++index) {
    const auto& report = reports[index];
    view.items.push_back(native_ui::RecoverySupportItemView{
        .name = report.path.filename().string(),
        .detail = "ZIP / " + std::to_string(report.bytes) + " B",
        .bytes = report.bytes,
        .sha256 = report.sha256,
        .included = true,
        .selected = index == selectedIndex,
    });
  }
  return view;
}

}

core::Result<std::unique_ptr<NativeEditorApp>> NativeEditorApp::create(
    NativeEditorAppConfig config) {
  auto app = std::unique_ptr<NativeEditorApp>{
      new NativeEditorApp(std::move(config))};
  auto initialized = app->initialize();
  if (!initialized) {
    return core::Result<std::unique_ptr<NativeEditorApp>>{initialized.error()};
  }
  return app;
}

NativeEditorApp::~NativeEditorApp() {
  detachWindow();
  if (applicationMenu_ != nullptr) applicationMenu_->uninstall();
  applicationController_.reset();
  shutdownAudio();
  // The device goes before the authoring runtime, which owns the ring that its callback reads. A
  // device that did not say that it has stopped may still be calling back, and its destructor makes
  // a last attempt to stop it: that must find the ring still there. The runtime is reset here, and
  // not left to the members' destruction, because its render workers ask the window for repaints
  // through members that are declared after it. The processor reads only the ring and the meter,
  // and nothing calls it once the device is gone, so it may outlive the ring until the members go.
  audioDevice_.reset();
  authoring_.reset();
}

core::Result<void> NativeEditorApp::initialize() {
  const auto paths = config_.applicationSupportRoot.empty()
                         ? platform::applicationPaths()
                         : core::success(platform::ApplicationPaths::forTestRoot(
                               config_.applicationSupportRoot));
  if (!paths) return core::Result<void>{paths.error()};
  recoveryRoot_ = paths.value().recoveryRoot;
  auto production = makeProductionConfiguration(
      ProductionConfigurationInput{
          .mode = config_.runtimeMode,
          .paths = paths.value(),
          .voicebankRoots = config_.authoring.voicebankRoots,
          .trustedVoicebankKeys = config_.trustedVoicebankKeys,
          .developmentTrustRoot = config_.developmentTrustRoot,
          .allowDevelopmentVoicebanks = config_.allowDevelopmentVoicebanks,
          .forceThreadedAudio = config_.forceThreadedAudio,
          .bindFirstAvailableVoicebank =
              config_.authoring.bindFirstAvailableVoicebank,
          .startPaused = config_.startPaused,
          .sampleRate = config_.authoring.sampleRate,
          .outputChannels = config_.authoring.outputChannels,
          .audioBlockFrames = config_.audioBlockFrames,
          .characterPackage = config_.characterPackage,
      });
  if (!production) return core::Result<void>{production.error()};
  const auto& effective = production.value();
  config_.authoring.cacheRoot = effective.cacheRoot;
  config_.authoring.voicebankRoots = effective.voicebankRoots;
  config_.authoring.bindFirstAvailableVoicebank =
      effective.bindFirstAvailableVoicebank;
  config_.authoring.allowDevelopmentVoicebanks =
      effective.allowDevelopmentVoicebanks;
  config_.characterPackage = effective.characterPackage;
  config_.applicationSupportRoot = effective.applicationSupportRoot;
  config_.trustedVoicebankKeys = effective.trustedVoicebankKeys;
  config_.developmentTrustRoot = effective.developmentTrustRoot;
  config_.allowDevelopmentVoicebanks = effective.allowDevelopmentVoicebanks;
  config_.forceThreadedAudio = effective.forceThreadedAudio;
  config_.startPaused = effective.startPaused;
  const auto manualsRoot = config_.manualsRoot.empty()
                               ? paths.value().manualsRoot
                               : config_.manualsRoot;

  if (effective.mode == ProductionRuntimeMode::Release) {
    const auto eulaPath = manualsRoot / "EULA.md";
    std::error_code eulaError;
    if (manualsRoot.empty() ||
        !std::filesystem::is_regular_file(
            std::filesystem::symlink_status(eulaPath, eulaError)) ||
        eulaError) {
      return core::failure(core::ErrorCode::NotFound,
                           "Bundled EULA is required for release launch",
                           eulaPath.string());
    }
    auto eulaDigest = core::sha256File(eulaPath);
    if (!eulaDigest) return core::Result<void>{eulaDigest.error()};
    const auto acceptancePath =
        paths.value().settingsRoot / "eula-acceptance.json";
    auto acceptance = EulaAcceptanceStore::load(acceptancePath);
    if (!acceptance) return core::Result<void>{acceptance.error()};
    const auto accepted = acceptance.value().has_value() &&
                          EulaAcceptanceStore::matches(
                              *acceptance.value(),
                              kExternalBetaEulaDocumentVersion,
                              eulaDigest.value());
    if (!accepted) {
      auto prompted = platform::requestEulaAcceptance(eulaPath);
      if (!prompted) return core::Result<void>{prompted.error()};
      if (!prompted.value()) {
        return core::failure(core::ErrorCode::Conflict,
                             "External Beta EULA was not accepted");
      }
      auto saved = EulaAcceptanceStore::save(
          acceptancePath,
          EulaAcceptanceRecord{
              .documentVersion = std::string{kExternalBetaEulaDocumentVersion},
              .documentSha256 = eulaDigest.value(),
              .acceptedAtUtc = timestampNow(),
          });
      if (!saved) return saved;
    }
  }

  const platform::CrashCaptureConfig crashConfig{
      .root = paths.value().crashReportsRoot,
  };
  auto recoveredCrash = platform::CrashCapture::recoverPending(crashConfig);
  if (recoveredCrash) {
    startupCrashMarker_ = std::move(recoveredCrash).value();
  } else {
    lastError_ = recoveredCrash.error().message;
  }
  auto crashCapture = platform::CrashCapture::install(crashConfig);
  if (crashCapture) {
    crashCapture_ = std::move(crashCapture).value();
  } else {
    lastError_ = crashCapture.error().message;
  }
  supportBundle_ = std::make_unique<authoring::SupportBundleService>(
      paths.value().recoveryRoot / "SupportReports");
  supportExportRoot_ = config_.applicationSupportRoot.empty()
                           ? paths.value().userDataRoot / "Support"
                           : config_.applicationSupportRoot / "Support";
  supportIntakeDestination_ = config_.supportIntakeDestination;

  updatePolicyPath_ = config_.updatePolicyPath;
  updateManifestPath_ = config_.updateManifestPath;
  trustedUpdateRoot_ = config_.trustedUpdateRoot;
  audioSettingsStore_ = std::make_unique<authoring::AudioSettingsStore>(
      config_.applicationSupportRoot / "Settings" / "audio-settings.json");
  const auto persistedSettings = audioSettingsStore_->load();
  if (persistedSettings) {
    config_.authoring.sampleRate = persistedSettings.value().sampleRate;
    config_.authoring.outputChannels = persistedSettings.value().outputChannels;
    config_.audioBlockFrames = persistedSettings.value().blockFrames;
    startupDeviceId_ = persistedSettings.value().deviceId;
  } else if (persistedSettings.error().code != core::ErrorCode::NotFound) {
    lastError_ = persistedSettings.error().message;
  }

  if (config_.designPreferences)
    shell_.activate(native_ui::design::locateDesignAssets(), *config_.designPreferences);
  else if (config_.persistDesignPreferences)
    shell_.activate();
  else
    shell_.activate(native_ui::design::locateDesignAssets(), native_ui::design::DesignPreferences{});
  if (config_.uiClock) shell_.setUiClock(config_.uiClock);
  shell_.setRepaintCallback([this] {
    requestWindowRepaint();
  });
  // The native window keeps its surface between frames, so a frame that changes only the dynamic
  // layer updates just its damaged rectangles.
  shell_.setRetainedSurface(true);

  native_ui::EditorHostCallbacks callbacks{
      .requestRepaint = [this] {
        requestWindowRepaint();
      },
      .beginTextInput = [this](const native_ui::TextInputRequest& request) {
        if (window_ != nullptr) window_->beginTextInput(shell_.translateTextInput(request));
      },
      .endTextInput = [this] {
        shell_.textInputEnded();
        if (window_ != nullptr) window_->endTextInput();
      },
      .setPlaying = [this](bool playing) {
        if (playing) {
          if (authoring_->runtime().transport().state().available) {
            const auto started = startAudioForPlayback();
            if (!started) {
              static_cast<void>(authoring_->runtime().transport().pause());
              return started;
            }
          }
        } else {
          const auto stopped = stopAudioForPlayback();
          if (!stopped) {
            // The transport is paused, and the device is still running: the next frame asks it to
            // stop again, and the creator is told that it did not.
            requestWindowRepaint();
            return stopped;
          }
        }
        requestWindowRepaint();
        return core::success();
      },
      .documentChanged = [this] {
        if (applicationController_ != nullptr) {
          record(applicationController_->onDocumentChanged());
        }
        requestWindowRepaint();
      },
      .validateSingerControl = [this](domain::TrackId trackId,
                                      synthesis::RendererControl control) {
        if (applicationController_ == nullptr)
          return core::failure(core::ErrorCode::InvalidState,
                               "Singer capability resolution is unavailable");
        return applicationController_->validateSingerControl(trackId, control);
      },
      .cancelExport = [this] {
        if (applicationController_ != nullptr) {
          applicationController_->cancelExport();
        }
        requestWindowRepaint();
      },
      .previewSeam = [this](domain::PhonemeKey key, bool alternate) {
        if (authoring_ == nullptr) {
          return core::failure(core::ErrorCode::InvalidState,
                               "Standalone authoring runtime is unavailable");
        }
        const auto result = authoring_->runtime().previewSeam(key, alternate);
        requestWindowRepaint();
        return result;
      },
      .selectVoicebank = [this](std::string_view id, std::string_view version,
                                std::string_view contentHash) {
        if (applicationController_ == nullptr) {
          return core::failure(core::ErrorCode::InvalidState,
                               "Voicebank selection is unavailable");
        }
        const auto selected = applicationController_->selectVoicebank(
            id, version, contentHash);
        if (selected) refreshCrashRecoveryContext();
        requestWindowRepaint();
        return selected;
      },
      .diagnosticAction = [this](const authoring::Diagnostic& diagnostic,
                                 authoring::DiagnosticAction action) {
        return handleDiagnosticAction(diagnostic, action);
      },
      .selectSupportReport = [this](std::size_t index) {
        return selectSupportReport(index);
      },
      .viewChanged = [this] {
        requestWindowRepaint();
      },
      .applyAudioSettings = [this](authoring::AudioSettings settings)
          -> core::Result<void> {
        auto applied = applyAudioSettings(std::move(settings));
        if (!applied) return core::Result<void>{applied.error()};
        refreshCrashRecoveryContext();
        requestWindowRepaint();
        return core::success();
      },
      .reduceMotionEnabled = [] {
        return platform::currentAccessibilityPreferences().reduceMotion;
      },
      .prepareJapaneseReadingResource = config_.prepareJapaneseReadingResource,
      .resetOutputClip = [this] {
        outputMeter_.resetClip();
        requestWindowRepaint();
      },
  };
  auto created = AuthoringSession::create(config_.authoring,
                                          std::move(callbacks));
  if (!created) {
    return core::Result<void>{created.error()};
  }
  authoring_ = std::move(created).value();
  // Read the signed update manifest once the controller exists so a pause or an unsupported
  // build is visible in the running app. This reads the same envelope the client already
  // verifies, so a pause needs no new channel and a tampered manifest fails verification before
  // these fields are read.
  refreshDistributionAuthority();
  if (startupCrashMarker_.has_value()) {
    authoring_->controller().setDiagnostics({authoring::Diagnostic{
        .code = "CRASH_RECOVERY_AVAILABLE",
        .severity = authoring::DiagnosticSeverity::Warning,
        .messageKey = "crash.recovery_available",
        .affectedIds = {},
        .actions = authoring::DiagnosticRegistry::actions(
            "CRASH_RECOVERY_AVAILABLE"),
        .occurrenceCount = 1U}});
  }

  const auto supportRoot = config_.applicationSupportRoot;
  auto application = StandaloneApplicationController::create(
      *authoring_,
      config_.fileDialogFactory ? config_.fileDialogFactory() : platform::createNativeFileDialog(),
      config_.unsavedChangesPromptFactory ? config_.unsavedChangesPromptFactory()
                                          : platform::createNativeUnsavedChangesPrompt(),
      StandaloneApplicationControllerConfig{
          .autosaveRoot = supportRoot / "Autosaves",
          .recentProjectsPath = supportRoot / "recent-projects.json",
          .voicebankInstallRoot = supportRoot / "Voicebanks",
          // An installed procedural singer is a user resource like a bank, so the shipped application
          // catalogs it beside the banks and keeps its review decisions with the rest of the user data.
          // Declaring the engine here is what lets the picker offer a singer this build can really
          // render and name the reason for any it cannot.
          .proceduralSingerRoots = {distribution::ProceduralSearchRoot{
              .path = supportRoot / "Singers",
              .kind = distribution::ProceduralRootKind::Installed}},
          .renderableProceduralEngineId = std::string{voice_design::kSourceFilterEngineId},
          .renderableProceduralEngineRevision = voice_design::kSourceFilterEngineRevision,
          .proceduralReviewStorePath = supportRoot / "Singers" / "reviews.json",
          .manualsRoot = manualsRoot,
          .trustedVoicebankKeys = config_.trustedVoicebankKeys,
          .developmentTrustRoot = config_.developmentTrustRoot,
          .allowDevelopmentVoicebanks = config_.allowDevelopmentVoicebanks,
          .requestNewProject = config_.requestNewProject
                                   ? config_.requestNewProject
                                   : [this]() -> core::Result<std::optional<authoring::NewProjectRequest>> {
                                       auto dialog = createNativeNewProjectDialog();
                                       if (dialog == nullptr) {
                                         return core::failure<std::optional<authoring::NewProjectRequest>>(
                                             core::ErrorCode::Unsupported,
                                             "Native New Project form is unavailable");
                                       }
                                       const auto currentPath =
                                           authoring_->runtime().document().identity().projectPath;
                                       std::vector<NativeNewProjectSingerOption> proceduralSingers;
                                       std::vector<NativeNewProjectUnavailableSingerOption>
                                           unavailableProceduralSingers;
                                       if (applicationController_ != nullptr) {
                                         auto catalogue =
                                             applicationController_->installedSingerCatalogue();
                                         if (!catalogue) {
                                           return core::Result<std::optional<authoring::NewProjectRequest>>{
                                               catalogue.error()};
                                         }
                                         auto choices = makeNativeNewProjectSingerChoices(
                                             catalogue.value().offers,
                                             catalogue.value().issues,
                                             catalogue.value().omittedIssueCount,
                                             catalogue.value().scanLimitReached);
                                         proceduralSingers = std::move(choices.selectable);
                                         unavailableProceduralSingers =
                                             std::move(choices.unavailable);
                                       }
                                       return dialog->choose(NativeNewProjectDialogConfig{
                                           .candidates = authoring_->runtime().voicebanks().candidates(),
                                           .proceduralSingers = std::move(proceduralSingers),
                                           .unavailableProceduralSingers =
                                               std::move(unavailableProceduralSingers),
                                           .initialDirectory = currentPath.has_value()
                                                                   ? currentPath->parent_path()
                                                                   : std::filesystem::path{},
                                           .suggestedName = "Untitled.seam",
                                           .sampleRate = config_.authoring.sampleRate,
                                           .outputChannels = config_.authoring.outputChannels,
                                       });
                                     },
          .defaultNewProject = authoring::NewProjectRequest{
              .name = "Untitled",
              .tempoBpm = 120.0,
              .sampleRate = config_.authoring.sampleRate,
              .outputChannels = config_.authoring.outputChannels,
              .initialVoicebank = std::nullopt,
          },
          .stateChanged = [this] {
            refreshCrashRecoveryContext();
            if (applicationMenu_ != nullptr) applicationMenu_->refresh();
            requestWindowRepaint();
          },
          .progressChanged = [this] {
            requestWindowRepaint();
          },
          // A Transport-menu command changes only the transport. The frame it asks for is where this
          // app starts or stops the device and shows the new state.
          .transportChanged = [this] {
            requestWindowRepaint();
          },
          .openAudioSettings = [this] {
            authoring_->controller().showAudioSettings();
            requestWindowRepaint();
            return core::success();
          },
          .editPronunciationHint = [this] {
            const auto result = authoring_->controller().beginSelectedHintEdit();
            record(result);
            return result;
          },
          .findReplaceLyrics = [this] {
            const auto result = authoring_->controller().beginReplacementInput();
            record(result);
            return result;
          },
          .findNotes = [this] {
            const auto result = authoring_->controller().beginFindInput();
            record(result); return result;
          },
          .findActiveDiagnostics = [this] {
            const auto result = authoring_->controller().beginDiagnosticFindInput();
            record(result); return result;
          },
          .findNextNote = [this] {
            const auto result = authoring_->controller().repeatFind();
            record(result); return result;
          },
          .findPreviousNote = [this] {
            const auto result = authoring_->controller().repeatFind(true);
            record(result); return result;
          },
          .clearSelectedVibrato = [this] {
            const auto result = authoring_->controller().openClearVibratoReview();
            record(result); return result;
          },
          .editSelectedVibrato = [this] {
            const auto result = authoring_->controller().openVibratoInspector(); record(result); return result;
          },
          .editRegionDynamics = [this] {
            const auto result = authoring_->controller().openDynamicsInspector(); record(result); return result;
          },
          .nudgeFormantUp = [this] {
            const auto result = authoring_->controller().nudgeFormantShift(1); record(result); return result;
          },
          .nudgeFormantDown = [this] {
            const auto result = authoring_->controller().nudgeFormantShift(-1); record(result); return result;
          },
          .resetRegionFormantCurve = [this] {
            const auto result = authoring_->controller().resetFormantCurve(); record(result); return result;
          },
          .nudgeBreathinessUp = [this] {
            const auto result = authoring_->controller().nudgeBreathiness(1); record(result); return result;
          },
          .nudgeBreathinessDown = [this] {
            const auto result = authoring_->controller().nudgeBreathiness(-1); record(result); return result;
          },
          .resetRegionBreathinessCurve = [this] {
            const auto result = authoring_->controller().resetBreathinessCurve(); record(result); return result;
          },
          .nudgeTensionUp = [this] {
            const auto result = authoring_->controller().nudgeTension(1); record(result); return result;
          },
          .nudgeTensionDown = [this] {
            const auto result = authoring_->controller().nudgeTension(-1); record(result); return result;
          },
          .resetRegionTensionCurve = [this] {
            const auto result = authoring_->controller().resetTensionCurve(); record(result); return result;
          },
          .nudgeAirinessUp = [this] {
            const auto result = authoring_->controller().nudgeAiriness(1); record(result); return result;
          },
          .nudgeAirinessDown = [this] {
            const auto result = authoring_->controller().nudgeAiriness(-1); record(result); return result;
          },
          .resetRegionAirinessCurve = [this] {
            const auto result = authoring_->controller().resetAirinessCurve(); record(result); return result;
          },
          .nudgeGenderUp = [this] {
            const auto result = authoring_->controller().nudgeGender(1); record(result); return result;
          },
          .nudgeGenderDown = [this] {
            const auto result = authoring_->controller().nudgeGender(-1); record(result); return result;
          },
          .resetRegionGenderCurve = [this] {
            const auto result = authoring_->controller().resetGenderCurve(); record(result); return result;
          },
          .nudgeGrowlUp = [this] {
            const auto result = authoring_->controller().nudgeGrowl(1); record(result); return result;
          },
          .nudgeGrowlDown = [this] {
            const auto result = authoring_->controller().nudgeGrowl(-1); record(result); return result;
          },
          .resetRegionGrowlCurve = [this] {
            const auto result = authoring_->controller().resetGrowlCurve(); record(result); return result;
          },
          .openExpressionLane = [this] {
            const auto result = authoring_->controller().openExpressionLane(
                authoring_->controller().selectedExpressionChannel());
            record(result); return result;
          },
          .nextExpressionChannel = [this] {
            const auto result = authoring_->controller().cycleExpressionLane(1); record(result); return result;
          },
          .previousExpressionChannel = [this] {
            const auto result = authoring_->controller().cycleExpressionLane(-1); record(result); return result;
          },
          .nudgeExpressionChannelUp = [this] {
            const auto result = authoring_->controller().nudgeExpressionLane(1); record(result); return result;
          },
          .nudgeExpressionChannelDown = [this] {
            const auto result = authoring_->controller().nudgeExpressionLane(-1); record(result); return result;
          },
          .closeExpressionLane = [this] {
            const auto result = authoring_->controller().closeExpressionLane(); record(result); return result;
          },
          .editTrackStyle = [this] {
            const auto result = authoring_->controller().openStyleCoverageSheet(); record(result); return result;
          },
          .editJapaneseReading = [this] {
            const auto result = authoring_->controller().openJapaneseReadingReview(); record(result); return result;
          },
          .addRegion = [this]() -> core::Result<void> {
            auto added = authoring_->controller().addRegionToSelectedTrack();
            const auto result = added ? core::success() : core::Result<void>{added.error()};
            record(result);
            return result;
          },
          .showAbout = [this] {
            const auto result = shell_.setAboutOpen(authoring_->controller(), true);
            record(result);
            requestWindowRepaint();
            return result;
          },
          .reviewInterchangeImport = [this](
              const authoring::InterchangeImportDraft& draft) -> core::Result<bool> {
            if (config_.reviewInterchangeImport) {
              return config_.reviewInterchangeImport(draft);
            }
            auto dialog = createNativeInterchangeReviewDialog();
            if (!dialog) {
              return core::failure<bool>(core::ErrorCode::Unsupported,
                  "Native interchange review is unavailable");
            }
            return dialog->review(draft);
          },
          .reviewInterchangeExport = [this](
              const authoring::InterchangeExportDraft& draft) -> core::Result<bool> {
            if (config_.reviewInterchangeExport) {
              return config_.reviewInterchangeExport(draft);
            }
            auto dialog = createNativeInterchangeReviewDialog();
            if (!dialog) {
              return core::failure<bool>(core::ErrorCode::Unsupported,
                  "Native interchange export review is unavailable");
            }
            return dialog->reviewExport(draft);
          },
          .removeSelectedOverlaps = [this] {
            const auto result = authoring_->controller().openNoteCleanupReview(ui::NoteCleanupKind::RemoveOverlap);
            record(result); return result;
          },
          .closeSelectedGaps = [this] {
            const auto result = authoring_->controller().openNoteCleanupReview(ui::NoteCleanupKind::CloseGap);
            record(result); return result;
          },
          .autoLegatoSelectedNotes = [this] {
            const auto result = authoring_->controller().openNoteCleanupReview(ui::NoteCleanupKind::AutoLegato);
            record(result); return result;
          },
          .clearRegionDynamicsCurve = [this] {
            const auto result = authoring_->controller().openClearDynamicsReview(); record(result); return result;
          },
          // While VOICE is shown, the menu's Undo and Redo act on the Voice Designer on screen.
          .interceptCommand = [this](platform::ApplicationCommand command)
              -> std::optional<core::Result<void>> {
            if (command != platform::ApplicationCommand::Undo &&
                command != platform::ApplicationCommand::Redo)
              return std::nullopt;
            return shell_.routeUndo(command == platform::ApplicationCommand::Redo);
          },
          // The autosave interval counts on the clock the animation reads, so a test that moves one
          // moves both. Empty in the shipping app.
          .clock = config_.uiClock,
          .installationWarning = [this](authoring::Diagnostic warning) {
            const auto same = std::find_if(installationDiagnostics_.begin(), installationDiagnostics_.end(),
                [&](const auto& existing) { return existing.sameIssueAs(warning); });
            if (same == installationDiagnostics_.end()) installationDiagnostics_.push_back(std::move(warning));
            else same->addOccurrences(warning.occurrenceCount);
            auto diagnostics = authoring_->runtime().diagnostics();
            if (audioDiagnostic_) diagnostics.push_back(*audioDiagnostic_);
            if (rendererChangedDiagnostic_) diagnostics.push_back(*rendererChangedDiagnostic_);
            diagnostics.insert(diagnostics.end(), installationDiagnostics_.begin(), installationDiagnostics_.end());
            authoring_->controller().setDiagnostics(std::move(diagnostics));
            requestWindowRepaint();
          },
          .installFaultInjector = config_.installFaultInjector,
      },
      [this] { closeRequested_.store(true, std::memory_order_release); });
  if (!application) return core::Result<void>{application.error()};
  applicationController_ = std::move(application).value();
  // The SING shell's EXPORT workspace runs the same Export Set command as the menu and Command-E,
  // and shows the settings that command will use.
  shell_.setHostActions(native_ui::design::ShellHostActions{
      .exportSet = [this]() -> core::Result<void> {
        return applicationController_->dispatch(platform::ApplicationCommand::ExportSet);
      },
      .exportPlan = [this]() -> std::optional<native_ui::design::ShellExportPlan> {
        const auto plan = applicationController_->plannedExportSet();
        const auto format = plan.settings.format == voicebank::WavSampleFormat::Pcm16 ? "16-bit WAV"
                            : plan.settings.format == voicebank::WavSampleFormat::Pcm24
                                ? "24-bit WAV"
                                : "32-bit float WAV";
        return native_ui::design::ShellExportPlan{
            .sampleRate = plan.settings.sampleRate,
            .channels = plan.settings.channels,
            .format = format,
            .master = plan.settings.includeMaster,
            .stems = plan.settings.includeStems,
            .asksAboutPackaging = plan.asksAboutPackaging,
        };
      },
      .exportUnavailable = {},
      .exportBusy = [this] { return applicationController_->exportInProgress(); },
      .regionWaveform =
          [this] {
            auto& runtime = authoring_->runtime();
            const auto audible = runtime.audiblePublication();
            return native_ui::bindRegionWaveform(
                native_ui::RegionWaveformRequest{
                    .audio = audible.audio,
                    .stale = audible.stale,
                    .documentRevision = runtime.document().session().revision(),
                    .track = runtime.selectedTrack(),
                    .region = runtime.selectedRegion(),
                    .render = runtime.renderer().progress().state},
                waveforms_);
          },
      // Exactly the shortcuts keyDown dispatches as application commands below, plus the
      // editor's undo and redo.
      .applicationShortcut =
          [](const native_ui::KeyEvent& event) {
            if (!event.modifiers.primaryShortcut() || event.modifiers.alt) return false;
            switch (event.key) {
              case native_ui::NativeKey::N:
              case native_ui::NativeKey::O:
              case native_ui::NativeKey::S:
              case native_ui::NativeKey::E:
              case native_ui::NativeKey::Q:
              case native_ui::NativeKey::Z:
              case native_ui::NativeKey::Y: return true;
              default: return false;
            }
          },
      .voice = makeVoiceHost(),
  });
  applicationMenu_ = platform::createNativeApplicationMenu();
  if (applicationMenu_ != nullptr) {
    auto installed = applicationMenu_->install(*applicationController_);
    if (!installed && installed.error().code != core::ErrorCode::Unsupported) {
      return installed;
    }
  }

  std::error_code characterError;
  const auto hasCharacterPackage =
      !config_.characterPackage.empty() &&
      std::filesystem::is_directory(config_.characterPackage, characterError);
  if (hasCharacterPackage) {
    const auto character = character_.load(config_.characterPackage);
    if (character) {
      authoring_->controller().setCharacterMetadata(
          character_.displayName(), character_.styleName());
      const auto* package = character_.package();
      if (package != nullptr) {
        authoring_->controller().setCharacterBinding({
            .id = package->manifest.characterId,
            .version = package->manifest.version,
            .voicebankId = package->manifest.voicebankId,
            .resourceIdentity = package->manifest.resourceIdentity,
            .accentPrimary = package->manifest.accent.primary,
            .accentSecondary = package->manifest.accent.secondary,
            .hasPerformance = character_.hasPerformanceAssets(),
        });
      }
      authoring_->controller().setCharacterPortrait(
          character_.portrait(character::State::Neutral));
    } else {
      lastError_ = character.error().message;
    }
  }
  auto audio = initializeAudio();
  if (!audio) return audio;
  refreshCrashRecoveryContext();
  return core::success();
}

core::Result<void> NativeEditorApp::initializeAudio() {
  auto& runtime = authoring_->runtime();
  processor_ = std::make_unique<platform::MultichannelRingBufferAudioProcessor>(
      runtime.transport().ringBuffer(),
      std::max<std::size_t>(config_.audioBlockFrames, 4096U), &outputMeter_);
  platform::AudioDeviceConfig deviceConfig{
      .deviceId = startupDeviceId_,
      .sampleRate = runtime.transport().sampleRate(),
      .blockFrames = config_.audioBlockFrames,
      .outputChannels = runtime.transport().outputChannels(),
      .applicationName = "Project SEAM",
      .streamName = "Production standalone preview",
  };

  std::optional<core::Error> physicalError;
  if (config_.forceThreadedAudio) {
    if (config_.runtimeMode != ProductionRuntimeMode::DeterministicTest) {
      return core::failure(
          core::ErrorCode::InvalidState,
          "Callback-clock audio is restricted to deterministic test mode");
    }
    auto threaded = config_.threadedAudioDeviceFactory
                        ? config_.threadedAudioDeviceFactory()
                        : platform::createThreadedAudioDevice();
    auto opened = threaded->open(deviceConfig, *processor_);
    if (!opened) return opened;
    audioDevice_ = std::move(threaded);
  } else {
    auto physical = config_.systemAudioDeviceFactory
                        ? config_.systemAudioDeviceFactory()
                        : platform::createSystemAudioDevice();
    auto opened = physical->open(deviceConfig, *processor_);
    if (opened) audioDevice_ = std::move(physical);
    else physicalError = opened.error();
  }

  if (!config_.startPaused) {
    const auto played = runtime.transport().play();
    if (!played) return played;
  }
  const auto info = audioDevice_ != nullptr
                        ? audioDevice_->info()
                        : platform::AudioDeviceInfo{
                              .backend = "unavailable",
                              .deviceId = startupDeviceId_.empty()
                                              ? "system-default"
                                              : startupDeviceId_,
                              .deviceName = "Unavailable system output",
                              .sampleRate = deviceConfig.sampleRate,
                              .blockFrames = deviceConfig.blockFrames,
                              .outputChannels = deviceConfig.outputChannels,
                              .physical = false,
                          };
  authoring_->controller().setAudioState(
      audioDevice_ != nullptr && info.physical, info.backend);
  audioDeviceCatalog_ = config_.audioDeviceCatalogFactory
                            ? config_.audioDeviceCatalogFactory()
                            : platform::createSystemAudioDeviceCatalog();
  audioSettings_ = std::make_unique<authoring::AudioSettingsController>(
      authoring::AudioSettings{
          .deviceId = info.deviceId,
          .sampleRate = info.sampleRate,
          .blockFrames = info.blockFrames,
          .outputChannels = info.outputChannels,
          .revision = 1U,
      },
      [this](const authoring::AudioSettings& settings) {
        return restartAudio(settings);
      });
  if (physicalError.has_value()) setAudioUnavailable(*physicalError);
  else clearAudioNotice();
  return core::success();
}

core::Result<void> NativeEditorApp::startAudioForPlayback() {
  // However this returns (started, refused, or found running already), the transport is told what
  // the device is doing then.
  struct ReportOnExit final {
    NativeEditorApp& app;
    ~ReportOnExit() { app.reportConsumerToTransport(); }
  } reportOnExit{*this};
  if (authoring_ == nullptr || audioDevice_ == nullptr || processor_ == nullptr) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Audio playback is unavailable before initialization");
  }
  if (audioDevice_->running()) return core::success();

  // This brings the device up for playback that was asked for already: the creator's Play has sent
  // it before it calls this, and a repaint calls it for a transport that reports it is playing. It
  // sends the transport no command. A repaint decides from a report that can be a moment old, and a
  // Play sent from here would undo a Pause the creator gave in that moment.
  auto& transport = authoring_->runtime().transport();
  if (!transport.state().available) {
    return core::failure(core::ErrorCode::Conflict,
                         "Playable audio is not ready for the audio device");
  }

  // The device is stopped, so nothing reads the ring while the feeder fills it: the transport
  // answers the feeder's request to drop audio from before a seek or a play in the device's place,
  // so that the device starts on audio that follows what the creator asked for, and the wait for
  // the start buffer is one that can end.
  const auto buffered = transport.awaitStartBuffer(std::chrono::seconds{2});
  if (!buffered) return buffered;

  auto started = audioDevice_->start();
  if (!started) {
    // A device whose start failed is not running, and stopping a device that is not running
    // succeeds (see IAudioDevice::stop): this only makes sure.
    static_cast<void>(audioDevice_->stop());
    setAudioUnavailable(started.error());
    return started;
  }
  clearAudioNotice();
  const auto info = audioDevice_->info();
  authoring_->controller().setAudioState(info.physical, info.backend);
  return core::success();
}

core::Result<void> NativeEditorApp::stopAudioForPlayback() noexcept {
  // Stopped or not, the transport is told what the device is doing then. A device that does not say
  // that it has stopped goes on saying that it runs, and the transport goes on treating it as the
  // consumer.
  struct ReportOnExit final {
    NativeEditorApp& app;
    ~ReportOnExit() { app.reportConsumerToTransport(); }
  } reportOnExit{*this};
  if (audioDevice_ == nullptr) return core::success();
  auto stopped = audioDevice_->stop();
  if (!stopped) {
    // A device that does not say that it has stopped goes on being the consumer of the ring: it
    // stays here and running() goes on saying so, so that a later frame decides again and asks
    // again. The creator is told what is true of it, which is that it is still playing rather than
    // that audio is unavailable (it is not: the output is running), and the notice goes when a stop
    // succeeds.
    setAudioStopRefused(stopped.error());
    audioStopFailed_ = true;
    return stopped;
  }
  if (audioStopFailed_) {
    // It has stopped now, and what the creator was told of it is no longer true.
    audioStopFailed_ = false;
    clearAudioNotice();
    if (authoring_ != nullptr) {
      const auto info = audioDevice_->info();
      authoring_->controller().setAudioState(info.physical, info.backend);
    }
  }
  return core::success();
}

void NativeEditorApp::reportConsumerToTransport() noexcept {
  if (authoring_ == nullptr) return;
  authoring_->runtime().transport().setConsumerRunning(audioDevice_ != nullptr &&
                                                        audioDevice_->running());
}

std::chrono::steady_clock::time_point NativeEditorApp::uiNow() const {
  return config_.uiClock ? config_.uiClock() : std::chrono::steady_clock::now();
}

core::Result<void> NativeEditorApp::restartAudio(
    const authoring::AudioSettings& settings) {
  // However this returns (the new device, the old one put back, or none), the transport is told
  // what the device is doing then, and not by the next painted frame: the old device may have
  // stopped by itself since the last one, and the new one is not started until a Play needs it.
  struct ReportOnExit final {
    NativeEditorApp& app;
    ~ReportOnExit() { app.reportConsumerToTransport(); }
  } reportOnExit{*this};
  if (authoring_ == nullptr || processor_ == nullptr || audioSettings_ == nullptr) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Audio settings are unavailable before audio initialization");
  }

  const auto previous = audioSettings_->current();
  if (audioDevice_ != nullptr && settings.deviceId == previous.deviceId &&
      settings.sampleRate == previous.sampleRate &&
      settings.blockFrames == previous.blockFrames &&
      settings.outputChannels == previous.outputChannels) {
    return core::success();
  }
  // The creator changed the audio settings: whatever device comes of that is asked to start for a Play
  // that stands without the hold that the old device's failed start put on it. It has its own first try.
  deviceRetry_.clear();
  const auto previousDeviceInfo =
      audioDevice_ != nullptr
          ? audioDevice_->info()
          : platform::AudioDeviceInfo{
                .backend = "unavailable",
                .deviceId = previous.deviceId,
                .deviceName = "Unavailable system output",
                .sampleRate = previous.sampleRate,
                .blockFrames = previous.blockFrames,
                .outputChannels = previous.outputChannels,
                .physical = false,
            };
  const auto wasRunning = audioDevice_ != nullptr && audioDevice_->running();
  auto previousDevice = std::move(audioDevice_);
  if (previousDevice != nullptr) {
    // The device is the consumer of the ring until it says that it has stopped. When it does not,
    // nothing is changed: it stays where it was, running, the transport is not asked anything,
    // nothing answers the ring's resets in its place, no processor is replaced and no other device
    // is opened on the ring, and the transport is not told that the consumer is gone: the exit
    // guard reports the device that was put back, which still runs. The creator is told, and can
    // ask again.
    auto stopped = previousDevice->stop();
    if (!stopped) {
      audioDevice_ = std::move(previousDevice);
      return stopped;
    }
  }
  // The old device was the consumer, and it has said that it is stopped: nothing runs until a
  // device is put back. The transport is told now, and not only as this returns, because the Play
  // that restores what the creator was doing is asked for below, before any device is started, and
  // a Play that is asked for while no consumer runs is one that waits for a consumer (see
  // TransportController::setConsumerRunning). Asked for with the old device still reported as
  // running, it would look like a Play that a consumer has taken up, and a render that lands before
  // the next painted frame would drop it. A stop that the device did not report is not told: it
  // returned above.
  reportConsumerToTransport();
  // With the device stopped nothing reads the ring, so the transport can say where the creator is
  // and what they were doing: playing, paused, or listening to the end of a song that the feeder
  // has already handed over. The feeder's report cannot say it: it follows the creator's commands
  // by a moment, and it says the song has stopped before the creator has heard the end of it. The
  // transport is paused where the creator is, and playback is put back below when they were
  // playing, on the new device or on the old one when the new one cannot be opened.
  const auto suspended = authoring_->runtime().transport().suspend(wasRunning);
  if (!suspended) {
    // Nothing was queued or recorded, so the transport is as it was and the device is all that is
    // left to put back. The ring still holds what the device had not played.
    audioDevice_ = std::move(previousDevice);
    if (audioDevice_ != nullptr && wasRunning) {
      const auto restarted = audioDevice_->start();
      if (!restarted) {
        // Playback cannot go on, and the creator is told, as when a Play cannot start the device.
        static_cast<void>(authoring_->runtime().transport().pause());
        setAudioUnavailable(restarted.error());
      }
    }
    return core::Result<void>{suspended.error()};
  }
  const bool resumePlayback = suspended.value();

  auto open = [this](const authoring::AudioSettings& requested,
                     std::unique_ptr<platform::IAudioDevice>& device,
                     std::unique_ptr<platform::MultichannelRingBufferAudioProcessor>&
                         processor) -> core::Result<void> {
    auto& runtime = authoring_->runtime();
    auto nextProcessor =
        std::make_unique<platform::MultichannelRingBufferAudioProcessor>(
            runtime.transport().ringBuffer(),
            std::max<std::size_t>(requested.blockFrames, 4096U), &outputMeter_);
    platform::AudioDeviceConfig config{
        .deviceId = requested.deviceId,
        .sampleRate = requested.sampleRate,
        .blockFrames = requested.blockFrames,
        .outputChannels = requested.outputChannels,
        .applicationName = "Project SEAM",
        .streamName = "Production standalone preview",
    };
    std::unique_ptr<platform::IAudioDevice> nextDevice;
    if (config_.forceThreadedAudio) {
      if (config_.runtimeMode != ProductionRuntimeMode::DeterministicTest) {
        return core::failure(
            core::ErrorCode::InvalidState,
            "Callback-clock audio is restricted to deterministic test mode");
      }
      nextDevice = config_.threadedAudioDeviceFactory
                       ? config_.threadedAudioDeviceFactory()
                       : platform::createThreadedAudioDevice();
    } else {
      if (requested.deviceId == "threaded-callback-clock") {
        return core::failure(
            core::ErrorCode::InvalidArgument,
            "Callback-clock audio cannot be selected outside deterministic tests");
      }
      nextDevice = config_.systemAudioDeviceFactory
                       ? config_.systemAudioDeviceFactory()
                       : platform::createSystemAudioDevice();
    }
    const auto opened = nextDevice->open(config, *nextProcessor);
    if (!opened) return opened;
    processor = std::move(nextProcessor);
    device = std::move(nextDevice);
    return core::success();
  };

  std::optional<core::Error> requestedOpenError;
  const auto runtimeChanged = authoring_->runtime().reconfigureAudio(
      settings.sampleRate, settings.outputChannels, settings.blockFrames);
  if (runtimeChanged) {
    std::unique_ptr<platform::IAudioDevice> nextDevice;
    std::unique_ptr<platform::MultichannelRingBufferAudioProcessor> nextProcessor;
    const auto opened = open(settings, nextDevice, nextProcessor);
    if (opened) {
      audioDevice_ = std::move(nextDevice);
      processor_ = std::move(nextProcessor);
      const auto info = audioDevice_->info();
      authoring_->controller().setAudioState(info.physical, info.backend);
      clearAudioNotice();
      if (resumePlayback) {
        const auto resumed = authoring_->runtime().transport().play();
        if (!resumed) return resumed;
        if (wasRunning &&
            authoring_->runtime().transport().state().available) {
          const auto started = startAudioForPlayback();
          if (!started) return started;
        }
      }
      return core::success();
    }
    requestedOpenError = opened.error();
  }

  if (runtimeChanged) {
    static_cast<void>(authoring_->runtime().reconfigureAudio(
        previous.sampleRate, previous.outputChannels, previous.blockFrames));
  }
  std::optional<core::Error> restorationError;
  if (previousDevice != nullptr) {
    auto restoredProcessor =
        std::make_unique<platform::MultichannelRingBufferAudioProcessor>(
            authoring_->runtime().transport().ringBuffer(),
            std::max<std::size_t>(previous.blockFrames, 4096U), &outputMeter_);
    const auto restored = previousDevice->open(
        platform::AudioDeviceConfig{
            .deviceId = previousDeviceInfo.deviceId,
            .sampleRate = previous.sampleRate,
            .blockFrames = previous.blockFrames,
            .outputChannels = previous.outputChannels,
            .applicationName = "Project SEAM",
            .streamName = "Production standalone preview",
        },
        *restoredProcessor);
    auto started = restored ? core::success()
                            : core::Result<void>{restored.error()};
    if (restored && wasRunning) started = previousDevice->start();
    if (started) {
      processor_ = std::move(restoredProcessor);
      audioDevice_ = std::move(previousDevice);
      const auto info = audioDevice_->info();
      authoring_->controller().setAudioState(info.physical, info.backend);
      clearAudioNotice();
      if (resumePlayback) {
        const auto resumed = authoring_->runtime().transport().play();
        if (!resumed) return resumed;
        if (wasRunning &&
            authoring_->runtime().transport().state().available) {
          const auto restarted = startAudioForPlayback();
          if (!restarted) return restarted;
        }
      }
    } else {
      restorationError = started.error();
    }
  }
  if (restorationError.has_value()) setAudioUnavailable(*restorationError);
  if (!runtimeChanged) return runtimeChanged;
  if (requestedOpenError.has_value()) {
    if (audioDevice_ == nullptr) setAudioUnavailable(*requestedOpenError);
    return core::Result<void>{*requestedOpenError};
  }
  return core::failure(core::ErrorCode::Internal,
                       "Audio settings restart failed without a device error");
}

void NativeEditorApp::setAudioUnavailable(const core::Error& error) noexcept {
  // A notice that is raised now is not the one about a device that did not stop.
  audioStopFailed_ = false;
  raiseAudioNotice("AUDIO_UNAVAILABLE", "audio.unavailable", error);
}

void NativeEditorApp::setAudioStopRefused(const core::Error& error) noexcept {
  raiseAudioNotice("AUDIO_STOP_REFUSED", "audio.stop-refused", error);
}

void NativeEditorApp::raiseAudioNotice(std::string_view code, std::string_view messageKey,
                                       const core::Error& error) noexcept {
  lastError_ = error.message;
  if (!error.context.empty()) lastError_ += ": " + error.context;
  audioDiagnostic_ = authoring::Diagnostic{
      .code = std::string{code},
      .severity = authoring::DiagnosticRegistry::severity(code),
      .messageKey = std::string{messageKey},
      .affectedIds = {},
      .actions = authoring::DiagnosticRegistry::actions(code),
      .occurrenceCount = 1U,
  };
  // An output that could not be opened is not online; one that would not stop is running, which is
  // what it still is, and reporting it offline is what made the old notice read backwards.
  if (authoring_ != nullptr && code == "AUDIO_UNAVAILABLE") {
    authoring_->controller().setAudioState(false, "unavailable");
  }
  if (authoring_ != nullptr) {
    auto diagnostics = authoring_->runtime().diagnostics();
    diagnostics.push_back(*audioDiagnostic_);
    authoring_->controller().setDiagnostics(std::move(diagnostics));
  }
}

void NativeEditorApp::clearAudioNotice() noexcept {
  audioDiagnostic_.reset();
  audioStopFailed_ = false;
}

core::Result<void> NativeEditorApp::refreshSupportReports(
    const std::optional<std::filesystem::path>& preferred) {
  if (supportBundle_ == nullptr || authoring_ == nullptr ||
      supportExportRoot_.empty()) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Support report storage is unavailable");
  }
  auto listed = supportBundle_->listExports(supportExportRoot_);
  if (!listed) return core::Result<void>{listed.error()};
  supportReports_ = std::move(listed).value();
  if (supportReports_.empty()) {
    selectedSupportReportIndex_ = 0U;
    authoring_->controller().setRecoverySupportView({});
    return core::success();
  }
  if (preferred.has_value()) {
    const auto match = std::find_if(
        supportReports_.begin(), supportReports_.end(),
        [&preferred](const auto& report) { return report.path == *preferred; });
    selectedSupportReportIndex_ =
        match == supportReports_.end()
            ? 0U
            : static_cast<std::size_t>(
                  std::distance(supportReports_.begin(), match));
  } else {
    selectedSupportReportIndex_ =
        std::min(selectedSupportReportIndex_, supportReports_.size() - 1U);
  }
  authoring_->controller().setRecoverySupportView(supportReportsView(
      supportReports_, selectedSupportReportIndex_,
      startupCrashMarker_.has_value()));
  return core::success();
}

core::Result<void> NativeEditorApp::selectSupportReport(std::size_t index) {
  if (index >= supportReports_.size() || authoring_ == nullptr) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Support report selection is invalid");
  }
  selectedSupportReportIndex_ = index;
  authoring_->controller().setRecoverySupportView(supportReportsView(
      supportReports_, selectedSupportReportIndex_,
      startupCrashMarker_.has_value()));
  return core::success();
}

std::vector<authoring::DiagnosticAction> NativeEditorApp::exportedSupportActions() const {
  const auto registered = authoring::DiagnosticRegistry::actions("SUPPORT_BUNDLE_EXPORTED");
  if (!supportIntakeDestination_.empty()) return registered;
  // With no intake destination configured there is nowhere to submit to, so the button is withheld
  // instead of being shown as something that can only ever refuse.
  std::vector<authoring::DiagnosticAction> actions;
  for (const auto action : registered) {
    if (action != authoring::DiagnosticAction::SubmitSupportBundle) {
      actions.push_back(action);
    }
  }
  return actions;
}

// The platform token the update manifest vocabulary uses. Kept as a named constant rather than a
// literal at the call site so the expected value and the shipped manifest cannot drift apart.
constexpr std::string_view kExpectedUpdatePlatform{"macos-arm64"};

void NativeEditorApp::refreshDistributionAuthority() {
  // A missing or unverifiable manifest leaves the authority unknown rather than assumed good:
  // this build ships without a configured update channel, and claiming a verified pause from
  // nothing would be the same error in the opposite direction.
  native_ui::DistributionAuthorityView view;
  if (updatePolicyPath_.empty() || updateManifestPath_.empty() || !trustedUpdateRoot_.has_value()) {
    view.known = false;
    view.diagnostic = "No update channel is configured for this build";
    authoring_->controller().setDistributionAuthorityView(std::move(view));
    return;
  }
  auto controller = standalone::UpdateController::create(standalone::UpdateControllerConfig{
      .statePath = supportExportRoot_ / "update-state.json",
      .stagingRoot = supportExportRoot_ / "UpdateStaging",
      .expectedPlatform = std::string{kExpectedUpdatePlatform},
      .installedVersion = std::string{build::kApplicationVersion},
      .verificationTime = {},
      .trustedRoot = trustedUpdateRoot_});
  if (!controller) {
    view.known = false;
    view.diagnostic = controller.error().message;
    authoring_->controller().setDistributionAuthorityView(std::move(view));
    return;
  }
  const auto checked = controller.value()->check(updatePolicyPath_, updateManifestPath_);
  if (!checked) {
    view.known = false;
    view.diagnostic = checked.error().message;
    authoring_->controller().setDistributionAuthorityView(std::move(view));
    return;
  }
  view.known = true;
  view.diagnostic = checked.value().diagnostic;
  if (checked.value().manifest.has_value())
    view.minimumBuild = checked.value().manifest->minimumBuild;
  // A pause is reported as a pause rather than a generic failure, so a creator can tell an
  // operator decision apart from a corrupt or unsigned manifest. Both strings are the controller's
  // own diagnostics, so this classification cannot drift from the rule that produced it.
  view.paused = view.diagnostic.find("paused") != std::string::npos;
  view.supportedBuild = view.diagnostic.find("minimum supported build") == std::string::npos;
  authoring_->controller().setDistributionAuthorityView(std::move(view));
}

void NativeEditorApp::refreshCrashRecoveryContext() {
  if (crashCapture_ == nullptr || authoring_ == nullptr) return;
  platform::CrashRecoveryContext context{
      .candidateId = std::string{build::kBuildId},
      .host = "standalone:" +
              std::string{platform::crashCaptureBackendName()},
      .audioUnderflowFrames = processorStats().underflowFrames,
      .audioXruns = audioStats().xruns,
  };
  const auto& tracks = authoring_->runtime()
                           .document()
                           .session()
                           .project()
                           .vocalTracks();
  const auto bank = std::find_if(tracks.begin(), tracks.end(), [](const auto& track) {
    return !track.voicebank.id.empty() || !track.voicebank.version.empty() ||
           !track.voicebank.contentHash.empty();
  });
  if (bank != tracks.end()) {
    context.bankId = bank->voicebank.id;
    context.bankVersion = bank->voicebank.version;
    context.bankContentHash = bank->voicebank.contentHash;
  }
  if (crashRecoveryContext_.has_value() &&
      *crashRecoveryContext_ == context) {
    return;
  }
  const auto updated = crashCapture_->updateContext(context);
  if (updated) {
    crashRecoveryContext_ = std::move(context);
  } else {
    lastError_ = updated.error().message;
  }
}

core::Result<void> NativeEditorApp::handleDiagnosticAction(
    const authoring::Diagnostic& diagnostic,
    authoring::DiagnosticAction action) {
  switch (action) {
    case authoring::DiagnosticAction::Dismiss:
      if (diagnostic.code == "INSTALL_DURABILITY_UNCONFIRMED") {
        // Copy before rebuilding: the action may refer to the current panel's storage.
        const auto dismissed = diagnostic;
        std::erase_if(installationDiagnostics_, [&](const auto& held) { return held.sameIssueAs(dismissed); });
        auto diagnostics = authoring_->runtime().diagnostics();
        diagnostics.insert(diagnostics.end(), installationDiagnostics_.begin(), installationDiagnostics_.end());
        if (audioDiagnostic_) diagnostics.push_back(*audioDiagnostic_);
        if (rendererChangedDiagnostic_) diagnostics.push_back(*rendererChangedDiagnostic_);
        authoring_->controller().setDiagnostics(std::move(diagnostics));
        requestWindowRepaint();
        return core::success();
      }
      if (diagnostic.code == "CRASH_RECOVERY_AVAILABLE" && crashCapture_ != nullptr) {
        static_cast<void>(crashCapture_->clearMarker());
        startupCrashMarker_.reset();
      }
      if (diagnostic.code == "SUPPORT_BUNDLE_PREVIEW_READY") {
        pendingSupportBundle_.reset();
      }
      if (diagnostic.code == "SUPPORT_BUNDLE_PREVIEW_READY" ||
          diagnostic.code == "SUPPORT_BUNDLE_EXPORTED") {
        authoring_->controller().setRecoverySupportView({});
      }
      // The renderer notice is about a record in the document, so dismissing it dismisses this
      // telling of it. The record stays, and the notice returns if the difference changes into
      // something the creator has not seen yet.
      if (diagnostic.code == "RENDERER_CHANGED") {
        dismissedRendererDifference_ = diagnostic.detail;
        rendererChangedDiagnostic_.reset();
        authoring_->controller().setDiagnostics({});
        requestWindowRepaint();
        return core::success();
      }
      authoring_->runtime().clearDiagnostics();
      authoring_->controller().setDiagnostics({});
      requestWindowRepaint();
      return core::success();
    case authoring::DiagnosticAction::Retry:
      authoring_->runtime().requestPreview(true);
      requestWindowRepaint();
      return core::success();
    case authoring::DiagnosticAction::RecoverAutosave:
      if (applicationController_ == nullptr) {
        return core::failure(core::ErrorCode::InvalidState,
                             "Recovery is unavailable before application setup");
      }
      return applicationController_->dispatch(
          platform::ApplicationCommand::RecoverLatestAutosave);
    case authoring::DiagnosticAction::OpenSupport: {
      if (supportExportRoot_.empty() || supportBundle_ == nullptr) {
        return core::failure(core::ErrorCode::InvalidState,
                             "Support export root is unavailable");
      }
      refreshCrashRecoveryContext();
      const auto crashReport = diagnostic.code == "CRASH_RECOVERY_AVAILABLE";
      auto candidateId = std::string{build::kBuildId};
      if (crashReport) {
        if (!startupCrashMarker_.has_value() ||
            !startupCrashMarker_->contextAvailable ||
            startupCrashMarker_->context.candidateId.empty()) {
          return core::failure(
              core::ErrorCode::Conflict,
              "Crash report cannot be exported without its exact candidate identity");
        }
        candidateId = startupCrashMarker_->context.candidateId;
      }
      const auto level = diagnostic.severity == authoring::DiagnosticSeverity::Critical
                             ? core::LogLevel::Error
                             : diagnostic.severity == authoring::DiagnosticSeverity::Warning
                                   ? core::LogLevel::Warning
                                   : core::LogLevel::Error;
      std::vector<core::LogField> fields{
          {"messageKey", diagnostic.messageKey,
           core::LogPrivacyClass::ExportSafe},
      };
      if (crashReport) {
        const auto& marker = *startupCrashMarker_;
        const auto add = [&fields](std::string key, std::string value) {
          fields.push_back(core::LogField{
              .key = std::move(key),
              .value = std::move(value),
              .privacy = core::LogPrivacyClass::PublicTechnical,
          });
        };
        add("recoveryState", marker.code);
        add("crashPlatformCode", std::to_string(marker.platformCode));
        add("buildId", marker.context.candidateId);
        add("bankId", marker.context.bankId);
        add("bankVersion", marker.context.bankVersion);
        add("bankContentHash", marker.context.bankContentHash);
        add("hostFamily", marker.context.host);
        add("underflowFrames",
            std::to_string(marker.context.audioUnderflowFrames));
        add("xrunCount", std::to_string(marker.context.audioXruns));
      }
      const authoring::SupportBundleRequest request{
          .events = {core::LogEvent{
              .code = diagnostic.code,
              .level = level,
              .category = "diagnostic",
              .message = diagnostic.messageKey,
              .fields = std::move(fields),
              .occurrenceCount = diagnostic.occurrenceCount}},
          .attachments = {},
          .candidateId = std::move(candidateId),
          .createdAt = {}};
      auto prepared = supportBundle_->prepare(request);
      if (!prepared) return core::Result<void>{prepared.error()};
      pendingSupportBundle_ = std::move(prepared).value();
      authoring_->controller().setRecoverySupportView(supportPreviewView(
          *pendingSupportBundle_, startupCrashMarker_.has_value()));
      authoring_->controller().setDiagnostics({authoring::Diagnostic{
          .code = "SUPPORT_BUNDLE_PREVIEW_READY",
          .severity = authoring::DiagnosticSeverity::Info,
          .messageKey = "support.preview-ready",
          .affectedIds = {},
          .actions = authoring::DiagnosticRegistry::actions(
              "SUPPORT_BUNDLE_PREVIEW_READY"),
          .occurrenceCount = 1U}});
      requestWindowRepaint();
      return core::success();
    }
    case authoring::DiagnosticAction::ExportSupportBundle: {
      if (supportBundle_ == nullptr || !pendingSupportBundle_.has_value()) {
        return core::failure(core::ErrorCode::InvalidState,
                             "No reviewed support report is ready to export");
      }
      auto exported = supportBundle_->exportPrepared(*pendingSupportBundle_,
                                                     supportExportRoot_);
      if (!exported) return core::Result<void>{exported.error()};
      lastError_ = exported.value().destination.string();
      pendingSupportBundle_.reset();
      authoring_->controller().setRecoverySupportView({});
      const auto reports = refreshSupportReports(exported.value().destination);
      if (!reports) {
        supportReports_ = {authoring::SupportBundleRecord{
            .path = exported.value().destination,
            .bytes = exported.value().preview.archiveBytes,
            .sha256 = exported.value().preview.archiveSha256,
        }};
        selectedSupportReportIndex_ = 0U;
        authoring_->controller().setRecoverySupportView(supportReportsView(
            supportReports_, selectedSupportReportIndex_,
            startupCrashMarker_.has_value()));
        lastError_ = "Support report was exported, but the report directory "
                     "could not be listed: " +
                     reports.error().message;
      }
      authoring_->controller().setDiagnostics({authoring::Diagnostic{
          .code = "SUPPORT_BUNDLE_EXPORTED",
          .severity = authoring::DiagnosticSeverity::Info,
          .messageKey = "support.exported-local-only",
          .affectedIds = {},
          .actions = exportedSupportActions(),
          .occurrenceCount = 1U}});
      requestWindowRepaint();
      return core::success();
    }
    case authoring::DiagnosticAction::OpenSupportFolder:
      if (supportExportRoot_.empty()) {
        return core::failure(core::ErrorCode::InvalidState,
                             "Support export folder is unavailable");
      }
      return platform::openExternalPath(supportExportRoot_);
    case authoring::DiagnosticAction::SubmitSupportBundle: {
      if (supportBundle_ == nullptr || supportReports_.empty() ||
          selectedSupportReportIndex_ >= supportReports_.size()) {
        return core::failure(core::ErrorCode::InvalidState,
                             "No exported support report is available to submit");
      }
      // Defence in depth behind exportedSupportActions(), which withholds this action from a build
      // with no destination. Reached directly, it refuses rather than recording a submission to
      // somewhere that does not exist.
      if (supportIntakeDestination_.empty()) {
        return core::failure(core::ErrorCode::InvalidState,
                             "This build has no configured support intake destination");
      }
      // Submitting records that the bundle was handed to a named destination. It does NOT mark the
      // bundle received: that is the intake endpoint's acknowledgement, applied separately.
      const auto submitted = supportBundle_->recordIntake(
          supportReports_[selectedSupportReportIndex_], supportIntakeDestination_,
          timestampNow());
      if (!submitted) return core::Result<void>{submitted.error()};
      lastError_ = "Support report submitted for acknowledgement: " +
                   submitted.value().submissionId;
      authoring_->controller().setDiagnostics({authoring::Diagnostic{
          .code = "SUPPORT_BUNDLE_SUBMITTED",
          .severity = authoring::DiagnosticSeverity::Info,
          .messageKey = "support.submitted-awaiting-acknowledgement",
          .affectedIds = {},
          .actions = authoring::DiagnosticRegistry::actions(
              "SUPPORT_BUNDLE_SUBMITTED"),
          .occurrenceCount = 1U}});
      requestWindowRepaint();
      return core::success();
    }
    case authoring::DiagnosticAction::DeleteSupportBundle:
      if (supportBundle_ == nullptr || supportReports_.empty() ||
          selectedSupportReportIndex_ >= supportReports_.size()) {
        return core::failure(core::ErrorCode::InvalidState,
                             "No owned support report is available to delete");
      }
      {
        const auto deleted = supportBundle_->deleteExport(
            supportReports_[selectedSupportReportIndex_], supportExportRoot_);
        if (!deleted) return deleted;
      }
      {
        const auto reports = refreshSupportReports();
        if (!reports) return reports;
      }
      if (supportReports_.empty()) {
        authoring_->controller().setDiagnostics({});
      }
      requestWindowRepaint();
      return core::success();
    case authoring::DiagnosticAction::ChooseVoicebank:
      authoring_->controller().showVoicebankBrowser();
      return core::success();
    case authoring::DiagnosticAction::RelinkVoicebank:
      if (applicationController_ == nullptr) {
        return core::failure(core::ErrorCode::InvalidState,
                             "Voicebank relink is unavailable");
      }
      return applicationController_->relinkVoicebankFromDialog();
    case authoring::DiagnosticAction::InstallVoicebank:
      if (applicationController_ == nullptr) {
        return core::failure(core::ErrorCode::InvalidState,
                             "Voicebank installation is unavailable");
      }
      return applicationController_->dispatch(
          platform::ApplicationCommand::InstallVoicebank);
    case authoring::DiagnosticAction::SaveAs:
      if (applicationController_ == nullptr) {
        return core::failure(core::ErrorCode::InvalidState,
                             "Save As is unavailable before application setup");
      }
      return applicationController_->dispatch(
          platform::ApplicationCommand::SaveProjectAs);
    case authoring::DiagnosticAction::OpenRecoveryFolder:
      if (recoveryRoot_.empty()) {
        return core::failure(core::ErrorCode::InvalidState,
                             "Recovery folder is unavailable");
      }
      {
        std::error_code error;
        std::filesystem::create_directories(recoveryRoot_, error);
        if (error) {
          return core::failure(core::ErrorCode::IoError,
                               "Unable to create recovery folder",
                               error.message());
        }
      }
      return platform::openExternalPath(recoveryRoot_);
    case authoring::DiagnosticAction::CopyDiagnostic: {
      std::string text = "Project SEAM diagnostic\n";
      text += "Code: " + diagnostic.code + "\n";
      text += "Message: " + diagnostic.messageKey + "\n";
      if (!diagnostic.detail.empty()) {
        text += "Detail";
        if (diagnostic.detailTruncated) text += " [truncated]";
        if (diagnostic.detailEscaped) text += " [escaped bytes]";
        text += ": " + diagnostic.detail + "\n";
      }
      text += "Occurrences: " +
              std::to_string(diagnostic.occurrenceCount) + "\n";
      if (!diagnostic.affectedIds.empty()) {
        text += "Affected IDs:\n";
        for (const auto& id : diagnostic.affectedIds) {
          text += "- " + id + "\n";
        }
      }
      return platform::copyTextToClipboard(text);
    }
    case authoring::DiagnosticAction::OpenSettings:
      authoring_->controller().showAudioSettings();
      return core::success();
    case authoring::DiagnosticAction::RelinkMedia:
      if (applicationController_ == nullptr) {
        return core::failure(core::ErrorCode::InvalidState,
                             "Backing media relink is unavailable");
      }
      return applicationController_->relinkBackingMediaFromDialog();
  }
  return core::failure(core::ErrorCode::Unsupported,
                       "Unknown diagnostic action");
}

void NativeEditorApp::setWindow(native_ui::INativeWindow& window) noexcept {
  std::lock_guard lock(windowMutex_);
  window_ = &window;
}

void NativeEditorApp::detachWindow() noexcept {
  // Envelope workers first: stop() joins them, and a finishing worker's repaint request takes
  // windowMutex_, which is not held here yet.
  waveforms_.stop();
  std::lock_guard lock(windowMutex_);
  window_ = nullptr;
}

void NativeEditorApp::requestWindowRepaint() const noexcept {
  std::lock_guard lock(windowMutex_);
  if (window_ != nullptr) window_->requestRepaint();
}

core::Result<void> NativeEditorApp::writeUiEvidence(const std::filesystem::path& dir) {
  const auto deviceScale = lastPaintScale_;
  if (authoring_ == nullptr || !shell_.presentedLastFrame())
    return core::failure(core::ErrorCode::InvalidState,
                         "UI evidence needs a frame the SING shell presented");
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  if (error) return core::failure(core::ErrorCode::IoError, "Cannot create the evidence folder", dir.string());
  auto& controller = authoring_->controller();
  controller.rebuildAccessibilityTree();
  shell_.rebuildSemantics(controller, controller.sceneState());
  std::vector<double> sorted = paintMillis_;
  std::sort(sorted.begin(), sorted.end());
  const auto percentile = [&sorted](double p) {
    if (sorted.empty()) return 0.0;
    const auto index = static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1U));
    return sorted[index];
  };
  formats::JsonValue::Object memory{{"scope", "whole standalone process (phys_footprint)"}};
#if defined(__APPLE__)
  task_vm_info_data_t info{};
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) ==
      KERN_SUCCESS) {
    memory.emplace("physFootprintBytes", static_cast<std::int64_t>(info.phys_footprint));
    memory.emplace("physFootprintPeakBytes", static_cast<std::int64_t>(info.ledger_phys_footprint_peak));
  }
#endif
  const formats::JsonValue performance = formats::JsonValue::Object{
      {"schema", "seam-ui-performance-evidence-v1"},
      {"source", "NativeEditorApp::paint wall time for every frame of this run"},
      {"frames", static_cast<std::int64_t>(sorted.size())},
      {"paintMillis",
       formats::JsonValue::Object{{"p50", percentile(0.5)},
                                  {"p95", percentile(0.95)},
                                  {"max", sorted.empty() ? 0.0 : sorted.back()}}},
      {"memory", std::move(memory)},
  };
  const auto write = [&dir](std::string_view name, const formats::JsonValue& value) -> core::Result<void> {
    std::ofstream out(dir / std::string{name}, std::ios::binary | std::ios::trunc);
    out << formats::stringifyJson(value, true) << '\n';
    if (!out) return core::failure(core::ErrorCode::IoError, "Cannot write UI evidence", std::string{name});
    return core::success();
  };
  if (auto written = write("geometry.json", native_ui::design::singLayoutEvidence(shell_, deviceScale)); !written)
    return written;
  if (auto written = write("semantic-bounds.json", native_ui::design::semanticEvidence(shell_.accessibilityTree()));
      !written)
    return written;
  const auto& registered = native_ui::paint::bundledFonts();
  formats::JsonValue::Object roles;
  constexpr std::array<std::string_view, 7U> roleNames{
      "Ui", "UiMedium", "UiSemibold", "UiBold", "Mono", "Display", "DisplayRounded"};
  for (std::size_t index = 0; index < roleNames.size(); ++index) {
    const auto role = static_cast<native_ui::paint::FontRole>(index);
    roles.emplace(std::string{roleNames[index]}, formats::JsonValue::Object{
        {"postScriptName", native_ui::paint::fontFaceName(role)},
        {"file", native_ui::paint::fontFaceFile(role).string()},
        {"source", registered.face[index].empty() ? "system" : "bundled"}});
  }
  formats::JsonValue::Array refused;
  for (const auto& reason : registered.refused) refused.emplace_back(reason);
  if (auto written = write("font-identity.json", formats::JsonValue::Object{
          {"selectedDirectory", registered.directory.string()},
          {"roles", std::move(roles)}, {"refused", std::move(refused)}}); !written)
    return written;
  return write("performance.json", performance);
}


void NativeEditorApp::shutdownAudio() noexcept {
  // A device that does not say that it has stopped is destroyed by the destructor next, before the
  // authoring runtime that owns the ring it reads, and its own destructor makes a last attempt.
  static_cast<void>(stopAudioForPlayback());
  if (authoring_ != nullptr) {
    static_cast<void>(authoring_->runtime().transport().pause());
  }
}

core::Result<void> NativeEditorApp::openProject(
    const std::filesystem::path& path) {
  if (applicationController_ == nullptr) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Project lifecycle is not initialized");
  }
  const auto opened = applicationController_->openRecent(path);
  record(opened);
  return opened;
}

void NativeEditorApp::openProjectPath(
    const std::filesystem::path& path) noexcept {
  static_cast<void>(openProject(path));
}

std::optional<std::filesystem::path> NativeEditorApp::documentPath()
    const noexcept {
  if (authoring_ == nullptr) return std::nullopt;
  return authoring_->runtime().document().identity().projectPath;
}

void NativeEditorApp::paint(native_ui::RasterCanvas& canvas) noexcept {
  if (authoring_ == nullptr) return;
  const auto paintStarted = std::chrono::steady_clock::now();
  struct PaintTimer final {
    std::vector<double>& samples;
    std::chrono::steady_clock::time_point started;
    ~PaintTimer() {
      if (samples.size() >= 4096U) samples.erase(samples.begin(), samples.begin() + 1024);
      samples.push_back(
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    }
  } paintTimer{paintMillis_, paintStarted};
  lastPaintScale_ = canvas.scale();
  authoring_->controller().pollReplacementReview();
  if (applicationController_ != nullptr) {
    record(applicationController_->tickAutosave());
    // Proposal generation only reads its captured score on the worker. Adoption
    // and stale-document validation belong here, on the editor's owner thread.
    const auto proposal =
        applicationController_->applyPendingAutomaticPerformanceProposal();
    if (!proposal) record(proposal.error());
    // A background export committed on its own thread and reported the renderer it used instead of
    // editing the document there. This is the owner thread, so the record becomes an ordinary edit
    // here, once, and only for an export that actually committed.
    const auto applied = applicationController_->applyPendingRendererProvenance();
    if (!applied) record(applied.error());
  }
  // A device can stop on its own (it is unplugged, or the system takes it away). The transport
  // cannot see that, and is told once per frame, before it is asked what to do about the device.
  reportConsumerToTransport();
  const auto transport = authoring_->runtime().transport().state();
  const auto decidedAction = decideDeviceAction(
      transport, authoring_->runtime().transport().ringBuffer().availableReadFrames(),
      audioDevice_ != nullptr, audioDevice_ != nullptr && audioDevice_->running());
  // A Start that failed holds the next one back (see DeviceStartRetry). The frames that come meanwhile,
  // for whatever reason, neither ask the device nor move the deadline.
  const auto deviceAction = deviceRetry_.gate(decidedAction, uiNow());
  auto deviceActionFailed = false;
  switch (deviceAction) {
    case DeviceAction::Stop:
      // A device that does not say that it has stopped is asked again by the next frame (it runs on,
      // and a running device keeps the window painting), and the creator has been told by the notice
      // that asking raised.
      deviceActionFailed = !stopAudioForPlayback();
      break;
    case DeviceAction::Start: {
      const auto started = startAudioForPlayback();
      if (!started) {
        record(started);
        deviceActionFailed = true;
      }
      break;
    }
    case DeviceAction::None:
      break;
  }
  // A window that paints only on request runs no frame that nothing asked for, and what a frame does
  // for the device goes on over frames: while there is more to do, this one asks for the next, and a
  // start that failed is asked again by its deadline. The time of the failure is read now, after the
  // attempt, which can take a while.
  deviceRetry_.afterFrame(transport, audioDevice_ != nullptr, decidedAction, deviceAction,
                          deviceActionFailed, uiNow());
  // A report that is unsettled is one the feeder has not applied the creator's last command to. It is
  // ordinarily a millisecond behind, so the frame that waits for it is asked at once; a feeder that
  // has stopped leaves it unsettled for ever, and asking at the display's rate would spin a window
  // that nothing else asks for (see kTransportSettleWait).
  const auto settledNow = uiNow();
  if (transport.settled) unsettledSince_.reset();
  else if (!unsettledSince_.has_value()) unsettledSince_ = settledNow;
  // The report settled again (the feeder is back, or a command of its own has been applied): a hold
  // whose deadline went by while the feeder was stopped is offered at once, because a frame painted
  // for a settled report can act on it and the creator's Play should not wait out a hold that ended
  // before the device ever saw it.
  if (transport.settled && deviceRetry_.notBefore().has_value() &&
      *deviceRetry_.notBefore() <= settledNow) {
    deviceRetry_.clear();
  }
  const auto unsettledFor = unsettledSince_.has_value()
                                ? settledNow - *unsettledSince_
                                : std::chrono::steady_clock::duration::zero();
  if (deviceNeedsFrame(transport, audioDevice_ != nullptr,
                       audioDevice_ != nullptr && audioDevice_->running(), unsettledFor)) {
    requestWindowRepaint();
  } else if (unsettledSince_.has_value() &&
             unsettledFor >= std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                  kTransportSettleWait)) {
    // The wait is over and the report is still unsettled: the feeder has stopped. The frame is owed
    // again one wait on rather than not at all (a Play the feeder never acknowledged is still the
    // creator's), and re-basing here is what makes that a bounded rate instead of the display's.
    unsettledSince_ = settledNow;
  }
  const auto progress = authoring_->runtime().renderer().progress();
  authoring_->controller().setPlaying(transport.playing);
  authoring_->controller().setLoopEnabled(transport.loop.enabled);
  authoring_->controller().setRenderStatus(native_ui::RenderStatusView{
      .state = renderStatusState(progress.state),
      .requestedRevision = progress.requestedRevision,
      .audibleRevision = transport.publishedRevision,
      .requestedQuality = progress.requestedQuality,
      .audibleQuality = progress.publishedQuality,
      .completedPhrases = progress.completedPhrases,
      .totalPhrases = progress.totalPhrases,
      .fraction = progress.fraction,
      .hasAudibleAudio = transport.available,
      .audibleAudioStale = progress.audibleAudioStale,
      .diagnostic = progress.diagnostic,
      .activeVoicebankId = progress.activeVoicebankId,
      .activeVoicebankVersion = progress.activeVoicebankVersion,
      .activeRenderer = progress.activeRenderer,
  });
  const auto activeAudio = audioInfo();
  // The notice that the audio is unavailable (a start that failed, a stop that was refused) says so
  // here as well, as setAudioUnavailable gave it. A frame that put the device's own state back would
  // be undone by the next failure and then set again by the next frame, and each change asks for a
  // frame: a device that cannot start would keep a window that paints only on request painting.
  const auto audioUp = audioDevice_ != nullptr && !audioDiagnostic_.has_value();
  const auto audioOnline = audioUp && activeAudio.physical;
  const auto audioBackend = audioUp ? activeAudio.backend : std::string{"unavailable"};
  const auto currentScene = authoring_->controller().sceneState();
  if (currentScene.audioDeviceOnline != audioOnline ||
      currentScene.audioBackend != audioBackend) {
    authoring_->controller().setAudioState(audioOnline, audioBackend);
  }
  auto diagnostics = authoring_->runtime().diagnostics();
  if (audioDiagnostic_.has_value()) diagnostics.push_back(*audioDiagnostic_);
  // A record that this project sounded different asks the creator a question, so it is raised where
  // the project is being looked at rather than only stored in a file nobody reads.
  if (applicationController_ != nullptr) {
    const auto provenance = applicationController_->rendererProvenance();
    if (provenance.state == domain::RendererProvenance::Changed &&
        provenance.difference != dismissedRendererDifference_) {
      authoring::Diagnostic notice{
          .code = "RENDERER_CHANGED",
          .severity = authoring::DiagnosticRegistry::severity("RENDERER_CHANGED"),
          .messageKey = "render.renderer_changed",
          .affectedIds = {},
          .actions = authoring::DiagnosticRegistry::actions("RENDERER_CHANGED"),
          .occurrenceCount = 1U,
      };
      // The detail names the field that differs, so the creator can tell a renderer change from an
      // accidental edit instead of being told only that something is different.
      notice.setDetail(provenance.difference.empty()
                           ? std::string{"recorded renderer differs from this build"}
                           : provenance.difference);
      rendererChangedDiagnostic_ = std::move(notice);
    } else {
      rendererChangedDiagnostic_.reset();
      // Re-arm dismissal once the project's record matches this build again, so a later change is a
      // new event rather than one the creator silenced by accident.
      if (provenance.state != domain::RendererProvenance::Changed) {
        dismissedRendererDifference_.clear();
      }
    }
  }
  if (rendererChangedDiagnostic_.has_value()) {
    diagnostics.push_back(*rendererChangedDiagnostic_);
  }
  diagnostics.insert(diagnostics.end(), installationDiagnostics_.begin(), installationDiagnostics_.end());
  authoring_->controller().setDiagnostics(std::move(diagnostics));
  const auto& project = authoring_->runtime().document().session().project();
  const auto tick = project.tempoMap().tickAtSampleFrame(
      transport.audiblePlayhead, authoring_->runtime().transport().sampleRate());
  if (applicationController_ != nullptr) {
    authoring_->controller().setExportProgress(
        applicationController_->exportProgress().progress());
    // The cards are copied field by field out of the browser model, and a window that paints at the
    // display's rate while the device plays pays for that on every one of those frames. The panel is
    // made of the cards alone, so an equal list changes nothing drawn and nothing answered: the same
    // rule setDiagnostics uses for the notices.
    const auto& cards = applicationController_->voicebankCards();
    if (!publishedVoicebankCards_.has_value() || *publishedVoicebankCards_ != cards) {
      authoring_->controller().setVoicebankCards(cards);
      publishedVoicebankCards_ = cards;
    }
    authoring_->controller().setLastExport(
        applicationController_->lastExport());
  }
  if (const auto settings = audioSettings(); settings) {
    authoring_->controller().setAudioSettings(
        settings.value(), audioDeviceList(settings.value()), processorStats().underflowFrames,
        audioStats().xruns);
  }
  // The output meter reads what the audio thread measured in the blocks the device received. A
  // stopped or missing device publishes nothing, so the meter shows its empty scale. The window
  // keeps repainting while the device runs (see deviceNeedsFrame), so the hold and decay move.
  if (auto reading = outputMeter_.read(audioDevice_ != nullptr && audioDevice_->running(),
                                       std::chrono::steady_clock::now())) {
    authoring_->controller().setOutputLevel(native_ui::EditorSceneState::OutputLevel{
        .peak = std::move(reading->peak),
        .hold = std::move(reading->hold),
        // The editor plays its master mix to the device's first channels; there is no output
        // pair selection to name.
        .bus = "Master",
        .clipped = reading->clipped,
    });
  } else {
    authoring_->controller().setOutputLevel(std::nullopt);
  }
  // The surface and its geometry are chosen before the scene state is derived from them.
  const auto shellFrame = shell_.prepareFrame(authoring_->controller(), canvas.logicalWidth(),
                                              canvas.logicalHeight());
  auto state = authoring_->controller().sceneState();
  state.playheadPixel =
      authoring_->controller().pianoRoll().timeline().tickToPixel(tick);
  // An edit that lands on the playhead needs the tick, not the pixel.
  authoring_->controller().setPlayheadTick(tick);
  if (progress.state == authoring::RenderState::Queued ||
      progress.state == authoring::RenderState::Rendering ||
      progress.state == authoring::RenderState::Stale) {
    requestWindowRepaint();
  }
  character_.setDisplayMode(state.characterMode);
  character_.setState(state.characterState);
  // The dock follows the phrase the current render published. A rebuild clears whatever was showing
  // first, so a new render that published nothing closes the mouth instead of leaving the previous
  // phrase's mouth on screen.
  // Evaluation advances the generation. Reading only the counter here would be
  // circular: nothing else in the paint path evaluates a newly published render.
  const auto* publishedPerformance = authoring_->characterPerformance();
  if (boundPerformanceGeneration_ != authoring_->characterPerformanceGeneration()) {
    boundPerformanceGeneration_ = authoring_->characterPerformanceGeneration();
    character_.clearPerformanceSnapshot();
    if (publishedPerformance != nullptr) {
      const auto followed = character_.followSinger(
          character::performanceBindingKey(*publishedPerformance));
      if (followed) {
        static_cast<void>(character_.setPerformanceSnapshot(*publishedPerformance));
      }
    }
  }
  // The audio phrase may be valid while this artwork belongs to another bank. Only a successfully
  // bound presentation for the currently verified voice identity may animate the dock.
  state.characterPerformance.reset();
  state.characterMouth = nullptr;
  state.characterMouthPlacement.reset();
  state.characterVoiceStyle.clear();
  state.characterScorePitchRange.clear();
  if (state.voiceIdentity.characterActive && character_.hasPerformanceSnapshot()) {
    if (const auto* snapshot = character_.performanceSnapshot(); snapshot != nullptr) {
      state.characterVoiceStyle = snapshot->style;
      if (snapshot->scorePitchRange.has_value())
        state.characterScorePitchRange = character::scorePitchRangeLabel(*snapshot->scorePitchRange);
    }
    if (const auto frame = authoring_->characterPerformanceFrameAt(tick); frame.has_value()) {
      state.characterPerformance = native_ui::EditorSceneState::CharacterPerformanceView{
          .mouth = frame->mouth,
          .energy = frame->energy,
          .expression = frame->expression,
          .performing = frame->performing,
          .audibleStale = authoring_->characterPerformanceStale(),
          .reducedMotion = platform::currentAccessibilityPreferences().reduceMotion,
          .voiceStyle = state.characterVoiceStyle,
          .scorePitchRange = state.characterScorePitchRange,
      };
      state.characterMouth = character_.mouth(frame->mouth);
      if (state.characterMouth != nullptr)
        state.characterMouthPlacement = character_.mouthPlacement();
    }
  }
  // The native accessibility tree is rebuilt from the controller's own read model, not from the
  // local paint state. Publish the same verified phrase (or clear it) before rebuilding that tree.
  if (state.characterPerformance.has_value())
    authoring_->controller().setCharacterPerformance(*state.characterPerformance);
  else
    authoring_->controller().clearCharacterPerformance();
  state.characterPortrait = character_.portrait();
  // Layout asks the package, not the decoded frame, so the dock cannot appear or vanish because the
  // render status changed which state's artwork is being shown.
  state.characterDockReserved = character_.dockVisible(state.characterMode);
  authoring_->controller().setCharacterPortrait(state.characterPortrait);
  if (state.characterName.empty()) state.characterName = character_.displayName();
  if (state.characterStyle.empty()) state.characterStyle = character_.styleName();
  authoring_->controller().rebuildAccessibilityTree();
  shellPresentedFrame_ = shellFrame && shell_.paint(canvas, authoring_->controller(), state, tick);
  // The shell is the only editor surface; without the vector backend the window says so.
  if (!shellPresentedFrame_) native_ui::paintEditorUnavailable(canvas);
  else
    shell_.rebuildSemantics(authoring_->controller(), state);
}

native_ui::FrameDamage NativeEditorApp::paintFrame(native_ui::RasterCanvas& canvas) noexcept {
  shellPresentedFrame_ = false;
  paint(canvas);
  return shellPresentedFrame_ ? shell_.lastFrameDamage() : native_ui::FrameDamage::everything();
}

void NativeEditorApp::resized(double logicalWidth, double logicalHeight,
                              double) noexcept {
  authoring_->controller().resize(logicalWidth, logicalHeight);
  // The shell's layout follows the size at once (see EditorRuntime::resize).
  static_cast<void>(shell_.prepareFrame(authoring_->controller(), logicalWidth, logicalHeight));
}

void NativeEditorApp::pointerDown(
    const native_ui::PointerEvent& event) noexcept {
  record(shell_.pointerDown(authoring_->controller(), event));
}
void NativeEditorApp::pointerMove(
    const native_ui::PointerEvent& event) noexcept {
  record(shell_.pointerMove(authoring_->controller(), event));
}
void NativeEditorApp::pointerUp(
    const native_ui::PointerEvent& event) noexcept {
  record(shell_.pointerUp(authoring_->controller(), event));
}
void NativeEditorApp::scroll(double deltaX, double deltaY, ui::Point anchor,
                             native_ui::InputModifiers modifiers) noexcept {
  if (!shell_.scroll(authoring_->controller(), deltaX, deltaY, anchor, modifiers))
    authoring_->controller().scroll(deltaX, deltaY, anchor, modifiers);
}
void NativeEditorApp::keyDown(const native_ui::KeyEvent& event) noexcept {
  if (shell_.handleShellKey(authoring_->controller(), event)) return;
  if (applicationController_ != nullptr && event.modifiers.primaryShortcut()) {
    std::optional<platform::ApplicationCommand> command;
    if (event.key == native_ui::NativeKey::N) {
      command = platform::ApplicationCommand::NewProject;
    } else if (event.key == native_ui::NativeKey::O) {
      command = platform::ApplicationCommand::OpenProject;
    } else if (event.key == native_ui::NativeKey::S) {
      command = event.modifiers.shift
                    ? platform::ApplicationCommand::SaveProjectAs
                    : platform::ApplicationCommand::SaveProject;
    } else if (event.key == native_ui::NativeKey::E) {
      command = platform::ApplicationCommand::ExportSet;
    } else if (event.key == native_ui::NativeKey::Q) {
      command = platform::ApplicationCommand::Quit;
    }
    if (command.has_value()) {
      record(applicationController_->dispatch(*command));
      return;
    }
  }
  // Without the vector backend the window shows a notice, not an editor: no key edits a score
  // nobody can see. Application commands above still run.
  if (!shell_.available()) return;
  const auto pressed = authoring_->controller().keyDown(event);
  record(pressed);
  // lastError_ reaches only stderr when the process ends, never the window. A key the editor refused as
  // things stand is shown in the window as a notice.
  if (!pressed) authoring_->controller().noteRefusal(pressed.error());
}
void NativeEditorApp::textComposition(
    std::u32string text,
    ui::CompositionSelection selection) noexcept {
  record(authoring_->controller().updateTextComposition(std::move(text),
                                                        selection));
}
void NativeEditorApp::textCommit(std::u32string text) noexcept {
  auto& controller = authoring_->controller();
  // A commit that arrives with no field open (the field ended first) has nothing to refuse and
  // nothing to tell.
  const bool composing = controller.textInputActive();
  const auto committed = controller.commitTextComposition(std::move(text));
  record(committed);
  if (!committed && composing) controller.noteRefusal(committed.error());
}
void NativeEditorApp::textCancel() noexcept {
  authoring_->controller().cancelTextComposition();
}
const native_ui::AccessibilityTree* NativeEditorApp::accessibilityTree()
    const noexcept {
  if (authoring_ == nullptr) return nullptr;
  // The presented SING shell publishes its own tree in its own geometry.
  if (shell_.presentedLastFrame()) return &shell_.accessibilityTree();
  return &authoring_->controller().accessibilityTree();
}

core::Result<void> NativeEditorApp::dispatchAccessibility(
    std::string_view id, native_ui::SemanticAction action) noexcept {
  if (authoring_ == nullptr) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Accessibility dispatch requires an authoring session");
  }
  if (shell_.presentedLastFrame() && native_ui::design::SingShell::ownsSemantic(id))
    return shell_.dispatchSemantic(authoring_->controller(), id, action);
  // The presented shell validates the id against what it publishes now: a retained element of a
  // score EXPORT hides (or a control the layout dropped) must not act on the document.
  if (shell_.presentedLastFrame())
    return shell_.dispatchController(authoring_->controller(), id, action);
  auto result = authoring_->controller().dispatchAccessibility(id, action);
  if (result && action == native_ui::SemanticAction::SetFocus) shell_.controllerFocusTaken();
  return result;
}

core::Result<void> NativeEditorApp::setAccessibilityValue(
    std::string_view id, std::string_view value) {
  if (authoring_ == nullptr) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Accessibility value setting requires an authoring session");
  }
  if (shell_.presentedLastFrame())
    return shell_.setControllerValue(authoring_->controller(), id, value);
  return authoring_->controller().setAccessibilityValue(id, value);
}

core::Result<void> NativeEditorApp::dispatchApplicationCommand(
    platform::ApplicationCommand command) {
  if (applicationController_ == nullptr) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Application commands are unavailable before initialization");
  }
  return applicationController_->dispatch(command);
}

bool NativeEditorApp::requestClose() noexcept {
  if (applicationController_ == nullptr) return true;
  auto requested = applicationController_->requestClose();
  if (!requested) {
    record(core::Result<void>{requested.error()});
    return false;
  }
  const bool accepted = requested.value();
  if (accepted && window_ != nullptr) window_->saveRestorationState();
  return accepted;
}

bool NativeEditorApp::wantsClose() const noexcept {
  return closeRequested_.load(std::memory_order_acquire);
}

std::optional<std::chrono::steady_clock::time_point> NativeEditorApp::nextFrameDue()
    const noexcept {
  // The shipping app injects no UI clock, so the shell reads the steady clock and its time is the
  // window's; only a test moves it by hand (NativeEditorAppConfig::uiClock).
  auto due = shell_.nextFrameDue();
  // The transport report is read here as well, because this is what decides whether the retry hold is
  // offered as it stands or one delay on: a feeder that has come back settles the report, and the
  // window has to be woken for the retry rather than still waiting for a report that has arrived. The
  // state itself is settled in paint(), which is what a frame reads it from.
  const auto reportSettled = authoring_ != nullptr &&
                             authoring_->runtime().transport().state().settled;
  const auto reportUnsettled = !reportSettled && unsettledSince_.has_value();
  // paint() also does the owner thread's time-driven work, and a window that paints only on request
  // paints for it only if it is asked to. That work is the autosave tick and the next try at a start
  // of the audio device that failed: with nothing animating (Reduce Motion, a held pose) no frame
  // would ever run them, and a document edited and then left alone would not be saved again however
  // long it stayed open. Work that a worker finishes asks for its own frame when it has published
  // (progressChanged, stateChanged).
  if (applicationController_ != nullptr && authoring_ != nullptr) {
    if (const auto autosave = applicationController_->autosaveDue(); autosave.has_value())
      due = due.has_value() ? std::min(*due, *autosave) : *autosave;
  }
  if (const auto retry = deviceRetry_.notBeforeFor(uiNow(), reportUnsettled);
      retry.has_value())
    due = due.has_value() ? std::min(*due, *retry) : *retry;
  return due;
}

platform::AudioDeviceInfo NativeEditorApp::audioInfo() const {
  return audioDevice_ == nullptr ? platform::AudioDeviceInfo{}
                                 : audioDevice_->info();
}

core::Result<platform::AudioDeviceCatalogSnapshot>
NativeEditorApp::enumerateAudioDevices() {
  if (audioDeviceCatalog_ == nullptr) {
    return core::failure<platform::AudioDeviceCatalogSnapshot>(
        core::ErrorCode::InvalidState,
        "Audio device catalog is unavailable before audio initialization");
  }
  return audioDeviceCatalog_->enumerate();
}

const std::vector<native_ui::EditorSceneState::AudioDeviceOption>&
NativeEditorApp::audioDeviceList(const authoring::AudioSettings& settings) {
  const auto info = audioInfo();
  // The catalog is a set of HAL property queries per device, and a window that paints at the
  // display's rate while the device plays asked for it on every one of those frames: about a quarter
  // of the paint time in the measured profile. The list is built from the catalog, the settings that
  // choose the row and the device the platform reports, so it is rebuilt only when one of those has
  // moved on. A device that is taken away changes the third, so its fallback row comes back.
  if (!publishedAudioDevices_.has_value() ||
      !(audioDeviceListSettings_ == settings) || !(audioDeviceListDevice_ == info)) {
    std::vector<native_ui::EditorSceneState::AudioDeviceOption> devices;
    if (const auto catalog = enumerateAudioDevices(); catalog) {
      devices.reserve(catalog.value().devices.size() + 1U);
      for (const auto& device : catalog.value().devices) {
        devices.push_back(native_ui::EditorSceneState::AudioDeviceOption{
            .id = device.id,
            .name = device.name,
            .physical = device.physical,
            .selected = device.id == settings.deviceId,
        });
      }
    }
    const auto activeDevice = std::find_if(
        devices.begin(), devices.end(), [&chosen = settings.deviceId](const auto& device) {
          return device.id == chosen;
        });
    if (activeDevice == devices.end() && !settings.deviceId.empty()) {
      devices.push_back(native_ui::EditorSceneState::AudioDeviceOption{
          .id = settings.deviceId,
          .name = settings.deviceId,
          .physical = info.physical,
          .selected = true,
      });
    }
    publishedAudioDevices_ = std::move(devices);
    audioDeviceListSettings_ = settings;
    audioDeviceListDevice_ = info;
  }
  return publishedAudioDevices_.value();
}

core::Result<authoring::AudioSettings> NativeEditorApp::audioSettings() const {
  if (audioSettings_ == nullptr) {
    return core::failure<authoring::AudioSettings>(
        core::ErrorCode::InvalidState,
        "Audio settings are unavailable before audio initialization");
  }
  return audioSettings_->current();
}

core::Result<authoring::AudioSettings> NativeEditorApp::applyAudioSettings(
    authoring::AudioSettings requested) {
  if (audioSettings_ == nullptr) {
    return core::failure<authoring::AudioSettings>(
        core::ErrorCode::InvalidState,
        "Audio settings are unavailable before audio initialization");
  }
  if (requested.deviceId.empty()) requested.deviceId = audioSettings_->current().deviceId;
  if (requested.deviceId != audioSettings_->current().deviceId &&
      audioDeviceCatalog_ != nullptr) {
    const auto catalog = audioDeviceCatalog_->enumerate();
    if (!catalog) {
      return core::Result<authoring::AudioSettings>{catalog.error()};
    }
    const auto found = std::find_if(
        catalog.value().devices.begin(), catalog.value().devices.end(),
        [&requested](const auto& device) { return device.id == requested.deviceId; });
    if (found == catalog.value().devices.end()) {
      return core::failure<authoring::AudioSettings>(
          core::ErrorCode::NotFound,
          "Requested audio device is not present in the current catalog");
    }
    if (requested.blockFrames < found->minimumBlockFrames ||
        requested.blockFrames > found->maximumBlockFrames ||
        requested.outputChannels < found->minimumOutputChannels ||
        requested.outputChannels > found->maximumOutputChannels ||
        std::find(found->supportedSampleRates.begin(),
                  found->supportedSampleRates.end(), requested.sampleRate) ==
            found->supportedSampleRates.end()) {
      return core::failure<authoring::AudioSettings>(
          core::ErrorCode::InvalidArgument,
          "Requested audio settings exceed the selected device capabilities");
    }
  }
  const auto previous = audioSettings_->current();
  auto applied = audioSettings_->apply(std::move(requested));
  if (!applied) return applied;
  if (audioSettingsStore_ != nullptr) {
    const auto saved = audioSettingsStore_->save(applied.value());
    if (!saved) {
      static_cast<void>(audioSettings_->apply(previous));
      return core::Result<authoring::AudioSettings>{saved.error()};
    }
  }
  return applied;
}

platform::AudioDeviceStats NativeEditorApp::audioStats() const noexcept {
  return audioDevice_ == nullptr ? platform::AudioDeviceStats{}
                                 : audioDevice_->stats();
}
platform::MultichannelRingProcessorStats
NativeEditorApp::processorStats() const noexcept {
  return processor_ == nullptr ? platform::MultichannelRingProcessorStats{}
                               : processor_->stats();
}

native_ui::design::ShellVoiceHost NativeEditorApp::makeVoiceHost() {
  native_ui::design::ShellVoiceHost host;
  // The session exists from the first time VOICE asks for it. Installed singers and banks are
  // signed, immutable content, so a draft may never be saved inside their roots.
  host.designer = [this]() -> native_ui::VoiceDesignerSession* {
    if (voiceDesigner_ == nullptr) {
      voiceDesigner_ = std::make_unique<native_ui::VoiceDesignerSession>();
      std::vector<std::filesystem::path> roots{config_.applicationSupportRoot / "Singers",
                                               config_.applicationSupportRoot / "Voicebanks"};
      for (const auto& root : distribution::defaultProceduralSearchRoots()) roots.push_back(root.path);
      voiceDesigner_->setProtectedRoots(std::move(roots));
    }
    return voiceDesigner_.get();
  };
  // The Voicebank Studio's own dialogs: recipe files, pose naming, exact seeds, discard prompt.
  const auto dialog = [this]() -> std::unique_ptr<platform::IFileDialog> {
    return config_.fileDialogFactory ? config_.fileDialogFactory() : platform::createNativeFileDialog();
  };
  const auto missing = [] { return core::Error{core::ErrorCode::Unsupported, "No native dialog is available", {}}; };
  host.choosePath = [dialog, missing](bool save, const std::filesystem::path& current)
      -> core::Result<std::optional<std::filesystem::path>> {
    auto files = dialog();
    if (!files) return missing();
    return files->choose(platform::FileDialogRequest{
        .purpose = save ? platform::FileDialogPurpose::SaveDesignerRecipe
                        : platform::FileDialogPurpose::SelectProceduralRecipe,
        .title = save ? "Save Draft Voice Recipe" : "Open Draft Voice Recipe",
        .initialDirectory = current.parent_path(),
        .suggestedName = save ? "voice-recipe.json" : "",
        .extensions = {"json"}});
  };
  host.confirmDiscard = [dialog, missing]() -> core::Result<bool> {
    auto files = dialog();
    if (!files) return missing();
    return files->confirmDiscardDesignerChanges();
  };
  host.choosePoseIdentity = [dialog, missing](bool frication)
      -> core::Result<std::optional<std::pair<std::string, std::string>>> {
    using Output = std::optional<std::pair<std::string, std::string>>;
    auto files = dialog();
    if (!files) return missing();
    const auto identity = files->chooseDesignerPoseIdentity(
        frication ? platform::IFileDialog::DesignerPoseKind::Frication
                  : platform::IFileDialog::DesignerPoseKind::Voiced);
    if (!identity) return core::Result<Output>{identity.error()};
    if (!identity.value()) return Output{};
    return Output{std::pair{identity.value()->phone, identity.value()->style}};
  };
  host.chooseSeed = [dialog, missing](const std::string& current, bool frication)
      -> core::Result<std::optional<std::string>> {
    auto files = dialog();
    if (!files) return missing();
    return files->chooseDesignerSeed(current, frication);
  };
  // Auditions play the way the Voicebank Studio plays them: a one-shot stream on the system output.
  host.play = [this](std::shared_ptr<const voicebank::AudioBuffer> audio) -> core::Result<void> {
    if (!audio) return core::failure(core::ErrorCode::InvalidArgument, "No audition audio to play");
    auto device = config_.systemAudioDeviceFactory ? config_.systemAudioDeviceFactory()
                                                   : platform::createSystemAudioDevice();
    const auto frames = audio->frameCount();
    auto started = voiceAudition_.start(std::move(device), std::move(audio), 0U, frames, 0.25F);
    if (started) requestWindowRepaint();
    return started;
  };
  host.stop = [this] { record(voiceAudition_.stop()); };
  host.level = [this]() -> std::optional<float> {
    const auto polled = voiceAudition_.poll();
    if (!polled) {
      record(core::Result<void>{polled.error()});
      return std::nullopt;
    }
    return voiceAudition_.level();
  };
  return host;
}

void NativeEditorApp::record(const core::Result<void>& result) noexcept {
  if (!result) {
    lastError_ = result.error().message;
    if (!result.error().context.empty()) {
      lastError_ += ": " + result.error().context;
    }
  }
}

}  // namespace seam::standalone
