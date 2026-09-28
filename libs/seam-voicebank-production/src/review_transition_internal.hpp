#pragma once

#include "seam/core/result.hpp"
#include "seam/voicebank_production/project.hpp"

namespace seam::voicebank_production::review_internal {

// The review rules between the durable generation and the proposed one, enforced by the single
// durable append path so that no caller, command or generic save can bypass them:
//  - history is append-only: reviews, review material, annotations and receipts, processing
//    revisions, stored assets, and each take's identity and processing chain are never removed or
//    rewritten, so regeneration and retakes keep earlier manual work;
//  - review status (marker review or approval) is granted only by a "review" event that appends,
//    for each decided take, an independent decision and the material it was made on, bound to
//    this generation's audio and review basis. Imports, generation, edits and saves never grant it;
//  - an approval carried forward is lowered to marker review when the material its decision is
//    bound to changed, so a persisted approval always names a review that still applies.
// "current" is null for a new producer, which starts unreviewed. Never grants anything itself;
// the only change it makes to "proposed" is lowering stale approvals.
[[nodiscard]] core::Result<void> applyReviewTransition(
    const VoicebankProductionProject* current, VoicebankProductionProject& proposed,
    const ProductionJournalEvent& event, bool allowStyleOwnershipTransition);

}  // namespace seam::voicebank_production::review_internal

