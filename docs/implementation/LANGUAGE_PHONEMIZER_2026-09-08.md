# U27/U28 language phonemizer checkpoint

Status: bounded rule-backed bootstrap services are implemented and connected to
inspection/sample-snapshot selection. They are not the final reviewed English
or Korean resource packages required for Beta GO.

## English

`EnglishPhonemizer` accepts English/unspecified lyric metadata, preserves the
visible text, gives explicit space-separated phone hints precedence, and emits a
small versioned lexicon with primary/secondary/unstressed vowel symbols (for
example `eh0` and `ow1`). A deterministic grapheme fallback covers bounded ASCII
syllables; unknown/non-ASCII words remain visible, emit an `UnsupportedCharacter`
diagnostic and become a pause rather than being passed through Japanese rules.
Punctuation and `-`/`~` continuation are handled separately. Hint parsing limits
the inventory to 256 supported phones and 4096 bytes.

## Korean

`KoreanHangulPhonemizer` decomposes precomposed Hangul into initial/medial/final
components, maps coda consonants, transfers a coda to an empty next onset for
simple liaison, and supports `-`/`~` melisma plus a small romanized bootstrap
lexicon. Unsupported scripts are retained with bounded diagnostics. Explicit
phone hints remain authoritative and are validated before token creation.

Both services produce normal `PhonemeToken` roles/voicing and are wrapped by
`resolveEnglishPronunciation`, `resolveKoreanPronunciation`, and the generic
`resolvePronunciationForLanguage` identity builder. Each result carries a
source-derived SHA-256 resource identity and per-note/lyric context addresses.
Generic native/embedded inspection chooses the explicit language service; the
sample render snapshot chooses the service matching the selected bank language.
Editor mutation/reconciliation, technical-edit review, generated-phone Find,
performance-take validation and copied render-edit binding use the same region
resolver. Explicit phone hints are dispatched to the matching language
inventory, and all adapters share the bounded 10,000-note/65,536-character/
4,096-edit admission contract.

The focused `seam_language_phonemizer_tests` target covers stressed output,
explicit hints, unknown diagnostics, Hangul decomposition/liaison,
romanization, context addresses and resource identity. Release and core runs
pass. The current lexicon/rules are intentionally not claimed as native-speaker
review, complete dictionary coverage, full bank vocabulary, or acoustic quality;
those remain U27/U28/U42 acceptance work.

Procedural render snapshots now select the same generic language resolver. The
snapshot identity records the selected language/resource/sequence, and the
recipe validator must find every resolved symbol before audio is rendered;
there is no Japanese fallback for an English or Korean region.
The score-voice projection path uses the same resolver independently for each
allocated voice, so overlapping multilingual notes cannot inherit a Japanese
or neighboring-voice pronunciation context.
