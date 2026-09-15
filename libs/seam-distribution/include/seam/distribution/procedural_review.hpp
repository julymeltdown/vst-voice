#pragma once

#include "seam/distribution/procedural_package.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seam::distribution {

// A review decision is evidence about one exact rendering of one exact recipe. It is not a property
// of a resource id, because a resource id survives edits that change what the singer sounds like.
// Everything a decision depends on is therefore named here, and the basis digest is what a stored
// decision is compared against.
//
// Signing proves authenticity. This proves what a person reviewed, and nothing about musical quality
// beyond the decision they recorded.
struct ProceduralReviewBasis final {
  static constexpr std::int32_t kSchemaVersion = 1;
  static constexpr std::string_view kFormatId = "com.project-seam.procedural-review-basis";

  std::string resourceId;
  std::string version;
  // The digest of the recipe's canonical encoding, which is the identity the renderer validates.
  std::string recipeSha256;
  // The installed content hash, so a decision cannot outlive a replacement of the installed bytes.
  std::string contentHash;
  std::string engineId;
  std::uint32_t engineRevision{0U};
  std::string renderAbi;
  std::uint32_t compilerRevision{0U};
  std::uint32_t sampleRate{0U};
  // The evidence the review was performed against. Empty means the review had no such evidence,
  // which is a fact the candidate must state rather than leave ambiguous.
  std::string scoreSha256;
  std::string audioSha256;
  // Canonical settings digest, so a decision taken at one render setting does not cover another.
  std::string settingsDigest;

  [[nodiscard]] core::Result<void> validate() const;
  // Over the canonical encoding of the fields above, the same way a manifest binds the canonical
  // recipe rather than raw bytes.
  [[nodiscard]] core::Result<std::string> digest() const;
  friend bool operator==(const ProceduralReviewBasis&, const ProceduralReviewBasis&) = default;
};

class ProceduralReviewBasisJsonCodec final {
public:
  [[nodiscard]] core::Result<std::string> encode(const ProceduralReviewBasis& basis) const;
  [[nodiscard]] core::Result<ProceduralReviewBasis> decode(std::string_view json) const;
};

// Names the fields that differ between two bases, so a stale decision says why it is stale.
[[nodiscard]] std::vector<std::string> proceduralReviewBasisDifferences(
    const ProceduralReviewBasis& recorded, const ProceduralReviewBasis& current);

// An exact candidate: identity plus the basis it is frozen at. Freezing is read-only over signed
// content; it never mutates the recipe or the installed resource.
struct ProceduralReviewCandidate final {
  std::string candidateId;
  ProceduralSingerManifest manifest;
  ProceduralReviewBasis basis;
};

enum class ProceduralReviewDecisionKind { Accept, Reject };

[[nodiscard]] std::string_view proceduralReviewDecisionName(
    ProceduralReviewDecisionKind kind) noexcept;

struct ProceduralReviewDecision final {
  std::string reviewId;
  std::string candidateId;
  // The basis the reviewer actually reviewed. A decision whose recorded digest is not the
  // candidate's current digest is stale and must not be treated as an approval.
  std::string basisDigest;
  // The basis itself, so a stale decision can name which fields changed instead of only reporting
  // that something did. Optional so a caller that stores only the digest still compares correctly,
  // but a decision without it can only say `basisDigest`.
  std::optional<ProceduralReviewBasis> recordedBasis;
  ProceduralReviewDecisionKind kind{ProceduralReviewDecisionKind::Reject};
  std::string reviewerId;
  std::string reviewedAtUtc;
  std::string note;
};

struct ProceduralReviewedDecision final {
  ProceduralReviewDecision decision;
  std::vector<std::string> differences;
};

struct ProceduralReviewReceipt final {
  std::string candidateId;
  std::string basisDigest;
  // Decisions that still cover this candidate, in record order.
  std::vector<ProceduralReviewDecision> current;
  // Decisions that were recorded against a different basis, with the differing field names.
  std::vector<ProceduralReviewedDecision> stale;

  [[nodiscard]] bool accepted() const noexcept;
  [[nodiscard]] bool reject() const noexcept;
};

// Freezes the supplied evidence against an exact identity, hashing the evidence rather than trusting
// a supplied digest. A missing or oversized evidence file is refused.
[[nodiscard]] core::Result<ProceduralReviewBasis> freezeProceduralReviewBasis(
    const ProceduralSingerManifest& manifest,
    std::string_view contentHash,
    std::string_view renderAbi,
    std::uint32_t compilerRevision,
    std::uint32_t sampleRate,
    const std::filesystem::path& scorePath,
    const std::filesystem::path& audioPath,
    std::string_view settingsDigest);

// Resolves decisions against a candidate. A decision whose basis digest differs is reported as stale
// with its differing fields rather than silently inherited.
[[nodiscard]] core::Result<ProceduralReviewReceipt> resolveProceduralReviewDecisions(
    const ProceduralReviewCandidate& candidate,
    const std::vector<ProceduralReviewDecision>& decisions);

// Refuses a decision whose recorded digest does not match the basis it claims to describe, so a
// caller cannot manufacture an approval by writing the candidate's digest next to an older review.
[[nodiscard]] core::Result<void> recordProceduralReviewDecision(
    const ProceduralReviewCandidate& candidate,
    const ProceduralReviewDecision& decision);

}  // namespace seam::distribution
