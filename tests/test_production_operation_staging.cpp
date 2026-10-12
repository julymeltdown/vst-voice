#include "test_framework.hpp"
#include "test_support.hpp"
#include "../libs/seam-voicebank-production/src/operation_staging_internal.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/take_inspection_receipt.hpp"

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

  explicit StagingFixture(std::uint32_t rate = 48000U,
      seam::voicebank::WavSampleFormat format = seam::voicebank::WavSampleFormat::Pcm24) {
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
    CHECK(seam::voicebank::writeWav(audio, {.sampleRate=rate, .channels=1U,
      .sampleFormat=format}, seam::test::support::sineWave(rate, 440.0, 0.12, 0.25F)));
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
  production::AssetRecord legacyEdit() {
    // Synthetic pre-diagnostic writer: an append-only edit without a receipt.
    const auto staged = repository.stageOperation(project,"take-a","",
        {.kind=production::OperationKind::Resample,.targetSampleRate=44100U},"legacy-stage"); CHECK(staged);
    production::ImmutableAssetStore store{root/"workspace"/"assets"};
    const auto asset=store.importFile(staged.value().path,production::AssetKind::Derived); CHECK(asset);
    project.assets.push_back(asset.value());
    project.derivedRevisions.push_back({.revisionId="legacy-edit",.inputSha256=staged.value().inputSha256,
        .outputSha256=staged.value().outputSha256,.operation=production::OperationKind::Resample,
        .operationVersion="seam-pcm-ops-1",.parameters={{"targetSampleRate","44100"}},
        .operatorId="producer",.performedAtUtc="2026-09-09T10:01:00Z"});
    project.takes.front().derivedRevisionIds.push_back("legacy-edit");
    CHECK(repository.save(project,{"transform","legacy-edit","producer","2026-09-09T10:01:00Z"}));
    CHECK(repository.verify(project));
    CHECK(!production::currentActiveTakeInspection(project,"take-a"));
    return asset.value();
  }
};

}  // namespace

TEST_CASE("explicit legacy reinspection preserves bytes lineage and failed diagnostics without approval") {
  StagingFixture fixture;
  const auto audio = fixture.legacyEdit();
  const auto before = fixture.project;
  const auto hash = seam::core::sha256Hex(production::encodeProductionProject(before));
  const auto recorded = fixture.repository.recordCurrentTakeInspection(fixture.project,"take-a","legacy-edit",hash,
      "producer","2026-09-09T10:02:00Z"); CHECK(recorded);
  CHECK(recorded.value().kind==production::kDerivedTakeInspectionRevisionKind);
  CHECK(recorded.value().committedGeneration==before.lastDurableGeneration+1U);
  CHECK(recorded.value().committedProjectSha256==seam::core::sha256Hex(production::encodeProductionProject(fixture.project)));
  CHECK(fixture.project.assets.size()==before.assets.size());
  CHECK(fixture.project.derivedRevisions.size()==before.derivedRevisions.size());
  CHECK(fixture.project.takes.front().derivedRevisionIds==before.takes.front().derivedRevisionIds);
  CHECK(fixture.project.metadataRevisions.size()==before.metadataRevisions.size()+1U);
  CHECK(std::equal(before.metadataRevisions.begin(),before.metadataRevisions.end(),fixture.project.metadataRevisions.begin(),production::sameMetadataRevision));
  const auto current=production::currentActiveTakeInspection(fixture.project,"take-a"); CHECK(current);
  CHECK(current->inspection.sourceSha256==audio.sha256);
  CHECK(current->inspection.outcome(seam::voicebank::TakeCheck::Format)==seam::voicebank::TakeCheckOutcome::Fail);
  CHECK(seam::core::sha256File(fixture.repository.assetPath(audio)).value()==audio.sha256);
  CHECK(seam::core::sha256File(fixture.repository.assetPath(fixture.raw)).value()==fixture.raw.sha256);
  CHECK(fixture.project.reviews.empty());
  CHECK(!fixture.project.unitAssignments.front().markerReviewed);
  CHECK(!fixture.project.unitAssignments.front().pitchReviewed);
  CHECK(fixture.project.unitAssignments.front().state==production::UnitQueueState::MarkerReview);
  const auto after=production::encodeProductionProject(fixture.project);
  CHECK(!fixture.repository.recordCurrentTakeInspection(fixture.project,"take-a","legacy-edit",
      seam::core::sha256Hex(after),"producer","2026-09-09T10:03:00Z"));
  CHECK(production::encodeProductionProject(fixture.project)==after);
  CHECK(production::encodeProductionProject(fixture.repository.recover().value())==after);
}

TEST_CASE("legacy reinspection refuses stale captures non-producers cancellation and revoked permission") {
  StagingFixture fixture;
  fixture.project.operators.push_back({"reviewer","REVIEWER"});
  CHECK(fixture.repository.save(fixture.project,StagingFixture::event("save","reviewer-fixture")));
  fixture.legacyEdit();
  const auto before=production::encodeProductionProject(fixture.project);
  const auto hash=seam::core::sha256Hex(before);
  const auto attempt=[&](std::string_view take,std::string_view revision,std::string_view captured,std::string actor,std::string utc) {
    CHECK(!fixture.repository.recordCurrentTakeInspection(fixture.project,take,revision,captured,std::move(actor),std::move(utc)));
    CHECK(production::encodeProductionProject(fixture.project)==before);
    CHECK(production::encodeProductionProject(fixture.repository.recover().value())==before);
  };
  attempt("take-a","legacy-edit",std::string(64U,'0'),"producer","2026-09-09T10:02:00Z");
  attempt("take-a","",hash,"producer","2026-09-09T10:02:00Z");
  attempt("take-a","other-edit",hash,"producer","2026-09-09T10:02:00Z");
  attempt("take-a","legacy-edit",hash,"reviewer","2026-09-09T10:02:00Z");
  attempt("take-a","legacy-edit",hash,"unknown","2026-09-09T10:02:00Z");
  attempt("missing","",hash,"producer","2026-09-09T10:02:00Z");
  attempt("take-a","legacy-edit",hash,"producer","not-a-utc");
  std::stop_source cancelled;cancelled.request_stop();
  CHECK(!fixture.repository.recordCurrentTakeInspection(fixture.project,"take-a","legacy-edit",hash,"producer",
      "2026-09-09T10:02:00Z",cancelled.get_token()));
  CHECK(production::encodeProductionProject(fixture.project)==before);
  auto stale=fixture.project;
  CHECK(fixture.repository.recordMetadataRevision(fixture.project,{"note","take-a",fixture.raw.sha256,
      "MARKERS_AND_PITCH",{{"vowelOnset","120"}},"producer","2026-09-09T10:02:00Z"},
      {"marker","note","producer","2026-09-09T10:02:00Z"}));
  CHECK(!fixture.repository.recordCurrentTakeInspection(stale,"take-a","legacy-edit",hash,"producer","2026-09-09T10:03:00Z"));
  CHECK(production::encodeProductionProject(stale)==before);
  fixture.project.sourceStrategies.front().permissions.transformation=false;
  CHECK(fixture.repository.save(fixture.project,StagingFixture::event("save","revoke-transform")));
  const auto denied=production::encodeProductionProject(fixture.project);
  CHECK(!fixture.repository.recordCurrentTakeInspection(fixture.project,"take-a","legacy-edit",seam::core::sha256Hex(denied),
      "producer","2026-09-09T10:04:00Z"));
  CHECK(production::encodeProductionProject(fixture.project)==denied);
  CHECK(!production::currentActiveTakeInspection(fixture.project,"take-a"));
}

TEST_CASE("current inspection can fill an absent raw receipt in a separate synthetic workspace") {
  StagingFixture fixture;
  auto rawOnly=fixture.project;
  rawOnly.lastDurableGeneration=0U;
  std::erase_if(rawOnly.metadataRevisions,[](const auto& revision) {
    return revision.kind==production::kTakeInspectionRevisionKind;
  });
  const auto root=fixture.root/"raw-without-diagnostics";
  production::ProductionProjectRepository repository{root};
  for (const auto& asset:rawOnly.assets) {
    const auto destination=repository.assetPath(asset);
    std::filesystem::create_directories(destination.parent_path());
    std::filesystem::copy_file(fixture.repository.assetPath(asset),destination);
  }
  std::filesystem::copy(fixture.root/"workspace"/"source-evidence",root/"source-evidence",std::filesystem::copy_options::recursive);
  CHECK(repository.save(rawOnly,StagingFixture::event("create",rawOnly.projectId)));
  CHECK(repository.verify(rawOnly));
  const auto before=production::encodeProductionProject(rawOnly);
  const auto result=repository.recordCurrentTakeInspection(rawOnly,"take-a","",seam::core::sha256Hex(before),
      "producer","2026-09-09T10:02:00Z");CHECK(result);
  CHECK(result.value().kind==production::kTakeInspectionRevisionKind);
  CHECK(production::currentActiveTakeInspection(rawOnly,"take-a")->inspection.sourceSha256==fixture.raw.sha256);
  CHECK(rawOnly.derivedRevisions.empty());
  CHECK(rawOnly.reviews.empty());
  CHECK(seam::core::sha256File(repository.assetPath(fixture.raw)).value()==fixture.raw.sha256);
  CHECK(production::encodeProductionProject(fixture.repository.recover().value())==production::encodeProductionProject(fixture.project));
}

TEST_CASE("converted take inspection describes the derivative while preserving failing raw evidence") {
  StagingFixture fixture{44100U, seam::voicebank::WavSampleFormat::Pcm16};
  const auto rawReceipt = production::currentTakeInspection(fixture.project, "take-a");
  CHECK(rawReceipt);
  CHECK(rawReceipt->inspection.outcome(seam::voicebank::TakeCheck::Format) == seam::voicebank::TakeCheckOutcome::Fail);
  const auto rawMetadata = fixture.project.metadataRevisions;
  const auto staged = fixture.repository.stageOperation(fixture.project, "take-a", "",
      {.kind=production::OperationKind::Resample, .targetSampleRate=48000U}, "converted");
  CHECK(staged);
  CHECK(fixture.repository.commitStaged(fixture.project, staged.value(), "converted-revision", "producer",
      "2026-09-09T10:01:00Z", "take-a", ""));
  const auto receipt = std::find_if(fixture.project.metadataRevisions.begin(), fixture.project.metadataRevisions.end(),
      [](const auto& value) { return value.kind == "derived-take-inspection.v1"; });
  CHECK(receipt != fixture.project.metadataRevisions.end());
  const auto evidence = production::validateTakeInspectionRevision(fixture.project, *receipt);
  CHECK(evidence);
  CHECK(evidence.value().inspection.sourceSha256 == staged.value().outputSha256);
  CHECK(evidence.value().inspection.measurements.sampleRate == 48000U);
  CHECK(evidence.value().inspection.measurements.bitsPerSample == 24U);
  CHECK(evidence.value().inspection.outcome(seam::voicebank::TakeCheck::Format) == seam::voicebank::TakeCheckOutcome::Pass);
  const auto active = production::currentActiveTakeInspection(fixture.project, "take-a");
  CHECK(active);
  CHECK(active->inspection.sourceSha256 == staged.value().outputSha256);
  CHECK(std::equal(rawMetadata.begin(), rawMetadata.end(), fixture.project.metadataRevisions.begin(), production::sameMetadataRevision));
  CHECK(production::currentTakeInspection(fixture.project, "take-a")->inspection.sourceSha256 == fixture.raw.sha256);
  CHECK(fixture.project.reviews.empty());
  CHECK(!fixture.project.unitAssignments.front().markerReviewed);
  CHECK(!fixture.project.unitAssignments.front().pitchReviewed);
}

TEST_CASE("derived inspection retains failing format and valid historical receipts after another edit") {
  StagingFixture fixture;
  const auto first = fixture.repository.stageOperation(fixture.project, "take-a", "",
      {.kind=production::OperationKind::Resample, .targetSampleRate=44100U}, "wrong-rate");
  CHECK(first);
  CHECK(fixture.repository.commitStaged(fixture.project, first.value(), "first-revision", "producer",
      "2026-09-09T10:01:00Z", "take-a", ""));
  const auto initial = std::find_if(fixture.project.metadataRevisions.begin(), fixture.project.metadataRevisions.end(),
      [](const auto& value) { return value.kind == "derived-take-inspection.v1"; });
  CHECK(initial != fixture.project.metadataRevisions.end());
  const auto historical = *initial;
  const auto bad = production::validateTakeInspectionRevision(fixture.project, historical);
  CHECK(bad);
  CHECK(bad.value().inspection.outcome(seam::voicebank::TakeCheck::Format) == seam::voicebank::TakeCheckOutcome::Fail);
  const auto second = fixture.repository.stageOperation(fixture.project, "take-a", "first-revision",
      {.kind=production::OperationKind::Resample, .targetSampleRate=48000U}, "correct-rate");
  CHECK(second);
  CHECK(fixture.repository.commitStaged(fixture.project, second.value(), "second-revision", "producer",
      "2026-09-09T10:02:00Z", "take-a", "first-revision"));
  CHECK(production::validateTakeInspectionRevision(fixture.project, historical));
  CHECK(production::validateProductionProject(fixture.project));
  const auto active = production::currentActiveTakeInspection(fixture.project, "take-a");
  CHECK(active);
  CHECK(active->inspection.sourceSha256 == second.value().outputSha256);
  auto forged = production::makeDerivedTakeInspectionRevision(bad.value(), fixture.raw.sha256,
      "second-revision", "producer", "2026-09-09T10:02:00Z");
  CHECK(!production::validateTakeInspectionRevision(fixture.project, forged));
  auto wrongUnit = bad.value();
  wrongUnit.binding.pitchLayer = 60;
  forged = production::makeDerivedTakeInspectionRevision(wrongUnit, fixture.raw.sha256,
      "first-revision", "producer", "2026-09-09T10:01:00Z");
  CHECK(!production::validateTakeInspectionRevision(fixture.project, forged));
  auto legacy = fixture.project;
  std::erase_if(legacy.metadataRevisions, [](const auto& value) {
    return value.kind == "derived-take-inspection.v1" && value.values.at("derivedRevisionId") == "second-revision";
  });
  CHECK(production::validateProductionProject(legacy));
  CHECK(production::currentTakeInspection(legacy, "take-a"));
  CHECK(!production::currentActiveTakeInspection(legacy, "take-a"));
  CHECK(fixture.project.reviews.empty());
}

TEST_CASE("a short derived take preserves failed root-pitch diagnostics without approval") {
  StagingFixture fixture;
  const auto staged = fixture.repository.stageOperation(fixture.project, "take-a", "",
      {.kind=production::OperationKind::Trim, .startFrame=0U, .endFrame=512U}, "short");
  CHECK(staged);
  CHECK(fixture.repository.commitStaged(fixture.project, staged.value(), "short-revision", "producer",
      "2026-09-09T10:01:00Z", "take-a", ""));
  const auto active = production::currentActiveTakeInspection(fixture.project, "take-a");
  CHECK(active);
  CHECK(active->inspection.sourceSha256 == staged.value().outputSha256);
  CHECK(active->inspection.outcome(seam::voicebank::TakeCheck::RootPitch) == seam::voicebank::TakeCheckOutcome::Fail);
  CHECK(!active->inspection.accepted());
  CHECK(production::currentTakeInspection(fixture.project, "take-a")->inspection.accepted());
  CHECK(fixture.project.reviews.empty());
  CHECK(fixture.project.unitAssignments.front().state == production::UnitQueueState::MarkerReview);
}

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
