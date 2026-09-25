# Designer handoff and phrase-duplication checkpoint

Status: local implementation evidence, not U21/U22/U23 acceptance or Beta GO.

## Designer to frozen job: hybrid verification

The native Studio test opened a synthetic recipe, changed aspiration from 0.05 to 0.25, selected **Prepare generation job from current draft**, chose a score with no saved procedural recipe reference, and saved `draft-capture-qa.seamjobdir`. The UI reported `JOB PREPARED / NOT GENERATED`; producer generation remained 2.

The package contains aspiration 0.25. The source recipe remains 0.05, and the original plain score retains a null procedural recipe reference. A later native session reopened the original at 0.05. The planned undo in the first session was not performed before the test deadline.

Two native defects were repaired:

- AppKit accessibility queried its cached Designer children before checking the current mode's tree. Returning to producer could expose stale Designer controls. It now checks the current tree first and clears the cached snapshot when no tree is supplied. The rebuilt app's mode switch was checked live and no longer exposed Designer nodes.
- Job preparation supplied an extension both in the suggested basename and the save-panel extension. The suggestion is now `generation-job`, with `seamjobdir` supplied separately. The fix builds; the corrected default has not been checked live. The successful test explicitly entered a basename without the extension.

The Mac locked during the second session's generation picker. Only the disposable test process was terminated (exit 143); this was not a clean GUI completion. Canonical CLI continuation rendered the captured job successfully, unapproved, with WAV SHA-256:

`86197ff8aabf31e4d9f7a9d48347b5c0ff05172ef33a5b40deea2d96ae6279fb`

Durable producer recovery now shows generation 3, one `changed-take` assignment in `MARKER_REVIEW`, no reviews, and retained expectation SHA-256:

`76ce0a2bc087bb13de5b367d7900a206f4ead72b33aa0e268b6fa0e8b6d7e092`

A subsequent canonical CLI import retry exited 0 with `AlreadyCollected`, `active: true`, generation 3 and the same audio hash. This verifies collection and historical retry, not completion through GUI only. All material is `SYNTHETIC_TEST_ONLY`; no commercial rights or listening-quality acceptance is implied.

Fixture root: `/var/folders/j4/41h_5mjj7j9d8f2j2bzcsngw0000gn/T/project-seam-articulated-export-67376-0`. Temporary artifacts are evidence references, not durable release assets.

## U23: common phrase translation

`PianoRollModel::duplicateSelection` previously placed each duplicate at `source.start + source.duration + snapGrid`. Different durations therefore changed relative onsets and rests, even though the operation was presented as duplicating a phrase.

It now validates source geometry and computes one translation:

`offset = latest selected end - earliest selected start + snapGrid`

Every new note starts at `source.start + offset`. This keeps the prior single-note placement convention, including the one-grid gap, while preserving multi-note rhythm. Span, gap and translated ends are checked before command execution. The existing composite command remains one undo group; failed admission does not change selection or append undo history.

Regression coverage includes reversed selection order, unequal 240/720-tick durations, a 600-tick rest, exact placement, undo/redo identity, and near-int64-limit rejection without partial insertion. The arithmetic-limit test injects boundary geometry after ordinary note creation to isolate duplication admission from AddNoteCommand's separate region-growth bounds.

This change does **not** complete U23. Shared-lyric remapping, independent duplicated slur identities, complete owned-edit copying, remaining slur edge cases and transactional tempo/meter UI still require their own implementation/acceptance. No new roadmap unit is marked complete.

## Verification

### Follow-up: dedicated performance-preservation coverage

The dedicated five-case `seam_performance_edit_preservation_tests` target was not included in `seam_tests`. Running it against current code exposed assertions pinned to the former per-note-offset duplication: they expected copied cross-note unit/seam relationships to become unresolved and the original slur ID to be reused. Those expectations contradict the approved common-translation/new-identity repair.

Updated the integration test to require usable complete copied spans/seams, preserved token count and relative timing, placement after the original phrase and a distinct slur ID. Existing assertions still require copied locked phoneme context, vibrato/articulation/hints, note-scoped ownership and accepted-take source offsets, original-note preservation and exact undo/redo. Region-time ownership remains time-scoped and is copied only over the selected phrase's intersecting interval; it is not converted into note ownership.

Added this preservation test file to the core target so future core runs include its five cases. Both the dedicated Debug/Release suite and the rebuilt Release core pass (5 cases at 0.52/0.72 s; 538 core cases at 13.48 s). The separately rebuilt performance-command suite also passes (0.63 s), covering partial spans remaining unresolved, occupied targets, ambiguous mappings and ownership collisions. Diff checks pass. This is a coverage correction and evidence of prior implementation, not an additional completed roadmap unit or new singer-quality evidence.

### Follow-up: slur allocation and mixed disable

`setSelectionSlur(true)` now rejects monotonic group allocation when the region's maximum group is UINT64_MAX, instead of wrapping to zero. Rejection occurs before command execution, preserving project state, revision and undo history. The existing policy remains: enabling extends the first selected existing group, if any; this may intentionally join its unselected existing members. This change does not redefine that policy as selection-isolated grouping.

A mixed-selection regression verifies that disabling converts Legato to Normal, clears selected group membership, preserves Staccato and lyric identities, and leaves unselected notes intact. It also checks exhaustion rejection, exact undo/redo and successful extension of a selected existing group even when an unrelated group has the maximum ID. This addresses U23 edge behavior without claiming its remaining UI/owned-edit acceptance is complete.

Verification: Release core passes 527 cases (11.68 s); all seven focused cases pass in Release/Debug (0.46/0.47 s). Strict builds and diff checks pass.

### Follow-up: copied syllable and slur identity

Duplication now maps each source lyric ID to one new lyric token and each selected source slur group to one new region-local group. Shared-lyric melisma remains shared within the duplicate, but neither lyric nor slur identity is reused from the original. Mapping keys are identities, not displayed text. A partial selection copies the relationships among selected notes only; it does not connect the copy to unselected originals. Exhausted monotonic slur IDs reject without inserting notes.

`AddNoteCommand::LyricMode::ReuseExact` explicitly requires an existing token equal to the captured token. It never creates or removes that token; the first copied note's ordinary Create command owns the token. Composite reverse-order undo removes shared notes before the owning insertion, and redo preserves the captured IDs. Default AddNote behavior still rejects existing lyric IDs. Both paths retain the same validation and pronunciation/override reconciliation.

New regressions verify shared Japanese melisma pronunciation (two three-phone phrases), fresh lyric/slur IDs, no orphan per-note lyrics, exact undo/redo, exhaustion rejection, missing/changed shared-token rejection, default collision rejection and non-owning undo. Existing owned-performance copying still runs after all note insertions. Its complete vocabulary and mixed-selection behavior require further U23 review; this follow-up does not accept the entire unit.

Follow-up verification: rebuilt Release core suite passed all 517 cases (11.22 s); the six-case focused suite passed in Release/Debug (0.46/0.52 s). Strict builds and `git diff --check` passed. Native interactive duplication has not been exercised in this checkpoint.

### Follow-up: phrase-window boundaries for time-scoped performance

Added a command-level regression for time-scoped manual ownership and accepted takes that cross the selected phrase boundary, plus scopes beginning exactly at the phrase end. Only the crossing interval's intersection is translated to the duplicate; the original ranges remain unchanged and the non-intersecting scopes are not copied. The copied accepted selection retains its source-take mapping through the adjusted source offset. Command undo/redo restores exact project snapshots. Release `seam_performance_command_tests` and `seam_performance_edit_preservation_tests` pass (22 and 5 cases respectively; focused CTest 2/2). This closes a phrase-window coverage gap, not all U23 acceptance or Beta readiness.

- Strict Release and Debug `seam_melisma_tests` builds passed.
- Focused four-case suite passed in both configurations (Release 0.45 s; Debug 0.42 s).
- Rebuilt Release core suite passed (11.40 s); `git diff --check` passed.
- Earlier native-fix checkpoint: Release core suite passed 514 cases and Debug Studio built; see execution ledger for subsequent core verification.
- The initial new boundary test failed because normal note insertion rejected the out-of-region fixture; the fixture was corrected as explained above, with no production admission checks bypassed.
- No commit, push, release publication, physical recording or subjective acoustic approval was performed.
