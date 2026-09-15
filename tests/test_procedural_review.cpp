// A review decision is evidence about one exact rendering of one exact recipe. These cases check the
// invariant that makes it worth storing: a changed recipe, renderer or evidence cannot inherit a
// prior approval, and a decision cannot be attached to material it does not describe.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/distribution/procedural_review.hpp"
#include "seam/distribution/procedural_review_store.hpp"
#include "seam/core/sha256.hpp"

#include <algorithm>
#include <atomic>
#include <thread>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using namespace seam;

distribution::ProceduralSingerManifest testManifest() {
  distribution::ProceduralSingerManifest manifest;
  manifest.id = "seam.review.pilot";
  manifest.version = "1.0.0";
  manifest.displayName = "Review Pilot";
  manifest.language = "ja";
  manifest.styles = {"neutral"};
  manifest.engineId = "seam.source-filter.v1";
  manifest.engineRevision = 14U;
  manifest.recipeEntry = "recipe.json";
  manifest.recipeSha256 = std::string(64U, 'a');
  manifest.phones = {"a", "i"};
  return manifest;
}

// Writes evidence of a known size so the frozen digest is a function of real bytes.
std::filesystem::path writeEvidence(const std::filesystem::path& root, std::string_view name,
                                    std::string_view contents) {
  std::filesystem::create_directories(root);
  const auto path = root / name;
  std::ofstream(path, std::ios::binary | std::ios::trunc) << contents;
  return path;
}

// A candidate whose evidence exists on disk, so freezing computes real digests.
struct Fixture final {
  std::filesystem::path root;
  std::filesystem::path score;
  std::filesystem::path audio;
  distribution::ProceduralReviewCandidate candidate;
};

Fixture makeFixture(std::string_view name, std::string_view scoreText = "score-v1",
                    std::string_view audioText = "audio-v1") {
  Fixture fixture;
  fixture.root = test::support::temporaryDirectory(name);
  fixture.score = writeEvidence(fixture.root, "score.json", scoreText);
  fixture.audio = writeEvidence(fixture.root, "song.wav", audioText);
  const auto manifest = testManifest();
  const auto basis = distribution::freezeProceduralReviewBasis(
      manifest, "content-hash-1", "seam-render-abi-3", 14U, 48000U, fixture.score, fixture.audio,
      "settings-1");
  if (!basis) throw test::Failure{"freezing a review basis failed: " + basis.error().message};
  fixture.candidate.candidateId = "candidate-1";
  fixture.candidate.manifest = manifest;
  fixture.candidate.basis = basis.value();
  return fixture;
}

distribution::ProceduralReviewDecision decisionFor(
    const distribution::ProceduralReviewCandidate& candidate, std::string_view reviewId,
    distribution::ProceduralReviewDecisionKind kind, std::string_view at = "2026-09-15T10:00:00Z") {
  distribution::ProceduralReviewDecision decision;
  decision.reviewId = std::string{reviewId};
  decision.candidateId = candidate.candidateId;
  decision.basisDigest = candidate.basis.digest().value();
  decision.recordedBasis = candidate.basis;
  decision.kind = kind;
  decision.reviewerId = "reviewer-1";
  decision.reviewedAtUtc = std::string{at};
  return decision;
}

}  // namespace

TEST_CASE("A review basis digest is stable and covers every field it claims to") {
  const auto fixture = makeFixture("procedural-review-basis-digest");
  const auto first = fixture.candidate.basis.digest();
  CHECK(first.hasValue());
  if (!first) return;
  const auto second = fixture.candidate.basis.digest();
  CHECK(second.hasValue());
  if (!second) return;
  CHECK(first.value() == second.value());
  CHECK(first.value().size() == 64U);

  // A JSON round trip must not change the digest, because the digest is what a stored decision is
  // compared against after a save and reload.
  distribution::ProceduralReviewBasisJsonCodec codec;
  const auto encoded = codec.encode(fixture.candidate.basis);
  CHECK(encoded.hasValue());
  if (!encoded) return;
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded.hasValue());
  if (!decoded) return;
  CHECK(decoded.value() == fixture.candidate.basis);
  CHECK(decoded.value().digest().value() == first.value());
}

TEST_CASE("A changed recipe cannot inherit a prior acceptance") {
  const auto fixture = makeFixture("procedural-review-changed-recipe");
  const auto accepted = decisionFor(fixture.candidate, "review-1",
                                    distribution::ProceduralReviewDecisionKind::Accept);
  const auto recorded = distribution::recordProceduralReviewDecision(fixture.candidate, accepted);
  CHECK(recorded.hasValue());
  if (!recorded) return;
  const auto fresh = distribution::resolveProceduralReviewDecisions(
      fixture.candidate, {accepted});
  CHECK(fresh.hasValue());
  if (!fresh) return;
  CHECK(fresh.value().accepted());
  CHECK(fresh.value().stale.empty());

  // The same resource id and version, but a different recipe and therefore a different singer.
  auto changed = fixture.candidate;
  changed.basis.recipeSha256 =
      seam::core::sha256Hex(std::string_view{"a different recipe encoding"});
  const auto receipt = distribution::resolveProceduralReviewDecisions(changed, {accepted});
  CHECK(receipt.hasValue());
  if (!receipt) return;
  CHECK(!receipt.value().accepted());
  CHECK(receipt.value().current.empty());
  CHECK(receipt.value().stale.size() == 1U);
  CHECK(receipt.value().stale.front().decision.reviewId == "review-1");
  // The stale report names the field that actually changed rather than only saying something did.
  CHECK(std::find(receipt.value().stale.front().differences.begin(),
                  receipt.value().stale.front().differences.end(),
                  "recipeSha256") != receipt.value().stale.front().differences.end());
}

TEST_CASE("A changed renderer revision or render ABI cannot inherit a prior acceptance") {
  const auto fixture = makeFixture("procedural-review-changed-renderer");
  const auto accepted = decisionFor(fixture.candidate, "review-1",
                                    distribution::ProceduralReviewDecisionKind::Accept);

  auto newerCompiler = fixture.candidate;
  newerCompiler.basis.compilerRevision = 15U;
  auto receipt = distribution::resolveProceduralReviewDecisions(newerCompiler, {accepted});
  CHECK(receipt.hasValue());
  if (!receipt) return;
  CHECK(!receipt.value().accepted());
  CHECK(receipt.value().stale.size() == 1U);

  auto newerAbi = fixture.candidate;
  newerAbi.basis.renderAbi = "seam-render-abi-4";
  receipt = distribution::resolveProceduralReviewDecisions(newerAbi, {accepted});
  CHECK(receipt.hasValue());
  if (!receipt) return;
  CHECK(!receipt.value().accepted());

  auto newerEngine = fixture.candidate;
  newerEngine.basis.engineRevision = 15U;
  receipt = distribution::resolveProceduralReviewDecisions(newerEngine, {accepted});
  CHECK(receipt.hasValue());
  if (!receipt) return;
  CHECK(!receipt.value().accepted());
}

TEST_CASE("Changed evidence invalidates a review even when the recipe is unchanged") {
  const auto fixture = makeFixture("procedural-review-changed-evidence");
  const auto accepted = decisionFor(fixture.candidate, "review-1",
                                    distribution::ProceduralReviewDecisionKind::Accept);
  // The recipe and renderer are identical; only the audio that was listened to is different. A
  // decision about one rendering must not cover another.
  auto reRendered = fixture.candidate;
  const auto audio = writeEvidence(fixture.root, "song-rerender.wav", "audio-v2");
  const auto refrozen = distribution::freezeProceduralReviewBasis(
      reRendered.manifest, reRendered.basis.contentHash, reRendered.basis.renderAbi,
      reRendered.basis.compilerRevision, reRendered.basis.sampleRate, fixture.score, audio,
      reRendered.basis.settingsDigest);
  CHECK(refrozen.hasValue());
  if (!refrozen) return;
  reRendered.basis = refrozen.value();
  const auto receipt = distribution::resolveProceduralReviewDecisions(reRendered, {accepted});
  CHECK(receipt.hasValue());
  if (!receipt) return;
  CHECK(!receipt.value().accepted());
  CHECK(receipt.value().stale.size() == 1U);

  const auto differences = distribution::proceduralReviewBasisDifferences(
      fixture.candidate.basis, reRendered.basis);
  CHECK(std::find(differences.begin(), differences.end(), "audioSha256") != differences.end());
}

TEST_CASE("A rejection after an acceptance withdraws it, and vice versa") {
  const auto fixture = makeFixture("procedural-review-decision-order");
  const auto accepted = decisionFor(fixture.candidate, "review-1",
                                    distribution::ProceduralReviewDecisionKind::Accept,
                                    "2026-09-15T10:00:00Z");
  const auto rejected = decisionFor(fixture.candidate, "review-2",
                                    distribution::ProceduralReviewDecisionKind::Reject,
                                    "2026-09-15T11:00:00Z");
  auto receipt = distribution::resolveProceduralReviewDecisions(fixture.candidate,
                                                              {accepted, rejected});
  CHECK(receipt.hasValue());
  if (!receipt) return;
  CHECK(receipt.value().current.size() == 2U);
  CHECK(!receipt.value().accepted());
  CHECK(receipt.value().reject());

  // The same two decisions in the other order, with the acceptance later, is an acceptance.
  const auto laterAccept = decisionFor(fixture.candidate, "review-3",
                                       distribution::ProceduralReviewDecisionKind::Accept,
                                       "2026-09-15T12:00:00Z");
  receipt = distribution::resolveProceduralReviewDecisions(fixture.candidate,
                                                          {accepted, rejected, laterAccept});
  CHECK(receipt.hasValue());
  if (!receipt) return;
  CHECK(receipt.value().accepted());
  CHECK(!receipt.value().reject());
}

TEST_CASE("A decision cannot be recorded against material it does not describe") {
  const auto fixture = makeFixture("procedural-review-mismatched-decision");
  auto forged = decisionFor(fixture.candidate, "review-1",
                            distribution::ProceduralReviewDecisionKind::Accept);
  forged.basisDigest = std::string(64U, 'b');
  const auto recorded = distribution::recordProceduralReviewDecision(fixture.candidate, forged);
  CHECK(!recorded.hasValue());

  auto malformed = decisionFor(fixture.candidate, "review-2",
                               distribution::ProceduralReviewDecisionKind::Accept);
  malformed.basisDigest = "not-a-digest";
  CHECK(!distribution::recordProceduralReviewDecision(fixture.candidate, malformed).hasValue());

  auto anonymous = decisionFor(fixture.candidate, "review-3",
                               distribution::ProceduralReviewDecisionKind::Accept);
  anonymous.reviewerId.clear();
  CHECK(!distribution::recordProceduralReviewDecision(fixture.candidate, anonymous).hasValue());

  // A carried basis that contradicts its own digest is refused, so a decision cannot appear to
  // describe material it does not.
  auto contradictory = decisionFor(fixture.candidate, "review-4",
                                   distribution::ProceduralReviewDecisionKind::Accept);
  contradictory.recordedBasis->contentHash = "a different installed resource";
  CHECK(!distribution::recordProceduralReviewDecision(fixture.candidate, contradictory).hasValue());
}

TEST_CASE("A candidate with no evidence cannot be accepted") {
  const auto fixture = makeFixture("procedural-review-no-evidence");
  auto bare = fixture.candidate;
  bare.basis.scoreSha256.clear();
  bare.basis.audioSha256.clear();
  const auto digest = bare.basis.digest();
  CHECK(digest.hasValue());
  if (!digest) return;
  distribution::ProceduralReviewDecision decision;
  decision.reviewId = "review-1";
  decision.candidateId = bare.candidateId;
  decision.basisDigest = digest.value();
  decision.kind = distribution::ProceduralReviewDecisionKind::Accept;
  decision.reviewerId = "reviewer-1";
  decision.reviewedAtUtc = "2026-09-15T10:00:00Z";
  // Acceptance claims evidence was examined, so it is refused rather than recorded as a claim
  // nobody can check.
  CHECK(!distribution::recordProceduralReviewDecision(bare, decision).hasValue());
  // A rejection without evidence is still a valid, recordable decision.
  decision.kind = distribution::ProceduralReviewDecisionKind::Reject;
  CHECK(distribution::recordProceduralReviewDecision(bare, decision).hasValue());
}

TEST_CASE("A decision belonging to another candidate is not this candidate's evidence") {
  const auto fixture = makeFixture("procedural-review-other-candidate");
  const auto accepted = decisionFor(fixture.candidate, "review-1",
                                    distribution::ProceduralReviewDecisionKind::Accept);
  auto other = fixture.candidate;
  other.candidateId = "candidate-2";
  const auto receipt = distribution::resolveProceduralReviewDecisions(other, {accepted});
  CHECK(receipt.hasValue());
  if (!receipt) return;
  CHECK(!receipt.value().accepted());
  CHECK(receipt.value().current.empty());
  // It is not stale either: it belongs to a different resource entirely and is simply not evidence
  // for this one.
  CHECK(receipt.value().stale.empty());
}

TEST_CASE("Freezing hashes the evidence rather than trusting a supplied digest") {
  const auto fixture = makeFixture("procedural-review-freeze-hashes");
  // The frozen basis names the digest of the bytes on disk.
  const auto expectedScore = seam::core::sha256File(fixture.score, 1024U * 1024U);
  CHECK(expectedScore.hasValue());
  if (!expectedScore) return;
  CHECK(fixture.candidate.basis.scoreSha256 == expectedScore.value());
  const auto expectedAudio = seam::core::sha256File(fixture.audio, 1024U * 1024U);
  CHECK(expectedAudio.hasValue());
  if (!expectedAudio) return;
  CHECK(fixture.candidate.basis.audioSha256 == expectedAudio.value());

  // Missing evidence is refused instead of freezing an empty digest.
  const auto missing = distribution::freezeProceduralReviewBasis(
      testManifest(), "content-hash-1", "seam-render-abi-3", 14U, 48000U,
      fixture.root / "absent.json", fixture.audio, "settings-1");
  CHECK(!missing.hasValue());

  // A basis that names no rendering at all is refused.
  distribution::ProceduralReviewBasis incomplete;
  incomplete.resourceId = "seam.review.pilot";
  CHECK(!incomplete.digest().hasValue());
  CHECK(!incomplete.validate().hasValue());
}

TEST_CASE("A malformed or wrong-family review basis document is refused") {
  distribution::ProceduralReviewBasisJsonCodec codec;
  CHECK(!codec.decode("{}").hasValue());
  CHECK(!codec.decode("not json").hasValue());
  // A basis claiming another family is refused rather than read as this one.
  const auto foreign =
      std::string{R"({"formatId":"com.project-seam.procedural-singer","schemaVersion":1})"};
  const auto decoded = codec.decode(foreign);
  CHECK(!decoded.hasValue());
  if (decoded) return;
  CHECK(decoded.error().code == seam::core::ErrorCode::Unsupported);
}

// A review store is the only durable record of what a reviewer approved, so it must refuse to lose or
// rewrite a decision. These cases check the store's own integrity, not the review rules it reuses.
TEST_CASE("A review store keeps decisions across reopen and refuses a duplicate id") {
  const auto root = test::support::temporaryDirectory("procedural-review-store");
  const auto statePath = root / "reviews" / "decisions.json";
  auto store = distribution::ProceduralReviewStore::open(statePath);
  CHECK(store.hasValue());
  if (!store) return;
  CHECK(!std::filesystem::exists(statePath));

  const auto fixture = makeFixture("procedural-review-store-record");
  const auto accepted = decisionFor(fixture.candidate, "review-1",
                                    distribution::ProceduralReviewDecisionKind::Accept);
  CHECK(store.value().record(fixture.candidate, accepted).hasValue());
  CHECK(std::filesystem::exists(statePath));
  CHECK(store.value().size() == 1U);
  // The same id describes the same decision, so it is refused rather than appended a second time.
  const auto duplicate = store.value().record(fixture.candidate, accepted);
  CHECK(!duplicate.hasValue());
  CHECK(store.value().size() == 1U);

  // A reopened store still resolves the approval, so the decision survived the round trip.
  auto reopened = distribution::ProceduralReviewStore::open(statePath);
  CHECK(reopened.hasValue());
  if (!reopened) return;
  const auto receipt = reopened.value().resolve(fixture.candidate);
  CHECK(receipt.hasValue());
  if (!receipt) return;
  CHECK(receipt.value().accepted());
  CHECK(receipt.value().current.size() == 1U);

  // A second decision against the same candidate is appended, and the later one decides.
  const auto rejected = decisionFor(fixture.candidate, "review-2",
                                    distribution::ProceduralReviewDecisionKind::Reject,
                                    "2026-09-15T12:00:00Z");
  CHECK(reopened.value().record(fixture.candidate, rejected).hasValue());
  const auto after = reopened.value().resolve(fixture.candidate);
  CHECK(after.hasValue());
  if (!after) return;
  CHECK(!after.value().accepted());
  CHECK(after.value().reject());
  CHECK(after.value().current.size() == 2U);
}

TEST_CASE("A review store refuses a decision that is not about the candidate it names") {
  const auto root = test::support::temporaryDirectory("procedural-review-store-refusals");
  auto store = distribution::ProceduralReviewStore::open(root / "decisions.json");
  CHECK(store.hasValue());
  if (!store) return;
  const auto fixture = makeFixture("procedural-review-store-refusal-fixture");
  // Every rule that makes a decision meaningful still applies through the store.
  auto forged = decisionFor(fixture.candidate, "review-1",
                            distribution::ProceduralReviewDecisionKind::Accept);
  forged.basisDigest = std::string(64U, 'b');
  CHECK(!store.value().record(fixture.candidate, forged).hasValue());
  auto anonymous = decisionFor(fixture.candidate, "review-2",
                               distribution::ProceduralReviewDecisionKind::Accept);
  anonymous.reviewerId.clear();
  CHECK(!store.value().record(fixture.candidate, anonymous).hasValue());
  auto noEvidence = fixture.candidate;
  noEvidence.basis.scoreSha256.clear();
  noEvidence.basis.audioSha256.clear();
  distribution::ProceduralReviewDecision bare;
  bare.reviewId = "review-3";
  bare.candidateId = noEvidence.candidateId;
  bare.basisDigest = noEvidence.basis.digest().value();
  bare.kind = distribution::ProceduralReviewDecisionKind::Accept;
  bare.reviewerId = "reviewer-1";
  bare.reviewedAtUtc = "2026-09-15T10:00:00Z";
  CHECK(!store.value().record(noEvidence, bare).hasValue());
  // Nothing was written, because every attempt was refused before the store was touched.
  CHECK(store.value().size() == 0U);
  CHECK(!std::filesystem::exists(root / "decisions.json"));
}

TEST_CASE("A corrupt review store is reported rather than replaced") {
  const auto root = test::support::temporaryDirectory("procedural-review-store-corrupt");
  const auto statePath = root / "decisions.json";
  std::filesystem::create_directories(root);
  // A store is the only record of an approval, so damage must surface instead of being overwritten.
  std::ofstream(statePath, std::ios::binary | std::ios::trunc) << "{ not json";
  const auto opened = distribution::ProceduralReviewStore::open(statePath);
  CHECK(!opened.hasValue());
  CHECK(std::filesystem::exists(statePath));
  // A store claiming another format is refused even when it is valid JSON.
  std::ofstream(statePath, std::ios::binary | std::ios::trunc)
      << R"({\"formatId\":\"com.project-seam.procedural-singer\",\"schemaVersion\":1,\"decisions\":[]})";
  CHECK(!distribution::ProceduralReviewStore::open(statePath).hasValue());
  // An unrecognised decision kind must not be read as a rejection, which would discard an approval.
  std::ofstream(statePath, std::ios::binary | std::ios::trunc) << R"({\"formatId\":\""
      << "com.project-seam.procedural-review-store\" << R"(\",\"schemaVersion\":1,\"decisions\":[{\"reviewId\":\"r\",\"candidateId\":\"c\",\"basisDigest\":\""
      << std::string(64U, 'a') << R"(\",\"kind\":\"maybe\",\"reviewerId\":\"p\",\"reviewedAtUtc\":\"t\"}]})";
  CHECK(!distribution::ProceduralReviewStore::open(statePath).hasValue());
  // An empty path is a conflict rather than a silent no-op.
  CHECK(!distribution::ProceduralReviewStore::open(std::filesystem::path{}).hasValue());
}

// The store's read-modify-write must not lose a decision when two writers race. A lost approval is
// silent, so it is tested directly rather than assumed from the lock's presence.
TEST_CASE("Concurrent review decisions are all retained rather than lost") {
  const auto root = test::support::temporaryDirectory("procedural-review-store-race");
  const auto statePath = root / "decisions.json";
  const auto fixture = makeFixture("procedural-review-store-race-fixture");
  constexpr std::size_t kWriters = 8U;
  std::atomic<std::size_t> accepted{0U};
  std::atomic<std::size_t> refused{0U};
  std::vector<std::thread> threads;
  threads.reserve(kWriters);
  for (std::size_t index = 0U; index < kWriters; ++index) {
    threads.emplace_back([&, index] {
      auto store = distribution::ProceduralReviewStore::open(statePath);
      if (!store) {
        ++refused;
        return;
      }
      auto decision = decisionFor(fixture.candidate, "review-" + std::to_string(index),
                                  distribution::ProceduralReviewDecisionKind::Accept,
                                  "2026-09-15T10:00:0" + std::to_string(index) + "Z");
      if (store.value().record(fixture.candidate, decision)) {
        ++accepted;
      } else {
        ++refused;
      }
    });
  }
  for (auto& thread : threads) thread.join();
  // At least one writer must succeed, and every success must be present afterwards. A store that
  // lost an update would report fewer decisions than the number of successful records.
  CHECK(accepted.load() >= 1U);
  auto reopened = distribution::ProceduralReviewStore::open(statePath);
  CHECK(reopened.hasValue());
  if (!reopened) return;
  const auto receipt = reopened.value().resolve(fixture.candidate);
  CHECK(receipt.hasValue());
  if (!receipt) return;
  CHECK(receipt.value().current.size() == accepted.load());
  CHECK(accepted.load() + refused.load() == kWriters);
}

