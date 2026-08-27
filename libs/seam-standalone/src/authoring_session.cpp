#include "seam/standalone/authoring_session.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/application/arrangement_commands.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/rendering/streaming_pcm_source.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <memory>
#include <system_error>
#include <utility>

namespace seam::standalone {
namespace {

const voicebank::VoicebankCandidate* preferredCandidate(
    const std::vector<voicebank::VoicebankCandidate>& candidates) {
  const auto preferred = std::find_if(
      candidates.begin(), candidates.end(), [](const auto& candidate) {
        return candidate.manifest.id == "demo.public-domain.human.production" &&
               candidate.manifest.version == "0.12.0";
      });
  return preferred == candidates.end()
             ? (candidates.empty() ? nullptr : &candidates.front())
             : &*preferred;
}

}  // namespace

core::Result<std::unique_ptr<AuthoringSession>> AuthoringSession::create(
    AuthoringSessionConfig config,
    native_ui::EditorHostCallbacks callbacks) {
  if (config.cacheRoot.empty()) {
    return core::failure<std::unique_ptr<AuthoringSession>>(
        core::ErrorCode::InvalidArgument,
        "Standalone authoring cache root cannot be empty");
  }
  if (config.sampleRate < 8000U || config.sampleRate > 192000U) {
    return core::failure<std::unique_ptr<AuthoringSession>>(
        core::ErrorCode::InvalidArgument,
        "Standalone sample rate must be between 8000 and 192000 Hz");
  }
  if (config.outputChannels < 1U || config.outputChannels > 8U) {
    return core::failure<std::unique_ptr<AuthoringSession>>(
        core::ErrorCode::InvalidArgument,
        "Standalone output channel count must be between one and eight");
  }

  application::ProjectFactory factory{1000U};
  domain::TrackId trackId;
  domain::RegionId regionId;
  auto project = makeUntitledProject(factory, trackId, regionId,
                                     config.sampleRate,
                                     config.outputChannels);
  auto document = std::unique_ptr<authoring::ProjectDocument>{
      new authoring::ProjectDocument(
          std::move(project),
          application::ProjectFactory{factory.nextIdValue()})};
  auto runtime = std::make_unique<authoring::AuthoringRuntime>(
      std::move(document), authoring::AuthoringRuntimeConfig{
                               .cacheRoot = std::move(config.cacheRoot),
                               .voicebankRoots = std::move(config.voicebankRoots),
                               .previewSampleRate = config.sampleRate,
                               .outputChannels = config.outputChannels,
                               .allowDevelopmentVoicebanks = config.allowDevelopmentVoicebanks,
                               .enableTransport = true,
                           });
  auto result = std::unique_ptr<AuthoringSession>{new AuthoringSession(
      std::move(runtime), trackId, regionId, std::move(callbacks))};
  auto initialized = result->initialize(config.bindFirstAvailableVoicebank);
  if (!initialized) {
    return core::Result<std::unique_ptr<AuthoringSession>>{initialized.error()};
  }
  return result;
}

AuthoringSession::AuthoringSession(
    std::unique_ptr<authoring::AuthoringRuntime> runtime,
    domain::TrackId trackId,
    domain::RegionId regionId,
    native_ui::EditorHostCallbacks callbacks)
    : runtime_(std::move(runtime)),
      externalCallbacks_(std::move(callbacks)),
      trackId_(trackId),
      regionId_(regionId) {}

AuthoringSession::~AuthoringSession() {
  if (runtime_) {
    runtime_->setCompletionCallback({});
    runtime_->shutdown();
  }
  controller_.reset();
}

domain::Project AuthoringSession::makeUntitledProject(
    application::ProjectFactory& factory,
    domain::TrackId& trackId,
    domain::RegionId& regionId,
    std::uint32_t sampleRate,
    std::uint8_t outputChannels) {
  auto project = factory.createProject("Untitled");
  project.settings().sampleRate = sampleRate;
  project.settings().characterDisplay = domain::CharacterDisplayMode::Minimal;
  static_cast<void>(project.tempoMap().addOrReplace(time::Tick{0}, 120.0));
  trackId = factory.addVocalTrack(project, "Voice 1");
  regionId = factory.addRegion(project, trackId, "Region 1",
                               time::Tick{0}, time::Tick{15360});
  application::EditorSession routeSession{project};
  auto route = routeSession.execute(
      std::make_unique<application::ConfigureProjectOutputCommand>(
          outputChannels));
  if (route) project = routeSession.project();
  return project;
}

core::Result<void> AuthoringSession::initialize(
    bool bindFirstAvailableVoicebank) {
  auto initialized = runtime_->initialize();
  if (!initialized) return initialized;

  if (bindFirstAvailableVoicebank) {
    auto bound = bindInitialVoicebank();
    if (!bound && bound.error().code != core::ErrorCode::NotFound) return bound;
    if (bound) runtime_->clearDiagnostics();
  }
  static_cast<void>(runtime_->selectTrack(trackId_));
  static_cast<void>(runtime_->selectRegion(regionId_));
  configureController();
  runtime_->setCompletionCallback([this] { onRenderCompleted(); });
  runtime_->handleDocumentChanged();
  return core::success();
}

core::Result<void> AuthoringSession::bindInitialVoicebank() {
  const auto candidates = runtime_->voicebanks().candidates();
  const auto* candidate = preferredCandidate(candidates);
  if (candidate == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "No installed or development voicebank is available");
  }
  return runtime_->voicebanks().bindTrack(runtime_->document(), trackId_,
                                           *candidate);
}

void AuthoringSession::configureController() {
  native_ui::EditorHostCallbacks callbacks{
      .requestRepaint = externalCallbacks_.requestRepaint,
      .beginTextInput = externalCallbacks_.beginTextInput,
      .endTextInput = externalCallbacks_.endTextInput,
      .setPlaying = [this](bool playing) {
        const auto result = playing ? runtime_->transport().play()
                                    : runtime_->transport().pause();
        if (externalCallbacks_.setPlaying) {
          static_cast<void>(externalCallbacks_.setPlaying(result && playing));
        }
        return result;
      },
      .documentChanged = [this] { onDocumentChanged(); },
      .stopPlaying = [this] {
        const auto result = runtime_->transport().stop();
        if (externalCallbacks_.requestRepaint) {
          externalCallbacks_.requestRepaint();
        }
        return result;
      },
      .seekTick = [this](time::Tick tick) {
        const auto frame = runtime_->document().session().project().tempoMap()
                               .sampleFrameAt(tick, runtime_->transport().sampleRate());
        const auto result = runtime_->transport().seek(frame);
        if (externalCallbacks_.requestRepaint) {
          externalCallbacks_.requestRepaint();
        }
        return result;
      },
      .setLoopTicks = [this](time::Tick start, time::Tick end) {
        const auto& project = runtime_->document().session().project();
        const auto& tempo = project.tempoMap();
        const auto loop = rendering::PlaybackLoop{
            .enabled = true,
            .startFrame = tempo.sampleFrameAt(start, runtime_->transport().sampleRate()),
            .endFrame = tempo.sampleFrameAt(end, runtime_->transport().sampleRate()),
        };
        const auto result = runtime_->transport().setLoop(loop);
        if (externalCallbacks_.requestRepaint) {
          externalCallbacks_.requestRepaint();
        }
        return result;
      },
      .toggleLoop = [this] {
        const auto state = runtime_->transport().state();
        core::Result<void> result = core::success();
        if (state.loop.enabled) {
          result = runtime_->transport().setLoop(
              rendering::PlaybackLoop{.enabled = false});
        } else if (state.timelineEnd > time::SampleFrame{0}) {
          result = runtime_->transport().setLoop(
              rendering::PlaybackLoop{.enabled = true,
                                      .startFrame = 0,
                                      .endFrame = state.timelineEnd});
        }
        if (externalCallbacks_.requestRepaint) {
          externalCallbacks_.requestRepaint();
        }
        return result;
      },
      .cancelRender = [this] {
        runtime_->renderer().cancel();
        if (externalCallbacks_.requestRepaint) {
          externalCallbacks_.requestRepaint();
        }
      },
      .retryRender = [this] {
        runtime_->requestPreview(true);
        if (externalCallbacks_.requestRepaint) {
          externalCallbacks_.requestRepaint();
        }
      },
      .cycleUnitVariant = [this](domain::PhonemeKey key) {
        return runtime_->technicalEdits().cycleUnitVariant(key);
      },
      .cycleUnitRenderer = [this](domain::PhonemeKey key) {
        return runtime_->technicalEdits().cycleUnitRenderer(key);
      },
      .upsertPitchPoint = [this](domain::PitchAutomationPoint point) {
        return runtime_->technicalEdits().upsertPitchPoint(point);
      },
      .movePitchPoint = [this](time::Tick from,
                               domain::PitchAutomationPoint point) {
        return runtime_->technicalEdits().movePitchPoint(from, point);
      },
      .removePitchPoint = [this](time::Tick tick) {
        return runtime_->technicalEdits().removePitchPoint(tick);
      },
      .cyclePitchInterpolation = [this](time::Tick tick) {
        return runtime_->technicalEdits().cyclePitchInterpolation(tick);
      },
      .movePhonemeBoundary = [this](domain::PhonemeKey key, bool start,
                                    time::Microseconds offset) {
        return runtime_->technicalEdits().movePhonemeBoundary(key, start, offset);
      },
      .previewSeam = externalCallbacks_.previewSeam,
      .loadSampleMicroscope = [this](domain::PhonemeKey key)
          -> core::Result<native_ui::SampleMicroscopeData> {
        const auto view = runtime_->technicalEdits().unitDiagnostic(key);
        if (!view.has_value()) {
          return core::failure<native_ui::SampleMicroscopeData>(
              core::ErrorCode::NotFound,
              "Rendered Unit is unavailable for sample inspection");
        }
        const auto resolution = runtime_->voicebanks().resolveTrack(
            runtime_->document().session().project(), trackId_);
        if (!resolution.resolved()) {
          return core::failure<native_ui::SampleMicroscopeData>(
              core::ErrorCode::NotFound,
              "Selected track Voicebank is unavailable for sample inspection",
              resolution.diagnostic);
        }
        const auto* unit = resolution.candidate->manifest.findUnit(
            view->entry.unitId);
        if (unit == nullptr) {
          return core::failure<native_ui::SampleMicroscopeData>(
              core::ErrorCode::NotFound,
              "Rendered Voicebank Unit is missing", view->entry.unitId);
        }
        auto audio = voicebank::readWav(
            resolution.candidate->bankRoot / unit->audioPath);
        if (!audio) {
          return core::Result<native_ui::SampleMicroscopeData>{audio.error()};
        }
        const auto* track = runtime_->document().session().project()
                                .findVocalTrack(trackId_);
        const auto* region = runtime_->document().session().project()
                                .findRegion(regionId_);
        std::string context = "DESTINATION";
        if (track != nullptr) context += " / " + track->name;
        if (region != nullptr) context += " / " + region->name;
        context += " / MIDI " + std::to_string(view->entry.targetMidi);
        return native_ui::SampleMicroscopeData{
            .unit = *unit,
            .audio = std::move(audio).value(),
            .destinationContext = std::move(context),
        };
      },
      .microscopeUnitChanged = externalCallbacks_.microscopeUnitChanged,
      .playMicroscopeSample = externalCallbacks_.playMicroscopeSample,
      .selectVoicebank = externalCallbacks_.selectVoicebank,
      .diagnosticAction = externalCallbacks_.diagnosticAction,
      .viewChanged = externalCallbacks_.viewChanged,
      .applyAudioSettings = externalCallbacks_.applyAudioSettings,
      .uiClock = externalCallbacks_.uiClock,
      .reduceMotionEnabled = externalCallbacks_.reduceMotionEnabled,
  };
  controller_ = std::make_unique<native_ui::NativeEditorController>(
      runtime_->document().session(), runtime_->document().factory(),
      regionId_, std::move(callbacks));
  controller_->setDirty(runtime_->document().dirty());
}

void AuthoringSession::onDocumentChanged() {
  runtime_->handleDocumentChanged();
  if (controller_) controller_->setDirty(runtime_->document().dirty());
  if (externalCallbacks_.documentChanged) externalCallbacks_.documentChanged();
}

void AuthoringSession::onRenderCompleted() {
  if (externalCallbacks_.requestRepaint) externalCallbacks_.requestRepaint();
}


core::Result<void> AuthoringSession::createNewProject(
    authoring::NewProjectRequest request) {
  authoring::ProjectLifecycleService lifecycle{&runtime_->voicebanks()};
  auto created = lifecycle.createNew(runtime_->document(), request);
  if (!created) return created;
  return rebindAfterProjectReplacement();
}

core::Result<authoring::OpenProjectResult> AuthoringSession::openProject(
    const std::filesystem::path& path) {
  authoring::ProjectLifecycleService lifecycle{&runtime_->voicebanks()};
  auto opened = lifecycle.open(runtime_->document(), path);
  if (!opened) {
    return core::Result<authoring::OpenProjectResult>{opened.error()};
  }
  auto rebound = rebindAfterProjectReplacement();
  if (!rebound) {
    return core::Result<authoring::OpenProjectResult>{rebound.error()};
  }
  return opened;
}

core::Result<void> AuthoringSession::saveProject() {
  authoring::ProjectLifecycleService lifecycle{&runtime_->voicebanks()};
  auto saved = lifecycle.save(runtime_->document());
  if (saved && controller_) controller_->setDirty(false);
  return saved;
}

core::Result<void> AuthoringSession::saveProjectAs(
    const std::filesystem::path& path) {
  authoring::ProjectLifecycleService lifecycle{&runtime_->voicebanks()};
  auto saved = lifecycle.saveAs(runtime_->document(), path);
  if (saved && controller_) controller_->setDirty(false);
  return saved;
}

core::Result<void> AuthoringSession::recoverProject(
    authoring::AutosaveService& autosave,
    const authoring::RecoveryCandidate& candidate) {
  auto recovered = autosave.recover(runtime_->document(), candidate);
  if (!recovered) return recovered;
  return rebindAfterProjectReplacement();
}

core::Result<authoring::MediaImportResult> AuthoringSession::importBackingMedia(
    const std::filesystem::path& sourcePath, authoring::MediaImportMode mode,
    std::string trackName, time::Tick startTick) {
  if (runtime_ == nullptr) {
    return core::failure<authoring::MediaImportResult>(
        core::ErrorCode::InvalidState,
        "Backing media import requires an initialized authoring session");
  }
  const auto projectPath = runtime_->document().identity().projectPath.value_or(
      std::filesystem::path{});
  auto imported = authoring::MediaImportService::import(
      authoring::MediaImportRequest{
          .trackId = runtime_->document().factory().nextTrackId(),
          .trackName = std::move(trackName),
          .sourcePath = sourcePath,
          .projectPath = projectPath,
          .startTick = startTick,
          .mode = mode,
      });
  if (!imported) return imported;

  auto& track = imported.value().track;
  const auto outputChannels = runtime_->document().session().project()
                                  .routing()
                                  .deviceOutputChannels;
  if (outputChannels == 1U) {
    track.outputRoute = domain::TrackOutputRoute{
        .bus = runtime_->document().session().project().routing().masterBus,
        .matrix = domain::RoutingMatrix::identity(1U),
    };
  } else {
    domain::RoutingMatrix matrix{
        .sourceChannels = 1U,
        .destinationChannels = outputChannels,
        .gains = std::vector<float>(outputChannels, 1.0F),
    };
    track.outputRoute = domain::TrackOutputRoute{
        .bus = runtime_->document().session().project().routing().masterBus,
        .matrix = std::move(matrix),
    };
  }

  const auto executed = runtime_->execute(
      std::make_unique<application::AddAudioTrackCommand>(track));
  if (!executed) {
    if (!imported.value().ownedPath.empty()) {
      std::error_code error;
      std::filesystem::remove(imported.value().ownedPath, error);
    }
    return core::Result<authoring::MediaImportResult>{executed.error()};
  }
  if (controller_) controller_->setDirty(runtime_->document().dirty());
  if (externalCallbacks_.documentChanged) externalCallbacks_.documentChanged();
  return imported;
}

core::Result<void> AuthoringSession::relinkBackingMedia(
    domain::TrackId trackId, const std::filesystem::path& sourcePath) {
  if (runtime_ == nullptr) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Backing media relink requires an initialized session");
  }
  const auto& project = runtime_->document().session().project();
  const auto existing = std::find_if(
      project.audioTracks().begin(), project.audioTracks().end(),
      [trackId](const auto& track) { return track.id == trackId; });
  if (existing == project.audioTracks().end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Backing media track was not found", trackId.toString());
  }
  if (existing->mediaHash.empty()) {
    return core::failure(core::ErrorCode::Conflict,
                         "Backing media has no exact content identity",
                         trackId.toString());
  }
  std::error_code error;
  const auto canonical = std::filesystem::weakly_canonical(sourcePath, error);
  if (error || !std::filesystem::is_regular_file(canonical, error)) {
    return core::failure(core::ErrorCode::NotFound,
                         "Backing media relink source is not a regular file",
                         sourcePath.string());
  }
  auto source = rendering::StreamingPcmSource::open(canonical, 4096U);
  if (!source) return core::Result<void>{source.error()};
  if (source.value()->info().contentHash != existing->mediaHash) {
    return core::failure(core::ErrorCode::Conflict,
                         "Exact backing media relink requires a matching content hash",
                         existing->mediaHash + " != " +
                             source.value()->info().contentHash);
  }
  auto replacement = *existing;
  if (existing->mediaOwnership == domain::MediaOwnership::ProjectCopy) {
    const auto projectPath = runtime_->document().identity().projectPath;
    if (!projectPath.has_value()) {
      return core::failure(core::ErrorCode::InvalidState,
                           "Project-owned backing media relink requires a saved project");
    }
    auto imported = authoring::MediaImportService::import(
        authoring::MediaImportRequest{
            .trackId = trackId,
            .trackName = existing->name,
            .sourcePath = canonical,
            .projectPath = *projectPath,
            .startTick = existing->startTick,
            .mode = authoring::MediaImportMode::Copy,
        });
    if (!imported) return core::Result<void>{imported.error()};
    replacement.mediaPath = imported.value().track.mediaPath;
    replacement.mediaOwnership = domain::MediaOwnership::ProjectCopy;
  } else {
    replacement.mediaPath = canonical.string();
    replacement.mediaOwnership = domain::MediaOwnership::ExternalReference;
  }
  replacement.mediaHash = source.value()->info().contentHash;
  replacement.originalFilename = canonical.filename().string();
  replacement.sourceSampleRate = source.value()->info().sampleRate;
  replacement.sourceChannels = source.value()->info().channels;
  replacement.sourceFrameCount = source.value()->info().frameCount;
  const auto executed = runtime_->execute(
      std::make_unique<application::ReplaceAudioTrackCommand>(
          trackId, std::move(replacement)));
  if (!executed) return executed;
  if (controller_) controller_->setDirty(runtime_->document().dirty());
  if (externalCallbacks_.documentChanged) externalCallbacks_.documentChanged();
  return core::success();
}

core::Result<void> AuthoringSession::rebindAfterProjectReplacement() {
  const auto& project = runtime_->document().session().project();
  const domain::VocalTrack* selectedTrack = nullptr;
  const domain::VocalRegion* selectedRegion = nullptr;
  for (const auto& track : project.vocalTracks()) {
    if (!track.regions.empty()) {
      selectedTrack = &track;
      selectedRegion = &track.regions.front();
      break;
    }
  }
  trackId_ = selectedTrack == nullptr ? domain::TrackId{} : selectedTrack->id;
  regionId_ = selectedRegion == nullptr ? domain::RegionId{} : selectedRegion->id;
  if (selectedTrack != nullptr) {
    auto track = runtime_->selectTrack(trackId_);
    if (!track) return track;
  }
  if (selectedRegion != nullptr) {
    auto region = runtime_->selectRegion(regionId_);
    if (!region) return region;
  }
  static_cast<void>(runtime_->transport().stop());
  configureController();
  runtime_->handleDocumentChanged();
  if (controller_) controller_->setDirty(runtime_->document().dirty());
  return core::success();
}

voicebank::VoicebankResolution AuthoringSession::voicebankResolution() const {
  return runtime_->voicebanks().resolveTrack(
      runtime_->document().session().project(), trackId_);
}

}  // namespace seam::standalone
