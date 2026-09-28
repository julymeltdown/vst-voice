#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/native_ui/voicebank_studio.hpp"
#include "seam/platform/recording_input_session.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "seam/voicebank_production/take_inspection_receipt.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace core = seam::core;
namespace platform = seam::platform;
namespace production = seam::voicebank_production;
using seam::native_ui::VoicebankStudioController;

struct FakeInputState final {
  platform::IAudioInputProcessor* processor{nullptr};
  platform::AudioInputDeviceInfo info{.backend = "Injected test microphone",
      .deviceName = "recording-import-parity", .sampleRate = 48000U,
      .blockFrames = 256U, .physical = true};
  platform::AudioInputDeviceStats stats{};
  bool running{false};

  void emit(std::span<const float> samples) {
    CHECK(processor != nullptr);
    CHECK(running);
    processor->process({.sampleRate = static_cast<double>(info.sampleRate),
        .frameCount = samples.size(), .mono = samples});
    ++stats.callbacks;
    stats.frames += static_cast<std::uint64_t>(samples.size());
  }
};

class FakeInputDevice final : public platform::IAudioInputDevice {
public:
  explicit FakeInputDevice(std::shared_ptr<FakeInputState> state)
      : state_(std::move(state)) {}

  core::Result<void> open(const platform::AudioInputDeviceConfig& config,
                          platform::IAudioInputProcessor& processor) override {
    if (config.sampleRate != state_->info.sampleRate ||
        config.blockFrames != state_->info.blockFrames) {
      return core::failure(core::ErrorCode::InvalidArgument,
          "Test capture requested an unexpected device format");
    }
    state_->processor = &processor;
    return core::success();
  }
  core::Result<void> start() override {
    state_->running = true;
    return core::success();
  }
  void stop() noexcept override { state_->running = false; }
  bool running() const noexcept override { return state_->running; }
  platform::AudioInputDeviceInfo info() const override { return state_->info; }
  platform::AudioInputDeviceStats stats() const noexcept override { return state_->stats; }

private:
  std::shared_ptr<FakeInputState> state_;
};

struct ProducerWorkspace final {
  std::filesystem::path root;
  std::filesystem::path workspace;
  production::VoicebankProductionProject project;

  explicit ProducerWorkspace(const std::filesystem::path& sharedRoot,
                             std::string workspaceName)
      : root(sharedRoot), workspace(root / std::move(workspaceName)) {
    const auto license = root / "fixture-license.txt";
    project = {.projectId = "recording-import-parity",
        .inventoryId = "fixture", .inventorySha256 = std::string(64U, 'a'),
        .selectedSourceStrategyId = "fixture",
        .licenseLocator = license.string(), .immutableAssetRoot = "assets"};
    project.licenseSha256 = core::sha256Hex(
        "Synthetic recording/import parity fixture; no singer qualification.");
    project.sourceStrategies = {{.id = "fixture",
        .kind = production::SourceStrategyKind::ProceduralSynthesis,
        .rights = production::Feasibility::Pass,
        .coverage = production::Feasibility::Pass,
        .listening = production::Feasibility::Pass,
        .permissions = {true, true, true, true},
        .licenseLocator = license.string(),
        .licenseSha256 = project.licenseSha256,
        .evidenceState = "SYNTHETIC_TEST_ONLY"}};
    project.operators = {{"producer", "PRODUCER"}};
    project.unitAssignments = {{.coverageKey = "sustain:a", .pitchLayer = 69,
        .promptId = "prompt-a", .plannedTakeId = "take-a"}};
    const auto actualLicenseHash = core::sha256File(license);
    CHECK(actualLicenseHash);
    if (actualLicenseHash) {
      project.licenseSha256 = actualLicenseHash.value();
      project.sourceStrategies.front().licenseSha256 = actualLicenseHash.value();
    }
    production::ProductionProjectRepository repository{workspace};
    CHECK(repository.initialize(project, {.action = "create",
        .subjectId = project.projectId, .operatorId = "producer",
        .occurredAtUtc = "2026-09-20T12:00:00Z"}));
  }
};

core::Result<void> drain(VoicebankStudioController& controller) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
  while (controller.proceduralImportBusy()) {
    const auto polled = controller.pollProceduralCandidateImport();
    if (!polled) return polled;
    if (std::chrono::steady_clock::now() >= deadline) {
      return core::failure(core::ErrorCode::Internal,
          "Studio raw WAV importer did not finish within 30 seconds");
    }
    if (controller.proceduralImportBusy()) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
  }
  return core::success();
}

std::filesystem::path captureFixtureWav(const std::filesystem::path& outputDirectory) {
  auto state = std::make_shared<FakeInputState>();
  platform::RecordingInputFactories factories;
  factories.physical = [state] { return std::make_unique<FakeInputDevice>(state); };
  factories.synthetic = {};
  platform::RecordingSession recording{48000U, 1U};
  platform::RecordingInputSession capture{recording, platform::RecordingInputMode::Physical,
      std::move(factories)};
  CHECK(capture.begin());
  CHECK(capture.capturing());
  CHECK(capture.info().physical);
  CHECK(capture.info().deviceName == "recording-import-parity");

  const auto samples = seam::test::support::sineWave(48000U, 440.0, 0.12, 0.25F);
  for (std::size_t offset = 0U; offset < samples.size(); offset += 256U) {
    const auto count = std::min<std::size_t>(256U, samples.size() - offset);
    state->emit(std::span<const float>{samples}.subspan(offset, count));
  }
  CHECK(recording.recordedFrames() == samples.size());
  CHECK(capture.finish());
  CHECK(capture.pending());

  const auto wavPath = outputDirectory / "captured.wav";
  CHECK(capture.exportPending(wavPath, seam::voicebank::WavSampleFormat::Pcm24));
  const auto audio = seam::voicebank::readWav(wavPath);
  CHECK(audio);
  if (audio) {
    CHECK(audio.value().sampleRate == 48000U);
    CHECK(audio.value().frameCount() == samples.size());
    CHECK(audio.value().bitsPerSample == 24U);
  }
  CHECK(capture.acknowledgePublished());
  CHECK(!capture.pending());
  CHECK(recording.recordedFrames() == 0U);
  return wavPath;
}

void assertImportedCapture(const VoicebankStudioController& controller,
                           const std::string& expectedSha256) {
  const auto* project = controller.productionProject();
  CHECK(project != nullptr);
  if (project == nullptr) return;
  CHECK(project->takes.size() == 1U);
  CHECK(project->metadataRevisions.size() == 1U);
  CHECK(project->reviews.empty());
  CHECK(project->unitAssignments.size() == 1U);
  if (project->takes.size() == 1U) {
    CHECK(project->takes.front().takeId == "take-a");
    CHECK(project->takes.front().rawAssetSha256 == expectedSha256);
    CHECK(project->takes.front().state == production::UnitQueueState::MarkerReview);
  }
  if (project->unitAssignments.size() == 1U) {
    const auto& assignment = project->unitAssignments.front();
    CHECK(assignment.takeId == "take-a");
    CHECK(assignment.state == production::UnitQueueState::MarkerReview);
    CHECK(!assignment.markerReviewed);
    CHECK(!assignment.pitchReviewed);
  }
  if (project->metadataRevisions.size() == 1U) {
    CHECK(project->metadataRevisions.front().kind == production::kTakeInspectionRevisionKind);
    const auto receipt = production::currentTakeInspection(*project, "take-a");
    CHECK(receipt.has_value());
    if (receipt) {
      CHECK(receipt->binding.coverageKey == "sustain:a");
      CHECK(receipt->binding.pitchLayer == 69);
      CHECK(receipt->inspection.sourceSha256 == expectedSha256);
      CHECK(receipt->inspection.accepted());
    }
  }
}
}  // namespace

TEST_CASE("fake microphone capture WAV imports through Studio and matches normal WAV import") {
  const auto root = seam::test::support::temporaryDirectory(
      "studio-recording-import-parity");
  CHECK(core::durableAtomicWriteTextNew(root / "fixture-license.txt",
      "Synthetic recording/import parity fixture; not source authorization."));
  const auto capturedWav = captureFixtureWav(root);
  const auto capturedSha = core::sha256File(capturedWav);
  CHECK(capturedSha);
  if (!capturedSha) return;

  ProducerWorkspace captureWorkspace{root, "capture-workspace"};
  ProducerWorkspace normalWavWorkspace{root, "normal-wav-workspace"};
  VoicebankStudioController capturedImport;
  VoicebankStudioController normalWavImport;
  CHECK(capturedImport.openProductionProject(captureWorkspace.workspace,
      captureWorkspace.project.inventorySha256, "producer", true));
  CHECK(normalWavImport.openProductionProject(normalWavWorkspace.workspace,
      normalWavWorkspace.project.inventorySha256, "producer", true));

  constexpr auto occurredAt = "2026-09-20T12:01:00Z";
  CHECK(capturedImport.beginRawTakeImport(capturedWav, occurredAt, capturedSha.value()));
  const auto capturedImportResult = drain(capturedImport);
  CHECK(capturedImportResult);
  CHECK(normalWavImport.inspectSelectedProductionTake(capturedWav));
  CHECK(normalWavImport.takeInspection().has_value());
  if (normalWavImport.takeInspection()) {
    CHECK(normalWavImport.takeInspection()->sourceSha256 == capturedSha.value());
    CHECK(normalWavImport.takeInspection()->accepted());
  }
  CHECK(normalWavImport.importSelectedTake(capturedWav, occurredAt));

  assertImportedCapture(capturedImport, capturedSha.value());
  assertImportedCapture(normalWavImport, capturedSha.value());
  if (capturedImport.productionProject() && normalWavImport.productionProject()) {
    const auto capturedState = production::encodeProductionProject(
        *capturedImport.productionProject());
    const auto normalWavState = production::encodeProductionProject(
        *normalWavImport.productionProject());
    CHECK(capturedState == normalWavState);
  }

  production::ProductionProjectRepository capturedRepository{captureWorkspace.workspace};
  production::ProductionProjectRepository normalWavRepository{normalWavWorkspace.workspace};
  const auto capturedDurable = capturedRepository.recover();
  const auto normalWavDurable = normalWavRepository.recover();
  CHECK(capturedDurable);
  CHECK(normalWavDurable);
  if (capturedDurable && normalWavDurable && capturedImport.productionProject() &&
      normalWavImport.productionProject()) {
    CHECK(production::encodeProductionProject(*capturedImport.productionProject()) ==
        production::encodeProductionProject(capturedDurable.value()));
    CHECK(production::encodeProductionProject(*normalWavImport.productionProject()) ==
        production::encodeProductionProject(normalWavDurable.value()));
    CHECK(production::encodeProductionProject(capturedDurable.value()) ==
        production::encodeProductionProject(normalWavDurable.value()));
    CHECK(capturedDurable.value().reviews.empty());
    CHECK(normalWavDurable.value().reviews.empty());
  }
}
