# Application singer validation campaign — September 20, 2026

## Lower-rate L1 experiment: reconstruction improves, application audio does not

The same two-epoch warm start at learning rate 0.00008 completed 534 updates
with verified coverage. Both checkpoints remain; the final receipt SHA-256 is
`38253f67c22baee4ec92386853f26ec205d6a9d47025daedbc3c7ae79ff17de6`,
binary SHA-256 `f45c41569d3c12cfc0936c96561e4dfde75eb41e385e0f8356f1d6a025ceb98e`.
Acoustic export passed runtime checks; graph SHA-256
`e8fb0057382770106f1674b83ba7c53a1148b2908cf0f1710bc45311e0ccd878`.
Bundle manifest SHA-256 `5f59e632858b4e136773c7177a9fc9120eae7d72df06a2aa757c9a89bf5692a1`.

| Song | Pitch mean absolute cents | Within 50 / measurable pairs | Spectral distance |
| --- | --- | --- | --- |
| 00003 | 59.5739 | 1071 / 1123 | 1.201079 |
| 00005 | 74.5030 | 936 / 996 | 1.222540 |
| 00024 | 76.4914 | 829 / 885 | 1.245853 |
| 00402 | 32.4922 | 475 / 502 | 0.917433 |
| 00420 | 37.9838 | 448 / 476 | 1.191715 |

All five executions pass; all strict comparisons remain `MISMATCH`. Weighted
pitch MAE is 61.0730 cents, versus epoch 21's 35.2267 and the higher-rate L1
control's 45.7906. Measurable pairs fall to 3982 (3759 within tolerance, 94.3998%);
358 frames are unmeasurable and 165 have voicing mismatches. There are 110
interior and 113 boundary-crossing pitch errors. Both scored rests remain
exactly silent. Song 00402's mean pitch error improves relative to epoch 21,
and song 00420 has zero voicing mismatches, but the aggregate and all five
spectral distances regress. Preserve these mixed results without exclusions.

Fresh-session fixed-selection mel MAE improves to 2.291161 on training and
2.315017 on validation, versus 2.391302/2.400242 at epoch 21. This directly
demonstrates why reconstruction loss alone cannot choose the deployed candidate.

Evidence under `/Users/lhs/seam-corpus-pauses-2026-09-19-r1/`:
`campaign-e21-reset-l1-low-lr-r1/campaign.json`, SHA-256
`936bddb71832f824259f538f87e532d66513a6f91d67dee8647527db7f2c8806`;
`acoustic-low-lr-partitions-r1/experiment.json`, SHA-256
`d7c245a47be9fe25a53e9cd140528f74bb4b17a4d58e2a0425ed836f4c8ea940`.

Decision: retain epoch 21, stop this short loss/learning-rate sweep, and do not
promote or extend its failed candidates. Next strengthen reproducible candidate
comparison and separately frozen cross-model ancestry/coverage evaluation.
Strict diagnostic equality is not interchangeable with the product's designated
steady-frame pitch criteria; neither is enough to prove intelligibility or
naturalness. Keep the original full product acceptance requirements intact.

## Paired warm starts: neither L1 reset nor L2 replacement improves epoch 21

Two epochs per arm, identical epoch-21 weights, reset optimizer/RNG, seed 933,
learning rate 0.0008 and unchanged data. L1 is the reset-optimizer control; L2
changes only the loss. Both completed 534 updates with full coverage. Both
exports passed runtime checks and all ten application executions passed, but
every strict pitch comparison remains `MISMATCH`.

| Song | Pitch MAE baseline / L1 / L2 (cents) | Spectral distance L1 / L2 |
| --- | --- | --- |
| 00003 | 28.96 / 32.36 / 419.37 | 1.143787 / 1.799822 |
| 00005 | 41.81 / 62.82 / 575.60 | 1.179731 / 1.814183 |
| 00024 | 37.59 / 44.26 / 472.99 | 1.229705 / 1.762821 |
| 00402 | 36.49 / 48.38 / 506.24 | 0.885078 / 1.544716 |
| 00420 | 30.76 / 42.40 / 441.45 | 1.155833 / 1.763058 |

| Aggregate | Epoch 21 | L1 reset | L2 reset |
| --- | --- | --- | --- |
| Weighted pitch MAE (cents) | 35.2267 | 45.7906 | 484.8801 |
| Within 50 / measurable pairs | 3919 / 4116 | 3824 / 4008 | 2413 / 3242 |
| Within-50 fraction | 95.2138% | 95.4092% | 74.4294% |
| Unmeasurable frames | 260 | 343 | 1007 |
| Voicing mismatches | 122 | 150 | 251 |
| Train mel MAE, fixed 5510 frames | 2.391302 | 2.560466 | 3.966899 |
| Validation mel MAE, fixed 4619 frames | 2.400242 | 2.552088 | 3.982111 |

The slightly higher L1 within-50 fraction is not an improvement claim: fewer
pairs remain measurable and mean error/voicing/spectral distance worsen.
L1 has 74 interior + 110 boundary errors; L2 has 696 + 133. Both scored rests
remain exactly zero in both channels. No failed frames or songs were dropped.
Reject both as replacements; retain epoch 21. This short warm-start experiment
does not establish that all L2 training or convergence schedules are inferior.

Evidence under `/Users/lhs/seam-corpus-pauses-2026-09-19-r1/`:

- `campaign-e21-reset-l1-r1/campaign.json`, SHA-256
  `70da75007ae6100cd7e88a483b850dd38edd4068d7f9a4f9f6b7e9ddaee69393`.
- `campaign-e21-reset-l2-r1/campaign.json`, SHA-256
  `88b0dea2919346c2e54e4671007b52269d17baa46d06c02cffec1b9d97e4a53a`.
- `acoustic-reset-l1-l2-partitions-r1/experiment.json`, SHA-256
  `47a5bc67562572302c206019e780bd9f72cd1e1ab43fd2c58ed8a63b3c2916c6`.

Next bounded hypothesis: two L1 warm-start epochs from the same epoch-21 parent,
same reset and seed, changing only learning rate from 0.0008 to 0.00008. Compare
against the L1 control and epoch 21 using the same generated-output metrics.
This tests update-size sensitivity; it is not yet a demonstrated remedy.
All qualification, training-overlap, independent-cohort and Windows TODO
limitations remain unchanged.

## Acoustic epoch 37: reject as a replacement for epoch 21

The 16-epoch continuation lowered training loss but **regressed on every fixed
application song**. All five application executions passed; all strict pitch
comparisons remain `MISMATCH`. Vocoder epoch two, production 10-step sampling,
source projects, reference audio and evaluation thresholds were unchanged.

| Song | Spectral distance e21 → e37 | Pitch mean absolute cents e21 → e37 | Within 50 / measurable pairs at e37 |
| --- | --- | --- | --- |
| 00003 | 1.082442 → 1.728768 | 28.96 → 350.44 | 768 / 954 |
| 00005 | 1.085034 → 1.728485 | 41.81 → 342.76 | 659 / 825 |
| 00024 | 1.148612 → 1.750579 | 37.59 → 444.65 | 585 / 723 |
| 00402 | 0.796122 → 1.433498 | 36.49 → 228.48 | 367 / 420 |
| 00420 | 1.038213 → 1.632547 | 30.76 → 253.25 | 309 / 397 |

Weighted pitch MAE worsens from 35.2267 to 341.9935 cents; within-tolerance
pairs fall from 3919/4116 (95.2138%) to 2688/3319 (80.9882%). Unmeasurable
frames increase from 260 to 975 and voicing mismatches from 122 to 214.
Score-linked diagnostics locate 519 interior and 112 boundary-crossing errors;
none are excluded. Both scored rests remain exactly silent in both channels.

On the identical saved five-training/five-validation selection, fresh-session
10-step training-conditioning inference worsens train mel MAE from 2.391302 to
2.917484 (5510 frames), and validation from 2.400242 to 2.948018 (4619 frames).
Because reconstruction worsens on the training sample too, these observations
alone do not establish ordinary held-out overfitting. Lower DDPM training loss
is not a sufficient checkpoint-selection metric. No further training extension
is justified by these results; retain epoch 21 while diagnosing the mismatch
between the training objective and deployed sampling.

Evidence under `/Users/lhs/seam-corpus-pauses-2026-09-19-r1/`:
`campaign-e37-v512-e2-r1/campaign.json`, SHA-256
`c26e63083b6e45cb739101760caf74eff0bd6bf63e8ccfc5107b18f06494814e`;
`acoustic-e37-train-validation-fresh-r1/experiment.json`, SHA-256
`d46b871841222aedff0e70fb5375655614ed9ec890f1f437986908ce4809a6e9`.
All per-song artifacts, failed comparisons and prior candidates are retained.
The vocoder training-overlap limitation and independent qualification-cohort
requirement remain unchanged. Neither candidate is a qualified singer.

A bounded follow-up on song 00005 used captured training conditioning and a
fresh ONNX session for each epoch/step pair. Requested steps 10/20/32 yield mel
MAE 2.417955/2.336895/2.297900 for epoch 21 versus
2.978614/3.023851/2.960633 for epoch 37. More requested steps do not rescue this
regression. Evidence: `acoustic-e21-e37-steps-00005-r1/experiment.json`, with the
source selection recorded before inference. This is a single-phrase mel-only
diagnostic, not a replacement audio campaign. Production remains at 10 steps.

### Fixed-noise diagnosis: loss improves while generation deteriorates

Loaded both verified checkpoints into the clean pinned DiffSinger checkout,
using the same ten previously selected phrases and captured training inputs.
Training-versus-deployment encoder outputs match exactly on all ten phrases
for both checkpoints. With identical Torch seed 91 and 10 sampling steps,
frame-weighted sampled mel MAE worsens from 2.349434 to 2.852459 on training
and from 2.376004 to 2.897271 on validation. Thus this regression also occurs
in Torch; it is not solely an ONNX export/runtime problem. Torch and ONNX RNG
streams differ, so these are paired within-Torch measurements, not waveform
parity claims across runtimes.

For each phrase, a fixed noise tensor was added to the same normalized target
at timesteps 0/100/300/500/700/900/999. The actual trained denoiser predicted
epsilon; the clean estimate was reconstructed using that checkpoint's schedule.
Representative frame-weighted validation results:

| Timestep | Noise MAE e21 → e37 | Unclamped clean MAE e21 → e37 | Clamped clean MAE e21 → e37 |
| --- | --- | --- | --- |
| 100 | 0.327994 → 0.294801 | 0.112259 → 0.100898 | 0.106643 → 0.098412 |
| 500 | 0.197692 → 0.163974 | 0.680646 → 0.564555 | 0.385828 → 0.370394 |
| 900 | 0.214189 → 0.165387 | 13.027494 → 10.059207 | 0.830599 → 1.017119 |
| 999 | 0.214460 → 0.162709 | 33.757526 → 25.611612 | 0.894439 → 1.017367 |

Clean errors are normalized-latent units, not mel units. At timestep 900,
93.26%/89.08% of clean estimates require clamping for e21/e37 respectively;
fewer clipped values do not imply better reconstruction. Both models remain
poor high-noise reconstructors. The measured mechanism is consistent with
epsilon-loss improvements failing to improve the bounded reverse trajectory;
it does not establish that a particular loss, learning rate, or architecture
change will fix generation. Preserve the clamp and acceptance thresholds.

Evidence: `acoustic-e21-e37-fixed-noise-r1/experiment.json`, SHA-256
`dac5f37e00aa8ad8dcafcb875086a7c8e356905fc1ef131bbe0f4e5fb10b86d4`.
The saved pre-inference selection, checkpoint hashes, per-source/per-timestep
results, target hashes and Torch/upstream versions are retained alongside it.

Next implementation priority is a separately identified warm-start experiment
path, not weakening strict resume identity: retain the best known epoch-21
weights, explicitly record any optimizer/objective change with reset optimizer
state, and evaluate short completed intervals using generated-output metrics.
Any such run must preserve existing checkpoints and normal source/label admission.
Do not silently edit optimizer state or label changed training as an exact resume.

## Acoustic epoch 21: consistent five-song improvement, still unqualified

The eight-epoch acoustic continuation completed; the fixed vocoder remains epoch
two and production sampling remains 10 steps. All five real application
render/export/reopen/measurement executions passed. Every strict pitch status
remains `MISMATCH`; execution success is not musical qualification.

| Song | Spectral distance e13 → e21 | Pitch mean absolute cents e13 → e21 | Within 50 / measurable pairs at e21 |
| --- | --- | --- | --- |
| 00003 | 1.218442 → 1.082442 | 35.09 → 28.96 | 1128 / 1172 |
| 00005 | 1.226101 → 1.085034 | 114.59 → 41.81 | 976 / 1025 |
| 00024 | 1.298877 → 1.148612 | 100.64 → 37.59 | 869 / 913 |
| 00402 | 1.005430 → 0.796122 | 44.16 → 36.49 | 480 / 512 |
| 00420 | 1.149218 → 1.038213 | 31.75 → 30.76 | 466 / 494 |

Aggregate measurable-pair weighted mean absolute error improves from 70.1603
to 35.2267 cents. Within-tolerance pairs increase from 3732/3974 (93.9104%) to
3919/4116 (95.2138%), while unmeasurable frames fall from 378 to 260 and voicing
mismatches from 144 to 122. Song 00005's voicing mismatches nevertheless rise
from 39 to 41. Keep this regression visible alongside the improvements.
These fractions do not substitute for the product's designated steady-frame
acceptance, intelligibility, naturalness or listener evidence.

The exact two-channel rest checks pass for song 00402 [84000,102000) and song
00420 [66000,78000); other songs have no scored rests. Score-linked diagnostics
are retained for all five songs without suppressing boundary errors.

On the same fixed five-training/five-validation source selection, fresh-session
10-step inference with training conditioning improves training mel MAE from
2.839868 to 2.391302 (5510 frames), and validation from 2.831527 to 2.400242
(4619 frames). This supports the continuation's engineering value, not general
musical qualification or proof that further epochs must improve.

Evidence root: `/Users/lhs/seam-corpus-pauses-2026-09-19-r1/`.
Application campaign: `campaign-e21-v512-e2-r1/campaign.json`, SHA-256
`c0b44c9efb7ed5f3cb154c6d3a900ad162d1f3e435b61461d1f4b9062c93d4fe`.
Acoustic-only comparison: `acoustic-e21-train-validation-fresh-r1/experiment.json`,
bound to the earlier saved source selection. The candidate-bound vocoder audit
still discloses training overlaps 00003/00005/00024. No model was installed or
promoted to qualified status; previous candidates remain available.

Decision: retain epoch 21 as the improved experimental comparison candidate.
Next inspect the remaining error locations and model-reconstruction gap before
choosing another bounded intervention. Keep the same regression cohort and
independent-cohort requirement; do not chase a threshold by dropping failures.

## Training-versus-native conditioning: compared, not a demonstrated fix

The captured dataset snapshot matches acoustic epoch 13's dataset identity.
The normal conditioning batch reader revalidated shard bytes against captured
labels before comparison. All five native token sequences match the training
vocabulary/sequence exactly. Training/native phone-frame differences are small:

| Song | Phone-frame mismatches | F0 voiced/unvoiced differences | Common voiced frames | Mean absolute F0 difference (cents) |
| --- | --- | --- | --- | --- |
| 00003 | 7 / 1266 | 50 | 1178 | 35.3310 |
| 00005 | 12 / 1149 | 49 | 1049 | 25.0367 |
| 00024 | 16 / 1008 | 48 | 918 | 43.7510 |
| 00402 | 7 / 633 | 20 | 516 | 25.2048 |
| 00420 | 5 / 563 | 15 | 501 | 35.5406 |

Duration discrepancies are one-frame boundary ownership differences; training F0
is measured whereas native F0 is score/voicing conditioned. Neither difference
alone establishes a bug or authorizes changing existing labels/cache revisions.

A fresh-session 10-step ablation on song 00005, preserving native sample dynamics,
tests these differences directly. Each changed F0 conditions both acoustic and
vocoder graphs, unlike the earlier vocoder-only F0 swap.

| Conditioning | Mel MAE | Pitch mean absolute cents | Within 50 / measurable | Unmeasurable |
| --- | --- | --- | --- | --- |
| Native baseline | 2.837745 | 114.587734 | 919 / 996 | 109 |
| Training durations only | 2.842628 | 109.514191 | 920 / 992 | 107 |
| Training F0 only | 2.838651 | 120.080445 | 916 / 1003 | 107 |
| Both training inputs | 2.843495 | 119.644037 | 911 / 998 | 104 |

All cases remain `MISMATCH`; training-input substitution does not repair mel
reconstruction in this example. Native baseline WAV is byte-identical to prior
replay. Retained directory: `training-conditioning-ablation-00005-r1/` under
the pause-corpus root. `experiment.json` SHA-256:
`7883d23689c5ef34a1c34d78c1ca9bf500e45561dfa8241fc7cd99ba0358ac41`.
It binds the dataset, native inputs, graphs and source. This song overlaps vocoder
training; neither this ablation nor the cohort establishes musical qualification.

Decision: do not patch duration rounding, substitute measured F0 at runtime, or
relax pitch checks based on these results. Next assess acoustic reconstruction
on training and validation material under the same fresh-session inference
contract to distinguish poor learned reconstruction from generalization alone,
then select a bounded training/model intervention with frozen regression checks.

## Corrected five-song sampler sweep: no production change

All 15 cases (five fixed songs at 10, 20 and 32 steps) completed with fresh
acoustic/vocoder sessions per case and exact captured native dynamics. Every
10-step baseline matches both native master channels after the fixed float32
center-pan gain, with maximum sample errors between 6.3330e-8 and 7.4506e-8.
No fitted gain, offset search, changed pitch threshold or excluded failure was used.
This supersedes the session-reusing step experiments preserved below.

| Steps | Within 50 cents / measurable pairs | Weighted pitch MAE (cents) | Unmeasurable frames | Voicing mismatches |
| --- | --- | --- | --- | --- |
| 10 | 3732 / 3974 (93.9104%) | 70.1603 | 378 | 144 |
| 20 | 3749 / 3992 (93.9128%) | 69.8606 | 364 | 142 |
| 32 | 3755 / 3997 (93.9455%) | 66.4902 | 357 | 142 |

All 15 strict pitch comparisons remain `MISMATCH`. Song 00420 worsens from
31.7524 to 38.8146 cents at 32 steps; the aggregate is not universal improvement.
Mel MAE improves slightly at 20 steps on all five songs and is slightly worse at
32 than at 20. Changing steps alone does not resolve the conditioning problem.
These are diagnostic measurable-pair statistics, not full-product acceptance.

Artifacts: `/Users/lhs/seam-corpus-pauses-2026-09-19-r1/sampler-steps-e13-v2-cohort-fresh-r2/`.
Each song retains all three WAVs, full pitch comparisons, capture/source hashes
and baseline parity. Top-level `experiment.json` SHA-256:
`8f9a076a35d8c92c3b05e1e5094ed7f8abee3f47510da48cc33d55729b4bcde2`.
The four refreshed captures plus the earlier song 00024 capture now all contain
native dynamics runs; their application rerenders retain the original master hashes.
Full Python training-tool suite: 258 tests run, 257 passed, one skipped (46.834 s).

Decision: keep production at 10 steps and retain both vocoder candidates. Next
compare native tokens/durations/F0 against the acoustic training-conditioning
representation for the same captured sources. Resolve any proven mismatch before
further training; do not change labels or boundaries solely to improve a metric.
The three known vocoder-training overlaps remain disclosed, and musical
qualification is still open. Windows support remains TODO.

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

## Completed graph replay and conditioning ablation

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

### Initial sampler-step experiment (song 00005 only)

**Superseded as a step-effect experiment:** this initial sweep reused an ONNX
acoustic session. Its seeded random generator advances per call, unlike the
production worker's fresh session per request. Therefore the 20/32-step rows
change both noise draw and steps; they cannot isolate the effect of step count.
The baseline 10-step replay remains valid. Artifacts are preserved, not erased.

With the exact replay inputs and epoch-two vocoder fixed:

| Steps | Mel MAE | Pitch mean absolute cents | Within 50 / measurable | Unmeasurable |
| --- | --- | --- | --- | --- |
| 10 | 2.837745 | 114.587734 | 919 / 996 | 109 |
| 20 | 2.725565 | 61.292000 | 943 / 984 | 114 |
| 32 | 2.743716 | 54.068293 | 929 / 977 | 122 |

All strict comparisons still fail. More steps improve this song's measured pitch
error but lose measurable coverage, and 32 steps do not minimize mel MAE. The
10-step raw WAV is byte-identical to the earlier ablation baseline. Do not pick
a production default from this one training-overlap song. Native prepared-input
exports for the other four fixed songs are being collected for the same sweep.

Artifacts: `sampler-steps-e13-v2-00005-r1/` under the pause-corpus root.
`experiment.json` SHA-256:
`5d903255abcd52575efca88c1973a875d44d9520cfa024707a247ff39594940e`.
All three WAVs and full-grid pitch comparisons are retained. Production remains
at 10 steps and the candidate bundle is unchanged.

## macOS authoring regression alongside training

### Replay sweep correction and dynamics completeness

The initial cohort extension `sampler-steps-e13-v2-cohort-r1` completed song
00003, then stopped on song 00024 when the baseline differed from native output
by 0.157964. This fail-closed parity check prevented using that replay as native
evidence. Its reused acoustic session was the wrong execution model: see the
explicit session-scoped RNG contract in `tools/voice_model_training/onnx_acoustic.py`.
All previous multi-call sampler sweeps are confounded, including song 00003's
apparent step regression. Do not select a production default from them.

The test harness additionally exports exact sample-domain `dynamicsRuns`, so
future replay applies native finalization gains instead of inferring them from
rest labels. Both English and moraic-nasal CTests passed (3.99 seconds); song
00024's rerender remains byte-identical to its baseline master. The updated
capture is `replay-e2-00024-r2-inputs.json`. No shipped worker behavior changed.

Corrected step experiments must create fresh acoustic sessions for every case,
verify baseline parity per song, and retain the fixed five-song selection and
all coverage losses. The fresh-session song 00005 rerun completed under
`sampler-steps-e13-v2-00005-fresh-r2`:

| Steps | Mel MAE | Pitch mean absolute cents | Within 50 / measurable | Unmeasurable |
| --- | --- | --- | --- | --- |
| 10 | 2.837745 | 114.587734 | 919 / 996 | 109 |
| 20 | 2.763131 | 117.068838 | 928 / 1004 | 101 |
| 32 | 2.766036 | 102.277514 | 929 / 1000 | 105 |

All remain `MISMATCH`. The 10-step WAV again matches the original replay byte
for byte. Higher steps are not the large pitch repair suggested by the confounded
experiment. Native dynamics for song 00024 are exactly unity throughout, further
excluding a missing gain mask as the cause of that failed baseline replay.

The reusable `tools/voice_model_training/native_input_replay.py` now enforces
fresh single-call sessions, bounded ten-second/48-kHz/80-bin geometry, exact
sample-domain dynamics runs, finite controls and output checks including padding.
Callers must still bind captured graph/project/source identities and validate
native mixer parity. It does not authorize a model or alter production inference.
Nine replay/source-vocoder unit tests pass, including session lifecycle and
malformed-capture refusal. The real song 00024 replay now matches its native
master within 6.7056e-8 maximum sample error with fixed center-pan gain, resolving
the earlier 0.157964 mismatch without changing any graph or audio threshold.

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
