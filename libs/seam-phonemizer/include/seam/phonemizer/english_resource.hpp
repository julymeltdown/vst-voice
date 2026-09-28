#pragma once

#include "seam/phonemizer/pronunciation_resolver.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace seam::phonemizer {

// Frozen English phone vocabulary. It lists every symbol the English service
// can emit and every symbol an English hint, exception entry or phoneme edit
// may use: 24 consonants, 17 vowels in bare/0/1/2 lexical-stress forms, and
// "pau". A bank, recipe or model built for English declares its coverage
// against this vocabulary; English resolution never borrows another
// language's symbols. Changing the list requires a new identifier.
inline constexpr std::string_view kEnglishVocabularyId = "seam-en-arpabet-v1";
[[nodiscard]] std::span<const std::string_view> englishVocabulary() noexcept;
[[nodiscard]] bool isEnglishVocabularySymbol(std::string_view symbol) noexcept;
// SHA-256 of the identifier and the ordered symbol list, one per line.
[[nodiscard]] const std::string& englishVocabularySha256();

// Rule revisions whose behavior shapes resolved English output. They are
// recorded in the resource manifest and bound into the builtin resource hash
// through the implementation sources.
inline constexpr std::string_view kEnglishNormalizationRule = "seam-en-normalization-v1";
inline constexpr std::string_view kEnglishSyllabificationRule = "seam-en-legal-onset-v1";
inline constexpr std::string_view kEnglishNoteDistributionRule = "seam-en-note-distribution-v1";
inline constexpr std::string_view kEnglishDerivationRule = "seam-en-derivation-v1";
inline constexpr std::string_view kEnglishSpellingRule = "seam-en-spelling-v1";
inline constexpr std::string_view kEnglishExceptionOverlayRule = "seam-en-exception-overlay-v1";

// Literal phones plus optional explicit syllable boundaries. A boundary names
// the first phone of the following syllable; an empty list means boundaries
// are inferred by the legal-onset rule.
struct EnglishPhoneReading final {
  std::vector<std::string> phones;
  std::vector<std::size_t> syllableBreaks;

  friend bool operator==(const EnglishPhoneReading&, const EnglishPhoneReading&) = default;
};

// English hint syntax: at most 256 space-separated vocabulary symbols with
// optional spaced "." separators; every explicit syllable has exactly one
// vowel and no pause.
[[nodiscard]] core::Result<EnglishPhoneReading> parseEnglishPhoneReading(std::string_view text);

enum class EnglishReadingSource {
  Exception,         // SEAM exception table entry
  Dictionary,        // pinned CMUdict entry (first listed variant)
  Derived,           // regular rule applied to a lexical entry; flagged for review
  SpellingEstimate,  // letter-pattern estimate; flagged for review
};

struct EnglishWordReading final {
  EnglishPhoneReading reading;
  EnglishReadingSource source{EnglishReadingSource::Dictionary};
  // Lexical entry a derived reading came from; empty otherwise.
  std::string basis;
};

// Versioned English pronunciation resource: the pinned CMUdict revision, the
// SEAM exception table and the derivation/spelling rules. Resolution order for
// one lowercase ASCII word is exception, dictionary, derivation, then spelling
// estimate. Lexical success is not native-speaker or singing qualification.
class EnglishPronunciationResource final {
public:
  // Bundled resource. Its hash binds the embedded dictionary and exception
  // bytes plus the resolver sources.
  [[nodiscard]] static const EnglishPronunciationResource& builtin();

  // Layers exception entries over this resource and returns a resource with a
  // new identity, so dependent pronunciation, performance and render requests
  // see the change. Lines are "word<TAB>phones" in English hint syntax; blank
  // lines and '#' comments are ignored. At most 4096 entries and 1 MiB; a
  // repeated word, invalid word or invalid reading rejects the whole table.
  [[nodiscard]] core::Result<EnglishPronunciationResource> withExceptions(
      std::string_view table) const;

  [[nodiscard]] const std::string& resourceHash() const noexcept;
  // Fails when the bundled exception table could not be admitted; resolution
  // then fails closed with that error.
  [[nodiscard]] core::Result<void> status() const;
  // Exception or dictionary reading only; no derivation or estimate.
  [[nodiscard]] std::optional<EnglishWordReading> lexicalReading(std::string_view word) const;
  // Complete bounded resolution of one lowercase ASCII word (letters, digits,
  // apostrophes). Returns nullopt when no reading or estimate exists.
  [[nodiscard]] std::optional<EnglishWordReading> resolveWord(std::string_view word) const;

private:
  struct State;
  explicit EnglishPronunciationResource(std::shared_ptr<const State> state);
  std::shared_ptr<const State> state_;
};

// Provenance digests of the bundled bytes, computed from the embedded copies.
// They must equal the resource manifest and third-party intake records.
[[nodiscard]] const std::string& englishLexiconSha256();
[[nodiscard]] const std::string& englishExceptionsSha256();

// Shared-resolver English service with an explicit resource. The builtin
// resource yields the same identity as resolveEnglishPronunciation(region).
[[nodiscard]] core::Result<ResolvedPronunciation> resolveEnglishPronunciation(
    const domain::VocalRegion& region, const EnglishPronunciationResource& resource,
    std::stop_token stop = {});

// Coverage of resolved English phones by a bank, recipe or model vocabulary.
// Exact requires the resolved symbol itself. FoldLexicalStress is an explicit
// declaration that the resource does not model lexical stress: a vowel is
// then required in its bare form ("ow1" -> "ow"). Nothing else is mapped, and
// a missing phone is reported, never substituted.
enum class EnglishStressCoverage { Exact, FoldLexicalStress };

struct EnglishVocabularyDeclaration final {
  std::string resourceId;
  std::vector<std::string> symbols;
  EnglishStressCoverage stress{EnglishStressCoverage::Exact};
};

inline constexpr std::size_t kMaximumEnglishCoverageKeysPerPhone = 32U;

struct EnglishMissingPhone final {
  std::string symbol;          // resolved symbol
  std::string requiredSymbol;  // symbol the resource must declare
  std::size_t occurrences{0U};
  // First occurrences in token order, at most kMaximumEnglishCoverageKeysPerPhone.
  std::vector<domain::PhonemeKey> keys;
};

struct EnglishVocabularyCoverage final {
  std::size_t requiredSymbols{0U};  // distinct symbols the resource must declare
  std::vector<EnglishMissingPhone> missing;  // in first-occurrence order

  [[nodiscard]] bool complete() const noexcept { return missing.empty(); }
};

// Silence tokens ("pau") are rendered through each backend's explicit silence
// mapping and are not required vocabulary. Tokens outside the English
// vocabulary, and declarations that repeat or exceed 4096 symbols, fail.
[[nodiscard]] core::Result<EnglishVocabularyCoverage> checkEnglishVocabularyCoverage(
    std::span<const domain::PhonemeToken> tokens,
    const EnglishVocabularyDeclaration& declaration);

enum class EnglishEditReconciliation { Unchanged, Rebound, Unresolved };

struct EnglishResourceReconciliation final {
  // Same order and count as region.phonemeOverrides. A proposal: the caller
  // applies it through its own command so undo restores the prior edits.
  std::vector<domain::PhonemeOverride> overrides;
  std::vector<EnglishEditReconciliation> outcomes;
};

// Re-addresses saved phoneme edits when the English resource changes. Edits
// bound to the old resource's base context are rebound only when every optimal
// alignment of the note's old and new base sequences agrees; they then carry
// the new context. Ambiguous or removed targets are retained but marked
// unresolved. Edits without a context or with an already stale context stay
// unchanged, so resolution continues to diagnose them.
[[nodiscard]] core::Result<EnglishResourceReconciliation> reconcileEnglishResourceChange(
    const domain::VocalRegion& region, const EnglishPronunciationResource& before,
    const EnglishPronunciationResource& after, std::stop_token stop = {});

}  // namespace seam::phonemizer
