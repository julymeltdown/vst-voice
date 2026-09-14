#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"

namespace {
namespace production = seam::voicebank_production;
using J = seam::formats::JsonValue;

// A source-aware but style-free producer, which is the state an existing producer workspace is in
// before it is migrated. Its take arrives through the ordinary import path, so the legacy bytes are
// the real ones rather than a hand-written document.
struct MigrationFixture final {
  std::filesystem::path root{seam::test::support::temporaryDirectory("production-style-migration")};
  production::ProductionProjectRepository repository{root / "workspace"};
  production::VoicebankProductionProject project;
  std::string projectSha256;
  const std::string language{"ja"};
  const std::string style{"original"};

  static production::ProductionJournalEvent event(std::string action, std::string subject) {
    return {.action = std::move(action), .subjectId = std::move(subject), .operatorId = "producer",
            .occurredAtUtc = "2026-09-14T10:00:00Z"};
  }

  explicit MigrationFixture() {
    const auto license = root / "synthetic-license.txt";
    CHECK(seam::core::durableAtomicWriteTextNew(license, "SYNTHETIC TEST ONLY; not production qualification"));
    const auto licenseHash = seam::core::sha256File(license);
    CHECK(licenseHash);
    project.schemaVersion = production::kProductionAssessmentSchemaVersion;
    project.projectId = "legacy-producer";
    project.inventoryId = "fixture-inventory";
    project.inventorySha256 = std::string(64U, 'a');
    project.selectedSourceStrategyId = "synthetic";
    project.licenseLocator = license.string();
    project.licenseSha256 = licenseHash.value();
    project.lifecycle = production::ProductionLifecycle::Draft;
    project.operators = {{.operatorId = "producer", .role = "PRODUCER"},
                         {.operatorId = "reviewer", .role = "REVIEWER"}};
    project.sourceStrategies = {{.id = "synthetic",
        .kind = production::SourceStrategyKind::ProceduralSynthesis,
        .rights = production::Feasibility::Pass,
        .coverage = production::Feasibility::NotAssessed,
        .listening = production::Feasibility::NotAssessed,
        .permissions = {true, true, true, true},
        .licenseLocator = license.string(),
        .licenseSha256 = licenseHash.value(),
        .evidenceState = "SYNTHETIC_TEST_ONLY"}};
    project.unitAssignments = {
        {.coverageKey = "sustain:a", .pitchLayer = 69, .promptId = "a", .plannedTakeId = "take-a"},
        {.coverageKey = "sustain:i", .pitchLayer = 69, .promptId = "i", .plannedTakeId = "take-i"}};
    CHECK(repository.initialize(project, event("create", project.projectId)));
    const auto source = root / "a.wav";
    CHECK(seam::voicebank::writeWav(source, {.sampleRate = 48000U, .channels = 1U,
        .sampleFormat = seam::voicebank::WavSampleFormat::Pcm24},
        seam::test::support::sineWave(48000U, 440.0, 0.12, 0.25F)));
    CHECK(repository.importRaw(project, source,
        {.takeId = "take-a", .promptId = "a", .coverageKey = "sustain:a", .pitchLayer = 69},
        event("import", "take-a")));
    projectSha256 = seam::core::sha256Hex(production::encodeProductionProject(project));
  }

  // The migration a plan is allowed to propose, written here independently of the operation so the
  // two have to agree before anything is written.
  production::VoicebankProductionProject expectedMigration() const {
    auto expected = project;
    expected.schemaVersion = production::kProductionStyleSchemaVersion;
    expected.language = language;
    expected.lifecycle = expected.takes.empty() ? production::ProductionLifecycle::Draft
                                                 : production::ProductionLifecycle::Experimental;
    for (auto& take : expected.takes) take.style = style;
    for (auto& assignment : expected.unitAssignments) {
      assignment.style = style;
      assignment.markerReviewed = false;
      assignment.pitchReviewed = false;
    }
    return expected;
  }

  std::filesystem::path writePlan(std::string name, const std::string& status,
                                  const production::VoicebankProductionProject* proposed) {
    const auto parsed = proposed == nullptr
        ? J{J::Object{}}
        : seam::formats::parseJson(production::encodeProductionProject(*proposed)).value();
    J::Object fields{{{"format", J{"com.project-seam.production-style-migration-plan"}},
        {"schemaVersion", J{std::int64_t{1}}},
        {"sourceProjectId", J{project.projectId}},
        {"sourceProjectSha256", J{projectSha256}},
        {"inventorySha256", J{std::string(64U, 'b')}},
        {"status", J{status}}, {"releaseEligible", J{false}},
        {"qualification", J{"REASSESSMENT_REQUIRED"}}}};
    if (proposed != nullptr) fields.emplace("proposedProject", std::move(parsed));
    else fields.emplace("reason", J{"Legacy inventory does not uniquely assign a style"});
    const auto path = root / name;
    CHECK(seam::core::durableAtomicWriteTextNew(path, seam::formats::stringifyJson(J{std::move(fields)})));
    return path;
  }
};
}  // namespace

TEST_CASE("a resolved legacy plan migrates style ownership as one new generation") {
  MigrationFixture fixture;
  CHECK(fixture.project.schemaVersion == production::kProductionAssessmentSchemaVersion);
  CHECK(fixture.project.language.empty());
  const auto expected = fixture.expectedMigration();
  const auto plan = fixture.writePlan("migration-plan.json", "RESOLVED_NOT_APPLIED", &expected);
  const auto before = production::encodeProductionProject(fixture.project);
  const auto generationBefore = fixture.project.lastDurableGeneration;
  const auto committed = fixture.repository.applyStyleMigration(fixture.project, plan,
      fixture.projectSha256, "producer", "2026-09-14T10:01:00Z");
  CHECK(committed);
  if (!committed) return;
  CHECK(committed.value().committedGeneration == generationBefore + 1U);
  CHECK(committed.value().durabilityConfirmed);
  CHECK(fixture.project.schemaVersion == production::kProductionStyleSchemaVersion);
  CHECK(fixture.project.language == fixture.language);
  CHECK(fixture.project.lifecycle == production::ProductionLifecycle::Experimental);
  for (const auto& take : fixture.project.takes) CHECK(take.style == fixture.style);
  for (const auto& assignment : fixture.project.unitAssignments) {
    CHECK(assignment.style == fixture.style);
    CHECK(!assignment.markerReviewed);
    CHECK(!assignment.pitchReviewed);
  }
  CHECK(fixture.repository.verify(fixture.project));
  // The plan is retained as the receipt, and the state it was applied to is still readable.
  bool retained = false;
  for (const auto& entry : std::filesystem::directory_iterator(fixture.root / "workspace" / "migrations")) {
    const auto bytes = seam::core::readFileBytesLimited(entry.path(), 4U * 1024U * 1024U);
    CHECK(bytes);
    if (bytes) CHECK(seam::core::sha256Hex(bytes.value()) == entry.path().stem().string());
    retained = true;
  }
  CHECK(retained);
  const auto historical = fixture.repository.recoverGeneration(generationBefore, fixture.projectSha256);
  CHECK(historical);
  if (historical) {
    CHECK(historical.value().language.empty());
    CHECK(historical.value().schemaVersion == production::kProductionAssessmentSchemaVersion);
  }
  // The same plan cannot be applied twice: its snapshot is no longer the current producer.
  CHECK(!fixture.repository.applyStyleMigration(fixture.project, plan, fixture.projectSha256,
      "producer", "2026-09-14T10:02:00Z"));
  CHECK(production::encodeProductionProject(fixture.project) != before);
}

TEST_CASE("a legacy plan without a unique style binding is refused without touching the producer") {
  MigrationFixture fixture;
  const auto plan = fixture.writePlan("unresolved.json", "UNRESOLVED", nullptr);
  const auto before = production::encodeProductionProject(fixture.project);
  const auto refused = fixture.repository.applyStyleMigration(fixture.project, plan,
      fixture.projectSha256, "producer", "2026-09-14T10:01:00Z");
  CHECK(!refused);
  if (!refused) {
    CHECK(refused.error().code == seam::core::ErrorCode::Unsupported);
    CHECK(refused.error().message.find("explicit per-assignment evidence") != std::string::npos);
  }
  CHECK(production::encodeProductionProject(fixture.project) == before);
  CHECK(std::filesystem::is_empty(fixture.root / "workspace" / "migrations"));
}

TEST_CASE("style migration refuses a stale snapshot and a plan that proposes different work") {
  MigrationFixture fixture;
  auto proposed = fixture.expectedMigration();
  const auto plan = fixture.writePlan("migration-plan.json", "RESOLVED_NOT_APPLIED", &proposed);
  // A producer that moved on since the plan was written cannot be migrated by it.
  CHECK(!fixture.repository.applyStyleMigration(fixture.project, plan, std::string(64U, 'c'),
      "producer", "2026-09-14T10:01:00Z"));
  CHECK(fixture.project.schemaVersion == production::kProductionAssessmentSchemaVersion);
  // A plan whose proposed project keeps an approval is not the migration this operation computes.
  auto tampered = fixture.expectedMigration();
  tampered.unitAssignments.front().markerReviewed = true;
  const auto tamperedPlan = fixture.writePlan("tampered-plan.json", "RESOLVED_NOT_APPLIED", &tampered);
  const auto refused = fixture.repository.applyStyleMigration(fixture.project, tamperedPlan,
      fixture.projectSha256, "producer", "2026-09-14T10:01:00Z");
  CHECK(!refused);
  if (!refused) CHECK(refused.error().message.find("differs from this operation") != std::string::npos);
  CHECK(fixture.project.schemaVersion == production::kProductionAssessmentSchemaVersion);
  // Two styles in one legacy producer is not a migration this operation may choose.
  auto ambiguous = fixture.expectedMigration();
  ambiguous.takes.front().style = "soft";
  const auto ambiguousPlan = fixture.writePlan("ambiguous-plan.json", "RESOLVED_NOT_APPLIED", &ambiguous);
  CHECK(!fixture.repository.applyStyleMigration(fixture.project, ambiguousPlan, fixture.projectSha256,
      "producer", "2026-09-14T10:01:00Z"));
  // A generic save still cannot carry a legacy producer into style ownership.
  auto upgraded = fixture.expectedMigration();
  CHECK(!fixture.repository.save(upgraded, MigrationFixture::event("save", fixture.project.projectId)));
  CHECK(fixture.project.schemaVersion == production::kProductionAssessmentSchemaVersion);
}

TEST_CASE("style migration requires a producer and a well-formed unapplied plan") {
  MigrationFixture fixture;
  auto proposed = fixture.expectedMigration();
  const auto plan = fixture.writePlan("migration-plan.json", "RESOLVED_NOT_APPLIED", &proposed);
  CHECK(!fixture.repository.applyStyleMigration(fixture.project, plan, fixture.projectSha256,
      "reviewer", "2026-09-14T10:01:00Z"));
  CHECK(!fixture.repository.applyStyleMigration(fixture.project, plan, fixture.projectSha256,
      "producer", "not-a-timestamp"));
  const auto malformed = fixture.root / "malformed.json";
  CHECK(seam::core::durableAtomicWriteTextNew(malformed, "{}"));
  CHECK(!fixture.repository.applyStyleMigration(fixture.project, malformed, fixture.projectSha256,
      "producer", "2026-09-14T10:01:00Z"));
  const auto applied = fixture.writePlan("applied.json", "RESOLVED_APPLIED", &proposed);
  CHECK(!fixture.repository.applyStyleMigration(fixture.project, applied, fixture.projectSha256,
      "producer", "2026-09-14T10:01:00Z"));
  CHECK(fixture.project.schemaVersion == production::kProductionAssessmentSchemaVersion);
}
