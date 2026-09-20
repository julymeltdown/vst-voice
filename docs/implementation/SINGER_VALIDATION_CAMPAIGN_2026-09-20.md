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
