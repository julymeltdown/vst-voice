#pragma once

#include "seam/domain/phoneme.hpp"

#include <optional>
#include <span>
#include <vector>

namespace seam::phonemizer {

enum class EditCorrespondence { Matched, MissingOriginal, RemovedOrAmbiguous };

struct ReconciledPhonemeEdit final {
  domain::PhonemeOverride original;
  std::optional<domain::PhonemeOverride> rebound;
  EditCorrespondence correspondence{EditCorrespondence::RemovedOrAmbiguous};
};

// A correspondence proposal, not permission to apply an edit. The caller must
// supply unedited base tokens from verified compatible resolver/resource contexts.
// Every input edit is retained. Only matches common to ALL optimal ordered
// alignments may be rebound; repeated sounds never use a first-match heuristic.
// Inputs are one note, contiguous ordinals, at most 256 tokens/edits each.
[[nodiscard]] core::Result<std::vector<ReconciledPhonemeEdit>> reconcilePhonemeOverrides(
    domain::NoteId noteId,
    std::span<const domain::PhonemeToken> before,
    std::span<const domain::PhonemeToken> after,
    std::span<const domain::PhonemeOverride> edits);

// Unit spans require every constituent token to survive contiguously. A seam
// within a note can use the two-token span around its boundary; matching only
// its incoming token does not establish that the join still means the same thing.
// A valid but missing/ambiguous/noncontiguous span returns nullopt, not a guess.
[[nodiscard]] core::Result<std::optional<domain::PhonemeKey>> reconcilePhonemeSpan(
    domain::NoteId noteId,
    std::span<const domain::PhonemeToken> before,
    std::span<const domain::PhonemeToken> after,
    domain::PhonemeKey start, std::uint16_t tokenCount);

class RegionPhonemeCorrespondence final {
public:
  [[nodiscard]] std::optional<domain::PhonemeKey> mapSpan(
      domain::PhonemeKey start, std::uint16_t count) const;
  [[nodiscard]] std::optional<domain::PhonemeKey> mapBoundary(
      domain::PhonemeKey incoming) const;
private:
  friend core::Result<RegionPhonemeCorrespondence> reconcileRegionPhonemes(
      std::span<const domain::PhonemeToken>, std::span<const domain::PhonemeToken>);
  std::vector<domain::PhonemeKey> before_;
  std::vector<domain::PhonemeKey> after_;
  std::vector<std::optional<std::size_t>> targets_;
};

// Bounded region stream: <=4096 tokens, contiguous note groups, <=256 per note.
// Each note is aligned once; spans and joins then reuse that correspondence.
// Note identity is preserved even when neighboring notes share the same sounds.
[[nodiscard]] core::Result<RegionPhonemeCorrespondence> reconcileRegionPhonemes(
    std::span<const domain::PhonemeToken> before,
    std::span<const domain::PhonemeToken> after);

}  // namespace seam::phonemizer
