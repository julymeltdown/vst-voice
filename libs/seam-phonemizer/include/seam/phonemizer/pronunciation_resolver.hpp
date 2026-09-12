#pragma once

#include "seam/phonemizer/phonemizer.hpp"
#include <stop_token>

namespace seam::phonemizer {

// Region-wide resolution keeps phrase context for creator batches. Text and
// override budgets remain independent; these are not renderer admission limits.
inline constexpr std::size_t kMaximumPronunciationNotes = 10000U;
inline constexpr std::size_t kMaximumPronunciationTokens = 65536U;

struct ResolvedPronunciation final {
  domain::PronunciationIdentity identity;
  Result pronunciation;
};

// Identity for the bundled Japanese source/rule implementation, not a singer,
// external dictionary, compiled binary attestation or durable edit authorization.
[[nodiscard]] core::Result<ResolvedPronunciation> resolveJapanesePronunciation(
    const domain::VocalRegion& region, std::stop_token stop = {});

[[nodiscard]] std::string pronunciationSequenceHash(
    std::span<const domain::PhonemeToken> tokens);

// UI adapter: retain normal warnings; expose bounded resolution failure instead
// of silently falling back to an unbounded or differently configured engine.
[[nodiscard]] Result inspectJapanesePronunciation(const domain::VocalRegion& region);

// Use unedited base resolution. Also addresses explicit appended-token slots
// relative to that note's base sequence; absence means no verifiable owner.
[[nodiscard]] std::optional<std::string> phonemeEditContextId(
    const ResolvedPronunciation& base, domain::PhonemeKey key);

// Re-address verified copied bindings after explicit note/region ID changes.
// Stale, unresolved or changed-sound bindings are retained, never promoted.
[[nodiscard]] core::Result<void> rebindTransferredPhonemeContexts(
    const domain::VocalRegion& source, domain::VocalRegion& destination,
    std::span<const domain::PerformanceNoteRemap> mapping);

// Validate copied unit spans and both sides of copied seams after phoneme transfer.
[[nodiscard]] core::Result<void> validateTransferredRenderEdits(
    const domain::VocalRegion& source, domain::VocalRegion& destination,
    std::span<const domain::PerformanceNoteRemap> mapping);

}  // namespace seam::phonemizer
