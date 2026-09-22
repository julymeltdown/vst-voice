# U16 low-frequency contamination control panel — prospective revision 1

Date: 2026-09-22. Prepared after the source-frequency diagnosis in
U16_WORLD_REFERENCE_COMPARISON_2026-09-22.md, before implementing or running this
panel. Baseline code: `48f02a3fbc968ba9f1d492db3b663657f2ed5b56`.
Status: fixed next-experiment specification, NOT executed or qualified.

## Question

Does adding only a known low-frequency component to the existing voiced controls
reproduce the large raw reconstruction-energy/spectral changes? This tests a
contributor suggested by the frozen technical speech fixture. It cannot prove
that the fixture has the same physical cause, repair an acoustic FAIL, or select
a production renderer by itself.

## Fixed 42-input panel

- Rates: 44,100 and 48,000 Hz.
- Underlying voiced F0: exact MIDI 48, 60 and 72 frequencies from
  `440 * exp2((midi - 69) / 12)`.
- Duration: 500 ms. Start from the exact existing `diagnostics::vowel` generator
  in `target_voicing_test_metrics.hpp`; multiply each Float32 sample by the same
  fixed gain `0.2F`. Its existing formants/envelope/generator normalization are
  unchanged. This fixed common gain provides headroom; there is no per-case gain
  or normalization based on measured/candidate output.
- For each of those six voiced bases, one zero-contamination case, plus six
  contaminated cases: LF frequencies 10 and 30 Hz crossed with peak amplitudes
  0.05, 0.20 and 0.60; sine phase zero. Thus `6 * (1 + 2 * 3) = 42` inputs.
- LF sample: `float(amplitude * sin(2*pi*frequency*sampleIndex/rate))`.
  Combined sample: `float(double(voicedSample) + double(lfSample))`.
  Retain all three Float32 components: voiced, LF (including the zero control),
  and combined source. Recomposition from retained component samples must match.
  The common bound is 0.09 + 0.60 = 0.69; never clip or normalize to enforce it.
- IDs: `lf-{rate}-{midi}-none` and
  `lf-{rate}-{midi}-{10|30}hz-a{05|20|60}`. No duplicate zero cases.

Do not add a candidate-dependent frequency/amplitude sweep or choose a passing
subset. Actual mixed-signal band shares are measured, not assumed equal to an
amplitude-squared formula: finite-window cross terms can exist.

## Fixed processing and measurement

Use the same two explicitly pinned WORLD source arms and same SEAM experimental
converter. Analyze with the known underlying voiced F0 held constant for every
member of a pair. No DIO, re-estimated F0, score-frequency substitution, or
analysis with zero target F0 in this panel. Keep the current CheapTrick/D4C
options, exact source length, serial calls and same-build repeat checks.

For each admitted WORLD analysis, retain original-F0/AP reconstruction and
forced-unvoiced F0=0/AP=1 output from the SAME envelope. Preserve raw rejected AP
and every ERROR/NOT_RUN separately from acoustic FAIL. The SEAM comparator runs
independently of WORLD admission. Its stream ID, absolute origin and noise seed
stay identical to the existing non-speech comparator, not derived from new case IDs.

Keep the original 100–400 ms window and numeric conjunction unchanged: RMS ratio
in (0.7,1.3), source-lag NCC below 0.65 AND below source, maximum five-band share
error below 0.20. Use the underlying voiced F0 as the frozen scoring frequency
even when a low-frequency component dominates total power. Reconstruction NCC
is still NOT_APPLICABLE_RECONSTRUCTION; retain its raw numerical losses.

Report source DC/LF shares separately using the additive source-frequency tool,
and compare each contaminated analysis/reconstruction with its declared clean
pair. These comparisons describe sensitivity, not new pass thresholds. Do not
infer perceptual voicing from spectral peaks or high/low correlation.

## Retention and decision

Use a new `lf-controls-v1` packet namespace bound to the executable, source lock
and this protocol's SHA-256. The existing nine-case comparison and all historical
assessment/calibration reports stay untouched. Require all 42 IDs and raw
components/results; partial execution cannot become an aggregate success.

After reviewing all results, distinguish: a reproduced LF sensitivity; other
unexplained losses; invalid analysis; and inconclusive results. Any next method
change needs its own explicit rationale. Supplementary real speech/singing
selection must use source-only provenance/coverage/quality strata frozen before
renderer results, preserve LF-heavy stress cases, and retain a separate untouched
validation partition. No recording, download, corpus approval, implementation
acceptance, U6/U16 closure or production promotion is authorized by a numeric
result from this panel.
