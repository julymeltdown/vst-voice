#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"

#include <iomanip>
#include <sstream>

namespace {
using namespace seam;
namespace production = voicebank_production;

struct ImportFixture final {
  std::filesystem::path root{test::support::temporaryDirectory("production-import-outcomes")};
  production::ProductionProjectRepository repository{root / "producer"};
  production::VoicebankProductionProject project;
  synthesis::ProceduralSingerResource recipe;
  std::filesystem::path audio{root / "synthetic.wav"};
  std::string audioSha256, renderHash{core::sha256Hex("synthetic persistence fixture, not production generation evidence")};
  static constexpr std::int64_t frames = 4800;
  explicit ImportFixture(bool legacy = false) {
    const auto license = root / "synthetic-license.txt";
    CHECK(core::durableAtomicWriteTextNew(license, "Synthetic import persistence fixture only. No singer or release qualification."));
    const auto digest = core::sha256File(license); CHECK(digest);
    project = {.projectId = "import-outcomes", .inventoryId = "fixture", .inventorySha256 = std::string(64U, 'a'),
        .selectedSourceStrategyId = "synthetic", .licenseLocator = license.string(), .licenseSha256 = digest.value()};
    if (legacy) { project.schemaVersion = 1; project.lifecycle = production::ProductionLifecycle::LegacyUnclassified; }
    project.sourceStrategies = {{.id = "synthetic", .kind = production::SourceStrategyKind::ProceduralSynthesis,
        .rights = production::Feasibility::Pass, .coverage = legacy ? production::Feasibility::Pass : production::Feasibility::NotAssessed,
        .listening = legacy ? production::Feasibility::Pass : production::Feasibility::NotAssessed,
        .permissions = {true,true,true,true}, .licenseLocator = license.string(), .licenseSha256 = digest.value(), .evidenceState = "SYNTHETIC_TEST_ONLY"}};
    project.operators = {{"producer", "PRODUCER"}};
    project.unitAssignments = {{.coverageKey = "sustain:a", .pitchLayer = 69, .promptId = "a", .plannedTakeId = "take-a"},
        {.coverageKey = "sustain:i", .pitchLayer = 69, .promptId = "i", .plannedTakeId = "take-i"}};
    CHECK(repository.initialize(project, {.action = "create", .subjectId = project.projectId, .operatorId = "producer", .occurredAtUtc = "2026-09-09T11:00:00Z"}));
    CHECK(voicebank::writeWav(audio, {.sampleRate = 48000U, .channels = 1U, .sampleFormat = voicebank::WavSampleFormat::Float32},
        test::support::sineWave(48000U, 440.0, 0.1, 0.25F)));
    audioSha256 = core::sha256File(audio).value();
    voice_design::VoiceRecipe model; model.id = "synthetic-import-persistence";
    model.poses = {{"a", "neutral", 0.0, {{700.0,80.0,0.0},{1200.0,100.0,-3.0},{2600.0,140.0,-6.0}}},
        {"i", "neutral", 0.0, {{300.0,80.0,0.0},{2300.0,100.0,-3.0},{3000.0,140.0,-6.0}}}};
    const auto frozen = voice_design::freezeVoiceRecipeResource(model); CHECK(frozen); recipe = frozen.value();
  }
  production::RawTakeInput take(std::string phone = "a") const {
    return {.takeId = "take-" + phone, .promptId = phone, .coverageKey = "sustain:" + phone, .pitchLayer = 69};
  }
  production::ProductionJournalEvent event(std::string action = "import", std::string subject = "take-a") const {
    return {std::move(action), std::move(subject), "producer", "2026-09-09T11:01:00Z"};
  }
  std::filesystem::path metadata(std::string phone = "a") const {
    using J = formats::JsonValue;
    const auto path = root / (phone + "-synthetic.json");
    const auto json = formats::stringifyJson(J{J::Object{
        {"formatId", "com.project-seam.procedural-candidate"}, {"schemaVersion", std::int64_t{1}},
        {"approval", "unapproved"}, {"markerSemantics", "planned-vowel-gestures"},
        {"audioSha256", audioSha256}, {"renderContentHash", renderHash}, {"renderAbi", "SYNTHETIC_PERSISTENCE_TEST_ONLY"},
        {"recipeId", recipe.identity.id}, {"recipeVersion", recipe.identity.version}, {"recipeHash", recipe.identity.contentHash},
        {"style", "neutral"}, {"sampleRate", std::int64_t{48000}}, {"frameCount", frames}, {"scoreOriginFrame", std::int64_t{0}},
        {"proceduralRevision", std::int64_t{1}}, {"compilerRevision", std::int64_t{1}},
        {"markers", J::Array{J{J::Object{{"key", domain::PhonemeKey{domain::NoteId{1U}, 0U}.toString()},
            {"phone", phone}, {"startFrame", std::int64_t{0}}, {"endFrame", frames}}}}}}});
    CHECK(core::durableAtomicWriteTextNew(path, json));
    return path;
  }
  production::GenerationImportExpectation expectation(std::string phone = "a") const {
    const auto result = production::captureGenerationImportExpectation(project, take(phone), recipe, "neutral", renderHash, 48000U, frames);
    CHECK(result); return result.value();
  }
  void obstructPointer() const {
    CHECK(std::filesystem::remove(root / "producer/project.json"));
    CHECK(std::filesystem::create_directory(root / "producer/project.json"));
  }
  void obstructNextGeneration() const {
    std::ostringstream filename; filename << std::setw(20) << std::setfill('0') << project.lastDurableGeneration + 1U << ".json";
    CHECK(std::filesystem::create_directory(root / "producer/generations" / filename.str()));
  }
  void assertCommitted(const production::ProductionCommitReceipt& receipt, std::size_t takeCount, bool durable) const {
    CHECK(receipt.durabilityConfirmed == durable);
    CHECK(receipt.committedGeneration == project.lastDurableGeneration);
    CHECK(receipt.committedProjectSha256 == core::sha256Hex(production::encodeProductionProject(project)));
    const auto recovered = repository.recover(); CHECK(recovered);
    CHECK(production::encodeProductionProject(recovered.value()) == production::encodeProductionProject(project));
    CHECK(project.takes.size() == takeCount); CHECK(project.sourceBindings.size() == takeCount);
    CHECK(project.reviews.empty());
    for (const auto& take : project.takes) CHECK(take.state == production::UnitQueueState::MarkerReview);
    if (!durable) { CHECK(!receipt.diagnostic.empty()); CHECK(!repository.verify(project)); }
    CHECK(core::sha256File(audio).value() == audioSha256);
  }
};
} // namespace

TEST_CASE("raw import rejects wrong journal action or subject before any asset or evidence writes") {
  ImportFixture fixture;
  const auto before = production::encodeProductionProject(fixture.project);
  for (const auto& event : {fixture.event("save"), fixture.event("review"), fixture.event("import", "other-take"), fixture.event("import-procedural")}) {
    const auto result = fixture.repository.importRaw(fixture.project, fixture.audio, fixture.take(), event);
    CHECK(!result); CHECK(result.error().code == core::ErrorCode::InvalidArgument);
    CHECK(production::encodeProductionProject(fixture.project) == before);
    CHECK(std::filesystem::is_empty(fixture.root / "producer/assets"));
    CHECK(std::filesystem::is_empty(fixture.root / "producer/source-evidence"));
  }
  CHECK(production::encodeProductionProject(fixture.repository.recover().value()) == before);
}

TEST_CASE("raw import returns a normal committed receipt without changing approval state") {
  ImportFixture fixture;
  const auto beforeGeneration = fixture.project.lastDurableGeneration;
  const auto result = fixture.repository.importRaw(fixture.project, fixture.audio, fixture.take(), fixture.event());
  CHECK(result); CHECK(result.value().sha256 == fixture.audioSha256);
  CHECK(fixture.project.lastDurableGeneration == beforeGeneration + 1U);
  fixture.assertCommitted(result.value(), 1U, true);
}

TEST_CASE("recoverably committed raw import preserves the actual migrated project and warns against retry") {
  ImportFixture fixture(true);
  const auto legacyPath = fixture.root / "producer/generations/00000000000000000001.json";
  const auto legacyHash = core::sha256File(legacyPath).value();
  const auto beforeGeneration = fixture.project.lastDurableGeneration;
  fixture.obstructPointer();
  const auto result = fixture.repository.importRaw(fixture.project, fixture.audio, fixture.take(), fixture.event());
  CHECK(result); CHECK(result.value().sha256 == fixture.audioSha256);
  CHECK(fixture.project.schemaVersion == 2);
  CHECK(fixture.project.lifecycle == production::ProductionLifecycle::Experimental);
  CHECK(fixture.project.lastDurableGeneration == beforeGeneration + 1U);
  fixture.assertCommitted(result.value(), 1U, false);
  CHECK(core::sha256File(legacyPath).value() == legacyHash);
  const auto committed = production::encodeProductionProject(fixture.project);
  CHECK(!fixture.repository.importRaw(fixture.project, fixture.audio, fixture.take(), fixture.event()));
  CHECK(production::encodeProductionProject(fixture.project) == committed);
}

TEST_CASE("uncommitted generation-write failure retains caller state and does not return a commit receipt") {
  ImportFixture fixture;
  const auto before = production::encodeProductionProject(fixture.project);
  fixture.obstructNextGeneration();
  const auto result = fixture.repository.importRaw(fixture.project, fixture.audio, fixture.take(), fixture.event());
  CHECK(!result);
  CHECK(production::encodeProductionProject(fixture.project) == before);
  CHECK(production::encodeProductionProject(fixture.repository.recover().value()) == before);
  CHECK(result.error().message.find("did not confirm a commit") != std::string::npos);
}

TEST_CASE("failed stale import cannot misidentify a different recovered generation as its own commit") {
  ImportFixture fixture;
  auto current = fixture.project;
  current.lifecycle = production::ProductionLifecycle::Experimental;
  CHECK(fixture.repository.save(current, fixture.event("save", "unrelated-durable-change")));
  const auto staleCaller = production::encodeProductionProject(fixture.project);
  const auto result = fixture.repository.importRaw(fixture.project, fixture.audio, fixture.take(), fixture.event());
  CHECK(!result); CHECK(result.error().message.find("different work") != std::string::npos);
  CHECK(production::encodeProductionProject(fixture.project) == staleCaller);
  CHECK(production::encodeProductionProject(fixture.repository.recover().value()) == production::encodeProductionProject(current));
}

TEST_CASE("recoverably committed procedural import retains exact lineage and original generation recognition") {
  ImportFixture fixture;
  const auto metadata = fixture.metadata();
  const auto expectation = fixture.expectation();
  fixture.obstructPointer();
  const auto result = fixture.repository.importProceduralCandidate(fixture.project, metadata, fixture.audio, fixture.recipe,
      fixture.take(), fixture.event("import-procedural"), {}, &expectation);
  CHECK(result); fixture.assertCommitted(result.value(), 1U, false);
  CHECK(fixture.project.metadataRevisions.size() == 1U);
  CHECK(fixture.project.metadataRevisions.front().kind == "procedural-lineage");
  CHECK(fixture.project.metadataRevisions.front().values.at("approval") == "unapproved");
  const auto recognized = fixture.repository.findCollectedGeneration(expectation); CHECK(recognized); CHECK(recognized.value());
  CHECK(recognized.value()->audioSha256 == result.value().sha256);
  CHECK(recognized.value()->generation == result.value().committedGeneration);
}

TEST_CASE("recoverably committed generated batch returns all assets and one exact durable generation") {
  ImportFixture fixture;
  const auto beforeGeneration = fixture.project.lastDurableGeneration;
  const std::vector<production::GeneratedCandidateInput> inputs{
      {fixture.metadata("a"), fixture.audio, fixture.recipe, fixture.expectation("a")},
      {fixture.metadata("i"), fixture.audio, fixture.recipe, fixture.expectation("i")}};
  fixture.obstructPointer();
  const auto result = fixture.repository.importGeneratedBatch(fixture.project, inputs,
      fixture.event("import-generated-batch", "synthetic-batch"));
  CHECK(result); CHECK(result.value().assets.size() == 2U);
  fixture.assertCommitted(result.value(), 2U, false);
  CHECK(result.value().committedGeneration == beforeGeneration + 1U);
  CHECK(fixture.project.assets.size() == 1U); // Shared bytes do not merge source/take ownership.
  CHECK(fixture.project.sourceBindings[0].id != fixture.project.sourceBindings[1].id);
  for (const auto& input : inputs) {
    const auto recognized = fixture.repository.findCollectedGeneration(input.expectation); CHECK(recognized); CHECK(recognized.value());
  }
}

TEST_CASE("cancelled procedural and batch imports return no receipt and leave caller project unchanged") {
  ImportFixture fixture;
  const auto before = production::encodeProductionProject(fixture.project);
  const auto metadata = fixture.metadata();
  const auto expectation = fixture.expectation();
  std::stop_source stop; stop.request_stop();
  CHECK(!fixture.repository.importProceduralCandidate(fixture.project, metadata, fixture.audio, fixture.recipe,
      fixture.take(), fixture.event("import-procedural"), stop.get_token(), &expectation));
  const std::vector<production::GeneratedCandidateInput> inputs{{metadata, fixture.audio, fixture.recipe, expectation}};
  CHECK(!fixture.repository.importGeneratedBatch(fixture.project, inputs,
      fixture.event("import-generated-batch", "cancelled-batch"), 32ULL * 1024ULL * 1024ULL, stop.get_token()));
  CHECK(production::encodeProductionProject(fixture.project) == before);
  CHECK(production::encodeProductionProject(fixture.repository.recover().value()) == before);
}
