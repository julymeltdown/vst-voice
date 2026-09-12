# U5 timing acceptance review — 2026-09-06

Final verdict: **U5 local implementation acceptance PASS**, supported by `U5_ACCEPTANCE_AUDIT_2026-09-06.md` and the post-fix Debug/Release broad and focused runs. The chronological findings below explain defects and follow-ups; their earlier incomplete verdicts are superseded by that audit. They do not imply full Beta GO, pitch-preserving singer quality or publication.

## Consolidated follow-up verdict

Unaligned-unit follow-up: normal snapshot selection now requires evidence for every multi-nucleus candidate, including those without edits. It discovers matching candidate sidecars before selection, falls back to compatible smaller units, and returns an explicit conflict for forced unaligned long units. The frozen pipeline also rejects a manually supplied legacy plan without alignment. Resource-free selector calls remain planning-only unless callers opt into this capability policy. New tests cover fallback, forced rejection, absent alternatives and pipeline defense; the existing complete forced-relationship fixture now supplies valid landmarks. All 63 focused cases pass in each configuration. A consolidated source/contract audit and broader native/core regression run are still required before changing the U5 verdict.

Package identity follow-up: bank catalog identity now includes bounded, sorted, exact-byte hashes of present manifest-unit alignment sidecars, preserving hashes for banks without sidecars. Signed-package preflight agrees with installed identity. A valid sidecar survives pack/install with matching content hash and idempotent reinstall; modifying installed bytes invalidates receipt-backed trusted status. Identity is not semantic or musical approval. The historical package-identity gap below is superseded; unaligned multi-nucleus behavior and consolidated U5 acceptance remain open.

Cross-note follow-up: the aligned waveform/command/cache regression now also spans two adjacent same-pitch notes, checking the second note's own 30 ms offset rather than a first-note-relative offset. It verifies both anchors, exact undo/redo and disk replay. A changed second-note pitch originally rendered successfully while retaining the first placement pitch; the new failing regression exposed that defect. Aligned raw rendering now rejects pitch changes within the unit (raw revision 5). This is an explicit capability boundary, not implementation of U6 whole-melody pitch following. Unaligned multi-nucleus behavior, package identity and consolidated acceptance remain open.

Snapshot/command integration follow-up: matching interior-edited candidate units now have present sidecars and source WAVs frozen before selection, sharing the selected-resource loader and aggregate budgets. Missing sidecars preserve fallback; invalid present resources reject. The normal `UpsertPhonemeOverrideCommand` apply/revert/reapply path now verifies second-vowel displacement through snapshot creation, exact original/edited waveform restoration and content identities, plus disk-cache replay after memory eviction. Focused Debug/Release tests pass. Cross-note acceptance, unsupported unaligned multi-nucleus policy, bank packaging identity and full U5 acceptance remain open; the historical discovery/undo gaps below are superseded by this follow-up.

Evidence-aware selection follow-up: candidate generation, forced selection validation and the timing solver now accept source alignment evidence and validate its exact unit/phone coverage, WAV digest and frame bounds before allowing interior timing edits. The frozen pipeline forwards its owned evidence. A direct selection/pipeline regression moves the second vowel 1,440 frames at 48 kHz within the same long unit while preserving the first, and rejects stale/missing evidence. Snapshot-factory discovery still happens after selection; automatic editor-originated snapshot creation, command undo and cache replay for this edit remain unverified and unfinished. This is not U5 completion.

Latest integration status: optional audio-bound sidecars are now frozen and included in render identity v4. The normal frozen phrase pipeline consumes their full source-to-target maps under an effective Raw renderer request. A WAV-backed regression verifies both nucleus samples at their requested frames after the original sidecar is removed, plus explicit/global renderer precedence and rejection of unsupported raw overrides. This closes unedited aligned-unit dispatch, not general U5 acceptance: alignment-aware interior-edit selection, nonfirst-edit command/undo/cache replay, cross-note coverage and bank packaging identity remain open. Pitch-preserving alignment remains unsupported. Earlier findings and follow-ups below are chronological evidence, not statements that these newly implemented components are still absent.

The explicit-interior-boundary, shared display, region-release, overlap-window and dedicated-test findings below now have implementation or verification follow-ups. However, **general multi-nucleus acoustic-unit alignment remains incomplete**. The existing positive acceptance examples must not be generalized to this path.

`UnitCandidateGenerator` still accepts a unit spanning several nuclei when no interior override is present; `supportsExplicitPhonemeTiming` only excludes unsupported explicit boundaries. The `interior timing edits select compatible smaller units and reject forced long units` test deliberately confirms that the unedited `かき` sequence selects one long unit before an edit forces smaller CV units. `TimingSolver` then searches for the first nucleus and publishes one `desiredVowelOnset` for the entire unit. `UnitMarkers` likewise exposes one vowel-onset landmark, not one source landmark for every nucleus.

Consequently, the compiler's distinct default anchors for subsequent nuclei are not sufficient evidence that the long unit renders those nuclei at their assigned times. This is a source-confirmed missing mapping, not a listening-quality judgment. Existing frame-accurate tests prove the separate-unit and first-nucleus/onset paths only.

Required completion path:

Target-contract follow-up: `TimedUnitPlacement::phonemeTargets` now retains every covered compiler record, including both nuclei in the long-unit regression. Timing plans must cover the token sequence exactly once, bounding total target storage. Source-landmark persistence, validation and waveform mapping remain unimplemented; this data-path change alone does not close the finding.

Source-contract follow-up: `SourcePhonemeAlignment` now validates authored phone landmarks against a unit, exact externally verified audio hash and decoded frame count. It requires complete ordered phone coverage within sample bounds and does not infer landmarks or claim phonetic accuracy. Persistence and renderer consumption remain open; the validation type alone is not multi-nucleus audio support.

Mapping follow-up: `compileSourceTargetMap` now pairs authored source positions with retained nucleus/explicit target boundaries and exclusive endpoints. It checks coverage, repeated keys, target bounds and strictly ordered noncontradictory knots. The two-nucleus regression verifies both source/target pairs. This constructs the warp map; applying it to audio and resource discovery/cache integration remain open.

Waveform follow-up: `applySourceTargetMap` now performs bounded, cancellable linear interpolation across the map and verifies exact multi-landmark relocation in a diagnostic waveform. It is a standalone operator, not yet an installed-resource/render-pipeline path. Its local-rate changes are not pitch-preserving; source discovery, dispatch/capability handling and cache integration still remain.

1. Carry all covered target nucleus/phone anchors through placement, not only the first.
2. Define verified source landmarks for multi-nucleus units and apply a corresponding piecewise source-to-target mapping. Never infer phoneme source positions from equal source-duration fractions and call them measured landmarks.
3. When a unit lacks the required source alignment, use compatible smaller units if available or return an explicit capability conflict. This is a safe compatibility path, not a substitute for multi-nucleus support in the full product.
4. Verify an unedited multi-nucleus unit against every requested anchor, then edit a nonfirst nucleus and verify actual output displacement, undo and cache replay. Include cross-note units so this contract can support U6's whole-melody requirement.

No runtime behavior changed in this consolidated source review and no test suite was rerun. The execution ledger contains the recent focused results; no broad all-green or U5 completion claim follows from them. The full Beta GO scope remains unchanged.

Authority: `docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md:332`. Scope remains ordered target phoneme spans, deterministic acoustic edits/undo, and bounded conflicts. Later production-singer listening qualification is not substituted for this implementation gate.

## Current evidence

- `phoneme_timing_plan.cpp` allocates successive nucleus anchors, applies note-relative offsets, rejects reversed nuclei/crossing explicit ends, and lets automatic ends follow edited nuclei.
- `timing_solver.cpp` uses the first covered nucleus, the final covered token end, and an optional explicit start on the first token. It rejects insufficient source-transition space rather than extending a target end.
- Raw and dispatched renderers retime around a vowel landmark for explicit onset starts. WAV-backed command tests verify fixed vowel anchors and exact audio undo.
- The snapshot regression verifies negative preutterance clipping sample-by-sample at 44.1/48 kHz and identical disk-cache replay.
- Timing/raw revisions 2/3 separate the changed algorithms from older cache identities.

These checks support the concrete behaviors above, not every token boundary. No new test execution is claimed for this source-only review; the execution ledger records the completed runs.

## Remaining findings

### 1. Intermediate edits inside a selected unit can be unused

Implementation follow-up: candidate generation and the timing solver now share an explicit-boundary capability check. Automatic selection excludes units that hide interior starts/ends; forced incompatible units reject with a specific conflict. A long `かき` unit is replaced by compatible CV units in the regression, with WAV-backed rendered start/end assertions. This addresses the ignored-boundary path with compatible smaller units, not arbitrary per-phone source alignment or the remaining display/coarticulation findings.

`libs/seam-synthesis/src/timing_solver.cpp` selects the first nucleus in a unit and takes only the final token's end. An explicit start on an intermediate token, or an explicit end on a nonfinal token, can therefore be represented in the compiled plan without affecting that unit's audio. The onset retimer has one vowel landmark, not a general per-phone source alignment map.

Required behavior: a manual boundary must either be represented by supported source/target landmarks and rendered, or constrain unit selection to compatible smaller units. A forced unit lacking the required alignment must produce a specific conflict, not accept an inaudible edit. Do not fabricate per-phone source landmarks from uniform source duration.

Tests: multi-phone units with edits on each interior start/end, suitable smaller-unit alternatives, forced incompatible units, and actual audio/undo. Test changes to unit selection as well as timing compilation; a timing-plan field alone is insufficient evidence.

### 2. Editor lane geometry contradicts renderer timing

Partial repair: start-only overrides now retain the preexisting displayed end, with hit-testing regressions at two zoom levels. The role-weight layout still differs from the renderer's timing model; this local geometry repair does not close the shared-display requirement.

Further implementation: nucleus spans and explicit timing now come from the shared compiler. Source-dependent onset/coda extents use explicitly marked estimate labels in a separate band; compiler conflicts are flagged instead of presented as valid target spans. Native painting and hit testing now share absolute coordinates, and unit-row targeting no longer inherits the phoneme bands. The authoring guide documents estimate/conflict semantics and the display reference rate. Role-weight geometry remains only as flagged conflict fallback, not normal resolved timing.

`libs/seam-editor-ui/src/phoneme_lane_model.cpp:10` defines independent role weights; `:36` onward distributes the entire note width using those weights. This disagrees with the renderer's nucleus-anchored default allocation. At `:51`, a start-only override replaces x but retains the old width, so the displayed end can move even when the intended end does not.

Required behavior: introduce one explicit display contract derived from target timing. Separate the nucleus anchor, target start/end and source-dependent preutterance estimate; do not label a source estimate as a resolved target boundary. Onset and vowel must not both be drawn as the same full syllable span merely because they share a nucleus anchor. Display conflicts visibly rather than hiding reversed widths behind a two-pixel clamp. Use the same geometry for painting and hit testing.

Tests: `かき` default and edited boundaries, start-only and end-only edits, tempo changes, zoom, short spans, and rendered anchor-to-pixel agreement. Verify compact native layout without overlap.

### 3. Release/coarticulation policy is only partially encoded

Release-bound follow-up: explicit ends can extend past a note within its region, but region overruns reject without mutating saved offsets. A WAV-backed snapshot test verifies the actual tail end; compiler tests verify overrun rejection and independent anchors on overlapping notes. Timing revision 5 reflects the new boundary policy. This resolves the region-release boundary, not every coarticulation/source-transition interaction.

Composition follow-up: a deterministic regression now verifies the existing maximum-overlap crossfade behavior at exact sample positions, zero-window handoff, negative-limit rejection and unchanged unit/timeline extents. The authoring guide distinguishes crossfade duration from incoming-unit duration and phase continuity from target placement. This is composition-contract evidence, not perceptual qualification.

Frame-zero preutterance clipping is verified. Within-note nucleus ordering and selected-unit transition feasibility are implemented. Permissible overlaps across notes and the treatment of explicit release ends still need a stated, enforceable contract and matching tests. Overlap reporting alone is not a complete policy.

Required behavior: distinguish allowed source preutterance overlap from an edited target boundary crossing another protected target. Define region/project release limits and conflict versus clipping rules without mutating saved offsets. Test the actual composition result, including overlapping notes and phrase edges.

### 4. Dedicated timing test artifact remains missing

Implementation follow-up: `tests/test_phoneme_timing.cpp` and the `seam_phoneme_timing_tests` target now exist, covering tempo-resolved endpoints, output-rate rounding, trailing-coda ownership, malformed inputs and insufficient frames. The coda regression exposed and drove a correction to the preceding-nucleus lookup. This establishes the dedicated artifact; the matrix must still expand with the remaining display and release/coarticulation contract.

The approved plan names `tests/test_phoneme_timing.cpp`. Current regressions were added to `test_synthesis.cpp` and `test_performance_snapshot.cpp`; a dedicated test target/file must collect the compiler contract and boundary matrix without removing the end-to-end tests.

## Implementation order

1. Make intermediate timing edits observable through compatible unit selection/alignment, with explicit conflicts for unsupported forced units.
2. Establish shared target/display boundaries and repair lane geometry/hit testing.
3. Complete release/coarticulation policy and its dedicated boundary matrix.
4. Rebuild and run focused timing, synthesis, snapshot, native UI and cache verification in both configurations; update the U5 verdict only from that evidence.

The existing equal-time nucleus allocator and linear onset retimer are deterministic baselines, not claims of perceptual quality. Preserve the full Beta GO goal and the current uncommitted worktree while completing the missing behavior.
