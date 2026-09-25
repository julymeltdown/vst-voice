# U27/U28 language phonemizer checkpoint

Status: bounded rule-backed bootstrap services are implemented and connected to
inspection/sample-snapshot selection. They are not the final reviewed English
or Korean resource packages required for Beta GO.

## English

`EnglishPhonemizer` accepts English/unspecified lyric metadata, preserves the
visible text, gives explicit space-separated phone hints precedence, and reads
the pinned CMU Pronouncing Dictionary resource before consulting the
deterministic spelling fallback. CMUdict stress digits are retained in the
common English phone inventory; the first listed pronunciation is selected for
alternate-entry words. The exact revision, hashes, license, attribution and
accuracy boundary are recorded in `assets/pronunciation/en-us/README.md`,
`third_party/manifest.yml` and `THIRD_PARTY_NOTICES.md`. A source-derived
resource identity includes the dictionary bytes. Since the vendored resource
has out-of-order entries, the phonemizer builds a case-insensitive line index
partitioned into sorted runs before lookup; equal normalized keys retain source
order, so the first-pronunciation policy remains deterministic. Dictionary syllable breaks
are derived from vowel nuclei and the bounded legal-onset inventory rather
than a word-specific boundary list. The bounded fallback still
marks out-of-lexicon spelling as `EstimatedPronunciation`; unknown/non-ASCII
words remain visible, emit an `UnsupportedCharacter` diagnostic and become a
pause rather than being passed through Japanese rules. Punctuation and `-`/`~`
continuation are handled separately. Leading/trailing ASCII whitespace,
quotes/brackets and sentence punctuation are ignored for word lookup without
changing the saved lyric text; apostrophe-led dictionary entries and internal
punctuation are preserved. Hint parsing limits the inventory to 256
supported phones and 4096 bytes.

## Korean

`KoreanHangulPhonemizer` decomposes precomposed Hangul into initial/medial/final
components, maps coda consonants, transfers a coda to an empty next onset for
simple liaison, and supports `-`/`~` melisma plus a small romanized bootstrap
lexicon. Unsupported scripts are retained with bounded diagnostics. Explicit
phone hints remain authoritative and are validated before token creation.
Modern canonical L/V/T jamo sequences are composed algorithmically before
decomposition, so canonically decomposed lyrics resolve to the same phones and
sequence identity as precomposed Hangul. Compatibility and archaic jamo are
not guessed into syllables; unsupported input still follows the visible
diagnostic path.

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

The focused `seam_language_phonemizer_tests` target covers CMUdict output and
stress, spelling fallback, explicit hints, unknown diagnostics, Hangul decomposition/liaison,
romanization, context addresses and resource identity. Release and core runs
pass. The current lexicon/rules are intentionally not claimed as native-speaker
review, complete dictionary coverage, full bank vocabulary, or acoustic quality;
those remain U27/U28/U42 acceptance work.

### Korean compound nasal-insertion exceptions (2026-09-25)

The Hangul path now carries an exact-word, source-versioned allowlist for nine
common Rule 29 examples: `꽃잎`, `깻잎`, `막일`, `솜이불`, `홑이불`, `한여름`,
`나뭇잎`, `논일`, and `앞이마`. At the selected internal boundary it inserts
an onset `n` before the existing coda-assimilation pass, which produces the
expected coda nasalization as well as the inserted onset. This is deliberately
not generalized to every coda followed by a vowel-initial syllable: Rule 29
depends on compound/affix boundaries, and ordinary `꽃이` must keep its distinct
liaison reading. Tests assert all nine phone sequences, nonempty pronunciation
resource/sequence hashes, and `꽃이` as the negative control. The Korean
resource digest includes `korean_phonemizer.cpp`, so changing this allowlist
changes pronunciation/cache identity.

The allowlist is a bounded bootstrap, not a Korean compound analyzer or a
complete Rule 29 lexicon. The sources are the NIKL's [Rule 29 examples and
explanation](https://www.korean.go.kr/front/onlineQna/onlineQnaView.do?mn_id=216&pageIndex=1&qna_seq=324463)
and its [standard pronunciations for `꽃잎` and related words](https://www.korean.go.kr/front/onlineQna/onlineQnaView.do?mn_id=&pageIndex=1&qna_seq=332023).
Native-speaker review and matching singer-bank phone coverage remain required
before claiming Korean support.

### Canonically decomposed Hangul input (2026-09-25)

Before syllable decomposition, modern leading/vowel/trailing jamo are composed
using Unicode's algorithmic Hangul composition formula. This makes NFC and NFD
forms of the same modern Hangul resolve to identical phone sequences and
pronunciation identities, including compound-vowel and coda cases. The path is
deliberately limited to modern canonical jamo; compatibility and archaic jamo
remain diagnosable rather than being heuristically assembled. The regression
compares precomposed and decomposed `꽃잎`, including their exact sequence and
resource hashes, and separately checks decomposed `왜`.

Procedural render snapshots now select the same generic language resolver. The
snapshot identity records the selected language/resource/sequence, and the
recipe validator must find every resolved symbol before audio is rendered;
there is no Japanese fallback for an English or Korean region.
The score-voice projection path uses the same resolver independently for each
allocated voice, so overlapping multilingual notes cannot inherit a Japanese
or neighboring-voice pronunciation context.
