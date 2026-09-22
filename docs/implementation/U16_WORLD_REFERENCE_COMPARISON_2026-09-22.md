# U16 offline WORLD reference comparison — revision 2

Status: both arms executed; implementation review pending. The exact extracted
guard resolves the six observed synthetic AP rejections. All three speech
hypotheses still FAIL the unchanged acoustic conjunction. Additive source-energy
diagnostics identify a fixture limitation, not renderer acceptance.
Baseline: `1f56f23234e68777e78f808d6c0a061fdd6d7978`.

## Purpose and reference identity

Compare configured pipelines on identical frozen source samples: the existing
SEAM envelope/noise experiment, upstream WORLD reconstruction, and upstream WORLD
forced-unvoiced synthesis. This is not an isolated comparison of envelope
algorithms, a replacement acceptance metric, or a perceptual gold standard.

WORLD revision `f8dd5fb289db6a7f7f704497752bf32b258f9151` is the upstream
pin in OpenUtau revision `8c0dc4007e6e8c8181f3a12c10205671800eeb8b`,
`cpp/WORKSPACE.bazel`. OpenUtau additionally applies `world.patch` and its own
analysis/renderer logic. The first arm deliberately uses **unpatched upstream**;
the second applies only the exactly pinned D4C denominator guard described below.
Neither is an execution or quality assessment of OpenUtau itself.
The [upstream synthesis API](https://github.com/mmorise/World/blob/f8dd5fb289db6a7f7f704497752bf32b258f9151/src/world/synthesis.h)
accepts independent F0, spectral-envelope and aperiodicity arrays.

Only 25 explicit upstream C++/header/license files are downloaded into a fresh
external directory. The committed lock binds each Git blob and length; the
verifier derives a SHA-256 source manifest. No media, models, test recordings,
OpenUtau patches or source implementations are copied into the SEAM repository.
The separate external guard directory retains the full patch, extracted hunk and
OpenUtau MIT license; only the extracted hunk is applied there.
The upstream copyright and BSD-style license remain in that external directory.
No linked reference binary is installed or packaged as part of SEAM.

## Prospective fixed matrix and bounds

- Six synthetic controls: rates 44,100/48,000 Hz x MIDI 48/60/72; the exact
  existing 500-ms harmonic/formant fixture. Known constant F0 is used for both
  WORLD analysis and the original reconstruction's synthesis. The scorer uses
  that same ground-truth frequency.
- Three speech hypotheses use the unchanged 44.1-kHz source WAV SHA-256
  `caf8ceb04b864c7501371a2adae46697495e617b9ade178560ba2ea1aa6b8cb9`:
  constant root MIDI 67 (391.99543598174927 Hz); the existing SEAM estimator's
  constant 991.01406569073924 Hz; and a new DIO->StoneMask contour. Both constant
  hypotheses are retained, neither selected as the correct physical F0. Their
  outputs are constant-F0 resynthesis, not faithful original-F0 reconstruction.
- In all three speech cases the scoring lag remains the independently obtained
  original 991.01406569073924 Hz, and measurement stays at 100–400 ms. The DIO
  contour does not redefine the oracle. Its side-by-side SEAM comparator retains
  root-67 carrier conditioning; SEAM does not consume that DIO contour.
- DIO: floor 71 Hz, ceiling 1200 Hz, 2 channels/octave, 5-ms frames, speed 1,
  allowed range 0.1; refine with StoneMask and require finite values in [0,1500] Hz.
  CheapTrick: q1=-0.15, floor 71 Hz,
  FFT size from its pinned API. D4C threshold=0.85. No post-hoc option sweep.
- Bound input to these two rates and 400–1000 ms before integer conversions and
  allocations. Require 2–202 analysis frames, FFT 256–4096, and at most 524,288
  elements per spectral/AP matrix. This does not claim SEAM's full rate range.
- Serial upstream calls only: the pinned library has global reseeded random
  state. Repeat analysis and synthesis and require exact same-build arrays/PCM.
  No independent WORLD noise seeds or thread-safety claims are made.

## Analysis, construction and measurement separation

Analyze each policy once for the compared outputs. Retain and hash its actual
time axis, F0, spectral envelope and estimated D4C aperiodicity as bounded
little-endian Float64 arrays, including row/bin geometry. A second analysis is
only a reproducibility check and must match exactly.

Reconstruction uses those F0/AP arrays. Forced-unvoiced synthesis copies the
arrays, sets synthesis F0=0 and AP=1, and reuses the identical original envelope.
Both override arrays are retained. The envelope is never reanalyzed with target
F0=0. Forced-unvoiced is a construction, not an inferred D4C label.

All three outputs retain the exact source length. Score and write raw Float32
with no gain/peak matching, clipping, fade, alignment search or post-hoc crop.
Retain peak and finite/overflow checks, RMS ratio, unchanged source-lag
correlation, and unchanged five-band spectral-share errors. The unvoicing
conjunction remains RMS (0.7,1.3), NCC<0.65 and below source, band error<0.20.
That NCC cutoff is explicitly not applicable to the voiced reconstruction;
its losses are still reported before drawing conversion conclusions.

Every case remains in the comparison report, including execution errors and
numerical failures. Execution failure is not an acoustic FAIL. Numeric PASS
cannot grant perceptual qualification or production eligibility. A shared FAIL
may reflect the known oracle limitation or reconstruction losses, not a proven
specific defect in either method. No historical assessment is overwritten.

## Build and evidence boundary

`SEAM_ENABLE_WORLD_REFERENCE_EXPERIMENT` defaults OFF. Enabling it requires
`SEAM_WORLD_REFERENCE_SOURCE_DIR`; configure does no network access. All pinned
source/header/license bytes are verified at configure, before each reference
build and again at execution. The external static library and executable are
EXCLUDE_FROM_ALL; no production library depends on them. CTest timeout: 180 s.

```sh
# NEW_DIRECTORY must not already exist; its parent must exist.
python3 -B -m tools.singing_quality.prepare_world_reference fetch NEW_DIRECTORY
cmake -S . -B build-u4-macos \
  -DSEAM_ENABLE_WORLD_REFERENCE_EXPERIMENT=ON \
  -DSEAM_WORLD_REFERENCE_SOURCE_DIR=NEW_DIRECTORY
cmake --build build-u4-macos --target seam_world_reference_experiment_tests -j 6
ctest --test-dir build-u4-macos -R '^seam_world_reference_experiment_tests$' -V
```

Use an explicitly trusted CA bundle if the Python installation lacks its CA
file; never disable TLS verification. On this Mac, the first fetch failed on
the missing Python framework CA file. A fresh-directory fetch with
`SSL_CERT_FILE=/etc/ssl/cert.pem` succeeded; the partial directory was not reused.

Historical revision-1 results use `<build>/world-reference-experiment-v1/<executable-sha256>/`, with
source/parameter/output hashes, external source-manifest identity, compiler and
configuration. These are local diagnostics, not release evidence. Build and
CTest logs retain failures. All prior experiment/calibration packets remain intact.

Developer 2 concurred with this direction subject to these separation, bounds,
serial-call and provenance requirements. Pinned implementation review is still
required; this does not close target-unvoicing support, U6/U16 or Beta GO.

## First execution and prospective extracted-guard follow-up (revision 2)

The unmodified upstream run completed all three speech hypotheses but rejected
all six synthetic cases for nonfinite D4C AP. Release NaN-cell counts were
81,132 / 50,704 / 4,016 at 44.1 kHz and 81,246 / 51,659 / 7,823 at 48 kHz,
ordered by MIDI 48/60/72. Finite AP values were in [0,1]. The original CTest
target failed; that result is retained, not retrospectively labeled successful.
Original Release packet: `world-reference-experiment-v1/43f8c47184e8a988550f19f74b006d7f9b4a3f16aba6a8f8c504f55fdbcaa656/`.
The diagnostic and raw-analysis-retention reruns have separate executable-bound
packets; the latter's Release identity is
`714be4f8dd96e63bf0e58a054198ff16f907bcb564dc8e1af1b62c34b2bcc794`.

Speech reconstruction RMS ratios were 0.155471 / 0.078818 / 0.191724 for
root-67 / estimator-constant / DIO->StoneMask. Forced-unvoiced ratios were
0.155360 / 0.116242 / 0.146782 and all failed the unchanged numerical conjunction.
These large reconstruction losses are an unresolved finding, not explained or
repaired merely by finding invalid AP in different synthetic inputs.

Developer 2 recommended one narrow causal follow-up before changing methods:
compare upstream with **only the exact D4C safe-minimum hunk** from OpenUtau's
pinned patch, not the full patch (which also changes synthesis and its ABI).
This revision is fixed before the guarded arm is executed. All nine inputs,
F0 policies/options, source samples, compiler settings, oracle, measurement
windows, thresholds and raw gain policy stay unchanged. No dither or output
sanitization is introduced. F0/time/envelope identities must match across arms.

Exact provenance:

- Full OpenUtau patch SHA-256: `daba7824bfe790455a2d37770254fbcea535fae67b5d32b1d739298f5ec7fd89` (retained, not fully applied).
- Extracted D4C hunk: `e3c30ca24cf523f339108db3b62b181e7bff48e46f14e4abd8d49aa3233a581a`.
- Resulting `src/d4c.cpp`: `28fdbe66ef6d8c5aaca63b35aa04471accc6aaf3f9830935ae8c068ed6addfa6`.
- Retained OpenUtau MIT notice: `cdef6370f9e705c804fd1acbe1b1c65027c13186c466b51ccb0323d5607cc9c4`.

Explicit preparation creates a different directory and checks the original
source, full patch, extracted hunk, resulting bytes and license. Verification
defaults to upstream and never silently accepts patched bytes. The separate
guard target has a different static library, source check, generated identity,
executable and report directory; no combined in-process library symbols.

```sh
python3 -B -m tools.singing_quality.prepare_world_reference guard NEW_GUARD_DIRECTORY \
  --upstream UPSTREAM_DIRECTORY \
  --openutau-patch /absolute/OpenUtau-review/cpp/third_party/world.patch \
  --openutau-license /absolute/OpenUtau-review/LICENSE.txt
cmake -S . -B build-u4-macos \
  -DSEAM_ENABLE_WORLD_REFERENCE_EXPERIMENT=ON \
  -DSEAM_WORLD_REFERENCE_SOURCE_DIR=UPSTREAM_DIRECTORY \
  -DSEAM_WORLD_REFERENCE_GUARD_SOURCE_DIR=NEW_GUARD_DIRECTORY
cmake --build build-u4-macos --target seam_world_reference_experiment_tests seam_world_d4c_guard_experiment_tests -j 6
```

Revision-2 diagnostics retain bounded raw IEEE754 AP arrays before admission,
including NaN/positive-infinity/negative-infinity/out-of-range counts and row-major
indices. Repeated invalid arrays are compared by serialized bits. No rejected
array reaches synthesis. The independent SEAM comparator runs even if WORLD
analysis is rejected; dependent WORLD outputs say NOT_RUN, not acoustic FAIL.

The upstream engineering regression now requires exactly the six known synthetic
AP rejections and no unrelated execution error. Its report still says execution
ERROR and preserves each rejection. A passing CTest therefore verifies faithful
reporting, not successful upstream processing. The guarded arm requires no such
rejections; finite AP would resolve only this observed defect on this grid.
Output acoustic results are computed independently and cannot be promoted by
that engineering check. Artifacts use `world-reference-experiment-v2/<variant>/<executable-sha256>/`.

## Executed outcome and evidence

Fresh final builds pass three selected CTest targets in each configuration:
two WORLD targets with two engineering cases each, plus the six-case unchanged
SEAM experiment/calibration target. Release: 5.41 s; Debug: 31.99 s. The Python
singing-quality suite discovers 61 tests: 57 pass, four native-driver workflow
tests skip because their explicit driver/analyzer environment was not supplied.
Those skips are not native workflow evidence. No broad production-suite rerun is
claimed for this developer-only change.

| Variant / input | Execution | Forced-unvoiced numeric result |
|---|---|---|
| Upstream / six synthetic controls | Six AP-NaN rejections, retained as ERROR | NOT_RUN |
| Exact D4C guard / same six controls | PASS; finite admitted AP | 6/6 PASS |
| Either arm / three speech hypotheses | PASS | 0/3 PASS; all remain FAIL |

The guarded synthetic forced-unvoiced outputs have RMS ratios 0.977345–1.143618,
source-lag correlation 0.071066–0.149265, and maximum band-share error
0.003073–0.143165. Raw voiced reconstructions are not unvoicing candidates:
their high-register RMS ratios reach 1.320013 and 1.336932. Some raw outputs have
peaks above 1.0; no clipping or normalization hides them.

The three speech hypotheses share ONE short recording, not three independent
speakers or recordings. Within each configuration their parameters and outputs
are byte-identical between the upstream and guarded arms. The D4C guard did not
repair or explain their reconstruction/spectral losses.

Producer verification recomputed 372 recorded file hashes, decoded 216 parameter
arrays, and checked 36 AP admissions against independently recomputed invalid-cell
masks/indices. Cross-arm source WAV, time-axis, F0 and envelope identities match
within each configuration. An initial overstrict cross-configuration equality
assertion failed: Release/Debug are NOT bit-identical. Maximum absolute differences
are AP 2.374e-10, F0 2.842e-14 Hz, envelope 1.055e-14 and reported metrics 1.184e-13.
Of 42 corresponding output WAV pairs, 31 hashes match and 11 differ. Execution/
acoustic verdicts and invalid-cell masks match. Same-build repeatability remains
exact as required by the harness; no cross-build byte-reproducibility claim.

| Build / arm | Executable SHA-256 | comparison.json SHA-256 |
|---|---|---|
| Release / upstream | `6f3cd735a4a7f0bed052e56cb2fe1d5e04611e90714e5bb0b0fce27762a70343` | `69cc3cac7ce7783bf8d00b50bcc143d10b2a951601fe3c89804247730f07bc1a` |
| Release / guard | `46536f29de644b97b8c145be08edb15918667b728eaef370387df233dc837c1b` | `4d0c585445c148a132e0c03ae8f9309a31b5f08d7503acf8791314e8dba1825c` |
| Debug / upstream | `8587b47b027776e4677d7fa37cb9eee815f0b9b40391f70341d2330190b52f03` | `fd8c528e88721a5bacf263bba0b1a6af4033aa7d7d195ea27f4277e5573925fb` |
| Debug / guard | `02e8cc00eff49772dd6192cbe5f8ce8fd1214efed250055c1c8e946c3bbfa874` | `0c210c8a050532d94c8d420b5680709b4d02da0bfe71b3ad5696c11818416825` |

Use `build-u4-macos` for Release and `build/debug` for Debug, followed by
`world-reference-experiment-v2/{upstream|openutau-d4c-guard}/{executable-sha}/comparison.json`.
Logs are `u16-world-final-{build,ctest}.log` in each build. The producer's hash/array
summary is `build-u4-macos/u16-world-final-artifact-check.json`.
Source lock SHA-256: `d90d25068c59f8a0c5c5156c405730dd39cc27121d01c93c2c60b7488fcbfd49`.
Upstream source manifest: `7995697c60f58734f2e2494e4c7ae3ab0967f0fb4a96f5cde118a83ea6b0a8cb`.
Guard source manifest: `92298f7db0f90bd759745a2c425207f090d2a8bb1f89a025e6d07efaea59142b`.
External directories: `/tmp/seam-world-reference.Cmj0FH/{source,d4c-guard}`.
These are local reproducibility inputs, not repository release assets.

A fresh default-OFF configure at `/tmp/seam-world-default-off.nTR29W` has neither
reference target nor external WORLD source in its Ninja graph. Verification
rejects each external source directory when requested as the other variant.
The original assessment and 168-control calibration JSON hashes documented in the
U6 records remain unchanged. No `libs/` or `apps/` source changed, and neither
reference library is a production dependency.

## Additive source-frequency diagnosis, not a revised verdict

`tools/singing_quality/source_frequency_diagnostic.py` decodes a hash-bound source
without transformations and reports rectangular and symmetric-Hann one-sided
DFT power shares. Interior bins are doubled; DC and even-length Nyquist are not.
Each spectrum is checked against its own time-domain energy via Parseval.
Silence has undefined (`null`) shares, not passing zeroes. No filter, pitch
estimate, source-suitability threshold, automatic selection or renderer score
is introduced. Five analytical tests cover known energy shares, odd/even/DC/
Nyquist, silence, invalid geometry/PCM and source-hash/window binding.

On the frozen 100–400 ms speech window:

| Quantity | Measured value |
|---|---:|
| Rectangular power below 40 Hz | 97.612973% |
| Rectangular power below 71 Hz | 98.304028% |
| Hann-windowed power below 71 Hz | 98.488536% |
| DC contribution to rectangular energy | 1.072111% |
| Window mean / RMS | 0.028291 / 0.273226 |
| Rectangular time energy / DFT energy | 987.6504189763218 / 987.6504189763217 |

A separate standard-library PCM16 decoder and direct trigonometric DFT of the
22 bins below 71 Hz reproduced the 0.9830402806947169 share without NumPy's FFT.
Developer 2 independently decoded with SciPy and reproduced both spectra and
energy accounting. Subtracting the WINDOW mean in their separately identified
in-memory diagnostic left 98.285648% below 71 Hz: non-DC low-frequency dominance,
not merely a nonzero mean. Original audio and assessments were not changed.

The original retained `talking.wav` (SHA-256
`26c3ceacfb31a7300824a3841904f077856bdba41dafba0310c603cabb86d07f`)
shows 98.299734% below 71 Hz in 21.94–22.24 s, corresponding approximately to the
analysis portion of the provenance's "near 21.84 seconds" crop. This is not
sample-exact derivation alignment; it corroborates that the pattern predates
the derived recording.

CheapTrick's pinned implementation removes a local window-weighted mean. That
makes LF attenuation a plausible contributor, NOT a proven exclusive cause or
an ideal 71-Hz high-pass model. Gain/API errors are not ruled out. Dominant bins
are not true voice F0, and neither the historical 991-Hz oracle nor MIDI-67
metadata becomes physical F0 ground truth. No physical origin or perceptual
quality is inferred.

Reproduce without overwriting evidence:

```sh
python3 -B -m tools.singing_quality.source_frequency_diagnostic \
  assets/demo-human-voicebank-public-domain/production-bank/audio/human-vowel-demo.wav \
  --sha256 caf8ceb04b864c7501371a2adae46697495e617b9ade178560ba2ea1aa6b8cb9 \
  --sample-rate 44100 --output NEW_SOURCE_DIAGNOSTIC.json
python3 -B -m tools.singing_quality.source_frequency_diagnostic \
  assets/demo-human-voicebank-public-domain/source/talking.wav \
  --sha256 26c3ceacfb31a7300824a3841904f077856bdba41dafba0310c603cabb86d07f \
  --sample-rate 44100 --start-frame 963144 --frames 24255 \
  --output NEW_ORIGIN_DIAGNOSTIC.json
```

Retained reports: `build-u4-macos/u16-world-source-frequency.json`
(SHA-256 `8fa2c343655a9cd2d0a16ce5084dd7434cbe8d9acb4e28f4acdfd272830a767d`)
and `build-u4-macos/u16-world-origin-frequency.json`
(`8e191c4922f9b97867698b7283822d05009b1766f9d9e034f834121d8ad5a7fd`).

## Next acoustic action and non-claims

Developer 2 recommends a predeclared panel of clean synthetic vowels and fixed
LF-contaminated counterparts before another algorithm choice. Keep the underlying
voiced amplitude, rates, F0, phase/seeds, analysis windows, raw output policy and
thresholds fixed; retain zero-contamination controls and every result. This tests
whether adding LF alone reproduces the pattern, not a historical FAIL reversal.

A supplemental independent speech/singing corpus needs frozen source-only
admission/stratification rules: rights, decoding, clipping/noise/level, DC/LF,
phonetic coverage and independent annotations. Preserve LF-heavy stress cases;
never select clips by candidate-renderer success. Actual singing and held-out
validation remain required before U16 qualification. No new recording/download
or corpus admission is claimed by this increment.

No U6/U16 closure, qualified singer, extra accepted unit or Beta GO is claimed.
Independent diagnostic concurrence is NOT implementation approval; the exact
committed candidate still needs review.
