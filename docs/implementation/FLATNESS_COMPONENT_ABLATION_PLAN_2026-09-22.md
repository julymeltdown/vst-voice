# Flatness/level 2x2 component ablation plan (staged, awaiting clearance)

Status: STAGED for developer-2 clearance. The split-coefficient objective,
the multi-arm evaluator, the two new training configurations and the
production-equivalence probe are implemented; the probe has PASSED. No
ablation training run has been launched.

## Question

The completed paired experiment (control weight-0 vs combined auxiliary at
lambda = 0.08040502229238589) produced a STOP: the combined flatness+level
auxiliary moved unvoiced flatness AWAY from the reference on the frozen
development panel (target-relative error 3.798 vs 2.832 nats, 0/5 songs
improved). The unanswered, directly actionable question is which loss
component produced that result: the flatness term, the level term, or
only their combination.

## Cells

- base (0, 0): existing completed control run
  acoustic-flatness-control-e9-r1, config
  training-flatness-aux-calibration-r1.json
  (bb5d5e5a202db45f3dc06835074d0186ece51a3019a0fc789c24e3b51d1182d8);
- combined (lambda, lambda): existing completed treatment run
  acoustic-flatness-unvoiced-e9-r1, config
  training-flatness-aux-unvoiced-r1.json
  (ebc255e6f95fedb834ea10f1ccc5eedcbf7ea1f322540da7cb9d95f951c92e5d);
- flatness-only (lambda, 0): NEW run
  acoustic-flatness-flatonly-e9-r1, config
  training-flatness-components-flat-only-r1.json
  (9035426ac73f3c93a0eda02c9c8c37ed71db602895e608fc557d8e62319fae52);
- level-only (0, lambda): NEW run
  acoustic-flatness-levelonly-e9-r1, config
  training-flatness-components-level-only-r1.json
  (34005dd69a2954cfeddc23a01f9ed1070577c59b7852f75745116f106f08605f).

lambda = 0.08040502229238589, the r2 calibration value, reused unchanged.
Coefficients are deliberately NOT recalibrated to equalize per-component
gradient norms: rescaling would conflate removing a component with
changing its magnitude.

## Implementation

DiffSingerDDPMUnvoicedFlatnessComponentsObjective (new kind
"unvoiced-target-log-flatness-components", objective id
diffsinger-ddpm-l1-unvoiced-target-log-flatness-components-v2):
identical forward, timestep/noise draws, unvoiced/rest/silent masking,
alpha_bar weighting and last_draw attribution fields as the single-weight
objective. The auxiliary is

    (flatness_weight * flatness + level_weight * level) * mask * alpha_bar

with one deliberate exception: when flatness_weight == level_weight the
code computes weight * ((flatness + level) * mask * alpha_bar) in the
single-weight objective's exact operation order, so the equal-weight
corners are bitwise identical rather than merely algebraically equal.

Config schema: schemaVersion 3 unchanged; the new kind requires
{kind, flatnessWeight, levelWeight, unvoicedSymbols} while the existing
kinds keep {kind, weight, unvoicedSymbols}. Warm-start admission treats
auxiliaryObjective as a governed field exactly as before.

## Cell-reuse equivalence evidence (already executed)

acoustic_ablation_equivalence.py, receipt
/Users/lhs/seam-corpus-pauses-2026-09-19-r1/ablation-equivalence-e8-r1.json
(file sha256 0e1d4869d08e49ce80c322fab99348f2c8869d2434a9d4379db3186d9c507d47).

Method: the exact production setup is reconstructed (e8 parent
04f72263, admitted dataset snapshot 09756315 verified against the
recorded epoch identity, seed 933, fresh AdamW 0.0008, identical source
order), then the first 24 admitted updates are replayed in lockstep.
Within each update the Torch RNG state is captured before the
single-weight update and restored before the split-weight update, so
both consume identical timestep/noise draws.

Result: equivalent = true on both corners. For all 24 replayed updates:
the single-weight replay matched the recorded production steps.json
bitwise (loss, gradientNorm, sourceId, timesteps, noiseSha256, masked
part sums); the split objective matched the single-weight objective
bitwise on the same fields; and post-update model parameters AND AdamW
state tensors were torch.equal between the lockstepped models. Because
parameters, optimizer state and RNG are bitwise identical after every
replayed update, equivalence extends inductively to the remaining
updates of the epoch (deterministic ops, identical consumption order).

Scope limit: this demonstrates equivalence of the loss/gradient/update
path for the two coefficient corners on this dataset slice; it is not a
proof for arbitrary batches, and it says nothing about which component
is beneficial.

## Frozen evaluation and decision rule (declared before launch)

Evaluator: the corrected acoustic_flatness_pair_eval.py generalized to N
named arms (--arm NAME=DIR, repeated), same corrected primary/guardrail
computation on ACTUAL returned mel draws and vocoder-rendered audio -
not on sampler t0 clean estimates. Draw policy, replay inputs, vocoder
binding (0731edd1, graph 9a733810, zero-v1), held-out panel and
reporting are unchanged from the paired evaluation; existing saved
control/combined outputs supply their metrics where identities and
protocol match, with no cherry-picked draws.

Panels stay separate: the zero-breathiness five-source development
panel remains the primary panel; the 12-item held-out panel is reported
as its own separately labeled panel (one-draw zero-breathiness,
mel-based guardrails not evaluated). Listening stays NOT_REVIEWED.

Component decision (frozen):

- RETAIN a term only if its isolated cell (flatness-only or level-only)
  improves the target-relative unvoiced flatness error on FINAL sampler
  output versus the base cell - strictly lower mean
  |log_flatness_hat - log_flatness_ref| over the fixed eligible-frame
  phone inventory - without unacceptable level or pitch damage, where
  "unacceptable" means firing any guardrail that the paired evaluation
  already froze (waveform RMS ratio outside [0.80, 1.20] mean-of-phones,
  mean |rmsRatio - 1| worse than base by > 0.05, measurable voiced pitch
  pairs down > 2%, weighted pitch error worse by > 5 cents, new clipping
  or new silence, voiced target-relative flatness error up > 0.05 nats).
- If neither isolated cell satisfies the above, STOP this auxiliary
  family at this tested setting; no layer/weight sweep is opened.
- The combined cell is reported for completeness; a combined-only
  benefit with both isolated cells neutral/negative is recorded as an
  interaction observation, not a retain decision, and would need its own
  cleared follow-up.

Interpretation bounds: one seed, one latent schedule, five development
sources - a bounded local component attribution, not a unique training
mechanism or qualification.

## Launch specification (only after clearance)

Frozen dispatch context identical to the paired experiment: same working
directory, interpreter, corpus root, e8 parent, dataset/target/
conditioning/policy identities, seed 933, 267-update epoch, 900-second
budget, minimum 4 GiB free disk, one tmux session per arm, output
directories must not exist at dispatch.

Flatness-only arm:

  cd /Users/lhs/Downloads/project-seam-usable-alpha-u3-master && \
  build/neural-runtime/diffsinger-telemetry-free-env/bin/python \
    -m tools.voice_model_training.train \
    --training-config /Users/lhs/seam-corpus-pauses-2026-09-19-r1/prepared-combined/training-flatness-components-flat-only-r1.json \
    --training-sha256 9035426ac73f3c93a0eda02c9c8c37ed71db602895e608fc557d8e62319fae52 \
    --dataset-config /Users/lhs/seam-corpus-pauses-2026-09-19-r1/prepared-combined/dataset-config-conditioned-r1.json \
    --dataset-sha256 b4a5bf1cfd4da9a625dc36443ea61ba3bbe3a5ca2723b2796ef400554132d327 \
    --targets /Users/lhs/seam-corpus-pauses-2026-09-19-r1/prepared-combined/targets.json \
    --targets-sha256 9b77f87fd048b1d2149f9f6294d26a9972e441e04c1abda052a34829ec21a050 \
    --source-root /Users/lhs/seam-corpus-pauses-2026-09-19-r1/prepared-combined \
    --conditioning /Users/lhs/seam-corpus-pauses-2026-09-19-r1/prepared-combined/conditioning-breathiness-r1 \
    --trusted-checkout /Users/lhs/Downloads/project-seam-usable-alpha-u3-master/build/neural-runtime/DiffSinger-source \
    --warm-start /Users/lhs/seam-corpus-pauses-2026-09-19-r1/acoustic-breathiness-r1/epoch-000008 \
    --warm-start-receipt-sha256 04f72263b700804cc4557879208def3ca4157d68955dea516b179140f755007e \
    --rights-policy-sha256 ec1f39f07b9b881565956dbed4f4abd8a1375284b0813ba152132e035cae0185 \
    --label-policy-sha256 e3f2fe1b4c270611cdd4cfc22a035bacdce78e4d6deb90c57997c3aae379e81b \
    --epochs 1 --maximum-run-seconds 900 --retain-checkpoints 1 \
    --minimum-free-bytes 4294967296 \
    --output /Users/lhs/seam-corpus-pauses-2026-09-19-r1/acoustic-flatness-flatonly-e9-r1

Level-only arm: identical except
--training-config .../training-flatness-components-level-only-r1.json,
--training-sha256 34005dd69a2954cfeddc23a01f9ed1070577c59b7852f75745116f106f08605f,
--output .../acoustic-flatness-levelonly-e9-r1.

Post-run audit: verify all 267 source/timestep/noise draws match the
recorded pair draws and coverage is complete; export each new arm and
run the four-cell evaluation under the frozen rule.

## Out of scope

No promotion, qualification, release eligibility or deployment claim.
No soft clamp, straight-through estimator, new noise target, encoder
freeze, or parameter-group surgery in this experiment. The vocoder line
stays at 0731edd1. One seed per cell; no automatic continuation.
