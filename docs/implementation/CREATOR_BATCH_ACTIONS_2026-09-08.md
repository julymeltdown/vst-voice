# Creator batch actions

Status: clear-vibrato, duration cleanup, auto-legato and explicit region-native-dynamics clearing previews/native reviews/macOS menus are implemented locally. Selected-note-only dynamics reset and cross-host/acoustic/large-selection qualification are unfinished. U24 and Full-Scope Beta GO are not accepted.

## Governing requirements

The full-scope plan U24 references creator-workflow plan U4. Its normalization/reset operations are auto-legato, remove-overlap, close-gap, clear-vibrato and clear-dynamics, with target previews and one undo group. It does not authorize automatic normalization of displayed lyric text. Auto-legato must respect staccato and explicit separation; geometry cleanup must preserve positive durations and note identities; expression clearing must preserve unrelated intent and retained edits.

## Remove-overlap and close-gap previews

### Explicit region-native-dynamics clearing

`seam/ui/dynamics_clear_preview.hpp` captures an immutable source and opaque document-generation context for `Clear Region Dynamics Curve…`. The native dynamics curve is continuous and region-owned, not note-owned. Deleting points inside a selected note's span could change interpolation outside that span; this action therefore explicitly targets the **entire active region**, regardless of note selection. It is not a selected-note-only reset and does not close that acceptance question by renaming the requirement.

The review shows region-note/point counts, retained generated-selection counts, paged before-gain values and the native unity default. Its title and Apply label disclose region scope. When generated Dynamics selections exist, a visible warning states that generated dynamics may still apply. Clearing the native curve does not promise unity in the final compiled or audible performance.

Apply uses the canonical `EditPerformanceCommand` through guarded performance publication, producing one undo group. It clears only `VocalRegion::dynamicsAutomation`; other regions, pitch, vibrato, hints, ownership, unit/seam edits, generated takes and accepted selections are preserved. Wrong region, revision/source drift, replaced document generation, cancelled and reused previews reject. Empty native curves disable Apply and model no-ops do not create history. Refresh explicitly recaptures the current region. Admission limits region notes to 10000; capture and command staging remain synchronous, without a worst-case latency qualification.

Model regression compares the whole project before/after with an accepted generated Dynamics take present, then checks exact undo/redo, cancellation, stale generation and no-op behavior. Native regression uses two notes with only one initially selected, seven curve points, retained generated dynamics and a 480×320 canvas; it verifies pagination, selection-independent region scope, stale revision/Refresh, warning text, explicit Apply, one notification, exact preservation/undo and Cancel. The application dispatcher and macOS menu route the explicit command.

Verification: strict Release app/core/focused and Debug native-UI/platform/focused builds pass. The focused suite passes all 20 cases in both configurations; final Release core run passes all 575 cases (12.32 s). Inspected system-font minimum-size capture `/tmp/seam-dynamics-clear.jG7CE6/review.png`: region scope, retained-generated warning, counts, six rows and actions fit without overlap. The new native generated-take fixture initially failed validation because its take range did not cover the selected note; correcting the fixture range restored the pass without weakening validation. Live menu/Apply, audible output, screen-reader use, Windows/embedded workflows and maximum-size latency remain unqualified. Changes local/uncommitted; no U24 or Beta GO acceptance.

### Auto-legato follow-up

The cleanup planner now includes Auto Legato, exposed by the macOS Edit command `Auto Legato Selected Notes…`. The same review surface shows both duration and articulation changes, dependency outcomes, skipped-pair reasons and explicit Apply/Cancel/Refresh.

Eligibility is deliberately specific: both notes must be selected, adjacent and unambiguous in full region order; neither may be staccato; neither may lie in an existing overlap (including overlap from an earlier long note); their slur-group values must match, including both unassigned; and the nonnegative gap must be at most one captured snap grid. A positive gap may be closed only to an on-grid successor start. Already-touching off-grid pairs can receive articulation changes without moving their starts. Long rests, separate group assignments, intervening unselected notes and existing overlaps are preserved/reported rather than silently bridged.

An eligible pair gets Legato articulation at both ends, while only a gapped predecessor's duration extends. Existing group IDs and lyric ownership are never invented or rewritten. A row can have no outgoing successor yet still show `set legato` because it is the endpoint of an eligible incoming pair; changed-note counts include that articulation-only edit.

The private dry run and real publication use one composite of canonical resize and articulation commands. This retains their reconciliation behavior and one user undo group. Shared-lyric continuation changes are included in the dependency preview. No-op plans remain history-neutral.

Tests cover chains, touching pairs, one-grid gaps, long rests, staccato on either side, distinct slur groups, nested overlap, intervening unselected notes, preserved starts/MIDI keys/lyric IDs/group IDs/vibrato settings, one-step undo/redo and no-op behavior. The shared-lyric dependency regression now covers auto-legato's combined geometry/articulation path as well as overlap removal. Native controller and dispatcher tests cover the new mode and command. The system-font render `/tmp/seam-auto-legato.e3L5qe/legato.png` was inspected at 720×520: both duration and endpoint-articulation changes are visible.

Verification: 573 Release core cases pass (13.22 s); 19 focused creator-batch cases pass Release/Debug (0.67/0.77 s). Strict Release app/core and Debug native-UI/platform/focused builds and diff checks pass. Live auto-legato menu/Apply, audible behavior and large-selection latency remain unqualified. Changes local/uncommitted; this does not accept U24 or Beta GO.

### Shared short-window layout follow-up

The shared review/time-map panel previously had fixed 326-pixel height and a minimum top offset of 48, allowing controls to fall below a 320-pixel-tall canvas. Panel height is now capped to the available height with margins. Actions remain 26 pixels high, while six-row spacing contracts as needed; active time-map text input uses the same bounded row height. At ordinary larger sizes the established row/action geometry is retained.

Regression checks panel containment, all six rows and eight time-map actions, pairwise non-overlap and at least 22-pixel row/input height at 480×320, 640×360, 720×520 and 960×640. It also verifies native semantic-node bounds and pointer cancellation of a clear-vibrato review at 480×320 without project mutation. The system-font capture `/tmp/seam-short-review.MatiD4/review.png` was inspected: the panel, target/count text and actions fit the canvas.

Verification: 571 Release core cases pass (12.15 s); strict Release app and Debug native-UI builds and diff checks pass. This is shared geometry, rendered-fixture and controller-input evidence, not full live IME resizing, all content variants or embedded-host minimum-size qualification. Changes local/uncommitted; release acceptance remains open.

### Native cleanup review/menu checkpoint

The macOS Edit menu now exposes `Remove Selected Overlaps…` and `Close Selected Gaps…` through distinct application commands. Both open the shared native review in cleanup mode, with selected/changed counts, captured snap grid, six-row note outcomes, per-key dependency pages, explicit Apply, Cancel and Refresh. Unchanged/skipped notes stay visible with concrete reasons. No-op reviews disable Apply; grid, selection and source changes disable stale previews. Successful Apply uses the canonical preview command, notifies once and rebuilds the piano-roll index.

Mode-specific IDs isolate cleanup actions from lyric/vibrato review and old pages/refreshes. Status/counts are exposed in a native semantic child. The existing modal pointer/keyboard/scroll and embedded adapter guards apply. Synchronous capture/canonical dry-run work remains; this checkpoint does not qualify its worst-case latency.

Controller regression exercises both actions, visible no-successor rows, stale-grid rejection, refresh identity, dependency switching, Apply, no-op review, cancellation, Delete isolation and exact undo. Dispatcher regression checks both menu routes independently. Test-generated system-font screenshots at `/tmp/seam-note-cleanup.GKHWND/overlap.png` and `gap.png` were inspected at 720×520: duration changes, skipped row, counts and controls are separated and visible.

Live verification used the exact Release app, a temporary two-note project and disposable support root under `/tmp/seam-note-cleanup.GKHWND`, paused with nonphysical audio. Both menu entries were observed. Selecting only the predecessor and opening Remove Overlap exposed `1440 -> 1440 ticks / unselected neighbor`, zero changes and disabled Apply; Cancel preserved revision 2. Box-selection automation failed with `noWindowsAvailable`, so the live two-note Apply journey is not claimed. The same process handle was retained and later confirmed exit 0 with zero physical audio frames. A subsequent observation showed a different session/backend; no further actions were taken there.

Verification: 570 Release core cases pass (12.88 s); 17 focused creator-batch cases pass Release/Debug (0.77/0.99 s). Strict Release app/core and Debug native-UI/platform/focused builds and diff checks pass. Live successful Apply, complete small-window behavior, screen-reader use, Windows/embedded interaction and acoustic/large-selection qualification remain open. Changes local/uncommitted.

### Earlier model checkpoints

### Canonical dependency preview follow-up

Cleanup preparation now applies the proposed resizes to a private project copy before publishing the preview. Canonical validation/reconciliation failures therefore reject preparation, not just final Apply. The preview exposes per-key before/after unresolved state for retained phoneme, unit and seam records.

`seam/ui/dependent_edit_review.hpp` centralizes that comparison for cleanup and lyric replacement/distribution reviews. It distinguishes an absent key from a resolved record, orders outcomes by kind/key, rejects duplicate keys on either side, checks cancellation while indexing/publishing and limits each side to 30000 combined records. Cleanup checks its source dependency budget before snapshot capture/staging. Existing replacement outcome type names remain compatibility aliases; the native lyric-review representation is unchanged.

The new geometry regression starts with overlapping legato notes sharing a Japanese lyric. Removing the overlap makes them contiguous and changes melisma continuation; the canonical preview predicts all three existing phone/unit/seam records become unresolved. The live project is unchanged before Apply, actual outcomes exactly match the preview, and undo restores the complete source project. Separate helper tests cover removed/new keys, false-versus-absent state, duplicate source/result keys, cancellation and bounds.

Verification: all 569 Release core cases pass (12.68 s); 17 focused creator-batch cases pass Release/Debug (0.98/0.93 s). Strict Release app/core and Debug native-UI/focused builds and diff checks pass. These are model/command diagnostics; native cleanup review/menu presentation remains to be connected. Snapshot copying and canonical dry-run work are synchronous and not mid-call cancellable, so worst-case preparation latency remains unqualified. No acoustic or release acceptance is claimed.

### Geometry policy and initial evidence

`libs/seam-editor-ui/include/seam/ui/note_cleanup_preview.hpp` adds bounded captured previews for Remove Overlap and Close Gap. Each selected note produces a typed outcome row, including unchanged/skipped notes, and changed rows retain exact before/after durations. Apply uses one canonical `ResizeNotesCommand` through the generation-guarded performance publication path, preserving its existing timing/dependency reconciliation and undo semantics.

The implementation policy is explicit:

- Order the complete region by start tick then note ID; consider only adjacent notes when both are selected. Never jump across an unselected intervening note.
- Never change starts, pitches, identities, lyric assignments or articulation directly.
- Remove Overlap only shortens an overlapping predecessor. Its end snaps downward to the current grid, using nonnegative subtraction/modulo rather than overflow-prone rounding-up arithmetic.
- Close Gap only extends a predecessor to an on-grid successor onset. Off-grid successors are reported as skipped rather than moving their starts or pretending a gap was completely closed.
- Simultaneous-start groups or ambiguous successor groups are skipped. Nonpositive snapped durations, unselected neighbors and the last note are reported explicitly. Close Gap also skips staccato predecessors; it is not an auto-legato/articulation command.

Admission is 1–10000 selected notes in a region of at most 10000 notes, with a positive grid and a valid captured project. Project validation happens before calling `endTick`, so overflowing source end ticks reject. Preview/application capture and recheck selection, region, revision, grid and opaque document/source context. No-op application adds no command; cancellation/reuse reject. Cancellation is checked during planning and immediately before the synchronous canonical commit path.

Tests compare preview Apply with an independently constructed canonical resize command, then check exact undo/redo and no-op history. Cases cover positive gap closure, overlap removal, off-grid successors, simultaneous starts, staccato, unselected neighbors, nonpositive snapped lengths, stale grids, foreign targets, cancellation, copied receipts and overflowing source notes.

Initial verification: 567 Release core cases passed (11.87 s); 15 focused creator-batch cases passed Release/Debug (0.82/0.77 s). The follow-up above adds canonical dependency outcomes. Native menu/review integration and large-selection latency remain open, as do auto-legato and clear-dynamics. Changes local/uncommitted.

## Clear-vibrato implementation evidence

### Native integration checkpoint

The macOS Edit menu now exposes `Clear Selected Vibrato…` through an explicit application command. Its callback opens the shared native review surface in clear-vibrato mode. Six-row pages identify enabled notes that will change, with selected/changed counts, a settings-preservation notice, explicit Clear Vibrato, Cancel and Refresh. A selection/source change disables Apply; Refresh captures a new preview. No enabled vibrato produces a no-op review with Apply disabled.

The existing review input isolation and standalone/embedded adapter guards apply to this mode. Mode-specific semantic IDs distinguish it from lyric review, and status/counts are exposed as an actual child node rather than only root metadata. Successful Apply notifies document change once; cancellation releases the preview without mutation. Snapshot capture remains synchronous, so large-selection responsiveness is not newly qualified.

Controller regression pages seven targets, rejects an old page action and changed selection, refreshes to one note, applies exactly that note, undoes, cancels and checks no-op availability. The application-dispatcher test verifies command routing without document mutation. The system-font raster `/tmp/seam-clear-vibrato.014tBC/review.png` was inspected at 720×520: count, target row and controls are visible without overlap.

Live verification used the exact rebuilt Release app with a test-generated temporary project, paused/nonphysical audio and support root `/tmp/seam-clear-vibrato.014tBC/support`. Menu review showed one selected/enabled vibrato at revision 2. Apply advanced to revision 3; reopening showed zero enabled vibratos and disabled Apply. After Cancel and one Cmd+Z, reopening showed one enabled vibrato at revision 4. Final review cancellation and Discard closed the app with exit 0. No physical audio frames were emitted. The temporary input file's SHA-256 remained `50af0e09103ecd402a08bf714bd9737aaf32bd0cc085c22acf9f1d6e7a6c3df1`.

Latest verification: 565 Release core cases pass (13.29 s), 13 focused creator-batch cases pass Release/Debug (0.63/1.01 s), strict Release app and Debug native-UI/platform builds pass, and diff checks pass. This qualifies the standalone editing/menu/undo route, not acoustic output, full screen-reader use, Windows or interactive embedded-host behavior. Changes local/uncommitted.

### Model contract and earlier evidence

`libs/seam-editor-ui/include/seam/ui/vibrato_clear_preview.hpp` captures a validated immutable project through the existing performance-job context, plus editor revision, active region and selected note IDs. Admission requires 1–10000 selected notes entirely within a region containing at most 10000 notes. Changes are listed in tick/ID order; already-disabled notes are not added to the edit list.

Clear means disabling `NoteVibrato::enabled`, not replacing the complete value with defaults. Period, phase, depth, onset and fades remain saved. Independently authored pitch ownership is not relinquished. This allows subsequent explicit re-enabling without losing the prior vibrato design and avoids erasing manual pitch under the guise of clearing vibrato.

Apply checks selected region/IDs, editor revision and opaque document generation/source context, then calls `executePerformanceResult` with the canonical `EditPerformanceCommand`. Phonetic hints are carried unchanged, and no dynamics/style/ownership edits are requested. Successful application consumes the receipt; a copied preview cannot apply again after the original succeeds, even after undo. No-op previews add no history. Cancelled/applied instances reject reuse; cancellation is checked during preparation and immediately before the synchronous command publication path.

Capture and Apply run on the editor owner thread. Project validation/copy and canonical commit are not mid-call interruptible. No native view callback or background UI access is introduced here. Native menu/review integration, live interaction, acoustic effect verification and 10000-note latency qualification are still open.

## Verification

The new regression compares the complete result project with a source copy whose selected note has only `vibrato.enabled` changed. It includes non-default depth/phase, an unselected enabled vibrato, pitch and dynamics automation, a phonetic hint, manual pitch ownership, unit selection and a seam override. It verifies no pre-Apply mutation, target drift, cancellation, exact undo/redo, no-op revision neutrality, copied-receipt rejection, same-content document replacement and foreign targets.

- Release focused creator-batch suite: 13 cases pass, 2.91 s.
- Debug focused creator-batch suite: 13 cases pass, 4.47 s.
- Rebuilt Release core: 564 cases pass, 14.69 s.
- Strict builds and `git diff --check`: pass.

These are command/preview preservation results, not full batch-action or release qualification. Changes remain local/uncommitted.

## Remaining work

- Qualify the connected clear-vibrato review/menu across remaining hosts, sizes, accessibility and large selections.
- Implement reviewed clear-dynamics with an explicit target scope that preserves curves outside that scope.
- Qualify the connected cleanup reviews/menus/dependency pages across large selections, live Apply, small windows and remaining hosts.
- Qualify the connected auto-legato operation across live hosts, large selections and audible phrase transitions; it remains distinct from Close Gap.
- Qualify large selections, cancellation, native keyboard/pointer/accessibility, IME transitions and embedded/Windows behavior.
