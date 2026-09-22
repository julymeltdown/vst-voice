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

- Compiler revision 18 consumes accepted Formant (semitones), Breathiness,
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
  disables that prior outside its scope. Ownership is evaluated at the actual
  absolute frame for the phone's owning note, not at a clamped note edge or on
  a neighboring note. Pitch/envelope extension remains separate. Explicit
  silence remains silent.
- Procedural source tilt follows every compiled frame. Tract processing groups
  only consecutive equal Formant/Gender values, sampling from the excitation's
  start rather than its future end. Short selections and manual-ownership
  boundaries therefore cannot disappear between caller blocks. The source and
  tract halves of Gender observe the same score time.
- Normal sample snapshots refuse all six accepted timbral controls by name.
  Neural snapshots admit accepted breathiness only when the actual admitted
  acoustic graph declares that conditioning input; they refuse the other five.
  Accepted neutral or manually overridden selections remain explicit capability
  requests. Drawn neutral curves without accepted selections retain their prior
  behavior. No silent Raw fallback is introduced.
- The compiler revision enters existing render/cache identity and recorded
  renderer provenance. No project schema migration is needed: these lanes and
  their ownership already persist in schema 18.
- Audible DSP semantics are bound to source-filter engine revision 15,
  sustained renderer revision 14 and articulated renderer revision 11. Existing
  distributed engine-14 recipes are not silently certified for engine 15:
  packages must be rebuilt and re-reviewed under the new declared revision.

## Independent review and repairs

Developer 2 returned REQUEST CHANGES for the first increment, `d939154`.
Its four focused suites passed 102 cases per configuration, but that evidence
did not cover two important cases:

1. Constant-control PCM comparisons missed block-dependent procedural timbre.
   A new varying-control subdivision regression reproduced the defect locally
   (47 snapshot cases passed, 1 failed). Frame-based tilt and constant-control
   tract runs now replace caller-block sampling, including the sustained path's
   future-frame lookup. The test covers both sustained and articulated voices,
   all six controls, ramps, a 75-frame selection containing a neutral manual
   island, 127/733/1024-frame chunk cuts, checkpoint replay and no early effect.
2. The reviewer independently reproduced breathiness scope leaking into
   explicit neural preutterance/tails. Request assembly now keeps note-edge
   pitch/envelope extension separate from absolute-time timbre ownership.
   Protocol regressions cover exact note boundaries, zero/nonzero accepted
   values, empty-curve neutral manual Replace, priors, silence, unchanged
   F0/dynamics, and a preceding note whose accepted lane must not be borrowed.

Revision 18 and the renderer revisions above supersede the first increment's
audio identity. No training or learned-model inference was used for either
repair. Independent re-review is pending.

## Evidence

Fresh strict Release and Debug builds, five focused CTest targets in each:

| Suite | Cases | Evidence supplied |
|---|---:|---|
| `seam_performance_compiler_tests` | 22/22 | Six channels across 8/44.1/192 kHz, tempo change, interpolation, source offset, exact scope edges, manual ownership, unselected proposals, immutable evaluation, null/out-of-range rejection, explicit zero, and continued StyleBlend refusal |
| `seam_performance_snapshot_tests` | 48/48 | Real procedural PCM for all six accepted channels through application commands; manual-equivalent constants; scoped ramps/short selections across sustained/articulated chunking; save/reopen, undo/redo, frozen jobs, content identity, and sample-route refusal |
| `seam_neural_worker_protocol_tests` | 26/26 | Scoped breathiness, explicit zero, manual ownership and priors, exact preutterance/tail boundaries, neighboring note exclusion, silence, unchanged F0/dynamics, and wire round-trip |
| `seam_neural_render_tests` | 7/7 | Actual graph-declaration admission, inert proposals, selected zero/nonzero breathiness, and named refusal of unsupported accepted timbre |
| `seam_voice_design_tests` | 39/39 | Existing source/tract/articulation DSP regressions under the new renderer identities |

Focused totals: 142/142 cases per configuration; Debug CTest passed in 30.07 s.
The neural graphs in admission tests are fixtures: they prove
control routing and rejection, not neural voice quality. The procedural PCM
comparisons exercise the actual synthesizer, but are not perceptual acceptance.

The broader Release rerun passed 8/8 CTest targets (995 individual cases) in
24.89 s: the five focused suites plus `seam_tests` (830/830),
`seam_authoring_render_coordinator_tests` (19/19), and
`seam_original_singer_song_journey_tests` (4/4). Logs are retained under
`build-u4-macos/u6-timbre-final-ctest.log` and
`build/debug/u6-timbre-final-ctest.log`; detailed case output is in each
configuration's CTest `Testing/Temporary/LastTest.log` at this checkpoint.
Independent re-review remains pending.

## Remaining scope

StyleBlend remains unsupported by this compiler until paired style resources
and their renderer semantics exist. This increment does not create a new
performance generator or train a neural model, add timbral algorithms to sample
renderers, or qualify a production singer. No new native UI interaction,
Windows runtime, human listening, or full-product acceptance is claimed.

The full plan, mandatory qualified neural singer, and unresolved product gates
remain unchanged. No additional full roadmap unit is counted complete.
