# Acoustic flatness-objective paired experiment plan (staged, awaiting clearance)

Status: STAGED for developer-2 re-review after the correction pass.
Nothing below has been launched; the r2 calibration receipt exists but
no training run has started.

## Question

Does one additional epoch of conditioned acoustic training under a
combined target-relative log-flatness + log-energy auxiliary objective
produce less spectrally concentrated unvoiced mel predictions than the
same budget under the base objective? This pair tests the combined
auxiliary intervention; it does NOT isolate flatness from level.

## Bounded evidence basis (corrected per review)

On the tested configuration - one development song, eight posterior
draws, ten sampler steps - the conditioned epoch-8 model produced
unvoiced predictions whose phone-by-draw flatness stayed in
0.0011-0.0885 against a reference mean of 0.712 (receipt
acoustic-sample-variance-e8-r2.json, matched ceil/floor masks). The
posterior is not collapsed (mel std across draws 2.56), but none of the
eight evaluated draws reached reference-like unvoiced flatness; seed
variation alone did not fix this configuration. Coverage, gradient
share and conditioning magnitude were measured as non-dominant
explanations; gradient error-share is not proof of sufficient gradient
pressure. The e9 shape+level auxiliary restored energy but not
aperiodic structure.

## Arms

Both arms warm-start from the same frozen parent: acoustic-breathiness-r1
epoch-000008, receipt 04f72263b700804cc4557879208def3ca4157d68955dea516b179140f755007e,
checkpoint 914eef408685ea1b57f8dbb1879e1491c67e9c7b897c9ae34714de594b53e602.
Fresh AdamW at the captured learning rate 0.0008, seed 933, identical
dataset snapshot 09756315c5fc5aaca80f428ef920ce8de24512602d9905c76959943ef4c7301e
(assembly configuration b4a5bf1cfd4da9a625dc36443ea61ba3bbe3a5ca2723b2796ef400554132d327),
identical targets 9b77f87fd048b1d2149f9f6294d26a9972e441e04c1abda052a34829ec21a050,
identical source order and timestep/noise draws, one epoch of 267
updates each.

- Control: training-flatness-aux-calibration-r1.json
  (bb5d5e5a202db45f3dc06835074d0186ece51a3019a0fc789c24e3b51d1182d8),
  kind unvoiced-target-log-flatness at weight 0.0 - bit-for-bit the base
  objective, so the adapter itself is not a variable.
- Treatment: training-flatness-aux-unvoiced-r1.json
  (ebc255e6f95fedb834ea10f1ccc5eedcbf7ea1f322540da7cb9d95f951c92e5d),
  same kind at weight 0.08040502229238589 from the r2 calibration
  receipt aux-calibration-flatness-e8-r2.json (regenerated after the
  mask change; the r1 receipt is superseded).

## Objective under test

DiffSingerDDPMUnvoicedFlatnessObjective: epsilon DDPM loss unchanged,
plus on labeled unvoiced, non-rest, non-silent frames a smooth-L1 on
per-frame log spectral flatness (log_flatness(z) = mean(z) -
(logsumexp(z) - log M)) against the reference's own log flatness, plus
the e9 log-energy level term, weighted by alpha_bar_t and the single
calibrated coefficient. Frames whose reference log-mean amplitude sits
at the capture floor (level_ref <= -11.5) are excluded and counted in
last_draw.excludedSilentFrames; flatness and level part sums are logged
separately in last_draw.flatnessTerm / last_draw.levelTerm. The
forward, timestep and noise draws are identical to the base objective;
weight zero reproduces the base objective bit for bit.

## Frozen decision rule (declared before launch)

Primary statistic: per-phone mean predicted-mel flatness (arithmetic
flatness, the run_spectral_flatness statistic) on the five development
replay sources, aggregated as the mean of per-phone means. The
treatment ADVANCES only if BOTH hold:

- the unvoiced primary statistic improves versus the control arm by at
  least 15 percent relative (ratio treatment/control >= 1.15) AND at
  least 4 of 5 songs improve on their own per-song mean;
- no guardrail below fires.

Guardrails (each evaluated on its own panel; no cross-panel
cancellation):

- unvoiced waveform RMS level ratio (fixed vocoder 0731edd1) must stay
  within [0.80, 1.20] as a mean of per-phone ratios;
- unvoiced mean |rmsRatio - 1| may not exceed the control arm's value
  by more than 0.05;
- measurable voiced pitch pairs may not drop below the control arm by
  more than 2 percent, AND weighted mean absolute pitch error may not
  regress by more than 5 cents;
- no reconstructed item may show new clipping (peak >= 1.0) or new
  silence (RMS < 1e-4 over a voiced phone span) absent in the control;
- voiced target-relative flatness error |log_flatness_hat -
  log_flatness_ref| may not increase versus control by more than 0.05
  in natural-log units (an increase in voiced flatness toward the
  reference is permissible).

A null or negative result is recorded and the acoustic line returns to
mechanism analysis. One epoch per arm; no sweep, no architecture
change, no automatic continuation.

## Frozen evaluation specification

Replay inputs: the five frozen development captures
replay-e2-{00003,00005,00024,00402,00420}-r2-inputs.json (digests bound
in the prior paired receipts). Sampler steps: 10.

Draw policy: for each (source, arm), eight mel draws are taken inside
ONE onnxruntime session so seeded RandomNormal ops advance per Run and
the draws are genuinely distinct; the session is discarded per source.
predicted_mel's fresh-session-per-call behavior would repeat the same
seeded sequence and is not used for the multi-draw panel. All eight mel
outputs per (source, arm) are written to disk and bound by sha256 in
the receipt so mel-level and waveform-level diagnostics analyze the
SAME realizations.

Waveform evaluation: each retained mel draw is rendered through the
fixed vocoder export bound to receipt 0731edd1 (graph 9a733810,
zero-v1), then campaign_phone_diagnostics (lag-256 correlation, RMS
ratio, per-phone) and the retained-per-lag dense probe run per draw and
per source; reported as mean across draws with per-draw ranges kept.

Reconstruction panel: because the acoustic model is the variable, the
12-item held-out panel runs BOTH acoustic predictions through the fixed
vocoder (acoustic-then-vocoder), not the reference-mel-fed vocoder-only
comparison used for vocoder arms. The 12 source IDs are absent from e8
coveredSourceFrames; ancestry/independence limitations are stated
explicitly and combinedModelHoldoutVerified stays false.

Listening: clip list and order are frozen (the five development
reconstructions, both arms, randomized order, blind labels); the panel
is marked NOT_REVIEWED until human listening actually occurs and is
never treated as a pass by absence.

## Launch specification

Frozen command per arm (run inside tmux; literal expected hashes at
dispatch, not recomputed):

  python -m tools.voice_model_training.train \
    --training-config <cfg> --training-sha256 <cfg sha> \
    --dataset-config prepared-combined/dataset-config-conditioned-r1.json \
    --dataset-sha256 b4a5bf1cfd4da9a625dc36443ea61ba3bbe3a5ca2723b2796ef400554132d327 \
    --targets prepared-combined/targets.json \
    --targets-sha256 9b77f87fd048b1d2149f9f6294d26a9972e441e04c1abda052a34829ec21a050 \
    --source-root prepared-combined \
    --conditioning prepared-combined/conditioning-breathiness-r1 \
    --trusted-checkout build/neural-runtime/DiffSinger-source \
    --warm-start acoustic-breathiness-r1/epoch-000008 \
    --warm-start-receipt-sha256 04f72263b700804cc4557879208def3ca4157d68955dea516b179140f755007e \
    --rights-policy-sha256 ec1f39f07b9b881565956dbed4f4abd8a1375284b0813ba152132e035cae0185 \
    --label-policy-sha256 e3f2fe1b4c270611cdd4cfc22a035bacdce78e4d6deb90c57997c3aae379e81b \
    --epochs 1 --maximum-run-seconds 900 --retain-checkpoints 1 \
    --minimum-free-bytes 4294967296 \
    --output acoustic-flatness-{control,unvoiced}-e9-r1

Caps: maximumUpdates 267 (inside the config), maximum-run-seconds 900,
minimum free disk 4 GiB at launch; a run that does not complete 267
verified-update coverage is recorded as failed, not partial. Post-run
audit verifies all 267 source/timestep/noise draws match between arms
and coverage is complete.

## Calibration evidence

aux-calibration-flatness-e8-r2.json: 8 training phrases with unvoiced
coverage; median lambda for a 10% auxiliary-to-base gradient ratio
0.08040502229238589. Caveat: every calibrated phrase drew timestep 495
under the reset RNG, so the 10% figure is a local gradient-norm scale
heuristic, not an across-schedule guarantee; flatness and level part
sums are now logged separately in last_draw for post-hoc attribution.

## Out of scope

No promotion, qualification, release eligibility, or deployment claim
follows from either outcome. The vocoder line stays at 0731edd1.
