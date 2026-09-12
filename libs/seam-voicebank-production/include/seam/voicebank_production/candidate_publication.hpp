#pragma once

#include "seam/voicebank/voicebank.hpp"
#include "seam/voicebank_production/project.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>

namespace seam::voicebank_production {

inline constexpr std::string_view kSampleCandidateReviewKind = "sample-candidate-review-v2";

struct SampleCandidateUnitBinding final {
  std::string unitId;
  std::string takeId;
  std::string audioSha256;
  std::string reviewId;
  std::string reviewMetadataRevisionId;
};

struct SampleCandidateRequest final {
  std::uint64_t expectedGeneration{0U};
  std::string expectedProjectSha256;
  voicebank::Manifest manifest;
  std::vector<SampleCandidateUnitBinding> units;
};

enum class SampleCandidateReviewDecision { Accept, Reject };

struct SampleCandidateReviewUnit final {
  std::string unitId, takeId, audioSha256;
  std::string originOperatorId, originJournalSha256;
  std::uint64_t originGeneration{0U};
  std::uint32_t sampleRate{0U};
  std::uint64_t frameCount{0U};
  double peak{0.0}, rms{0.0};
};

struct SampleCandidateReviewPacket final {
  std::string projectId;
  std::uint64_t sourceGeneration{0U};
  std::string sourceProjectSha256;
  std::string reviewBasisSha256;
  voicebank::Manifest manifest;
  std::vector<SampleCandidateReviewUnit> units;
  std::string packetSha256;
};

struct SampleCandidateReviewReceipt final {
  std::uint64_t committedGeneration{0U};
  std::string committedProjectSha256;
  std::vector<ReviewRecord> reviews;
  // Present only when every current unit has applicable accepted review.
  std::optional<SampleCandidateRequest> candidate;
  // A returned receipt always represents a committed decision, including a
  // recoverable exact generation after a pointer-write/sync failure.
  bool durabilityConfirmed{true};
  std::string diagnostic;
};

// Read-only capture of current material. It never changes approval state.
// The manifest is a caller-owned edited model; paths are replaced with the
// current assignment's content-addressed audio before review is presented.
// A subset may be reviewed while other assignments still await recordings;
// full assignment coverage remains mandatory for candidate publication.
[[nodiscard]] core::Result<SampleCandidateReviewPacket> prepareSampleCandidateReview(
    const std::filesystem::path& repositoryRoot,
    const VoicebankProductionProject& project,
    const voicebank::Manifest& manifest,
    std::stop_token stop = {});

// One durable save applies an explicit independent review decision to all
// packet units, or to the listed unit IDs. Other units and history are retained.
// Cancellation/stale input rejects without changing caller state or approvals.
[[nodiscard]] core::Result<SampleCandidateReviewReceipt> commitSampleCandidateReview(
    const std::filesystem::path& repositoryRoot,
    VoicebankProductionProject& project,
    const SampleCandidateReviewPacket& packet,
    std::string reviewerId,
    std::string reviewedAtUtc,
    SampleCandidateReviewDecision decision,
    std::span<const std::string> unitIds = {},
    std::stop_token stop = {});

// Hash-bound, bounded JSON interchange; file I/O belongs to CLI/native owners.
[[nodiscard]] core::Result<std::string> encodeSampleCandidateReviewPacket(const SampleCandidateReviewPacket& packet);
[[nodiscard]] core::Result<SampleCandidateReviewPacket> decodeSampleCandidateReviewPacket(std::string_view json);

// Reconstructs a request from current persisted approvals; never accepts,
// rebinds, or refreshes stale material. Suitable for a later CLI publish action.
[[nodiscard]] core::Result<SampleCandidateRequest> resolveReviewedSampleCandidate(
    const std::filesystem::path& repositoryRoot,
    const VoicebankProductionProject& project,
    const voicebank::Manifest& editedManifest,
    std::stop_token stop = {});

enum class CandidatePublicationStage { AudioStaged, BeforeCommit, AfterCommitBeforeParentSync };
struct CandidatePublicationOptions final {
  std::uint64_t maximumAudioBytes{256ULL * 1024ULL * 1024ULL};
  // Cannot approve input or replace final resource verification. A failure at
  // AfterCommitBeforeParentSync returns a committed, durability-uncertain value.
  std::function<core::Result<void>(CandidatePublicationStage)> faultInjector;
};

struct PublishedSampleCandidate final {
  std::filesystem::path root;
  std::string manifestSha256;
  std::string contentSha256;
  std::string candidateSha256;
  std::uint64_t sourceGeneration{0U};
  bool releaseEligible{false};
  bool durabilityConfirmed{true};
  std::string diagnostic;
};

// Pure review material, not an approval operation. A reviewer must inspect it,
// retain a PASS ReviewRecord, and persist these exact values in an independently
// attributed MetadataRevision of kSampleCandidateReviewKind. Any subsequent
// source, processing, annotation, assignment, policy or manifest edit changes it.
[[nodiscard]] core::Result<std::map<std::string, std::string, std::less<>>>
sampleCandidateReviewValues(const VoicebankProductionProject& project,
                           const voicebank::Manifest& manifest,
                           const SampleCandidateUnitBinding& binding);

// Publishes a new directory from one already reviewed durable generation.
// Never updates the producer, overwrites a candidate, signs a package, installs
// a bank, or grants release qualification. The current producer schema has no
// per-style assignment identity, so multi-style publication explicitly rejects.
// A value always means committed. If durabilityConfirmed is false, inspect the
// returned destination and exact hashes before any retry; do not regenerate it.
[[nodiscard]] core::Result<PublishedSampleCandidate> publishSampleCandidate(
    const std::filesystem::path& repositoryRoot,
    const VoicebankProductionProject& project,
    const SampleCandidateRequest& request,
    const std::filesystem::path& destination,
    const CandidatePublicationOptions& options = {},
    std::stop_token stop = {});

}  // namespace seam::voicebank_production
