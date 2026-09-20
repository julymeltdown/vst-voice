# Application singer validation campaign — September 20, 2026

Selection recorded before application rendering: use the five sources already
selected by `acoustic-combined-e9-validation.json`: procedural-song-00003,
00005, 00024, 00402 and 00420. Their membership in the validation partition was
checked against `prepared-combined/corpus.json`. This includes three continuous
songs and two pause-containing songs. Do not replace failed items with easier ones.

Candidate: `bundle-combined-e9-v512-e1` under
`/Users/lhs/seam-corpus-pauses-2026-09-19-r1/`, manifest SHA-256
`d4dd7737eedd54638dd66c09b25f0024cceb052daca447be75ac525e2c750f3d`.
The acoustic and vocoder exports remain unchanged during this campaign.

Each original project runs through `tools/neural_runtime/check_production_render.py`
with the actual native production renderer and worker. Its committed master is
compared with the matching `prepared-combined/song-NNN/source.wav` using
`tools.voice_model_training.compare_application_export`. Project/source identities
must match their corpus capture. No gain fitting, trimming or time alignment.

Outputs are new sibling directories `validation-combined-e9-v512-e1-NNNNN` under
the candidate's parent. A report of execution success is separate from pitch and
spectral quality. This is development validation on a
generated-teacher corpus, not blind listening or release qualification.

## Results and implementation repair

All five project/source hashes matched their corpus captures before measurement.
Song 00024 initially failed before inference with `Neural consonant timing is
unresolved; provide resolved timing before inference`. It contains two standalone
Japanese `ん` notes. The phonemizer emits voiced `N` with Coda role, but the
score-timing compiler only resolved whole-note pauses without vowel nuclei.

`ProceduralInNote` now assigns the score start to a sole untimed voiced Coda `N`.
It retains voicing and Coda identity and does not fabricate a nucleus. Explicit
timing edits, other lone consonants, multiple-N clusters, and SourceDependent
policy retain their prior behavior. Timing-policy revision 5 invalidates the
neural/procedural cache identities already bound to this revision.

After rebuilding, song 00024 exported master/stems/project and reopened successfully
in 23.6643 seconds. Its new output directory has suffix `-00024-timing5`; the
failed attempt was not relabeled as a pass. The other four rows were measured
before this repair. The campaign is development evidence across that repair,
not one immutable release-candidate certification run.

| Song | Spectral distance | Measurable voiced pairs | Within 50 cents | Fraction of measurable pairs | Mean absolute cents |
|---|---:|---:|---:|---:|---:|
| 00003 | 1.72974 | 1020 | 864 | 84.71% | 231.33 |
| 00005 | 1.71595 | 934 | 771 | 82.55% | 308.40 |
| 00024 after timing repair | 1.81454 | 795 | 671 | 84.40% | 267.73 |
| 00402 | 1.44111 | 467 | 413 | 88.44% | 202.94 |
| 00420 | 1.61447 | 433 | 371 | 85.68% | 187.88 |

All five strict pitch comparisons are `MISMATCH`. Across measurable pairs,
3090/3649 (84.68%) are within 50 cents and the weighted mean error is 250.20 cents.
This is not a percentage of all score frames or overall project completion. The
earlier single test song's 94.18% did not establish validation-set performance.
Exact pause intervals [84000,102000) in 00402 and [66000,78000) in 00420 have zero
nonzero PCM samples on both exported channels, with peak exactly zero.

Each output contains `comparison.json` with native pitch tracks, reference/master
hashes, extractor identity and the unmodified strict verdict. Report SHA-256:

| Song | Comparison receipt SHA-256 |
|---|---|
| 00003 | `9cc19f518dcaaccb5459ffb6ef49bae2b91088f328aa86af9c3c826c97c8100c` |
| 00005 | `21b2d027ccdff2de49535a3d4a515e02c8366f420b3b229faee7511e3a78694a` |
| 00024 | `d5d4987c91743a652058450d219c2dac5bcc1606b932f5bec76b1a9b3aa77208` |
| 00402 | `4e38da451a3ad4e01ff7e58f5c9dc98689ff6f01dd370fd8bbc7e6a60a7ef3ca` |
| 00420 | `a7f95a06de0cfc573d2ec4f680d407cf15614a39606f65d2e983c5535e332325` |

The focused timing CTest passes, including actual Japanese pronunciation at three
sample rates, preservation of edits, unresolved lone onset and multiple-N cluster.
The native production-render suite adds a companion Japanese-nasal fixture while
retaining the original English fixture. Ongoing CI exercises the nasal through
worker inference, export and reopen instead of relying only on the external corpus.
Final local verification: timing, original English production-render and companion
nasal production-render CTests all pass (3/3, 3.66 seconds). Source closure,
phase11 source verification and staged diff checks pass.

Next: compare completed vocoder epoch 2 on these same validation inputs; inspect
remaining note/transition errors without changing thresholds or substituting easier
songs. Naturalness, intelligibility and original-singer qualification remain open.

## Completed acoustic epoch 13 comparison

The previously unfinished acoustic training target is now complete. Epochs 10..13
add 1,068 updates using unchanged inputs and configuration. See the continuation
entry in `INTEGRATED_SINGER_EXECUTION.md` for checkpoint hashes and retention.
Acoustic-only validation on the same five sources improves frame-weighted mel
MAE from 3.6662582201 to 2.7009600825. The exported graph passes its runtime check.

Application candidate `bundle-combined-e13-v512-e1` keeps the same epoch-one vocoder,
vocabulary, score and configuration. Acoustic graph:
`e4065bdf8b2ea033afa69a30da31290d29ffe67fd59218ebd0c7b9cf90eb455f`.
Bundle manifest:
`06f2143307a9e5c5e78cc13388c7e90d82b383d4b7418f75e6339a7b88bdb4d6`.
All five actual application exports and saved-project reopens passed using timing
policy 5. Their outputs are `validation-combined-e13-v512-e1-NNNNN` siblings.

| Song | Spectral distance | Measurable voiced pairs | Within 50 cents | Mean absolute cents | Unmeasurable frames |
|---|---:|---:|---:|---:|---:|
| 00003 | 1.30853 | 1126 | 1061 | 79.31 | 97 |
| 00005 | 1.28626 | 1025 | 959 | 104.37 | 85 |
| 00024 | 1.42944 | 890 | 826 | 96.41 | 74 |
| 00402 | 1.04810 | 500 | 468 | 31.73 | 42 |
| 00420 | 1.24149 | 492 | 467 | 29.35 | 27 |

Aggregate **3781/4033 = 93.75%** within 50 cents on measurable voiced pairs,
versus **3090/3649 = 84.68%** for epoch nine. Frame-weighted mean absolute pitch
error falls from **250.20 to 77.46 cents**. More frames are measurable, so the
denominators intentionally differ; neither fraction is an all-frame or product
completion percentage. Every strict pitch status remains `MISMATCH`. The exact
score-rest intervals in 00402 and 00420 still contain zero nonzero PCM samples
on either output channel. No signal normalization or threshold change was applied.

Comparison receipt SHA-256 values:

| Song | SHA-256 |
|---|---|
| 00003 | `2fbc98da07ebeb97944b1c10654dd07c72656a3c87be508398f1b1f00b54c9a7` |
| 00005 | `067c860cc9830e3beba00af59a93463a9749903cc0d96e060f5789e00d989b57` |
| 00024 | `1b7586866cf41a0142c7c3450282e3ca8348f7f18a9647789bb1cd6eefbbb408` |
| 00402 | `c5db86fe50a7a255a3ec12552d89affe1baa45631cda1d32adee59d019b2e3e9` |
| 00420 | `2923e70f179b2c35a634d9fea2d9e4a44af6fc523624020cac9f46f72b9ec50d` |

Epoch thirteen is the stronger measured acoustic engineering candidate. Its
vocoder/source limitations and missing musical qualification remain. The live
vocoder epoch-two run is separate and must finish before its output can be
compared; do not claim its completion from these acoustic results.
