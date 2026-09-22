# U6 generated timbre automation

Date: 2026-09-22. Scope: shared performance compilation, normal render snapshot
admission, and neural breathiness ownership. This is an implementation increment
within U6/U39, not completion of U6 or full-product Beta acceptance.

## Reproduced gap

The shared compiler rejected every accepted timbral lane, including Formant,
despite already evaluating drawn timbral curves for the procedural renderer.
The new six-channel compiler regression failed at accepted compilation before
the production change (20 passed, 1 failed). The existing Formant assignment
was unreachable through this admission check.

## Implemented behavior

- Compiler revision 17 consumes accepted Formant (semitones), Breathiness,
  Tension, Airiness, Gender (bipolar), and Growl in their own units.
- Existing source-tick offsets, tick interpolation, absolute-frame half-open
  scope membership, and manual Replace precedence apply to all six channels.
  Unselected proposals remain inert; compilation still owns bounded immutable
  symbolic data rather than allocating per-sample song arrays.
- `ScorePerformanceSample::breathinessIsExplicit` distinguishes an intentional
  zero from an unowned frame. A drawn curve owns the full active-note region;
  accepted lanes and manual Replace records own only their selected spans.
- Neural requests use model-bound per-phone breathiness defaults only on
  unowned frames. A partial accepted lane neither loses to the prior nor
  disables that prior outside its scope. Explicit silence remains silent.
- Normal sample snapshots refuse all six accepted timbral controls by name.
  Neural snapshots admit accepted breathiness only when the actual admitted
  acoustic graph declares that conditioning input; they refuse the other five.
  Accepted neutral or manually overridden selections remain explicit capability
  requests. Drawn neutral curves without accepted selections retain their prior
  behavior. No silent Raw fallback is introduced.
- The compiler revision enters existing render/cache identity and recorded
  renderer provenance. No project schema migration is needed: these lanes and
  their ownership already persist in schema 18.

## Evidence

Fresh strict Release and Debug builds, four focused CTest targets in each:

| Suite | Cases | Evidence supplied |
|---|---:|---|
| `seam_performance_compiler_tests` | 22/22 | Six channels across 8/44.1/192 kHz, tempo change, interpolation, source offset, exact scope edges, manual ownership, unselected proposals, immutable evaluation, null/out-of-range rejection, explicit zero, and continued StyleBlend refusal |
| `seam_performance_snapshot_tests` | 47/47 | Real procedural PCM for all six accepted channels through application proposal/acceptance commands; exact equivalence to manual constant controls; save/reopen, undo/redo, frozen jobs, content identity, and named sample-route refusal |
| `seam_neural_worker_protocol_tests` | 26/26 | Scoped generated breathiness, neutral manual ownership, explicit zero versus per-phone priors, silence, drawn/manual/accepted precedence, and request wire round-trip |
| `seam_neural_render_tests` | 7/7 | Actual graph-declaration admission, inert proposals, selected zero/nonzero breathiness, and named refusal of unsupported accepted timbre |

Focused totals: 102/102 cases per configuration. Release CTest passed in 4.22 s;
Debug in 19.65 s. The neural graphs in admission tests are fixtures: they prove
control routing and rejection, not neural voice quality. The procedural PCM
comparisons exercise the actual synthesizer, but are not perceptual acceptance.

The broader Release rerun passed 7/7 CTest targets (955 individual cases) in
19.72 s: the four focused suites plus `seam_tests` (830/830),
`seam_authoring_render_coordinator_tests` (19/19), and
`seam_original_singer_song_journey_tests` (4/4). Logs are retained under
`build-u4-macos/u6-generated-timbre-regressions-ctest.log` and
`build/debug/u6-generated-timbre-ctest.log`; detailed case output is in each
configuration's CTest `Testing/Temporary/LastTest.log` at this checkpoint.
Independent review remains pending.

## Remaining scope

StyleBlend remains unsupported by this compiler until paired style resources
and their renderer semantics exist. This increment does not create a new
performance generator or train a neural model, add timbral algorithms to sample
renderers, or qualify a production singer. No new native UI interaction,
Windows runtime, human listening, or full-product acceptance is claimed.

The full plan, mandatory qualified neural singer, and unresolved product gates
remain unchanged. No additional full roadmap unit is counted complete.
