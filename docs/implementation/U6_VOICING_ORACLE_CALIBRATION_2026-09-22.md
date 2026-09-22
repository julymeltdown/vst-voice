# U6 source-lag oracle calibration — prospective revision 1

Date: 2026-09-22. Baseline: `8b8668d9563130fba43e4b8ec2340475c887e5b0`.
Status: fixed-grid calibration executed; construction-label scope limitation reproduced.
This is additive measurement research, not production unvoicing or singer qualification.

## Question and fixed controls

Can the frozen `source-lag-ncc-v1` cutoff distinguish a periodic excitation from
independent random excitation after resonant filtering? The exact existing C++
`periodicity()` function is reused without modification. Its cutoff stays 0.65.
This probes that cutoff alone, not the entire speech conjunction (which also
requires RMS, spectral retention and improvement over the source).

The following grid is declared before the C++ calibration is executed:

- Sample rates: 44,100 and 48,000 Hz.
- Oracle frequency / tone or resonator center: 220, 440, 991.01406569073924 Hz.
- Random seeds: 91073, 71931, 26701; `std::mt19937`, direct uint32 half-bin
  mapping to uniform Float32 samples. No implementation-defined distribution.
- Resonator bandwidth parameters: 20, 50, 100, 200, 400 Hz. The radius is
  `exp(-pi * bandwidth / rate)`. Independent causal recurrence:
  `y[n] = x[n] + 2*r*cos(2*pi*center/rate)*y[n-1] - r*r*y[n-2]`.
- Duration: 800 ms; score the 500–800 ms interval after resonator warmup.
- Positive controls: one pure sinusoid per rate/center (6 total).
- Aperiodic-excitation controls: white noise and five filtered versions for
  each rate/center/seed (108 total). No periodic driver is present in these
  constructions. This is NOT a perceptual unvoiced or natural-breath label.
- Mixtures: unit-RMS tone/noise weighted by square roots of 0.25/0.50/0.75
  and their complements (54 total). These are nominal component-power weights;
  finite-sample cross terms mean they are not exact proportions of total power.
  Mixed controls are reported separately, not forced into binary error counts.
- Every control is peak-normalized to 0.45 and stored as Float32 WAV. The exact
  same Float32 samples are measured and checked after WAV readback.

The 168 controls do not use the experimental converter or production synthesis.
Their labels derive from generator construction, not the measured score.
False-positive/negative counts refer only to these construction labels. They
are not clinical/statistical population estimates or perceptual error rates.
The three seeds, six frequency/rate combinations and bandwidth variants are a
small fixed diagnostic grid, not independent samples of all singing material.

The regression requires the periodic controls to exceed 0.9 and white controls
to remain below 0.15. At least one filtered-noise counterexample to the 0.65
cutoff is expected from a prior independent NumPy pilot. All fixed cases are
retained, including those that do not breach. No post-hoc parameter selection
or threshold change is permitted. Failure to reproduce must be reported.

## Artifact and historical-evidence policy

New executions write into
`<build>/target-voicing-experiment/<executable-sha256>/`. Speech assessments
remain algorithm revision 1 with their original thresholds and FAIL results.
The historical assessments and WAVs directly under
`<build>/target-voicing-experiment/` are left untouched.

The new `oracle-calibration-v1/` subdirectory contains all 168 WAVs and
`calibration.json`, with exact output hashes, executable identity, geometry,
generator arguments, correlations and construction-label error counts.
It always reports no replacement metric, production disabled, release ineligible
and listening NOT_REVIEWED. Per-executable directories avoid overwriting prior
binary-bound assessments; they remain local diagnostic artifacts, not release
evidence or a transactional artifact store.

## Consequence boundary

A reproduced counterexample limits interpreting this short-lag correlation as
an excitation classifier. It does not demonstrate that either failed speech
output sounds unvoiced, natural, intelligible or musically usable. The historical
speech FAIL and normal production Unsupported behavior remain in force.
No alternative metric, widened threshold or production promotion is introduced.
Any proposed replacement must be validated on separately held-out controls and
fixed speech material, report old and new results side by side, and receive a
separate contract review before being used for acceptance.

## Observed results

Fresh Release and Debug builds succeeded. The dedicated target passes six
engineering cases in each configuration (1.71 and 14.57 seconds). The sixth
case contains the 168 controls; it is not 168 independent clinical or listening
tests. The other five cases preserve the original experiment regression coverage.
No production source changed, and the broader product suites were not rerun for
this additive test-only increment.

| Aperiodic-excitation control | Above the 0.65 cutoff | Correlation range |
|---|---:|---:|
| White noise | 0/18 | 0.008351–0.021145 |
| Resonator bandwidth 20 Hz | 18/18 | 0.707713–0.952647 |
| 50 Hz | 12/18 | 0.442712–0.874000 |
| 100 Hz | 6/18 | 0.201750–0.747901 |
| 200 Hz | 0/18 | 0.040556–0.543903 |
| 400 Hz | 0/18 | 0.001099–0.295152 |

Thus 36/108 aperiodic-excitation constructions are classified as periodic by
this cutoff. All six periodic-tone controls are detected (zero construction-
label false negatives); 54 mixed controls are retained separately. This is a
counterexample to treating that cutoff as a universal excitation classifier,
not evidence of a 33.3% perceptual error rate or a qualified replacement metric.

After omitting only executable identity, the Release and Debug calibration JSON
files are identical, including every correlation and WAV digest. Separate
`shasum -a 256 -c` verification passes for all 168 retained WAVs per configuration.
Both new speech assessments are identical to their historical reports except
executable identity; the original historical report hashes remain unchanged.

| Identity | SHA-256 |
|---|---|
| Release executable / artifact subdirectory | `aaeedd8c2e978b7803c8befd5d2d6b114590fa580d58f5299200aa310e8c52c3` |
| Debug executable / artifact subdirectory | `2b05889dfdb47d92bf8d48fba47656f384c69de5114f554ec3c63db4fc73ed7a` |
| Release calibration.json | `b76c35f09f4cc0bd2619d60dee0bc3b8bf7782298e60e3401f3a9728dcb41024` |
| Debug calibration.json | `6533716e23d11a2531a1308d32cf9d37cd1b723330ed9fc68ce6e22b321dd1db` |

Build/CTest logs are `u6-voicing-calibration-{build,ctest}.log` in each build
directory. Use the existing default-OFF experiment target's reproduction commands;
its output names the exact executable-bound artifact directory. There is no new
runtime dependency, production target or applied-control capability.

Developer 2 approved this next research direction separately from approving
the original experiment at `86a4ae43`; implementation review of this calibration
is still required after execution.
