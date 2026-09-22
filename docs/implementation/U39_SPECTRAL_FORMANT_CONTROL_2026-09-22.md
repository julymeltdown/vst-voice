# U39: independent Spectral Classic sample formants

Baseline: `ae2b4fd4e94133222a242ac15fefdf23ce2e4a82`. Date: 2026-09-22.
This is a production implementation increment, not completion of U39 or Beta GO.
The full R1–R20 / U1–U48 objective, qualified singer and listening requirements
remain unchanged. The first independent review requested an export-completeness
repair; the repaired combined engineering increment is independently approved at
`550600ba7a7b9e729734558a920dae208b0041ef` (details below).

## Delivered path

Manual and accepted Formant automation now reaches the first-party Spectral
Classic renderer through normal snapshot preparation, preview/final rendering,
PCM cache, save/reopen and the transactional export service. This is independent
of the old pitch-linked `formantFollow` parameter. The native editor resolves
sample capabilities in standalone and CLAP; the CLAP fixture exercises an actual
editor nudge, current asynchronous preview and fixed-audio offline preparation.

No WORLD experiment was promoted, no target-unvoicing route was enabled, and no
training data, model, voicebank or external rights were changed.

## DSP contract

- Finish the existing carrier's pitch/time render, gain, whole-unit DC correction
  and edge fade first. Apply a formant delta afterward, before the shared compiled
  dynamics/articulation gain. That common gain is applied exactly once.
- Process the complete unit: copied onset, release and known-unvoiced spans are
  not excluded from the new stage. Control ownership uses actual absolute score
  time, never an edge-clamped neighboring note. Whole context renders precede
  owned-output cropping.
- Use Hann-windowed FFTs on an absolute-time hop lattice, with every intersecting
  edge window and zero padding. Smooth magnitude with a rectangular moving
  average of radius `ceil(300 Hz * FFT size / sample rate)`, at least one bin.
- For a shift s, the target envelope samples the original at frequency divided
  by `2^(s/12)`. Beyond Nyquist the target is zero, not a held endpoint. The
  denominator floor is `max(1e-12, maximum envelope * 1e-4)`; correction is bounded
  to [0,16]. These are fixed regularization parameters, not output normalization.
- Compute only required nonzero integer-semitone anchor filters in [-24,+24].
  Fractional controls interpolate their delta signals at **each output sample**.
  This is an approximation between adjacent filters, not exact fractional
  envelope warping. All anchors read the same unchanged input window.
- Overlap-add deltas with Hann-squared normalization. A zero evaluated control
  adds exactly zero, including short neutral/manual islands inside a shifted
  phrase. Fade only the added edge delta; do not double-fade the baseline or run
  another whole-unit mean correction that would modify untouched samples.
- Preserve length and existing target timing. Static engineering fixtures retain
  ordinary F0 while the envelope changes. This does not prove that abrupt
  time-varying filters never modulate audio, nor that isolated sample islands have
  an independently measurable instantaneous formant.

## Work and failure boundaries

Before allocating destination PCM or running carrier FFTs, an exact preflight
records the anchor mask for every window. Scan geometry and summed forward/inverse
FFT cells each have a separate 64 Mi-cell per-unit ceiling. The latter is
`sum(FFT size * (one forward + required inverse anchors))`; FFT logarithmic cost
still varies by size. No curve decimation or dropped ownership island is allowed
to satisfy these limits. Cancellation is checked during planning, windows,
anchors and output application. Output is capped at 32 Mi frames.

Nonfinite active source/input/output is rejected. An active shifted output
exceeding unit headroom after compiled gain is rejected with a source/unit-gain
diagnostic; it is not clipped, normalized or sent to a Raw fallback. Neutral/no-op
audio retains the legacy behavior, including the legacy gain policy.

These are offline bounded-work checks, not real-time, worst-machine memory, or
complete maximum-project performance qualification.

## Capability and identity

- The sample-bank **family** stays conservative. Only the Spectral Classic
  renderer advertises Formant; Raw, Classic PSOLA and Stretch explicitly reject
  it, including direct unit calls and the source-aligned phrase bypass.
- An accepted Formant selection requires capability even if its value is zero
  or manual replacement currently masks it. An unselected proposal is inert.
- Snapshot admission validates the actual selected/inherited/overridden renderer
  of every primary and secondary style unit before DSP. The requirement is stored
  in snapshot render options, already included in cache identity. Dispatcher
  admission also derives intent from compiled performance; an omitted request bit
  cannot drop the control. A required control without compiled input is refused.
- Spectral algorithm revision advances from 7 to 8. Existing render identities
  include that revision, the project controls, accepted/manual ownership, source
  hashes, renderer choices and required controls.
- Authoring capability discovery is deliberately conservative: it intersects
  enabled units in the selected styles and active overrides. A heterogeneous
  inventory may therefore withhold the UI edit even where an explicitly forced
  Spectral plan can render. Exact selected-plan UI capability refinement remains
  open; no global claim that all sample banks support formants is made.

## Verification

The new `tests/test_spectral_formant.cpp` exercises:

1. Independent upward/downward/fractional envelope movement and constant F0;
   exact neutral PCM; short onset/sustain/release control islands.
2. Normal Preview/Final PCM, dependency-sensitive cache, project reload and
   committed Float32 WAV export equal to the normal project render.
3. Accepted source-offset ramps, sign/integer crossings, exact manual-neutral
   islands, arbitrary owned-window cuts, undo/redo, inert proposals and accepted
   zero intent that unsupported renderers still reject.
4. Both ordered style arms, inherited renderer hints, explicit overrides, missing
   compiled input, unsupported dispatcher requests and no silent fallback.
5. Silent input, both +/-24 extremes, fractional controls, unvoiced source-map
   material, single application of dynamics, excessive gain, nonfinite input,
   scan/FFT-budget rejection, cancellation, 8/384 kHz and 128/8192 FFT geometry.
6. Native undoable edits, mixed/disabled/missing-style capability checks, plus
   CLAP edit -> current preview -> fixed-audio offline bounce when CLAP is built.

The 110 Hz harmonic fixture measured baseline centroid 1546.06 Hz/F0 110.009 Hz;
shifts -7/+7/+7.5 measured centroids 1281.47/2051.42/2114.90 Hz and F0
110.035/110.002/110.001 Hz. These are synthetic engineering measurements, not a
listening score or qualified singer.

Initial failures were retained and repaired: two test compile mistakes (missing
language-resolver include and incorrect controller constructor argument), and an
override fixture whose PSOLA hint lacked required pitch marks. No acceptance
assertion was weakened. A requested Release target name was incorrect and was
corrected; it was not a production compile failure.

## Fresh build/test results

All builds keep warnings-as-errors. Existing duplicate-library linker warnings
remain. The old `build-u4-macos` Ninja state also repeatedly reported recovery of
a premature end-of-file; builds completed and test exit codes were checked.

| Build / run | Result |
|---|---|
| `build-u4-macos` Release, CLAP disabled | Six selected CTest targets PASS in 27.58 s: singer-route 9, capabilities 6, performance-snapshot 50, style-blending 11, formants 16, core 842 cases. Counts overlap. |
| `build/debug`, CLAP enabled | Five selected targets PASS in 65.81 s: 9/6/50/11/17 cases. |
| `build/release`, CLAP enabled, final rerun | Four targets PASS in 3.96 s: singer-route 9, capabilities 6, CLAP microscope 2, formants 17. |
| Debug formants after isolating the CLAP test cache | 17/17 PASS in 20.79 s. |
| Tracked source closure / whitespace | `SOURCE_CLOSURE=PASS`; `git diff --cached --check` PASS. |

The final Release CLAP nudge/preview/bounce case took 0.215 s. The four 8192-frame
geometry checks took 1.289–3.735 ms; cancellation response in that run was
0.011 ms. These are observations on this Mac, not performance guarantees or a
worst-case full-anchor sweep benchmark. Debug's corresponding earlier run measured
24.1–62.3 ms and 0.173 ms respectively.

Reproduction (use the existing configurations; do not infer optional CLAP coverage
from the CLAP-disabled build):

```sh
cmake --build build/release --target seam_formant_expression_tests seam_renderer_capability_tests seam_singer_route_tests seam_clap_microscope_tests -j6
ctest --test-dir build/release -R '^seam_(formant_expression|renderer_capability|singer_route|clap_microscope)_tests$' --output-on-failure
cmake --build build/debug --target seam_formant_expression_tests seam_renderer_capability_tests seam_performance_snapshot_tests seam_style_blending_tests seam_singer_route_tests -j4
ctest --test-dir build/debug -R '^seam_(formant_expression|renderer_capability|performance_snapshot|style_blending|singer_route)_tests$' --output-on-failure
python3 -B scripts/verify_tracked_source_closure.py --root .
```

The full CTest inventory was not rerun. No full U6/U16/U39, installed-host,
language, singer or Beta acceptance is inferred.

## Independent review and Final-completeness repair

Developer 2 reviewed `2b760431f719a5105a6b047a91a5d7b7e5de2933` against
`ae2b4fd4e94133222a242ac15fefdf23ce2e4a82` and returned **REQUEST CHANGES**.
No additional confirmed DSP defect was found in that formant diff. Their
independent Release run passed four selected CTest targets in 5.33 seconds.
Their Debug run was **not clean**: performance-snapshot, style-blending and
formant targets failed during a run reporting disk exhaustion. The first
`initialRegion` assertion failure's cause was not separately established; do not
silently attribute it to storage or turn that run into a PASS.

The blocking integration finding predates the formant change: project rendering
collects recoverable failed sample phrases and failed backing media into
diagnostics, but previously rejected them only if no valid clip remained. A
successful clip could therefore let incomplete Final PCM reach single-file or
Export Set publication. Direct `commitRendered` also accepted diagnostic-bearing
PCM. Important corrections retained from review:

- Region rendering only collects **NotFound/Conflict** phrase errors. Unsupported
  formant capability/budget/headroom errors already propagate immediately.
- CLAP offline preparation already rejects diagnostic-bearing Final results;
  its guard was not missing and has not been removed or credited as a new repair.

The new regression suite reproduced six failures before the fix: missing sample
phrase, conflicting sample phrase, backing-media omission, single-file export,
Export Set and direct rendered-PCM export all accepted incomplete content. Two
control cases already passed: unsupported Formant refusal and CLAP offline
refusal. The retained local red log is
`build/release/Testing/final-completeness-red.log` (2 pass / 6 fail).

`validateCompleteProjectRender` now owns a shared completeness check. Final
project rendering applies it before final mix allocation/publication;
`commitRendered` applies it before any destination or staging-directory changes.
It retains the first failure's code, message and track/region/phrase/source
context. Every current project diagnostic represents omitted requested content,
not an informational warning; this is now explicit in the public result type.
Partial Preview behavior and intentionally muted/non-soloed exclusions remain
unchanged. Export Set inherits the shared Final check and its existing rollback
cleans already-staged outputs.

`tests/test_final_render_completeness.cpp` has seven base cases plus an optional
actual-CLAP-runtime case. It covers a successful phrase beside a missing phrase,
a successful singer beside a conflicting singer, missing paths/empty media,
changed hashes/source geometry, muted/solo exclusions, existing and new export
destinations, master-only and stems-only sets, cleanup after one successful stem,
and direct incomplete-PCM rejection before parent creation. Directory structure
and every retained file's hash are compared across failed publications. Complete
exports remain successful; no failure assertion or existing test was weakened.

The standalone new suite initially passed 8/8 after the repair in 0.76 seconds.
Fresh Release builds then passed all 12 selected CTest targets in 41.24 seconds:
authoring 13, singer-route 9, capabilities 6, performance-snapshot 50, style-blend
11, CLAP microscope 2, offline-session 3, coordinator 21, export 26, completeness
8, formants 17, and core 980 cases. These are **1,246 overlapping case executions**,
not 1,246 distinct tests. The full CTest inventory was not run.

The Debug Make configuration needed an explicit CMake regeneration before it
knew the new target; the initial unknown-target command was not a C++ failure.
Fresh Debug builds pass all seven selected targets in 72.86 seconds: singer-route
9, capabilities 6, performance-snapshot 50, style-blend 11, export 26, completeness
8 and formants 17 cases (127 overlapping case executions). The formerly failing
`initialRegion` case now passes; this does not establish the earlier failure's
cause. No storage was deleted for this repair.
No new whole U unit, singer qualification or Beta GO is counted.
Tracked source closure reports `SOURCE_CLOSURE=PASS`; staged whitespace checks
also pass. The scope remains local macOS engineering verification.

Developer 2 **APPROVED** the repair and combined engineering increment at
`550600ba7a7b9e729734558a920dae208b0041ef`, resolving the prior integration
REQUEST CHANGES. They inspected the complete seven-file repair, all 262 new test
lines, surrounding region/project/export/CLAP paths and updated evidence. No
remaining actionable scoped finding was established. Independently executed
existing candidate artifacts (not an independent rebuild): Release export,
completeness and formant targets **3/3 PASS in 5.26 s**, 26/8/17 cases; Debug
snapshot, style-blend, export, completeness and formant targets **5/5 PASS in
67.25 s**, 50/11/26/8/17 cases. Both include the actual CLAP completeness case.
The reviewer inspected the producer's six-failure red log but did not rerun the
old source; the earlier initialRegion failure's cause remains unestablished.
Their source-closure and whitespace checks pass. The full CTest inventory,
installed DAWs, Windows and listening/resource qualification were not rerun or
approved. Mixed-inventory exact-plan UI capability refinement remains open.

Reproduction:

```sh
cmake -S . -B build/debug
cmake --build build/release --target seam_tests seam_export_tests seam_final_render_completeness_tests seam_formant_expression_tests seam_performance_snapshot_tests seam_style_blending_tests seam_authoring_render_coordinator_tests seam_authoring_performance_tests seam_renderer_capability_tests seam_singer_route_tests seam_clap_microscope_tests seam_offline_render_session_tests -j4
ctest --test-dir build/release -R '^(seam_tests|seam_(export|final_render_completeness|formant_expression|performance_snapshot|style_blending|authoring_render_coordinator|authoring_performance|renderer_capability|singer_route|clap_microscope|offline_render_session)_tests)$' --output-on-failure
cmake --build build/debug --target seam_final_render_completeness_tests seam_export_tests seam_formant_expression_tests seam_performance_snapshot_tests seam_style_blending_tests seam_renderer_capability_tests seam_singer_route_tests -j4
ctest --test-dir build/debug -R '^seam_(export|final_render_completeness|formant_expression|performance_snapshot|style_blending|renderer_capability|singer_route)_tests$' --output-on-failure
```
