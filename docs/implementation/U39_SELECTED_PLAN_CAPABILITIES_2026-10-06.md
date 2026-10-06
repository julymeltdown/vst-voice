# U39 selected-plan authoring capabilities

Baseline: `b46d1e450d2869d319a07fbd2013d8c84e4fbd0a`. Date: 2026-10-06.

An unused Raw unit in a sample bank could disable the Formant editor even when
the prepared region explicitly selected Spectral Classic. The standalone
session also dropped the application's capability callback when configuring
its editor controller. Both failures were reproduced with current asynchronous
sample previews before changing the production code.

## Behavior

The renderer publishes the effective renderer set from its prepared sample
resource. It resolves inherited hints, renderer policy and explicit overrides,
then combines every selected primary/secondary style arm and polyphonic voice.
The set is derived before PCM-cache lookup, so cache hits retain the same
capability information. The publication identifies the active track and region.

Standalone and CLAP use one authoring-runtime check. A selected plan grants
capabilities only from a Ready publication for the current request, matching
project identity/revision, selected region and resolved bank ID/version/content
hash, with no render diagnostics. View selection can change without incrementing
the project revision, so region identity is checked separately. Pending, stale,
incomplete or mismatched publications use the existing conservative inventory
check. Selection and audio loading are not rerun on the UI thread.

Formant still requires every selected renderer to support it. A Raw override,
mixed voice or unsupported secondary style arm withholds the edit; snapshot and
dispatcher admission continue to reject unsupported selected operations.
This changes authoring discovery and callback wiring, not DSP, serialized project
data, PCM identity or resource approval.

## Verification

The isolated retained Apple arm64 Release build uses native and CLAP support.
Only the targets actually tested and their dependency closures are rebuilt,
using one job. Builds have a 600-second cap and test stages a 240-second cap,
with a measured 12-GiB free-disk floor and a 512-MiB owned-build ceiling.

The baseline's two filtered cases failed the Formant nudge after asserting that
the current preview selected the Spectral unit. The same two cases passed after
the repair. Additional regression assertions cover overrides, both style arms,
polyphonic mixed renderers, cache-hit equality and region selection on both
surfaces. The standalone selection test retains a readable publication for the
old region at the same revision and proves it cannot grant Formant to the newly
selected Raw region; returning to a current Spectral plan restores the edit.

Focused candidate results are recorded with raw logs outside the checkout in
`SEAM_U39_SELECTED_PLAN_20261006`: 10/10 CTests passed in 16.11 seconds. The native
suites passed 22 formant, 9 singer-route, 6 renderer-capability, 66 performance-
snapshot, 11 style-blending and 5 standalone cases. The three original import
regressions and 11 recipe/input cases ran without macOS skips; score export
interop and CLI help also passed. The final incremental build took 19.30 seconds.

These results do not inherit the historical full CTest result. No installed host,
signing, Windows, human listening or complete U39/Beta acceptance is inferred.
