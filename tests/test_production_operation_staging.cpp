#include "test_framework.hpp"
#include "test_support.hpp"
#include "../libs/seam-voicebank-production/src/operation_staging_internal.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/project_codec.hpp"

#include <barrier>
#include <optional>
#include <thread>

namespace {
namespace production = seam::voicebank_production;

struct StagingFixture final {
  std::filesystem::path root{seam::test::support::temporaryDirectory("production-operation-staging")};
  production::ProductionProjectRepository repository{root / "workspace"};
  production::VoicebankProductionProject project;
  production::AssetRecord raw;

  StagingFixture() {
    const auto license = root / "synthetic-license.txt";
    CHECK(seam::core::durableAtomicWriteTextNew(license, "SYNTHETIC TEST ONLY; no production qualification"));
    const auto licenseHash = seam::core::sha256File(license); CHECK(licenseHash);
    project = {.projectId="operation-staging", .inventoryId="fixture", .inventorySha256=std::string(64U, 'a'),
      .selectedSourceStrategyId="synthetic", .licenseLocator=license.string(), .licenseSha256=licenseHash.value()};
    project.sourceStrategies = {{.id="synthetic", .kind=production::SourceStrategyKind::ProceduralSynthesis,
      .rights=production::Feasibility::Pass, .coverage=production::Feasibility::Pass, .listening=production::Feasibility::Pass,
      .permissions={true,true,true,true}, .licenseLocator=license.string(), .licenseSha256=licenseHash.value(),
      .evidenceState="SYNTHETIC_TEST_ONLY"}};
    project.operators = {{.operatorId="producer", .role="PRODUCER"}};
    project.unitAssignments = {{.coverageKey="sustain:a", .pitchLayer=69, .promptId="a", .plannedTakeId="take-a"}};
    CHECK(repository.initialize(project, event("create", project.projectId)));
    const auto audio = root / "raw.wav";
    CHECK(seam::voicebank::writeWav(audio, {.sampleRate=48000U, .channels=1U,
      .sampleFormat=seam::voicebank::WavSampleFormat::Pcm24},
      seam::test::support::sineWave(48000U, 440.0, 0.12, 0.25F)));
    const auto imported = repository.importRaw(project, audio,
      {.takeId="take-a", .promptId="a", .coverageKey="sustain:a", .pitchLayer=69}, event("import", "take-a"));
    CHECK(imported); raw = imported.value();
  }
  static production::ProductionJournalEvent event(std::string action, std::string subject) {
    return {.action=std::move(action), .subjectId=std::move(subject), .operatorId="producer",
      .occurredAtUtc="2026-09-09T10:00:00Z"};
  }
  production::StagedOperation stage(std::string id="normalized") const {
    const auto result = repository.stageOperation(project, "take-a", "",
        {.kind=production::OperationKind::NormalizeGain, .targetPeak=0.15F}, std::move(id));
    CHECK(result); return result.value();
  }
};

}  // namespace

TEST_CASE("staged execution descriptor binds every request field and exact owner identity") {
  StagingFixture fixture;
  const auto staged = fixture.stage();
  CHECK(production::validateStagedOperation(fixture.root / "workspace", staged));
  const auto before = production::encodeProductionProject(fixture.project);
  const std::vector<std::function<void(production::StagedOperation&)>> mutations{
    [](auto& value) { value.request.kind=production::OperationKind::Downmix; },
    [](auto& value) { value.request.targetPeak=0.9F; },
    // This change is invisible in operationParameters' six-decimal string.
    [](auto& value) { value.request.targetPeak=std::nextafter(value.request.targetPeak, 1.0F); },
    [](auto& value) { value.request.channelIndex=1U; },
    [](auto& value) { value.request.targetSampleRate=44100U; },
    [](auto& value) { value.request.startFrame=1U; },
    [](auto& value) { value.request.endFrame=100U; },
    [](auto& value) { value.inputSha256=std::string(64U, 'b'); },
    [](auto& value) { value.outputSha256=std::string(64U, 'c'); },
    [](auto& value) { value.takeId="take-other"; },
    [](auto& value) { value.parentRevisionId="parent-other"; },
    [](auto& value) { value.sourceProjectSha256=std::string(64U, 'd'); },
    [](auto& value) { value.stagingId="not-the-executed-stage"; },
    [](auto& value) { value.path=value.path.parent_path() / "other.wav"; },
  };
  for (const auto& mutate : mutations) {
    auto forged=staged; mutate(forged);
    CHECK(!production::validateStagedOperation(fixture.root / "workspace", forged));
    CHECK(!fixture.repository.commitStaged(fixture.project, forged, "forged-revision", "producer",
        "2026-09-09T10:01:00Z", "take-a", ""));
    CHECK(production::encodeProductionProject(fixture.project)==before);
  }
  // The preserved descriptor and PCM still permit the actual executed edit.
  const auto committed = fixture.repository.commitStaged(fixture.project, staged, "actual-revision", "producer",
      "2026-09-09T10:01:00Z", "take-a", "");
  CHECK(committed);
  CHECK(committed.value().parameters == production::operationParameters(staged.request));
}

TEST_CASE("recomputing staged project hash cannot rebind old PCM to a newer generation") {
  StagingFixture fixture;
  auto staged=fixture.stage();
  CHECK(fixture.repository.save(fixture.project, StagingFixture::event("save", fixture.project.projectId)));
  staged.sourceProjectSha256=seam::core::sha256Hex(production::encodeProductionProject(fixture.project));
  CHECK(!production::validateStagedOperation(fixture.root / "workspace", staged));
  CHECK(!fixture.repository.commitStaged(fixture.project, staged, "rebound", "producer",
      "2026-09-09T10:01:00Z", "take-a", ""));
  CHECK(fixture.project.derivedRevisions.empty());
}

TEST_CASE("copied staging files cannot rebind an execution descriptor to a different workspace") {
  StagingFixture fixture;
  auto staged=fixture.stage();
  const auto copy=fixture.root / "copied-workspace";
  std::filesystem::copy(fixture.root / "workspace", copy, std::filesystem::copy_options::recursive);
  staged.path=copy / "staging" / "normalized.wav";
  CHECK(!production::validateStagedOperation(copy, staged));
  CHECK(fixture.project.derivedRevisions.empty());
}

TEST_CASE("staged PCM24 bytes match the existing WAV writer for every supported operation") {
  StagingFixture fixture;
  const auto input=seam::voicebank::readWav(fixture.repository.assetPath(fixture.raw)); CHECK(input);
  const std::vector<production::OperationRequest> requests{
    {.kind=production::OperationKind::ChannelSelect},
    {.kind=production::OperationKind::Downmix},
    {.kind=production::OperationKind::Resample, .targetSampleRate=44100U},
    {.kind=production::OperationKind::RemoveDc},
    {.kind=production::OperationKind::NormalizeGain, .targetPeak=0.9F},
    {.kind=production::OperationKind::Trim, .startFrame=13U, .endFrame=511U},
    {.kind=production::OperationKind::Segment, .startFrame=20U, .endFrame=1024U},
  };
  for (std::size_t index=0U; index<requests.size(); ++index) {
    const auto staged=fixture.repository.stageOperation(fixture.project, "take-a", "", requests[index],
        "wire-"+std::to_string(index)); CHECK(staged);
    const auto processed=production::applyOperation(input.value(), requests[index]); CHECK(processed);
    const auto reference=fixture.root / ("reference-"+std::to_string(index)+".wav");
    CHECK(seam::voicebank::writeWav(reference, {.sampleRate=processed.value().sampleRate,
        .channels=processed.value().channels, .sampleFormat=seam::voicebank::WavSampleFormat::Pcm24},
        processed.value().interleaved));
    const auto expected=seam::core::sha256File(reference); CHECK(expected);
    CHECK(expected.value()==staged.value().outputSha256);
    CHECK(production::validateStagedOperation(fixture.root / "workspace", staged.value()));
  }
  CHECK(fixture.repository.inspectStaged(fixture.project).size()==requests.size());
}

TEST_CASE("staged operation requires its original durable descriptor after reopening") {
  StagingFixture fixture;
  const auto staged=fixture.stage();
  production::ProductionProjectRepository reopened{fixture.root / "workspace"};
  const auto recovered=reopened.recover(); CHECK(recovered);
  CHECK(production::validateStagedOperation(fixture.root / "workspace", staged));
  const auto descriptor=staged.path.parent_path() / "normalized.operation";
  // Simulate an interrupted/incomplete stage without destroying its evidence.
  std::filesystem::rename(descriptor, descriptor.string()+".saved");
  CHECK(!production::validateStagedOperation(fixture.root / "workspace", staged));
  CHECK(reopened.inspectStaged(recovered.value())==std::vector<std::filesystem::path>{staged.path});
  CHECK(!reopened.stageOperation(recovered.value(), "take-a", "", staged.request, staged.stagingId));
  std::filesystem::rename(descriptor.string()+".saved", descriptor);
  CHECK(production::validateStagedOperation(fixture.root / "workspace", staged));
  auto restored=recovered.value();
  CHECK(reopened.commitStaged(restored, staged, "reopened", "producer", "2026-09-09T10:01:00Z", "take-a", ""));
  CHECK(reopened.inspectStaged(restored).empty());
}

TEST_CASE("staging refuses occupied outputs descriptors and interrupted private files without overwrite") {
  for (const auto& name : {"occupied.wav", "occupied.operation", ".occupied.wav.pending", ".occupied.operation.pending"}) {
    StagingFixture fixture;
    const auto path=fixture.root / "workspace/staging" / name;
    CHECK(seam::core::durableAtomicWriteTextNew(path, "preserve-existing-bytes"));
    const auto before=production::encodeProductionProject(fixture.project);
    CHECK(!fixture.repository.stageOperation(fixture.project, "take-a", "",
        {.kind=production::OperationKind::NormalizeGain, .targetPeak=0.9F}, "occupied"));
    const auto bytes=seam::core::readTextFileLimited(path, 1024U); CHECK(bytes);
    CHECK(bytes.value()=="preserve-existing-bytes");
    CHECK(production::encodeProductionProject(fixture.project)==before);
  }
}

TEST_CASE("concurrent same-ID stages have one winner and cannot truncate its executed audio") {
  StagingFixture fixture;
  std::barrier start{2};
  std::optional<seam::core::Result<production::StagedOperation>> left, right;
  std::jthread first([&] {
    start.arrive_and_wait();
    left=fixture.repository.stageOperation(fixture.project, "take-a", "",
        {.kind=production::OperationKind::NormalizeGain, .targetPeak=0.15F}, "contended");
  });
  std::jthread second([&] {
    start.arrive_and_wait();
    right=fixture.repository.stageOperation(fixture.project, "take-a", "",
        {.kind=production::OperationKind::NormalizeGain, .targetPeak=0.85F}, "contended");
  });
  first.join(); second.join();
  CHECK(left && right);
  CHECK(left->hasValue()!=right->hasValue());
  const auto& winner=left->hasValue() ? left->value() : right->value();
  const auto& loser=left->hasValue() ? right->error() : left->error();
  CHECK(loser.code==seam::core::ErrorCode::Conflict);
  CHECK(production::validateStagedOperation(fixture.root / "workspace", winner));
  const auto audio=seam::voicebank::readWav(winner.path); CHECK(audio);
  CHECK_NEAR(seam::voicebank::analyzeAudio(audio.value().interleaved).peak, winner.request.targetPeak, 0.000001);
  CHECK(fixture.repository.inspectStaged(fixture.project).size()==1U);
  CHECK(fixture.project.derivedRevisions.empty());
}

#if !defined(_WIN32)
// Native Windows symlink creation needs developer mode or extra privileges;
// POSIX executes these containment cases without either requirement.
TEST_CASE("staging parent symlink cannot redirect writes outside the producer workspace") {
  StagingFixture fixture;
  const auto outside=fixture.root / "outside-workspace";
  CHECK(std::filesystem::create_directory(outside));
  CHECK(seam::core::durableAtomicWriteTextNew(outside / "sentinel", "unchanged"));
  const auto parent=fixture.root / "workspace/staging";
  CHECK(std::filesystem::remove(parent));  // Only this fixture's known empty directory.
  std::filesystem::create_directory_symlink(outside, parent);
  CHECK(!fixture.repository.stageOperation(fixture.project, "take-a", "",
      {.kind=production::OperationKind::NormalizeGain, .targetPeak=0.9F}, "escaped"));
  CHECK(std::distance(std::filesystem::directory_iterator{outside}, std::filesystem::directory_iterator{})==1);
  const auto sentinel=seam::core::readTextFileLimited(outside / "sentinel", 1024U); CHECK(sentinel);
  CHECK(sentinel.value()=="unchanged");
}

TEST_CASE("occupied symlink outputs and lock files cannot overwrite external targets") {
  for (const auto& name : {"escaped.wav", "escaped.operation", ".escaped.wav.pending", ".escaped.lock"}) {
    StagingFixture fixture;
    const auto outside=fixture.root / "external-sentinel";
    CHECK(seam::core::durableAtomicWriteTextNew(outside, "unchanged"));
    std::filesystem::create_symlink(outside, fixture.root / "workspace/staging" / name);
    CHECK(!fixture.repository.stageOperation(fixture.project, "take-a", "",
        {.kind=production::OperationKind::NormalizeGain, .targetPeak=0.9F}, "escaped"));
    const auto sentinel=seam::core::readTextFileLimited(outside, 1024U); CHECK(sentinel);
    CHECK(sentinel.value()=="unchanged");
  }
}

TEST_CASE("stage validation refuses replaced parent and symlinked descriptor or audio") {
  StagingFixture fixture;
  const auto staged=fixture.stage();
  const auto parent=staged.path.parent_path();
  const auto preserved=fixture.root / "preserved-staging";
  std::filesystem::rename(parent, preserved);
  std::filesystem::create_directory_symlink(preserved, parent);
  CHECK(!production::validateStagedOperation(fixture.root / "workspace", staged));
  CHECK(std::filesystem::remove(parent));  // Removes only the fixture's symlink.
  std::filesystem::rename(preserved, parent);
  for (const auto& path : {staged.path, parent / "normalized.operation"}) {
    const auto original=std::filesystem::path{path.string()+".original"};
    std::filesystem::rename(path, original);
    std::filesystem::create_symlink(original, path);
    CHECK(!production::validateStagedOperation(fixture.root / "workspace", staged));
    CHECK(std::filesystem::remove(path));
    std::filesystem::rename(original, path);
  }
  CHECK(production::validateStagedOperation(fixture.root / "workspace", staged));
}
#endif

TEST_CASE("staged audio tampering cannot be legitimized by recomputing the public digest") {
  StagingFixture fixture;
  auto staged=fixture.stage();
  // Deliberate privileged fixture tampering; normal staging never overwrites.
  std::filesystem::permissions(staged.path, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
  CHECK(seam::core::durableAtomicWriteText(staged.path, "not the executed WAV"));
  CHECK(!production::validateStagedOperation(fixture.root / "workspace", staged));
  const auto digest=seam::core::sha256File(staged.path); CHECK(digest);
  staged.outputSha256=digest.value();
  CHECK(!production::validateStagedOperation(fixture.root / "workspace", staged));
  CHECK(!fixture.repository.commitStaged(fixture.project, staged, "tampered-audio", "producer",
      "2026-09-09T10:01:00Z", "take-a", ""));
}

TEST_CASE("staged descriptor corruption cannot be treated as execution evidence") {
  StagingFixture fixture;
  const auto staged=fixture.stage();
  const auto descriptor=staged.path.parent_path() / "normalized.operation";
  std::filesystem::permissions(descriptor, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
  CHECK(seam::core::durableAtomicWriteText(descriptor, "SEAM-STAGED-OPERATION\n999\n"));
  CHECK(!production::validateStagedOperation(fixture.root / "workspace", staged));
  CHECK(!fixture.repository.commitStaged(fixture.project, staged, "tampered-descriptor", "producer",
      "2026-09-09T10:01:00Z", "take-a", ""));
}
