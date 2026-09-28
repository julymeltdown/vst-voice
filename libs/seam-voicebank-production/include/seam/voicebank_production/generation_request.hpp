#pragma once

#include "seam/core/result.hpp"
#include "seam/voicebank_production/project.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace seam::voicebank_production {

// A generation request submitted to one producer workspace. It is immutable queue evidence: the
// exact producer generation it was captured against, the recipe, every requested assignment with
// its job ID, duration, style and pitch, and the resource budgets. Submitting it never reserves an
// assignment or grants review; results reach the producer only through the canonical repository
// writer, and only while the request's own expectation is still the current producer state.
struct GenerationRequestJob final {
  std::string jobId, takeId, style, coverageKey;
  std::int32_t pitchLayer{0};
  std::int64_t frameCount{0};
  std::int64_t batchIndex{0};
  friend bool operator==(const GenerationRequestJob&, const GenerationRequestJob&) = default;
};
struct GenerationRequestBudget final {
  std::int64_t maximumJobs{0}, maximumFrames{0}, maximumBytes{0};
  std::int64_t batchMaximumJobs{0}, batchMaximumFrames{0};
  friend bool operator==(const GenerationRequestBudget&, const GenerationRequestBudget&) = default;
};
struct GenerationRequest final {
  // SHA-256 of the immutable worker definition (for campaigns, the campaign definition).
  std::string requestId;
  // Where the worker definition was found at submission. A locator, never identity.
  std::string definitionLocator;
  std::int64_t expectedGeneration{0};
  std::string expectedProjectSha256;
  std::string language, recipeId, recipeVersion, recipeHash;
  GenerationRequestBudget budget;
  std::vector<GenerationRequestJob> jobs;
  std::string submittedBy, submittedAtUtc;
  friend bool operator==(const GenerationRequest&, const GenerationRequest&) = default;
};

// Terminal outcomes only. A cancelled invocation is not terminal: its retained work stays resumable.
enum class GenerationRequestOutcome { Completed, Stale, BudgetExhausted };
struct GenerationRequestTerminal final {
  GenerationRequestOutcome outcome{GenerationRequestOutcome::Stale};
  // The producer state the outcome was observed against. For COMPLETED this is the generation
  // the final batch committed, not whatever review or edit followed it.
  std::int64_t observedGeneration{0};
  std::string observedProjectSha256;
  // Batches whose collection is proven by the history that follows the expected generation.
  std::int64_t completedBatches{0};
  // Retained worker bytes; nonzero only for BUDGET_EXHAUSTED.
  std::int64_t retainedBytes{0};
  std::string detail, recordedBy, recordedAtUtc;
  friend bool operator==(const GenerationRequestTerminal&, const GenerationRequestTerminal&) = default;
};
struct GenerationRequestRecord final {
  GenerationRequest request;
  std::string requestSha256;
  std::optional<GenerationRequestTerminal> terminal;
  [[nodiscard]] std::int64_t batchCount() const noexcept {
    return request.jobs.empty() ? 0 : request.jobs.back().batchIndex + 1;
  }
};

[[nodiscard]] std::string toString(GenerationRequestOutcome value);
[[nodiscard]] core::Result<std::string> encodeGenerationRequest(const GenerationRequest& request);
[[nodiscard]] core::Result<std::string> encodeGenerationRequestTerminal(
    const GenerationRequestTerminal& terminal, std::string_view requestId, std::string_view requestSha256);

// Workspace-owned registry under <workspace>/generation-requests/<requestId>/. Each request has one
// create-new request.json and at most one create-new terminal.json; neither is ever rewritten.
// Every read re-verifies both records against the producer's durable history. These are integrity
// and consistency records, not signatures, approvals or release evidence.
class GenerationRequestRegistry final {
public:
  explicit GenerationRequestRegistry(std::filesystem::path workspaceRoot);
  // Admits a request only against the exact current producer generation. Retrying the identical
  // request returns the retained record; different content under the same ID is a conflict.
  [[nodiscard]] core::Result<GenerationRequestRecord> submit(
      const GenerationRequest& request, std::stop_token stop = {}) const;
  [[nodiscard]] core::Result<std::optional<GenerationRequestRecord>> find(
      std::string_view requestId, std::stop_token stop = {}) const;
  // Every retained request, ordered by submission time and then ID.
  [[nodiscard]] core::Result<std::vector<GenerationRequestRecord>> list(std::stop_token stop = {}) const;
  // Records the single terminal outcome. COMPLETED must be proven by exactly one import-generated-batch
  // generation per batch, each introducing that batch's takes unapproved; STALE must observe a
  // different producer; BUDGET_EXHAUSTED must exceed the admitted byte budget. An identical retry
  // returns the retained record; a different outcome is a conflict.
  [[nodiscard]] core::Result<GenerationRequestRecord> recordTerminal(
      std::string_view requestId, const GenerationRequestTerminal& terminal, std::stop_token stop = {}) const;

private:
  std::filesystem::path root_;
};

}  // namespace seam::voicebank_production
