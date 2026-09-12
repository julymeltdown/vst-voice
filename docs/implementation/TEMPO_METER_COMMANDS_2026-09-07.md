# U23 tempo and meter command checkpoint

Status: implemented application commands; native editing and complete U23 acceptance remain open.

## Contract

`EditTempoCommand(tick, optional<double> bpm)` inserts/replaces a tempo event or removes it when the value is absent. `EditMeterCommand(tick, optional<Signature>)` does the same for meter. Both operate on a candidate map and publish only after the existing map validator accepts the change. They do not move notes, change PPQ, or retime saved musical-tick data.

The first successful application captures complete before/after maps. Undo and redo require the current map to equal the expected captured side; out-of-band changes reject instead of being overwritten. EditorSession retains its normal project-level transaction/validation and history-recovery policy. A failed initial edit adds no history entry. Identical-value edits currently remain valid commands and can add an undo entry, matching the session's existing command behavior.

Existing time-map rules remain authoritative: initial events cannot be removed, missing removal targets reject, negative insertion ticks reject, BPM must be finite in `(0, 1000]`, and signatures use numerator 1–32 and denominator 1/2/4/8/16/32. This checkpoint does not claim extreme-domain sample-conversion qualification or new musical restrictions on meter placement.

Both commands declare `ProjectAudio` and `projectWide=true`. Meter is conservatively included because it participates in performance-job context and beat-dependent consumers. EditorSession's existing context generation rejects previously captured worker receipts after either change, including after undo restores the former map. This verifies the application invalidation contract, not a newly wired native playback control.

## Verification scope

The new four-case `seam_tempo_meter_tests` target is also included in the core suite. It covers:

- A sustained 1920-tick note with a tempo event at 960: at 48 kHz, 120→60 BPM produces a 72000-frame endpoint; replacement with 240 BPM produces 36000; removal restores 48000. Inverse sample mapping and unchanged note geometry are checked.
- Exact map undo/redo and irreversible invalidation of old worker receipts.
- Meter insertion/replacement/removal and bar numbering, with tempo unchanged.
- Invalid/nonfinite tempo, unsupported meter, initial/missing removals, unchanged failed-edit state/history, and stale direct undo/redo rejection.

Strict Debug/Release builds passed. The four-case focused suite passed in Release/Debug (0.47/0.44 s); the rebuilt Release core suite passed all 521 cases (11.34 s). `git diff --check` passed. No UI-only tempo-edit workflow, physical listening check, serialization regression specific to these commands, or full U23/Beta GO acceptance is claimed.

## Next integration

### Event selection retention

Refresh and successful removal no longer unconditionally jump to the initial row. Within the same project, the controller captures the selected tick/type, rebuilds the snapshot, and selects the exact matching event. If deleted, selection moves to its sorted successor, or the final remaining event when no successor exists. The page is derived from that selection so it stays visible. Different-project opening starts at the initial row. This supersedes the earlier first-row-refresh limitation below.

The model uses lower_bound on chronological tick/type keys, not old row offsets. Regression selects tick 5760 on a later page, inserts tick 480 ahead of it, refreshes while preserving 5760, removes it and verifies successor 6720; same-tick meter identity and end-of-list fallback are also checked. Snapshot freshness and initial-event deletion guards remain unchanged. Live selection-retention interaction is not yet separately qualified.

Verification: rebuilt Release core passes 533 cases (12.62 s); Debug native UI build and diff checks pass.

### Embedded AppKit text-input parity

`embedded_view_appkit.mm` had the same commit-then-hide ordering as the standalone bridge. It now captures the text, retires the old native field, and only then calls the runtime commit. A native-active flag suppresses duplicate/reentrant didEndEditing commits caused by responder changes. The text-field delegate explicitly handles Enter, Tab/Backtab and Escape; Tab publishes current text before routing the common controller key operation, and Escape cancels rather than committing. NSTextField continues to own standard text selection and IME behavior.

The embedded canvas key map now includes Tab, A, N and R, enabling the panel's traversal/add/refresh routes. This preserves the existing embedded physical-key mapping convention; it does not claim complete international keyboard equivalence.

Verification: strict Release plugin/host/core and Debug embedded-editor builds pass; Release core passes 532 cases (12.01 s). A real Cocoa host smoke run exited 0 with GUI created/visible, screenshot written, nonzero fixture note energy (841.31), 24576 captured frames, offline render accepted, active-load rejection and byte-identical state round-trip (11611 bytes). Its exact fixture identity and result are retained at `/tmp/seam-embedded-bridge.hXHJ3R/summary.json`; these are temporary test artifacts using the existing development fixture, not release singer evidence.

The automated host only opens/pumps/closes its window; it does not type through the new fields. Embedded interactive Enter/Tab/Escape/IME and actual DAW qualification therefore remain open. No new roadmap unit is accepted by this checkpoint.

### Live keyboard bridge repair and verification

Keyboard testing (not accessibility value assignment) found that Cmd+A did not replace the field: typing appended `123.75` to the old `120`. AppKit lacked a select-all command handler and also omitted A from both ASCII and non-Latin key mappings. Added those mappings, a Korean-layout mapping regression, and active-text Cmd+A handling that selects all native text storage and publishes composition state.

Inspection also found native Enter ended text input *after* invoking the client commit. A two-stage client could open its next field during that callback, only to have it immediately closed. Native Enter now retires its current field before the client commit; Escape uses the same ordering before cancellation. This preserves a synchronously opened successor field rather than requiring a timing workaround.

Final live AppKit keyboard check passed: click initial BPM → Cmd+A → type `123.75` → Enter; the event list confirmed exactly 123.75. Add Tempo → Cmd+A/type `1920` → Enter retained an editable BPM field; Cmd+A/type `90.5` → Enter produced the correct tick-1920 row. A subsequent typed meter insertion reached its `7/8` value stage, then Escape cancelled it with project revision still 2 and no new meter row. Close showed the normal unsaved dialog; only the disposable Untitled document was discarded, and the exact app process exited 0 with nonphysical audio and zero frames.

Release standalone build and 532-case core suite pass (12.11 s); Debug native UI build and diff checks pass. The first intermediate build still failed Cmd+A until the missing A mappings were added; only the final run above is positive evidence. This covers numeric keyboard entry in the current AppKit runtime, not general multilingual IME composition, VoiceOver, embedded plugin or physical playback qualification.

### Live discovery: missing visible composition

A Release AppKit run using an isolated temporary support root opened the panel through its semantic action, exposed only panel controls, entered Add Tempo, and advanced from tick 1920 to BPM via semantic value assignment. The accessibility field changed correctly, but the screenshot still showed the static BPM/meter toolbar beneath its focus outline: no editable text was painted. Therefore earlier controller/renderer tests were insufficient to prove visible input usability.

The scene previously found composition bounds only through a note's lyric identity. Time-map input now has explicit shared bounds for native callbacks, accessibility and painting, with a dedicated active flag. Panel input occupies a wide row inside the panel; initial toolbar editing temporarily expands to a bounded 240-pixel field. Its text is painted last over the panel with bounded text drawing. Ordinary lyric input retains its prior paint path.

Regression checks shared callback/scene geometry, actual displayed composition text, cancellation and the full 19-digit int64 tick in the panel, with optional `SEAM_TIME_MAP_INPUT_CAPTURE`. The first disposable app ended at its configured test deadline with exit 0, nonphysical audio and no delivered frames; this does not prove ordinary dirty-close behavior or device playback.

Live fixed-build recheck: the native AppKit screenshot shows the tick field visibly painted inside the panel. Semantic insertion committed tempo 90.5 at tick 1920 (revision 1), refresh exposed it, meter 7/8 was inserted at the same tick (revision 2), and selecting/removing only that meter retained tempo 90.5 (revision 3). Closing the panel restored the normal editor accessibility tree. Closing the test window displayed Save/Discard/Cancel for Untitled; Discard was selected only for this disposable document, and the exact app process exited 0. No user project file was opened or saved. Support/artifact root: `/tmp/seam-time-map-live.Tz3gL0`.

Verification: Release core passes 532 cases (11.74 s); Release standalone and Debug native UI builds pass, with clean diff checks. The inspected deterministic input capture (`input.png` in that temporary root) verifies the full int64 text fits. Physical audio was disabled and delivered zero frames, with BANK_MISSING accurately retained. These live checks used accessibility value assignment, not a full keyboard/IME or VoiceOver session; those boundaries and complete U23/Beta GO acceptance remain open.

### Panel semantic accessibility

The TIME MAP opener now has semantic activation. While open, the controller builds a dedicated bounded accessibility tree rather than retaining background score notes. It exposes visible event rows with tick/type/value, selected state and initial-event protection, all eight actions with availability, and the active tick/BPM/signature input plus cancellation. Row activation selects; the Edit action opens its value. Tab/Shift+Tab traversal is confined to the custom tree; Enter routes focused actions or edits a focused row.

Targets include project identity, document/snapshot revisions, page, selection, input stage and a monotonically increasing interaction identity. Reopening/cancelling a field or advancing from tick to value cannot revive an earlier input target. Semantic actions require a currently materialized enabled node; the shared dispatcher alone does not enforce enabled state. Background and stale actions reject. Input value assignment uses the same composition commit as native text input.

Regression drives semantic open/add/cancel/reopen/tick/value/refresh/close, rejects prior-stage/cancelled/background targets, checks initial/stale action protection and verifies zero virtual score notes plus panel-confined focus. This is semantic/controller coverage, not live VoiceOver or AppKit/embedded qualification.

Verification: rebuilt Release core suite passes 531 cases (11.72 s); Debug native UI builds and diff checks pass. The first full run caught a new opener SetFocus shortcut that bypassed the common focus-state update; removing that shortcut restored the existing all-node focus regression. Disabled panel controls now advertise no focus actions, so traversal skips them.

### Two-stage event insertion

The panel now exposes ADD TEMPO (N) and ADD METER (Shift+N). The first composition accepts a complete nonnegative int64 tick; the second accepts BPM or meter. Header prompts distinguish the stages. No mutation occurs after tick entry, and Escape cancels either stage. Successful final commit remains one canonical command/undo operation and leaves the panel stale pending explicit refresh.

Insertion never silently replaces an existing same-kind event. Both stages verify the original revision and time-map snapshot, and the final stage rechecks target occupancy. Same-tick tempo and meter events are independent and allowed. Native text-overlay teardown is followed by another revision check before advancing to value entry. Map size admission remains the list model's existing 16384-event bound; format/render extreme-domain qualification remains separate.

Regression covers protected initial collisions, tempo insertion, cancellation after tick entry, same-tick meter insertion, stale/occupied targets between stages, invalid decimal/suffixed/negative/overflow ticks and exact int64 parsing. Panel height increases to 326 logical pixels for its third action row. Panel accessibility and live platform tests remain open.

Insertion also rejects at the list's combined event capacity, preserving reopenability. Final verification after this guard: Release core passes 530 cases (11.81 s); Debug native UI builds and diff checks pass. The inspected `/tmp/seam-time-map-insert.253zTp/panel.png` renderer capture shows all eight actions fitting at 720×520. This is not a live window or screen-reader test.

### Visible event-panel follow-up

A `TIME MAP` button in the keyboard-side ruler opens a centered six-row event panel. Click selects, double-click/Enter edits, Up/Down selects across pages, Left/Right pages, Delete removes a selected noninitial event, R refreshes, and Escape closes. Visible buttons expose previous/next/edit/remove/refresh/close. Removal refreshes after its own successful commit; other edits leave an explicit stale warning and require Refresh. Refresh currently selects the first row rather than preserving selection.

The open panel consumes background pointer-down, keyboard and scroll input and rejects background accessibility dispatch/value assignment. During event text composition, Escape cancels the field while retaining the panel; Enter/Tab commits to the captured event. Text editing uses the existing native overlay in the toolbar area. Arbitrary event insertion and panel-specific accessibility are not connected yet; this remains incomplete U23 UI.

Regression opens through the actual button hit area, navigates/edits, observes stale rejection, refreshes/removes, closes and verifies undo. A deterministic 720×520 renderer capture is available through `SEAM_TIME_MAP_CAPTURE`. Early compile errors in new paint/test call syntax were repaired without relaxing warnings-as-errors.

Verification: rebuilt Release core suite passes 529 cases (11.85 s); Debug native UI builds and diff checks pass. The inspected 720×520 capture at `/tmp/seam-time-map-panel.hgvvX2/panel.png` shows the panel rows/actions fitting without overlap. This is temporary renderer evidence, not a live native-window or screen-reader qualification.

### Event-list selection model

`TempoMeterModel::capture` snapshots project identity, editor revision and both time maps, and merges event rows in tick order (tempo before meter at the same tick). It supports six-row pages, explicit index selection and tick/type/value rows. Selection is not a mutation identity: controller actions use the captured event tick and kind. Oversized page indices return empty pages without multiplication overflow; invalid selections preserve the current selection.

The event editor admits at most 16384 combined events and rejects larger snapshots explicitly without touching the project. This is an editor admission bound, not a project-format limit or silent truncation. Snapshot matching checks both revision and map contents, including out-of-band map changes without a revision increment.

`timeMapEvents`, `beginSelectedTimeMapEdit` and `removeSelectedTimeMapEvent` connect the model to native composition/canonical commands. Initial tempo and meter rows are protected. Edits/removal invalidate the old list, requiring explicit recapture before another mutation; undo cannot revive the old revision. The visible panel and refresh/selection-retention UX remain to be wired.

Regression exercises 22 interleaved events over multiple pages, same-tick tempo/meter identity, invalid/oversized indices, selected editing, protected initial removal, meter-only deletion, undo, and stale/out-of-band rejection. This is model/controller integration, not a claim that the event list is visible yet.

Verification: rebuilt Release core suite passes 528 cases (11.87 s); Debug native UI build and diff checks pass.

### Final-render and export integration evidence

A new `test_export_service.cpp` regression drives a native tempo composition commit at tick 960 within a 1920-tick sustained synthetic vowel. At 48 kHz, the Final renderer returns 48000 frames before editing and 72000 after the 120→60 BPM change. Phrase content identity changes, no render diagnostics are emitted, and musical note geometry remains unchanged.

Native initial meter commit to `3/4` preserves this vowel's PCM. Save/reopen retains both exact time maps and rerendering yields identical slowed PCM. Canonical Float32 master/stem export publishes two committed WAVs, each with 72000 frames and exact Final-render samples. Undoing meter and tempo restores the original PCM; redoing tempo restores the slowed PCM.

This is executable native-controller → command → Final renderer → project codec → export evidence. It does not exercise window input, background scheduling races, device playback, live host transport, or quality of a production singer. These remain separate acceptance obligations.

Verification: rebuilt Release core suite passes 526 cases (11.62 s); rebuilt Debug export suite passes 20 cases (8.70 s), including its CLI integration paths. Diff checks pass.

### Native meter field follow-up

The toolbar's existing BPM rectangle is split into separate tempo and meter hit/semantic bounds, retaining its outer width and leaving surrounding controls in place. The meter is drawn as a signature such as `4/4`; initial meter supports click, semantic activation and value editing through native composition. The tempo label is numeric in the smaller subfield; its accessible name still identifies BPM. Visual density/readability needs live qualification.

`beginMeterEdit(tick)` captures revision, tick and the meter operation kind. The shared composition lifecycle handles cancellation, stale context and replacement by other text workflows. Tempo/meter accessibility assignments cannot retarget each other's pending edits. Parsing accepts complete integer `numerator/denominator` text with bounded length, validates before uint8 narrowing, and delegates supported signatures to MeterMap. Commit uses the existing undoable meter command; malformed input closes without publishing a change.

Regression covers rejected signatures, `7/8` application, unchanged tempo, undo to `4/4`, nonzero meter-event commit, cross-control rejection, cancellation, and nonoverlapping subfield geometry at 640/720/1000/1440 widths. An initial test compile used a wrong layout type name; it was corrected to `EditorSceneLayout` without changing production compiler settings. Event-list selection/removal and live interaction/playback qualification remain open.

Meter-field verification: rebuilt Release core suite passes all 525 cases (11.46 s); Debug native UI library build and diff checks pass. GUI readability and platform interaction are not yet verified live.

### Exact tempo text and event-bound input

`tempo_meter_model.hpp` now centralizes shortest-round-trip double formatting and bounded strict BPM parsing. The composition prefill and semantic value no longer truncate to six fractional digits. An unchanged high-precision BPM survives opening/committing exactly (although the existing command framework still records an unchanged-value command).

`beginTempoEdit(tick)` captures the target tick together with the revision. It can prepare insertion/replacement at an explicit nonzero tick using the effective BPM there as its starting value. Commit updates that captured tick, and the initial-toolbar accessibility setter rejects while a different event is being edited. Negative ticks reject before opening. The existing toolbar still targets tick zero; a visible event selector remains to be implemented.

Regression checks exact decimal round trips, invalid text, unchanged-field preservation, nonzero insertion, initial-toolbar retarget rejection while preserving composition, and event undo/redo. This is event-context groundwork, not completion of the event-list or meter UI.

Verification: rebuilt Release core suite passes all 524 cases (11.85 s); Debug native UI build and `git diff --check` pass. Live interaction remains unverified.

### Initial-tempo text input follow-up

The existing BPM toolbar bounds now open the native text-input overlay on click. The semantic node is an editable text field with activation/edit actions and a decimal value; accessibility value assignment uses the same commit path. This explicitly edits the initial tick-zero tempo, not an event inferred from the playhead.

The edit captures the editor revision before opening. Commit requires complete finite numeric text (maximum 64 UTF-8 bytes) and uses the guarded native command. Escape cancels; Tab commits without navigating lyrics. Invalid or stale input closes without changing tempo. Beginning another lyric/rename/batch composition clears the tempo target, preventing a later lyric commit from changing tempo. Missing native text-input support rejects activation explicitly.

Regression coverage exercises decimal commits, cancellation, accessibility assignment, malformed text, stale document context and replacement by lyric input. Event-list insertion/removal, meter UI, keyboard traversal qualification, live AppKit/embedded interaction and actual playback/export remain open. This is not full U23 acceptance.

Input follow-up verification: current rebuilt Release core suite passes 523 cases (11.67 s); Debug native UI builds and diff checks pass. An initial run used the pre-adjustment test binary with an incorrect one-close-per-open assertion for overlay replacement; rebuilding the corrected lifecycle assertion passed. No live GUI qualification is claimed.

### Native dispatch follow-up

`NativeEditorController::editTempo` and `editMeter` now accept an expected editor revision, target tick and optional event value. They reject stale revisions and edits during text composition, pointer dragging or phoneme review. Successful edits use the canonical application commands and the existing `markDocumentChanged`/repaint path; invalid requests do not dirty or notify the host. Standalone and embedded host callbacks route document changes to their existing authoring runtime handlers.

The native regression checks success-only notification counts, project-wide impact, stale selection rejection, invalid BPM, preservation of active lyric composition, removal and undo. The toolbar remains read-only: accessible editable semantics must wait for the complete input/commit/cancel flow. These methods are native integration entrypoints, not a claim that users can already edit the maps through visible controls.

Follow-up verification: rebuilt Release core suite passes all 522 cases (11.55 s); Debug native UI library builds and `git diff --check` passes. Source inspection confirms the runtime change handler cancels seam preview and requests a preview with the command's impact. Actual new-control playback scheduling remains to be exercised after visible input integration.

Connect these commands through a native tempo/meter model and existing authoring dispatch, with explicit selected-event context, integer tick validation, accessible insert/update/remove controls, initial-event protection, and cancelable edits. Verify actual render scheduling, timeline refresh, save/reopen and sustained-note playback/export. Continue mixed-selection/owned-performance-copy and slur edge-case qualification alongside this work.
