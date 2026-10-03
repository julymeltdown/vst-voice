// Review status in a producer comes only from an independent review decision on current material.
//
// The durable append path enforces this for every writer: history is append-only, a take is marked
// reviewed or approved only by a "review" generation that records an independent decision together
// with the material it was made on, and an approval whose bound material later changes is lowered
// to marker review. Every identity here is a synthetic fixture: the audio is generated sine tones,
// and no listening, singer qualification or release claim is made.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "../libs/seam-voicebank-production/src/candidate_publication_internal.hpp"
#include "../libs/seam-voicebank-production/src/review_transition_internal.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/candidate_publication.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "seam/voicebank_production/take_inspection_receipt.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <tuple>
#include <utility>

namespace {
namespace production = seam::voicebank_production;

struct ReviewFixture final {
  std::filesystem::path root{seam::test::support::temporaryDirectory("production-review-transition")};
  production::ProductionProjectRepository repository{root / "workspace"};
  production::VoicebankProductionProject project;

  // Two units, each with its own generated take. The second take can be imported by a registered
  // reviewer, who then may not review it.
  explicit ReviewFixture(std::string secondImporter = "producer") {
    const auto license = root / "synthetic-license.txt";
    CHECK(seam::core::durableAtomicWriteTextNew(license, "SYNTHETIC TEST ONLY; no production qualification"));
    const auto licenseHash = seam::core::sha256File(license); CHECK(licenseHash);
    project = {.projectId = "review-transition", .inventoryId = "fixture", .inventorySha256 = std::string(64U, 'a'),
               .selectedSourceStrategyId = "synthetic", .licenseLocator = license.string(),
               .licenseSha256 = licenseHash.value()};
    project.sourceStrategies = {{.id = "synthetic", .kind = production::SourceStrategyKind::ProceduralSynthesis,
        .rights = production::Feasibility::Pass, .coverage = production::Feasibility::Pass,
        .listening = production::Feasibility::Pass, .permissions = {true, true, true, true},
        .licenseLocator = license.string(), .licenseSha256 = licenseHash.value(), .evidenceState = "SYNTHETIC_TEST_ONLY"}};
    project.operators = {{.operatorId = "producer", .role = "PRODUCER"}, {.operatorId = "reviewer", .role = "REVIEWER"},
                         {.operatorId = "second-reviewer", .role = "REVIEWER"}};
    project.unitAssignments = {
        {.coverageKey = "sustain:a", .pitchLayer = 69, .promptId = "a", .plannedTakeId = "take-a"},
        {.coverageKey = "sustain:i", .pitchLayer = 69, .promptId = "b", .plannedTakeId = "take-b"}};
    CHECK(repository.initialize(project, event("create", project.projectId)));
    const seam::voicebank::WavOutputFormat format{.sampleRate = 48000U, .channels = 1U,
        .sampleFormat = seam::voicebank::WavSampleFormat::Pcm24};
    CHECK(seam::voicebank::writeWav(root / "a.wav", format, seam::test::support::sineWave(48000U, 440.0, 0.12, 0.25F)));
    CHECK(seam::voicebank::writeWav(root / "b.wav", format, seam::test::support::sineWave(48000U, 660.0, 0.12, 0.25F)));
    CHECK(repository.importRaw(project, root / "a.wav",
        {.takeId = "take-a", .promptId = "a", .coverageKey = "sustain:a", .pitchLayer = 69}, event("import", "take-a")));
    CHECK(repository.importRaw(project, root / "b.wav",
        {.takeId = "take-b", .promptId = "b", .coverageKey = "sustain:i", .pitchLayer = 69},
        event("import", "take-b", std::move(secondImporter))));
  }

  static production::ProductionJournalEvent event(std::string action, std::string subject,
                                                   std::string operatorId = "producer",
                                                   std::string at = "2026-09-28T10:00:00Z") {
    return {.action = std::move(action), .subjectId = std::move(subject), .operatorId = std::move(operatorId),
            .occurredAtUtc = std::move(at)};
  }

  // Markers and known periods of the generated 0.12 s tones; not measured singer annotations.
  [[nodiscard]] seam::voicebank::Manifest manifest() const {
    seam::voicebank::Manifest value{.id = "review.transition", .version = "0.1.0", .displayName = "Review transition",
        .characterId = {}, .characterVersion = {}, .language = seam::domain::Language::Japanese,
        .expectedSampleRate = 48000U, .styles = {"original"}};
    for (const auto& [id, phone, frequency] : {std::tuple<std::string, std::string, double>{"a-69", "a", 440.0},
                                                {"i-69", "i", 660.0}}) {
      seam::voicebank::Unit unit{.id = id, .alias = phone, .phones = {phone}, .kind = seam::voicebank::UnitKind::Sustain,
          .audioPath = "audio/pending.wav", .rootMidi = 69, .style = "original", .take = 1, .priority = 0, .gainDb = 0.0F,
          .renderer = seam::voicebank::RendererHint::ClassicPsola,
          .markers = {.audioOffset = 0, .consonantEnd = 0, .vowelOnset = 0, .stableStart = 480,
                      .loopStart = 480, .loopEnd = 4800, .releaseStart = 5280, .audioEnd = 5760},
          .pitchMarks = {}, .enabled = true};
      for (std::int64_t period = 1; ; ++period) {
        const auto frame = static_cast<seam::time::SampleFrame>(std::llround(static_cast<double>(period) * 48000.0 / frequency));
        if (frame >= unit.markers.audioEnd) break;
        unit.pitchMarks.push_back({.frame = frame, .confidence = 1.0F, .locked = true});
      }
      value.units.push_back(std::move(unit));
    }
    return value;
  }

  [[nodiscard]] production::SampleCandidateReviewPacket packet() const {
    const auto prepared = production::prepareSampleCandidateReview(root / "workspace", project, manifest());
    if (!prepared) throw seam::test::Failure{"review packet: " + prepared.error().message};
    return prepared.value();
  }

  void decide(production::SampleCandidateReviewDecision decision, std::string at, std::vector<std::string> units = {}) {
    const auto decided = production::commitSampleCandidateReview(root / "workspace", project, packet(), "reviewer",
                                                                 std::move(at), decision, units);
    if (!decided) throw seam::test::Failure{"review decision: " + decided.error().message};
  }

  [[nodiscard]] std::uint64_t durableGeneration() const { return repository.recover().value().lastDurableGeneration; }

  [[nodiscard]] const production::UnitAssignment& unit(std::string_view takeId) const {
    return *std::find_if(project.unitAssignments.begin(), project.unitAssignments.end(),
                         [&](const auto& value) { return value.takeId == takeId; });
  }
};

// Exactly the records the review command writes for one unit, assembled by hand. The repository
// judges the evidence, not the caller, so such a decision stands or falls on its own content.
production::VoicebankProductionProject handDecided(const production::VoicebankProductionProject& project,
    const production::SampleCandidateReviewPacket& packet, std::string_view unitId, const std::string& reviewerId,
    const std::string& at) {
  auto next = project;
  const auto& unit = *std::find_if(packet.units.begin(), packet.units.end(),
                                   [&](const auto& value) { return value.unitId == unitId; });
  const production::SampleCandidateUnitBinding binding{unit.unitId, unit.takeId, unit.audioSha256,
      "review-by-" + reviewerId, "review-material-by-" + reviewerId};
  const auto values = production::sampleCandidateReviewValues(project, packet.manifest, binding);
  if (!values) throw seam::test::Failure{"review material: " + values.error().message};
  auto take = std::find_if(next.takes.begin(), next.takes.end(), [&](const auto& value) { return value.takeId == unit.takeId; });
  next.reviews.push_back({binding.reviewId, unit.takeId, reviewerId, "PASS", at});
  next.metadataRevisions.push_back({binding.reviewMetadataRevisionId, unit.takeId, take->rawAssetSha256,
      std::string{production::kSampleCandidateReviewKind}, values.value(), reviewerId, at});
  take->state = production::UnitQueueState::Approved;
  for (auto& assignment : next.unitAssignments) {
    if (assignment.takeId != unit.takeId) continue;
    assignment.state = production::UnitQueueState::Approved;
    assignment.markerReviewed = true;
    assignment.pitchReviewed = true;
  }
  return next;
}

void approveByHand(production::VoicebankProductionProject& project, std::string_view takeId) {
  for (auto& take : project.takes)
    if (take.takeId == takeId) take.state = production::UnitQueueState::Approved;
  for (auto& assignment : project.unitAssignments) {
    if (assignment.takeId != takeId) continue;
    assignment.state = production::UnitQueueState::Approved;
    assignment.markerReviewed = true;
    assignment.pitchReviewed = true;
  }
}

// Every regular file below root and its digest: a published bank's exact bytes.
std::map<std::string, std::string> fileDigests(const std::filesystem::path& root) {
  std::map<std::string, std::string> digests;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    if (entry.is_regular_file())
      digests[std::filesystem::relative(entry.path(), root).generic_string()] = seam::core::sha256File(entry.path()).value();
  return digests;
}

const production::TakeRecord& takeOf(const production::VoicebankProductionProject& project, std::string_view takeId) {
  return *std::find_if(project.takes.begin(), project.takes.end(), [&](const auto& value) { return value.takeId == takeId; });
}

std::string projectSha(const production::VoicebankProductionProject& project) {
  return seam::core::sha256Hex(production::encodeProductionProject(project));
}

}  // namespace

TEST_CASE("the indexed review basis is byte-identical to the basis review decisions record") {
  namespace internal = production::candidate_publication_internal;
  ReviewFixture fixture;
  fixture.decide(production::SampleCandidateReviewDecision::Accept, "2026-09-28T10:01:00Z");
  const auto staged = fixture.repository.stageOperation(fixture.project, "take-b", "",
      {.kind = production::OperationKind::NormalizeGain, .targetPeak = 0.15F}, "b-normalized");
  CHECK(staged);
  CHECK(fixture.repository.commitStaged(fixture.project, staged.value(), "b-revision", "producer",
                                        "2026-09-28T10:02:00Z", "take-b", ""));
  CHECK(fixture.repository.recordMetadataRevision(fixture.project,
      {.revisionId = "a-markers", .takeId = "take-a", .rawAssetSha256 = fixture.project.takes.front().rawAssetSha256,
       .kind = "MARKERS_AND_PITCH", .values = {{"vowelOnset", "120"}}, .operatorId = "producer",
       .performedAtUtc = "2026-09-28T10:03:00Z"},
      ReviewFixture::event("marker", "a-markers", "producer", "2026-09-28T10:03:00Z")));
  // Variants reach every branch of the basis: bound takes with one or several source policies,
  // unbound legacy takes, a schema-1 lifecycle, style ownership and an unknown take.
  auto multiSource = fixture.project;
  multiSource.sourceStrategies.push_back(multiSource.sourceStrategies.front());
  multiSource.sourceStrategies.back().id = "other";
  multiSource.sourceQualityAssessments = {
      {"quality-1", "synthetic", std::string(64U, '1'), std::string(64U, '2'), std::string(64U, '3'), "reviewer",
       "2026-09-28T10:04:00Z", production::Feasibility::Pass, production::Feasibility::Pass},
      {"quality-2", "other", std::string(64U, '4'), std::string(64U, '5'), std::string(64U, '6'), "reviewer",
       "2026-09-28T10:05:00Z", production::Feasibility::Pass, production::Feasibility::NotAssessed}};
  auto unbound = multiSource;
  unbound.sourceBindings.clear();
  for (auto& take : unbound.takes) take.sourceBindingId.clear();
  auto schemaOne = unbound;
  schemaOne.schemaVersion = 1;
  schemaOne.lifecycle = production::ProductionLifecycle::LegacyUnclassified;
  auto styleOwned = fixture.project;
  styleOwned.schemaVersion = production::kProductionStyleSchemaVersion;
  styleOwned.language = "ja";
  styleOwned.declaredPitchLayers = {69};
  for (auto& take : styleOwned.takes) take.style = "neutral";
  for (auto& assignment : styleOwned.unitAssignments) assignment.style = "neutral";
  for (const auto* project : {&fixture.project, &multiSource, &unbound, &schemaOne, &styleOwned}) {
    const internal::ReviewBasisIndex index{*project};
    for (const auto* takeId : {"take-a", "take-b", "missing-take"})
      CHECK(index.basis(takeId) == internal::reviewBasis(*project, takeId));
  }
  CHECK(internal::ReviewBasisIndex{fixture.project}.basis("take-a") !=
        internal::ReviewBasisIndex{fixture.project}.basis("take-b"));
}

TEST_CASE("only an independent review decision on current material grants review status") {
  ReviewFixture fixture{"second-reviewer"};
  const auto generation = fixture.durableGeneration();
  auto forged = fixture.project;
  approveByHand(forged, "take-a");
  for (const auto* action : {"save", "review", "marker", "transform"}) {
    auto attempt = forged;
    CHECK(!fixture.repository.save(attempt, ReviewFixture::event(action, "take-a", "reviewer")));
  }
  auto handMadeRecord = forged;
  handMadeRecord.reviews.push_back({"hand-made", "take-a", "reviewer", "PASS", "2026-09-28T10:01:00Z"});
  CHECK(!fixture.repository.save(handMadeRecord, ReviewFixture::event("review", "take-a", "reviewer")));
  CHECK(!fixture.repository.recordMetadataRevision(fixture.project,
      {.revisionId = "material-as-annotation", .takeId = "take-a", .rawAssetSha256 = fixture.project.takes.front().rawAssetSha256,
       .kind = std::string{production::kSampleCandidateReviewKind}, .values = {{"reviewId", "hand-made"}},
       .operatorId = "reviewer", .performedAtUtc = "2026-09-28T10:01:00Z"},
      ReviewFixture::event("marker", "material-as-annotation", "reviewer", "2026-09-28T10:01:00Z")));
  for (const auto state : {production::UnitQueueState::PitchReview, production::UnitQueueState::Approved,
                           production::UnitQueueState::Retake}) {
    auto project = fixture.project;
    project.unitAssignments.push_back({.coverageKey = "sustain:u", .pitchLayer = 69, .promptId = "u", .plannedTakeId = "take-u"});
    CHECK(!fixture.repository.importRaw(project, fixture.root / "a.wav",
        {.takeId = "take-u", .promptId = "u", .coverageKey = "sustain:u", .pitchLayer = 69, .initialState = state},
        ReviewFixture::event("import", "take-u")));
  }
  CHECK(fixture.durableGeneration() == generation);

  // A decision holding exactly the command's records is judged on its evidence: refused when the
  // reviewer imported the take or is not a registered reviewer, or when its material is not the
  // take's current audio and basis; accepted from an independent reviewer.
  const auto packet = fixture.packet();
  const auto at = std::string{"2026-09-28T10:02:00Z"};
  auto byImporter = handDecided(fixture.project, packet, "i-69", "second-reviewer", at);
  CHECK(!fixture.repository.save(byImporter, ReviewFixture::event("review", packet.packetSha256, "second-reviewer", at)));
  auto byProducer = handDecided(fixture.project, packet, "i-69", "producer", at);
  CHECK(!fixture.repository.save(byProducer, ReviewFixture::event("review", packet.packetSha256, "producer", at)));
  auto staleBasis = handDecided(fixture.project, packet, "i-69", "reviewer", at);
  staleBasis.metadataRevisions.back().values["reviewBasisSha256"] = std::string(64U, '0');
  CHECK(!fixture.repository.save(staleBasis, ReviewFixture::event("review", packet.packetSha256, "reviewer", at)));
  auto otherAudio = handDecided(fixture.project, packet, "i-69", "reviewer", at);
  otherAudio.metadataRevisions.back().values["audioSha256"] = fixture.project.takes.front().rawAssetSha256;
  CHECK(!fixture.repository.save(otherAudio, ReviewFixture::event("review", packet.packetSha256, "reviewer", at)));
  auto unpaired = handDecided(fixture.project, packet, "i-69", "reviewer", at);
  unpaired.metadataRevisions.pop_back();
  CHECK(!fixture.repository.save(unpaired, ReviewFixture::event("review", packet.packetSha256, "reviewer", at)));
  auto beyondDecision = handDecided(fixture.project, packet, "i-69", "reviewer", at);
  approveByHand(beyondDecision, "take-a");
  CHECK(!fixture.repository.save(beyondDecision, ReviewFixture::event("review", packet.packetSha256, "reviewer", at)));
  CHECK(fixture.durableGeneration() == generation);
  auto independent = handDecided(fixture.project, packet, "i-69", "reviewer", at);
  CHECK(fixture.repository.save(independent, ReviewFixture::event("review", packet.packetSha256, "reviewer", at)));
  CHECK(independent.takes.back().state == production::UnitQueueState::Approved);
  CHECK(independent.takes.front().state == production::UnitQueueState::MarkerReview);
}

TEST_CASE("review history and manual work stay append-only") {
  ReviewFixture fixture;
  fixture.decide(production::SampleCandidateReviewDecision::Accept, "2026-09-28T10:01:00Z");
  const auto staged = fixture.repository.stageOperation(fixture.project, "take-b", "",
      {.kind = production::OperationKind::NormalizeGain, .targetPeak = 0.15F}, "b-normalized");
  CHECK(staged);
  CHECK(fixture.repository.commitStaged(fixture.project, staged.value(), "b-revision", "producer",
                                        "2026-09-28T10:02:00Z", "take-b", ""));
  const auto generation = fixture.durableGeneration();
  std::vector<std::pair<const char*, void (*)(production::VoicebankProductionProject&)>> rewrites{
      {"drop a decision", [](auto& project) { project.reviews.pop_back(); }},
      {"reverse a decision", [](auto& project) { project.reviews.front().result = "REJECTED"; }},
      {"rewrite review material", [](auto& project) {
         for (auto& revision : project.metadataRevisions)
           if (revision.kind == production::kSampleCandidateReviewKind) revision.values["unitId"] = "other";
       }},
      {"rewrite a receipt", [](auto& project) { project.metadataRevisions.front().operatorId = "reviewer"; }},
      {"drop an edit", [](auto& project) {
         project.takes.back().derivedRevisionIds.clear();
         project.derivedRevisions.clear();
       }},
      {"rewrite an edit", [](auto& project) { project.derivedRevisions.front().parameters["targetPeak"] = "0.5"; }},
      {"relabel a take", [](auto& project) {
         project.takes.back().supersedesTakeId = "take-a";
       }},
  };
  for (const auto& [name, rewrite] : rewrites) {
    auto attempt = fixture.project;
    rewrite(attempt);
    CHECK(!fixture.repository.save(attempt, ReviewFixture::event("save", name)));
    CHECK(fixture.durableGeneration() == generation);
  }
}

TEST_CASE("changed material lowers only the approvals it invalidates and keeps their history") {
  ReviewFixture fixture;
  fixture.decide(production::SampleCandidateReviewDecision::Accept, "2026-09-28T10:01:00Z");
  CHECK(fixture.unit("take-a").state == production::UnitQueueState::Approved);
  CHECK(fixture.unit("take-b").state == production::UnitQueueState::Approved);
  const auto decisions = fixture.project.reviews.size();

  // A marker annotation on one take lowers that take only.
  CHECK(fixture.repository.recordMetadataRevision(fixture.project,
      {.revisionId = "a-markers", .takeId = "take-a", .rawAssetSha256 = fixture.project.takes.front().rawAssetSha256,
       .kind = "MARKERS_AND_PITCH", .values = {{"vowelOnset", "120"}}, .operatorId = "producer",
       .performedAtUtc = "2026-09-28T10:02:00Z"},
      ReviewFixture::event("marker", "a-markers", "producer", "2026-09-28T10:02:00Z")));
  CHECK(fixture.unit("take-a").state == production::UnitQueueState::MarkerReview);
  CHECK(!fixture.unit("take-a").markerReviewed);
  CHECK(fixture.unit("take-b").state == production::UnitQueueState::Approved);

  // A generic save that changes what every basis names, here the inventory it was planned
  // against, keeps its own change and lowers every approval it invalidated, not refusing it.
  auto replanned = fixture.project;
  replanned.inventorySha256 = std::string(64U, 'b');
  CHECK(fixture.repository.save(replanned, ReviewFixture::event("save", "replanned")));
  CHECK(replanned.inventorySha256 == std::string(64U, 'b'));
  for (const auto& assignment : replanned.unitAssignments) {
    CHECK(assignment.state == production::UnitQueueState::MarkerReview);
    CHECK(!assignment.markerReviewed);
    CHECK(!assignment.pitchReviewed);
  }
  CHECK(replanned.reviews.size() == decisions);
  CHECK(std::count_if(replanned.metadataRevisions.begin(), replanned.metadataRevisions.end(), [](const auto& value) {
          return value.kind == production::kSampleCandidateReviewKind;
        }) == 2);
  const auto recovered = fixture.repository.recover();
  CHECK(recovered);
  CHECK(production::encodeProductionProject(recovered.value()) == production::encodeProductionProject(replanned));

  // Review status returns only through a new decision on the changed material.
  fixture.project = replanned;
  fixture.decide(production::SampleCandidateReviewDecision::Accept, "2026-09-28T10:03:00Z", {"a-69"});
  CHECK(fixture.unit("take-a").state == production::UnitQueueState::Approved);
  CHECK(fixture.unit("take-b").state == production::UnitQueueState::MarkerReview);
  CHECK(fixture.project.reviews.size() == decisions + 1U);
}

TEST_CASE("a new producer starts unreviewed") {
  const auto root = seam::test::support::temporaryDirectory("production-review-new");
  const auto license = root / "synthetic-license.txt";
  CHECK(seam::core::durableAtomicWriteTextNew(license, "SYNTHETIC TEST ONLY"));
  production::VoicebankProductionProject project{.projectId = "prereviewed", .inventoryId = "fixture",
      .inventorySha256 = std::string(64U, 'a'), .selectedSourceStrategyId = "synthetic",
      .licenseLocator = license.string(), .licenseSha256 = seam::core::sha256File(license).value()};
  project.sourceStrategies = {{.id = "synthetic", .kind = production::SourceStrategyKind::ProceduralSynthesis,
      .rights = production::Feasibility::Pass, .permissions = {true, true, false, false},
      .licenseLocator = license.string(), .licenseSha256 = project.licenseSha256, .evidenceState = "SYNTHETIC_TEST_ONLY"}};
  project.operators = {{.operatorId = "producer", .role = "PRODUCER"}};
  project.unitAssignments = {{.coverageKey = "sustain:a", .pitchLayer = 69, .promptId = "a", .plannedTakeId = "take-a",
                              .markerReviewed = true}};
  // A refused first generation leaves no durable producer behind.
  production::ProductionProjectRepository refused{root / "refused"};
  auto flagged = project;
  CHECK(!refused.initialize(flagged, ReviewFixture::event("create", project.projectId)));
  CHECK(!refused.recover());
  project.unitAssignments.front().markerReviewed = false;
  production::ProductionProjectRepository repository{root / "workspace"};
  CHECK(repository.initialize(project, ReviewFixture::event("create", project.projectId)));
  const auto recovered = repository.recover();
  CHECK(recovered);
  CHECK(recovered.value().reviews.empty());
  CHECK(!recovered.value().unitAssignments.front().markerReviewed);
}

// AE1: regenerating an approved, edited take adds an alternative. The bank published from the
// approval, the edited audio, its annotation and every decision stay exactly as they were.
TEST_CASE("a retake of an approved edited take keeps the installed bank and all earlier manual work") {
  ReviewFixture fixture;
  const auto staged = fixture.repository.stageOperation(fixture.project, "take-b", "",
      {.kind = production::OperationKind::NormalizeGain, .targetPeak = 0.15F}, "b-normalized");
  CHECK(staged);
  CHECK(fixture.repository.commitStaged(fixture.project, staged.value(), "b-revision", "producer",
                                        "2026-09-28T10:01:00Z", "take-b", ""));
  CHECK(fixture.repository.recordMetadataRevision(fixture.project,
      {.revisionId = "b-markers", .takeId = "take-b", .rawAssetSha256 = takeOf(fixture.project, "take-b").rawAssetSha256,
       .kind = "MARKERS_AND_PITCH", .values = {{"vowelOnset", "120"}}, .operatorId = "producer",
       .performedAtUtc = "2026-09-28T10:02:00Z"},
      ReviewFixture::event("marker", "b-markers", "producer", "2026-09-28T10:02:00Z")));
  fixture.decide(production::SampleCandidateReviewDecision::Accept, "2026-09-28T10:03:00Z");
  CHECK(fixture.unit("take-b").state == production::UnitQueueState::Approved);
  const auto candidate = production::resolveReviewedSampleCandidate(fixture.root / "workspace", fixture.project,
                                                                    fixture.manifest());
  CHECK(candidate);
  const auto installed = fixture.root / "installed";
  CHECK(production::publishSampleCandidate(fixture.root / "workspace", fixture.project, candidate.value(), installed));
  const auto bank = fileDigests(installed);
  CHECK(bank.size() >= 3U);
  const auto before = fixture.project;

  const seam::voicebank::WavOutputFormat format{.sampleRate = 48000U, .channels = 1U,
      .sampleFormat = seam::voicebank::WavSampleFormat::Pcm24};
  CHECK(seam::voicebank::writeWav(fixture.root / "b-retake.wav", format,
                                  seam::test::support::sineWave(48000U, 660.0, 0.12, 0.2F)));
  CHECK(fixture.repository.importRaw(fixture.project, fixture.root / "b-retake.wav",
      {.takeId = "take-b2", .promptId = "b", .coverageKey = "sustain:i", .pitchLayer = 69, .supersedesTakeId = "take-b"},
      ReviewFixture::event("retake", "take-b2", "producer", "2026-09-28T10:04:00Z")));

  // The installed bank is untouched.
  CHECK(fileDigests(installed) == bank);
  // The edited take is retained as an alternative with its audio, processing chain, annotation,
  // decision and review material; nothing earlier was removed or rewritten.
  const auto& old = takeOf(fixture.project, "take-b");
  CHECK(old.state == production::UnitQueueState::Retake);
  CHECK(old.rawAssetSha256 == takeOf(before, "take-b").rawAssetSha256);
  CHECK(old.derivedRevisionIds == std::vector<std::string>{"b-revision"});
  CHECK(fixture.project.reviews.size() == before.reviews.size());
  for (std::size_t row = 0U; row < before.reviews.size(); ++row) {
    CHECK(fixture.project.reviews[row].reviewId == before.reviews[row].reviewId);
    CHECK(fixture.project.reviews[row].result == before.reviews[row].result);
  }
  CHECK(fixture.project.metadataRevisions.size() > before.metadataRevisions.size());
  CHECK(std::equal(before.metadataRevisions.begin(), before.metadataRevisions.end(),
                   fixture.project.metadataRevisions.begin(), production::sameMetadataRevision));
  CHECK(fixture.project.derivedRevisions.size() == before.derivedRevisions.size());
  CHECK(fixture.project.derivedRevisions.front().outputSha256 == before.derivedRevisions.front().outputSha256);
  for (const auto& asset : before.assets) {
    CHECK(std::any_of(fixture.project.assets.begin(), fixture.project.assets.end(),
                      [&](const auto& value) { return value.sha256 == asset.sha256; }));
    CHECK(seam::core::sha256File(fixture.repository.assetPath(asset)).value() == asset.sha256);
  }
  // The retake waits for its own review; the other unit keeps its approval.
  CHECK(takeOf(fixture.project, "take-b2").supersedesTakeId == "take-b");
  CHECK(fixture.unit("take-b2").state == production::UnitQueueState::MarkerReview);
  CHECK(!fixture.unit("take-b2").markerReviewed);
  CHECK(fixture.unit("take-a").state == production::UnitQueueState::Approved);
  CHECK(fixture.repository.recover().value().takes.size() == 3U);

  // Until the retake is reviewed, neither a fresh nor the earlier candidate can be published.
  CHECK(!production::resolveReviewedSampleCandidate(fixture.root / "workspace", fixture.project, fixture.manifest()));
  CHECK(!production::publishSampleCandidate(fixture.root / "workspace", fixture.project, candidate.value(),
                                            fixture.root / "stale"));
  CHECK(!std::filesystem::exists(fixture.root / "stale"));
  // Reviewed, the retake publishes a new directory; the installed one is never overwritten.
  fixture.decide(production::SampleCandidateReviewDecision::Accept, "2026-09-28T10:05:00Z", {"i-69"});
  const auto next = production::resolveReviewedSampleCandidate(fixture.root / "workspace", fixture.project,
                                                               fixture.manifest());
  CHECK(next);
  CHECK(!production::publishSampleCandidate(fixture.root / "workspace", fixture.project, next.value(), installed));
  CHECK(production::publishSampleCandidate(fixture.root / "workspace", fixture.project, next.value(),
                                           fixture.root / "installed-retake"));
  CHECK(fileDigests(installed) == bank);
  CHECK(fileDigests(fixture.root / "installed-retake") != bank);
}

TEST_CASE("selecting a retained alternative keeps both histories and needs a new review") {
  ReviewFixture fixture;
  fixture.decide(production::SampleCandidateReviewDecision::Accept, "2026-09-28T10:01:00Z");
  const seam::voicebank::WavOutputFormat format{.sampleRate = 48000U, .channels = 1U,
      .sampleFormat = seam::voicebank::WavSampleFormat::Pcm24};
  CHECK(seam::voicebank::writeWav(fixture.root / "b-retake.wav", format,
                                  seam::test::support::sineWave(48000U, 660.0, 0.12, 0.2F)));
  CHECK(fixture.repository.importRaw(fixture.project, fixture.root / "b-retake.wav",
      {.takeId = "take-b2", .promptId = "b", .coverageKey = "sustain:i", .pitchLayer = 69, .supersedesTakeId = "take-b"},
      ReviewFixture::event("retake", "take-b2", "producer", "2026-09-28T10:02:00Z")));
  const auto generation = fixture.durableGeneration();
  const auto decisions = fixture.project.reviews.size();
  const auto at = std::string{"2026-09-28T10:03:00Z"};
  CHECK(!fixture.repository.selectTake(fixture.project, "take-b", std::string(64U, '0'), "producer", at));
  CHECK(!fixture.repository.selectTake(fixture.project, "take-b", projectSha(fixture.project), "reviewer", at));
  CHECK(!fixture.repository.selectTake(fixture.project, "take-b", projectSha(fixture.project), "producer", "10:03"));
  CHECK(!fixture.repository.selectTake(fixture.project, "take-b2", projectSha(fixture.project), "producer", at));
  CHECK(!fixture.repository.selectTake(fixture.project, "take-a", projectSha(fixture.project), "producer", at));
  CHECK(!fixture.repository.selectTake(fixture.project, "missing", projectSha(fixture.project), "producer", at));
  // Switching back by a generic save cannot carry the earlier approval with it.
  auto forged = fixture.project;
  for (auto& take : forged.takes) {
    if (take.takeId == "take-b") take.state = production::UnitQueueState::Approved;
    if (take.takeId == "take-b2") take.state = production::UnitQueueState::Retake;
  }
  for (auto& assignment : forged.unitAssignments)
    if (assignment.takeId == "take-b2") {
      assignment.takeId = "take-b";
      assignment.state = production::UnitQueueState::Approved;
      assignment.markerReviewed = assignment.pitchReviewed = true;
    }
  CHECK(!fixture.repository.save(forged, ReviewFixture::event("save", "take-b", "producer", at)));
  CHECK(fixture.durableGeneration() == generation);

  const auto selected = fixture.repository.selectTake(fixture.project, "take-b", projectSha(fixture.project), "producer", at);
  CHECK(selected);
  CHECK(selected.value().committedGeneration == generation + 1U);
  CHECK(selected.value().committedProjectSha256 == projectSha(fixture.project));
  CHECK(takeOf(fixture.project, "take-b").state == production::UnitQueueState::MarkerReview);
  CHECK(takeOf(fixture.project, "take-b2").state == production::UnitQueueState::Retake);
  CHECK(fixture.unit("take-b").state == production::UnitQueueState::MarkerReview);
  CHECK(!fixture.unit("take-b").markerReviewed);
  CHECK(fixture.unit("take-a").state == production::UnitQueueState::Approved);
  CHECK(fixture.project.reviews.size() == decisions);
  CHECK(production::encodeProductionProject(fixture.repository.recover().value()) ==
        production::encodeProductionProject(fixture.project));
  const auto journal = seam::core::readTextFileLimited(
      fixture.root / "workspace/journal" / (std::string(19U, '0') + std::to_string(generation + 1U) + ".json"),
      1024U * 1024U);
  CHECK(journal);
  CHECK(journal.value().find("\"select-take\"") != std::string::npos);

  // Only a new independent decision approves the selected take again.
  fixture.decide(production::SampleCandidateReviewDecision::Accept, "2026-09-28T10:04:00Z", {"i-69"});
  CHECK(fixture.unit("take-b").state == production::UnitQueueState::Approved);
  CHECK(fixture.project.reviews.size() == decisions + 1U);
  // An approved take can be set aside for an alternative; the approval stays in history only.
  CHECK(fixture.repository.selectTake(fixture.project, "take-b2", projectSha(fixture.project), "producer",
                                      "2026-09-28T10:05:00Z"));
  CHECK(takeOf(fixture.project, "take-b").state == production::UnitQueueState::Retake);
  CHECK(fixture.unit("take-b2").state == production::UnitQueueState::MarkerReview);
  CHECK(fixture.project.reviews.size() == decisions + 1U);
  CHECK(fixture.project.reviews.back().result == "PASS");
}

TEST_CASE("a duration-changing edit is approved again only through a review of remapped markers") {
  ReviewFixture fixture;
  fixture.decide(production::SampleCandidateReviewDecision::Accept, "2026-09-28T10:01:00Z");
  const auto staged = fixture.repository.stageOperation(fixture.project, "take-a", "",
      {.kind = production::OperationKind::Trim, .startFrame = 0U, .endFrame = 2880U}, "a-trimmed");
  CHECK(staged);
  CHECK(fixture.repository.commitStaged(fixture.project, staged.value(), "a-trim", "producer",
                                        "2026-09-28T10:02:00Z", "take-a", ""));
  CHECK(fixture.unit("take-a").state == production::UnitQueueState::MarkerReview);
  CHECK(fixture.unit("take-b").state == production::UnitQueueState::Approved);
  // Markers placed for the longer audio cannot be put in front of a reviewer for the shorter audio.
  CHECK(!production::prepareSampleCandidateReview(fixture.root / "workspace", fixture.project, fixture.manifest()));
  auto remapped = fixture.manifest();
  auto& unit = *std::find_if(remapped.units.begin(), remapped.units.end(), [](const auto& value) { return value.id == "a-69"; });
  unit.markers = {.audioOffset = 0, .consonantEnd = 0, .vowelOnset = 0, .stableStart = 240,
                  .loopStart = 240, .loopEnd = 2400, .releaseStart = 2640, .audioEnd = 2880};
  std::erase_if(unit.pitchMarks, [](const auto& mark) { return mark.frame >= 2880; });
  const auto packet = production::prepareSampleCandidateReview(fixture.root / "workspace", fixture.project, remapped);
  CHECK(packet);
  const std::vector<std::string> trimmed{"a-69"};
  CHECK(production::commitSampleCandidateReview(fixture.root / "workspace", fixture.project, packet.value(), "reviewer",
      "2026-09-28T10:03:00Z", production::SampleCandidateReviewDecision::Accept, trimmed));
  CHECK(fixture.unit("take-a").state == production::UnitQueueState::Approved);
  // The decision names the remapped markers: only they resolve a publishable candidate.
  CHECK(!production::resolveReviewedSampleCandidate(fixture.root / "workspace", fixture.project, fixture.manifest()));
  CHECK(production::resolveReviewedSampleCandidate(fixture.root / "workspace", fixture.project, remapped));
}

// The durable path's own backstop, independent of the operations that already lower approvals
// before they save: every kind of per-take change lowers exactly that take, and an approval with
// no recorded material (legacy history) stands until its own take changes.
TEST_CASE("the durable path lowers exactly the approvals a change reaches") {
  namespace internal = production::review_internal;
  ReviewFixture fixture;
  fixture.decide(production::SampleCandidateReviewDecision::Accept, "2026-09-28T10:01:00Z");
  const auto current = fixture.project;
  const auto save = ReviewFixture::event("save", "review-transition", "producer", "2026-09-28T10:02:00Z");
  const auto state = [](const production::VoicebankProductionProject& project, std::string_view takeId) {
    return std::find_if(project.unitAssignments.begin(), project.unitAssignments.end(),
                        [&](const auto& value) { return value.takeId == takeId; })->state;
  };
  const auto lowered = [&](auto change) {
    auto proposed = current;
    change(proposed);
    const auto applied = internal::applyReviewTransition(&current, proposed, save, false);
    if (!applied) throw seam::test::Failure{"transition: " + applied.error().message};
    return std::pair{state(proposed, "take-a") != production::UnitQueueState::Approved,
                     state(proposed, "take-b") != production::UnitQueueState::Approved};
  };
  const auto a = takeOf(current, "take-a");
  CHECK((lowered([&](auto& project) {  // an annotation
    project.metadataRevisions.push_back({.revisionId = "a-note", .takeId = "take-a", .rawAssetSha256 = a.rawAssetSha256,
        .kind = "MARKERS_AND_PITCH", .values = {{"vowelOnset", "130"}}, .operatorId = "producer",
        .performedAtUtc = "2026-09-28T10:02:00Z"});
  }) == std::pair{true, false}));
  CHECK((lowered([&](auto& project) {  // processing
    project.assets.push_back({.sha256 = std::string(64U, 'f'), .relativePath = "derived/ff.wav", .byteSize = 10U,
                              .kind = production::AssetKind::Derived});
    project.derivedRevisions.push_back({.revisionId = "a-edit", .inputSha256 = a.rawAssetSha256,
        .outputSha256 = std::string(64U, 'f'), .operation = production::OperationKind::NormalizeGain,
        .operationVersion = "1", .parameters = {}, .operatorId = "producer", .performedAtUtc = "2026-09-28T10:02:00Z"});
    for (auto& take : project.takes)
      if (take.takeId == "take-a") take.derivedRevisionIds.push_back("a-edit");
  }) == std::pair{true, false}));
  CHECK((lowered([&](auto& project) {  // the assignment it fills
    for (auto& assignment : project.unitAssignments)
      if (assignment.takeId == "take-b") assignment.plannedTakeId = "take-b-replanned";
  }) == std::pair{false, true}));
  CHECK((lowered([&](auto& project) {  // the producer-wide source policy
    project.sourceStrategies.front().evidenceState = "RECAPTURED";
  }) == std::pair{true, true}));
  CHECK((lowered([&](auto& project) {  // unrelated to any decision
    project.operators.push_back({.operatorId = "observer", .role = "PRODUCER"});
  }) == std::pair{false, false}));

  // Legacy history: an approval without recorded material stands until its own take changes.
  auto legacy = fixture.project;
  std::erase_if(legacy.metadataRevisions, [](const auto& value) { return production::candidate_publication_internal::reviewMetadata(value.kind); });
  const auto legacyChange = [&](std::string_view takeId) {
    auto proposed = legacy;
    proposed.metadataRevisions.push_back({.revisionId = "note", .takeId = std::string{takeId},
        .rawAssetSha256 = takeOf(legacy, takeId).rawAssetSha256, .kind = "MARKERS_AND_PITCH",
        .values = {{"vowelOnset", "130"}}, .operatorId = "producer", .performedAtUtc = "2026-09-28T10:02:00Z"});
    CHECK(internal::applyReviewTransition(&legacy, proposed, save, false));
    return std::pair{state(proposed, "take-a"), state(proposed, "take-b")};
  };
  CHECK((legacyChange("take-b") == std::pair{production::UnitQueueState::Approved, production::UnitQueueState::MarkerReview}));
  CHECK((legacyChange("take-a") == std::pair{production::UnitQueueState::MarkerReview, production::UnitQueueState::Approved}));
}

TEST_CASE("an approval needs the take's current take-inspection receipt; a rejection does not") {
  namespace internal = production::review_internal;
  ReviewFixture fixture;
  const auto packet = fixture.packet();
  const auto event = ReviewFixture::event("review", "take-a", "reviewer", "2026-09-28T10:01:00Z");
  const auto inspected = [](const production::VoicebankProductionProject& project, std::string_view takeId) {
    return std::any_of(project.metadataRevisions.begin(), project.metadataRevisions.end(), [&](const auto& value) {
      return value.takeId == takeId && value.kind == production::kTakeInspectionRevisionKind &&
             value.rawAssetSha256 == takeOf(project, takeId).rawAssetSha256;
    });
  };
  CHECK(inspected(fixture.project, "take-a"));
  CHECK(inspected(fixture.project, "take-b"));
  auto current = fixture.project;
  auto accepted = handDecided(current, packet, "a-69", "reviewer", "2026-09-28T10:01:00Z");
  CHECK(internal::applyReviewTransition(&current, accepted, event, false));
  // Legacy material: the same take without the receipt its import now appends. The decision is
  // made on that exact material, so nothing but the missing receipt differs.
  auto legacy = fixture.project;
  std::erase_if(legacy.metadataRevisions,
                [](const auto& value) { return value.kind == production::kTakeInspectionRevisionKind; });
  CHECK(!inspected(legacy, "take-a"));
  auto uninspected = handDecided(legacy, packet, "a-69", "reviewer", "2026-09-28T10:01:00Z");
  const auto refused = internal::applyReviewTransition(&legacy, uninspected, event, false);
  CHECK(!refused);
  CHECK(refused.error().message.find("take-inspection.v2 receipt") != std::string::npos);
  // A rejection admits nothing, so it needs no receipt.
  auto rejected = uninspected;
  rejected.reviews.back().result = "REJECTED";
  for (auto& take : rejected.takes)
    if (take.takeId == "take-a") take.state = production::UnitQueueState::Rejected;
  for (auto& assignment : rejected.unitAssignments)
    if (assignment.takeId == "take-a") {
      assignment.state = production::UnitQueueState::Rejected;
      assignment.markerReviewed = assignment.pitchReviewed = false;
    }
  CHECK(internal::applyReviewTransition(&legacy, rejected, event, false));
}
