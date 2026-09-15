#pragma once

#include "seam/distribution/procedural_review.hpp"
#include "seam/core/result.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace seam::distribution {

// A durable store of procedural review decisions for one installation. Decisions are append-only and
// keyed by the exact basis they were made about, so a store can never silently rewrite what a reviewer
// approved: a changed recipe is a new basis and therefore a new decision, and the previous one is
// reported as stale rather than edited. This stores evidence of a decision; it does not make one.
class ProceduralReviewStore final {
public:
  // The store is a single JSON document. An empty path is a conflict rather than a silent no-op,
  // because a surface that believes it is recording decisions must not be doing nothing.
  [[nodiscard]] static core::Result<ProceduralReviewStore> open(std::filesystem::path statePath,
                                                                std::size_t maximumDecisions = 4096U);

  // Records a decision after checking it against the candidate. Every rule that makes a decision
  // meaningful is enforced here, so a caller cannot store an approval it did not earn.
  [[nodiscard]] core::Result<void> record(const ProceduralReviewCandidate& candidate,
                                          const ProceduralReviewDecision& decision);

  [[nodiscard]] core::Result<std::vector<ProceduralReviewDecision>> decisionsFor(
      std::string_view candidateId) const;

  // Resolves the stored decisions for a candidate, reporting which are current and which are stale.
  [[nodiscard]] core::Result<ProceduralReviewReceipt> resolve(
      const ProceduralReviewCandidate& candidate) const;

  [[nodiscard]] const std::filesystem::path& statePath() const noexcept { return statePath_; }
  [[nodiscard]] std::size_t size() const;

private:
  ProceduralReviewStore(std::filesystem::path statePath, std::size_t maximumDecisions);
  [[nodiscard]] core::Result<std::vector<ProceduralReviewDecision>> load() const;
  [[nodiscard]] core::Result<void> persist(
      const std::vector<ProceduralReviewDecision>& decisions) const;

  std::filesystem::path statePath_;
  std::size_t maximumDecisions_{4096U};
};

}  // namespace seam::distribution

