# U6: production phonetic pickup and release context

Date: 2026-09-22. Baseline: `d3b763c361216844af772c4a1719ec718085ceff`.
Scope: U6 timing consumption and U7 context/output ownership. Full U6, U7 and
Beta acceptance are not claimed. Independent review is pending.

## Reproduced production gap

Developer 2's read-only gap audit identified a mismatch between admitted timing
and production rendering. At 120 BPM, a vowel note at ticks 480–960 has a score
window of 250–500 ms. Authored offsets of -30,000 and 280,000 microseconds ask
for a phone at 220–530 ms, inside a region at ticks 0–1920. The timing compiler
can represent it, but the neural factory/runner used only the score window;
procedural vowels explicitly rejected it. Previously passing direct neural
adapter tests supplied the wider context themselves and did not cover this
integration boundary.

Two new production regressions failed before repair: the procedural snapshot
was refused, and the neural production runner could not prepare its request.
The baseline build succeeded. Logs: `build-u4-macos/u6-context-red-build.log`
and `u6-context-red-ctest.log`. These are local development logs, not immutable
signed-release evidence.

## Implementation

- `CompiledScorePerformance::phoneticContext()` derives the union of score and
  resolved phone spans. It requires resolved starts and valid owning notes,
  enforces the region's frame boundaries and the existing 32 Mi-frame phrase
  limit, and does not allocate per-frame score arrays. Source-dependent starts
  are not invented. Classical source-map context remains on its existing path.
- `atPhonetic(frame, owner)` is an explicit phone-owned view. In-note frames
  retain the normal evaluator. Outside that note, pitch/vibrato and dynamics
  retain its edge values; a closed staccato/accepted-release tail stays closed.
  A following active score note cannot supply its pitch, envelope or timbral
  controls. Timbral ownership remains at actual score time, not edge-clamped
  time. The ordinary `at()` contract is unchanged. A bounded note-ID index
  avoids linear per-sample note searches.
- Procedural excitation, post-tract gain and tract control runs use the active
  gesture's owner. Vowel scheduling and recipe articulation admit the complete
  resolved phonetic context. Source phase remains continuous across processing
  cuts; output ownership never restarts a phonetic onset.
- Neural preparation uses the same owner evaluator while retaining explicit
  phone voicing, silence, model breathiness defaults and the existing two-second
  per-owner extension limit. That limit and model/transport frame budgets are
  not relaxed.
- Factories, splitting, markers, procedural checkpoints, scheduler publication
  and the authoring neural runner consume the shared context. Neural pipeline
  validation now also requires exact full-context output when no explicit owned
  window is supplied.
- Complete-region procedural/neural snapshots retain tempo events through the
  region end: a tempo change after the last note still defines the actual
  region boundary. Sample phrase extraction keeps its previous scope.
- Broad verification caught a regression in existing whole-note breath/closure
  baking: those event starts were resolved only inside recipe articulation.
  Timing policy revision 6 now records that existing note-owned start in the
  shared timing plan, including an authored end without an authored start.
  Explicit starts still win; unresolved consonant clusters remain unsupported.
- Performance compiler revision is 21; sustained procedural revision is 15;
  articulated-stream revision is 12. Existing snapshot identities include these
  revisions, so old cached output cannot stand in for the changed algorithm.
  No project, recipe, model or worker-wire schema changes are required.

## Verification scope

New coverage includes:

- normal vowel and fricative/vowel snapshots with audible 30 ms pickups/tails
  at 8, 44.1, 48 and 192 kHz;
- exact whole-output versus independently rendered chunks and resumed stream
  checkpoints, plus scheduler assembly at 48 kHz;
- marker extents, ownership overrun rejection, frame-zero admission and
  before-region rejection;
- a following note's changed pitch leaving the preceding phone's entire tail
  byte-identical, with a closed staccato tail remaining silent while the next
  note later sounds;
- owner-edge pitch/dynamics versus actual-time timbre at four rates, including
  a tempo change;
- the actual authoring runner with the admitted silence transport fixture,
  validating request admission and exact output extents without claiming audio
  quality;
- real ONNX worker regression code requiring nonzero pickup/tail audio and exact
  non-hop-aligned chunk reconstruction with arithmetic model fixtures;
- a post-note tempo change that makes an otherwise apparently contained tail
  exceed the true region end.

The 8 kHz frication fixture explicitly uses a 2.5 kHz noise band. The default
5 kHz band is correctly inadmissible at that rate; renderer frequency limits
were not changed. Existing tests that expected rejection solely because a vowel
crossed its note box now expect admission; independent region/PCM tests retain
the actual safety and musical requirements.

A passing transport fixture is not a substitute for the separately executed
ONNX regression. Final producer results after both repairs are:

| Configuration | Executed verification | Result |
|---|---|---|
| `build-u4-macos`, Release, warnings-as-errors ON, CLAP OFF | 12 selected CTest targets, 1,056 overlapping case executions | PASS, 20.99 seconds |
| `build/debug`, Debug, warnings-as-errors OFF, CLAP ON | Same 12 targets, 1,187 overlapping case executions | PASS, 167.86 seconds |
| `build/release`, Release with native ONNX configured | Production-worker English and Japanese-moraic-nasal fixtures, each including nonzero extended-context rendering and exact owned chunks | 2/2 PASS, 6.89 seconds |
| System Python | `python3 -m unittest tools.voice_model_training.test_prepare_captured_teacher` | 12/12 PASS, no skips |

All three targeted C++ builds and their CTest commands returned exit 0. The
first two runs include `seam_tests`, phoneme timing, performance compiler and
snapshot suites, voice design, neural protocol/render/runner/workflow/selection,
authoring render coordinator and the original singer song journey. The
monolithic counts are 842 Release and 973 Debug; separate focused suites
overlap those checks and are not additional unique product requirements.
`build-u4-macos` and `build/debug` do not configure the native ONNX target;
the third configuration above supplies its actual execution, not a skipped
in-process case. No compiler warning setting was weakened.

Final logs are `u6-context-review-final-{build,ctest}.log` in the first two
build directories and `u6-context-native-review-final-{build,ctest}.log` in
`build/release`. Source-closure validation passes after indexing this new
report, and whitespace checks pass. These are producer-run development
checks; independent pinned review remains pending.

The first broad runs passed ten selected targets in each configuration but
failed the monolithic suite on the existing declared-event bake case (Release
841/842; Debug 972/973). Those runs are not final passing evidence. The
shared-timing repair subsequently passed in both configurations. The
initial native-worker build also caught a shadowed test variable; it was renamed
without changing warning flags, after which both native ONNX cases passed.

## Independent provisional review and reproduced repair

Developer 2's source-only review found one P1 before pinned approval: an
articulated pickup followed by an early final release leaves a valid silent
suffix in the score-union context. After the final gesture, the renderer still
dereferenced its missing gesture while selecting a tract pose. The null access
predated this increment, but the widened pickup admission exposed it to another
valid input. The reviewer did not execute a crash or claim approval.

The producer added the exact normal-path regression: an `s` pickup at -30 ms,
an `a` from 0–200 ms on a 250 ms note, and 50 ms of owned trailing silence.
Release CTest reproduced `Exception: SegFault` before repair (exit 8; log
`build-u4-macos/u6-context-review-red-ctest.log`). Pose selection and transition
accesses now require an active gesture. Source/filter/noise state continues
advancing through uncovered context; the output window is not shrunk and
valid timing is not rejected. The regression requires exact full extent, zero
trailing PCM, no trailing phone marker, a trailing-only crop, independent and
resumed chunks, and scheduler assembly. The final producer matrix above passes
with this regression included; the pinned reviewer decision remains pending.

## Remaining boundaries

This does not implement accepted null-pitch/target-voicing conversion over
voiced sample material. It does not qualify a learned singer, pronunciation
quality, recorded voice resources, perceptual continuity, a signed installation
or a DAW/Windows host tuple. The existing neural research/listening decisions
are unchanged. No training, model replacement or listening acceptance occurs
in this increment, and no additional complete roadmap unit is counted.
