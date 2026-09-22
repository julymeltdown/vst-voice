# U6 target-unvoicing experiment — acoustic FAIL, production disabled

Date: 2026-09-22. Baseline: `2fe9871c64d19ab6010cc0932cddd5191acbd41f`.
Status: isolated developer experiment independently approved; acoustic FAIL and production disabled.
**This does not close U6, U16, or any additional roadmap unit.**

## Outcome

A first-party envelope/noise conversion was implemented and exercised through all
four sample adapters during development. Synthetic harmonic/formant controls and
production-path integration tests passed. The provenance-bound speech fixture
did NOT meet the fixed conjunction of acoustic checks, however. After independent
review, ALL production wiring, cache revision changes, compiler fast-path changes
and provisional product tests were removed. Ordinary rendering still reports
Unsupported for conversion-required voiced material; existing known-unvoiced
source behavior is unchanged. Nothing silently falls back to this experiment.

The retained code is under `tools/singing_quality/target_voicing_experiment.*`.
It is compiled only into a separate test/developer executable when
`SEAM_ENABLE_SAMPLE_TARGET_VOICING_EXPERIMENT=ON`; the option defaults OFF.
No production library links it. It cannot enter normal preview/cache/export or
advertise an applied production control. Its separately written WAVs are labeled
experimental assessment artifacts, not product exports or release evidence.

## Frozen speech assessment

Input WAV SHA-256:
`caf8ceb04b864c7501371a2adae46697495e617b9ade178560ba2ea1aa6b8cb9`.
Existing production manifest SHA-256:
`ea199788012edc818459a00504a6043829560b22abe46efd4572fe886df973a5`.
Both are already recorded in the repository singing-quality corpus. This is the
existing public-domain spoken-voice technical fixture, not a female singer,
phonetic inventory, intelligible song, or new rights approval.

At 44.1 kHz the fixed measurement window starts at frame 4410 and contains 13230
frames (100–400 ms). RMS and five spectral-power shares are compared with that
same source window; band boundaries are 400 / 900 / 1800 / 3200 Hz.

The independent original pitch estimator returns 991.014 Hz. Its fixed lag set
defines the correlation oracle in BOTH cases: fractional-period normalized
correlation plus integer lags around 0.5, 1, 2 and 3 periods, each within
max(1 sample, 2%). Take the maximum. Source correlation is 0.992754.
The manifest's nominal root is NOT proof of physical constant F0, and the
estimator's disagreement is not declared an estimator defect.

| Synthesis input | RMS ratio | Fixed-oracle correlation | Maximum band-share error | Assessment |
|---|---:|---:|---:|---|
| Existing root MIDI 67, target MIDI 67: 391.995 Hz | 0.935185 | **0.896720** | 0.003083 | **FAIL: periodicity** |
| Original unconstrained estimator: 991.014 Hz | 0.956426 | 0.601195 | **0.285658** | **FAIL: spectral retention** |

The unchanged conjunction is RMS ratio in (0.7, 1.3), correlation below 0.65 and
below the source, and maximum absolute spectral-share error below 0.20.
Neither tested synthesis input passes. No per-clip passing Hz was selected and
no threshold was relaxed.

Correcting the test from its original estimated carrier to the pre-existing
declared root was reviewed as legitimate only after matching the actual caller
formula (max(target MIDI, root MIDI), here both 67) AND keeping the original
measurement oracle independent of that synthesis parameter. The intermediate
391.995-Hz run that also changed the oracle lag is not acceptance evidence.
Both fixed-oracle cases are now rerun and recorded by the same executable.

`assessment.json` distinguishes `executionStatus: PASS` from computed
`acousticAssessment: FAIL`, and records `productionEnabled: false`,
`releaseEligible: false`, `listeningStatus: NOT_REVIEWED`, exact thresholds,
input/manifest/output/executable hashes, window, oracle and per-case failed
criteria. A CTest that verifies faithful FAIL reporting can pass; that is
**engineering PASS, not acoustic PASS**.

## Experiment algorithm and engineering contracts

- An active note with absent compiled frequency is not silence. Only that exact
  sample-domain mask is eligible. Explicitly unvoiced source-map spans bypass
  conversion; unknown does not mean already unvoiced.
- Manual replacement, enabled-vibrato ownership, accepted source offsets, tempo,
  note gaps and half-open boundaries come from the existing compiled evaluator.
- The experiment takes immutable-original carrier samples through a rolling
  buffer, replacing only eligible samples. Ineligible PCM is never written.
  Preservation is relative to this carrier, not a counterfactual all-voiced
  render with different phase accumulation or whole-unit DC removal.
- Periodic Hann analysis: 32 ms rounded upward to radix-2, FFT 256–16384 across
  8–384 kHz, quarter-hop, absolute-zero lattice, zero padding, normalized overlap.
  Auxiliary storage is O(FFT); no additional full-song sample arrays.
- Power smoothing uses a box radius ceil(max(100 Hz, carrier Hz) * FFT /
  (2 * rate)), followed by a half-radius box. Envelope-filtered deterministic
  noise uses stable unit identity and absolute sample coordinates, never a job,
  chunk or snapshot digest. DC/Nyquist excitation is zero.
- Local gain correction is bounded to 0.25–4; window peak is bounded to four
  times original peak. Zero/below-1e-24 window energy produces no noise.
- Smoothstep transitions occupy at most 3 ms inside eligible runs and compress
  on short runs. A single eligible sample is replaced, but cannot establish
  perceptual aperiodicity and arbitrary continuity with unchanged neighbors.
- Cancellation checks occur at admission and FFT hops. Failed output is
  discarded; there is no reusable mutable renderer state.
- Complete carrier context is required. Arbitrary cropped input is NOT promised
  equivalent. Production experiments had rendered full units before cropping;
  those integrations are not retained as supported product behavior.

Engineering tests cover finite/headroom bounds, repeated execution, stable seeds,
later-input locality, zero/near-zero input, cancellation, invalid input, nonzero
origins, masks under tempo/source offsets/manual/vibrato ownership, explicitly
unvoiced source spans, one-frame alternating maps, 1/2/17/hop-1/hop/hop+1 runs,
and rates through 384 kHz.

Twelve synthetic formant cases use rates 8/44.1/48/192 kHz, registers 48/60/72,
formants 650/1150/2450 Hz and a moving amplitude envelope. Their fixed middle
300 ms passes the same numerical limits. This does not override the speech
failures, establish general register/transposition support, or qualify a singer.

## Reproduction

Explicitly enable the developer executable in an existing build configuration:

```sh
cmake -S . -B build-u4-macos -DSEAM_ENABLE_SAMPLE_TARGET_VOICING_EXPERIMENT=ON
cmake --build build-u4-macos --target seam_sample_target_voicing_experiment_tests -j 6
ctest --test-dir build-u4-macos -R '^seam_sample_target_voicing_experiment_tests$' -V
```

Repeat with `build/debug` for the Debug configuration. The per-build
`target-voicing-experiment/` directory contains source.wav,
declared-root-67.wav, estimated-pitch.wav and assessment.json. These are
regenerable local development artifacts, not immutable release records.

## Review disposition and next action

### Verified isolated candidate

After restoring production code, fresh selected builds and CTest runs exited zero:

| Configuration | Targets | Overlapping case executions | Wall time |
|---|---:|---:|---:|
| Release (`build-u4-macos`) | 6/6 | 956 | 23.43 s |
| Debug (`build/debug`) | 6/6 | 1,087 | 183.47 s |

The targets are the experiment, performance compiler, performance snapshot,
phoneme timing, style blending and monolithic core suites. The monolithic suites
contain 842 and 973 cases respectively. These are selected regression runs, not
the entire registered test inventory or singer qualification. The experiment's
five engineering cases include twelve synthetic formant subcases and faithful
reporting of the two speech failures. Both configurations report acoustic FAIL,
production disabled, release ineligible and listening NOT_REVIEWED.

Logs: `build-u4-macos/u6-unvoicing-isolated-{build,ctest}.log` and the corresponding
`build/debug/` files. The following hashes bind the executed binaries and reports:

| Artifact | SHA-256 |
|---|---|
| Release executable | `1201e8d7b23018389f710be39fbf90cfa37693458d9ad96a83ca6e254d2a2129` |
| Debug executable | `004220d790ad44ce1ce9eb171882cca9b544d5a0b2d9702d92508ce981d43d43` |
| Release assessment.json | `287989098b8f99a02f5bcc138b705552c0c75abe205eaf8c8188650172d39f02` |
| Debug assessment.json | `185c4bc4f3bb7133388dc0939cce5d6319b3416e2e10b69341d58d5a288d2a97` |

The final working production sources and pre-existing product tests have no diff
against baseline `2fe9871c`. A fresh configure with the experiment option omitted
records it OFF; the generated Ninja graph and CTest registry contain no experiment
source or target. That check is configure-time isolation evidence, not another
product build. The normal production behavior remains the already reviewed baseline.

Developer 2 APPROVED commit `86a4ae43e4397a7cb8d9496e86594f6ea4689e80`
against `2fe9871c64d19ab6010cc0932cddd5191acbd41f`, strictly for the isolated
default-OFF experiment. The reviewer inspected the entire seven-file diff,
existing default-OFF configure graph, production isolation and all four documented
hashes, and independently ran both existing experiment binaries (5 cases each).
Both reproduced the exact speech failures above. No actionable blocking finding
was established. The reviewer did not rebuild, freshly configure, rerun the
broader 956/1,087-case suites, validate Windows, measure in-flight cancellation
latency or profile performance. Approval does not enable production, close
U6/U16, or establish perceptual or release qualification.

An additive calibration with the exact existing oracle, predeclared construction-
labeled controls and unchanged thresholds was accepted as the next research step.
Any replacement metric requires held-out controls, side-by-side old/new results
and separate review. Calibration cannot retrospectively change either speech FAIL.

Developer 2 recommends landing only the isolated default-off experiment and
explicit failed assessment. U8 requires truthful admission; U16 requires
fixed-corpus numerical and listening support. Existing unqualified renderers do
not justify turning a new, demonstrably unmet requested effect into normal
success with a warning.

Next: test a stronger analysis/synthesis alternative and calibrate any
aperiodicity diagnostic against independent voiced/unvoiced controls. The current
short-lag correlation may be sensitive to spectral coloration, but that is a
hypothesis, not an excuse to relabel this assessment PASS. A validated replacement
metric would require a versioned, independently reviewed contract change and
retesting—not retrospective threshold relaxation. Production enablement requires
a separate pinned review after the claimed resource/control scope passes.

## Reference provenance

Inspected existing OpenUtau checkout at
`/Users/lhs/Downloads/OpenUtau-review`, commit
`8c0dc4007e6e8c8181f3a12c10205671800eeb8b`, including
`cpp/worldline/worldline.cpp` and `cpp/worldline/model/model.cpp`.
Official [WORLD synthesis](https://raw.githubusercontent.com/mmorise/World/master/src/synthesis.cpp)
separates periodic response from envelope-filtered noise and suppresses the
periodic part for unvoiced frames. Adopted that principle only, not equivalence
or qualification. No external source, model, audio, artwork or dependency was
copied. The experimental radix-2 transform is a copy of SEAM's own existing
implementation; production spectral code remains unchanged.
