# Bank root-pitch diagnostic: span and method correction

Observed on parent `c111cbac5ba183dee822441d637a516836a52a2f`. This test/documentation unit closes
the broader-suite failure disclosed in the [full-song pitch dossier](INSTALLED_CLAP_SONG_PITCH_2026-10-15.md).
The [evidence index](BANK_ROOT_DIAGNOSTIC_2026-10-15.evidence.json) records local
execution and immutable inputs. No audio, bank metadata, native estimator,
voice DSP, production qualification policy or acceptance criterion is changed.

## Actual defects

`tests/singing_quality/test_bank_root_pitch.py` selected frames by analysis-window
origin rather than centre, ignored schema-3 `markers.loopStart/loopEnd`, inspected
only the first unit and called `skipTest` for missing optional local copies. The
latter could skip later required banks. Discovery noticed unlisted banks but did
not notice removal of a listed shipped bank. Native outputs were used without
checking their source hash, algorithm/grid/extent, confidence or pitch range.

The original four-method module fails on the unchanged demo recording. The extended
eight-method baseline fails five assertions, including neighbouring-span selection,
low-confidence/ceiling exclusion, changed algorithm refusal and a later mismatching
unit. The later-unit harness initially used `assertRaises` around a unittest subtest;
it was corrected to inspect a child `TestResult`. Both baseline executions retain
five failures. This is disclosed as a harness correction, not a new native result.

The diagnostic now reuses the existing scorer's `feature_metadata` and `usable`
checks, selects window centres inside the requested span, requires confidence >=0.5
and an in-range finite F0, and reports an ordinary median. `_unit_span` supports
legacy fields and schema-3 marker fields, rejecting partial or invalid bounds.
Every unit is checked; missing shipped banks fail, while absent optional local
copies cannot skip required banks. Content-based discovery stays independent of
the filename, and now also detects removal. The fixture bounds remain unchanged:
a discrepancy greater than 500 cents for the known spoken demo and less than 50 cents for the
local 440-Hz tones. No demo is reclassified as a production singer.

## Native observations and numerical interpretation

Both demo WAV paths have unchanged SHA-256
`caf8ceb04b864c7501371a2adae46697495e617b9ade178560ba2ea1aa6b8cb9`:
mono PCM16, 44.1 kHz, 24,255 frames. Their declared loop is 5,292..21,609 frames;
the legacy manifest has one unit, the schema-3 manifest eight. All use MIDI67.
Native `extract-pitch` uses FFT autocorrelation v3, 2,048-frame windows, 256-frame
hop, full-hop zero padding and 60–1,200 Hz range. The retained original demo output
has 95 frames; the loop-selected diagnostic uses 39 usable estimates.

| Selection over the demo loop | Median estimate | Difference from MIDI67 |
|---|---:|---:|
| Window origins, reproducing the failing legacy test | 383.356580 Hz | -38.579896 cents |
| Window centres, describing samples inside the loop | 681.868412 Hz | +958.387794 cents |

The schema-3 test previously ignored its loop and measured the whole file instead.
Using the same span for both schemas removes that inconsistency. The centre-selected
frequency spread is about 9.79, further illustrating that this utterance is not a
sustained tone. These figures describe this estimator/configuration/sampling rule;
changing frame membership changes a median on a highly variable source. They do
not establish its physical fundamental or validate the historical v1 interpretation.
The [2026-10-03 report](LISTENING_PACKET_ROOT_MIDI_MISMATCH_2026-10-03.md) remains
byte-identical historical evidence. Its +992-cent conclusion must not be presented
as current ground truth or as proof that all renderer errors come from the bank.
Known [v3 alias/boundary limits](PITCH_LIMITS_DISCLOSURE_2026-10-13.md) remain.

The two local phase7 copies retain the same 440-Hz tone bytes. Their proper
256..4,096 loop yields 12 estimates, median 440.000032 Hz and +0.000124 cents.
The generated one-second known-answer tone test also passes. All nine shipped
unit rows plus two optional local rows are retained individually; repeated rows
of the same spoken audio are not nine distinct recordings or independent results.

## Verification and retention

The repaired eight-method regression run passes, then the final bank suite passes
**12 methods**. Broader `seam_singing_quality_contract_tests` passes its one CTest
entry in 2.453 seconds: **189 methods, 19 skips, 170 executed methods**. The focused
12 repeat a subset of the broader suite; counts must not be added. Four extra
methods cover schema fields, partial spans, missing shipped banks and optional
traversal. Mocked protocol/source and traversal tests are not native WAV evidence.
The first repaired stdout includes two explicitly mocked traversal rows; final
mocked probes capture stdout so the final 11-row table contains only real inputs.

This unit is Python/test/documentation only. The retained native binary is identical
to the preceding unit's binary; embedded source ID `fe340737` is reported as found,
not authenticated build lineage. No C++ change or fresh native rebuild is claimed.
`out/evidence/bank-root-diagnostic-2026-10-15/manifest.json` binds **49 artifacts /14 preserved inputs**, 9191 bytes,
SHA-256 `9d025b7d3079ea5ba24513c1d9000475c532f520edbd4f9ed5b6d9c995494c9a`. Parent/final test source, native/helper sources,
binary/header, all bank/audio inputs, numerical comparisons and command receipts
are retained. The read-only verifier independently recalculates measurements from
held native frames and rehashes all 71 prior song-pitch artifacts/eight inputs.
It uses Git to check the historical bank-test source: the prior verifier's live
worktree assertion that this test still equals its old parent naturally no longer
holds after this intentional repair. Frozen earlier roots are not edited.

```sh
python3 -B out/evidence/bank-root-diagnostic-2026-10-15/verify.py --source-commit HEAD
```

The verifier does not execute native code, authenticate historical execution or
supply independent F0 ground truth. Replay requires existing bank/source paths.
Canonical four files and the 297 downloaded internet originals remain unchanged;
this unit pins their retained source manifest rather than performing another full
internet-source remeasurement. Production source/performer/identity/public-license
rights, real producer/distinct reviewer, phonetic coverage, failed-QC policy and
human listening remain unresolved. The existing ccbb39d6 review is confirmed live;
later full-song/publication/scorer/this unit are unreviewed. Full acoustic/classical/
neural/resources/targets/DAW/release/tolerance/integration requirements remain.
Alpha has 0/20 recorded passes, Beta is BLOCKED, U45 held and the full goal ACTIVE.
