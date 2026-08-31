#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

TEST_CASE("voicebank production project recovers staged deterministic work") {
  namespace production = seam::voicebank_production;
  const auto root = seam::test::support::temporaryDirectory("voicebank-production");
  const auto licensePath = root / "provider-agreement.txt";
  CHECK(seam::core::durableAtomicWriteText(licensePath, "synthetic-test-license"));
  const auto licenseDigest = seam::core::sha256File(licensePath);
  CHECK(licenseDigest);

  production::VoicebankProductionProject project{
      .projectId = "beta-bank-production-test",
      .inventoryId = "test-ja-inventory",
      .inventorySha256 = std::string(64U, 'a'),
      .selectedSourceStrategyId = "human-contract-recording",
      .licenseLocator = licensePath.string(),
      .licenseSha256 = licenseDigest.value(),
      .immutableAssetRoot = "assets",
  };
  project.sourceStrategies.push_back(production::SourceStrategyAssessment{
      .id = "human-contract-recording",
      .kind = production::SourceStrategyKind::HumanRecording,
      .rights = production::Feasibility::Pass,
      .coverage = production::Feasibility::Pass,
      .listening = production::Feasibility::Pass,
      .permissions = {.sourceUse = true,
                      .transformation = true,
                      .singingBankRedistribution = true,
                      .commercialRenders = true},
      .licenseLocator = licensePath.string(),
      .licenseSha256 = licenseDigest.value(),
      .evidenceState = "SYNTHETIC_TEST_ONLY",
  });
  project.unitAssignments = {
      {.coverageKey = "cv:k:a", .pitchLayer = 60,
       .promptId = "prompt-001", .plannedTakeId = "take-001"},
      {.coverageKey = "cv:k:a", .pitchLayer = 72,
       .promptId = "prompt-002", .plannedTakeId = "take-003"},
  };
  project.operators = {
      {.operatorId = "operator-a", .role = "PRODUCER"},
      {.operatorId = "reviewer-a", .role = "REVIEWER"},
  };

  const auto outsideWorkspace = root / "outside-workspace";
  CHECK(std::filesystem::create_directories(outsideWorkspace));
  const auto linkedWorkspace = root / "linked-workspace";
  std::error_code symlinkError;
  std::filesystem::create_directory_symlink(
      outsideWorkspace, linkedWorkspace, symlinkError);
  if (!symlinkError) {
    auto linkedProject = project;
    production::ProductionProjectRepository linkedRepository{linkedWorkspace};
    CHECK(!linkedRepository.initialize(
        linkedProject,
        {.action = "create", .subjectId = linkedProject.projectId,
         .operatorId = "operator-a",
         .occurredAtUtc = "2026-08-31T09:59:00Z"}));
  }

  production::ProductionProjectRepository repository{root / "workspace"};
  CHECK(repository.initialize(
      project, {.action = "create", .subjectId = project.projectId,
                .operatorId = "operator-a", .occurredAtUtc = "2026-08-31T10:00:00Z"}));
  CHECK(project.lastDurableGeneration == 1U);

  const auto mono = seam::test::support::sineWave(44100U, 220.0, 0.1, 0.25F);
  std::vector<float> stereo;
  stereo.reserve(mono.size() * 2U);
  for (const auto sample : mono) {
    stereo.push_back(sample + 0.02F);
    stereo.push_back(sample * 0.5F + 0.02F);
  }
  const auto sourcePath = root / "source.wav";
  CHECK(seam::voicebank::writeWav(
      sourcePath,
      {.sampleRate = 44100U, .channels = 2U,
       .sampleFormat = seam::voicebank::WavSampleFormat::Pcm24},
      stereo));

  const auto imported = repository.importRaw(
      project, sourcePath,
      {.takeId = "take-001", .promptId = "prompt-001",
       .coverageKey = "cv:k:a", .pitchLayer = 60,
       .review = production::ReviewRecord{
           .reviewId = "dry-take-take-001", .takeId = "take-001",
           .reviewerId = "operator-a", .result = "PASS",
           .reviewedAtUtc = "2026-08-31T10:01:00Z"}},
      {.action = "import", .subjectId = "take-001", .operatorId = "operator-a",
       .occurredAtUtc = "2026-08-31T10:01:00Z"});
  CHECK(imported);
  CHECK(project.lastDurableGeneration == 2U);
  CHECK(project.reviews.size() == 1U);
  CHECK(std::filesystem::is_regular_file(repository.assetPath(imported.value())));

  auto escapedAsset = imported.value();
  escapedAsset.relativePath = "../../source.wav";
  CHECK(!repository.stageOperation(
      escapedAsset,
      {.kind = production::OperationKind::Downmix}, "escaped-asset"));

  auto unboundAssignment = project;
  unboundAssignment.unitAssignments[0].takeId = "missing-take";
  CHECK(!production::validateProductionProject(unboundAssignment));

  auto mismatchedLicense = project;
  mismatchedLicense.sourceStrategies[0].licenseSha256 = std::string(64U, 'b');
  CHECK(!production::validateProductionProject(mismatchedLicense));

  const production::OperationRequest operation{
      .kind = production::OperationKind::Downmix,
  };
  const auto staged = repository.stageOperation(imported.value(), operation, "stage-001");
  const auto repeated = repository.stageOperation(imported.value(), operation, "stage-002");
  CHECK(staged);
  CHECK(repeated);
  CHECK(staged.value().outputSha256 == repeated.value().outputSha256);
  CHECK(repository.inspectStaged(project).size() == 2U);
  CHECK(seam::core::durableAtomicWriteText(root / "workspace" / "project.json",
                                           "interrupted-pointer"));

  production::ProductionProjectRepository recoveredRepository{root / "workspace"};
  const auto recovered = recoveredRepository.recover();
  CHECK(recovered);
  CHECK(recovered.value().lastDurableGeneration == 2U);
  CHECK(recovered.value().takes.size() == 1U);
  CHECK(recovered.value().unitAssignments[0].plannedTakeId == "take-001");
  CHECK(recoveredRepository.inspectStaged(recovered.value()).size() == 2U);

  auto resumed = recovered.value();
  const auto revision = recoveredRepository.commitStaged(
      resumed, staged.value(), "revision-001", "operator-a", "2026-08-31T10:02:00Z");
  CHECK(revision);
  CHECK(resumed.lastDurableGeneration == 3U);
  CHECK(recoveredRepository.inspectStaged(resumed).empty());
  auto brokenRevisionChain = resumed;
  brokenRevisionChain.derivedRevisions[0].inputSha256 =
      brokenRevisionChain.derivedRevisions[0].outputSha256;
  CHECK(!production::validateProductionProject(brokenRevisionChain));
  auto unknownRevisionOperator = resumed;
  unknownRevisionOperator.derivedRevisions[0].operatorId = "unknown-operator";
  CHECK(!production::validateProductionProject(unknownRevisionOperator));
  resumed.unitAssignments[0].takeId = "take-001";
  resumed.unitAssignments[0].state = production::UnitQueueState::MarkerReview;
  resumed.unitAssignments[0].markerReviewed = true;
  resumed.unitAssignments[0].pitchReviewed = true;
  resumed.unitAssignments[0].state = production::UnitQueueState::Approved;
  resumed.takes[0].state = production::UnitQueueState::Approved;
  CHECK(recoveredRepository.save(
      resumed, {.action = "review", .subjectId = "take-001", .operatorId = "reviewer-a",
                .occurredAtUtc = "2026-08-31T10:03:00Z"}));

  const auto queues = production::summarizeQueues(resumed);
  CHECK(queues.approved == 1U);
  CHECK(queues.missing == 1U);
  CHECK(production::selectedStrategyReady(resumed));
  CHECK(recoveredRepository.verify(resumed));
  auto divergedFromGeneration = resumed;
  divergedFromGeneration.projectId = "different-project";
  CHECK(!recoveredRepository.verify(divergedFromGeneration));

  const auto exported = recoveredRepository.exportU57Inputs(
      resumed, root / "u57-inputs",
      {.action = "candidate-export", .subjectId = resumed.projectId,
       .operatorId = "operator-a", .occurredAtUtc = "2026-08-31T10:04:00Z"});
  CHECK(exported);
  CHECK(std::filesystem::is_regular_file(exported.value().briefPath));
  CHECK(std::filesystem::is_regular_file(exported.value().candidateTemplatePath));
  CHECK(exported.value().status == "SYNTHETIC_READY_REAL_ASSETS_REQUIRED");

  const auto retake = recoveredRepository.importRaw(
      resumed, sourcePath,
      {.takeId = "take-002", .promptId = "prompt-001",
       .coverageKey = "cv:k:a", .pitchLayer = 60,
       .supersedesTakeId = "take-001"},
      {.action = "retake", .subjectId = "take-002", .operatorId = "operator-a",
       .occurredAtUtc = "2026-08-31T10:05:00Z"});
  CHECK(retake);
  CHECK(resumed.takes.size() == 2U);
  CHECK(resumed.takes[0].state == production::UnitQueueState::Retake);
  CHECK(resumed.takes[1].supersedesTakeId == "take-001");
  CHECK(resumed.unitAssignments[0].takeId == "take-002");

  const auto unsupported = recoveredRepository.stageOperation(
      imported.value(),
      {.kind = static_cast<production::OperationKind>(999)}, "unsupported");
  CHECK(!unsupported);

  const auto generationBeforeFailedExport = resumed.lastDurableGeneration;
  const auto invalidExportDestination = root / "not-a-directory";
  CHECK(seam::core::durableAtomicWriteText(invalidExportDestination, "occupied"));
  CHECK(!recoveredRepository.exportU57Inputs(
      resumed, invalidExportDestination,
      {.action = "candidate-export", .subjectId = resumed.projectId,
       .operatorId = "operator-a", .occurredAtUtc = "2026-08-31T10:06:00Z"}));
  CHECK(resumed.lastDurableGeneration == generationBeforeFailedExport);

  CHECK(!recoveredRepository.save(
      resumed, {.action = "save", .subjectId = resumed.projectId,
                .operatorId = "operator-a", .occurredAtUtc = "not-a-timestamp"}));
}

TEST_CASE("voicebank production PCM operations are bounded and deterministic") {
  namespace production = seam::voicebank_production;
  const seam::voicebank::AudioBuffer stereo{
      .sampleRate = 4U,
      .channels = 2U,
      .bitsPerSample = 24U,
      .interleaved = {0.1F, 0.3F, 0.2F, 0.4F, 0.3F, 0.5F, 0.4F, 0.6F},
  };
  const auto selected = production::applyOperation(
      stereo, {.kind = production::OperationKind::ChannelSelect,
               .channelIndex = 1U});
  CHECK(selected);
  CHECK(selected.value().channels == 1U);
  CHECK_NEAR(selected.value().interleaved[0], 0.3, 1e-6);

  const auto downmixed = production::applyOperation(
      stereo, {.kind = production::OperationKind::Downmix});
  CHECK(downmixed);
  CHECK_NEAR(downmixed.value().interleaved[0], 0.2, 1e-6);

  const auto resampled = production::applyOperation(
      downmixed.value(), {.kind = production::OperationKind::Resample,
                          .targetSampleRate = 8U});
  CHECK(resampled);
  CHECK(resampled.value().frameCount() == 8U);

  const auto withoutDc = production::applyOperation(
      downmixed.value(), {.kind = production::OperationKind::RemoveDc});
  CHECK(withoutDc);
  CHECK_NEAR(seam::voicebank::analyzeAudio(withoutDc.value().interleaved).dcOffset,
             0.0, 1e-6);

  const auto normalized = production::applyOperation(
      withoutDc.value(), {.kind = production::OperationKind::NormalizeGain,
                          .targetPeak = 0.5F});
  CHECK(normalized);
  CHECK_NEAR(seam::voicebank::analyzeAudio(normalized.value().interleaved).peak,
             0.5, 1e-6);

  for (const auto kind : {production::OperationKind::Trim,
                          production::OperationKind::Segment}) {
    const auto sliced = production::applyOperation(
        stereo, {.kind = kind, .startFrame = 1U, .endFrame = 3U});
    CHECK(sliced);
    CHECK(sliced.value().frameCount() == 2U);
  }
  CHECK(!production::applyOperation(
      stereo, {.kind = production::OperationKind::ChannelSelect,
               .channelIndex = 2U}));
  CHECK(!production::applyOperation(
      stereo, {.kind = production::OperationKind::Resample,
               .targetSampleRate = 0U}));
  CHECK(!production::applyOperation(
      stereo, {.kind = production::OperationKind::Trim,
               .startFrame = 3U, .endFrame = 1U}));

  const seam::voicebank::AudioBuffer oneFrame{
      .sampleRate = 48000U,
      .channels = 1U,
      .bitsPerSample = 24U,
      .interleaved = {0.25F},
  };
  const auto oneFrameResampled = production::applyOperation(
      oneFrame, {.kind = production::OperationKind::Resample,
                 .targetSampleRate = 1U});
  CHECK(oneFrameResampled);
  CHECK(oneFrameResampled.value().frameCount() == 1U);
}
