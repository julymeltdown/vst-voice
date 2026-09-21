# Acoustic flatness-objective paired experiment plan (staged, awaiting clearance)

Status: STAGED for developer-2 clearance. Nothing below has been launched;
the calibration receipt exists but no training run has started.

## Question

Does one additional epoch of conditioned acoustic training under a
target-relative log-flatness auxiliary objective produce noise-like
unvoiced mel spectra, where the same budget under the base objective
(control) and under the shape+level auxiliary (e9) did not?

## Rationale

Evidence chain (INTEGRATED_SINGER_EXECUTION): the conditioned model's
unvoiced flatness is 0.01-0.09 against a reference ~0.71 across five
songs and eight posterior draws; coverage, gradient share, sampling
steps, prior magnitude and posterior collapse are all ruled out. The
e9 shape+level auxiliary restored energy but not aperiodic structure.
The remaining hypothesis is that the objective does not score spectral
concentration on unvoiced frames.

## Arms

Both arms warm-start from the same frozen parent: acoustic-breathiness-r1
epoch-000008, receipt 04f72263b700804cc4557879208def3ca4157d68955dea516b179140f755007e,
checkpoint 914eef408685ea1b57f8dbb1879e1491c67e9c7b897c9ae34714de594b53e602.
Fresh AdamW at the captured learning rate, seed 933, identical dataset
(b4a5bf1c), identical targets (9b77f87f), identical source order,
identical timestep and noise draws, one epoch of 267 updates each.

- Control: training-flatness-aux-calibration-r1.json
  (bb5d5e5a202db45f3dc06835074d0186ece51a3019a0fc789c24e3b51d1182d8),
  kind unvoiced-target-log-flatness at weight 0.0 - bit-for-bit the base
  objective, so the adapter itself is not a variable.
- Treatment: training-flatness-aux-unvoiced-r1.json
  (10ec6c8dd0046f84f47990b3ee15f939d39edbc4e5c642a2e9d6f7ab4b7176e2),
  same kind at weight 0.06300557133924936 from the gradient-only
  calibration receipt aux-calibration-flatness-e8-r1.json.

## Objective under test

DiffSingerDDPMUnvoicedFlatnessObjective: epsilon DDPM loss unchanged,
plus on labeled non-silent unvoiced frames a smooth-L1 on per-frame log
spectral flatness (mean(z) - (logsumexp(z) - log M) in log-mel space)
against the reference's own log flatness, plus the e9 log-energy level
term, weighted by alpha_bar_t and the single calibrated coefficient.
The auxiliary targets the measured statistic of the data, not
flatness=1 or injected noise.

## Frozen numeric guardrails (declared before launch)

Primary: unvoiced mean predicted-mel log-flatness on the five
development replay sources must move toward the reference (reported as
flatness, target ~0.71 unvoiced mean).

Guardrails - the treatment arm is rejected if ANY holds:

- unvoiced mean RMS level ratio (waveform, against the same sources
  through the fixed vocoder 0731edd1) falls below 0.80 or above 1.20;
- unvoiced mean |rmsRatio - 1| exceeds the control arm's value by more
  than 0.05;
- measurable voiced pitch pairs drop below the control arm's count by
  more than 2 percent;
- any reconstructed item shows new clipping (peak >= 1.0) or new
  silence (RMS < 1e-4 over a voiced phone) absent in the control;
- voiced log-flatness mean moves more than 0.05 toward 0 (voiced
  phones must stay harmonic).

Advance requires BOTH: unvoiced flatness improves versus control AND
no guardrail fires. One epoch per arm; no sweep, no architecture
change, no automatic continuation. A null or negative result is
recorded and the acoustic line returns to mechanism analysis.

## Evaluation

Export both arms, run the frozen replay inputs through the fixed
vocoder 0731edd1, then: run_spectral_flatness (mel flatness per phone),
campaign_phone_diagnostics (waveform lag-256 and RMS per phone),
dense-lag probe with per-lag absolute and squared errors retained,
12-item held-out reconstruction summary, and the fixed listening
panel. Mel-level and waveform-level results are reported separately.

## Calibration evidence

aux-calibration-flatness-e8-r1.json: 8 training phrases with unvoiced
coverage, median lambda for a 10% auxiliary-to-base gradient ratio
0.06300557133924936. The near-identical scale to the e9 spectral arm
(0.063089) is expected: both auxiliaries are per-element losses of
similar magnitude under the same alpha_bar weighting.

## Out of scope

No promotion, qualification, release eligibility, or deployment claim
follows from either outcome. The vocoder line stays at 0731edd1.
