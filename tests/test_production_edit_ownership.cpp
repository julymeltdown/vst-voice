#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"

namespace {
namespace production = seam::voicebank_production;
struct EditFixture final {
  std::filesystem::path root{seam::test::support::temporaryDirectory("production-edit-owner")};
  production::ProductionProjectRepository repository{root / "workspace"};
  production::VoicebankProductionProject project;
  production::AssetRecord firstAsset;
  production::AssetRecord secondAsset;

  explicit EditFixture(bool sharedBytes = true) {
    const auto license = root / "synthetic-license.txt";
    CHECK(seam::core::durableAtomicWriteTextNew(license, "SYNTHETIC TEST ONLY; no production qualification"));
    const auto licenseHash = seam::core::sha256File(license); CHECK(licenseHash);
    project = {.projectId="edit-ownership", .inventoryId="fixture", .inventorySha256=std::string(64U, 'a'),
      .selectedSourceStrategyId="synthetic", .licenseLocator=license.string(), .licenseSha256=licenseHash.value()};
    project.sourceStrategies = {{.id="synthetic", .kind=production::SourceStrategyKind::ProceduralSynthesis,
      .rights=production::Feasibility::Pass, .coverage=production::Feasibility::Pass, .listening=production::Feasibility::Pass,
      .permissions={true,true,true,true}, .licenseLocator=license.string(), .licenseSha256=licenseHash.value(),
      .evidenceState="SYNTHETIC_TEST_ONLY"}};
    project.operators = {{.operatorId="producer", .role="PRODUCER"}, {.operatorId="reviewer", .role="REVIEWER"}};
    project.unitAssignments = {
      {.coverageKey="sustain:a", .pitchLayer=69, .promptId="a", .plannedTakeId="take-a"},
      {.coverageKey="sustain:i", .pitchLayer=69, .promptId="b", .plannedTakeId="take-b"}};
    CHECK(repository.initialize(project, event("create", project.projectId)));
    const auto first = root / "a.wav";
    const auto second = root / "b.wav";
    const seam::voicebank::WavOutputFormat options{.sampleRate=48000U, .channels=1U,
      .sampleFormat=seam::voicebank::WavSampleFormat::Pcm24};
    CHECK(seam::voicebank::writeWav(first, options, seam::test::support::sineWave(48000U, 440.0, 0.12, 0.25F)));
    CHECK(seam::voicebank::writeWav(second, options, seam::test::support::sineWave(48000U, sharedBytes ? 440.0 : 660.0, 0.12, 0.25F)));
    const auto a = repository.importRaw(project, first,
      {.takeId="take-a", .promptId="a", .coverageKey="sustain:a", .pitchLayer=69}, event("import", "take-a"));
    CHECK(a); firstAsset=a.value();
    const auto b = repository.importRaw(project, second,
      {.takeId="take-b", .promptId="b", .coverageKey="sustain:i", .pitchLayer=69}, event("import", "take-b"));
    CHECK(b); secondAsset=b.value();
  }
  static production::ProductionJournalEvent event(std::string action, std::string subject) {
    return {.action=std::move(action), .subjectId=std::move(subject), .operatorId="producer", .occurredAtUtc="2026-09-09T10:00:00Z"};
  }
};
} // namespace

TEST_CASE("production edits bind the selected take instead of the first shared audio hash") {
  EditFixture fixture;
  CHECK(fixture.firstAsset.sha256 == fixture.secondAsset.sha256);
  const auto staged=fixture.repository.stageOperation(fixture.project, "take-b", "",
      {.kind=production::OperationKind::NormalizeGain, .targetPeak=0.15F}, "b-normalized"); CHECK(staged);
  const auto unchanged=production::encodeProductionProject(fixture.project);
  CHECK(!fixture.repository.commitStaged(fixture.project, staged.value(), "misdirected", "producer",
      "2026-09-09T10:01:00Z", "take-a", ""));
  CHECK(production::encodeProductionProject(fixture.project)==unchanged);
  const auto revised=fixture.repository.commitStaged(fixture.project, staged.value(), "b-revision", "producer",
      "2026-09-09T10:01:00Z", "take-b", ""); CHECK(revised);
  CHECK(fixture.project.takes[0].derivedRevisionIds.empty());
  CHECK(fixture.project.takes[1].derivedRevisionIds == std::vector<std::string>{"b-revision"});
  const auto recovered=fixture.repository.recover(); CHECK(recovered);
  CHECK(production::encodeProductionProject(recovered.value()) == production::encodeProductionProject(fixture.project));
}

TEST_CASE("production edits require exact parent revision even when a no-op preserves audio bytes") {
  EditFixture fixture;
  const auto staged=fixture.repository.stageOperation(fixture.project, "take-b", "",
      {.kind=production::OperationKind::Downmix}, "b-noop"); CHECK(staged);
  CHECK(staged.value().inputSha256 == staged.value().outputSha256);
  CHECK(fixture.repository.commitStaged(fixture.project, staged.value(), "b-first", "producer",
      "2026-09-09T10:01:00Z", "take-b", ""));
  const auto before=production::encodeProductionProject(fixture.project);
  CHECK(!fixture.repository.commitStaged(fixture.project, staged.value(), "b-stale", "producer",
      "2026-09-09T10:02:00Z", "take-b", ""));
  CHECK(production::encodeProductionProject(fixture.project) == before);
  const auto nextStage=fixture.repository.stageOperation(fixture.project, "take-b", "b-first",
      {.kind=production::OperationKind::Downmix}, "b-noop-current"); CHECK(nextStage);
  CHECK(fixture.repository.commitStaged(fixture.project, nextStage.value(), "b-second", "producer",
      "2026-09-09T10:02:00Z", "take-b", "b-first"));
  CHECK(fixture.project.takes[0].derivedRevisionIds.empty());
  CHECK(fixture.project.takes[1].derivedRevisionIds.size() == 2U);
}

TEST_CASE("production edit rejects wrong input owner and missing target without durable changes") {
  EditFixture fixture{false};
  const auto staged=fixture.repository.stageOperation(fixture.project, "take-a", "",
      {.kind=production::OperationKind::NormalizeGain, .targetPeak=0.15F}, "a-normalized"); CHECK(staged);
  const auto before=production::encodeProductionProject(fixture.project);
  for (const auto target : {"take-b", "missing", ""}) {
    CHECK(!fixture.repository.commitStaged(fixture.project, staged.value(), "wrong-owner", "producer",
        "2026-09-09T10:01:00Z", target, ""));
    CHECK(production::encodeProductionProject(fixture.project) == before);
  }
  const auto recovered=fixture.repository.recover(); CHECK(recovered);
  CHECK(production::encodeProductionProject(recovered.value()) == before);
}

TEST_CASE("production edit rejects stale and locally mutated base snapshots") {
  EditFixture fixture;
  const auto staged=fixture.repository.stageOperation(fixture.project, "take-b", "",
      {.kind=production::OperationKind::NormalizeGain, .targetPeak=0.15F}, "b-normalized"); CHECK(staged);
  auto changed=fixture.project;
  changed.inventoryId="unsaved-local-change";
  CHECK(!fixture.repository.commitStaged(changed, staged.value(), "local-mutation", "producer",
      "2026-09-09T10:01:00Z", "take-b", ""));
  auto stale=fixture.project;
  CHECK(fixture.repository.save(fixture.project, EditFixture::event("save", fixture.project.projectId)));
  CHECK(!fixture.repository.commitStaged(stale, staged.value(), "stale", "producer",
      "2026-09-09T10:01:00Z", "take-b", ""));
  CHECK(stale.takes[1].derivedRevisionIds.empty());
  CHECK(fixture.project.takes[1].derivedRevisionIds.empty());
}

TEST_CASE("production audio edit invalidates only dependent current review status and keeps history") {
  EditFixture fixture;
  // Isolated mutation-boundary fixture, not an end-to-end approval workflow.
  // The candidate publication tests exercise the supported independent review API.
  for (std::size_t i=0U; i<2U; ++i) {
    fixture.project.takes[i].state=production::UnitQueueState::Approved;
    fixture.project.unitAssignments[i].state=production::UnitQueueState::Approved;
    fixture.project.unitAssignments[i].markerReviewed=true;
    fixture.project.unitAssignments[i].pitchReviewed=true;
    fixture.project.reviews.push_back({.reviewId="review-"+std::to_string(i), .takeId=fixture.project.takes[i].takeId,
      .reviewerId="reviewer", .result="PASS", .reviewedAtUtc="2026-09-09T10:00:00Z"});
  }
  CHECK(fixture.repository.save(fixture.project, EditFixture::event("review", "isolated-test-state")));
  const auto staged=fixture.repository.stageOperation(fixture.project, "take-b", "",
      {.kind=production::OperationKind::NormalizeGain, .targetPeak=0.15F}, "b-normalized"); CHECK(staged);
  CHECK(fixture.repository.commitStaged(fixture.project, staged.value(), "b-revision", "producer",
      "2026-09-09T10:01:00Z", "take-b", ""));
  CHECK(fixture.project.unitAssignments[0].state == production::UnitQueueState::Approved);
  CHECK(fixture.project.unitAssignments[0].markerReviewed);
  CHECK(fixture.project.unitAssignments[0].pitchReviewed);
  CHECK(fixture.project.unitAssignments[1].state == production::UnitQueueState::MarkerReview);
  CHECK(!fixture.project.unitAssignments[1].markerReviewed);
  CHECK(!fixture.project.unitAssignments[1].pitchReviewed);
  CHECK(fixture.project.reviews.size() == 2U);
  CHECK(fixture.project.reviews[1].result == "PASS");
}

TEST_CASE("production staging checks take source authorization before DSP or staging output") {
  EditFixture fixture;
  fixture.project.sourceStrategies.front().permissions.transformation=false;
  CHECK(fixture.repository.save(fixture.project, EditFixture::event("save", fixture.project.projectId)));
  const auto before=production::encodeProductionProject(fixture.project);
  const auto staged=fixture.repository.stageOperation(fixture.project,"take-b","",
      {.kind=production::OperationKind::NormalizeGain,.targetPeak=0.15F},"forbidden-transform");
  CHECK(!staged);
  CHECK(!std::filesystem::exists(fixture.root / "workspace/staging/forbidden-transform.wav"));
  CHECK(production::encodeProductionProject(fixture.project)==before);
}

TEST_CASE("production transform reports a recoverable commit after the pointer write fails") {
  EditFixture fixture;
  const auto staged=fixture.repository.stageOperation(fixture.project,"take-b","",
      {.kind=production::OperationKind::NormalizeGain,.targetPeak=0.15F},"uncertain-transform"); CHECK(staged);
  const auto generation=fixture.project.lastDurableGeneration;
  const auto pointer=fixture.root / "workspace/project.json";
  std::filesystem::rename(pointer,fixture.root / "saved-pointer.json");
  CHECK(std::filesystem::create_directory(pointer));
  const auto committed=fixture.repository.commitStaged(fixture.project,staged.value(),"uncertain-revision","producer",
      "2026-09-09T10:01:00Z","take-b","");
  CHECK(committed); CHECK(!committed.value().durabilityConfirmed);
  CHECK(!committed.value().diagnostic.empty());
  CHECK(committed.value().committedGeneration>generation);
  CHECK(fixture.project.lastDurableGeneration==committed.value().committedGeneration);
  const auto recovered=fixture.repository.recover(); CHECK(recovered);
  CHECK(production::encodeProductionProject(recovered.value())==production::encodeProductionProject(fixture.project));
  CHECK(fixture.project.takes[0].derivedRevisionIds.empty());
  CHECK(fixture.project.takes[1].derivedRevisionIds==std::vector<std::string>{"uncertain-revision"});
}

TEST_CASE("metadata edits bind actor and preserve their actual postcommit state") {
  EditFixture fixture;
  production::MetadataRevision revision{.revisionId="markers-b",.takeId="take-b",.rawAssetSha256=fixture.secondAsset.sha256,
      .kind="MARKERS_AND_PITCH",.values={{"loopStart","1000"}},.operatorId="producer",.performedAtUtc="2026-09-09T10:00:00Z"};
  auto event=EditFixture::event("marker",revision.revisionId);
  event.operatorId="reviewer";
  CHECK(!fixture.repository.recordMetadataRevision(fixture.project,revision,event));
  CHECK(fixture.project.metadataRevisions.empty());
  event.operatorId="producer";
  const auto pointer=fixture.root / "workspace/project.json";
  std::filesystem::rename(pointer,fixture.root / "saved-pointer.json");
  CHECK(std::filesystem::create_directory(pointer));
  const auto committed=fixture.repository.recordMetadataRevision(fixture.project,revision,event);
  CHECK(committed); CHECK(!committed.value().durabilityConfirmed);
  CHECK(fixture.project.metadataRevisions.size()==1U);
  CHECK(fixture.project.metadataRevisions.front().revisionId=="markers-b");
  CHECK(production::encodeProductionProject(fixture.repository.recover().value())==production::encodeProductionProject(fixture.project));
}
