#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/core/exclusive_file_lock.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/rendering/region_renderer.hpp"
#include "seam/voicebank/content_identity.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/candidate_publication.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"

#include <algorithm>
#include <barrier>
#include <filesystem>
#include <limits>
#include <string>
#include <thread>

namespace {
struct CandidateFixture final {
  std::filesystem::path root;
  seam::voicebank_production::VoicebankProductionProject project;
  seam::voicebank_production::SampleCandidateRequest request;
  seam::voicebank_production::SampleCandidateReviewPacket packet;
};

CandidateFixture reviewedCandidateFixture(bool withProcessing = true, std::string rawOperatorId = "producer", bool approve = true, bool styleOwned = false) {
  namespace production = seam::voicebank_production;
  CandidateFixture fixture;
  fixture.root = seam::test::support::temporaryDirectory("reviewed-sample-candidate");
  const auto license = fixture.root / "source-notice.txt";
  CHECK(seam::core::durableAtomicWriteText(license, "GENERATED_TEST_FIXTURE_ONLY: no production singer qualification"));
  const auto digest = seam::core::sha256File(license);
  CHECK(digest);
  fixture.project = {
      .projectId = "candidate-test", .inventoryId = "test-inventory",
      .inventorySha256 = std::string(64U, 'a'), .selectedSourceStrategyId = "test-synthesis",
      .licenseLocator = license.string(), .licenseSha256 = digest.value(), .immutableAssetRoot = "assets",
  };
  auto& project = fixture.project;
  if (styleOwned) { project.schemaVersion = production::kProductionStyleSchemaVersion; project.language = "ja"; }
  project.sourceStrategies.push_back({
      .id = "test-synthesis", .kind = production::SourceStrategyKind::ProceduralSynthesis,
      .rights = production::Feasibility::Pass, .coverage = production::Feasibility::Pass,
      .listening = production::Feasibility::Pass,
      .permissions = {.sourceUse = true, .transformation = true,
                     .singingBankRedistribution = true, .commercialRenders = true},
      .licenseLocator = license.string(), .licenseSha256 = digest.value(), .evidenceState = "SYNTHETIC_TEST_ONLY",
  });
  project.operators = {{.operatorId = "producer", .role = "PRODUCER"}, {.operatorId = "reviewer", .role = "REVIEWER"}};
  project.unitAssignments = {{.coverageKey = "sustain:a", .pitchLayer = 69, .promptId = "prompt-a", .plannedTakeId = "take-a"}};
  if (styleOwned) project.unitAssignments.front().style = "original";
  production::ProductionProjectRepository repository{fixture.root / "workspace"};
  CHECK(repository.initialize(project, {.action = "create", .subjectId = project.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:00:00Z"}));
  const auto source = fixture.root / "raw.wav";
  CHECK(seam::voicebank::writeWav(source, {.sampleRate = 48000U, .channels = 1U,
      .sampleFormat = seam::voicebank::WavSampleFormat::Pcm24}, seam::test::support::sineWave(48000U, 440.0, 0.12, 0.25F)));
  const auto raw = repository.importRaw(project, source,
      {.takeId = "take-a", .promptId = "prompt-a", .coverageKey = "sustain:a", .pitchLayer = 69, .style = styleOwned ? "original" : ""},
      {.action = "import", .subjectId = "take-a", .operatorId = rawOperatorId, .occurredAtUtc = "2026-09-09T10:01:00Z"});
  CHECK(raw);
  auto effectiveAudioSha256 = raw.value().sha256;
  if (withProcessing) {
    const auto operation = repository.stageOperation(project, "take-a", "", {.kind = production::OperationKind::NormalizeGain,
        .targetPeak = 0.2F}, "normalized-a");
    CHECK(operation);
    const auto derived = repository.commitStaged(project, operation.value(), "revision-a", "producer", "2026-09-09T10:02:00Z", "take-a", "");
    CHECK(derived);
    CHECK(derived.value().outputSha256 != raw.value().sha256);
    effectiveAudioSha256 = derived.value().outputSha256;
  }

  auto& request = fixture.request;
  request.manifest = {.id = "generated.test.candidate", .version = "0.1.0", .displayName = "Generated Test Candidate",
      .characterId = {}, .characterVersion = {}, .language = seam::domain::Language::Japanese,
      .expectedSampleRate = 48000U, .styles = {"original"}};
  request.manifest.units.push_back({.id = "a-69", .alias = "a", .phones = {"a"}, .kind = seam::voicebank::UnitKind::Sustain,
      .audioPath = std::filesystem::path{"audio"} / (effectiveAudioSha256 + ".wav"),
      .rootMidi = 69, .style = "original", .take = 1, .priority = 0, .gainDb = 0.0F,
      .renderer = seam::voicebank::RendererHint::ClassicPsola,
      .markers = {.audioOffset = 0, .consonantEnd = 0, .vowelOnset = 0, .stableStart = 480,
                  .loopStart = 480, .loopEnd = 4800, .releaseStart = 5280, .audioEnd = 5760},
      .pitchMarks = {}, .enabled = true});
  // Known periods of this generated 440 Hz fixture cover the whole source,
  // including its sustain/release. These are not measured singer annotations.
  for (std::int64_t period = 1; ; ++period) {
    const auto frame = static_cast<seam::time::SampleFrame>(std::llround(static_cast<double>(period) * 48000.0 / 440.0));
    if (frame >= request.manifest.units.front().markers.audioEnd) break;
    request.manifest.units.front().pitchMarks.push_back({.frame = frame, .confidence = 1.0F, .locked = true});
  }
  const auto beforePreparation = production::encodeProductionProject(project);
  const auto packet = production::prepareSampleCandidateReview(fixture.root / "workspace", project, request.manifest);
  CHECK(packet);
  CHECK(production::encodeProductionProject(project) == beforePreparation);
  CHECK(project.reviews.empty());
  CHECK(project.unitAssignments.front().state == production::UnitQueueState::MarkerReview);
  fixture.packet = packet.value();
  request = {project.lastDurableGeneration, seam::core::sha256Hex(beforePreparation), packet.value().manifest,
      {{"a-69", "take-a", effectiveAudioSha256, {}, {}}}};
  if (approve) {
    const auto receipt = production::commitSampleCandidateReview(fixture.root / "workspace", project, packet.value(),
        "reviewer", "2026-09-09T10:03:00Z", production::SampleCandidateReviewDecision::Accept);
    CHECK(receipt);
    CHECK(receipt.value().durabilityConfirmed);
    CHECK(receipt.value().reviews.size() == 1U);
    CHECK(receipt.value().candidate);
    request = *receipt.value().candidate;
  }
  return fixture;
}

void addSecondReviewUnit(CandidateFixture& fixture, std::string importer = "producer") {
  namespace production = seam::voicebank_production;
  fixture.project.unitAssignments.push_back({.coverageKey = "sustain:i", .pitchLayer = 69,
      .promptId = "prompt-i", .plannedTakeId = "take-i"});
  production::ProductionProjectRepository repository{fixture.root / "workspace"};
  CHECK(repository.importRaw(fixture.project, fixture.root / "raw.wav",
      {.takeId = "take-i", .promptId = "prompt-i", .coverageKey = "sustain:i", .pitchLayer = 69},
      {.action = "import", .subjectId = "take-i", .operatorId = std::move(importer), .occurredAtUtc = "2026-09-09T10:02:00Z"}));
  auto unit = fixture.request.manifest.units.front();
  unit.id = "i-69"; unit.alias = "i"; unit.phones = {"i"};
  fixture.request.manifest.units.push_back(std::move(unit));
  const auto packet = production::prepareSampleCandidateReview(fixture.root / "workspace", fixture.project, fixture.request.manifest);
  CHECK(packet);
  fixture.packet = packet.value();
}
}  // namespace

TEST_CASE("sample review packet roundtrips bounded material and rejects tampering") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture(false, "producer", false);
  const auto before = production::encodeProductionProject(fixture.project);
  const auto encoded = production::encodeSampleCandidateReviewPacket(fixture.packet);
  CHECK(encoded);
  const auto decoded = production::decodeSampleCandidateReviewPacket(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value().manifest == fixture.packet.manifest);
  CHECK(decoded.value().packetSha256 == fixture.packet.packetSha256);
  CHECK(production::encodeSampleCandidateReviewPacket(decoded.value()).value() == encoded.value());
  CHECK(fixture.packet.units.front().audioSha256 == fixture.project.takes.front().rawAssetSha256);
  CHECK(fixture.packet.units.front().originOperatorId == "producer");
  CHECK(fixture.packet.units.front().frameCount == 5760U);
  CHECK(fixture.packet.units.front().rms > 0.0);
  CHECK(!production::resolveReviewedSampleCandidate(fixture.root / "workspace", fixture.project, fixture.packet.manifest));
  CHECK(production::encodeProductionProject(fixture.project) == before);

  auto tampered = fixture.packet;
  tampered.manifest.units.front().gainDb = -1.0F;
  CHECK(!production::encodeSampleCandidateReviewPacket(tampered));
  CHECK(!production::commitSampleCandidateReview(fixture.root / "workspace", fixture.project, tampered,
      "reviewer", "2026-09-09T10:03:00Z", production::SampleCandidateReviewDecision::Accept));
  CHECK(production::encodeProductionProject(fixture.project) == before);
  const auto parsed = seam::formats::parseJson(encoded.value());
  CHECK(parsed);
  for (const auto& key : {"sourceProjectSha256", "packetSha256"}) {
    auto changed = parsed.value();
    *changed.find(key) = seam::formats::JsonValue{std::string(64U, '0')};
    CHECK(!production::decodeSampleCandidateReviewPacket(seam::formats::stringifyJson(changed)));
  }
  auto wrongType = parsed.value();
  *wrongType.find("sourceGeneration") = seam::formats::JsonValue{true};
  CHECK(!production::decodeSampleCandidateReviewPacket(seam::formats::stringifyJson(wrongType)));
  auto unknown = parsed.value();
  unknown.asObject().emplace("approved", seam::formats::JsonValue{true});
  CHECK(!production::decodeSampleCandidateReviewPacket(seam::formats::stringifyJson(unknown)));
  auto duplicated = encoded.value();
  duplicated.insert(1U, "\"schemaVersion\":1,");
  CHECK(!production::decodeSampleCandidateReviewPacket(duplicated));
  // The first textual "units" belongs to manifest.units, not the packet's
  // attribution rows. Locate the exact serialized top-level row instead.
  const auto row = seam::formats::stringifyJson(parsed.value().find("units")->asArray().front(), false);
  duplicated = seam::formats::stringifyJson(parsed.value(), false);
  const auto rowPosition = duplicated.find(row);
  CHECK(rowPosition != std::string::npos);
  CHECK(rowPosition == duplicated.rfind(row));
  duplicated.insert(rowPosition + 1U, "\"takeId\":\"take-a\",");
  CHECK(!seam::formats::parseJson(duplicated));
  CHECK(!production::decodeSampleCandidateReviewPacket(duplicated));
  tampered = fixture.packet;
  tampered.units.front().peak = std::numeric_limits<double>::infinity();
  CHECK(!production::encodeSampleCandidateReviewPacket(tampered));
  CHECK(!production::decodeSampleCandidateReviewPacket(std::string(64U * 1024U * 1024U + 1U, ' ')));
  auto oversizedManifest = fixture.packet.manifest;
  oversizedManifest.displayName = std::string(2U * 1024U * 1024U + 1U, 'x');
  CHECK(!production::prepareSampleCandidateReview(fixture.root / "workspace", fixture.project, oversizedManifest));
  CHECK(production::encodeProductionProject(fixture.project) == before);
}

TEST_CASE("style-owned review keeps approvals separate for identical PCM and pitch") {
  namespace production = seam::voicebank_production;
  auto legacy = reviewedCandidateFixture(false, "producer", false);
  auto legacyManifest = legacy.request.manifest;
  legacyManifest.styles.push_back("soft");
  auto legacySoft = legacyManifest.units.front(); legacySoft.id = "a-soft-69"; legacySoft.style = "soft";
  legacyManifest.units.push_back(legacySoft);
  const auto legacyPacket = production::prepareSampleCandidateReview(legacy.root / "workspace", legacy.project, legacyManifest);
  CHECK(!legacyPacket);
  CHECK(legacyPacket.error().code == seam::core::ErrorCode::Unsupported);
  auto fixture = reviewedCandidateFixture(false, "producer", false, true);
  auto& project = fixture.project;
  project.unitAssignments.push_back({.coverageKey = "sustain:a", .pitchLayer = 69,
      .promptId = "prompt-soft", .plannedTakeId = "take-soft", .style = "soft"});
  production::ProductionProjectRepository repository{fixture.root / "workspace"};
  CHECK(repository.importRaw(project, fixture.root / "raw.wav",
      {.takeId = "take-soft", .promptId = "prompt-soft", .coverageKey = "sustain:a", .pitchLayer = 69, .style = "soft"},
      {"import", "take-soft", "producer", "2026-09-13T00:00:00Z"}));
  auto manifest = fixture.request.manifest;
  manifest.styles.push_back("soft");
  auto soft = manifest.units.front(); soft.id = "a-soft-69"; soft.style = "soft";
  manifest.units.push_back(soft);
  const auto packet = production::prepareSampleCandidateReview(fixture.root / "workspace", project, manifest); CHECK(packet);
  CHECK(packet.value().units.size() == 2U);
  CHECK(packet.value().units[0].takeId != packet.value().units[1].takeId);
  CHECK(packet.value().units[0].audioSha256 == packet.value().units[1].audioSha256);
  const std::vector<std::string> originalSelection{"a-69"}, softSelection{"a-soft-69"};
  const auto first = production::commitSampleCandidateReview(fixture.root / "workspace", project, packet.value(),
      "reviewer", "2026-09-13T00:01:00Z", production::SampleCandidateReviewDecision::Accept, originalSelection); CHECK(first);
  CHECK(project.unitAssignments[0].state == production::UnitQueueState::Approved);
  CHECK(project.unitAssignments[1].state == production::UnitQueueState::MarkerReview);
  CHECK(!first.value().candidate);
  const auto firstReview = project.reviews.front().reviewId;
  CHECK(!production::commitSampleCandidateReview(fixture.root / "workspace", project, packet.value(),
      "reviewer", "2026-09-13T00:02:00Z", production::SampleCandidateReviewDecision::Accept, softSelection));
  const auto refreshed = production::prepareSampleCandidateReview(fixture.root / "workspace", project, manifest); CHECK(refreshed);
  CHECK(production::commitSampleCandidateReview(fixture.root / "workspace", project, refreshed.value(),
      "reviewer", "2026-09-13T00:02:00Z", production::SampleCandidateReviewDecision::Accept, softSelection));
  CHECK(project.reviews.front().reviewId == firstReview);
  CHECK(project.reviews.size() == 2U);
  CHECK(project.reviews[0].takeId != project.reviews[1].takeId);
  CHECK(project.unitAssignments[1].state == production::UnitQueueState::Approved);
  auto relabeled = manifest; relabeled.units[1].style = "original";
  CHECK(!production::prepareSampleCandidateReview(fixture.root / "workspace", project, relabeled));
  CHECK(production::encodeProductionProject(repository.recover().value()) == production::encodeProductionProject(project));
}

TEST_CASE("sample review decisions atomically accept reject and retain previous history") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture(false, "producer", false);
  const auto initialGeneration = fixture.project.lastDurableGeneration;
  const auto accepted = production::commitSampleCandidateReview(fixture.root / "workspace", fixture.project, fixture.packet,
      "reviewer", "2026-09-09T10:03:00Z", production::SampleCandidateReviewDecision::Accept);
  CHECK(accepted);
  CHECK(accepted.value().committedGeneration == initialGeneration + 1U);
  CHECK(accepted.value().candidate);
  CHECK(accepted.value().reviews.size() == 1U);
  CHECK(fixture.project.takes.front().state == production::UnitQueueState::Approved);
  CHECK(fixture.project.unitAssignments.front().markerReviewed);
  CHECK(fixture.project.unitAssignments.front().pitchReviewed);
  const auto acceptedValues = fixture.project.metadataRevisions.front().values;
  const auto acceptedReviewId = fixture.project.reviews.front().reviewId;
  const auto nextPacket = production::prepareSampleCandidateReview(fixture.root / "workspace", fixture.project, fixture.packet.manifest);
  CHECK(nextPacket);
  CHECK(nextPacket.value().reviewBasisSha256 == fixture.packet.reviewBasisSha256);
  const auto rejected = production::commitSampleCandidateReview(fixture.root / "workspace", fixture.project, nextPacket.value(),
      "reviewer", "2026-09-09T10:04:00Z", production::SampleCandidateReviewDecision::Reject);
  CHECK(rejected);
  CHECK(rejected.value().committedGeneration == initialGeneration + 2U);
  CHECK(!rejected.value().candidate);
  CHECK(fixture.project.takes.front().state == production::UnitQueueState::Rejected);
  CHECK(!fixture.project.unitAssignments.front().markerReviewed);
  CHECK(!fixture.project.unitAssignments.front().pitchReviewed);
  CHECK(fixture.project.reviews.size() == 2U);
  CHECK(fixture.project.reviews.front().reviewId == acceptedReviewId);
  CHECK(fixture.project.metadataRevisions.front().values == acceptedValues);
  const auto recovered = production::ProductionProjectRepository{fixture.root / "workspace"}.recover();
  CHECK(recovered);
  CHECK(production::encodeProductionProject(recovered.value()) == production::encodeProductionProject(fixture.project));
  CHECK(!production::resolveReviewedSampleCandidate(fixture.root / "workspace", recovered.value(), fixture.packet.manifest));
}

TEST_CASE("sample review rejects stale edited media cancelled and invalid actor decisions") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture(false, "producer", false);
  production::ProductionProjectRepository repository{fixture.root / "workspace"};
  const auto original = production::encodeProductionProject(fixture.project);
  std::stop_source stop;
  stop.request_stop();
  CHECK(!production::prepareSampleCandidateReview(fixture.root / "workspace", fixture.project, fixture.packet.manifest, stop.get_token()));
  CHECK(!production::commitSampleCandidateReview(fixture.root / "workspace", fixture.project, fixture.packet,
      "reviewer", "2026-09-09T10:03:00Z", production::SampleCandidateReviewDecision::Accept, {}, stop.get_token()));
  for (const auto& actor : {"unknown", "producer"})
    CHECK(!production::commitSampleCandidateReview(fixture.root / "workspace", fixture.project, fixture.packet,
        actor, "2026-09-09T10:03:00Z", production::SampleCandidateReviewDecision::Accept));
  CHECK(production::encodeProductionProject(fixture.project) == original);
  CHECK(repository.recordMetadataRevision(fixture.project,
      {.revisionId = "new-source-annotation", .takeId = "take-a", .rawAssetSha256 = fixture.project.takes.front().rawAssetSha256,
       .kind = "source-annotation", .values = {{"annotation", "edited"}}, .operatorId = "producer", .performedAtUtc = "2026-09-09T10:03:00Z"},
      {.action = "marker", .subjectId = "new-source-annotation", .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:03:00Z"}));
  const auto editedState = production::encodeProductionProject(fixture.project);
  CHECK(!production::commitSampleCandidateReview(fixture.root / "workspace", fixture.project, fixture.packet,
      "reviewer", "2026-09-09T10:04:00Z", production::SampleCandidateReviewDecision::Accept));
  CHECK(production::encodeProductionProject(fixture.project) == editedState);
  CHECK(fixture.project.reviews.empty());

  auto media = reviewedCandidateFixture(false, "producer", false);
  production::ProductionProjectRepository mediaRepository{media.root / "workspace"};
  const auto mediaState = production::encodeProductionProject(media.project);
  CHECK(seam::voicebank::writeMonoPcm16Wav(mediaRepository.assetPath(media.project.assets.front()), 48000U,
      seam::test::support::sineWave(48000U, 220.0, 0.12, 0.15F)));
  CHECK(!production::commitSampleCandidateReview(media.root / "workspace", media.project, media.packet,
      "reviewer", "2026-09-09T10:04:00Z", production::SampleCandidateReviewDecision::Accept));
  CHECK(production::encodeProductionProject(media.project) == mediaState);
  CHECK(media.project.reviews.empty());
}

TEST_CASE("sequential sample unit reviews preserve independent approvals and bind changed unit material") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture(false, "producer", false);
  addSecondReviewUnit(fixture);
  const auto generation = fixture.project.lastDurableGeneration;
  const auto initialPacket = fixture.packet;
  const std::vector<std::string> aSelection{"a-69"}, iSelection{"i-69"};
  const auto first = production::commitSampleCandidateReview(fixture.root / "workspace", fixture.project, initialPacket,
      "reviewer", "2026-09-09T10:03:00Z", production::SampleCandidateReviewDecision::Accept, aSelection);
  CHECK(first);
  CHECK(!first.value().candidate);
  CHECK(fixture.project.lastDurableGeneration == generation + 1U);
  CHECK(fixture.project.unitAssignments[0].state == production::UnitQueueState::Approved);
  CHECK(fixture.project.unitAssignments[1].state == production::UnitQueueState::MarkerReview);
  const auto firstReview = fixture.project.reviews.front().reviewId;
  const auto firstMaterial = fixture.project.metadataRevisions.front().values;
  const auto afterFirst = production::encodeProductionProject(fixture.project);
  CHECK(!production::commitSampleCandidateReview(fixture.root / "workspace", fixture.project, initialPacket,
      "reviewer", "2026-09-09T10:04:00Z", production::SampleCandidateReviewDecision::Accept, iSelection));
  CHECK(production::encodeProductionProject(fixture.project) == afterFirst);
  const auto refreshed = production::prepareSampleCandidateReview(fixture.root / "workspace", fixture.project, initialPacket.manifest);
  CHECK(refreshed);
  CHECK(refreshed.value().reviewBasisSha256 == initialPacket.reviewBasisSha256);
  const auto second = production::commitSampleCandidateReview(fixture.root / "workspace", fixture.project, refreshed.value(),
      "reviewer", "2026-09-09T10:04:00Z", production::SampleCandidateReviewDecision::Accept, iSelection);
  CHECK(second);
  CHECK(second.value().candidate);
  CHECK(second.value().candidate->units.size() == 2U);
  CHECK(fixture.project.lastDurableGeneration == generation + 2U);
  CHECK(fixture.project.reviews.front().reviewId == firstReview);
  CHECK(fixture.project.metadataRevisions.front().values == firstMaterial);
  auto changedManifest = initialPacket.manifest;
  changedManifest.units[1].gainDb = -1.0F;
  CHECK(!production::resolveReviewedSampleCandidate(fixture.root / "workspace", fixture.project, changedManifest));
  const auto changed = production::prepareSampleCandidateReview(fixture.root / "workspace", fixture.project, changedManifest);
  CHECK(changed);
  const auto reaccepted = production::commitSampleCandidateReview(fixture.root / "workspace", fixture.project, changed.value(),
      "reviewer", "2026-09-09T10:05:00Z", production::SampleCandidateReviewDecision::Accept, iSelection);
  CHECK(reaccepted);
  CHECK(reaccepted.value().candidate);
  CHECK(fixture.project.metadataRevisions.front().values == firstMaterial);
  CHECK(fixture.project.reviews.front().reviewId == firstReview);
  CHECK(production::publishSampleCandidate(fixture.root / "workspace", fixture.project,
      *reaccepted.value().candidate, fixture.root / "two-unit-candidate"));
}

TEST_CASE("multi-unit sample review validates every selected owner before saving any decision") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture(false, "producer", false);
  addSecondReviewUnit(fixture, "reviewer");
  const auto before = production::encodeProductionProject(fixture.project);
  const auto rejected = production::commitSampleCandidateReview(fixture.root / "workspace", fixture.project, fixture.packet,
      "reviewer", "2026-09-09T10:03:00Z", production::SampleCandidateReviewDecision::Accept);
  CHECK(!rejected);
  CHECK(rejected.error().message.find("raw importer") != std::string::npos);
  CHECK(production::encodeProductionProject(fixture.project) == before);
  CHECK(fixture.project.reviews.empty());
  const std::vector<std::string> duplicate{"a-69", "a-69"}, missing{"no-such-unit"};
  for (const auto& selection : {duplicate, missing})
    CHECK(!production::commitSampleCandidateReview(fixture.root / "workspace", fixture.project, fixture.packet,
        "reviewer", "2026-09-09T10:03:00Z", production::SampleCandidateReviewDecision::Accept, selection));
  CHECK(production::encodeProductionProject(fixture.project) == before);
  CHECK(production::ProductionProjectRepository{fixture.root / "workspace"}.verify(fixture.project));
}

TEST_CASE("available sample units can be reviewed while missing assignments cannot publish") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture(false, "producer", false);
  fixture.project.unitAssignments.push_back({.coverageKey = "sustain:i", .pitchLayer = 69,
      .promptId = "prompt-i", .plannedTakeId = "take-i"});
  CHECK(production::ProductionProjectRepository{fixture.root / "workspace"}.save(fixture.project,
      {.action = "save", .subjectId = fixture.project.projectId, .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:02:00Z"}));
  const auto partial = production::prepareSampleCandidateReview(fixture.root / "workspace", fixture.project, fixture.request.manifest);
  CHECK(partial);
  const auto reviewed = production::commitSampleCandidateReview(fixture.root / "workspace", fixture.project, partial.value(),
      "reviewer", "2026-09-09T10:03:00Z", production::SampleCandidateReviewDecision::Accept);
  CHECK(reviewed);
  CHECK(!reviewed.value().candidate);
  CHECK(fixture.project.unitAssignments[0].state == production::UnitQueueState::Approved);
  CHECK(fixture.project.unitAssignments[1].state == production::UnitQueueState::Missing);
  CHECK(!production::resolveReviewedSampleCandidate(fixture.root / "workspace", fixture.project, fixture.request.manifest));
}

TEST_CASE("concurrent sample review decisions cannot both advance one captured producer generation") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture(false, "producer", false);
  auto left = fixture.project, right = fixture.project;
  std::barrier start{2};
  bool leftSaved = false, rightSaved = false;
  std::jthread first([&] {
    start.arrive_and_wait();
    leftSaved = static_cast<bool>(production::commitSampleCandidateReview(fixture.root / "workspace", left, fixture.packet,
        "reviewer", "2026-09-09T10:03:00Z", production::SampleCandidateReviewDecision::Accept));
  });
  std::jthread second([&] {
    start.arrive_and_wait();
    rightSaved = static_cast<bool>(production::commitSampleCandidateReview(fixture.root / "workspace", right, fixture.packet,
        "reviewer", "2026-09-09T10:04:00Z", production::SampleCandidateReviewDecision::Reject));
  });
  first.join(); second.join();
  CHECK(leftSaved != rightSaved);
  const auto recovered = production::ProductionProjectRepository{fixture.root / "workspace"}.recover();
  CHECK(recovered);
  CHECK(recovered.value().lastDurableGeneration == fixture.project.lastDurableGeneration + 1U);
  CHECK(recovered.value().reviews.size() == 1U);
  CHECK(recovered.value().metadataRevisions.size() == 1U);
}

TEST_CASE("reviewed sample candidate publishes exact effective audio markers pitch and source identity") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture();
  const auto before = production::encodeProductionProject(fixture.project);
  const auto published = production::publishSampleCandidate(fixture.root / "workspace", fixture.project,
      fixture.request, fixture.root / "candidate");
  CHECK(published);
  CHECK(!published.value().releaseEligible);
  CHECK(published.value().durabilityConfirmed);
  CHECK(published.value().sourceGeneration == fixture.project.lastDurableGeneration);
  const auto reopened = seam::voicebank::ManifestJsonCodec{}.load(published.value().root / "manifest.json");
  CHECK(reopened);
  CHECK(reopened.value() == fixture.request.manifest);
  const auto& unit = reopened.value().units.front();
  CHECK(seam::core::sha256File(published.value().root / unit.audioPath).value() == fixture.request.units.front().audioSha256);
  CHECK(unit.markers == fixture.request.manifest.units.front().markers);
  CHECK(unit.pitchMarks == fixture.request.manifest.units.front().pitchMarks);
  CHECK(seam::voicebank::computeVoicebankContentHash(reopened.value(), published.value().root).value() == published.value().contentSha256);
  CHECK(seam::core::sha256File(published.value().root / "manifest.json").value() == published.value().manifestSha256);
  CHECK(seam::core::sha256File(published.value().root / "candidate.json").value() == published.value().candidateSha256);
  const auto descriptor = seam::formats::parseJson(seam::core::readTextFileLimited(published.value().root / "candidate.json", 1024U * 1024U).value());
  CHECK(descriptor);
  const auto* history = descriptor.value().find("originHistory");
  CHECK(history && history->isArray());
  CHECK(history->asArray().size() == 2U);
  for (const auto& entry : history->asArray()) {
    const auto generationFile = published.value().root / entry.find("generationPath")->asString();
    const auto journalFile = published.value().root / entry.find("journalPath")->asString();
    CHECK(seam::core::sha256File(generationFile).value() == entry.find("generationSha256")->asString());
    CHECK(seam::core::sha256File(journalFile).value() == entry.find("journalSha256")->asString());
    const auto journal = seam::formats::parseJson(seam::core::readTextFileLimited(journalFile, 1024U * 1024U).value());
    CHECK(journal);
    CHECK(journal.value().find("projectSha256")->asString() == entry.find("generationSha256")->asString());
  }
  CHECK(seam::core::readTextFileLimited(published.value().root / "provenance/production.json", 1024U * 1024U).value() == before);
  CHECK(production::encodeProductionProject(fixture.project) == before);
  CHECK(production::ProductionProjectRepository{fixture.root / "workspace"}.verify(fixture.project));

  seam::application::ProjectFactory factory{98000U};
  auto song = factory.createProject("Published candidate consumer");
  const auto trackId = factory.addVocalTrack(song, "Candidate voice");
  const auto regionId = factory.addRegion(song, trackId, "Audible phrase", seam::time::Tick{0}, seam::time::Tick{1920});
  auto [lyric, note] = factory.makeNote(seam::time::Tick{0}, seam::time::Tick{960}, 69U,
      U"あ", seam::domain::Language::Japanese);
  auto* region = song.findRegion(regionId);
  CHECK(region);
  region->lyrics.push_back(std::move(lyric));
  region->notes.push_back(std::move(note));
  auto* track = song.findVocalTrack(trackId);
  CHECK(track);
  track->voicebank = {.id = reopened.value().id, .version = reopened.value().version,
      .contentHash = published.value().contentSha256};
  track->styleSelection = {.origin = seam::domain::VoiceStyleOrigin::Explicit, .styleId = "original"};
  CHECK(song.validate());
  const auto songPath = fixture.root / "consumer-song.seam";
  CHECK(seam::formats::ProjectJsonCodec{}.save(song, songPath));

  // Remove access to producer inputs before either rendering or warming a
  // cache. Only the independently published bank can satisfy runtime loading.
  std::filesystem::rename(fixture.root / "workspace", fixture.root / "producer-unavailable");
  std::filesystem::rename(fixture.root / "raw.wav", fixture.root / "raw-unavailable.wav");
  CHECK(!std::filesystem::exists(fixture.root / "workspace"));
  const auto consumerManifest = seam::voicebank::ManifestJsonCodec{}.load(published.value().root / "manifest.json");
  CHECK(consumerManifest);
  const auto reopenedSong = seam::formats::ProjectJsonCodec{}.load(songPath);
  CHECK(reopenedSong);
  CHECK(reopenedSong.value().findVocalTrack(trackId)->voicebank == track->voicebank);
  seam::rendering::PcmCache cache{fixture.root / "consumer-cache"};
  seam::rendering::ProductionRegionRenderer renderer;
  const auto render = [&] {
    return renderer.render(reopenedSong.value(), consumerManifest.value(), published.value().root,
        trackId, regionId, 1U, 48000U, seam::rendering::RenderQuality::Final, {}, {}, &cache);
  };
  const auto audio = render();
  if (!audio) throw std::runtime_error(audio.error().message + ": " + audio.error().context);
  CHECK(audio.value().failures.empty());
  CHECK(audio.value().phrases.size() == 1U);
  CHECK(audio.value().unitCount == 1U);
  CHECK(audio.value().fallbackCount == 0U);
  CHECK(audio.value().phrases.front().rendererIdentity == "classic-psola");
  CHECK(audio.value().cacheHits == 0U);
  CHECK(!audio.value().mono.empty());
  CHECK(std::all_of(audio.value().mono.begin(), audio.value().mono.end(), [](float sample) { return std::isfinite(sample); }));
  CHECK(seam::voicebank::analyzeAudio(audio.value().mono).rms > 1e-4);
  cache.clearMemory();
  const auto cached = render();
  CHECK(cached);
  CHECK(cached.value().cacheHits == 1U);
  CHECK(cache.stats().diskHits == 1U);
  CHECK(cached.value().mono == audio.value().mono);
  CHECK(cached.value().phrases.front().contentHash == audio.value().phrases.front().contentHash);
  CHECK(cached.value().phrases.front().rendererIdentity == audio.value().phrases.front().rendererIdentity);
  CHECK(cached.value().fallbackCount == 0U);
}

TEST_CASE("sample candidate rejects changed review material and incomplete candidate coverage") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture();
  const auto reject = [&](production::SampleCandidateRequest request) {
    CHECK(!production::publishSampleCandidate(fixture.root / "workspace", fixture.project, request, fixture.root / "rejected"));
    CHECK(!std::filesystem::exists(fixture.root / "rejected"));
  };
  auto changed = fixture.request;
  ++changed.expectedGeneration;
  reject(changed);
  changed = fixture.request;
  changed.expectedProjectSha256 = std::string(64U, 'b');
  reject(changed);
  changed = fixture.request;
  changed.manifest.units.front().markers.stableStart += 1;
  reject(changed);
  changed = fixture.request;
  changed.manifest.units.front().pitchMarks.front().confidence = 0.5F;
  reject(changed);
  changed = fixture.request;
  changed.units.front().audioSha256 = fixture.project.takes.front().rawAssetSha256;
  reject(changed);
  changed = fixture.request;
  changed.units.front().reviewMetadataRevisionId = "missing-review";
  reject(changed);
  changed = fixture.request;
  changed.units.clear();
  reject(changed);
  changed = fixture.request;
  changed.manifest.styles.push_back("soft");
  reject(changed);
}

TEST_CASE("sample candidate rejects unapproved stale and altered producer assets") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture();
  production::ProductionProjectRepository repository{fixture.root / "workspace"};
  const auto original = fixture.project;
  fixture.project.takes.front().state = production::UnitQueueState::MarkerReview;
  fixture.project.unitAssignments.front().state = production::UnitQueueState::MarkerReview;
  fixture.project.unitAssignments.front().markerReviewed = false;
  CHECK(repository.save(fixture.project, {.action = "save", .subjectId = "take-a", .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:04:00Z"}));
  CHECK(!production::publishSampleCandidate(fixture.root / "workspace", original, fixture.request, fixture.root / "stale"));
  fixture.request.expectedGeneration = fixture.project.lastDurableGeneration;
  fixture.request.expectedProjectSha256 = seam::core::sha256Hex(production::encodeProductionProject(fixture.project));
  CHECK(!production::publishSampleCandidate(fixture.root / "workspace", fixture.project, fixture.request, fixture.root / "unapproved"));
  CHECK(!std::filesystem::exists(fixture.root / "unapproved"));

  auto missing = reviewedCandidateFixture();
  production::ProductionProjectRepository missingRepository{missing.root / "workspace"};
  const auto& asset = missing.project.assets.back();
  CHECK(std::filesystem::remove(missingRepository.assetPath(asset)));
  CHECK(!production::publishSampleCandidate(missing.root / "workspace", missing.project, missing.request, missing.root / "missing"));
  CHECK(!std::filesystem::exists(missing.root / "missing"));
}

TEST_CASE("sample candidate preserves occupied destinations and cleans failed staging") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture();
  const auto destination = fixture.root / "candidate";
  CHECK(std::filesystem::create_directory(destination));
  CHECK(seam::core::durableAtomicWriteText(destination / "existing.txt", "preserve-existing-candidate"));
  CHECK(!production::publishSampleCandidate(fixture.root / "workspace", fixture.project, fixture.request, destination));
  CHECK(seam::core::readTextFileLimited(destination / "existing.txt", 1024U).value() == "preserve-existing-candidate");
  CHECK(!production::publishSampleCandidate(fixture.root / "workspace", fixture.project, fixture.request, fixture.root / "workspace" / "candidate"));
  CHECK(!production::publishSampleCandidate(fixture.root / "workspace", fixture.project, fixture.request, fixture.root / ".." / "unsafe"));
  std::stop_source cancelled;
  cancelled.request_stop();
  CHECK(!production::publishSampleCandidate(fixture.root / "workspace", fixture.project, fixture.request,
      fixture.root / "cancelled", {}, cancelled.get_token()));
  for (const auto failingStage : {production::CandidatePublicationStage::AudioStaged, production::CandidatePublicationStage::BeforeCommit}) {
    CHECK(!production::publishSampleCandidate(fixture.root / "workspace", fixture.project, fixture.request, fixture.root / "failed",
        {.maximumAudioBytes = 256ULL * 1024ULL * 1024ULL,
         .faultInjector = [failingStage](production::CandidatePublicationStage stage) {
           return stage == failingStage ? seam::core::failure(seam::core::ErrorCode::IoError, "Injected storage exhaustion") : seam::core::success();
         }}));
    CHECK(!std::filesystem::exists(fixture.root / "failed"));
  }
  CHECK(!production::publishSampleCandidate(fixture.root / "workspace", fixture.project, fixture.request,
      fixture.root / "oversized", {.maximumAudioBytes = 16U, .faultInjector = {}}));
  for (const auto& entry : std::filesystem::directory_iterator(fixture.root)) {
    const auto filename = entry.path().filename().string();
    CHECK(filename == ".seam-candidate-writer.lock" || !filename.starts_with(".seam-candidate-"));
  }
  std::error_code error;
  std::filesystem::create_directory_symlink(destination, fixture.root / "linked-parent", error);
  if (!error) {
    CHECK(!production::publishSampleCandidate(fixture.root / "workspace", fixture.project, fixture.request, fixture.root / "linked-parent" / "child"));
    CHECK(!std::filesystem::exists(destination / "child"));
  }
}

TEST_CASE("sample candidate cannot reuse approval after a later rejection or source edit") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture();
  production::ProductionProjectRepository repository{fixture.root / "workspace"};
  fixture.project.reviews.push_back({.reviewId = "rejection-a", .takeId = "take-a", .reviewerId = "reviewer",
      .result = "REJECTED", .reviewedAtUtc = "2026-09-09T10:04:00Z"});
  CHECK(repository.save(fixture.project, {.action = "review", .subjectId = "take-a", .operatorId = "reviewer",
      .occurredAtUtc = "2026-09-09T10:04:00Z"}));
  fixture.request.expectedGeneration = fixture.project.lastDurableGeneration;
  fixture.request.expectedProjectSha256 = seam::core::sha256Hex(production::encodeProductionProject(fixture.project));
  CHECK(!production::publishSampleCandidate(fixture.root / "workspace", fixture.project, fixture.request, fixture.root / "rejected"));

  auto edited = reviewedCandidateFixture();
  production::ProductionProjectRepository editedRepository{edited.root / "workspace"};
  CHECK(editedRepository.recordMetadataRevision(edited.project,
      {.revisionId = "later-annotation", .takeId = "take-a", .rawAssetSha256 = edited.project.takes.front().rawAssetSha256,
       .kind = "source-annotation", .values = {{"vowelOnset", "120"}}, .operatorId = "producer",
       .performedAtUtc = "2026-09-09T10:04:00Z"},
      {.action = "marker", .subjectId = "later-annotation", .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:04:00Z"}));
  edited.request.expectedGeneration = edited.project.lastDurableGeneration;
  edited.request.expectedProjectSha256 = seam::core::sha256Hex(production::encodeProductionProject(edited.project));
  CHECK(!production::publishSampleCandidate(edited.root / "workspace", edited.project, edited.request, edited.root / "stale-review"));
  CHECK(!std::filesystem::exists(edited.root / "stale-review"));
}

TEST_CASE("sample candidate locks the generation through its final publish") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture();
  bool writerRejected = false;
  const auto published = production::publishSampleCandidate(fixture.root / "workspace", fixture.project, fixture.request,
      fixture.root / "candidate", {.maximumAudioBytes = 256ULL * 1024ULL * 1024ULL,
      .faultInjector = [&](production::CandidatePublicationStage stage) {
        if (stage == production::CandidatePublicationStage::BeforeCommit) {
          auto concurrent = fixture.project;
          concurrent.operators.push_back({.operatorId = "other", .role = "PRODUCER"});
          writerRejected = !production::ProductionProjectRepository{fixture.root / "workspace"}.save(concurrent,
              {.action = "save", .subjectId = "candidate-test", .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:04:00Z"});
        }
        return seam::core::success();
      }});
  CHECK(published);
  CHECK(writerRejected);
}

TEST_CASE("sample candidate manifest must fit the installed decoder before publication") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture();
  fixture.request.manifest.displayName = std::string(2U * 1024U * 1024U + 1U, 'x');
  const auto encoded = seam::voicebank::ManifestJsonCodec{}.encode(fixture.request.manifest);
  CHECK(encoded);
  CHECK(!seam::voicebank::ManifestJsonCodec{}.decode(encoded.value()));
  CHECK(!production::sampleCandidateReviewValues(fixture.project, fixture.request.manifest, fixture.request.units.front()));
  const auto published = production::publishSampleCandidate(fixture.root / "workspace", fixture.project,
      fixture.request, fixture.root / "oversized-manifest");
  CHECK(!published);
  CHECK(published.error().code == seam::core::ErrorCode::ParseError);
  CHECK(!std::filesystem::exists(fixture.root / "oversized-manifest"));
}

TEST_CASE("sample candidate reopens staged manifest before the atomic commit") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture();
  bool altered = false;
  const auto published = production::publishSampleCandidate(fixture.root / "workspace", fixture.project, fixture.request,
      fixture.root / "candidate", {.maximumAudioBytes = 256ULL * 1024ULL * 1024ULL,
      .faultInjector = [&](production::CandidatePublicationStage stage) {
        if (stage == production::CandidatePublicationStage::BeforeCommit) {
          for (const auto& entry : std::filesystem::directory_iterator(fixture.root)) {
            if (entry.is_directory() && entry.path().filename().string().starts_with(".seam-candidate-")) {
              const auto written = seam::core::durableAtomicWriteText(entry.path() / "manifest.json", "{}");
              if (!written) return written;
              altered = true;
            }
          }
        }
        return seam::core::success();
      }});
  CHECK(altered);
  CHECK(!published);
  CHECK(published.error().message.find("does not reopen") != std::string::npos);
  CHECK(!std::filesystem::exists(fixture.root / "candidate"));
}

TEST_CASE("sample candidate rejects raw importer self review and unavailable original attribution") {
  namespace production = seam::voicebank_production;
  auto selfReviewed = reviewedCandidateFixture(false, "reviewer", false);
  CHECK(selfReviewed.project.derivedRevisions.empty());
  const auto rejected = production::commitSampleCandidateReview(selfReviewed.root / "workspace", selfReviewed.project,
      selfReviewed.packet, "reviewer", "2026-09-09T10:03:00Z", production::SampleCandidateReviewDecision::Accept);
  CHECK(!rejected);
  CHECK(rejected.error().message.find("raw importer") != std::string::npos);
  CHECK(!std::filesystem::exists(selfReviewed.root / "self-reviewed"));

  auto independent = reviewedCandidateFixture(false);
  CHECK(production::publishSampleCandidate(independent.root / "workspace", independent.project,
      independent.request, independent.root / "independent"));
  CHECK(std::filesystem::remove(independent.root / "workspace/generations/00000000000000000001.json"));
  CHECK(!production::publishSampleCandidate(independent.root / "workspace", independent.project,
      independent.request, independent.root / "missing-attribution"));
  CHECK(!std::filesystem::exists(independent.root / "missing-attribution"));
}

TEST_CASE("sample candidate reports committed state when parent durability cannot be confirmed") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture();
  const auto published = production::publishSampleCandidate(fixture.root / "workspace", fixture.project, fixture.request,
      fixture.root / "candidate", {.maximumAudioBytes = 256ULL * 1024ULL * 1024ULL,
      .faultInjector = [](production::CandidatePublicationStage stage) {
        return stage == production::CandidatePublicationStage::AfterCommitBeforeParentSync
            ? seam::core::failure(seam::core::ErrorCode::IoError, "Injected parent sync failure") : seam::core::success();
      }});
  CHECK(published);
  CHECK(!published.value().durabilityConfirmed);
  CHECK(published.value().diagnostic.find("committed") != std::string::npos);
  CHECK(published.value().diagnostic.find("before retrying") != std::string::npos);
  CHECK(seam::core::sha256File(published.value().root / "candidate.json").value() == published.value().candidateSha256);
  CHECK(seam::core::sha256File(published.value().root / "manifest.json").value() == published.value().manifestSha256);
  CHECK(seam::voicebank::computeVoicebankContentHash(fixture.request.manifest, published.value().root).value() == published.value().contentSha256);
  CHECK(!production::publishSampleCandidate(fixture.root / "workspace", fixture.project, fixture.request, fixture.root / "candidate"));
  CHECK(seam::core::sha256File(published.value().root / "candidate.json").value() == published.value().candidateSha256);
}

TEST_CASE("sample candidate cannot reattribute an import across a missing intermediate generation") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture(false);
  const auto generation = fixture.root / "workspace/generations/00000000000000000003.json";
  const auto generationBytes = seam::core::readTextFileLimited(generation, 1024U * 1024U);
  CHECK(generationBytes);
  // A surviving batch snapshot also contains pre-existing takes. Its actor is
  // not evidence that they originally imported every take in that snapshot.
  const auto batchJournal = seam::formats::stringifyJson(seam::formats::JsonValue{seam::formats::JsonValue::Object{
      {"format", "com.project-seam.voicebank-production-journal-event"},
      {"schemaVersion", std::int64_t{1}}, {"generation", std::int64_t{3}},
      {"projectSha256", seam::core::sha256Hex(generationBytes.value())},
      {"action", "import-generated-batch"}, {"subjectId", "later-batch"},
      {"operatorId", "producer"}, {"occurredAtUtc", "2026-09-09T10:03:00Z"}}}, true) + "\n";
  CHECK(seam::core::durableAtomicWriteText(fixture.root / "workspace/journal/00000000000000000003.json", batchJournal));
  CHECK(std::filesystem::remove(fixture.root / "workspace/generations/00000000000000000002.json"));
  CHECK(std::filesystem::remove(fixture.root / "workspace/journal/00000000000000000002.json"));
  CHECK(production::ProductionProjectRepository{fixture.root / "workspace"}.verify(fixture.project));
  const auto rejected = production::publishSampleCandidate(fixture.root / "workspace", fixture.project,
      fixture.request, fixture.root / "misattributed");
  CHECK(!rejected);
  CHECK(rejected.error().message.find("contiguous") != std::string::npos);
  CHECK(rejected.error().context.find("Missing generation 2") != std::string::npos);
  CHECK(!std::filesystem::exists(fixture.root / "misattributed"));
}

namespace {
CandidateFixture recoveredOriginFixture(bool completeJournal = false) {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture(false, "producer", false);
  production::ProductionProjectRepository repository{fixture.root / "workspace"};
  CHECK(fixture.project.lastDurableGeneration == 2U);
  const auto journalPath = fixture.root / "workspace/journal/00000000000000000003.json";
  std::string interrupted = "interrupted";
  if (completeJournal) {
    auto intended = fixture.project;
    intended.lastDurableGeneration = 3U;
    interrupted = seam::formats::stringifyJson(seam::formats::JsonValue{seam::formats::JsonValue::Object{
        {"format", "com.project-seam.voicebank-production-journal-event"}, {"schemaVersion", std::int64_t{1}},
        {"generation", std::int64_t{3}}, {"projectSha256", seam::core::sha256Hex(production::encodeProductionProject(intended))},
        {"action", "save"}, {"subjectId", intended.projectId}, {"operatorId", "producer"},
        {"occurredAtUtc", "2026-09-09T10:02:00Z"},
        {"ancestry", seam::formats::JsonValue::Object{{"format", "parent-and-aborted-journals-v1"},
            {"parentGeneration", std::int64_t{2}},
            {"parentProjectSha256", seam::core::sha256File(fixture.root / "workspace/generations/00000000000000000002.json").value()},
            {"parentJournalSha256", seam::core::sha256File(fixture.root / "workspace/journal/00000000000000000002.json").value()},
            {"abortedGenerations", seam::formats::JsonValue::Array{}}}}}}, true) + "\n";
  }
  CHECK(seam::core::durableAtomicWriteTextNew(journalPath, interrupted));
  CHECK(repository.recover().value().lastDurableGeneration == 2U);
  CHECK(repository.save(fixture.project, {.action = "save", .subjectId = fixture.project.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:03:00Z"}));
  CHECK(fixture.project.lastDurableGeneration == 4U);
  CHECK(repository.verify(fixture.project));
  CHECK(seam::core::readTextFileLimited(journalPath, 1024U * 1024U).value() == interrupted);
  CHECK(!std::filesystem::exists(fixture.root / "workspace/generations/00000000000000000003.json"));
  addSecondReviewUnit(fixture);
  CHECK(fixture.project.lastDurableGeneration == 5U);
  CHECK(repository.verify(fixture.project));
  CHECK(fixture.packet.units.size() == 2U);
  CHECK(fixture.project.reviews.empty());
  return fixture;
}
}  // namespace

TEST_CASE("certified aborted journal recovery preserves source origins through explicit review and publication") {
  namespace production = seam::voicebank_production;
  for (const bool completeJournal : {false, true}) {
    auto fixture = recoveredOriginFixture(completeJournal);
    const auto accepted = production::commitSampleCandidateReview(fixture.root / "workspace", fixture.project,
        fixture.packet, "reviewer", "2026-09-09T10:04:00Z", production::SampleCandidateReviewDecision::Accept);
    CHECK(accepted); CHECK(accepted.value().candidate);
    CHECK(accepted.value().reviews.size() == 2U);
    const auto published = production::publishSampleCandidate(fixture.root / "workspace", fixture.project,
        *accepted.value().candidate, fixture.root / "candidate");
    CHECK(published); CHECK(!published.value().releaseEligible);
    const auto descriptor = seam::formats::parseJson(seam::core::readTextFileLimited(
        published.value().root / "candidate.json", 1024U * 1024U).value());
    CHECK(descriptor);
    CHECK(descriptor.value().find("originHistory")->asArray().size() == 5U);
    const auto& aborted = descriptor.value().find("originHistory")->asArray()[2];
    CHECK(aborted.find("generation")->asInt64() == 3);
    CHECK(aborted.find("entryType")->asString() == "aborted-write");
    CHECK(!aborted.find("generationPath"));
    CHECK(seam::core::sha256File(published.value().root / aborted.find("journalPath")->asString()).value() ==
        seam::core::sha256File(fixture.root / "workspace/journal/00000000000000000003.json").value());
    CHECK(!std::filesystem::exists(published.value().root / "provenance/history/generations/00000000000000000003.json"));
    const auto& units = descriptor.value().find("unitBindings")->asArray();
    CHECK(units[0].find("originGeneration")->asInt64() == 2);
    CHECK(units[1].find("originGeneration")->asInt64() == 5);
    for (const auto& unit : units) CHECK(unit.find("originOperatorId")->asString() == "producer");
  }
}

TEST_CASE("aborted recovery evidence cannot hide deleted committed or changed interrupted history") {
  namespace production = seam::voicebank_production;
  for (unsigned scenario = 0U; scenario < 4U; ++scenario) {
    auto fixture = recoveredOriginFixture();
    const auto accepted = production::commitSampleCandidateReview(fixture.root / "workspace", fixture.project,
        fixture.packet, "reviewer", "2026-09-09T10:04:00Z", production::SampleCandidateReviewDecision::Accept);
    CHECK(accepted); CHECK(accepted.value().candidate);
    if (scenario == 0U) {
      CHECK(std::filesystem::remove(fixture.root / "workspace/journal/00000000000000000003.json"));
    } else if (scenario == 1U) {
      CHECK(seam::core::durableAtomicWriteText(fixture.root / "workspace/journal/00000000000000000003.json", "changed interrupted evidence"));
    } else if (scenario == 2U) {
      CHECK(std::filesystem::remove(fixture.root / "workspace/generations/00000000000000000002.json"));
      CHECK(std::filesystem::remove(fixture.root / "workspace/journal/00000000000000000002.json"));
    } else {
      const auto journalPath = fixture.root / "workspace/journal/00000000000000000004.json";
      auto journal = seam::formats::parseJson(seam::core::readTextFileLimited(journalPath, 1024U * 1024U).value());
      CHECK(journal);
      *journal.value().find("ancestry")->find("parentGeneration") = seam::formats::JsonValue{std::int64_t{1}};
      CHECK(seam::core::durableAtomicWriteText(journalPath, seam::formats::stringifyJson(journal.value())));
    }
    CHECK(!production::prepareSampleCandidateReview(fixture.root / "workspace", fixture.project, fixture.request.manifest));
    CHECK(!production::publishSampleCandidate(fixture.root / "workspace", fixture.project,
        *accepted.value().candidate, fixture.root / "tampered-history"));
    CHECK(!std::filesystem::exists(fixture.root / "tampered-history"));
  }
}

TEST_CASE("recovery never certifies a missing snapshot named by the durable published pointer") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture(false, "producer", false);
  CHECK(std::filesystem::remove(fixture.root / "workspace/generations/00000000000000000002.json"));
  production::ProductionProjectRepository repository{fixture.root / "workspace"};
  auto recovered = repository.recover(); CHECK(recovered);
  CHECK(recovered.value().lastDurableGeneration == 1U);
  const auto saved = repository.save(recovered.value(), {.action = "save", .subjectId = fixture.project.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:03:00Z"});
  CHECK(!saved);
  CHECK(saved.error().message.find("missing published generation") != std::string::npos);
  CHECK(recovered.value().lastDurableGeneration == 1U);
  CHECK(!std::filesystem::exists(fixture.root / "workspace/generations/00000000000000000003.json"));
}

TEST_CASE("new parent journal links preserve complete legacy generation and journal bytes") {
  namespace production = seam::voicebank_production;
  auto fixture = reviewedCandidateFixture(false, "producer", false);
  std::map<std::filesystem::path, std::string> originalBytes;
  for (const auto* number : {"00000000000000000001.json", "00000000000000000002.json"}) {
    const auto journalPath = fixture.root / "workspace/journal" / number;
    auto journal = seam::formats::parseJson(seam::core::readTextFileLimited(journalPath, 1024U * 1024U).value()); CHECK(journal);
    journal.value().asObject().erase("ancestry");
    CHECK(seam::core::durableAtomicWriteText(journalPath, seam::formats::stringifyJson(journal.value(), true) + "\n"));
    originalBytes.emplace(journalPath, seam::core::readTextFileLimited(journalPath, 1024U * 1024U).value());
    const auto generationPath = fixture.root / "workspace/generations" / number;
    originalBytes.emplace(generationPath, seam::core::readTextFileLimited(generationPath, 1024U * 1024U).value());
  }
  production::ProductionProjectRepository repository{fixture.root / "workspace"};
  CHECK(repository.verify(fixture.project));
  CHECK(repository.save(fixture.project, {.action = "save", .subjectId = fixture.project.projectId,
      .operatorId = "producer", .occurredAtUtc = "2026-09-09T10:03:00Z"}));
  for (const auto& [path, bytes] : originalBytes) CHECK(seam::core::readTextFileLimited(path, 1024U * 1024U).value() == bytes);
  CHECK(production::prepareSampleCandidateReview(fixture.root / "workspace", fixture.project, fixture.request.manifest));
}

TEST_CASE("production writers reject concurrent and stale durable bases") {
  namespace production = seam::voicebank_production;
  const auto root = seam::test::support::temporaryDirectory("production-writers");
  const auto license = root / "license.txt";
  CHECK(seam::core::durableAtomicWriteText(license, "SYNTHETIC_TEST_ONLY"));
  const auto digest = seam::core::sha256File(license);
  CHECK(digest);
  production::VoicebankProductionProject project{
      .projectId = "writer-test", .inventoryId = "test-inventory",
      .inventorySha256 = std::string(64U, 'a'),
      .selectedSourceStrategyId = "test-strategy",
      .licenseLocator = license.string(), .licenseSha256 = digest.value(),
      .immutableAssetRoot = "assets",
  };
  project.sourceStrategies.push_back({
      .id = "test-strategy", .kind = production::SourceStrategyKind::HumanRecording,
      .rights = production::Feasibility::Pass,
      .coverage = production::Feasibility::Pass,
      .listening = production::Feasibility::Pass,
      .permissions = {.sourceUse = true, .transformation = true,
                     .singingBankRedistribution = true, .commercialRenders = true},
      .licenseLocator = license.string(), .licenseSha256 = digest.value(),
      .evidenceState = "SYNTHETIC_TEST_ONLY",
  });
  project.operators = {{.operatorId = "producer", .role = "PRODUCER"}};
  production::ProductionProjectRepository first{root / "workspace"};
  production::ProductionProjectRepository second{root / "workspace"};
  const production::ProductionJournalEvent create{
      .action = "create", .subjectId = project.projectId, .operatorId = "producer",
      .occurredAtUtc = "2026-09-06T10:00:00Z"};
  CHECK(first.initialize(project, create));
  auto left = project;
  auto right = project;
  left.operators.push_back({.operatorId = "left", .role = "PRODUCER"});
  right.operators.push_back({.operatorId = "right", .role = "PRODUCER"});
  auto save = create;
  save.action = "save";
  std::barrier start{2};
  bool leftSaved = false;
  bool rightSaved = false;
  std::jthread leftWriter([&] {
    start.arrive_and_wait();
    leftSaved = static_cast<bool>(first.save(left, save));
  });
  std::jthread rightWriter([&] {
    start.arrive_and_wait();
    rightSaved = static_cast<bool>(second.save(right, save));
  });
  leftWriter.join();
  rightWriter.join();
  CHECK(leftSaved != rightSaved);
  auto recovered = first.recover();
  CHECK(recovered);
  CHECK(recovered.value().lastDurableGeneration == 2U);
  CHECK(recovered.value().operators.back().operatorId == (leftSaved ? "left" : "right"));
  const auto stale = second.save(project, save);
  CHECK(!stale);
  CHECK(stale.error().code == seam::core::ErrorCode::Conflict);
  CHECK(project.lastDurableGeneration == 1U);
  CHECK(first.recover().value().lastDurableGeneration == 2U);
  CHECK(second.save(recovered.value(), save));
  CHECK(recovered.value().lastDurableGeneration == 3U);
  CHECK(first.verify(recovered.value()));

  std::stop_source cancelledSave;
  cancelledSave.request_stop();
  CHECK(!first.save(recovered.value(), save, cancelledSave.get_token()));
  CHECK(recovered.value().lastDurableGeneration == 3U);
  CHECK(first.recover().value().lastDurableGeneration == 3U);

  // A torn future journal must not prevent saving from the latest complete base,
  // and its occupied generation must never be reused.
  CHECK(seam::core::durableAtomicWriteText(
      root / "workspace" / "journal" / "00000000000000000004.json", "interrupted"));
  CHECK(first.save(recovered.value(), save));
  CHECK(recovered.value().lastDurableGeneration == 5U);
  CHECK(first.verify(recovered.value()));

  const auto lockPath = root / "workspace" / ".writer.lock";
  std::filesystem::rename(lockPath, root / "workspace" / "previous-writer.lock");
  std::error_code symlinkError;
  std::filesystem::create_symlink(license, lockPath, symlinkError);
  if (!symlinkError) {
    CHECK(!first.save(recovered.value(), save));
    CHECK(recovered.value().lastDurableGeneration == 5U);
    CHECK(seam::core::sha256File(license).value() == digest.value());
    CHECK(first.recover().value().lastDurableGeneration == 5U);
  }
}

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

  auto escapedProject = project;
  escapedProject.assets.front().relativePath = "../../source.wav";
  CHECK(!repository.stageOperation(
      escapedProject, "take-001", "",
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
  const auto staged = repository.stageOperation(project, "take-001", "", operation, "stage-001");
  const auto repeated = repository.stageOperation(project, "take-001", "", operation, "stage-002");
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
      resumed, staged.value(), "revision-001", "operator-a", "2026-08-31T10:02:00Z", "take-001", "");
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
      resumed, "take-002", "",
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
