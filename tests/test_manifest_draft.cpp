#include "test_framework.hpp"
#include "test_support.hpp"
#include "english_manifest_draft_fixture.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/pitch_marks.hpp"
#include "seam/voicebank_production/manifest_draft.hpp"
#include "seam/voicebank_production/candidate_publication.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"

#include <algorithm>
#include <barrier>
#include <thread>

namespace {
using namespace seam;
namespace production = voicebank_production;
struct Fixture final {
  std::filesystem::path root{test::support::temporaryDirectory("manifest-draft")};
  production::ProductionProjectRepository repository{root / "producer"};
  production::VoicebankProductionProject project;
  production::SampleManifestDraftIdentity identity{"test.original", "0.1.0", "Synthetic Original", domain::Language::Japanese, "original"};
  explicit Fixture(bool qualified = false, bool styleOwned = false) {
    const auto license = root / "synthetic-license.txt";
    CHECK(core::durableAtomicWriteTextNew(license, "SYNTHETIC TEST ONLY. Not a singer or release qualification."));
    const auto hash = core::sha256File(license); CHECK(hash);
    project = {.projectId = "draft-test", .inventoryId = "fixture", .inventorySha256 = std::string(64U, 'a'),
        .selectedSourceStrategyId = "synthetic", .licenseLocator = license.string(), .licenseSha256 = hash.value()};
    project.sourceStrategies = {{.id = "synthetic", .kind = production::SourceStrategyKind::ProceduralSynthesis,
        .rights = production::Feasibility::Pass, .coverage = qualified ? production::Feasibility::Pass : production::Feasibility::NotAssessed,
        .listening = qualified ? production::Feasibility::Pass : production::Feasibility::NotAssessed,
        .permissions = {true, true, qualified, qualified}, .licenseLocator = license.string(), .licenseSha256 = hash.value(), .evidenceState = "SYNTHETIC_TEST_ONLY"}};
    project.operators = {{"producer", "PRODUCER"}, {"reviewer", "REVIEWER"}};
    if (styleOwned) { project.schemaVersion = production::kProductionStyleSchemaVersion; project.language = "ja"; }
    if (styleOwned) project.declaredPitchLayers = {69};
    project.unitAssignments = {{.coverageKey = "sustain:a", .pitchLayer = 69, .promptId = "a", .plannedTakeId = "take-a"}};
    if (styleOwned) project.unitAssignments.front().style = "original";
    CHECK(repository.initialize(project, {.action = "create", .subjectId = project.projectId, .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:00:00Z"}));
    CHECK(voicebank::writeWav(root / "raw.wav", {.sampleRate = 48000U, .channels = 1U, .sampleFormat = voicebank::WavSampleFormat::Pcm24},
        test::support::sineWave(48000U, 440.0, 0.2, 0.25F)));
    CHECK(repository.importRaw(project, root / "raw.wav", {.takeId = "take-a", .promptId = "a", .coverageKey = "sustain:a", .pitchLayer = 69, .style = styleOwned ? "original" : ""},
        {.action = "import", .subjectId = "take-a", .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:01:00Z"}));
  }
  core::Result<production::CreatedSampleManifestDraft> create(std::string name = "editable",
      production::SampleManifestDraftOptions options = {}, std::stop_token stop = {}) {
    return production::createSampleManifestDraft(root / "producer", project, identity, std::filesystem::absolute(root / name), options, stop);
  }
};
std::filesystem::path privateStage(const std::filesystem::path& parent, std::string_view prefix) {
  for (const auto& entry : std::filesystem::directory_iterator(parent)) {
    if (entry.path().filename().string().starts_with(prefix) &&
        std::filesystem::is_directory(entry.symlink_status())) return entry.path();
  }
  throw test::Failure{"Expected a private publication stage"};
}
} // namespace

TEST_CASE("English generated producer drafts preserve declared folding or exact stress and actual audio rate") {
  for (const bool distinguish : {false, true}) {
    test::support::EnglishManifestDraftFixture fixture{distinguish};
    const auto before = production::encodeProductionProject(fixture.project);
    const auto created = fixture.create("editable"); CHECK(created);
    const auto loaded = voicebank::ManifestJsonCodec{}.load(created.value().root / "manifest.json"); CHECK(loaded);
    const auto& manifest = loaded.value(); CHECK(manifest.languageContract);
    CHECK(manifest.languageContract->vocabularyId == fixture.profile.vocabularyId);
    CHECK(manifest.languageContract->vocabularySha256 == fixture.profile.vocabularySha256);
    CHECK(manifest.languageContract->stressPolicy == fixture.profile.stressPolicy);
    CHECK(manifest.expectedSampleRate == 16000U); CHECK(manifest.units.size() == 1U);
    CHECK(manifest.units.front().phones.front() == (distinguish ? "iy1" : "iy"));
    CHECK(manifest.matchedPhone("iy1") == (distinguish ? "iy1" : "iy"));
    CHECK(manifest.matchedPhone("iy2") == (distinguish ? "iy2" : "iy"));
    CHECK(core::sha256File(created.value().root / manifest.units.front().audioPath).value() == fixture.project.takes.front().rawAssetSha256);
    CHECK(production::encodeProductionProject(fixture.repository.recover().value()) == before);
    CHECK(fixture.project.reviews.empty()); CHECK(!created.value().releaseEligible);
    CHECK(!production::requireTakeSourceQualification(fixture.project, fixture.project.takes.front().takeId));
  }
  Fixture legacy;
  legacy.identity.language = domain::Language::English;
  const auto created = legacy.create(); CHECK(created);
  const auto manifest = voicebank::ManifestJsonCodec{}.load(created.value().root / "manifest.json"); CHECK(manifest);
  CHECK(!manifest.value().languageContract); CHECK(manifest.value().matchedPhone("iy1") == "iy1");
}

TEST_CASE("English review capture refuses valid altered or removed inventory contracts") {
  for (const bool distinguish : {false, true}) {
    test::support::EnglishManifestDraftFixture fixture{distinguish, true};
    const auto created = fixture.create("editable"); CHECK(created);
    const auto loaded = voicebank::ManifestJsonCodec{}.load(created.value().root / "manifest.json"); CHECK(loaded);
    CHECK(production::prepareSampleCandidateReview(fixture.producerRoot, fixture.project, loaded.value()));
    const auto before = production::encodeProductionProject(fixture.project);
    for (const bool remove : {false, true}) {
      auto changed = loaded.value();
      if (remove) changed.languageContract.reset();
      else changed.languageContract->stressPolicy = distinguish ? "FOLD_LEXICAL_STRESS" : "DISTINGUISH_LEXICAL_STRESS";
      CHECK(changed.validate());
      const auto result = production::prepareSampleCandidateReview(fixture.producerRoot, fixture.project, changed);
      CHECK(!result); CHECK(result.error().code == core::ErrorCode::Conflict);
      CHECK(result.error().message == "English review contract must match the bound producer inventory");
      CHECK(result.error().context == "languageContract");
      CHECK(production::encodeProductionProject(fixture.repository.recover().value()) == before);
      CHECK(fixture.project.reviews.empty());
    }
  }
}

TEST_CASE("English review commit rechecks retained inventory before any review mutation") {
  test::support::EnglishManifestDraftFixture fixture;
  CHECK(fixture.repository.registerReviewer(fixture.project, "reviewer",
      core::sha256Hex(production::encodeProductionProject(fixture.project)), "producer", "2026-10-15T00:03:00Z"));
  const auto created = fixture.create("editable"); CHECK(created);
  const auto manifest = voicebank::ManifestJsonCodec{}.load(created.value().root / "manifest.json"); CHECK(manifest);
  const auto packet = production::prepareSampleCandidateReview(fixture.producerRoot, fixture.project, manifest.value()); CHECK(packet);
  const auto before = production::encodeProductionProject(fixture.project);
  std::filesystem::rename(fixture.producerRoot.parent_path() / "inventory.json", fixture.root / "inventory-backup.json");
  const auto result = production::commitSampleCandidateReview(fixture.producerRoot, fixture.project, packet.value(),
      "reviewer", "2026-10-15T00:04:00Z", production::SampleCandidateReviewDecision::Reject);
  CHECK(!result);
  CHECK(result.error().message == "English draft inventory must be a retained regular file, not missing or linked");
  CHECK(production::encodeProductionProject(fixture.repository.recover().value()) == before);
  CHECK(fixture.project.reviews.empty());
  std::filesystem::rename(fixture.root / "inventory-backup.json", fixture.producerRoot.parent_path() / "inventory.json");
  // Restoring only the inventory makes the exact same packet and registered
  // reviewer eligible for a rejection. No source or take approval is granted.
  CHECK(production::commitSampleCandidateReview(fixture.producerRoot, fixture.project, packet.value(),
      "reviewer", "2026-10-15T00:04:00Z", production::SampleCandidateReviewDecision::Reject));
  CHECK(!fixture.project.reviews.empty());
}

TEST_CASE("English draft layout guard refuses a verified renamed producer without changing its authority") {
  test::support::EnglishManifestDraftFixture fixture;
  const auto before = production::encodeProductionProject(fixture.project);
  const auto inventory = fixture.producerRoot.parent_path() / "inventory.json";
  const auto inventoryHash = core::sha256File(inventory); CHECK(inventoryHash);
  const auto arbitrary = fixture.producerRoot.parent_path() / "arbitrary-workspace";
  std::filesystem::rename(fixture.producerRoot, arbitrary);
  production::ProductionProjectRepository repository{arbitrary};
  CHECK(repository.verify(fixture.project));
  const auto raw = repository.assetPath(fixture.project.assets.front());
  const auto rawHash = core::sha256File(raw); CHECK(rawHash);
  const auto destination = std::filesystem::absolute(fixture.root / "refused-layout");
  const auto result = production::createSampleManifestDraft(arbitrary, fixture.project, fixture.identity, destination);
  CHECK(!result); CHECK(result.error().code == core::ErrorCode::Conflict);
  CHECK(result.error().message.find("DESTINATION/producer") != std::string::npos);
  CHECK(result.error().message.find("new-producer-workspace") != std::string::npos);
  CHECK(!std::filesystem::exists(destination));
  CHECK(production::encodeProductionProject(repository.recover().value()) == before);
  CHECK(core::sha256File(raw).value() == rawHash.value());
  CHECK(core::sha256File(inventory).value() == inventoryHash.value());
  CHECK(fixture.project.reviews.empty());
}

TEST_CASE("English producer drafts refuse absent tampered mismatched linked or out of inventory authority") {
  test::support::EnglishManifestDraftFixture fixture;
  const auto inventory = fixture.root / "workspace" / "inventory.json";
  const auto original = core::readTextFileLimited(inventory, 1024U * 1024U); CHECK(original);
  const auto backup = fixture.root / "inventory-backup.json";
  std::filesystem::rename(inventory, backup);
  CHECK(!fixture.create("missing")); CHECK(!std::filesystem::exists(fixture.root / "missing"));
  std::error_code error; std::filesystem::create_symlink(backup, inventory, error);
  if (!error) { CHECK(!fixture.create("linked")); CHECK(!std::filesystem::exists(fixture.root / "linked")); std::filesystem::remove(inventory); }
  std::filesystem::rename(backup, inventory);
  auto document = formats::parseJson(original.value()); CHECK(document);
  document.value().asObject()["profileId"] = "foreign-inventory";
  CHECK(core::durableAtomicWriteText(inventory, formats::stringifyJson(document.value(), true)));
  CHECK(!fixture.create("tampered")); CHECK(!std::filesystem::exists(fixture.root / "tampered"));
  auto other = fixture.profile; other.profileId = "foreign-inventory";
  const auto regenerated = production::generateDraftInventory(other); CHECK(regenerated);
  CHECK(core::durableAtomicWriteText(inventory, regenerated.value().documentJson));
  CHECK(!fixture.create("mismatched")); CHECK(!std::filesystem::exists(fixture.root / "mismatched"));
  CHECK(core::durableAtomicWriteText(inventory, original.value()));
  fixture.project.unitAssignments.push_back({.coverageKey = "sustain:aa", .pitchLayer = 69,
      .promptId = "outside", .plannedTakeId = "outside", .style = "neutral"});
  CHECK(fixture.repository.save(fixture.project, {"save", "outside", "producer", "2026-10-15T00:03:00Z"}));
  CHECK(!fixture.create("outside")); CHECK(!std::filesystem::exists(fixture.root / "outside"));
}

TEST_CASE("English producer inventory replacement during draft staging prevents publication") {
  test::support::EnglishManifestDraftFixture fixture;
  auto changed = fixture.profile; changed.stressPolicy = "DISTINGUISH_LEXICAL_STRESS";
  const auto regenerated = production::generateDraftInventory(changed); CHECK(regenerated);
  const auto before = production::encodeProductionProject(fixture.project);
  const auto result = fixture.create("changed", {.faultInjector = [&](production::ManifestDraftStage stage) {
    if (stage == production::ManifestDraftStage::BeforeCommit)
      CHECK(core::durableAtomicWriteText(fixture.root / "workspace" / "inventory.json", regenerated.value().documentJson));
    return core::success();
  }});
  CHECK(!result); CHECK(!std::filesystem::exists(fixture.root / "changed"));
  CHECK(production::encodeProductionProject(fixture.repository.recover().value()) == before);
}

TEST_CASE("style-owned manifest drafts preserve all styles and distinguish missing assignments") {
  Fixture fixture{false, true};
  fixture.project.unitAssignments.push_back({.coverageKey = "sustain:a", .pitchLayer = 69,
      .promptId = "soft-a", .plannedTakeId = "soft-a", .style = "soft"});
  CHECK(fixture.repository.save(fixture.project, {"save", fixture.project.projectId, "producer", "2026-09-13T00:00:00Z"}));
  const auto partial = fixture.create("partial"); CHECK(partial);
  CHECK(partial.value().missingAssignments == std::vector<std::string>{"soft:sustain:a@69"});
  CHECK(fixture.repository.importRaw(fixture.project, fixture.root / "raw.wav",
      {.takeId = "soft-a", .promptId = "soft-a", .coverageKey = "sustain:a", .pitchLayer = 69, .style = "soft"},
      {"import", "soft-a", "producer", "2026-09-13T00:01:00Z"}));
  const auto before = production::encodeProductionProject(fixture.project);
  const auto complete = fixture.create("complete"); CHECK(complete);
  CHECK(complete.value().missingAssignments.empty());
  const auto manifest = voicebank::ManifestJsonCodec{}.load(complete.value().root / "manifest.json"); CHECK(manifest);
  CHECK(manifest.value().styles == (std::vector<std::string>{"original", "soft"}));
  CHECK(manifest.value().units.size() == 2U);
  CHECK(manifest.value().units[0].id != manifest.value().units[1].id);
  CHECK(manifest.value().units[0].style != manifest.value().units[1].style);
  CHECK(manifest.value().units[0].audioPath == manifest.value().units[1].audioPath);
  CHECK(production::prepareSampleCandidateReview(fixture.root / "producer", fixture.project, manifest.value()));
  fixture.identity.style = "soft";
  const auto selectedSoft = fixture.create("selected-soft"); CHECK(selectedSoft);
  CHECK(voicebank::ManifestJsonCodec{}.load(selectedSoft.value().root / "manifest.json").value() == manifest.value());
  fixture.identity.style = "unknown";
  CHECK(!fixture.create("unknown-style"));
  CHECK(!std::filesystem::exists(fixture.root / "unknown-style"));
  CHECK(production::encodeProductionProject(fixture.project) == before);
  CHECK(fixture.project.reviews.empty());
}

TEST_CASE("editable manifest draft copies exact current audio and estimates unlocked markers without approving experimental sources") {
  Fixture fixture;
  const auto before = production::encodeProductionProject(fixture.project);
  const auto created = fixture.create(); CHECK(created);
  CHECK(!created.value().releaseEligible); CHECK(created.value().durabilityConfirmed);
  const auto manifest = voicebank::ManifestJsonCodec{}.load(created.value().root / "manifest.json"); CHECK(manifest);
  CHECK(manifest.value().units.size() == 1U);
  const auto& unit = manifest.value().units.front();
  CHECK(unit.phones == std::vector<std::string>{"a"}); CHECK(unit.style == "original");
  CHECK(unit.renderer == voicebank::RendererHint::ClassicPsola); CHECK(unit.pitchMarks.size() >= 6U);
  CHECK(std::all_of(unit.pitchMarks.begin(), unit.pitchMarks.end(), [](const auto& mark) { return !mark.locked; }));
  CHECK(unit.markers.validate(9600));
  CHECK(core::sha256File(created.value().root / unit.audioPath).value() == fixture.project.takes.front().rawAssetSha256);
  CHECK(core::sha256File(created.value().root / "manifest.json").value() == created.value().manifestSha256);
  CHECK(core::sha256File(created.value().root / "draft.json").value() == created.value().draftSha256);
  const auto descriptor = formats::parseJson(core::readTextFileLimited(created.value().root / "draft.json", 1024U * 1024U).value()); CHECK(descriptor);
  CHECK(descriptor.value().find("status")->asString() == "UNREVIEWED_DRAFT");
  CHECK(descriptor.value().find("approval")->asString() == "unchanged");
  CHECK(production::encodeProductionProject(fixture.repository.recover().value()) == before);
  CHECK(fixture.project.reviews.empty()); CHECK(!production::requireTakeSourceQualification(fixture.project, "take-a"));
  const auto capture = production::prepareSampleCandidateReview(fixture.root / "producer", fixture.project, manifest.value()); CHECK(capture);
  CHECK(!production::resolveReviewedSampleCandidate(fixture.root / "producer", fixture.project, manifest.value()));
}

TEST_CASE("editable draft keeps missing coverage visible and stable unit identity across row order and effective edits") {
  Fixture fixture;
  fixture.project.unitAssignments.push_back({.coverageKey = "sustain:i", .pitchLayer = 69, .promptId = "i", .plannedTakeId = "take-i"});
  CHECK(fixture.repository.save(fixture.project, {.action = "save", .subjectId = fixture.project.projectId, .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:02:00Z"}));
  const auto first = fixture.create(); CHECK(first); CHECK(first.value().missingAssignments == std::vector<std::string>{"sustain:i@69"});
  const auto old = voicebank::ManifestJsonCodec{}.load(first.value().root / "manifest.json"); CHECK(old);
  const auto stage = fixture.repository.stageOperation(fixture.project, "take-a", "", {.kind = production::OperationKind::NormalizeGain, .targetPeak = 0.1F}, "quieter"); CHECK(stage);
  const auto edited = fixture.repository.commitStaged(fixture.project, stage.value(), "edit-a", "producer", "2026-09-09T10:03:00Z", "take-a", ""); CHECK(edited);
  std::reverse(fixture.project.unitAssignments.begin(), fixture.project.unitAssignments.end());
  CHECK(fixture.repository.save(fixture.project, {.action = "save", .subjectId = fixture.project.projectId, .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:04:00Z"}));
  const auto second = fixture.create("edited"); CHECK(second);
  const auto updated = voicebank::ManifestJsonCodec{}.load(second.value().root / "manifest.json"); CHECK(updated);
  CHECK(updated.value().units.front().id == old.value().units.front().id);
  CHECK(core::sha256File(second.value().root / updated.value().units.front().audioPath).value() == edited.value().outputSha256);
  CHECK(updated.value().units.front().audioPath != old.value().units.front().audioPath);
  CHECK(core::sha256File(first.value().root / old.value().units.front().audioPath).value() == fixture.project.takes.front().rawAssetSha256);
}

TEST_CASE("draft creation preserves existing destinations and rejects stale unauthorized cancelled or excessive work") {
  Fixture fixture;
  const auto created = fixture.create(); CHECK(created);
  const auto before = core::sha256File(created.value().root / "manifest.json"); CHECK(before);
  CHECK(!fixture.create());
  CHECK(core::sha256File(created.value().root / "manifest.json").value() == before.value());
  std::stop_source cancelled; cancelled.request_stop();
  CHECK(!fixture.create("cancelled", {}, cancelled.get_token())); CHECK(!std::filesystem::exists(fixture.root / "cancelled"));
  CHECK(!fixture.create("budget", {.maximumAudioBytes = 8U})); CHECK(!std::filesystem::exists(fixture.root / "budget"));
  CHECK(!fixture.create("work", {.maximumPitchTransformButterflies = 1U})); CHECK(!std::filesystem::exists(fixture.root / "work"));
  auto stale = fixture.project;
  CHECK(fixture.repository.save(fixture.project, {.action = "save", .subjectId = fixture.project.projectId, .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:02:00Z"}));
  CHECK(!production::createSampleManifestDraft(fixture.root / "producer", stale, fixture.identity, std::filesystem::absolute(fixture.root / "stale")));
  fixture.project.sourceStrategies.front().permissions.transformation = false;
  CHECK(fixture.repository.save(fixture.project, {.action = "save", .subjectId = "synthetic", .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:03:00Z"}));
  CHECK(!fixture.create("denied")); CHECK(!std::filesystem::exists(fixture.root / "denied"));
}

TEST_CASE("editable draft cannot claim implicit language or overwrite producer and rejects unsafe output parents") {
  Fixture fixture;
  fixture.identity.language = domain::Language::Unspecified; CHECK(!fixture.create());
  fixture.identity.language = domain::Language::Japanese; fixture.identity.style.clear(); CHECK(!fixture.create());
  fixture.identity.style = "original";
  CHECK(!fixture.create("producer/inside")); CHECK(!std::filesystem::exists(fixture.root / "producer/inside"));
  std::error_code error;
  std::filesystem::create_directory(fixture.root / "external");
  std::filesystem::create_directory_symlink(fixture.root / "external", fixture.root / "linked", error);
  if (!error) { CHECK(!fixture.create("linked/draft")); CHECK(std::filesystem::is_empty(fixture.root / "external")); }
  fixture.project.unitAssignments.front().coverageKey = "not-a-canonical-kind:a";
  fixture.project.takes.front().coverageKey = "not-a-canonical-kind:a";
  // Local mutation is rejected before any artifact, independently of mapping.
  CHECK(!fixture.create("invalid")); CHECK(!std::filesystem::exists(fixture.root / "invalid"));
}

TEST_CASE("draft atomic publication rejects changed stage and preserves a late committed durability receipt") {
  Fixture fixture;
  const auto tamper = [&](production::ManifestDraftStage stage) -> core::Result<void> {
    if (stage == production::ManifestDraftStage::BeforeCommit) {
      for (const auto& entry : std::filesystem::directory_iterator(fixture.root)) {
        if (entry.path().filename().string().starts_with(".seam-draft-"))
          CHECK(core::durableAtomicWriteText(entry.path() / "manifest.json", "{}"));
      }
    }
    return core::success();
  };
  CHECK(!fixture.create("tampered", {.faultInjector = tamper})); CHECK(!std::filesystem::exists(fixture.root / "tampered"));
  const auto late = fixture.create("uncertain", {.faultInjector = [](production::ManifestDraftStage stage) {
    return stage == production::ManifestDraftStage::AfterCommitBeforeParentSync ? core::failure(core::ErrorCode::IoError, "injected directory sync failure") : core::success();
  }});
  CHECK(late); CHECK(!late.value().durabilityConfirmed); CHECK(!late.value().releaseEligible);
  CHECK(core::sha256File(late.value().root / "manifest.json").value() == late.value().manifestSha256);
  CHECK(late.value().diagnostics.back().find("do not overwrite or repeat") != std::string::npos);
  CHECK(fixture.project.reviews.empty());
}

TEST_CASE("draft output from supported creator commands can be explicitly reviewed and published") {
  Fixture fixture{true};
  const auto draft = fixture.create(); CHECK(draft);
  const auto manifest = voicebank::ManifestJsonCodec{}.load(draft.value().root / "manifest.json"); CHECK(manifest);
  const auto packet = production::prepareSampleCandidateReview(fixture.root / "producer", fixture.project, manifest.value()); CHECK(packet);
  CHECK(!production::commitSampleCandidateReview(fixture.root / "producer", fixture.project, packet.value(), "producer", "2026-09-09T10:02:00Z", production::SampleCandidateReviewDecision::Accept));
  const auto accepted = production::commitSampleCandidateReview(fixture.root / "producer", fixture.project, packet.value(), "reviewer", "2026-09-09T10:02:00Z", production::SampleCandidateReviewDecision::Accept);
  CHECK(accepted); CHECK(accepted.value().candidate);
  const auto published = production::publishSampleCandidate(fixture.root / "producer", fixture.project, *accepted.value().candidate, std::filesystem::absolute(fixture.root / "candidate"));
  CHECK(published); CHECK(!published.value().releaseEligible);
  CHECK(core::sha256File(draft.value().root / "manifest.json").value() == draft.value().manifestSha256);
}

TEST_CASE("draft rejects an identical replacement directory and cleanup preserves replacement-owned files") {
  Fixture fixture;
  std::filesystem::path stagePath;
  const auto preserved = fixture.root / "preserved-original-stage";
  const auto substitution = [&](production::ManifestDraftStage stage) -> core::Result<void> {
    if (stage == production::ManifestDraftStage::BeforeCommit) {
      stagePath = privateStage(fixture.root, ".seam-draft-");
      std::filesystem::rename(stagePath, preserved);
      std::filesystem::copy(preserved, stagePath, std::filesystem::copy_options::recursive);
      CHECK(core::durableAtomicWriteTextNew(stagePath / "replacement-owned.txt", "preserve replacement"));
    }
    return core::success();
  };
  const auto result = fixture.create("replacement-result", {.faultInjector = substitution});
  CHECK(!result); CHECK(!std::filesystem::exists(fixture.root / "replacement-result"));
  CHECK(core::readTextFileLimited(stagePath / "replacement-owned.txt", 1024U).value() == "preserve replacement");
  CHECK(std::filesystem::is_regular_file(preserved / "manifest.json"));
  CHECK(std::filesystem::is_regular_file(stagePath / "manifest.json"));
}

#if !defined(_WIN32)
TEST_CASE("draft rejects stage directory symlink substitution without deleting either owner") {
  Fixture fixture;
  std::filesystem::path stagePath;
  const auto preserved = fixture.root / "preserved-original-stage";
  const auto substitution = [&](production::ManifestDraftStage stage) -> core::Result<void> {
    if (stage == production::ManifestDraftStage::BeforeCommit) {
      stagePath = privateStage(fixture.root, ".seam-draft-");
      std::filesystem::rename(stagePath, preserved);
      std::filesystem::create_directory_symlink(preserved, stagePath);
    }
    return core::success();
  };
  const auto result = fixture.create("symlink-result", {.faultInjector = substitution});
  CHECK(!result); CHECK(!std::filesystem::exists(fixture.root / "symlink-result"));
  CHECK(std::filesystem::is_symlink(stagePath));
  CHECK(std::filesystem::is_regular_file(preserved / "manifest.json"));
  CHECK(std::filesystem::is_regular_file(preserved / "draft.json"));
}

TEST_CASE("draft parent replacement does not redirect commit or cleanup") {
  Fixture fixture;
  const auto parent = fixture.root / "output-parent";
  const auto preserved = fixture.root / "preserved-output-parent";
  CHECK(std::filesystem::create_directory(parent));
  std::filesystem::path replacementStage;
  const auto substitution = [&](production::ManifestDraftStage stage) -> core::Result<void> {
    if (stage == production::ManifestDraftStage::BeforeCommit) {
      const auto name = privateStage(parent, ".seam-draft-").filename();
      std::filesystem::rename(parent, preserved);
      CHECK(std::filesystem::create_directory(parent));
      replacementStage = parent / name;
      CHECK(std::filesystem::create_directory(replacementStage));
      CHECK(core::durableAtomicWriteTextNew(replacementStage / "replacement-owned.txt", "keep"));
    }
    return core::success();
  };
  const auto result = fixture.create("output-parent/result", {.faultInjector = substitution});
  CHECK(!result); CHECK(!std::filesystem::exists(parent / "result"));
  CHECK(core::readTextFileLimited(replacementStage / "replacement-owned.txt", 1024U).value() == "keep");
  CHECK(std::filesystem::is_regular_file(privateStage(preserved, ".seam-draft-") / "manifest.json"));
}

TEST_CASE("reviewed candidate shares the captured-directory substitution guard") {
  Fixture fixture{true};
  const auto draft = fixture.create(); CHECK(draft);
  const auto manifest = voicebank::ManifestJsonCodec{}.load(draft.value().root / "manifest.json"); CHECK(manifest);
  const auto packet = production::prepareSampleCandidateReview(fixture.root / "producer", fixture.project, manifest.value()); CHECK(packet);
  const auto accepted = production::commitSampleCandidateReview(fixture.root / "producer", fixture.project, packet.value(),
      "reviewer", "2026-09-09T10:02:00Z", production::SampleCandidateReviewDecision::Accept);
  CHECK(accepted); CHECK(accepted.value().candidate);
  std::filesystem::path stagePath;
  const auto preserved = fixture.root / "preserved-candidate-stage";
  const auto substitution = [&](production::CandidatePublicationStage stage) -> core::Result<void> {
    if (stage == production::CandidatePublicationStage::BeforeCommit) {
      stagePath = privateStage(fixture.root, ".seam-candidate-");
      std::filesystem::rename(stagePath, preserved);
      std::filesystem::create_directory_symlink(preserved, stagePath);
    }
    return core::success();
  };
  const auto result = production::publishSampleCandidate(fixture.root / "producer", fixture.project,
      *accepted.value().candidate, std::filesystem::absolute(fixture.root / "candidate-result"), {.faultInjector = substitution});
  CHECK(!result); CHECK(!std::filesystem::exists(fixture.root / "candidate-result"));
  CHECK(std::filesystem::is_symlink(stagePath));
  CHECK(std::filesystem::is_regular_file(preserved / "candidate.json"));
}
#endif

TEST_CASE("pitch mark generation accepts cancellation and bounded work without changing valid output") {
  const auto samples = test::support::sineWave(48000U, 440.0, 0.2, 0.25F);
  const voicebank::PitchMarkGenerationConfig config{.pitch = {.correlationMethod = voicebank::PitchCorrelationMethod::Fft}};
  const auto original = voicebank::generatePitchMarks(samples, 48000U, 0, static_cast<time::SampleFrame>(samples.size()), config); CHECK(original);
  const auto bounded = voicebank::generatePitchMarks(samples, 48000U, 0, static_cast<time::SampleFrame>(samples.size()), config, {},
      {.maximumFrames = 100U, .maximumCorrelationTerms = 0U, .maximumTransformButterflies = 10U * 1024U * 1024U});
  CHECK(bounded); CHECK(bounded.value() == original.value());
  CHECK(!voicebank::generatePitchMarks(samples, 48000U, 0, static_cast<time::SampleFrame>(samples.size()), config, {}, {.maximumTransformButterflies = 1U}));
  std::stop_source cancelled; cancelled.request_stop();
  CHECK(!voicebank::generatePitchMarks(samples, 48000U, 0, static_cast<time::SampleFrame>(samples.size()), config, cancelled.get_token()));
}
