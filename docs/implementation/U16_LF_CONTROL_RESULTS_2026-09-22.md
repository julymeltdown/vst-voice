# U16 fixed low-frequency control panel: results and decision

Date: 2026-09-22. Baseline: `e9877a9684f49425bfde2d55ab34c2375f9d8e9a`.
Developer-only experiment; independent implementation review pending at this checkpoint.
No production promotion, qualified singer, extra accepted unit or Beta GO.

## Decision

The frozen panel reproduces strong sensitivity to added low-frequency (LF) energy.
This supports LF contamination as a contributor to the earlier technical speech
fixture's result, but does not establish its exclusive cause, prove that WORLD
preserves the wanted voiced component, or qualify either renderer. Keep production
target-unvoicing unsupported. Do not normalize, filter, change thresholds/F0, or
replace the frozen failing clip with a passing one.

This is not simply an unchanged reconstruction divided by a larger input RMS.
LF addition also changes the analyzed envelope and reconstruction. There is no
identified linear LF transfer function or recovered LF component here. The panel
contains 42 fixed constructions at two phase-zero LF frequencies, not a recording
population or perceptual benchmark. Next acoustic work must use source-only
stratification of available, provenance-bound speech/singing material, preserve
LF-heavy stress cases and an untouched validation partition. No new download,
corpus approval or training occurred. The existing source-lag oracle's excitation
classification limitation remains open; this panel cannot erase it.

## Frozen implementation

The unchanged prospective protocol `U16_LF_CONTROL_PANEL_2026-09-22.md` has SHA-256
`348f0cc113908ae4690cb17cc2dba1ea90eafc05bb73542711cb13ceeee7e649`.
Its initial "NOT executed" status is historical and intentionally not edited.

Six voiced bases (two rates, three pitches), each at fixed Float32 gain 0.2,
have one clean and six LF-added cases: 10/30 Hz at amplitudes 0.05/0.20/0.60.
Components are retained and their exact Float32 sum verified. These gain-0.2 clean
cases are not byte-identical to the old nine-case experiment's controls.

The shared C++ runner retains the old six-rejection expectation only in the old
nine-case regression. The new panel counts actual rejections with no expected AP
count or predetermined acoustic result. All 42 cases are required. A complete
retained AP rejection yields packet COMPLETE with WORLD execution ERROR; unexpected
execution/artifact errors instead prevent the complete-packet claim.

`lf_control_diagnostics.py` runs the unchanged source-frequency tool on every
combined source, checks all retained hashes, component geometry, exact voiced-pair
identity/recomposition, raw AP masks, and F0/AP overrides. Independent NumPy sine
verification has absolute tolerance 1e-7 for C++/NumPy libm differences; retained
Float32 pair and recomposition checks are exact. Bounded regular-file checks do
not claim concurrent filesystem immutability.

Clean-pair envelope comparisons use all 101 analysis frames and 1,025 bins, with
unweighted array sums. Paired output RMS is explicitly derived from retained C++
output/source RMS times independently measured combined-source RMS in 100–400 ms,
not independently decoded output PCM. Output WAV hashes are checked without
clipping overshoots or passing them through a normalized-source decoder.

## All-case outcomes

The following counts reproduce in both Release and Debug:

| Arm / output | Clean inputs | LF-added inputs |
|---|---|---|
| Unmodified WORLD analysis | 6/6 rejected for invalid AP | 29/36 rejected; 7 admitted |
| Unmodified WORLD forced UV | 6 NOT_RUN | 29 NOT_RUN, 7 numeric FAIL |
| Exact OpenUtau D4C guard analysis | 6/6 admitted | 36/36 admitted |
| Guarded WORLD forced UV | 6/6 numeric PASS | 36/36 numeric FAIL |
| Independent SEAM experimental comparator | 6/6 numeric PASS | 36/36 numeric PASS |

Admitted reconstructions are NOT_APPLICABLE_RECONSTRUCTION, never unvoicing PASS.
Guarded clean reconstruction RMS/input RMS is 1.084–1.337: clean forced-UV passes
do not imply perfect reconstruction. No invalid AP reaches synthesis; all raw
rejected AP and indices remain available. SEAM runs independently on all inputs.

SEAM's numeric passes are **not** a quality win. Retaining LF can favor this
total-source energy criterion; suppressing it can fail the same criterion.
Additionally, 11/42 SEAM Float32 outputs exceed unity, with maximum peak 1.274030.
They are retained without clipping/gain compensation. The original conjunction
has no peak-headroom criterion, so numeric PASS is not export-safe or perceptual
approval. WORLD output peaks remain below unity on this panel.

## Paired sensitivity, guarded WORLD (Release)

Ranges include every input at each amplitude, across rates/pitches/LF frequencies.
Power shares are measured, not inferred from amplitude ratios; cross terms remain.

| LF amplitude | Cases | Source power below 71 Hz, rectangular | Reconstruction RMS / combined-source RMS | Reconstruction RMS / clean reconstruction RMS |
|---|---:|---:|---:|---:|
| 0 | 6 | 0.000357–0.007994% | 1.084–1.337 | 1.000 |
| 0.05 | 12 | 48.634–78.782% | 0.524–0.961 | 0.946–1.352 |
| 0.20 | 12 | 93.808–98.345% | 0.199–0.535 | 0.974–3.834 |
| 0.60 | 12 | 99.272–99.813% | 0.088–0.524 | 1.045–11.194 |

Relative envelope L1 differences against the clean pair span 0.00619–4.75438 at
amplitude 0.05, 0.07307–76.49117 at 0.20, and 0.59237–688.40508 at 0.60. These are
descriptive array differences, not physical LF-component estimates or new quality
thresholds. LF removal with an otherwise preserved voice is not demonstrated.

Upstream WORLD has no admitted clean reconstruction, so all its reconstructed
audio pairs are UNAVAILABLE_PAIR, never replaced by the guard's clean output.
Its finite retained envelopes can still be compared with its own clean analyses.

## Verification

Strict selected C++ builds pass in both configurations. Two WORLD targets each
contain five engineering cases: ten pass per configuration (Release 16.08 s,
Debug 88.16 s). Retained expected acoustic FAIL/analysis ERROR is not an engineering
test failure. Python discovery: 69 tests, 65 pass/four native-driver cases explicitly
skipped because runtime inputs were not supplied. Ruff/whitespace checks pass.
Broad product, installed, host and listening suites were not rerun for this isolation.

Four diagnostics cover 42 cases each: 168 case-arm-build records, not 168 distinct
constructions. Within each configuration, all 42 source/component/time/F0/envelope
and SEAM-output identities match across arms. Across Release/Debug, 156/182 output
WAV comparisons match exactly and 26 differ; verdicts match, maximum absolute
RMS/NCC/band-error/peak difference 6.435e-12. Cross-build bit identity is not claimed.

The four new nine-case reports equal the prior accepted reports after removing
only executable SHA-256. Historical speech assessment hashes remain
`287989098b8f99a02f5bcc138b705552c0c75abe205eaf8c8188650172d39f02`
(Release) and `185c4bc4f3bb7133388dc0939cce5d6319b3416e2e10b69341d58d5a288d2a97`
(Debug). Original speech FAILs and calibration findings remain unchanged.

## Retained identities

Packet: `<build>/world-reference-experiment-v2/<arm>/<executable>/lf-controls-v1/comparison.json`.
Build directories: `build-u4-macos` (Release), `build/debug` (Debug).
Guard arm directory: `openutau-d4c-guard`; upstream: `upstream`.
Source locks remain those in `U16_WORLD_REFERENCE_COMPARISON_2026-09-22.md`.

| Build / arm | Executable SHA-256 | Comparison SHA-256 |
|---|---|---|
| Release / upstream | `296f94bc8aa9fd23d4f8d41f49f68ddbdd91f7c9769b816d3bb8c5dfbe2f0104` | `d39f17686c5832c1c70cf1db2667afbba440510fadb1d18b0629068c2d31b140` |
| Release / guard | `601d74f9c5179a6d7a649a896370d925544f31f0b249d1f77848572e4af3bb20` | `e20b37bc8d94c4d2bc32b1f9a74e2250211075592eecd5e13a06c5edda24b920` |
| Debug / upstream | `ee9d5dece35b4a0650c20a52d4f5a4f92e4812a22aa80d5f342c97013d4eb533` | `6024452199b1f1a2729ba97064697b1b64e0ab9506037be90388e71ccba3ce65` |
| Debug / guard | `4aeb7b6dda973a9b8959ea58138d3324848871e6b186887d971fde9803e2c144` | `8a84be1722f578b2c5b427be511fe61c14c9dc28c4c6cb52da6e655d100335b0` |

| Diagnostics file, relative to corresponding build | SHA-256 |
|---|---|
| `u16-lf-release-upstream-diagnostics-v2.json` | `4186dd8b96a3eb31c5cb2dc8120135bb10d8d0f1ab532206637fcd2181c11370` |
| `u16-lf-release-guard-diagnostics-v2.json` | `57cd1f1bb4965e74a0003fd3dc3ec243eed634c7e19387becd5a330954628c06` |
| `u16-lf-debug-upstream-diagnostics-v2.json` | `efc6d2f66e90548492539fe6c6d15b1584f52a2192379d8599aea43ae0ad9adb` |
| `u16-lf-debug-guard-diagnostics-v2.json` | `76f3cd46c95d74fc20d2d156742537945f29a26ff7e7c681ca35171ff8481a52` |

The earlier Release guard diagnostics-v1 is retained but superseded by v2, which
names the envelope sum precisely and checks source uniqueness. All v2 reports
bind diagnostic tool SHA `f478131e09b2574504f5989d6f4c074c14a34bd18b74eaae45317831cdcab98d`.

## Reproduction

Using the existing explicit opt-in builds and verified source arms:

```sh
cmake --build build-u4-macos --target seam_world_reference_experiment_tests seam_world_d4c_guard_experiment_tests -j4
ctest --test-dir build-u4-macos -R '^seam_world_(reference|d4c_guard)_experiment_tests$' -V
cmake --build build/debug --target seam_world_reference_experiment_tests seam_world_d4c_guard_experiment_tests -j4
ctest --test-dir build/debug -R '^seam_world_(reference|d4c_guard)_experiment_tests$' -V
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/singing_quality -v
python3 -B -m tools.singing_quality.lf_control_diagnostics /absolute/comparison.json --output /absolute/new-diagnostics.json
```

Run diagnostics separately for every arm/build using the packet path printed by
WORLD-LF-CONTROLS. Existing output files refuse overwrite. This completes the
fixed experiment, not U6/U16: locally accepted roadmap progress stays 6/48.
