#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/candidate_publication.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "seam/voicebank_production/source_assessment.hpp"

namespace {
namespace production = seam::voicebank_production;
struct DraftFixture {
  std::filesystem::path root{seam::test::support::temporaryDirectory("production-draft")};
  production::VoicebankProductionProject project;
  DraftFixture() {
    project.projectId = "honest-draft";
    project.operators = {{"producer", "PRODUCER"}, {"other-producer", "PRODUCER"}, {"reviewer", "REVIEWER"}};
    CHECK(production::ProductionProjectRepository{root / "workspace"}.initialize(project,
        {.action = "create", .subjectId = project.projectId, .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:00:00Z"}));
  }
  void source(std::string id, bool redistributable = false) {
    const auto file = root / (id + ".txt");
    CHECK(seam::core::durableAtomicWriteText(file, "GENERATED TEST SOURCE AUTHORIZATION: " + id));
    const auto hash = seam::core::sha256File(file); CHECK(hash);
    project.sourceStrategies.push_back({.id = id, .kind = production::SourceStrategyKind::ProceduralSynthesis,
        .rights = production::Feasibility::Pass, .coverage = production::Feasibility::NotAssessed,
        .listening = production::Feasibility::NotAssessed,
        .permissions = {.sourceUse = true, .transformation = true,
                       .singingBankRedistribution = redistributable, .commercialRenders = redistributable},
        .licenseLocator = file.string(), .licenseSha256 = hash.value(), .evidenceState = "SYNTHETIC_TEST_ONLY"});
    project.selectedSourceStrategyId = id;
    project.licenseLocator = file.string(); project.licenseSha256 = hash.value();
    if (project.inventoryId.empty()) { project.inventoryId = "test-inventory"; project.inventorySha256 = std::string(64U, 'a'); }
    CHECK(production::ProductionProjectRepository{root / "workspace"}.save(project,
        {.action = "save", .subjectId = id, .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:01:00Z"}));
  }
  std::filesystem::path wav() const {
    const auto file = root / "source.wav";
    CHECK(seam::voicebank::writeWav(file, {.sampleRate = 48000U, .channels = 1U,
        .sampleFormat = seam::voicebank::WavSampleFormat::Pcm24}, seam::test::support::sineWave(48000U, 440.0, 0.12, 0.25F)));
    return file;
  }
  void assignment(std::string phone) {
    project.unitAssignments.push_back({.coverageKey = "sustain:" + phone, .pitchLayer = 69,
        .promptId = "prompt-" + phone, .plannedTakeId = "take-" + phone});
  }
  seam::core::Result<production::CommittedAssetRecord> importTake(std::string phone, std::string actor = "producer") {
    return production::ProductionProjectRepository{root / "workspace"}.importRaw(project, wav(),
        {.takeId = "take-" + phone, .promptId = "prompt-" + phone, .coverageKey = "sustain:" + phone, .pitchLayer = 69},
        {.action = "import", .subjectId = "take-" + phone, .operatorId = std::move(actor), .occurredAtUtc = "2026-09-09T10:02:00Z"});
  }
  production::SourceQualityAssessment quality(std::string id = "quality-1") const {
    const auto evidence = root / "quality-evidence.txt";
    CHECK(seam::core::durableAtomicWriteText(evidence,"SYNTHETIC reviewer evidence; not real singer acceptance."));
    const auto material = production::sourceQualityMaterialIdentity(project,project.selectedSourceStrategyId); CHECK(material);
    const auto source = std::find_if(project.sourceStrategies.begin(),project.sourceStrategies.end(),[&](const auto& row) { return row.id == project.selectedSourceStrategyId; });
    return {std::move(id),source->id,production::sourceQualityPolicyIdentity(*source),material.value(),
        seam::core::sha256File(evidence).value(),"reviewer","2026-09-09T10:03:00Z",production::Feasibility::Pass,production::Feasibility::Pass};
  }
  seam::core::Result<production::ProductionCommitReceipt> assess(const production::SourceQualityAssessment& value) {
    return production::ProductionProjectRepository{root / "workspace"}.recordSourceQualityAssessment(project,value,root / "quality-evidence.txt",
        seam::core::sha256Hex(production::encodeProductionProject(project)));
  }
};

seam::voicebank::Manifest sampleManifest() {
  seam::voicebank::Manifest manifest{.id = "draft-test-bank", .version = "0.1.0", .displayName = "Draft Test Bank",
      .characterId = {}, .characterVersion = {}, .language = seam::domain::Language::Japanese, .expectedSampleRate = 48000U,
      .styles = {"original"}};
  manifest.units.push_back({.id = "a-69", .alias = "a", .phones = {"a"}, .kind = seam::voicebank::UnitKind::Sustain,
      .audioPath = "audio/assigned-source.wav", .rootMidi = 69, .style = "original", .take = 1, .priority = 0, .gainDb = 0.0F,
      .renderer = seam::voicebank::RendererHint::Raw,
      .markers = {.audioOffset = 0, .consonantEnd = 0, .vowelOnset = 0, .stableStart = 480,
          .loopStart = 480, .loopEnd = 4800, .releaseStart = 5280, .audioEnd = 5760}});
  return manifest;
}
}  // namespace

TEST_CASE("source quality assessment upgrades only the current schema and retains immutable rights and evidence") {
  DraftFixture fixture; fixture.source("original",true); fixture.assignment("a"); CHECK(fixture.importTake("a"));
  const auto binding = fixture.project.sourceBindings; const auto record = fixture.quality();
  CHECK(!production::requireTakeSourceQualification(fixture.project,"take-a"));
  const auto saved = fixture.assess(record); CHECK(saved); CHECK(saved.value().durabilityConfirmed);
  CHECK(fixture.project.schemaVersion == 3); CHECK(fixture.project.sourceBindings == binding);
  CHECK(fixture.project.sourceQualityAssessments == std::vector{record});
  CHECK(fixture.project.reviews.empty()); CHECK(fixture.project.unitAssignments.front().state == production::UnitQueueState::MarkerReview);
  CHECK(production::requireTakeSourceQualification(fixture.project,"take-a"));
  CHECK(production::selectedStrategyReady(fixture.project));
  CHECK(production::decodeProductionProject(production::encodeProductionProject(fixture.project)));
  production::ProductionProjectRepository repository{fixture.root / "workspace"}; CHECK(repository.verify(fixture.project));
  CHECK(std::filesystem::remove(fixture.root / "quality-evidence.txt")); CHECK(repository.verify(fixture.project));
  CHECK(production::encodeProductionProject(repository.recover().value()) == production::encodeProductionProject(fixture.project));
  auto rewrite = fixture.project; rewrite.sourceQualityAssessments.clear();
  CHECK(!repository.save(rewrite,{"save","erase-history","producer","2026-09-09T10:04:00Z"}));
}

TEST_CASE("source registration connects an empty draft to execution without musical approval") {
  DraftFixture fixture;
  const auto evidence = fixture.root / "declared-source.txt";
  CHECK(seam::core::durableAtomicWriteTextNew(evidence,"Synthetic test declaration, no legal or musical qualification."));
  production::SourceStrategyAssessment source{.id="new-source",.kind=production::SourceStrategyKind::ProceduralSynthesis,
      .rights=production::Feasibility::Pass,.permissions={true,true,false,false},
      .licenseLocator=evidence.string(),.licenseSha256=seam::core::sha256File(evidence).value()};
  production::ProductionProjectRepository repository{fixture.root / "workspace"};
  const auto before = fixture.project;
  const auto hash = seam::core::sha256Hex(production::encodeProductionProject(before));
  const auto result = repository.registerSource(fixture.project,source,hash,"producer","2026-09-09T10:01:00Z");
  CHECK(result); CHECK(result.value().durabilityConfirmed);
  CHECK(fixture.project.lastDurableGeneration == before.lastDurableGeneration + 1U);
  CHECK(fixture.project.selectedSourceStrategyId == source.id);
  CHECK(production::requireSelectedSourceExecution(fixture.project));
  CHECK(!production::selectedStrategyReady(fixture.project)); CHECK(fixture.project.sourceQualityAssessments.empty());
  CHECK(fixture.project.reviews.empty()); CHECK(fixture.project.takes.empty());
  CHECK(!fixture.project.sourceStrategies.front().permissions.singingBankRedistribution);
  CHECK(seam::core::sha256File(fixture.root/"workspace/source-evidence"/(source.licenseSha256+".txt")).value() == source.licenseSha256);
  CHECK(production::encodeProductionProject(repository.recover().value()) == production::encodeProductionProject(fixture.project));
  auto stale = before; source.id = "stale-source";
  CHECK(!repository.registerSource(stale,source,hash,"producer","2026-09-09T10:02:00Z"));
  CHECK(stale.lastDurableGeneration == before.lastDurableGeneration);
}

TEST_CASE("source registration rejects changed evidence self promotion and ambiguous input without mutation") {
  DraftFixture fixture;
  const auto evidence = fixture.root / "declared-source.txt";
  CHECK(seam::core::durableAtomicWriteTextNew(evidence,"Synthetic declaration"));
  production::SourceStrategyAssessment source{.id="source",.kind=production::SourceStrategyKind::TtsDerived,
      .rights=production::Feasibility::NotAssessed,.permissions={false,false,false,false},
      .licenseLocator=evidence.string(),.licenseSha256=seam::core::sha256File(evidence).value()};
  production::ProductionProjectRepository repository{fixture.root / "workspace"};
  const auto before = production::encodeProductionProject(fixture.project);
  const auto hash = seam::core::sha256Hex(before);
  CHECK(!repository.registerSource(fixture.project,source,hash,"reviewer","2026-09-09T10:01:00Z"));
  CHECK(!repository.registerSource(fixture.project,source,std::string(64U,'b'),"producer","2026-09-09T10:01:00Z"));
  source.coverage = production::Feasibility::Pass;
  CHECK(!repository.registerSource(fixture.project,source,hash,"producer","2026-09-09T10:01:00Z"));
  source.coverage = production::Feasibility::NotAssessed;
  std::stop_source cancelled; cancelled.request_stop();
  CHECK(!repository.registerSource(fixture.project,source,hash,"producer","2026-09-09T10:01:00Z",cancelled.get_token()));
  CHECK(seam::core::durableAtomicWriteText(evidence,"Changed after capture"));
  CHECK(!repository.registerSource(fixture.project,source,hash,"producer","2026-09-09T10:01:00Z"));
  CHECK(production::encodeProductionProject(fixture.project) == before);
  CHECK(production::encodeProductionProject(repository.recover().value()) == before);
  source.licenseSha256 = seam::core::sha256File(evidence).value();
  CHECK(repository.registerSource(fixture.project,source,hash,"producer","2026-09-09T10:01:00Z"));
  CHECK(!production::requireSelectedSourceExecution(fixture.project));
  CHECK(fixture.project.sourceStrategies.front().rights == production::Feasibility::NotAssessed);
}

TEST_CASE("registering another source preserves existing take provenance and rejects policy replacement") {
  DraftFixture fixture; fixture.source("original"); fixture.assignment("a"); CHECK(fixture.importTake("a"));
  production::ProductionProjectRepository repository{fixture.root / "workspace"};
  const auto bindings = fixture.project.sourceBindings;
  const auto original = fixture.project.sourceStrategies.front();
  auto second = original; second.id = "second"; second.kind = production::SourceStrategyKind::HumanRecording;
  const auto hash = seam::core::sha256Hex(production::encodeProductionProject(fixture.project));
  CHECK(repository.registerSource(fixture.project,second,hash,"other-producer","2026-09-09T10:03:00Z"));
  CHECK(fixture.project.sourceBindings == bindings); CHECK(fixture.project.sourceStrategies.front() == original);
  CHECK(fixture.project.selectedSourceStrategyId == "second");
  const auto before = production::encodeProductionProject(fixture.project);
  second.permissions.singingBankRedistribution = true;
  CHECK(!repository.registerSource(fixture.project,second,seam::core::sha256Hex(before),"producer","2026-09-09T10:04:00Z"));
  CHECK(production::encodeProductionProject(repository.recover().value()) == before);
}

TEST_CASE("source quality decisions do not grant absent redistribution rights and reassessments remain explicit") {
  DraftFixture fixture; fixture.source("restricted"); fixture.assignment("a"); CHECK(fixture.importTake("a"));
  const auto record = fixture.quality(); CHECK(fixture.assess(record));
  CHECK(!production::requireTakeSourceQualification(fixture.project,"take-a"));
  CHECK(production::requireTakeSourceExecution(fixture.project,"take-a"));
  auto rejected = fixture.quality("quality-2"); rejected.listening = production::Feasibility::Blocked;
  CHECK(fixture.assess(rejected)); CHECK(fixture.project.sourceQualityAssessments.size() == 2U);
  CHECK(fixture.project.sourceStrategies.front().listening == production::Feasibility::Blocked);
  CHECK(!fixture.assess(rejected));
}

TEST_CASE("source quality assessment rejects self-review stale snapshots and changed evidence without mutation") {
  DraftFixture fixture; fixture.source("original",true); fixture.assignment("a"); CHECK(fixture.importTake("a"));
  const auto before = production::encodeProductionProject(fixture.project); auto record = fixture.quality();
  record.reviewerId = "producer"; CHECK(!fixture.assess(record));
  record.reviewerId = "missing"; CHECK(!fixture.assess(record));
  record.reviewerId = "reviewer"; record.materialSha256 = std::string(64U,'b'); CHECK(!fixture.assess(record));
  record = fixture.quality();
  production::ProductionProjectRepository repository{fixture.root / "workspace"};
  CHECK(!repository.recordSourceQualityAssessment(fixture.project,record,fixture.root/"quality-evidence.txt",std::string(64U,'a')));
  std::stop_source cancelled; cancelled.request_stop();
  CHECK(!repository.recordSourceQualityAssessment(fixture.project,record,fixture.root/"does-not-exist",seam::core::sha256Hex(before),cancelled.get_token()));
  CHECK(seam::core::durableAtomicWriteText(fixture.root/"quality-evidence.txt","changed")); CHECK(!fixture.assess(record));
  CHECK(production::encodeProductionProject(fixture.project) == before);
  CHECK(production::encodeProductionProject(repository.recover().value()) == before);
}

TEST_CASE("source quality material edits invalidate the prior decision without changing captured source attribution") {
  DraftFixture fixture; fixture.source("original",true); fixture.assignment("a"); CHECK(fixture.importTake("a"));
  const auto record = fixture.quality(); CHECK(fixture.assess(record));
  auto changed = fixture.project;
  changed.inventorySha256 = std::string(64U,'b');
  CHECK(!production::requireTakeSourceQualification(changed,"take-a"));
  CHECK(!production::selectedStrategyReady(changed));
  CHECK(production::requireTakeSourceExecution(changed,"take-a"));
  CHECK(changed.sourceBindings == fixture.project.sourceBindings);
  changed = fixture.project; changed.schemaVersion = 2;
  CHECK(!production::validateProductionProject(changed));
  const auto retained = fixture.root/"workspace/source-evidence"/(record.evidenceSha256+".quality.txt");
  CHECK(seam::core::durableAtomicWriteText(retained,"tampered"));
  CHECK(!production::ProductionProjectRepository{fixture.root/"workspace"}.verify(fixture.project));
}

TEST_CASE("empty source-free production drafts persist without feasibility claims") {
  DraftFixture fixture;
  CHECK(fixture.project.schemaVersion == 2);
  CHECK(fixture.project.lifecycle == production::ProductionLifecycle::Draft);
  CHECK(fixture.project.sourceStrategies.empty());
  CHECK(fixture.project.sourceBindings.empty());
  CHECK(fixture.project.inventorySha256.empty());
  CHECK(fixture.project.licenseSha256.empty());
  CHECK(!production::requireSelectedSourceExecution(fixture.project));
  const auto encoded = production::encodeProductionProject(fixture.project);
  const auto decoded = production::decodeProductionProject(encoded); CHECK(decoded);
  CHECK(production::encodeProductionProject(decoded.value()) == encoded);
  production::ProductionProjectRepository repository{fixture.root / "workspace"};
  CHECK(repository.verify(fixture.project));
  CHECK(repository.save(fixture.project, {.action = "save", .subjectId = fixture.project.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:01:00Z"}));
  const auto recovered = repository.recover(); CHECK(recovered);
  CHECK(recovered.value().lifecycle == production::ProductionLifecycle::Draft);
  CHECK(recovered.value().sourceStrategies.empty());
  auto claimed = fixture.project;
  claimed.lifecycle = production::ProductionLifecycle::Qualified;
  CHECK(!production::validateProductionProject(claimed));
  fixture.project.sourceStrategies.push_back({.id = "unassessed", .kind = production::SourceStrategyKind::HumanRecording,
      .rights = production::Feasibility::NotAssessed, .coverage = production::Feasibility::NotAssessed,
      .listening = production::Feasibility::NotAssessed, .permissions = {}, .licenseLocator = {}, .licenseSha256 = {}, .evidenceState = "DRAFT"});
  fixture.project.selectedSourceStrategyId = "unassessed";
  CHECK(repository.save(fixture.project, {.action = "save", .subjectId = "unassessed", .operatorId = "producer",
      .occurredAtUtc = "2026-09-09T10:02:00Z"}));
  CHECK(!production::requireSelectedSourceExecution(fixture.project));
  CHECK(repository.recover().value().sourceStrategies.front().listening == production::Feasibility::NotAssessed);
}

TEST_CASE("authorized experimental imports retain source evidence without granting candidate qualification") {
  DraftFixture fixture;
  fixture.assignment("a");
  fixture.source("source-a");
  CHECK(!production::selectedStrategyReady(fixture.project));
  CHECK(production::requireSelectedSourceExecution(fixture.project));
  const auto imported = fixture.importTake("a"); CHECK(imported);
  CHECK(fixture.project.lifecycle == production::ProductionLifecycle::Experimental);
  CHECK(fixture.project.reviews.empty());
  CHECK(fixture.project.sourceBindings.size() == 1U);
  const auto source = fixture.project.sourceBindings.front();
  CHECK(source.takeId == "take-a");
  CHECK(source.importerId == "producer");
  CHECK(source.rawAssetSha256 == imported.value().sha256);
  CHECK(source.strategy.coverage == production::Feasibility::NotAssessed);
  CHECK(source.strategy.listening == production::Feasibility::NotAssessed);
  CHECK(fixture.project.takes.front().sourceBindingId == source.id);
  CHECK(seam::core::sha256File(fixture.root / "workspace" / source.licenseSnapshotPath).value() == source.strategy.licenseSha256);
  CHECK(production::requireTakeSourceExecution(fixture.project, "take-a"));
  CHECK(!production::requireTakeSourceQualification(fixture.project, "take-a"));
  const auto packet = production::prepareSampleCandidateReview(fixture.root / "workspace", fixture.project, sampleManifest()); CHECK(packet);
  const auto accepted = production::commitSampleCandidateReview(fixture.root / "workspace", fixture.project, packet.value(),
      "reviewer", "2026-09-09T10:03:00Z", production::SampleCandidateReviewDecision::Accept);
  CHECK(accepted);
  CHECK(!accepted.value().candidate);
  CHECK(fixture.project.unitAssignments.front().state == production::UnitQueueState::Approved);
  CHECK(fixture.project.sourceStrategies.front().listening == production::Feasibility::NotAssessed);
  CHECK(!production::resolveReviewedSampleCandidate(fixture.root / "workspace", fixture.project, packet.value().manifest));
  production::SampleCandidateRequest request{fixture.project.lastDurableGeneration,
      seam::core::sha256Hex(production::encodeProductionProject(fixture.project)), packet.value().manifest,
      {{"a-69", "take-a", imported.value().sha256, accepted.value().reviews.front().reviewId,
        fixture.project.metadataRevisions.back().revisionId}}};
  CHECK(!production::publishSampleCandidate(fixture.root / "workspace", fixture.project, request, fixture.root / "unqualified"));
  CHECK(!std::filesystem::exists(fixture.root / "unqualified"));
  CHECK(production::ProductionProjectRepository{fixture.root / "workspace"}.recover().value().sourceBindings.front() == source);
}

TEST_CASE("draft import requires actual source execution permission and unchanged evidence") {
  DraftFixture fixture;
  fixture.assignment("a"); fixture.source("source-a");
  production::ProductionProjectRepository repository{fixture.root / "workspace"};
  for (unsigned denied = 0U; denied < 3U; ++denied) {
    auto& strategy = fixture.project.sourceStrategies.front();
    strategy.permissions.sourceUse = denied != 0U;
    strategy.permissions.transformation = denied != 1U;
    strategy.rights = denied == 2U ? production::Feasibility::Blocked : production::Feasibility::Pass;
    CHECK(repository.save(fixture.project, {.action = "save", .subjectId = "source-a", .operatorId = "producer",
        .occurredAtUtc = "2026-09-09T10:03:00Z"}));
    const auto before = production::encodeProductionProject(fixture.project);
    CHECK(!fixture.importTake("a"));
    CHECK(production::encodeProductionProject(fixture.project) == before);
    CHECK(fixture.project.takes.empty());
    CHECK(fixture.project.sourceBindings.empty());
  }
  auto& strategy = fixture.project.sourceStrategies.front();
  strategy.rights = production::Feasibility::Pass; strategy.permissions.sourceUse = true; strategy.permissions.transformation = true;
  CHECK(repository.save(fixture.project, {.action = "save", .subjectId = "source-a", .operatorId = "producer",
      .occurredAtUtc = "2026-09-09T10:04:00Z"}));
  // save() replaces the caller's project with the committed snapshot; never
  // retain a reference into the previous sourceStrategies vector across it.
  const auto licensePath = fixture.project.sourceStrategies.front().licenseLocator;
  CHECK(seam::core::durableAtomicWriteText(licensePath, "changed evidence"));
  const auto before = production::encodeProductionProject(fixture.project);
  CHECK(!fixture.importTake("a"));
  CHECK(fixture.project.takes.empty());
  CHECK(fixture.project.sourceBindings.empty());
  CHECK(production::encodeProductionProject(fixture.project) == before);
  CHECK(std::filesystem::is_empty(fixture.root / "workspace/assets"));
}

TEST_CASE("shared audio keeps distinct immutable source and importer attribution across selection changes") {
  DraftFixture fixture;
  fixture.assignment("a"); fixture.assignment("i"); fixture.source("source-a", true);
  const auto first = fixture.importTake("a"); CHECK(first);
  const auto firstBinding = fixture.project.sourceBindings.front();
  fixture.source("source-b", false);
  const auto second = fixture.importTake("i", "other-producer"); CHECK(second);
  CHECK(first.value().sha256 == second.value().sha256);
  CHECK(fixture.project.assets.size() == 1U);
  CHECK(fixture.project.sourceBindings.size() == 2U);
  CHECK(fixture.project.sourceBindings.front() == firstBinding);
  CHECK(fixture.project.sourceBindings[0].id != fixture.project.sourceBindings[1].id);
  CHECK(fixture.project.sourceBindings[0].strategy.id == "source-a");
  CHECK(fixture.project.sourceBindings[1].strategy.id == "source-b");
  CHECK(fixture.project.sourceBindings[0].strategy.permissions.singingBankRedistribution);
  CHECK(!fixture.project.sourceBindings[1].strategy.permissions.singingBankRedistribution);
  CHECK(fixture.project.sourceBindings[1].importerId == "other-producer");
  CHECK(production::requireTakeSourceExecution(fixture.project, "take-a"));
  CHECK(production::requireTakeSourceExecution(fixture.project, "take-i"));
  CHECK(std::filesystem::remove(fixture.root / "source-a.txt"));
  production::ProductionProjectRepository repository{fixture.root / "workspace"};
  CHECK(repository.verify(fixture.project));
  CHECK(repository.recover().value().sourceBindings.front() == firstBinding);
  CHECK(production::requireTakeSourceExecution(fixture.project, "take-a"));
  const auto staged = repository.stageOperation(fixture.project, "take-a", "",
      {.kind = production::OperationKind::NormalizeGain, .targetPeak = 0.15F}, "source-owned-stage");
  CHECK(staged);
  auto forged = fixture.project;
  forged.sourceBindings.front().strategy.permissions.singingBankRedistribution = false;
  const auto generation = fixture.project.lastDurableGeneration;
  CHECK(!repository.save(forged, {.action = "save", .subjectId = "source-a", .operatorId = "producer",
      .occurredAtUtc = "2026-09-09T10:04:00Z"}));
  CHECK(repository.recover().value().lastDurableGeneration == generation);
  forged = fixture.project;
  forged.takes.front().sourceBindingId = forged.sourceBindings.back().id;
  CHECK(!production::validateProductionProject(forged));
}

TEST_CASE("legacy producer generations retain exact bytes and unknown origins when new imports migrate") {
  const auto root = seam::test::support::temporaryDirectory("legacy-production-migration");
  const auto license = root / "legacy-license.txt";
  CHECK(seam::core::durableAtomicWriteText(license, "LEGACY SYNTHETIC FIXTURE"));
  production::VoicebankProductionProject legacy;
  legacy.schemaVersion = 1; legacy.lifecycle = production::ProductionLifecycle::LegacyUnclassified;
  legacy.projectId = "legacy-project"; legacy.inventoryId = "legacy-inventory"; legacy.inventorySha256 = std::string(64U, 'a');
  legacy.selectedSourceStrategyId = "legacy-source"; legacy.licenseLocator = license.string(); legacy.licenseSha256 = seam::core::sha256File(license).value();
  legacy.sourceStrategies = {{.id = "legacy-source", .kind = production::SourceStrategyKind::HumanRecording,
      .rights = production::Feasibility::Pass, .coverage = production::Feasibility::Pass, .listening = production::Feasibility::Pass,
      .permissions = {.sourceUse = true, .transformation = true, .singingBankRedistribution = true, .commercialRenders = true},
      .licenseLocator = license.string(), .licenseSha256 = legacy.licenseSha256, .evidenceState = "LEGACY_SYNTHETIC_FIXTURE"}};
  legacy.operators = {{"producer", "PRODUCER"}};
  legacy.unitAssignments = {{.coverageKey = "sustain:a", .pitchLayer = 69, .promptId = "prompt-a", .plannedTakeId = "old-take"},
      {.coverageKey = "sustain:i", .pitchLayer = 69, .promptId = "prompt-i", .plannedTakeId = "new-take"}};
  production::ProductionProjectRepository repository{root / "workspace"};
  CHECK(repository.initialize(legacy, {.action = "create", .subjectId = legacy.projectId, .operatorId = "producer", .occurredAtUtc = "2026-09-08T10:00:00Z"}));
  const auto raw = root / "legacy.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(raw, 48000U, seam::test::support::sineWave(48000U, 440.0, 0.1)));
  // Reconstruct a schema-1 fixture through its historical state/journal shape;
  // there was no per-take source field, so no importer/source is inferred.
  const auto asset = production::ImmutableAssetStore{root / "workspace/assets"}.importFile(raw, production::AssetKind::Raw); CHECK(asset);
  legacy.assets.push_back(asset.value());
  legacy.takes.push_back({.takeId = "old-take", .promptId = "prompt-a", .coverageKey = "sustain:a", .pitchLayer = 69,
      .rawAssetSha256 = asset.value().sha256, .derivedRevisionIds = {}, .supersedesTakeId = {}, .state = production::UnitQueueState::MarkerReview});
  legacy.unitAssignments[0].takeId = "old-take"; legacy.unitAssignments[0].state = production::UnitQueueState::MarkerReview;
  CHECK(repository.save(legacy, {.action = "import", .subjectId = "old-take", .operatorId = "producer", .occurredAtUtc = "2026-09-08T10:01:00Z"}));
  const auto generationPath = root / "workspace/generations/00000000000000000002.json";
  const auto original = seam::core::readTextFileLimited(generationPath, 1024U * 1024U); CHECK(original);
  const auto digest = seam::core::sha256File(generationPath); CHECK(digest);
  const auto decoded = production::decodeProductionProject(original.value()); CHECK(decoded);
  CHECK(decoded.value().schemaVersion == 1);
  CHECK(decoded.value().lifecycle == production::ProductionLifecycle::LegacyUnclassified);
  CHECK(production::encodeProductionProject(decoded.value()) == original.value());
  CHECK(decoded.value().takes.front().sourceBindingId.empty());
  CHECK(!production::requireTakeSourceExecution(decoded.value(), "old-take"));
  CHECK(repository.importRaw(legacy, raw, {.takeId = "new-take", .promptId = "prompt-i", .coverageKey = "sustain:i", .pitchLayer = 69},
      {.action = "import", .subjectId = "new-take", .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:02:00Z"}));
  CHECK(legacy.schemaVersion == 2);
  CHECK(legacy.lifecycle == production::ProductionLifecycle::Experimental);
  CHECK(legacy.takes.front().sourceBindingId.empty());
  CHECK(legacy.sourceBindings.size() == 1U);
  CHECK(legacy.sourceBindings.front().takeId == "new-take");
  CHECK(seam::core::sha256File(generationPath).value() == digest.value());
  CHECK(seam::core::readTextFileLimited(generationPath, 1024U * 1024U).value() == original.value());
  CHECK(repository.verify(legacy));
  CHECK(!production::requireTakeSourceExecution(legacy, "old-take"));
  CHECK(production::requireTakeSourceExecution(legacy, "new-take"));
}
