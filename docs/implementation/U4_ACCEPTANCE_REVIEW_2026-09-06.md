# U4 acceptance review — 2026-09-06

Final verdict: **U4 IMPLEMENTATION ACCEPTANCE PASS (local worktree).** The initial review below found concrete gaps; the follow-ups and final evidence resolve them for the approved U4 scope. Historical incomplete verdicts below describe checkpoints, not the current conclusion. This is not a release gate or a claim that U4 tests cover every later product workflow.

Authority: `docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md:316`. The full Beta GO scope is unchanged. Repository base HEAD is `741ae2f244b9d3ff8eb6f31dc1f73cae54ea974d`; reviewed implementation includes uncommitted working-tree changes. HEAD alone does not identify the reviewed source.

## Acceptance evidence

Final review: the shared-path requirement includes standalone coverage analysis, now migrated from the raw adapter to the bounded resolver with an oversized-input regression. A source sweep for `.phonemize(` in `libs` now finds only the adapter invocation inside `pronunciation_resolver.cpp`. The insertion/ambiguity criteria are covered by command-level continuation tests and the independent correspondence oracle. Bounded outcomes and persistence are covered by resolver, coverage and schema-8 tests. Undo and consumer identity are covered across note topology, transfer/review commands, native/embedded paths and snapshot construction. Split and copy relationships have explicit mapped validation; active render dependencies now stay in the same phrase or produce an explicit maximum-duration conflict. This completes the bounded Japanese-adapter implementation described by U4, not later English/Korean packages or production resource qualification.

All eight relevant test targets were rebuilt from the current worktree in strict Debug and Release. Both configurations then passed 8/8 CTest entries: 465 core, 14 correspondence, 36 region-state, 14 schema-8, 15 performance-command, 5 preservation, 12 snapshot and 13 live-job-context cases (**574 cases per configuration**). Debug elapsed 85.95 seconds; Release 12.74 seconds. Logs: `build/dev/Testing/Temporary/LastTest.log` and `build/release/Testing/Temporary/LastTest.log`. `git diff --check` passed. These results supersede the smaller historical verification sets below. Source remains uncommitted; full-build/source-closure publication and installed-platform qualification are not claimed.

| Requirement | Current evidence | Assessment |
| --- | --- | --- |
| Shared editor/render pronunciation | Native scene and hit testing, technical editor, embedded inspection and render snapshots call the shared resolver/inspection wrapper. Snapshot resolution occurs on the complete region before phrase projection (`render_snapshot.cpp:460`). | Shared path established for the inspected Japanese implementation; projection-boundary behavior needs the check below. |
| Revisioned language/resource/token identity | `pronunciation_resolver.cpp` bounds collections/text, hashes bundled implementation inputs, and assigns token context IDs plus lyric ownership. | Implemented for the bundled adapter, not proof of external dictionary availability or later language packages. |
| Insertions must not transfer a lock to a different sound | `test_commands.cpp:169` tests insertion before a continuation; `test_override_reconciliation.cpp` tests ambiguous repeated sounds and an exhaustive independent alignment oracle. | Covered for these paths; split/copy obligations remain. |
| Explicit bounded empty/missing outcomes | Resolver and inspection tests cover repeated lyric expansion bounds, empty lyrics and resolution failures; review excludes unsupported/warned targets. | Evidence supports current bundled-adapter boundaries, not external-resource qualification. |
| Undo restores pronunciation and dependent records | Command tests cover add, move, resize, delete and lyric reassignment; technical review tests cover explicit phoneme/unit/seam rebinding. | Covered paths restore captured metadata and override vectors. Region split and selected-note copy need broader unit/seam evidence. |
| Ambiguous work stays available | Unresolved payloads persist; the combined native/embedded review panel permits explicit rebinding with stale checks. | Implemented, but a retained-data UI cannot repair topology commands that incorrectly keep records active or omit copying them. |

Latest completed logs inspected in this review contain **464 core cases and 5 performance-preservation cases passing in each of Debug and Release**. No new test execution is claimed for this documentation-only review. Those suites do not establish completion of the missing cases below.

## Findings that determine the next implementation work

Follow-up: finding 1 now has a mapped effective-token validator wired into split and clone operations, plus a split regression covering broken cross-boundary spans, lost predecessors, preserved internal joins and changed continuation context. This closes the identified split-state omission, subject to the implementation ledger's verification results. Findings 2 and 3 remain open; U4 is still incomplete.

### 1. Region split transfers unit/seam addresses without verifying complete relationships

`libs/seam-application/src/arrangement_commands.cpp:280` partitions unit overrides by their starting note; the following seam loop uses only the incoming note. `:353` and `:355` call `rebindTransferredPhonemeContexts`, which verifies phoneme overrides, not complete unit spans or seam predecessors.

Consequences supported by the source: a unit starting on the left can retain its original token count after some covered notes move right; a right-side seam can retain an active flag despite losing its left-side predecessor. The split test at `tests/test_region_performance_state.cpp:427` checks phoneme continuation bindings, not these unit/seam cases. An audible failure has not been reproduced in this review; the command-state omission is directly visible.

Required repair: compare full source and destination effective token sequences through the explicit old/new note-ID maps. Keep a unit active only when all constituent sounds survive contiguously; keep a seam active only when both neighbors (or the original initial boundary) remain equivalent. Preserve invalidated payloads unresolved. Test cross-split spans, the first right-hand seam, unchanged internal spans/joins, continuation changes and exact undo/redo.

### 2. Selected-note duplication does not copy unit or seam overrides

Follow-up implementation: `CopyNotePerformanceCommand` now copies unit/seam records, validates copied relationships using only the selected-note map, retains partial relationships unresolved, and rejects occupied keys before publication. Direct command and actual piano-roll duplication regressions cover this path; see the execution ledger for verification. Phrase-projection finding 3 remains open.

`libs/seam-application/src/performance_commands.cpp:21` copies note-scoped performance state and phoneme overrides. The implementation has no unit-selection or seam-copy path. Existing records are not deleted, but the duplicate does not carry this editing work.

Required repair: copy complete selected spans/joins through the source-to-target map, retain partially selected relationships explicitly unresolved where their start/incoming note is copied, and reject destination collisions atomically. Do not silently extend a partial span over unrelated destination notes. Verify preserved renderer/tuning/lock payloads, changed continuation context, bounds and exact composite undo/redo.

### 3. Phrase projection needs a relationship-level regression before sign-off

Follow-up: snapshot construction now explicitly rejects active spans or seams whose full-region relationships are incomplete in the requested phrase. A real manifest/audio fixture verifies split rejection without project mutation and successful construction when the full relationship is included. This prevents silent reinterpretation, but does not yet provide automatic dependency-aware segmentation. U4 remains open until that product path is implemented and bounded-duration behavior is verified.

`libs/seam-rendering/src/render_snapshot.cpp:59` retains unit overrides by start-note membership and seam overrides by incoming-note membership. Later code filters the full-region token sequence to phrase notes before selection. Full-region resolution correctly preserves continuation context, but that alone does not prove every retained span/join remains valid in a projected segment.

This is an **unverified boundary risk**, not a demonstrated defect: phrase segmentation or selector validation may already prevent an invalid effective relationship. Add a regression with a manual cross-note span and seam at a phrase boundary, inspect the actual selector/render outcome, then fix whichever layer owns the violated invariant if it fails. Do not count the existing continuation-only test as this evidence.

## Execution order and stopping rule

Segmentation follow-up: active unit spans and seam neighbors now forbid internal phrase cuts. Overlapping dependencies form atomic groups, and duration lookahead permits a split before a group. A required multi-note group exceeding the configured maximum fails explicitly; unresolved edits do not constrain grouping. Snapshot tests now require default segmentation to include complete relationships, while separately constructed partial segments still exercise the defensive snapshot guard. All three findings have implementation follow-ups; final U4 sign-off still requires the matrix-wide verification listed below, not just this focused change.

1. Repair and test split relationship transfer using one reusable mapped-token mechanism.
2. Reuse it for selected-note unit/seam duplication without copying unrelated fixed-region performance ranges.
3. Exercise phrase projection at actual unit/seam boundaries.
4. Recheck this acceptance matrix and run the affected core, resolver, topology and render tests in both configurations.

U4 may be closed only after these findings are resolved or disproved with matching evidence. Later language packages, production singers, listening studies and installed-platform qualification remain in the full plan; they must not be either claimed complete here or silently added as prerequisites to this bounded U4 implementation review.
