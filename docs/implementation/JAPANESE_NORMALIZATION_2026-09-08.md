# Japanese input normalization — U26 implementation checkpoint

Status: targeted kana normalization implemented; U26 Japanese reading/dictionary and native-language qualification remain incomplete.

## Implemented behavior

The kana phonemizer now handles half-width katakana letters, half-width voiced/semi-voiced marks, and supported decomposed kana plus combining dakuten/handakuten. It folds these to the same internal kana used by the existing mora table. Half-width full stop/comma/middle dot map to their existing pause-separator counterparts. The half-width prolonged sound mark follows existing continuation/long-vowel handling.

This is deliberately a targeted kana transform, not general NFC/NFKC. Unmatched combining marks, spacing voicing marks, unsupported historic kana and unknown kanji remain explicit unsupported input; no dictionary pronunciation is guessed. Normalization does not rewrite `LyricToken.surface`. Source-scalar indices are retained during composition, so warnings after a two-scalar voiced kana point to the original input rather than shifted normalized positions. Explicit note phone hints bypass this lyric normalization and retain precedence.

The mapping facts were checked against the pinned [Unicode 17.0 UnicodeData](https://www.unicode.org/Public/17.0.0/ucd/UnicodeData.txt), specifically half-width kana mappings and kana decomposition pairs. No external dictionary, runtime Unicode dependency, full Unicode data file or generated vocal resource was added. Unicode equivalence is not native-speaker approval of singing pronunciation.

## Versioning and verification

`SEAM_PHONEMIZER_REVISION` advances from 3 to 4. The existing CMake source digest includes the modified Japanese implementation, so pronunciation resource/input/context identities also change; the existing reconciliation and render-cache paths continue to own stale-edit/cache handling. This is a source implementation identity, not a reviewed dictionary version.

Two added cases in `tests/test_phonemizer.cpp` compare precomposed hiragana, katakana, half-width kana and decomposed mark input through the shared resolver. They verify contracted mora, closure, nasal and prolonged-vowel output, unchanged projects, original warning positions and explicit hint precedence. The new focused `seam_japanese_pronunciation_tests` target runs the existing phonemizer suite as well. Release native/core and focused Release/Debug builds pass. Focused CTest passes Release/Debug (0.37/0.40 s); the Release core target passes (16.28 s). Diff checks pass.

## Remaining U26 work

- Select and intake a lawful bounded, pinned Japanese reading/dictionary component; no component has been selected by this change.
- Preserve editable surface versus reading versus explicit phone intent through the shared resolver and UI.
- Implement and qualify dictionary/context-sensitive readings and vocabulary mappings, including particles and pronunciation exceptions.
- Test contextual behavior across actual processing chunks and dictionary revision changes.
- Obtain native-language review of lyric-to-singing fixtures and acceptance against the shipping singer vocabulary.

Remaining U25 work, including live host/input/listening and maximum-query latency qualification, is still open. This checkpoint does not mark U25, U26 or full Beta GO complete. Changes remain local/uncommitted.
