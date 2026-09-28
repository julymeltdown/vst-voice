#pragma once

#include "seam/phonemizer/english_resource.hpp"
#include "seam/phonemizer/phonemizer.hpp"

#include <limits>
#include <stop_token>
#include <utility>

namespace seam::phonemizer {

// Explicit note hints are final user input: they replace the note's generated
// phones and are never labelled as dictionary readings or estimates. Optional
// spaced '.' separators delimit single-nucleus hint syllables;
// parseEnglishPhoneHint returns only their literal phones.
//
// A lyric's authored reading hint (domain::LyricToken::readingHint) is explicit
// input for the whole lyric in the same phone syntax. It precedes exceptions,
// the dictionary, derivations and estimates, is distributed across shared-lyric
// notes like a dictionary reading and is never estimated. A non-ASCII, invalid
// or pause-bearing reading hint yields a pause and an UnsupportedCharacter
// warning; the surface is not used in its place.
//
// Lyric text is normalized (typographic apostrophes, quotes, dashes and
// Latin-1 accented letters) without changing the stored surface, split into at
// most 32 words, and each word resolves through the English resource. Derived
// and spelling-estimated words, and vowel continuations of them, carry
// EstimatedPronunciation warnings that name the word. Nucleus stress is kept.
//
// Notes that continue a shared lyric (domain::continuesSharedLyric) form one
// word group. Its syllables are distributed in order, one per note; the last
// syllable's vowel is sustained over any extra notes and its coda closes the
// final note; surplus syllables share the final note. A note with its own hint
// replaces only its own share. Codas never migrate between separately authored
// lyrics; a literal "-"/"~" continuation needs an adjacent, unambiguous prior
// note.

[[nodiscard]] core::Result<std::vector<std::string>> parseEnglishPhoneHint(
    std::string_view text);

class EnglishPhonemizer final : public IPhonemizer {
public:
  EnglishPhonemizer() : resource_(EnglishPronunciationResource::builtin()) {}
  explicit EnglishPhonemizer(EnglishPronunciationResource resource)
      : resource_(std::move(resource)) {}

  [[nodiscard]] domain::Language language() const noexcept override {
    return domain::Language::English;
  }

  [[nodiscard]] Result phonemize(const domain::VocalRegion& region) const override;
  [[nodiscard]] core::Result<Result> phonemize(
      const domain::VocalRegion& region, std::stop_token stop,
      std::size_t maximumTokens = std::numeric_limits<std::size_t>::max()) const;

private:
  EnglishPronunciationResource resource_;
};

}  // namespace seam::phonemizer
