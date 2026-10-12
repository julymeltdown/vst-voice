#pragma once

#include "seam/core/result.hpp"
#include "seam/formats/json_value.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace seam::voicebank_production::history_internal {

// New writes require diagnostics for the reviewed active audio. Unmarked
// historical journals retain the earlier raw-receipt admission rule in replay.
inline constexpr std::int64_t kCurrentReviewInspectionPolicyVersion{2};

// Journal-only attempts are not committed generations. Their exact bytes are
// retained and named by the next committed journal, alongside its real parent.
// No missing committed snapshot may be silently reclassified during traversal.
struct AbortedGeneration final {
  std::uint64_t generation{};
  std::string journalSha256;
  std::uint64_t journalBytes{};
};
struct Ancestry final {
  bool recorded{false};
  std::uint64_t parentGeneration{};
  std::vector<AbortedGeneration> aborted;
};

[[nodiscard]] std::string generationFilename(std::uint64_t generation);
// Recognize the policy before interpreting fields owned by that policy.
[[nodiscard]] core::Result<void> validateInspectionPolicy(const formats::JsonValue& journal);
[[nodiscard]] core::Result<formats::JsonValue> captureAncestry(
    const std::filesystem::path& root, std::string_view projectId,
    std::uint64_t parentGeneration, std::uint64_t generation);
[[nodiscard]] core::Result<Ancestry> verifyAncestry(
    const std::filesystem::path& root, const formats::JsonValue& journal,
    std::string_view projectId, std::uint64_t generation);

}  // namespace seam::voicebank_production::history_internal
