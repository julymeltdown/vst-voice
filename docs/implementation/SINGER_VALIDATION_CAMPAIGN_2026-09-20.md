# Application singer validation campaign — September 20, 2026

## Vocoder epoch-two comparison: completed, mixed quality outcome

All five real production-worker render/export/reopen and measurement executions
completed successfully with acoustic epoch 13 fixed and vocoder epoch two.
Every song still reports strict pitch `MISMATCH`; neither candidate is qualified.
The campaign embeds the candidate-bound training-overlap audit: songs 00003,
00005 and 00024 are vocoder training sources, not jointly held-out evaluation.

| Song | Spectral distance e1 → e2 | Pitch mean absolute cents e1 → e2 | Within 50 cents / measurable pairs, e2 |
| --- | --- | --- | --- |
| 00003 | 1.308529 → 1.218442 | 79.31 → 35.09 | 1083 / 1126 |
| 00005 | 1.286255 → 1.226101 | 104.37 → 114.59 | 919 / 996 |
| 00024 | 1.429443 → 1.298877 | 96.41 → 100.64 | 819 / 876 |
| 00402 | 1.048101 → 1.005430 | 31.73 → 44.16 | 450 / 486 |
| 00420 | 1.241492 → 1.149218 | 29.35 → 31.75 | 461 / 490 |

The measurable-pair weighted mean decreases from 77.4599 to 70.1603 cents,
but four of five per-song means worsen. The within-tolerance fraction changes
from 3781/4033 (93.7515%) to 3732/3974 (93.9104%): the denominator also shrinks.
Unmeasurable frames rise from 325 to 378; voicing mismatches change from 148
to 144. Do not interpret the fraction alone as improved coverage or acceptance.

Source-driven epoch-two vocoder controls also completed for all five sources.
Their spectral distances are respectively 0.685116, 0.580071, 0.728925,
0.527699 and 0.606079, each lower than its epoch-one control. All five controls
still report pitch `MISMATCH`. These bypass acoustic prediction and application
rest/dynamics processing; they are not numerical quality ceilings.

Score-linked diagnostics retain all 242 measured >50-cent errors: 120 windows
wholly inside a note and 122 crossing a note boundary. No boundary error is
excluded. In the 120 interior windows, reference pitch is within 50 cents of
the written note in 84 and candidate pitch in 37 (overlapping categories).
Written notes are not ground truth for expressive or consonant pitch.
Song 00402's [84000, 102000) rest and song 00420's [66000, 78000) rest remain
exactly zero in both channels; the other three songs contain no scored rest.

Evidence root: `/Users/lhs/seam-corpus-pauses-2026-09-19-r1/`.
Campaign: `campaign-e13-v512-e2-r1/campaign.json`, SHA-256
`914e60c63aaf1d7dd6b3a4751fb7c462f5ec2ed19e4af5320f989127696976dd`.
Each song retains its comparison and `score-diagnostic.json`; source controls
are `validation-source-vocoder-e2-NNNNN/diagnostic.json`.
Checkpoint and export identities are in [the continuation record](VOCODER_RESUME_2026-09-20.md).

Decision: retain both candidates. Do not promote epoch two as a pitch fix or
close a musical gate. Before committing to further training, inspect the
per-song regressions (especially 00005) against the source controls and captured
pitch tracks. A separate ancestry-checked cohort and musical qualification
remain required. Windows remains TODO; none of this is Windows evidence.

## Follow-up: fixed-frame pitch regression in song 00005

Comparing only indices measurable in both epochs is a supplemental diagnostic,
not a replacement for the full-grid results above. For song 00005, 978 common
application pairs worsen from 81.1355 to 100.1555 cents mean absolute error;
47 previously measurable pairs disappear and 18 appear. Fifteen common pairs
newly exceed 50 cents and five recover. In contrast, 1,052 common source-control
pairs improve from 26.2929 to 24.5183 cents. This rules out changing measurement
coverage as the sole explanation for the application regression, without proving
which conditioning component causes it.

At source frame 3,584, source/control pitch is approximately 587 Hz, while the
epoch-two application estimate is 187.591 Hz. At frame 254,208, source/control
pitch is approximately 659 Hz, while the application estimate is 93.752 Hz.
Both analysis windows are inside scored notes, not rest or boundary exclusions.
These estimates correspond to periods near one and two 256-sample vocoder hops.

Independent direct normalized autocorrelation on the captured stereo-mean PCM,
using the same mean-centered 2,048-sample windows, confirms the selector behavior:

| Source frame | Intended lag | e1 correlation | e2 correlation | e2 winning lag / correlation |
| --- | --- | --- | --- | --- |
| 3,584 | 82 | 0.769599 | 0.598695 | 256 / 0.706022 |
| 254,208 | 73 | 0.663135 | 0.441063 | 512 / 0.663002 |

`libs/seam-voicebank/src/pitch.cpp` selects the first local peak meeting both
the voicing threshold and 92% of the strongest correlation. The intended peaks
fail that rule in epoch two. This is not evidence to lower the threshold.
Supplemental 4,096-sample Hann-window frequency projections still show substantial
intended-note energy; low-frequency projections alone do not establish a dominant
93.75/187.5-Hz sinusoid. The waveform has competing periodic structure, and an
audible pitch or perceptual quality conclusion requires stronger evidence.

Next bounded experiment: capture the actual application acoustic mel and vocoder
F0 inputs for these windows, bind them to the candidate/project, and compare
with source-derived inputs. Test conditioning mismatch or hop-periodic artifacts
before choosing further training or changing synthesis. Preserve the existing
strict diagnostic and both candidate outputs.

### Native prepared-input replay export

The production-render test harness now accepts an explicit `--inputs-output`
through `tools/neural_runtime/check_production_render.py`. It exports native
snapshot-derived tokens, durations, F0, optional breathiness, steps and geometry,
bound to project, bundle and worker digests. Publication refuses an existing
file. This is **prepared-input replay**, not a capture of worker mel tensors;
the shipped worker has no new file-writing hook or environment override.

Both English and moraic-nasal production-render CTests passed (4.05 seconds).
Song 00005's diagnostic rerender produced the exact original epoch-two master
hash `66eeec3a3166d0fe90ddd1dd0debeafedfb4d9f4e01388bf0bf6e67a058272b8`.
Artifacts: `replay-e2-00005-inputs.json` and `replay-e2-00005-export/` under the
pause-corpus root. The prepared F0 at hop indices 14 and 993 is respectively
587.329529 and 659.255127 Hz, matching the intended notes rather than the
187.5/93.75-Hz output estimates. The replay uses the worker's existing 10 steps.
Next: run the exact acoustic/vocoder graphs with these inputs and verify waveform
parity before interpreting the intermediate mel as native-worker-equivalent.

## macOS authoring regression alongside training

### Completed graph replay and conditioning ablation

Replay of the exported acoustic and epoch-two vocoder graphs with captured native
inputs reproduces the song 00005 master within 6.7056e-8 maximum absolute sample
error after the source-defined float32 center-pan gain. No gain fitting, offset
search, trimming beyond declared final-hop padding, or resampling was used.
The two master channels are identical. This supports using replay intermediates
for this candidate and song, not a blanket cross-platform parity claim.

Four retained raw-mono vocoder experiments vary mel and F0 independently. The
predicted mel is computed once with the native score inputs; replacing vocoder
F0 does not recompute acoustic mel.

| Mel / vocoder F0 | Pitch mean absolute cents | Within 50 / measurable | Unmeasurable |
| --- | --- | --- | --- |
| Predicted / score | 114.587734 | 919 / 996 | 109 |
| Predicted / measured source | 118.309962 | 917 / 1002 | 106 |
| Source / score | 15.942844 | 1039 / 1063 | 34 |
| Source / measured source | 34.245985 | 1011 / 1058 | 41 |

All four strict statuses remain `MISMATCH`. At frames 3,584 and 254,208, changing
F0 alone retains estimates near 187.59/93.75 Hz. Substituting source mel with the
same score F0 yields 587.16/659.30 Hz, close to the intended notes. Predicted versus
source mel mean absolute difference is 2.837745. These interventions implicate
predicted-mel conditioning in the observed regression; they do not prove which
acoustic feature, sampler setting or training distribution causes it, nor prove
the vocoder is otherwise correct. Source-derived mel is a diagnostic oracle, not
an implementable inference substitute for a new song.

Retained artifact directory: `conditioning-ablation-e2-00005-r1/` under the pause
corpus root, containing all four WAVs and full pitch comparisons. `ablation.json`
SHA-256: `8e5860bd32d210e6072364aafb8f23db822f219baac728a9cab782907f62b5dc`.
Input replay SHA-256: `b763ad4d667490d78f7eda627fae5fe7a17ae37c0cd540b324163152995956c8`.
The song overlaps vocoder training, and no qualification claim follows.

Next experiment: compare acoustic sampler-step settings with the same native
inputs and fixed vocoder, then inspect conditioning/training discrepancies if
the mel mismatch persists. The worker currently pins 10 steps. Do not silently
change that production value or its cache identity based on this one song.

Rebuilt `seam_original_singer_song_journey_tests` and
`seam_procedural_install_journey_tests` from source at `300ad02c`, including
dependent AppKit/native-editor code, using `cmake --build build/release --target
seam_original_singer_song_journey_tests seam_procedural_install_journey_tests -j 2`.
Both CTest targets passed in 14.02 seconds: four authored-song cases and fifteen
installation/management cases. This covers installed procedural song rendering,
tuning with undo/save/reopen/export, copying to a draft without mutating the
installation, audible timing changes, missing/replacement/interrupted installs,
review invalidation and application menu/dialog dispatch.

These are real application-controller regression tests, not physical mouse/keyboard
creator observation, DAW validation, Windows evidence, or learned-voice qualification.
No source repair was needed. Vocoder training stayed live and reached 925/2804
epoch-two updates during this check; that count is historical, not a completion
receipt. No roadmap unit or musical acceptance gate is closed by this rerun.

> **Qualification correction:** this is an acoustic-validation regression set,
> not a jointly held-out acoustic-plus-vocoder set. Songs 00003, 00005 and 00024
> occur in the vocoder's completed training coverage. The measurements remain
> valid diagnostics, but none establishes combined-model generalization.

## Cross-stage training overlap audit

The original vocoder 400-song corpus and combined acoustic 424-song corpus use
different splits. Exact source-WAV digest comparison found that all 31 original
corpus songs in the combined acoustic validation partition are vocoder training
data. The other two acoustic validation songs, 00402 and 00420, are absent from
the original vocoder corpus. Absence does not prove source/recipe independence.

For the fixed five songs, the actual completed vocoder epoch-one receipt also
records positive training updates for 00003, 00005 and 00024. Receipt SHA-256
`cec64e7c5adb3ec81cceaa7c81046ca8d62447e0126f49df2bba639fe536dd94`
binds the export's dataset identity, and the export graph matches the bundle.
Retained evidence: `campaign-e13-v512-e1-r1/vocoder-training-audit.json` under
the external pause-corpus artifact root.

The runner now declares `validationScope=selected-acoustic-corpus-only` and
`combinedModelHoldoutVerified=false`. Supply `--vocoder-export` and
`--vocoder-checkpoint` together to inspect candidate-bound epoch training IDs.
Missing evidence is NOT_AUDITED; inconsistent receipt/export/bundle identities
refuse before rendering. No overlap within one epoch still cannot establish
absence across pretraining/resume ancestry or duplicated audio.

Keep all five regression songs, retaining the distinction between vocoder-seen
and absent-from-original-corpus sources. Do not reshuffle existing partitions to
claim independence. Combined-singer qualification needs a separately frozen
cohort checked against both models' complete training ancestry, including audio,
grouping/recipe and rights identities. Epoch two can continue as a reconstruction
experiment; improvements on this cohort are not held-out qualification.

Overlap-audit verification: seven campaign tests passed; the full training-tool
suite ran 254 tests, 253 passed and one skipped in 45.654 seconds. Tests reject
wrong candidate graphs and changed checkpoint receipts and retain unverified
holdout status when no overlap is found. The real candidate-bound audit reports
the three overlapping IDs above. Source-closure and Phase 11 checks passed.

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

## Same-source vocoder controls and remaining error locations

Ran `reconstruct_source_vocoder` on all five fixed validation sources with the
same epoch-one vocoder. Each source digest was selected from the corpus capture.
This bypasses the acoustic model using source-derived mel and native measured F0;
it also omits application dynamics/rest muting, so it is a diagnostic comparison,
not an interchangeable application output or a numerical quality ceiling.

| Song | Spectral distance | Measurable pairs | Within 50 cents | Mean absolute cents |
|---|---:|---:|---:|---:|
| 00003 | 0.87526 | 1181 | 1101 | 56.19 |
| 00005 | 0.80773 | 1055 | 1007 | 27.16 |
| 00024 | 1.03779 | 902 | 834 | 72.47 |
| 00402 | 0.73947 | 515 | 476 | 36.61 |
| 00420 | 0.87574 | 494 | 463 | 48.04 |

Control aggregate: **3881/4147 (93.59%)**, mean absolute error **48.94 cents**.
Every strict status is MISMATCH. Application epoch thirteen is 93.75% with
77.46 cents mean error and a different measurable denominator. Similar aggregate
fractions do not establish identical failure mechanisms.

At exact matching analysis indices, 95 frames exceed 50 cents in both paths;
157 exceed it only in the application and 171 only in the control. "Only" means
the other path does not have a measured error above 50 at that index; it can also
be unmeasurable, not necessarily correct. No error or confidence threshold changed.

Of the application's 252 measured errors above 50 cents, 131 native 2048-sample
windows cross a score-note boundary and 168 cross a phoneme boundary. A crossing
means `windowStart < boundary < windowEnd`. These counts overlap and are not
exclusions: all remain in the published comparison. This suggests separate
transition and interior investigations, but does not prove a timing compiler bug
or justify changing training/runtime frame conventions to improve this metric.

Controls are retained as `validation-source-vocoder-e1-NNNNN/{source.wav,
reconstruction.wav,diagnostic.json}` under the same external artifact root.
Diagnostic receipt SHA-256:

| Song | SHA-256 |
|---|---|
| 00003 | `961cdfed19a9ccd4c186f2f8adbed1274b6e64dc21ab9bab9bc0bb301e62865c` |
| 00005 | `87995be1a46c78ed17071dcac7e5670941d9c0c282a6992130f95388fea2c87d` |
| 00024 | `5e0cc5c04210de6471b4c74843b7e4191545a360593805d7e84e8d6acab2a664` |
| 00402 | `d69a453ad81b501fc332e7d1b26f1bbf03d9374d22ebe595eacda8ffd715c066` |
| 00420 | `5db5b5a1692714341c20395c7bd3d0d30f597686ee4b724d8b66f6b923bc05c4` |

Next candidate comparison keeps acoustic epoch thirteen fixed and substitutes
only the completed vocoder epoch-two export when available. Run the same five
source controls alongside application exports, retaining failed items and exact
rest checks. Do not extend acoustic training or change pitch gates solely from
the current aggregate fraction; the control demonstrates residual vocoder errors
and the error-location analysis leaves multiple causes possible.

## Reproducible application campaign runner

Added `tools.voice_model_training.validation_campaign` so the next vocoder
checkpoint can use this exact selection without ad hoc per-song shell loops.
The explicit local selection is
`/Users/lhs/seam-corpus-pauses-2026-09-19-r1/validation-selection-five.json`,
SHA-256 `614f640fad06cf052dfab380eeeada5c2f584ee24a69fbd7d9620d9a4d377615`.
It binds captured corpus SHA-256
`ec77cc91df61718f171b83114a26bbe7df0e28d09f52357b2443844c3a9216ca`
and the five original project paths. Keep that selection unchanged when swapping
the completed vocoder candidate. Run with the training Python environment:

```sh
build/neural-runtime/diffsinger-model-env/bin/python \
  -m tools.voice_model_training.validation_campaign \
  --selection /Users/lhs/seam-corpus-pauses-2026-09-19-r1/validation-selection-five.json \
  --selection-sha256 614f640fad06cf052dfab380eeeada5c2f584ee24a69fbd7d9620d9a4d377615 \
  --corpus /Users/lhs/seam-corpus-pauses-2026-09-19-r1/prepared-combined/corpus.json \
  --bundle /absolute/path/to/new-candidate-bundle \
  --renderer "$PWD/build/release/seam_neural_production_render" \
  --pitch-executable "$PWD/build/release/seam_voicebank_cli" \
  --output /absolute/path/to/new-campaign-directory
```

The runner checks partition membership and captured source/project bytes before
any render. Each output retains frozen inputs, application export and reopen
verification, the unchanged strict comparison, and an item receipt. Execution
failures remain in the campaign rather than disappearing from an average.
Source-driven vocoder controls and exact rest checks remain separate required
diagnostics; this runner does not claim to replace them or certify a singer.

Verification: training-tool suite ran 248 tests, 247 passed and one skipped;
the six focused campaign tests passed, including failed-item retention,
continued execution after failure, and successful execution with MISMATCH quality.
Phase 11 source checks passed. Windows remains a README TODO; none of this is
Windows runtime or full-product Beta GO evidence.

Real execution also completed for all five songs with acoustic epoch thirteen
and vocoder epoch one. Results are retained at
`/Users/lhs/seam-corpus-pauses-2026-09-19-r1/campaign-e13-v512-e1-r1/campaign.json`.
Every item passed render/export/reopen and measurement; every strict pitch
status remains MISMATCH. This rerun is execution evidence, not new training or
an additional quality-qualified candidate.

## Written-score comparison and exact-rest regression

Added `score_application_export` with independently supplied comparison and label
receipt hashes and a freshly captured application master. It validates captured
score geometry, recomputes the original strict track comparison, retains its
unmeasurable/voicing counts, and reports score-relative locations without changing
the underlying quality result. Five focused regressions cover byte/clock binding,
octave errors without correction, exact boundary ownership, padding, unmeasurable
frames, and stereo rest cancellation. Combined campaign/score tests: 11 passed.

Applied it to all five application outputs. All master hashes exactly match the
previous individually executed epoch-13/vocoder-1 exports, despite copying project
inputs into the campaign directory. Per-song `score-diagnostic-v2.json` receipts
(including strict unmeasurable/voicing totals) are retained beside the campaign
comparison receipts; the initial `score-diagnostic.json` results are preserved.

| Song | Strict errors above 50 cents | Whole-window note interior | Reference within 50 of note | Candidate within 50 of note |
|---|---:|---:|---:|---:|
| 00003 | 65 | 38 | 30 | 7 |
| 00005 | 66 | 32 | 31 | 2 |
| 00024 | 64 | 37 | 23 | 17 |
| 00402 | 32 | 10 | 1 | 9 |
| 00420 | 25 | 4 | 0 | 4 |

Of 252 strict measured errors, 121 are whole-window note interiors and 131 cross
note boundaries. Among those 121, the reference is within 50 cents of the written
note in 85 cases and the candidate in 39. These categories overlap: four have
both individually within 50 but differ from each other by more than 50; one has
both outside. These are diagnostic counts, not designated steady-state acceptance
frames. Consonants, expressive pitch, source synthesis and pitch-estimator errors
can all disagree with written note pitch; the score is not an independent acoustic
oracle. Do not relabel or retrain the source to MIDI solely from these counts.

Final combined tool verification: 253 tests run, 252 passed, one skipped in
45.620 seconds. Tracked-source closure and Phase 11 source checks passed.

Song 00402 rest `[84000,102000)` and song 00420 rest `[66000,78000)` contain
zero nonzero PCM samples and zero peak on **both** channels. The other three
scores have no rest: they receive no rest-pass claim. No strict pitch gate changed.

Development decision: keep the completed acoustic epoch thirteen fixed and wait
for the live vocoder's completed epoch-two artifact before comparing this same
selection. Current evidence does not isolate a new timing compiler defect, justify
octave correction, or warrant another acoustic training run. Vocoder/source control
comparisons remain required; naturalness and lyric intelligibility remain unproven.
