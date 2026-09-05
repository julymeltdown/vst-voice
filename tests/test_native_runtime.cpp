#include "test_framework.hpp"
#include "seam/application/note_commands.hpp"
#include "seam/core/file_io.hpp"

#include "seam/platform/audio_device.hpp"
#include "seam/platform/application_paths.hpp"
#include "seam/platform/crash_capture.hpp"
#include "seam/platform/ring_buffer_processor.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/standalone/native_editor_app.hpp"
#include "seam/rendering/audio_ring_buffer.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/playback_engine.hpp"
#include "seam/rendering/playback_feeder_service.hpp"

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "test_support.hpp"
#include "crash_capture_test_support.hpp"

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required
#endif

namespace {

using namespace std::chrono_literals;

std::shared_ptr<const seam::rendering::PlaybackTimeline> testTimeline(
    std::size_t frames = 48000U) {
  std::vector<float> samples(frames, 0.15F);
  auto pcm = std::make_shared<const seam::rendering::CachedPcm>(
      seam::rendering::CachedPcm{
          .sampleRate = 48000U,
          .startFrame = 0,
          .samples = std::move(samples),
      });
  auto timeline = std::make_shared<seam::rendering::PlaybackTimeline>(48000U);
  const auto added = timeline->addClip(seam::rendering::PlaybackClip{
      .id = "native-runtime",
      .pcm = std::move(pcm),
      .gain = 1.0F,
      .fadeInFrames = 0,
      .fadeOutFrames = 0,
      .enabled = true,
  });
  if (!added) throw std::runtime_error(added.error().message);
  return timeline;
}

class ScriptedAudioDevice final : public seam::platform::IAudioDevice {
public:
  ScriptedAudioDevice(bool failOpen, bool physical, std::string backend,
                      std::string failure = {})
      : failOpen_(failOpen),
        failure_(std::move(failure)),
        info_{.backend = std::move(backend),
              .deviceId = physical ? "physical-test-device"
                                   : "threaded-callback-clock",
              .deviceName = "Scripted device",
              .sampleRate = 48000U,
              .blockFrames = 256U,
              .outputChannels = 2U,
              .physical = physical} {}

  seam::core::Result<void> open(
      const seam::platform::AudioDeviceConfig& config,
      seam::platform::IAudioProcessor&) override {
    if (failOpen_) {
      return seam::core::failure(seam::core::ErrorCode::IoError, failure_);
    }
    info_.deviceId = config.deviceId.empty() ? info_.deviceId : config.deviceId;
    info_.sampleRate = config.sampleRate;
    info_.blockFrames = config.blockFrames;
    info_.outputChannels = config.outputChannels;
    opened_ = true;
    return seam::core::success();
  }
  seam::core::Result<void> start() override {
    if (!opened_) {
      return seam::core::failure(seam::core::ErrorCode::InvalidState,
                                 "scripted device is not open");
    }
    running_ = true;
    return seam::core::success();
  }
  void stop() noexcept override { running_ = false; }
  bool running() const noexcept override { return running_; }
  seam::platform::AudioDeviceInfo info() const override { return info_; }
  seam::platform::AudioDeviceStats stats() const noexcept override { return {}; }

private:
  bool failOpen_{false};
  std::string failure_;
  seam::platform::AudioDeviceInfo info_;
  bool opened_{false};
  bool running_{false};
};

}  // namespace

TEST_CASE("dedicated playback feeder service owns production and stops cleanly") {
  seam::rendering::SpscAudioRingBuffer ring{8192U};
  seam::rendering::PlaybackFeeder feeder{ring, 48000U, 512U, 64U};
  seam::rendering::PlaybackFeederService service{
      feeder, seam::rendering::PlaybackFeederServiceConfig{
                  .targetBufferedFrames = 4096U,
                  .activePollInterval = 100us,
                  .idlePollInterval = 1ms,
              }};
  CHECK(service.setTimeline(testTimeline()));
  CHECK(service.start());
  CHECK(!service.start());
  CHECK(service.setPlaying(true));
  for (int attempt = 0; attempt < 100 && ring.availableRead() == 0U; ++attempt) {
    std::this_thread::sleep_for(1ms);
  }
  CHECK(ring.availableRead() > 0U);
  CHECK(service.running());
  service.stop();
  CHECK(!service.running());
  CHECK(service.stats().framesFed > 0U);
}

TEST_CASE("threaded audio device drives the real callback contract") {
  seam::rendering::SpscAudioRingBuffer ring{16384U};
  seam::rendering::PlaybackFeeder feeder{ring, 48000U, 512U, 64U};
  seam::rendering::PlaybackFeederService service{
      feeder, seam::rendering::PlaybackFeederServiceConfig{
                  .targetBufferedFrames = 8192U,
                  .activePollInterval = 100us,
                  .idlePollInterval = 1ms,
              }};
  seam::platform::RingBufferAudioProcessor processor{ring};
  auto device = seam::platform::createThreadedAudioDevice();
  CHECK(service.setTimeline(testTimeline(96000U)));
  CHECK(service.setLoop(seam::rendering::PlaybackLoop{
      .enabled = true,
      .startFrame = 0,
      .endFrame = 96000,
  }));
  CHECK(service.start());
  CHECK(service.setPlaying(true));
  for (int attempt = 0; attempt < 100 && ring.availableRead() < 2048U; ++attempt) {
    std::this_thread::sleep_for(1ms);
  }
  CHECK(device->open(seam::platform::AudioDeviceConfig{
                         .sampleRate = 48000U,
                         .blockFrames = 128U,
                         .outputChannels = 2U,
                         .applicationName = "SEAM test",
                         .streamName = "callback test",
                     },
                     processor));
  CHECK(device->start());
  CHECK(!device->start());
  for (int attempt = 0; attempt < 100 && device->stats().callbacks < 8U; ++attempt) {
    std::this_thread::sleep_for(2ms);
  }
  device->stop();
  service.stop();
  CHECK(device->stats().callbacks >= 8U);
  CHECK(device->stats().frames >= 1024U);
  CHECK(processor.stats().deliveredFrames > 0U);
  CHECK(device->info().physical == false);
}

TEST_CASE("playback controls remain race-free while feeder and callback threads run") {
  seam::rendering::SpscAudioRingBuffer ring{32768U};
  seam::rendering::PlaybackFeeder feeder{ring, 48000U, 256U, 512U};
  seam::rendering::PlaybackFeederService service{
      feeder, seam::rendering::PlaybackFeederServiceConfig{
                  .targetBufferedFrames = 16384U,
                  .activePollInterval = 100us,
                  .idlePollInterval = 1ms,
              }};
  seam::platform::RingBufferAudioProcessor processor{ring};
  auto device = seam::platform::createThreadedAudioDevice();
  CHECK(service.setTimeline(testTimeline(192000U)));
  CHECK(service.setLoop(seam::rendering::PlaybackLoop{
      .enabled = true,
      .startFrame = 0,
      .endFrame = 192000,
  }));
  CHECK(service.start());
  CHECK(service.setPlaying(true));
  CHECK(device->open(seam::platform::AudioDeviceConfig{
                         .sampleRate = 48000U,
                         .blockFrames = 64U,
                         .outputChannels = 2U,
                         .applicationName = "SEAM race test",
                         .streamName = "race test",
                     },
                     processor));
  CHECK(device->start());
  for (int index = 0; index < 80; ++index) {
    CHECK(service.seek(static_cast<seam::time::SampleFrame>(index * 97)));
    CHECK(service.setPlaying(index % 7 != 0));
    if (index % 7 == 0) CHECK(service.setPlaying(true));
    std::this_thread::sleep_for(200us);
  }
  std::this_thread::sleep_for(30ms);
  device->stop();
  service.stop();
  CHECK(feeder.stats().controlCommands > 100U);
  CHECK(device->stats().callbacks > 0U);
}

TEST_CASE("system audio adapter reports an explicit bounded open result") {
#if defined(__linux__)
  const auto* previous = std::getenv("PULSE_SERVER");
  const std::string previousValue = previous == nullptr ? std::string{} : previous;
  const bool hadPrevious = previous != nullptr;
  CHECK(::setenv("PULSE_SERVER",
                 "unix:/__project_seam_nonexistent__/pulse.sock", 1) == 0);
#endif

  seam::platform::SilenceProcessor processor;
  auto device = seam::platform::createSystemAudioDevice();
  CHECK(device != nullptr);
  const auto opened = device->open(seam::platform::AudioDeviceConfig{
                                       .sampleRate = 48000U,
                                       .blockFrames = 256U,
                                       .outputChannels = 2U,
                                       .applicationName = "SEAM probe",
                                       .streamName = "probe",
                                   },
                                   processor);

#if defined(__linux__)
  if (hadPrevious) {
    CHECK(::setenv("PULSE_SERVER", previousValue.c_str(), 1) == 0);
  } else {
    CHECK(::unsetenv("PULSE_SERVER") == 0);
  }
#endif

  if (opened) {
    CHECK(device->info().physical);
    device->stop();
  } else {
    CHECK(!opened.error().message.empty());
  }
}

TEST_CASE("native editor audio settings restart the live deterministic transport transactionally") {
  const auto root = seam::test::support::temporaryDirectory("native-audio-settings");
  seam::standalone::NativeEditorAppConfig config;
  config.runtimeMode = seam::standalone::ProductionRuntimeMode::DeterministicTest;
  config.applicationSupportRoot = root;
  config.forceThreadedAudio = true;
  config.authoring.sampleRate = 48000U;
  config.authoring.outputChannels = 2U;
  config.audioBlockFrames = 256U;
  auto app = seam::standalone::NativeEditorApp::create(std::move(config));
  CHECK(app);

  const auto current = app.value()->audioSettings();
  CHECK(current);
  CHECK(current.value().deviceId == "threaded-callback-clock");
  auto requested = current.value();
  requested.sampleRate = 44100U;
  requested.blockFrames = 128U;
  requested.outputChannels = 1U;
  const auto applied = app.value()->applyAudioSettings(requested);
  CHECK(applied);
  CHECK(applied.value().sampleRate == 44100U);
  CHECK(app.value()->authoring().runtime().transport().sampleRate() == 44100U);
  CHECK(app.value()->authoring().runtime().transport().outputChannels() == 1U);
  seam::authoring::AudioSettingsStore store{
      root / "Data" / "Settings" / "audio-settings.json"};
  const auto persisted = store.load();
  CHECK(persisted);
  CHECK(persisted.value().sampleRate == 44100U);

  requested.sampleRate = 88200U;
  CHECK(!app.value()->applyAudioSettings(requested));
  const auto afterFailure = app.value()->audioSettings();
  CHECK(afterFailure);
  CHECK(afterFailure.value().sampleRate == 44100U);
}

TEST_CASE("production audio failure never creates a callback clock fallback") {
  const auto root = seam::test::support::temporaryDirectory(
      "native-release-audio-failure");
  auto threadedCreations = std::make_shared<std::size_t>(0U);
  seam::standalone::NativeEditorAppConfig config;
  config.runtimeMode = seam::standalone::ProductionRuntimeMode::Development;
  config.applicationSupportRoot = root;
  config.systemAudioDeviceFactory = [] {
    return std::make_unique<ScriptedAudioDevice>(
        true, true, "physical-test", "physical-open-failure");
  };
  config.threadedAudioDeviceFactory = [threadedCreations] {
    ++*threadedCreations;
    return std::make_unique<ScriptedAudioDevice>(
        false, false, "threaded-callback-clock");
  };
  auto app = seam::standalone::NativeEditorApp::create(std::move(config));
  CHECK(app);
  CHECK(*threadedCreations == 0U);
  CHECK(!app.value()->authoring().controller().sceneState().audioDeviceOnline);
  const auto& diagnostics =
      app.value()->authoring().controller().diagnosticPanel().entries();
  CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& entry) {
    return entry.diagnostic.code == "AUDIO_UNAVAILABLE";
  }));
}

TEST_CASE("audio rollback preserves the requested device open error") {
  const auto root = seam::test::support::temporaryDirectory(
      "native-audio-open-rollback");
  auto creations = std::make_shared<std::size_t>(0U);
  seam::standalone::NativeEditorAppConfig config;
  config.runtimeMode = seam::standalone::ProductionRuntimeMode::Development;
  config.applicationSupportRoot = root;
  config.systemAudioDeviceFactory = [creations] {
    const auto fail = (*creations)++ == 1U;
    return std::make_unique<ScriptedAudioDevice>(
        fail, true, "physical-test", "requested-open-failure");
  };
  auto app = seam::standalone::NativeEditorApp::create(std::move(config));
  CHECK(app);
  const auto before = app.value()->audioSettings();
  CHECK(before);
  auto requested = before.value();
  requested.sampleRate = 44100U;
  requested.blockFrames = 128U;
  const auto applied = app.value()->applyAudioSettings(requested);
  CHECK(!applied);
  CHECK(applied.error().message == "requested-open-failure");
  const auto after = app.value()->audioSettings();
  CHECK(after);
  CHECK(after.value().sampleRate == before.value().sampleRate);
  CHECK(after.value().blockFrames == before.value().blockFrames);
  CHECK(app.value()->audioInfo().physical);
}

TEST_CASE("native editor defers audio callbacks until playable audio is ready") {
  const auto root = seam::test::support::temporaryDirectory(
      "native-audio-startup-gate");
  seam::standalone::NativeEditorAppConfig config;
  config.runtimeMode = seam::standalone::ProductionRuntimeMode::Development;
  config.applicationSupportRoot = root;
  config.forceThreadedAudio = false;
  config.systemAudioDeviceFactory = [] {
    return std::make_unique<ScriptedAudioDevice>(
        false, true, "physical-test");
  };
  config.startPaused = false;
  auto app = seam::standalone::NativeEditorApp::create(std::move(config));
  CHECK(app);

  std::this_thread::sleep_for(25ms);
  const auto transport = app.value()->authoring().runtime().transport().state();
  CHECK(!transport.available);
  CHECK(app.value()->audioStats().callbacks == 0U);
  CHECK(app.value()->processorStats().underflowFrames == 0U);
}

TEST_CASE("native editor projects completed render audio state during paint") {
  const auto root = seam::test::support::temporaryDirectory(
      "native-render-status-projection");
  seam::standalone::NativeEditorAppConfig config;
  config.runtimeMode = seam::standalone::ProductionRuntimeMode::DeterministicTest;
  config.applicationSupportRoot = root;
  config.forceThreadedAudio = true;
  config.allowDevelopmentVoicebanks = true;
  config.authoring.bindFirstAvailableVoicebank = true;
  config.authoring.allowDevelopmentVoicebanks = true;
  config.authoring.voicebankRoots = {
      seam::voicebank::VoicebankSearchRoot{
          .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
          .kind = seam::voicebank::VoicebankRootKind::Development,
      }};
  auto app = seam::standalone::NativeEditorApp::create(std::move(config));
  CHECK(app);

  auto [lyric, note] = app.value()->authoring().runtime().document().factory().makeNote(
      seam::time::Tick{0}, seam::time::Tick{1920}, 64U, U"こ",
      seam::domain::Language::Japanese);
  CHECK(app.value()->authoring().runtime().execute(
      std::make_unique<seam::application::AddNoteCommand>(
          app.value()->authoring().regionId(), std::move(lyric),
          std::move(note))));

  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (!app.value()->authoring().runtime().transport().state().available &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(2ms);
  }
  CHECK(app.value()->authoring().runtime().transport().state().available);

  seam::native_ui::PixelSurface surface{1280U, 720U};
  seam::native_ui::RasterCanvas canvas{surface, 1.0};
  app.value()->paint(canvas);
  const auto state = app.value()->authoring().controller().sceneState();
  CHECK(!state.audioDeviceOnline);
  CHECK(state.audioBackend == "threaded-callback-clock");
}

TEST_CASE("native editor audio settings resume a playing transport after restart") {
  const auto root = seam::test::support::temporaryDirectory("native-audio-settings-resume");
  seam::standalone::NativeEditorAppConfig config;
  config.runtimeMode = seam::standalone::ProductionRuntimeMode::Development;
  config.applicationSupportRoot = root;
  config.forceThreadedAudio = false;
  config.systemAudioDeviceFactory = [] {
    return std::make_unique<ScriptedAudioDevice>(
        false, true, "physical-test");
  };
  config.startPaused = false;
  auto app = seam::standalone::NativeEditorApp::create(std::move(config));
  CHECK(app);
  const auto initialDeadline = std::chrono::steady_clock::now() + 1s;
  while (!app.value()->authoring().runtime().transport().state().playing &&
         std::chrono::steady_clock::now() < initialDeadline) {
    std::this_thread::sleep_for(1ms);
  }
  CHECK(app.value()->authoring().runtime().transport().state().playing);

  auto requested = app.value()->audioSettings();
  CHECK(requested);
  requested.value().sampleRate = 44100U;
  requested.value().blockFrames = 128U;
  CHECK(app.value()->applyAudioSettings(requested.value()));
  const auto resumedDeadline = std::chrono::steady_clock::now() + 1s;
  while (!app.value()->authoring().runtime().transport().state().playing &&
         std::chrono::steady_clock::now() < resumedDeadline) {
    std::this_thread::sleep_for(1ms);
  }
  CHECK(app.value()->authoring().runtime().transport().state().playing);
}

TEST_CASE("native startup surfaces a persisted crash marker as a separate diagnostic") {
  if (seam::platform::crashCaptureBackendName() == "unavailable") return;
  const auto root = seam::test::support::temporaryDirectory("native-crash-recovery");
  const auto paths = seam::platform::ApplicationPaths::forTestRoot(root);
  const auto child = seam::test::support::runCrashCaptureProbe(
      paths.crashReportsRoot, "terminate");
  CHECK(child.launched);
  CHECK(child.completed);
  CHECK(child.abnormalExit);

  seam::standalone::NativeEditorAppConfig config;
  config.runtimeMode = seam::standalone::ProductionRuntimeMode::DeterministicTest;
  config.applicationSupportRoot = root;
  config.forceThreadedAudio = true;
  auto app = seam::standalone::NativeEditorApp::create(std::move(config));
  CHECK(app);
  CHECK(app.value()->startupCrashMarker().has_value());
  CHECK(app.value()->startupCrashMarker()->code == "TERMINATE");
  CHECK(app.value()->startupCrashMarker()->context.candidateId ==
        "candidate-crash-probe");
  const auto& entries = app.value()->authoring().controller().diagnosticPanel().entries();
  CHECK(!entries.empty());
  CHECK(entries.front().diagnostic.code == "CRASH_RECOVERY_AVAILABLE");
  CHECK(app.value()->authoring().controller().activateDiagnostic(
      0U, seam::authoring::DiagnosticAction::Dismiss));
  CHECK(!app.value()->startupCrashMarker().has_value());
  auto marker = seam::platform::CrashCapture::recoverPending(
      seam::platform::CrashCaptureConfig{.root = paths.crashReportsRoot});
  CHECK(marker);
  CHECK(!marker.value().has_value());
}

TEST_CASE("native crash support export contains recovered candidate context") {
  if (seam::platform::crashCaptureBackendName() == "unavailable") return;
  const auto root = seam::test::support::temporaryDirectory(
      "native-crash-support-context");
  const auto paths = seam::platform::ApplicationPaths::forTestRoot(root);
  const auto child = seam::test::support::runCrashCaptureProbe(
      paths.crashReportsRoot, "native");
  CHECK(child.launched);
  CHECK(child.completed);
  CHECK(child.abnormalExit);

  seam::standalone::NativeEditorAppConfig config;
  config.runtimeMode = seam::standalone::ProductionRuntimeMode::DeterministicTest;
  config.applicationSupportRoot = root;
  config.forceThreadedAudio = true;
  auto app = seam::standalone::NativeEditorApp::create(std::move(config));
  CHECK(app);
  CHECK(app.value()->authoring().controller().activateDiagnostic(
      0U, seam::authoring::DiagnosticAction::OpenSupport));
  const auto& preview =
      app.value()->authoring().controller().recoverySupportPanel().view();
  CHECK(preview.visible);
  CHECK(preview.candidateId == "candidate-crash-probe");
  CHECK(app.value()->authoring().controller().activateDiagnostic(
      0U, seam::authoring::DiagnosticAction::ExportSupportBundle));

  const auto supportRoot = root / "Data" / "Support";
  std::vector<std::filesystem::path> reports;
  for (const auto& entry : std::filesystem::directory_iterator{supportRoot}) {
    if (entry.is_regular_file() && entry.path().extension() == ".zip") {
      reports.push_back(entry.path());
    }
  }
  CHECK(reports.size() == 1U);
  CHECK(reports.front().filename().string().find("candidate-crash-probe") !=
        std::string::npos);
  auto bytes = seam::core::readFileBytesLimited(reports.front(), 8U * 1024U * 1024U);
  CHECK(bytes);
  const std::string payload(
      reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size());
  CHECK(payload.find("\"buildId\":\"candidate-crash-probe\"") !=
        std::string::npos);
  CHECK(payload.find("\"bankId\":\"beta-bank\"") != std::string::npos);
  CHECK(payload.find("\"underflowFrames\":\"17\"") != std::string::npos);
  CHECK(payload.find("\"xrunCount\":\"3\"") != std::string::npos);
  CHECK(payload.find(std::string{seam::platform::crashCaptureBackendName()}) !=
        std::string::npos);
  CHECK(app.value()->authoring().controller().activateDiagnostic(
      0U, seam::authoring::DiagnosticAction::DeleteSupportBundle));
}

TEST_CASE("native crash support refuses an unbound candidate") {
  if (seam::platform::crashCaptureBackendName() == "unavailable") return;
  const auto root = seam::test::support::temporaryDirectory(
      "native-crash-support-unbound");
  const auto paths = seam::platform::ApplicationPaths::forTestRoot(root);
  const auto child = seam::test::support::runCrashCaptureProbe(
      paths.crashReportsRoot, "terminate-no-context");
  CHECK(child.launched);
  CHECK(child.completed);
  CHECK(child.abnormalExit);

  seam::standalone::NativeEditorAppConfig config;
  config.runtimeMode = seam::standalone::ProductionRuntimeMode::DeterministicTest;
  config.applicationSupportRoot = root;
  config.forceThreadedAudio = true;
  auto app = seam::standalone::NativeEditorApp::create(std::move(config));
  CHECK(app);
  CHECK(!app.value()->authoring().controller().activateDiagnostic(
      0U, seam::authoring::DiagnosticAction::OpenSupport));
  CHECK(!std::filesystem::exists(root / "Data" / "Support"));
}

TEST_CASE("native support flow previews exports and deletes exact prepared bytes") {
  const auto root = seam::test::support::temporaryDirectory("native-support-flow");
  seam::standalone::NativeEditorAppConfig config;
  config.runtimeMode = seam::standalone::ProductionRuntimeMode::DeterministicTest;
  config.applicationSupportRoot = root;
  config.forceThreadedAudio = true;
  auto app = seam::standalone::NativeEditorApp::create(std::move(config));
  CHECK(app);
  app.value()->authoring().controller().setDiagnostics({seam::authoring::Diagnostic{
      .code = "PROJECT_NOT_FOUND",
      .severity = seam::authoring::DiagnosticSeverity::Error,
      .messageKey = "project.not-found",
      .affectedIds = {},
      .actions = seam::authoring::DiagnosticRegistry::actions(
          "PROJECT_NOT_FOUND"),
      .occurrenceCount = 1U}});

  CHECK(app.value()->authoring().controller().activateDiagnostic(
      0U, seam::authoring::DiagnosticAction::OpenSupport));
  const auto supportRoot = root / "Data" / "Support";
  CHECK(!std::filesystem::exists(supportRoot));
  auto diagnostics = app.value()->authoring().controller().diagnosticPanel().entries();
  CHECK(diagnostics.size() == 1U);
  CHECK(diagnostics.front().diagnostic.code == "SUPPORT_BUNDLE_PREVIEW_READY");
  auto supportView =
      app.value()->authoring().controller().recoverySupportPanel().view();
  CHECK(supportView.visible);
  CHECK(supportView.mode == seam::native_ui::RecoverySupportMode::Preview);
  CHECK(supportView.items.size() == 2U);
  const auto firstPreviewHash = supportView.archiveSha256;
  CHECK(app.value()->authoring().controller().keyDown(
      seam::native_ui::KeyEvent{.key = seam::native_ui::NativeKey::Escape}));
  CHECK(!app.value()
             ->authoring()
             .controller()
             .recoverySupportPanel()
             .view()
             .visible);
  CHECK(!std::filesystem::exists(supportRoot));
  app.value()->authoring().controller().setDiagnostics({seam::authoring::Diagnostic{
      .code = "PROJECT_NOT_FOUND",
      .severity = seam::authoring::DiagnosticSeverity::Error,
      .messageKey = "project.not-found",
      .affectedIds = {},
      .actions = seam::authoring::DiagnosticRegistry::actions(
          "PROJECT_NOT_FOUND"),
      .occurrenceCount = 1U}});
  CHECK(app.value()->authoring().controller().activateDiagnostic(
      0U, seam::authoring::DiagnosticAction::OpenSupport));
  CHECK(app.value()
            ->authoring()
            .controller()
            .recoverySupportPanel()
            .view()
            .archiveSha256 == firstPreviewHash);

  CHECK(app.value()->authoring().controller().activateDiagnostic(
      0U, seam::authoring::DiagnosticAction::ExportSupportBundle));
  CHECK(!std::filesystem::is_empty(supportRoot));
  diagnostics = app.value()->authoring().controller().diagnosticPanel().entries();
  CHECK(diagnostics.size() == 1U);
  CHECK(diagnostics.front().diagnostic.code == "SUPPORT_BUNDLE_EXPORTED");
  supportView = app.value()->authoring().controller().recoverySupportPanel().view();
  CHECK(supportView.mode == seam::native_ui::RecoverySupportMode::Reports);
  CHECK(supportView.reportCount == 1U);
  CHECK(supportView.items.front().sha256 == firstPreviewHash);

  app.value()->authoring().controller().setDiagnostics({seam::authoring::Diagnostic{
      .code = "PROJECT_NOT_FOUND",
      .severity = seam::authoring::DiagnosticSeverity::Error,
      .messageKey = "project.not-found",
      .affectedIds = {},
      .actions = seam::authoring::DiagnosticRegistry::actions(
          "PROJECT_NOT_FOUND"),
      .occurrenceCount = 1U}});
  CHECK(app.value()->authoring().controller().activateDiagnostic(
      0U, seam::authoring::DiagnosticAction::OpenSupport));
  const auto secondPreviewHash = app.value()
                                     ->authoring()
                                     .controller()
                                     .recoverySupportPanel()
                                     .view()
                                     .archiveSha256;
  CHECK(app.value()->authoring().controller().activateDiagnostic(
      0U, seam::authoring::DiagnosticAction::ExportSupportBundle));
  supportView = app.value()->authoring().controller().recoverySupportPanel().view();
  CHECK(supportView.reportCount == 2U);
  CHECK(std::count_if(supportView.items.begin(), supportView.items.end(),
                      [](const auto& item) { return item.selected; }) == 1);
  const auto selected = std::find_if(
      supportView.items.begin(), supportView.items.end(),
      [](const auto& item) { return item.selected; });
  CHECK(selected != supportView.items.end());
  CHECK(selected->sha256 == secondPreviewHash);

  CHECK(app.value()->authoring().controller().activateDiagnostic(
      0U, seam::authoring::DiagnosticAction::DeleteSupportBundle));
  supportView = app.value()->authoring().controller().recoverySupportPanel().view();
  CHECK(supportView.reportCount == 1U);
  CHECK(!std::filesystem::is_empty(supportRoot));
  CHECK(app.value()->authoring().controller().activateDiagnostic(
      0U, seam::authoring::DiagnosticAction::DeleteSupportBundle));
  CHECK(std::filesystem::is_empty(supportRoot));
  CHECK(!app.value()
             ->authoring()
             .controller()
             .recoverySupportPanel()
             .view()
             .visible);
}

TEST_CASE("native support keeps a committed export when directory refresh fails") {
  const auto root =
      seam::test::support::temporaryDirectory("native-support-refresh-failure");
  const auto supportRoot = root / "Data" / "Support";
  std::filesystem::create_directories(
      supportRoot / "project-seam-support-hostile.zip");
  seam::standalone::NativeEditorAppConfig config;
  config.runtimeMode = seam::standalone::ProductionRuntimeMode::DeterministicTest;
  config.applicationSupportRoot = root;
  config.forceThreadedAudio = true;
  auto app = seam::standalone::NativeEditorApp::create(std::move(config));
  CHECK(app);
  app.value()->authoring().controller().setDiagnostics({seam::authoring::Diagnostic{
      .code = "PROJECT_NOT_FOUND",
      .severity = seam::authoring::DiagnosticSeverity::Error,
      .messageKey = "project.not-found",
      .actions = seam::authoring::DiagnosticRegistry::actions(
          "PROJECT_NOT_FOUND"),
  }});

  CHECK(app.value()->authoring().controller().activateDiagnostic(
      0U, seam::authoring::DiagnosticAction::OpenSupport));
  CHECK(app.value()->authoring().controller().activateDiagnostic(
      0U, seam::authoring::DiagnosticAction::ExportSupportBundle));

  const auto& reports =
      app.value()->authoring().controller().recoverySupportPanel().view();
  CHECK(reports.visible);
  CHECK(reports.mode == seam::native_ui::RecoverySupportMode::Reports);
  CHECK(reports.items.size() == 1U);
  CHECK(reports.items.front().name.starts_with("project-seam-support-"));
  CHECK(app.value()->lastError().find("was exported") != std::string::npos);
}

TEST_CASE("native voicebank diagnostic opens the exact-card browser") {
  const auto root = seam::test::support::temporaryDirectory("native-voicebank-diagnostic");
  seam::standalone::NativeEditorAppConfig config;
  config.runtimeMode = seam::standalone::ProductionRuntimeMode::DeterministicTest;
  config.applicationSupportRoot = root;
  config.forceThreadedAudio = true;
  auto app = seam::standalone::NativeEditorApp::create(std::move(config));
  CHECK(app);
  app.value()->authoring().controller().setDiagnostics({seam::authoring::Diagnostic{
      .code = "BANK_COVERAGE_MISSING",
      .severity = seam::authoring::DiagnosticSeverity::Warning,
      .messageKey = "voicebank.coverage_missing",
      .affectedIds = {},
      .actions = seam::authoring::DiagnosticRegistry::actions(
          "BANK_COVERAGE_MISSING"),
      .occurrenceCount = 1U}});
  CHECK(app.value()->authoring().controller().activateDiagnostic(
      0U, seam::authoring::DiagnosticAction::ChooseVoicebank));
  CHECK(app.value()->authoring().controller().voicebankBrowserVisible());
}

TEST_CASE("native audio-unavailable diagnostic opens live audio settings") {
  const auto root = seam::test::support::temporaryDirectory(
      "native-audio-unavailable-diagnostic");
  seam::standalone::NativeEditorAppConfig config;
  config.runtimeMode = seam::standalone::ProductionRuntimeMode::DeterministicTest;
  config.applicationSupportRoot = root;
  config.forceThreadedAudio = true;
  auto app = seam::standalone::NativeEditorApp::create(std::move(config));
  CHECK(app);
  app.value()->authoring().controller().setDiagnostics({seam::authoring::Diagnostic{
      .code = "AUDIO_UNAVAILABLE",
      .severity = seam::authoring::DiagnosticSeverity::Error,
      .messageKey = "audio.unavailable",
      .affectedIds = {},
      .actions = seam::authoring::DiagnosticRegistry::actions(
          "AUDIO_UNAVAILABLE"),
      .occurrenceCount = 1U}});
  CHECK(app.value()->authoring().controller().activateDiagnostic(
      0U, seam::authoring::DiagnosticAction::OpenSettings));
  CHECK(app.value()->authoring().controller().audioSettingsVisible());
}

TEST_CASE("native startup promotes missing voicebank into an actionable diagnostic") {
  const auto root = seam::test::support::temporaryDirectory(
      "native-startup-bank-diagnostic");
  seam::standalone::NativeEditorAppConfig config;
  config.runtimeMode = seam::standalone::ProductionRuntimeMode::DeterministicTest;
  config.applicationSupportRoot = root;
  config.forceThreadedAudio = true;
  auto app = seam::standalone::NativeEditorApp::create(std::move(config));
  CHECK(app);
  CHECK(app.value()->authoring().runtime().selectedTrack().valid());
  const auto diagnostics = app.value()->authoring().runtime().diagnostics();
  const auto missing = std::find_if(
      diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.code == "BANK_MISSING";
      });
  CHECK(missing != diagnostics.end());
  CHECK(!missing->actions.empty());
  CHECK(missing->actions.front() ==
        seam::authoring::DiagnosticAction::InstallVoicebank);
}
