#pragma once

#include "seam/phonemizer/phonemizer.hpp"

#include <limits>
#include <stop_token>

namespace seam::phonemizer {

// Out-of-lexicon spelling estimates carry EstimatedPronunciation warnings,
// including vowel continuations derived from them. Explicit hints are final
// user input and are not labelled as dictionary or spelling estimates.
// Dictionary or explicit hint syllable boundaries override a bounded legal-
// onset inference rule. Optional spaced '.' separators delimit single-nucleus
// hint syllables; parseEnglishPhoneHint returns only their literal phones.
// Nucleus stress is retained. Codas never migrate between authored notes;
// vowel continuation requires an adjacent, unambiguous prior note.

[[nodiscard]] core::Result<std::vector<std::string>> parseEnglishPhoneHint(
    std::string_view text);

class EnglishPhonemizer final : public IPhonemizer {
public:
  [[nodiscard]] domain::Language language() const noexcept override {
    return domain::Language::English;
  }

  [[nodiscard]] Result phonemize(const domain::VocalRegion& region) const override;
  [[nodiscard]] core::Result<Result> phonemize(
      const domain::VocalRegion& region, std::stop_token stop,
      std::size_t maximumTokens = std::numeric_limits<std::size_t>::max()) const;
};

}  // namespace seam::phonemizer
