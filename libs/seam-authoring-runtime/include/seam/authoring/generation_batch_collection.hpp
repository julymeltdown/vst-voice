#pragma once
#include "seam/authoring/generation_job.hpp"
namespace seam::authoring {
// Retain the original producer and job references across retries. A failure may
// follow a committed import; retry this operation, never reprepare expectations.
// Recovered commits reconcile the exact current pointer under the repository
// writer lock before publishing the receipt; reconciliation never adds history.
[[nodiscard]] core::Result<voicebank_production::ProductionCommitReceipt> collectGenerationBatchWithReceipt(
    const voicebank_production::ProductionProjectRepository& repository,
    const voicebank_production::VoicebankProductionProject& originalProducer,
    std::span<const GenerationJobReference> jobs, const std::filesystem::path& receiptPath,
    const voicebank_production::ProductionJournalEvent& event,
    GenerationBatchLimits limits = {}, std::stop_token stop = {},
    std::function<bool()> interruptBeforeReceipt = {});
}
