# English pronunciation resource (seam-en-us)

This directory holds the English resource used by the U27 phonemizer. The
manifest `seam-en-us.resource.json` binds every part below to exact digests;
`tests/test_english_phonemizer.cpp` fails if the manifest, the embedded bytes,
the third-party intake record or the frozen vocabulary disagree.

## Files

`cmudict.dict` is the CMU Pronouncing Dictionary from the official
`cmusphinx/cmudict` repository at immutable revision
`74790861f652b15e4ac49015a90074ad62a27690`.

- Upstream source: <https://github.com/cmusphinx/cmudict>
- Upstream archive SHA-256: `741c592660bbf10ab93fe3d5aa709b73a3b5ba91e3291a066715440f4137a58d`
- Upstream dictionary SHA-256: `81917843c7f44ce2b094ac63873c2c7a4cf802040792c455ba3ca406891c3d22`
- Vendored bytes are unmodified; the dictionary has 135,166 lines.
- License: BSD-2-Clause; see `licenses/third-party/CMUdict-2026-09-24-LICENSE.txt`.
- 22 upstream entries carry a trailing `# annotation` (for example `hiv`,
  `gdp`, `d'artagnan`). SEAM reads the annotation as metadata, never as phones.

`seam-en-exceptions.tsv` is SEAM's own exception table (`word<TAB>phones`,
English hint syntax). It currently holds six entries: `ba` (CMUdict's second
variant instead of the letter-name reading), `lovin` and `nothin` (dropped-g
forms), and the spellings `woah`, `ooo` and `cuz`. Each entry cites the CMUdict
entry it follows. These are engineering selections pending native-speaker
review, not review results.

## Resolution

For each lyric word the resolver uses, in order: an explicit note phone hint,
the lyric's authored reading hint, an exception entry, the first-listed CMUdict
variant, a derivation rule, then a spelling estimate. A reading hint uses the
same phone syntax as a note hint and covers the whole lyric; an invalid,
non-ASCII or pause-bearing reading hint is diagnosed with a pause instead of
falling back to the surface. Derivations (`seam-en-derivation-v1`) cover dropped-g forms
(`dreamin'` from `dreaming`) and possessives (`kiss's`, `singers'`). Derived
and estimated words carry an EstimatedPronunciation warning that names the
word and its basis. Unsupported text produces a pause and a warning with the
character index; the lyric surface is never rewritten.

Lyric text is normalized (`seam-en-normalization-v1`): ASCII case, typographic
apostrophes, quotes and dashes, and Latin-1 accented letters. A note may hold up
to 32 words and 256 phones.

Syllables follow the legal-onset rule (`seam-en-legal-onset-v1`). When one
lyric token is shared by adjacent legato notes, its syllables are distributed
in order (`seam-en-note-distribution-v1`): one per note, the last vowel
sustained over extra notes with its coda on the final note, and surplus
syllables compressed into the final note. A note hint replaces only that
note's share.

## Vocabulary and identity

All emitted symbols belong to the frozen vocabulary `seam-en-arpabet-v1`: 24
consonants, 17 vowels in bare/0/1/2 stress forms, and `pau` (93 symbols,
listed in the manifest). Phoneme edits outside it are kept but not applied.
`checkEnglishVocabularyCoverage` reports which symbols a bank, recipe or model
lacks, with the affected note keys; it never substitutes another symbol. A
resource that does not model lexical stress must declare that explicitly
(`FoldLexicalStress`).

The builtin resource hash covers the resolver sources, `cmudict.dict` and
`seam-en-exceptions.tsv`, so any change produces a new pronunciation identity
and invalidates dependent performance and render requests.
`reconcileEnglishResourceChange` proposes how saved phoneme edits carry over:
unambiguous matches are rebound, ambiguous ones are kept but marked unresolved.

## Review status

The dictionary describes North American English and disclaims correctness.
No native speaker has reviewed this resource, its exception entries or the
engineering fixtures in `tests/fixtures/pronunciation/en-us-engineering-fixtures.json`.
Lexical success is not singing, dialect or proper-name qualification; those,
and the R7 song/resource qualification, remain open.
