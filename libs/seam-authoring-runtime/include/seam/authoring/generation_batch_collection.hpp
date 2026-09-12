#pragma once
#include "seam/authoring/generation_job.hpp"
namespace seam::authoring {
// Retain the original producer and job references across retries. A failure may
// follow a committed import; retry this operation, never reprepare expectations.
// Recovered commits return durabilityConfirmed=false: read-only recognition
// cannot reattest producer pointer fsync. Receipt-file publication is durable,
// but a campaign must reconcile producer pointer durability before advancing.
[[nodiscard]] core::Result<voicebank_production::ProductionCommitReceipt> collectGenerationBatchWithReceipt(
    const voicebank_production::ProductionProjectRepository& repository,
    const voicebank_production::VoicebankProductionProject& originalProducer,
    std::span<const GenerationJobReference> jobs, const std::filesystem::path& receiptPath,
    const voicebank_production::ProductionJournalEvent& event,
    GenerationBatchLimits limits = {}, std::stop_token stop = {},
    std::function<bool()> interruptBeforeReceipt = {});
}
